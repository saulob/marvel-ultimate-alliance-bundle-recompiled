#include "game_definition.h"

#include <algorithm>
#include <array>
#include <optional>

namespace mua {
namespace {

constexpr PageMask kInstallPages = PageBit(LauncherPage::kPlay) | PageBit(LauncherPage::kAbout);

constexpr PageMask kPlayablePages = kInstallPages | PageBit(LauncherPage::kDisplay) |
                                    PageBit(LauncherPage::kGraphics) | PageBit(LauncherPage::kGameplay) |
                                    PageBit(LauncherPage::kAchievements) | PageBit(LauncherPage::kControls);

constexpr std::array kGames = {
    GameDefinition{
        .id = GameId::kMua1,
        .key = "mua1",
        .display_name = "Marvel Ultimate Alliance",
        .tab_label = "MARVEL ULTIMATE ALLIANCE",
        .short_label = "MUA 1",
        .package_label = "Xbox 360 disc",
        .profile_id = "mua-x360",
        .legacy_folder = "",
        .runtime_supported = false,
        .pages = kInstallPages,
    },
    GameDefinition{
        .id = GameId::kMua1Gold,
        .key = "mua1-gold",
        .display_name = "Marvel Ultimate Alliance Gold Edition",
        .tab_label = "GOLD EDITION",
        .short_label = "GOLD EDITION",
        .package_label = "Gold Edition",
        .profile_id = "mua-gold-x360",
        .legacy_folder = "extracted",
        .runtime_supported = true,
        .pages = kPlayablePages,
    },
    GameDefinition{
        .id = GameId::kMua2,
        .key = "mua2",
        .display_name = "Marvel Ultimate Alliance 2",
        .tab_label = "MARVEL ULTIMATE ALLIANCE 2",
        .short_label = "MUA 2",
        .package_label = "Xbox 360 disc",
        .profile_id = "mua2-x360",
        .legacy_folder = "",
        .runtime_supported = false,
        .pages = kInstallPages,
    },
};

std::optional<GameId> g_active_game;

}  // namespace

std::span<const GameDefinition> AllGames() {
  return kGames;
}

const GameDefinition& Game(GameId id) {
  return *std::ranges::find_if(kGames, [&](const GameDefinition& game) { return game.id == id; });
}

const GameDefinition* FindGame(std::string_view key) {
  const auto match = std::ranges::find_if(kGames, [&](const GameDefinition& game) { return game.key == key; });
  return match == kGames.end() ? nullptr : &*match;
}

const GameDefinition& DefaultGame() {
  return Game(GameId::kMua1Gold);
}

const GameProfile& ProfileOf(const GameDefinition& game) {
  return *ProfileById(game.profile_id);
}

const GameDefinition* GameForProfile(const GameProfile& profile) {
  const auto match =
      std::ranges::find_if(kGames, [&](const GameDefinition& game) { return game.profile_id == profile.id; });
  return match == kGames.end() ? nullptr : &*match;
}

std::string GameWindowTitle(const GameDefinition& game) {
  return std::string(game.display_name) + " Recompiled";
}

void SetActiveGame(GameId id) {
  g_active_game = id;
}

const GameDefinition* ActiveGame() {
  return g_active_game ? &Game(*g_active_game) : nullptr;
}

}  // namespace mua
