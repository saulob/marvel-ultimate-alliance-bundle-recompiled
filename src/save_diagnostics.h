#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace mua {

constexpr uint32_t kTitleSettingMask = 0x3F00;
constexpr uint32_t kProfileHeaderSize = 8;
constexpr uint32_t kProfileSettingSize = 0x28;
constexpr uint32_t kProfileMaxSettings = 32;
constexpr uint8_t kProfileTypeBinary = 6;

inline uint32_t LoadBE32(const uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

inline bool IsTitleSetting(uint32_t setting_id) {
  return (setting_id & kTitleSettingMask) == kTitleSettingMask;
}

struct ProfileSettingView {
  uint32_t source = 0;
  uint32_t id = 0;
  uint8_t type = 0;
  uint32_t size = 0;
  bool title_specific = false;
};

// Layout of the buffer XamUserReadProfileSettings fills: count, pointer, then 0x28-byte entries.
inline bool ReadProfileSetting(const uint8_t* base, uint32_t buffer, uint32_t index, ProfileSettingView& out) {
  const uint32_t count = LoadBE32(base, buffer);
  const uint32_t settings = LoadBE32(base, buffer + 4);
  if (count > kProfileMaxSettings || index >= count || settings != buffer + kProfileHeaderSize) return false;
  const uint32_t entry = settings + index * kProfileSettingSize;
  out.source = LoadBE32(base, entry);
  out.id = LoadBE32(base, entry + 16);
  out.type = base[entry + 24];
  out.size = LoadBE32(base, entry + 32);
  out.title_specific = IsTitleSetting(out.id);
  return true;
}

inline std::string ContentFileName(const uint8_t* base, uint32_t content_data) {
  constexpr uint32_t kFileNameOffset = 264;
  constexpr uint32_t kFileNameSize = 42;
  const char* name = reinterpret_cast<const char*>(base + content_data + kFileNameOffset);
  return std::string(name, strnlen(name, kFileNameSize));
}

}  // namespace mua
