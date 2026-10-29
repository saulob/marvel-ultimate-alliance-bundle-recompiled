#include "game_definition.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <system_error>

namespace mua {
namespace {

constexpr PageMask kInstallPages = PageBit(LauncherPage::kPlay) | PageBit(LauncherPage::kAbout);

constexpr PageMask kPlayablePages = kInstallPages | PageBit(LauncherPage::kDisplay) |
                                    PageBit(LauncherPage::kGraphics) | PageBit(LauncherPage::kGameplay) |
                                    PageBit(LauncherPage::kAchievements) | PageBit(LauncherPage::kControls);

constexpr PageMask kCheatPages = kPlayablePages | PageBit(LauncherPage::kCheats);

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
        .runtime_supported = true,
        .pages = kCheatPages,
        .program = "marvel_ultimate_alliance_mua1",
        .data_folder = "mua1",
        .frame_policy = kMua1FramePolicy,
        .upscalers = kAllUpscalers,
        .scene_profile = "mua1",
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
        .pages = kCheatPages,
        .program = "marvel_ultimate_alliance_mua1_gold",
        .data_folder = "mua1_gold",
        .frame_policy = kMua1FramePolicy,
        .upscalers = kAllUpscalers,
        // MUA1's scene, which its later Direct3D also draws over at 4x MSAA between the scene's resolves.
        .scene_profile = "mua1-gold",
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
        .runtime_supported = true,
        .pages = kCheatPages,
        .program = "marvel_ultimate_alliance_mua2",
        .data_folder = "mua2",
        .frame_policy = kMua2FramePolicy,
        .upscalers = kAllUpscalers,
        // Its scene is one 1040x624 surface that fits the EDRAM at 2x MSAA, beside the 1280x720 composite.
        .scene_profile = "mua2",
        .scene_size = {1040, 624},
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

std::filesystem::path GameUserDataRoot(const GameDefinition& game, const std::filesystem::path& user_data_root) {
  return user_data_root / std::string(game.data_folder);
}

std::filesystem::path GameCacheRoot(const GameDefinition& game, const std::filesystem::path& cache_root) {
  return cache_root / std::string(game.data_folder);
}

std::string ProgramFileName(std::string_view program, ProgramPlatform platform) {
  std::string name(program);
  if (platform == ProgramPlatform::kWindows && !name.empty()) {
    name += ".exe";
  }
  return name;
}

std::filesystem::path GameProgramPath(const GameDefinition& game, const std::filesystem::path& program_folder,
                                      ProgramPlatform platform) {
  return program_folder / ProgramFileName(game.program, platform);
}

std::string LauncherRuntimeBlocker(const GameDefinition& game, const std::filesystem::path& program_folder,
                                   ProgramPlatform platform) {
  if (game.program.empty()) {
    return std::format("{} has no runtime yet.", game.display_name);
  }
  std::error_code ec;
  if (std::filesystem::is_regular_file(GameProgramPath(game, program_folder, platform), ec)) {
    return {};
  }
  return std::format("The {} runtime is not part of this build.", game.display_name);
}

void SetActiveGame(GameId id) {
  g_active_game = id;
}

const GameDefinition* ActiveGame() {
  return g_active_game ? &Game(*g_active_game) : nullptr;
}

}  // namespace mua
