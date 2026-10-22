#include "game_library.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "path_utf8.h"

namespace mua {
namespace {

namespace fs = std::filesystem;

bool IsDiscImage(const fs::path& path) {
  std::string extension = Utf8(path.extension());
  std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return extension == ".iso";
}

}  // namespace

const GameDefinition* GameForDisc(const DiscIdentity& identity) {
  const auto games = AllGames();
  const auto match = std::ranges::find_if(games, [&](const GameDefinition& game) {
    const GameProfile& profile = ProfileOf(game);
    return profile.title_id == identity.title_id && profile.media_id == identity.media_id;
  });
  return match == games.end() ? nullptr : &*match;
}

GameCheck CheckGame(const GameDefinition& game, const fs::path& dir) {
  return CheckGameFiles(dir, std::span<const GameProfile>(&ProfileOf(game), 1));
}

GameAvailability AvailabilityOf(const GameState& state, std::string_view runtime_blocker) {
  switch (state.check.state) {
    case GameFileState::kValid:
      return state.game->runtime_supported && runtime_blocker.empty() ? GameAvailability::kReady
                                                                      : GameAvailability::kRuntimeUnavailable;
    case GameFileState::kDifferentVersion:
    case GameFileState::kUnrecognized:
      return GameAvailability::kUnsupportedFiles;
    case GameFileState::kIncomplete:
      return GameAvailability::kIncomplete;
    case GameFileState::kReadError:
      return GameAvailability::kUnreadable;
    case GameFileState::kMissing:
      break;
  }
  return GameAvailability::kNotInstalled;
}

bool CanPlay(const GameState& state, std::string_view runtime_blocker) {
  return AvailabilityOf(state, runtime_blocker) == GameAvailability::kReady;
}

std::string SelectedGameLogText(const GameState& state, std::string_view runtime_blocker) {
  const std::string_view profile = state.check.profile ? state.check.profile->id : std::string_view("none");
  return std::format("Selected game: {} | profile={} | installed={} | source={} | supported={}",
                     state.game->display_name, profile, state.check.state == GameFileState::kValid ? "yes" : "no",
                     state.source_package.empty() ? "no" : "yes", CanPlay(state, runtime_blocker) ? "yes" : "no");
}

GameLibrary::GameLibrary(fs::path games_root, fs::path explicit_root, DiscReader disc_reader, GameChecker checker)
    : games_root_(std::move(games_root)),
      explicit_root_(std::move(explicit_root)),
      disc_reader_(std::move(disc_reader)),
      checker_(std::move(checker)) {
  if (!explicit_root_.empty()) {
    const GameCheck check = CheckGameFiles(explicit_root_);
    if (const GameDefinition* owner = check.profile ? GameForProfile(*check.profile) : nullptr) {
      explicit_game_ = owner->id;
    }
  }
  for (const GameDefinition& game : AllGames()) {
    states_[size_t(game.id)].game = &game;
    RefreshFiles(game.id);
  }
  RescanPackages();
}

GameCheck GameLibrary::Check(const GameDefinition& game, const fs::path& dir) const {
  return checker_ ? checker_(game, dir) : CheckGame(game, dir);
}

// The first folder holding a valid install wins; otherwise the first with files of another build, so the launcher can
// say what is wrong with them. A game without files keeps the folder an install would use.
void GameLibrary::RefreshFiles(GameId id) {
  const GameDefinition& game = Game(id);
  const fs::path canonical = games_root_ / std::string(game.key);
  std::vector<fs::path> candidates;
  if (explicit_game_ == id) {
    candidates.push_back(explicit_root_);
  }
  candidates.push_back(canonical);
  if (!game.legacy_folder.empty()) {
    candidates.push_back(games_root_ / std::string(game.legacy_folder));
  }

  GameState& state = states_[size_t(id)];
  GameState resolved;
  resolved.game = &game;
  resolved.data_dir = canonical;
  resolved.source_package = std::move(state.source_package);
  bool reported = false;
  for (const fs::path& dir : candidates) {
    GameCheck check = Check(game, dir);
    if (check.state == GameFileState::kValid) {
      resolved.data_dir = dir;
      resolved.check = std::move(check);
      break;
    }
    if (!reported && check.state != GameFileState::kMissing) {
      reported = true;
      resolved.data_dir = dir;
      resolved.check = std::move(check);
    }
  }
  state = std::move(resolved);
}

void GameLibrary::RescanPackages() {
  for (GameState& state : states_) {
    state.source_package.clear();
  }
  std::vector<fs::path> images;
  std::error_code ec;
  for (fs::directory_iterator it(games_root_, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
    std::error_code entry_ec;
    if (it->is_regular_file(entry_ec) && IsDiscImage(it->path())) {
      images.push_back(it->path());
    }
  }
  std::ranges::sort(images);
  for (const fs::path& image : images) {
    const std::optional<DiscIdentity> identity = disc_reader_ ? disc_reader_(image) : std::nullopt;
    const GameDefinition* game = identity ? GameForDisc(*identity) : nullptr;
    if (game && states_[size_t(game->id)].source_package.empty()) {
      states_[size_t(game->id)].source_package = image;
    }
  }
}

const GameState* BootableGame(const GameLibrary& library, GameId selected, std::string_view runtime_blocker) {
  const GameState& state = library.State(selected);
  return CanPlay(state, runtime_blocker) ? &state : nullptr;
}

}  // namespace mua
