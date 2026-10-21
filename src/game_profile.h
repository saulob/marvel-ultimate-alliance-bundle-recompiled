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
  std::string_view xex_sha256;  // upper-case hex; the authoritative fingerprint of the recompiled binary
  std::span<const CompanionFile> companion_files;
};

std::span<const GameProfile> SupportedProfiles();

// The profile the launcher presents when nothing has been detected.
const GameProfile& DefaultProfile();

}  // namespace mua
