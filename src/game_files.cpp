#include "game_files.h"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

#include "sha256.h"

namespace mua {
namespace {

constexpr const char* kExecutableName = "default.xex";
constexpr size_t kHeaderBytes = 64 * 1024;
constexpr size_t kHashChunkBytes = 1u << 20;

std::optional<std::vector<uint8_t>> ReadHeader(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::vector<uint8_t> bytes(kHeaderBytes);
  in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
  if (in.bad()) {
    return std::nullopt;
  }
  bytes.resize(size_t(in.gcount()));
  return bytes;
}

const GameProfile* FindProfile(std::span<const GameProfile> profiles, const XexIdentity& identity) {
  const auto match = std::ranges::find_if(profiles, [&](const GameProfile& profile) {
    return profile.title_id == identity.title_id && profile.media_id == identity.media_id &&
           profile.pe_name == identity.pe_name;
  });
  return match == profiles.end() ? nullptr : &*match;
}

bool IsKnownTitle(std::span<const GameProfile> profiles, uint32_t title_id) {
  return std::ranges::any_of(profiles, [&](const GameProfile& profile) { return profile.title_id == title_id; });
}

std::string FirstMissingCompanion(const std::filesystem::path& game_dir, const GameProfile& profile) {
  for (const CompanionFile& file : profile.companion_files) {
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(game_dir / std::filesystem::path(file.path), ec);
    if (ec || size != file.size) {
      return std::string(file.path);
    }
  }
  return {};
}

}  // namespace

std::string Sha256Hex(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return {};
  }
  Sha256 hash;
  std::vector<char> chunk(kHashChunkBytes);
  while (in) {
    in.read(chunk.data(), std::streamsize(chunk.size()));
    hash.Update(chunk.data(), size_t(in.gcount()));
  }
  if (!in.eof()) {
    return {};
  }
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string hex;
  for (const uint8_t byte : hash.Finish()) {
    hex.push_back(kDigits[byte >> 4]);
    hex.push_back(kDigits[byte & 0xF]);
  }
  return hex;
}

GameCheck CheckGameFiles(const std::filesystem::path& game_dir, std::span<const GameProfile> profiles) {
  GameCheck check;
  if (game_dir.empty()) {
    return check;
  }
  std::error_code ec;
  const std::filesystem::path xex = game_dir / kExecutableName;
  if (!std::filesystem::is_regular_file(xex, ec)) {
    return check;
  }
  check.xex_size = std::filesystem::file_size(xex, ec);
  const auto header = ec ? std::nullopt : ReadHeader(xex);
  if (!header) {
    check.state = GameFileState::kReadError;
    return check;
  }
  check.identity = ParseXexIdentity(*header);
  if (!check.identity) {
    check.state = GameFileState::kUnrecognized;
    return check;
  }
  check.profile = FindProfile(profiles, *check.identity);
  if (!check.profile) {
    check.state = IsKnownTitle(profiles, check.identity->title_id) ? GameFileState::kDifferentVersion
                                                                    : GameFileState::kUnrecognized;
    return check;
  }
  if (check.xex_size != check.profile->xex_size) {
    check.state = GameFileState::kDifferentVersion;
    return check;
  }
  check.xex_sha256 = Sha256Hex(xex);
  if (check.xex_sha256.empty()) {
    check.state = GameFileState::kReadError;
    return check;
  }
  if (check.xex_sha256 != check.profile->xex_sha256) {
    check.state = GameFileState::kDifferentVersion;
    return check;
  }
  check.missing_file = FirstMissingCompanion(game_dir, *check.profile);
  check.state = check.missing_file.empty() ? GameFileState::kValid : GameFileState::kIncomplete;
  return check;
}

}  // namespace mua
