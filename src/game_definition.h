#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "frame_limiter.h"
#include "game_profile.h"
#include "upscaling.h"

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
  std::string_view executable;  // the program holding this game's recompiled module; empty without a runtime
  std::string_view data_folder;  // the game's folder under the user data root
  FramePolicy frame_policy;      // how the launcher's frame rates meet the game's own timing
  UpscalerMask upscalers;        // the Upscaling choices the launcher offers and the runtime accepts
  // How the GPU plugin finds the 3D scene for NVIDIA DLSS and AMD FSR 3.1 (d3d12_scene_profile); a game without one
  // offers neither.
  std::string_view scene_profile;

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

// Where the game keeps saves, profiles, achievement progress and its own settings. MUA 1 and Gold Edition share a
// title ID, so each game has its own folder.
std::filesystem::path GameUserDataRoot(const GameDefinition& game, const std::filesystem::path& user_data_root);

// Shader and pipeline storage is named by title ID alone, so each game builds it in its own folder.
std::filesystem::path GameCacheRoot(const GameDefinition& game, const std::filesystem::path& cache_root);

// The game the runtime was started for; window titles follow it. Null until a verified game is started.
void SetActiveGame(GameId id);
const GameDefinition* ActiveGame();

}  // namespace mua
