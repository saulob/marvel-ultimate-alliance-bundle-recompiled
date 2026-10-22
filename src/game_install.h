#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "game_library.h"

namespace mua {

enum class InstallStatus {
  kInstalled,
  kCancelled,
  kNoGameFolder,
  kNotDisc,
  kUnknownDisc,
  kWrongGame,
  kForeignFolder,
  kNoSpace,
  kExtractFailed,
  kWrongVersion,
  kIncomplete,
  kWriteFailed,
};

struct InstallResult {
  InstallStatus status = InstallStatus::kInstalled;
  std::string detail;                         // for the log, not the UI
  const GameDefinition* disc_game = nullptr;  // the game the disc belongs to, when it was recognized
  uint64_t bytes = 0;
  size_t files = 0;
  bool ok() const { return status == InstallStatus::kInstalled; }
};

struct InstallHooks {
  std::function<void(const char* phase)> phase;
  std::function<bool()> cancelled;
};

// Copies the disc's files into `destination`; empty on success, else a message.
using DiscExtractor =
    std::function<std::string(const std::filesystem::path& disc, const std::filesystem::path& destination)>;

struct InstallServices {
  DiscReader read_identity;
  DiscExtractor extract;
  GameChecker check;  // default: CheckGame
};

// Where an install of the game goes, whichever folder its files are in now.
std::filesystem::path InstallFolder(const std::filesystem::path& games_root, const GameDefinition& game);

// Extracts the disc into a staging folder next to the target, checks it against the game's profile and only then
// swaps it in. On any failure or cancellation the previous install is left as it was.
InstallResult InstallFromDisc(const GameDefinition& game, const std::filesystem::path& disc,
                              const std::filesystem::path& games_root, const InstallServices& services,
                              const InstallHooks& hooks = {});

struct InstallMessage {
  std::string title;
  std::string text;
};

InstallMessage DescribeInstall(const InstallResult& result, const GameDefinition& selected);

}  // namespace mua
