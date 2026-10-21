#include "xex_identity.h"

#include <cstring>
#include <string_view>
#include <utility>

namespace mua {
namespace {

constexpr size_t kOptionalHeaderCountOffset = 0x14;
constexpr size_t kOptionalHeadersOffset = 0x18;
constexpr uint32_t kExecutionInfoId = 0x00040006;
constexpr uint32_t kOriginalPeNameId = 0x000183FF;
constexpr size_t kExecutionInfoSize = 0x14;

bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(uint32_t)) {
    return false;
  }
  value = uint32_t(bytes[offset]) << 24 | uint32_t(bytes[offset + 1]) << 16 | uint32_t(bytes[offset + 2]) << 8 |
          uint32_t(bytes[offset + 3]);
  return true;
}

std::string ReadPeName(std::span<const uint8_t> bytes, size_t offset) {
  uint32_t size = 0;
  if (!ReadU32(bytes, offset, size) || size < sizeof(uint32_t) || size > bytes.size() - offset) {
    return {};
  }
  const std::string_view text(reinterpret_cast<const char*>(bytes.data() + offset + sizeof(uint32_t)),
                              size - sizeof(uint32_t));
  return std::string(text.substr(0, text.find('\0')));
}

}  // namespace

std::optional<XexIdentity> ParseXexIdentity(std::span<const uint8_t> header) {
  uint32_t count = 0;
  if (header.size() < kOptionalHeadersOffset || std::memcmp(header.data(), "XEX2", 4) != 0 ||
      !ReadU32(header, kOptionalHeaderCountOffset, count)) {
    return std::nullopt;
  }
  std::optional<XexIdentity> identity;
  std::string pe_name;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t id = 0;
    uint32_t value = 0;
    const size_t entry = kOptionalHeadersOffset + size_t(i) * 8;
    if (!ReadU32(header, entry, id) || !ReadU32(header, entry + 4, value)) {
      return std::nullopt;
    }
    if (id == kExecutionInfoId && value <= header.size() && header.size() - value >= kExecutionInfoSize) {
      XexIdentity parsed;
      uint32_t discs = 0;
      ReadU32(header, value, parsed.media_id);
      ReadU32(header, value + 4, parsed.version);
      ReadU32(header, value + 8, parsed.base_version);
      ReadU32(header, value + 12, parsed.title_id);
      ReadU32(header, value + 16, discs);
      parsed.disc_number = uint8_t(discs >> 8);
      parsed.disc_count = uint8_t(discs);
      identity = parsed;
    } else if (id == kOriginalPeNameId) {
      pe_name = ReadPeName(header, value);
    }
  }
  if (identity) {
    identity->pe_name = std::move(pe_name);
  }
  return identity;
}

}  // namespace mua
