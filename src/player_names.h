#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "user_display_name.h"

namespace mua {

struct PlayerNameSetting {
  const char* key;
  const char* sdk_key;
  const char* label;
  const char* default_name;
  bool send_default;
};

inline constexpr char kPlayerNameHint[] = "Up to 15 characters.";
inline constexpr char kPlayerNamesHelp[] =
    "Changes the name shown in-game. The same names are used by all three games. Save data is not affected.";

// Shared by the three games, so they live in settings.toml and never in a game's own settings.
inline constexpr std::array<PlayerNameSetting, 4> kPlayerNames = {{
    {"mua_player_1_name", "local_player_1_name", "Player 1", "Player 1", true},
    {"mua_player_2_name", "local_player_2_name", "Player 2", "Player 2", false},
    {"mua_player_3_name", "local_player_3_name", "Player 3", "Player 3", false},
    {"mua_player_4_name", "local_player_4_name", "Player 4", "Player 4", false},
}};

using PlayerNameResult = rex::system::xam::UserDisplayNameResult;

inline bool IsValidPlayerName(std::string_view text) {
  return rex::system::xam::NormalizeUserDisplayName(text, nullptr);
}

inline PlayerNameResult CheckPlayerName(std::string_view raw) {
  const std::string_view trimmed = rex::system::xam::TrimUserDisplayName(raw);
  return rex::system::xam::MeasureUserDisplayName(trimmed);
}

// The value for the SDK's local_player_N_name cvar. An invalid name becomes the default. A user that is not signed
// in only gets a name once it differs from the default; empty leaves the SDK answering "no such user".
inline std::string SdkPlayerName(const PlayerNameSetting& player, std::string_view launcher_value) {
  std::string name;
  if (!rex::system::xam::NormalizeUserDisplayName(launcher_value, &name)) {
    name = player.default_name;
  }
  if (!player.send_default && name == player.default_name) {
    return {};
  }
  return name;
}

// The 16-byte buffer a game keeps a local player's display name in (the same ABI as XamUserGetName).
// Returns false and leaves out untouched when no custom name is configured, so the game's own default
// result (whatever it already wrote there) is left alone.
inline bool CachedPlayerNameBytes(std::string_view configured_sdk_value, char out[16]) {
  if (configured_sdk_value.empty()) {
    return false;
  }
  std::memset(out, 0, 16);
  const size_t length = rex::system::xam::Utf8PrefixLength(configured_sdk_value,
                                                            rex::system::xam::kUserDisplayNameMaxBytes);
  std::memcpy(out, configured_sdk_value.data(), length);
  return true;
}

// A name kept at its default leaves the game's own localized "Player N" text in place.
inline bool CustomPlayerNameBytes(const PlayerNameSetting& player, std::string_view configured_sdk_value,
                                  char out[16]) {
  return configured_sdk_value != player.default_name && CachedPlayerNameBytes(configured_sdk_value, out);
}

// MUA1 and Gold build each row of the pause menu's Players list on the stack: the controller's port digit
// ("1".."4", empty while the controller is inactive) at r1 + 80, then the row text "Player <digit>" at r1 + 96.
inline constexpr uint32_t kPlayerListPortTextOffset = 80;
inline constexpr uint32_t kPlayerListRowTextOffset = 96;
inline constexpr uint32_t kPlayerListRowTextSize = 100;

// The local user a Players list row belongs to, or -1 for an inactive controller.
inline int PlayerListRowUser(const char* port_text) {
  if (port_text[0] < '1' || port_text[0] > '4' || port_text[1] != '\0') {
    return -1;
  }
  return port_text[0] - '1';
}

inline const char* PlayerNameMessage(PlayerNameResult result) {
  switch (result) {
    case PlayerNameResult::Empty:
      return "Enter a name.";
    case PlayerNameResult::TooManyCharacters:
      return "Maximum 15 characters.";
    case PlayerNameResult::TooManyBytes:
      return "Name is too long for the game (15-byte limit).";
    case PlayerNameResult::BadCharacter:
      return "That name has a character that cannot be used.";
    default:
      return "";
  }
}

}  // namespace mua
