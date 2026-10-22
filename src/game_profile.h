#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace mua {

struct CompanionFile {
  std::string_view path;
  uint64_t size;
};

struct GameProfile {
  std::string_view id;
  std::string_view display_name;
  uint32_t title_id;
  uint32_t media_id;
  std::string_view pe_name;
  uint64_t xex_size;
  std::string_view xex_sha256;  // upper-case hex; the authoritative fingerprint of the game's binary
  std::span<const CompanionFile> companion_files;
  uint64_t install_bytes = 0;  // size of the extracted disc, for the free-space check; 0 skips it
};

// Every disc build the launcher can identify. A profile says what the files are, not that the runtime can start them.
std::span<const GameProfile> KnownProfiles();

const GameProfile* ProfileById(std::string_view id);

}  // namespace mua
