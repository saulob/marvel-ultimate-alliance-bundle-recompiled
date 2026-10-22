#include "game_install.h"

#include <chrono>
#include <format>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#include "path_utf8.h"

namespace mua {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kSpaceMargin = 64ull << 20;
constexpr int kRenameAttempts = 10;

// The antivirus scanner can hold a freshly written folder for a moment.
bool RenameWithRetry(const fs::path& from, const fs::path& to) {
  for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }
  return false;
}

// An existing folder is only replaced when it is empty or already holds a game.
bool IsReplaceable(const fs::path& dir) {
  std::error_code ec;
  return fs::is_directory(dir, ec) && (fs::is_empty(dir, ec) || fs::exists(dir / "default.xex", ec));
}

class StagingGuard {
 public:
  explicit StagingGuard(fs::path dir) : dir_(std::move(dir)) {}
  ~StagingGuard() {
    if (armed_) {
      std::error_code ec;
      fs::remove_all(dir_, ec);
    }
  }
  void Release() { armed_ = false; }

 private:
  fs::path dir_;
  bool armed_ = true;
};

InstallResult Failure(InstallStatus status, std::string detail = {}) {
  InstallResult result;
  result.status = status;
  result.detail = std::move(detail);
  return result;
}

void Measure(const fs::path& root, InstallResult& result) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    std::error_code entry_ec;
    if (it->is_regular_file(entry_ec)) {
      ++result.files;
      result.bytes += it->file_size(entry_ec);
    }
  }
}

}  // namespace

fs::path InstallFolder(const fs::path& games_root, const GameDefinition& game) {
  return games_root / std::string(game.key);
}

InstallResult InstallFromDisc(const GameDefinition& game, const fs::path& disc, const fs::path& games_root,
                              const InstallServices& services, const InstallHooks& hooks) {
  const auto phase = [&](const char* text) {
    if (hooks.phase) {
      hooks.phase(text);
    }
  };
  const auto cancelled = [&] { return hooks.cancelled && hooks.cancelled(); };

  std::error_code ec;
  fs::path parent = fs::absolute(games_root, ec).lexically_normal();
  if (!parent.has_filename()) {
    parent = parent.parent_path();
  }
  if (games_root.empty() || ec || parent.empty()) {
    return Failure(InstallStatus::kNoGameFolder);
  }

  phase("Reading disc...");
  const std::optional<DiscIdentity> identity =
      services.read_identity ? services.read_identity(disc) : std::nullopt;
  if (!identity) {
    return Failure(InstallStatus::kNotDisc, Utf8(disc.filename()));
  }
  const GameDefinition* disc_game = GameForDisc(*identity);
  const std::string ids = std::format("title {:08X}, media {:08X}", identity->title_id, identity->media_id);
  if (!disc_game) {
    return Failure(InstallStatus::kUnknownDisc, ids);
  }
  if (disc_game != &game) {
    InstallResult result = Failure(InstallStatus::kWrongGame, ids);
    result.disc_game = disc_game;
    return result;
  }

  const fs::path target = InstallFolder(parent, game);
  const fs::path staging = parent / (std::string(game.key) + ".staging");
  const fs::path backup = parent / (std::string(game.key) + ".backup");

  fs::create_directories(parent, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + Utf8(parent) + ": " + ec.message());
  }
  if (!fs::exists(target, ec) && fs::exists(backup, ec)) {
    RenameWithRetry(backup, target);
  }
  if (fs::exists(target, ec) && !IsReplaceable(target)) {
    return Failure(InstallStatus::kForeignFolder, Utf8(target));
  }
  fs::remove_all(staging, ec);
  if (ec || fs::exists(staging, ec)) {
    return Failure(InstallStatus::kWriteFailed, "cannot clear " + Utf8(staging));
  }
  const uint64_t required = ProfileOf(game).install_bytes;
  const fs::space_info space = fs::space(parent, ec);
  if (!ec && required > 0 && space.available < required + kSpaceMargin) {
    return Failure(InstallStatus::kNoSpace,
                   std::format("needs {} bytes, {} available", required, uint64_t(space.available)));
  }
  fs::create_directories(staging, ec);
  if (ec) {
    return Failure(InstallStatus::kWriteFailed, "cannot create " + Utf8(staging) + ": " + ec.message());
  }
  StagingGuard guard(staging);

  phase("Extracting game files...");
  const std::string error = services.extract ? services.extract(disc, staging) : "no extractor";
  if (cancelled()) {
    return Failure(InstallStatus::kCancelled);
  }
  if (!error.empty()) {
    return Failure(InstallStatus::kExtractFailed, error);
  }

  phase("Verifying installation...");
  const GameCheck check = services.check ? services.check(game, staging) : CheckGame(game, staging);
  switch (check.state) {
    case GameFileState::kValid:
      break;
    case GameFileState::kDifferentVersion:
    case GameFileState::kUnrecognized:
      return Failure(InstallStatus::kWrongVersion, ids);
    case GameFileState::kMissing:
    case GameFileState::kReadError:
    case GameFileState::kIncomplete:
      return Failure(InstallStatus::kIncomplete, check.missing_file);
  }
  if (cancelled()) {
    return Failure(InstallStatus::kCancelled);
  }

  InstallResult result;
  Measure(staging, result);

  phase("Finishing...");
  const bool replacing = fs::exists(target, ec);
  if (replacing) {
    fs::remove_all(backup, ec);
    if (!RenameWithRetry(target, backup)) {
      return Failure(InstallStatus::kWriteFailed, "cannot move aside " + Utf8(target));
    }
  }
  if (!RenameWithRetry(staging, target)) {
    if (replacing) {
      RenameWithRetry(backup, target);
    }
    return Failure(InstallStatus::kWriteFailed, "cannot activate " + Utf8(target));
  }
  guard.Release();
  if (replacing) {
    fs::remove_all(backup, ec);
  }
  result.disc_game = disc_game;
  return result;
}

InstallMessage DescribeInstall(const InstallResult& result, const GameDefinition& selected) {
  switch (result.status) {
    case InstallStatus::kInstalled:
      return {"Game files installed", std::string(selected.display_name) + " is ready."};
    case InstallStatus::kCancelled:
      return {"Installation cancelled", "The game files that were installed before are unchanged."};
    case InstallStatus::kNoGameFolder:
      return {"No game folder", "The port has no folder to install the game into."};
    case InstallStatus::kNotDisc:
      return {"Not a disc image", "That file is not an Xbox 360 disc image."};
    case InstallStatus::kUnknownDisc:
      return {"Unsupported disc", "This disc is not a Marvel Ultimate Alliance game this port supports."};
    case InstallStatus::kWrongGame:
      return {"Wrong game disc", std::format("This disc is {}, but {} is selected.",
                                             result.disc_game ? result.disc_game->display_name : "another game",
                                             selected.display_name)};
    case InstallStatus::kForeignFolder:
      return {"Game folder in use", "The game folder holds other files. Move or remove them, then try again."};
    case InstallStatus::kNoSpace:
      return {"Not enough disk space", "Free some space next to the program and try again."};
    case InstallStatus::kExtractFailed:
      return {"Could not extract the game files", "Check that the disc image is complete and the disk is not full."};
    case InstallStatus::kWrongVersion:
      return {"Different version of the game", "The extracted files do not match the supported build."};
    case InstallStatus::kIncomplete:
      return {"Installation incomplete", "Some game files are missing after extraction."};
    case InstallStatus::kWriteFailed:
      return {"Could not write the game files", "Check that the folder can be written to and try again."};
  }
  return {"Installation failed", ""};
}

}  // namespace mua
