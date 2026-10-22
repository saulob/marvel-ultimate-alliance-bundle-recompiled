#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "game_profile.h"

namespace mua {

enum class GameId { kMua1, kMua1Gold, kMua2 };

enum class LauncherPage { kPlay, kDisplay, kGraphics, kGameplay, kAchievements, kControls, kAbout };

using PageMask = uint32_t;

constexpr PageMask PageBit(LauncherPage page) {
  return PageMask(1) << unsigned(page);
}

struct GameDefinition {
  GameId id;
  std::string_view key;  // settings value and the game's folder name
  std::string_view display_name;
  std::string_view tab_label;
  std::string_view short_label;
  std::string_view package_label;
  std::string_view profile_id;
  std::string_view legacy_folder;  // earlier folder of an install that is still recognized
  bool runtime_supported;
  PageMask pages;

  bool HasPage(LauncherPage page) const { return (pages & PageBit(page)) != 0; }
};

std::span<const GameDefinition> AllGames();

const GameDefinition& Game(GameId id);
const GameDefinition* FindGame(std::string_view key);
const GameDefinition& DefaultGame();

const GameProfile& ProfileOf(const GameDefinition& game);

// The game a verified profile belongs to.
const GameDefinition* GameForProfile(const GameProfile& profile);

std::string GameWindowTitle(const GameDefinition& game);

// The game the runtime was started for; window titles follow it. Null until a verified game is started.
void SetActiveGame(GameId id);
const GameDefinition* ActiveGame();

}  // namespace mua
