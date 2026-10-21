#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "game_profile.h"
#include "path_utf8.h"
#include "xex_identity.h"

namespace mua {

enum class GameFileState {
  kMissing,
  kReadError,
  kUnrecognized,
  kDifferentVersion,
  kIncomplete,
  kValid,
};

struct GameCheck {
  GameFileState state = GameFileState::kMissing;
  const GameProfile* profile = nullptr;  // set once title ID and media ID match a profile
  std::optional<XexIdentity> identity;
  uint64_t xex_size = 0;
  std::string xex_sha256;
  std::string missing_file;  // set for kIncomplete
};

// The one readiness check: the launcher, Play and the startup decision all use it.
GameCheck CheckGameFiles(const std::filesystem::path& game_dir,
                         std::span<const GameProfile> profiles = SupportedProfiles());

// Upper-case hex SHA-256 of a file, empty when it cannot be read.
std::string Sha256Hex(const std::filesystem::path& file);

}  // namespace mua
