#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "game_library.h"
#include "user_data.h"

struct ImFont;
struct ImFontAtlas;

namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace mua {

class GameSettings;

struct LauncherPaths {
  std::filesystem::path user_dir;
  std::filesystem::path settings_file;
  std::filesystem::path logs_dir;
  UserDataOutcome user_data_outcome = UserDataOutcome::kPortable;
  rex::ui::ImmediateDrawer* immediate_drawer = nullptr;  // for the achievement icons
  std::function<std::string(const GameDefinition&)> runtime_blocker;  // why a game cannot start; empty when it can
};

struct LauncherCallbacks {
  std::function<void(const GameState&)> play;  // starts the selected game in this window
  std::function<bool(const GameDefinition&)> other_executable;  // the game's module is in another program
  std::function<bool(const GameState&)> start_other_executable;  // starts that program; false if it could not
  std::function<bool()> window_changed;     // the saved window size, monitor or VSync needs a new window
  std::function<bool()> restart;            // starts a new process with the saved settings; false if it could not
  std::function<void()> quit;
  std::function<void*()> window_handle;     // the native window the launcher is shown in
  std::function<bool()> window_fullscreen;
  std::function<void(bool)> set_window_fullscreen;  // the launcher's own temporary fullscreen
};

// `density` is the window's display scale, so text is rasterized for its real pixel size.
void LoadLauncherFonts(ImFontAtlas* atlas, float density);

// Re-rasterizes the launcher fonts in place when the window moves to a display with another scale.
void UpdateLauncherFonts(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, float density);

// The launcher's body font, null when the UI fonts were not found.
ImFont* LauncherRegularFont();
ImFont* LauncherSemiboldFont();

// Game files that are not ready always show the launcher: it is the only way to see why, so neither
// the player's "off" choice nor the relaunch flag can hide it.
inline bool LauncherWanted(bool game_ready, bool skip_once, bool shift_held, bool enabled) {
  if (!game_ready) {
    return true;
  }
  return !skip_once && (shift_held || enabled);
}

// Shown when the selected game is not ready, when Shift is held, or unless the
// player turned it off. Not shown on the relaunch that follows a window change.
bool ShouldShowLauncher(bool game_ready);

// Call before the window is created when the launcher is shown: the window
// is Windowed, at the launcher's size, on the primary display.
void PrepareLauncherWindow();

// Creates the launcher dialog; it deletes itself when closed. The selected game is the settings' selected game.
void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameLibrary library, GameSettings& settings,
                  LauncherCallbacks callbacks);

}  // namespace mua
