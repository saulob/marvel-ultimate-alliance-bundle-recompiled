#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

// MUA 2's main menu is an Anark scene driven by Lua. The Xbox LIVE tab takes its label from the name of its state in
// the compiled MainMenu\Init.lua, and its subtitle is a string of the scene file; both are rewritten in place, with the
// same length, once the pak reader has inflated them. Activating the tab calls the Lua global processSignin, which the
// runtime answers by ending the title.
namespace mua::main_menu_mua2 {

struct TextPatch {
  std::string_view find;
  std::string_view replace;
};

// Lua strings are a length that counts the terminator; the scene's strings are a length alone.
inline constexpr TextPatch kLabelPatch = {std::string_view("\x04\x00\x00\x00\x0aXbox LIVE\x00", 15),
                                          std::string_view("\x04\x00\x00\x00\x0aQuit Game\x00", 15)};
inline constexpr TextPatch kSubtitlePatch = {std::string_view("\x00\x00\x00\x0bPlay Online", 15),
                                             std::string_view("\x00\x00\x00\x0b" "Exit Game  ", 15)};
inline constexpr TextPatch kPatches[] = {kLabelPatch, kSubtitlePatch};

inline constexpr uint32_t kStreamNextOutOffset = 12;
inline constexpr uint32_t kMaxScannedBytes = 0x20000;
inline constexpr uint32_t kOverlapBytes = 14;

inline uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

// Replaces every occurrence of the patch's text in [begin, end) of guest memory; returns how many.
inline uint32_t ApplyPatch(uint8_t* base, uint32_t begin, uint32_t end, const TextPatch& patch) {
  if (patch.find.size() != patch.replace.size() || end <= begin) {
    return 0;
  }
  uint8_t* first = base + begin;
  uint8_t* last = base + end;
  uint32_t replaced = 0;
  for (uint8_t* at = std::search(first, last, patch.find.begin(), patch.find.end()); at != last;
       at = std::search(at + patch.find.size(), last, patch.find.begin(), patch.find.end())) {
    std::memcpy(at, patch.replace.data(), patch.replace.size());
    ++replaced;
  }
  return replaced;
}

// Patches the bytes an inflate call has just written to [begin, end); the span reaches back far enough to catch a
// string split between two calls. Blocks larger than a pak chunk are not menu data and are left alone.
inline uint32_t PatchMenuText(uint8_t* base, uint32_t begin, uint32_t end) {
  if (end <= begin || end - begin > kMaxScannedBytes) {
    return 0;
  }
  const uint32_t from = begin > kOverlapBytes ? begin - kOverlapBytes : begin;
  uint32_t replaced = 0;
  for (const TextPatch& patch : kPatches) {
    replaced += ApplyPatch(base, from, end, patch);
  }
  return replaced;
}

}  // namespace mua::main_menu_mua2
