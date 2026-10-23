#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include <rex/cvar.h>

#include "trg/settings.h"

#include "game_definition.h"
#include "window_placement.h"

REXCVAR_DECLARE(bool, mua_launcher);
REXCVAR_DECLARE(std::string, mua_game);
REXCVAR_DECLARE(std::string, mua_games_root);
REXCVAR_DECLARE(bool, mua_skip_launcher);
REXCVAR_DECLARE(int32_t, mua_frame_rate);
REXCVAR_DECLARE(bool, mua_show_fps);
REXCVAR_DECLARE(bool, mua_show_render_resolution);
REXCVAR_DECLARE(bool, mua_show_frame_counter);
REXCVAR_DECLARE(bool, mua_msaa_compatibility);
REXCVAR_DECLARE(std::string, mua_aspect_ratio);
REXCVAR_DECLARE(std::string, mua_upscaler);
REXCVAR_DECLARE(std::string, mua_upscale_quality);
REXCVAR_DECLARE(bool, detailed_logging);
REXCVAR_DECLARE(std::string, mua_graphics_adapter);
REXCVAR_DECLARE(int32_t, mua_launcher_x);
REXCVAR_DECLARE(int32_t, mua_launcher_y);
REXCVAR_DECLARE(int32_t, mua_launcher_width);
REXCVAR_DECLARE(int32_t, mua_launcher_height);
REXCVAR_DECLARE(bool, mua_launcher_maximized);
REXCVAR_DECLARE(bool, mua_launcher_fullscreen);

namespace mua {

struct MonitorInfo;
class GameSettings;

// Allowing tearing is VSync Off.
inline constexpr char kTearingSetting[] = "d3d12_allow_variable_refresh_rate_and_tearing";

inline constexpr char kSelectedGameKey[] = "mua_game";

// The player's settings file, loaded over the shipped marvel_ultimate_alliance.toml.
inline std::filesystem::path PlayerSettingsPath(const std::filesystem::path& user_data_root) {
  return user_data_root / "settings.toml";
}

// A game's own settings: only the values that differ from settings.toml.
inline std::filesystem::path GameSettingsPath(const std::filesystem::path& user_data_root,
                                              const GameDefinition& game) {
  return user_data_root / std::string(game.data_folder) / "game_settings.toml";
}
void LoadPlayerSettings(const std::filesystem::path& path);

// Sets the Xenos native_2x_msaa cvar from the launcher's Multisampling choice. Call once the GPU plugin
// has registered its cvars and before emulation starts.
void ApplyMultisampling();

// Sets async_shader_wait_ms for the Frame rate choice. Call with ApplyMultisampling.
void ApplyShaderWait();

// Sets present_effect for the game's Upscaling choice. Call before the presenter is created (OnPreSetup).
void ApplyUpscaler(const GameSettings& settings);

// Logs the internal and output render sizes of the Upscaling choice. Call after ApplyGameSettings.
void LogUpscaling();

// Keeps the guest video mode off the window size, which ReXGlue otherwise reports. Call once the window exists and
// before the guest starts.
void PinGuestVideoMode();

// Sets the guest video mode, which the presenter takes its display aspect from, to the Aspect Ratio choice. Call after
// PinGuestVideoMode.
void ApplyAspectRatio();

// Sets d3d12_adapter from the launcher's Graphics card choice. Call once the player settings are loaded and
// before the graphics device is created; Automatic leaves the SDK's own preference alone.
void ApplyGraphicsAdapter();

struct StartupWindowInfo {
  int selected_width = -1;
  int selected_height = -1;
  int configured_width = 0;
  int configured_height = 0;
  int output_width = 0;
  int output_height = 0;
  bool fullscreen = false;
  std::string controller;
};

// Logs the launcher's own selections separately from the effective runtime state, plus a crash-safe summary.
void LogStartupDiagnostics(const GameSettings& settings, const MonitorInfo& display, int monitor,
                           const std::filesystem::path& game_dir, const StartupWindowInfo& window);

// The launcher window placement loaded from the player file.
WindowPlacement SavedLauncherPlacement();
bool SavedLauncherFullscreen();

// Edits stay pending until Save() or Apply(), so nothing reaches the window while the launcher is open.
class GameSettings final : public trg::SettingsStore {
 public:
  // The settings file is TOML, so toggles are written as true and false.
  GameSettings() {
    true_value = "true";
    false_value = "false";
  }

  // Takes the settings in effect now as the saved state.
  void Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config);
  // Takes the settings that were not registered at Capture (the Xenos graphics cvars).
  // Call once after SetupPresentation and before the launcher is shown.
  void CaptureLateRegistered();

  std::string Get(std::string_view key, std::string_view def = {}) const override;
  void Set(std::string_view key, std::string value) override;
  bool Save() override;
  // Writes the given values at once. Pending edits stay pending, and Reset never touches them.
  bool SaveNow(const SettingValues& values);
  void ResetToDefaults() override;

  // Sets the running game's cvars that differ from the pending values.
  void Apply() const;
  // Sets the cvars of the settings that belong to the selected game.
  void ApplyGameSettings() const;

  // Records the window settings this process creates its window with.
  void MarkWindowCreated();
  // Whether the pending window size, monitor, VSync or graphics card needs a new process.
  bool WindowChanged() const;

  // Game-specific settings read and write the selected game's own values; the rest are shared.
  void SelectGame(GameId game);
  GameId SelectedGame() const { return game_; }

 private:
  using Values = std::map<std::string, std::string>;

  std::filesystem::path GameFile(GameId game) const;
  const std::string* GameValue(std::string_view key) const;
  bool SaveGameValues(GameId game);

  std::filesystem::path player_file_;
  std::filesystem::path shipped_config_;
  Values saved_;
  Values pending_;
  std::map<GameId, Values> game_saved_;
  std::map<GameId, Values> game_pending_;
  GameId game_ = GameId::kMua1Gold;
  Values window_created_;
  std::set<std::string> unregistered_;
};

}  // namespace mua
