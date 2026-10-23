#include "settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/logging.h>

#include "trg/settings.h"

#include "aspect_ratio.h"
#include "crash_report.h"
#include "diagnostics.h"
#include "frame_limiter.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "input_summary.h"
#include "keybinds.h"
#include "multisampling.h"
#include "platform.h"
#include "system_info.h"

REXCVAR_DEFINE_BOOL(mua_launcher, true, "Marvel Ultimate Alliance",
                    "Show the launcher before the game starts (hold Shift at start to force it)");
REXCVAR_DEFINE_STRING(mua_game, "mua1-gold", "Marvel Ultimate Alliance",
                      "Game the launcher has selected: mua1, mua1-gold or mua2");
REXCVAR_DEFINE_STRING(mua_games_root, "", "Marvel Ultimate Alliance",
                      "Folder holding the game folders and disc images (default: game next to the program)");
REXCVAR_DEFINE_BOOL(mua_skip_launcher, false, "Marvel Ultimate Alliance",
                    "Skip the launcher once (set when the launcher restarts the game)");
REXCVAR_DEFINE_INT32(mua_frame_rate, mua::kFrameRateDefault, "Marvel Ultimate Alliance",
                     "Frame rate: 30, 60 (original), 90, 120, 144, 165, 240 or 0 for unlimited");
REXCVAR_DEFINE_BOOL(mua_show_fps, true, "Marvel Ultimate Alliance", "Show the frame rate in the window title");
REXCVAR_DEFINE_BOOL(mua_show_render_resolution, false, "Marvel Ultimate Alliance",
                    "Show the render resolution in the window title");
REXCVAR_DEFINE_BOOL(mua_show_frame_counter, false, "Marvel Ultimate Alliance",
                    "Show the frame counter in the corner (F2 toggles it)");
REXCVAR_DEFINE_BOOL(mua_msaa_compatibility, false, "Marvel Ultimate Alliance",
                    "Draw the game's 2x MSAA render targets with 4x MSAA, for GPUs or drivers whose "
                    "native 2x MSAA misrenders (sets native_2x_msaa)");
REXCVAR_DEFINE_STRING(mua_aspect_ratio, "16:9", "Marvel Ultimate Alliance",
                      "Aspect ratio the game renders in: 16:9, 16:10, 21:9, 32:9, 4:3 or 5:4 (applied at start)");
REXCVAR_DEFINE_STRING(mua_graphics_adapter, "auto", "Marvel Ultimate Alliance",
                      "Graphics card: auto, or the index d3d12_adapter takes (applied at start)");
REXCVAR_DEFINE_INT32(mua_launcher_x, 0, "Marvel Ultimate Alliance",
                     "Launcher window left edge, written by the launcher");
REXCVAR_DEFINE_INT32(mua_launcher_y, 0, "Marvel Ultimate Alliance",
                     "Launcher window top edge, written by the launcher");
REXCVAR_DEFINE_INT32(mua_launcher_width, 0, "Marvel Ultimate Alliance",
                     "Launcher window restored width, written by the launcher");
REXCVAR_DEFINE_INT32(mua_launcher_height, 0, "Marvel Ultimate Alliance",
                     "Launcher window restored height, written by the launcher");
REXCVAR_DEFINE_BOOL(mua_launcher_maximized, false, "Marvel Ultimate Alliance",
                    "Launcher window was maximized when closed");
REXCVAR_DEFINE_BOOL(mua_launcher_fullscreen, false, "Marvel Ultimate Alliance",
                    "Launcher was left in its own F11 fullscreen, written by the launcher");

namespace mua {
namespace {

// Every setting the launcher captures, saves, applies and resets. The keyboard bindings come from
// the launcher's own table so their names live in one place.
std::vector<std::string> GameSettingKeys() {
  std::vector<std::string> keys = {
      "mua_launcher",
      kSelectedGameKey,
      "fullscreen",
      "window_width",
      "window_height",
      "monitor",
      kTearingSetting,
      "mua_graphics_adapter",
      "mua_aspect_ratio",
      "resolution_scale",
      "swap_post_effect",
      "mua_msaa_compatibility",
      "anisotropic_override",
      "mua_frame_rate",
      "mua_show_fps",
      "mua_show_render_resolution",
      "mua_show_frame_counter",
      "detailed_logging",
      "mnk_mode",
  };
  for (const KeyboardAction& action : kKeyboardActions) {
    keys.emplace_back(action.key);
  }
  return keys;
}

const std::vector<std::string> kGameSettingKeys = GameSettingKeys();

constexpr std::string_view kGameScopedKeys[] = {"mua_frame_rate", "mua_aspect_ratio"};

bool IsGameKey(std::string_view key) {
  return std::ranges::find(kGameScopedKeys, key) != std::end(kGameScopedKeys);
}

const std::vector<std::string> kWindowKeys = {
    "window_width",
    "window_height",
    "monitor",
    kTearingSetting,
    "mua_graphics_adapter",
};

bool IsWindowKey(std::string_view key) {
  return std::find(kWindowKeys.begin(), kWindowKeys.end(), key) != kWindowKeys.end();
}

// Monitor 0 (Default) is the primary display, as monitor 1 is.
std::string WindowValue(std::string_view key, const std::string& value) {
  if (key == "monitor") {
    return std::to_string(std::max(1, std::atoi(value.c_str())));
  }
  return value;
}

std::string TomlValue(std::string_view key, const std::string& value) {
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
  if (!info || info->type != rex::cvar::FlagType::String) {
    return value;
  }
  std::string quoted = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
    }
    quoted += c;
  }
  return quoted + '"';
}

std::optional<std::string> ConfigValue(const toml::table& table, std::string_view key) {
  const toml::node* node = table.get(key);
  if (!node) {
    return std::nullopt;
  }
  if (auto v = node->value_exact<bool>()) {
    return *v ? "true" : "false";
  }
  if (auto v = node->value_exact<int64_t>()) {
    return std::to_string(*v);
  }
  if (auto v = node->value_exact<double>()) {
    return std::to_string(*v);
  }
  return node->value_exact<std::string>();
}

trg::TextFileSettings OpenSettingsFile(const std::filesystem::path& path, std::string_view header) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (!std::filesystem::exists(path, ec)) {
    std::ofstream(path) << header;
  }
  return trg::TextFileSettings(Utf8(path));
}

trg::TextFileSettings OpenPlayerFile(const std::filesystem::path& path) {
  return OpenSettingsFile(path,
                          "# Marvel Ultimate Alliance player settings, written by the launcher.\n"
                          "# Loaded after marvel_ultimate_alliance.toml. Remove a line to use the shipped value.\n");
}

std::vector<std::pair<std::string, std::string>> ChangedValues(const std::map<std::string, std::string>& saved,
                                                              const std::map<std::string, std::string>& pending) {
  std::vector<std::pair<std::string, std::string>> changes;
  for (const auto& [key, value] : pending) {
    const auto it = saved.find(key);
    if (it == saved.end() || it->second != value) {
      changes.emplace_back(key, value);
    }
  }
  return changes;
}

std::string AspectRatioKey(std::string_view value) {
  return InfoFor(ParseAspectRatio(value)).key;
}

std::map<std::string, std::string> ReadGameValues(const std::filesystem::path& file) {
  std::map<std::string, std::string> values;
  std::error_code ec;
  if (file.empty() || !std::filesystem::exists(file, ec)) {
    return values;
  }
  try {
    const toml::table table = toml::parse_file(Utf8(file));
    for (const std::string_view key : kGameScopedKeys) {
      if (const auto value = ConfigValue(table, key)) {
        values[std::string(key)] = key == "mua_frame_rate"    ? std::to_string(FrameRateFromText(*value))
                                   : key == "mua_aspect_ratio" ? AspectRatioKey(*value)
                                                               : *value;
      }
    }
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("Cannot read {}: {}", Utf8(file.filename()), e.what());
  }
  return values;
}

std::string CvarText(std::string_view name, std::string_view fallback) {
  const std::string value = rex::cvar::GetFlagByName(name);
  return value.empty() ? std::string(fallback) : value;
}

std::string OnOff(bool value) {
  return value ? "On" : "Off";
}

std::string ChoiceLabel(std::initializer_list<std::pair<std::string_view, std::string_view>> choices,
                        std::string_view value, std::string_view fallback) {
  for (const auto& [key, label] : choices) {
    if (key == value) {
      return std::string(label);
    }
  }
  return std::string(fallback);
}

std::string SelectedGraphicsCardLabel(const GameSettings& settings) {
  const std::string setting = settings.Get("mua_graphics_adapter", kGraphicsAdapterAuto);
  const std::optional<int> index = ParseGraphicsAdapter(setting);
  if (!index) {
    return "Automatic";
  }
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  for (const GraphicsAdapter& adapter : adapters) {
    if (adapter.index != *index) {
      continue;
    }
    const auto same_name = std::count_if(adapters.begin(), adapters.end(), [&](const GraphicsAdapter& other) {
      return other.name == adapter.name;
    });
    return same_name > 1 ? adapter.name + " (" + std::to_string(adapter.index) + ")" : adapter.name;
  }
  return "Graphics card " + setting + " (not found)";
}

std::string TextureFilteringName(std::string_view value) {
  if (value == "-1") {
    return "Game";
  }
  if (value == "0") {
    return "Off";
  }
  if (value == "2") {
    return "2x";
  }
  if (value == "3") {
    return "4x";
  }
  if (value == "4") {
    return "8x";
  }
  if (value == "5") {
    return "16x";
  }
  return std::string(value);
}

std::string GameStateName(GameFileState state) {
  switch (state) {
    case GameFileState::kValid:
      return "verified";
    case GameFileState::kDifferentVersion:
      return "unsupported version";
    case GameFileState::kUnrecognized:
      return "unrecognized";
    case GameFileState::kIncomplete:
      return "incomplete";
    case GameFileState::kReadError:
      return "unreadable";
    case GameFileState::kMissing:
      return "missing";
  }
  return "unknown";
}

}  // namespace

void LoadPlayerSettings(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return;
  }
  rex::cvar::LoadConfig(path);
  REXLOG_INFO("Player settings: {}", Utf8(path.filename()));
}

WindowPlacement SavedLauncherPlacement() {
  struct Cvars {
    int GetInt(const char* key, int) const { return std::atoi(rex::cvar::GetFlagByName(key).c_str()); }
    bool GetBool(const char* key, bool) const { return rex::cvar::GetFlagByName(key) == "true"; }
  };
  return ReadPlacement(Cvars{});
}

bool SavedLauncherFullscreen() {
  return REXCVAR_GET(mua_launcher_fullscreen);
}

void GameSettings::Capture(const std::filesystem::path& player_file, const std::filesystem::path& shipped_config) {
  player_file_ = player_file;
  shipped_config_ = shipped_config;
  saved_.clear();
  unregistered_.clear();
  for (const std::string& key : kGameSettingKeys) {
    saved_[key] = rex::cvar::GetFlagByName(key);
    if (!rex::cvar::GetFlagInfo(key)) {
      unregistered_.insert(key);
    }
  }
  pending_ = saved_;
  std::string& frame_rate = pending_["mua_frame_rate"];
  frame_rate = std::to_string(FrameRateFromText(frame_rate));
  std::string& aspect = pending_["mua_aspect_ratio"];
  aspect = AspectRatioKey(aspect);
  game_saved_.clear();
  for (const GameDefinition& game : AllGames()) {
    game_saved_[game.id] = ReadGameValues(GameFile(game.id));
  }
  game_pending_ = game_saved_;
  const GameDefinition* selected = FindGame(pending_[kSelectedGameKey]);
  game_ = selected ? selected->id : DefaultGame().id;
}

void GameSettings::CaptureLateRegistered() {
  for (auto it = unregistered_.begin(); it != unregistered_.end();) {
    const std::string& key = *it;
    if (!rex::cvar::GetFlagInfo(key)) {
      ++it;
      continue;
    }
    if (pending_[key].empty()) {
      saved_[key] = pending_[key] = rex::cvar::GetFlagByName(key);
    }
    it = unregistered_.erase(it);
  }
}

std::string GameSettings::Get(std::string_view key, std::string_view def) const {
  if (const std::string* value = IsGameKey(key) ? GameValue(key) : nullptr; value && !value->empty()) {
    return *value;
  }
  const auto it = pending_.find(std::string(key));
  return it == pending_.end() || it->second.empty() ? std::string(def) : it->second;
}

void GameSettings::Set(std::string_view key, std::string value) {
  if (IsGameKey(key)) {
    game_pending_[game_][std::string(key)] = std::move(value);
    return;
  }
  pending_[std::string(key)] = std::move(value);
}

void GameSettings::SelectGame(GameId game) {
  game_ = game;
  pending_[kSelectedGameKey] = std::string(Game(game).key);
}

std::filesystem::path GameSettings::GameFile(GameId game) const {
  return player_file_.empty() ? std::filesystem::path() : GameSettingsPath(player_file_.parent_path(), Game(game));
}

const std::string* GameSettings::GameValue(std::string_view key) const {
  const auto values = game_pending_.find(game_);
  if (values == game_pending_.end()) {
    return nullptr;
  }
  const auto value = values->second.find(std::string(key));
  return value == values->second.end() ? nullptr : &value->second;
}

bool GameSettings::Save() {
  const auto changes = ChangedValues(saved_, pending_);
  std::vector<GameId> changed_games;
  for (const auto& [game, values] : game_pending_) {
    if (!ChangedValues(game_saved_[game], values).empty()) {
      changed_games.push_back(game);
    }
  }
  if (changes.empty() && changed_games.empty()) {
    return true;
  }
  for (const auto& [key, value] : changes) {
    if (!rex::cvar::GetFlagInfo(key)) {
      REXLOG_ERROR("Cannot save {}: the setting is not registered", key);
      return false;
    }
  }

  if (!changes.empty()) {
    trg::TextFileSettings file = OpenPlayerFile(player_file_);
    for (const auto& [key, value] : changes) {
      file.Set(key, TomlValue(key, value));
    }
    if (!file.Save()) {
      REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_.filename()));
      return false;
    }
    saved_ = pending_;
  }
  return std::ranges::all_of(changed_games, [this](GameId game) { return SaveGameValues(game); });
}

bool GameSettings::SaveGameValues(GameId game) {
  const std::filesystem::path path = GameFile(game);
  if (path.empty()) {
    return false;
  }
  const GameDefinition& definition = Game(game);
  trg::TextFileSettings file = OpenSettingsFile(
      path, "# " + std::string(definition.display_name) +
                " settings, written by the launcher.\n# Loaded over settings.toml. Remove a line to use the shared value.\n");
  for (const auto& [key, value] : ChangedValues(game_saved_[game], game_pending_[game])) {
    file.Set(key, TomlValue(key, value));
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write {} settings to {}", definition.key, Utf8(path.filename()));
    return false;
  }
  game_saved_[game] = game_pending_[game];
  return true;
}

bool GameSettings::SaveNow(const SettingValues& values) {
  trg::TextFileSettings file = OpenPlayerFile(player_file_);
  for (const auto& [key, value] : values) {
    file.Set(key, value);
  }
  if (!file.Save()) {
    REXLOG_ERROR("Cannot write player settings to {}", Utf8(player_file_.filename()));
    return false;
  }
  return true;
}

void GameSettings::ResetToDefaults() {
  toml::table shipped;
  try {
    shipped = toml::parse_file(Utf8(shipped_config_));
  } catch (const toml::parse_error& e) {
    REXLOG_WARN("Cannot read {}: {}", Utf8(shipped_config_.filename()), e.what());
  }
  for (const std::string& key : kGameSettingKeys) {
    if (key == kSelectedGameKey) {
      continue;
    }
    const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(key);
    pending_[key] = ConfigValue(shipped, key).value_or(info ? info->default_value : std::string());
  }
  for (auto& [game, values] : game_pending_) {
    for (auto& [key, value] : values) {
      value = pending_[key];
    }
  }
}

void GameSettings::Apply() const {
  for (const auto& [key, value] : pending_) {
    if (IsWindowKey(key) || IsGameKey(key) || rex::cvar::GetFlagByName(key) == value) {
      continue;
    }
    rex::cvar::SetFlagByName(key, value);
  }
  ApplyGameSettings();
  ApplyShaderWait();
  ApplyMultisampling();
}

void GameSettings::ApplyGameSettings() const {
  for (const std::string_view key : kGameScopedKeys) {
    const std::string value = Get(key);
    if (!value.empty() && rex::cvar::GetFlagByName(key) != value) {
      rex::cvar::SetFlagByName(key, value);
    }
  }
}

void ApplyShaderWait() {
  rex::cvar::SetFlagByName("async_shader_wait_ms",
                           std::to_string(ShaderWaitMsFor(FrameRateOrDefault(REXCVAR_GET(mua_frame_rate)))));
}

// Also replaces a native_2x_msaa = false written by launchers that labelled it Off.
void ApplyMultisampling() {
  rex::cvar::SetFlagByName("native_2x_msaa",
                           Native2xMsaaFor(REXCVAR_GET(mua_msaa_compatibility)) ? "true" : "false");
}

void PinGuestVideoMode() {
  for (const char* name : {"window_width", "window_height", "video_mode_width", "video_mode_height"}) {
    rex::cvar::ResetToDefault(name);
  }
}

void ApplyAspectRatio() {
  const AspectRatioInfo& info = InfoFor(ParseAspectRatio(REXCVAR_GET(mua_aspect_ratio)));
  const WindowSize mode = VideoModeSize(info.aspect);
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(mode.width));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(mode.height));
  REXLOG_INFO("Aspect ratio: {} ({}x{})", info.key, CanvasWidth(info.aspect), kCanvasHeight);
}

void ApplyGraphicsAdapter() {
  const std::string setting = REXCVAR_GET(mua_graphics_adapter);
  if (!ParseGraphicsAdapter(setting)) {
    REXLOG_INFO("Graphics card: Automatic");
    return;
  }
  const std::vector<GraphicsAdapter> adapters = ListGraphicsAdapters();
  const int index = ResolveGraphicsAdapter(setting, adapters);
  if (index < 0) {
    REXLOG_WARN("Graphics card {} is not available, using Automatic", setting);
    return;
  }
  rex::cvar::SetFlagByName("d3d12_adapter", std::to_string(index));
  for (const GraphicsAdapter& adapter : adapters) {
    if (adapter.index == index) {
      REXLOG_INFO("Graphics card: {} (adapter {})", adapter.name, index);
    }
  }
}

void LogStartupDiagnostics(const GameSettings& settings, const MonitorInfo& display, int monitor,
                           const std::filesystem::path& game_dir, const StartupWindowInfo& window) {
  const AspectRatio aspect = ParseAspectRatio(settings.Get("mua_aspect_ratio", "16:9"));
  const int scale = std::max(1, std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()));
  const WindowSize render = RenderSize(aspect, scale);
  const std::string selected_card = SelectedGraphicsCardLabel(settings);
  const bool selected_fullscreen = settings.GetBool("fullscreen", false);
  const bool selected_tearing = settings.GetBool(kTearingSetting, true);
  const bool runtime_tearing = CvarText(kTearingSetting, "true") == "true";

  StartupDiagnostics diagnostics;
  diagnostics.launcher_display.mode = selected_fullscreen ? "Borderless Fullscreen" : "Windowed";
  const int selected_width = window.selected_width >= 0 ? window.selected_width : window.configured_width;
  const int selected_height = window.selected_height >= 0 ? window.selected_height : window.configured_height;
  diagnostics.launcher_display.window_size =
      selected_width > 0 && selected_height > 0
          ? std::to_string(selected_width) + "x" + std::to_string(selected_height)
          : "Default";
  diagnostics.launcher_display.monitor =
      monitor > 0 ? "Display " + std::to_string(monitor) : "Default (primary)";
  diagnostics.launcher_display.aspect_ratio = InfoFor(aspect).label;
  diagnostics.launcher_display.vsync = selected_tearing ? "Off" : "On";
  diagnostics.launcher_display.render_resolution_in_title =
      OnOff(settings.GetBool("mua_show_render_resolution", false));

  diagnostics.launcher_graphics.graphics_card = selected_card;
  diagnostics.launcher_graphics.render_resolution =
      (scale == 1 ? std::string("Original") : std::to_string(scale) + "x") + " (" +
      std::to_string(render.width) + "x" + std::to_string(render.height) + ")";
  diagnostics.launcher_graphics.anti_aliasing =
      ChoiceLabel({{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}},
                  settings.Get("swap_post_effect", "none"), "Off");
  diagnostics.launcher_graphics.multisampling =
      settings.GetBool("mua_msaa_compatibility", false) ? "4x MSAA (Compatibility)" : "2x MSAA";
  diagnostics.launcher_graphics.texture_filtering =
      TextureFilteringName(settings.Get("anisotropic_override", "3"));

  diagnostics.system.os = OsDescription();
  diagnostics.system.cpu = CpuDescription();
  diagnostics.system.logical_processors = LogicalProcessorCount();

  diagnostics.gpu.selected = selected_card;
  const GraphicsAdapterDetails adapter =
      EffectiveGraphicsAdapter(std::atoi(CvarText("d3d12_adapter", "-1").c_str()));
  diagnostics.gpu.resolved = adapter.name;
  diagnostics.gpu.adapter = adapter.index;
  diagnostics.gpu.vendor_id = adapter.vendor_id;
  diagnostics.gpu.device_id = adapter.device_id;
  diagnostics.gpu.resolved_known = adapter.known;

  diagnostics.display.monitor = monitor;
  diagnostics.display.width = display.width;
  diagnostics.display.height = display.height;
  diagnostics.display.refresh_hz = display.refresh_hz;
  diagnostics.display.scale_percent = int(std::lround(display.scale * 100.0f));
  diagnostics.display.fullscreen = window.fullscreen;
  diagnostics.display.configured_width = window.configured_width;
  diagnostics.display.configured_height = window.configured_height;
  diagnostics.display.output_width = window.output_width;
  diagnostics.display.output_height = window.output_height;
  diagnostics.display.aspect = InfoFor(aspect).key;

  const GameCheck game = CheckGameFiles(game_dir);
  diagnostics.game.title_id = game.identity ? game.identity->title_id : ProfileOf(DefaultGame()).title_id;
  diagnostics.game.executable = "default.xex";
  diagnostics.game.size = game.xex_size;
  diagnostics.game.fingerprint = game.profile ? std::string("profile:") + std::string(game.profile->id) : "";
  diagnostics.game.status = GameStateName(game.state);
  diagnostics.game.sha256 = game.xex_sha256;

  const int diagnostic_fps = FrameRateFromText(settings.Get("mua_frame_rate", "60"));
  diagnostics.gameplay.frame_rate = FrameRateText(diagnostic_fps);
  diagnostics.gameplay.present = PresentationText(runtime_tearing);

  diagnostics.graphics.render_width = render.width;
  diagnostics.graphics.render_height = render.height;
  diagnostics.graphics.guest_width = CanvasWidth(aspect);
  diagnostics.graphics.guest_height = kCanvasHeight;
  const bool host_render_targets = HostRenderTargetsUsed(
      CvarText("render_target_path_d3d12", ""),
      RasterizerOrderedViewsSupported(std::atoi(CvarText("d3d12_adapter", "-1").c_str())));
  diagnostics.graphics.msaa = !host_render_targets                                ? "rov (setting unused)"
                              : settings.GetBool("mua_msaa_compatibility", false) ? "4x compatibility"
                                                                                  : "2x native";
  diagnostics.graphics.anti_aliasing = diagnostics.launcher_graphics.anti_aliasing;
  diagnostics.graphics.texture_filtering = diagnostics.launcher_graphics.texture_filtering;
  diagnostics.graphics.allow_invalid_fetch_constants = CvarText("gpu_allow_invalid_fetch_constants", "false");
  diagnostics.graphics.render_target_path = CvarText("render_target_path_d3d12", "default");

  diagnostics.input.keyboard_mode = settings.GetBool("mnk_mode", true) ? "On" : "Off";
  diagnostics.input.controller = window.controller;

  for (const std::string& line : FormatStartupDiagnostics(diagnostics)) {
    REXLOG_INFO("{}", line);
  }
  SetCrashRuntimeState(diagnostics.gpu.resolved_known ? diagnostics.gpu.resolved.c_str() : "unknown",
                       (std::to_string(display.width) + "x" + std::to_string(display.height) + " | window=" +
                        std::to_string(window.configured_width) + "x" + std::to_string(window.configured_height) +
                        " | output=" + std::to_string(window.output_width) + "x" +
                        std::to_string(window.output_height))
                           .c_str(),
                       diagnostics.gameplay.present.c_str(),
                       ("render=" + std::to_string(diagnostics.graphics.render_width) + "x" +
                        std::to_string(diagnostics.graphics.render_height) + " | msaa=" + diagnostics.graphics.msaa)
                           .c_str());
}

void GameSettings::MarkWindowCreated() {
  window_created_.clear();
  for (const std::string& key : kWindowKeys) {
    window_created_[key] = rex::cvar::GetFlagByName(key);
  }
}

bool GameSettings::WindowChanged() const {
  for (const std::string& key : kWindowKeys) {
    const auto created = window_created_.find(key);
    const std::string created_value = created == window_created_.end() ? std::string() : created->second;
    if (WindowValue(key, Get(key)) != WindowValue(key, created_value)) {
      return true;
    }
  }
  return false;
}

}  // namespace mua
