#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

// The main menu's item list as MUA1/Gold parse it: sibling XML nodes whose attributes are (name, value) pointer
// pairs. Each text item is bound by name to one scene slot, so dropping the Xbox Live item moves the content of the
// items below it up one slot and the freed last slot carries Exit Game.
namespace mua::main_menu {

inline constexpr std::string_view kLiveItem = "option02_text";
inline constexpr std::string_view kOptionsItem = "option03_text";
inline constexpr std::string_view kReviewItem = "option04_text";
inline constexpr std::string_view kExitText = "EXIT GAME";
inline constexpr std::string_view kExitCommand = "mua_exit_game";

inline constexpr std::string_view kNameAttribute = "name";
inline constexpr std::string_view kTextAttribute = "text";
inline constexpr std::string_view kCommandAttribute = "usecmd";
inline constexpr std::string_view kUpAttribute = "up";
inline constexpr std::string_view kDownAttribute = "down";

inline constexpr uint32_t kNodeNextOffset = 4;
inline constexpr uint32_t kNodeAttributeCountOffset = 12;
inline constexpr uint32_t kNodeAttributesOffset = 16;
inline constexpr uint32_t kAttributeSize = 8;
inline constexpr uint32_t kItemCommandOffset = 36;
inline constexpr uint32_t kExitStringsSize = 32;
inline constexpr uint32_t kExitCommandOffset = 16;
inline constexpr uint32_t kMaxSiblings = 256;
inline constexpr uint32_t kMaxAttributes = 64;
inline constexpr size_t kMaxNameLength = 64;

struct GuestAddresses {
  uint32_t menu_vtable;
  uint32_t live_name_entry;
  uint32_t empty_string;
  uint32_t exit_strings;
};

inline uint32_t LoadU32(const uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

inline void StoreU32(uint8_t* base, uint32_t address, uint32_t value) {
  const uint32_t swapped = std::byteswap(value);
  std::memcpy(base + address, &swapped, sizeof(swapped));
}

inline std::string_view GuestString(const uint8_t* base, uint32_t address) {
  if (address == 0) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(base + address);
  return std::string_view(text, strnlen(text, kMaxNameLength));
}

inline void StoreString(uint8_t* base, uint32_t address, std::string_view text) {
  std::memcpy(base + address, text.data(), text.size());
  base[address + text.size()] = 0;
}

// Guest-resident text and command of the Exit Game item; the attribute values point at them.
inline void WriteExitStrings(uint8_t* base, uint32_t exit_strings) {
  StoreString(base, exit_strings, kExitText);
  StoreString(base, exit_strings + kExitCommandOffset, kExitCommand);
}

inline uint32_t ExitText(const GuestAddresses& guest) { return guest.exit_strings; }

inline uint32_t ExitCommand(const GuestAddresses& guest) { return guest.exit_strings + kExitCommandOffset; }

inline bool IsExitCommand(const uint8_t* base, uint32_t command) {
  return command != 0 && GuestString(base, command) == kExitCommand;
}

inline bool IsBlank(std::string_view text) { return text.find_first_not_of(' ') == std::string_view::npos; }

inline bool IsMenu(const uint8_t* base, uint32_t menu, uint32_t vtable) {
  return menu != 0 && LoadU32(base, menu) == vtable;
}

// Guest address of the attribute's value pointer, or 0.
inline uint32_t AttributeSlot(const uint8_t* base, uint32_t node, std::string_view name) {
  const uint32_t count = LoadU32(base, node + kNodeAttributeCountOffset);
  for (uint32_t i = 0; i < count && i < kMaxAttributes; ++i) {
    const uint32_t attribute = node + kNodeAttributesOffset + i * kAttributeSize;
    if (GuestString(base, LoadU32(base, attribute)) == name) {
      return attribute + sizeof(uint32_t);
    }
  }
  return 0;
}

inline std::string_view AttributeValue(const uint8_t* base, uint32_t node, std::string_view name) {
  const uint32_t slot = AttributeSlot(base, node, name);
  return slot == 0 ? std::string_view() : GuestString(base, LoadU32(base, slot));
}

inline std::string_view ItemName(const uint8_t* base, uint32_t node) {
  return AttributeValue(base, node, kNameAttribute);
}

// The first item named `name` from `node` along its siblings, or 0.
inline uint32_t FindItem(const uint8_t* base, uint32_t node, std::string_view name) {
  for (uint32_t visited = 0; node != 0 && visited < kMaxSiblings; ++visited) {
    if (ItemName(base, node) == name) {
      return node;
    }
    node = LoadU32(base, node + kNodeNextOffset);
  }
  return 0;
}

// The review item's text holds the Exit Game text once its slot has been rebuilt.
inline bool IsRebuilt(const uint8_t* base, uint32_t review, const GuestAddresses& guest) {
  const uint32_t text = AttributeSlot(base, review, kTextAttribute);
  return text != 0 && LoadU32(base, text) == ExitText(guest);
}

// Leaves the Xbox Live branch of the main menu's handlers with no item to match: they compare the activated item's
// name against this table entry.
inline void DetachLiveHandlers(uint8_t* base, const GuestAddresses& guest) {
  StoreU32(base, guest.live_name_entry, guest.empty_string);
}

// The entry stays detached for the whole session, while the menu re-parses fresh item nodes every time it is built.
inline bool IsLiveEntryExpected(const uint8_t* base, const GuestAddresses& guest) {
  const uint32_t entry = LoadU32(base, guest.live_name_entry);
  return entry == guest.empty_string || GuestString(base, entry) == kLiveItem;
}

// Moves the Options and Review content up into the Xbox Live and Options slots and gives the last slot Exit Game, so
// the four slots, their focus models and their links stay the game's own. Changes nothing unless every part of the
// list is where the game's data puts it.
inline void ReplaceLiveItem(uint8_t* base, uint32_t node, const GuestAddresses& guest) {
  const uint32_t live = FindItem(base, node, kLiveItem);
  const uint32_t options = FindItem(base, live, kOptionsItem);
  const uint32_t review = FindItem(base, options, kReviewItem);
  if (guest.exit_strings == 0 || live == 0 || options == 0 || review == 0) {
    return;
  }
  if (IsRebuilt(base, review, guest)) {
    DetachLiveHandlers(base, guest);
    return;
  }
  if (!IsBlank(AttributeValue(base, live, kCommandAttribute)) || !IsLiveEntryExpected(base, guest)) {
    return;
  }

  const uint32_t live_text = AttributeSlot(base, live, kTextAttribute);
  const uint32_t live_command = AttributeSlot(base, live, kCommandAttribute);
  const uint32_t options_text = AttributeSlot(base, options, kTextAttribute);
  const uint32_t options_command = AttributeSlot(base, options, kCommandAttribute);
  const uint32_t review_text = AttributeSlot(base, review, kTextAttribute);
  const uint32_t review_command = AttributeSlot(base, review, kCommandAttribute);
  if (live_text == 0 || live_command == 0 || options_text == 0 || options_command == 0 || review_text == 0 ||
      review_command == 0) {
    return;
  }

  StoreU32(base, live_text, LoadU32(base, options_text));
  StoreU32(base, live_command, LoadU32(base, options_command));
  StoreU32(base, options_text, LoadU32(base, review_text));
  StoreU32(base, options_command, LoadU32(base, review_command));
  StoreU32(base, review_text, ExitText(guest));
  StoreU32(base, review_command, ExitCommand(guest));
  DetachLiveHandlers(base, guest);
}

}  // namespace mua::main_menu
