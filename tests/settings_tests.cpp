// Launcher settings: defaults from the shipped file, pending edits, saving to the player's settings.toml, reading
// them back, reset, applying to the running cvars, the window-change rule and the guest video mode pin.
// Scratch folders only. Run: mua_settings_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <toml++/toml.hpp>

#include <rex/cvar.h>

#include "settings.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

const fs::path kShipped = fs::path(MUA_SRC_DIR) / ".." / "config" / "marvel_ultimate_alliance.toml";

struct Scratch {
  fs::path root = fs::temp_directory_path() / "mua_settings_tests";
  fs::path shipped = root / "marvel_ultimate_alliance.toml";
  fs::path player = root / "user_data" / "settings.toml";

  Scratch() {
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file(kShipped, shipped);
  }
  ~Scratch() { fs::remove_all(root); }
};

void TestDefaults(const Scratch& scratch) {
  std::printf("defaults come from the shipped file\n");
  rex::cvar::LoadConfig(scratch.shipped);
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  CHECK(settings.Get("window_width") == "853");
  CHECK(settings.Get("window_height") == "480");
  CHECK(settings.Get("monitor") == "1");
  CHECK(settings.Get("mnk_mode") == "true");
  CHECK(settings.Get("fullscreen") == "false");
  CHECK(settings.Get("mua_launcher") == "true");
  CHECK(settings.Get("mua_aspect_ratio") == "16:9");
  CHECK(settings.Get("mua_graphics_adapter") == "auto");
  CHECK(settings.Get("mua_msaa_compatibility") == "false");
  CHECK(settings.Get("mua_show_render_resolution") == "false");
  CHECK(settings.Get("detailed_logging") == "true");
}

void TestSaveAndReload(const Scratch& scratch) {
  std::printf("saving writes the changes and nothing else, and they load back\n");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  CHECK(settings.Save());
  CHECK(!fs::exists(scratch.player));  // nothing changed, nothing written

  settings.Set("window_width", "1920");
  settings.Set("window_height", "1080");
  settings.Set("mua_msaa_compatibility", "true");
  settings.Set("mua_show_render_resolution", "true");
  settings.Set("mua_graphics_adapter", "1");
  settings.Set("mnk_mode", "false");
  settings.Set("fullscreen", "true");
  CHECK(settings.Save());

  const toml::table written = toml::parse_file(scratch.player.string());
  CHECK(written["window_width"].value<int64_t>() == 1920);
  CHECK(written["window_height"].value<int64_t>() == 1080);
  CHECK(written["mua_msaa_compatibility"].value<bool>() == true);
  CHECK(written["mua_show_render_resolution"].value<bool>() == true);
  CHECK(written["mua_graphics_adapter"].value<std::string>() == "1");
  CHECK(written["mnk_mode"].value<bool>() == false);
  CHECK(written["fullscreen"].value<bool>() == true);
  CHECK(!written.contains("monitor"));

  rex::cvar::LoadConfig(scratch.player);
  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  CHECK(reopened.Get("window_width") == "1920");
  CHECK(reopened.GetBool("mua_msaa_compatibility", false));
  CHECK(reopened.Get("mua_graphics_adapter") == "1");
  CHECK(!reopened.GetBool("mnk_mode", true));
}

void TestUnregisteredIsRefused(const Scratch& scratch) {
  std::printf("a setting no module registered is not written\n");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.Set("keybind_not_a_setting", "Space");
  CHECK(!settings.Save());
  CHECK(!toml::parse_file(scratch.player.string()).contains("keybind_not_a_setting"));
}

void TestReset(const Scratch& scratch) {
  std::printf("reset puts the shipped values back\n");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.Set("window_width", "2560");
  settings.Set("mua_msaa_compatibility", "true");
  settings.ResetToDefaults();
  CHECK(settings.Get("window_width") == "853");
  CHECK(settings.Get("window_height") == "480");
  CHECK(settings.Get("mnk_mode") == "true");
  CHECK(settings.Get("mua_msaa_compatibility") == "false");
  CHECK(settings.Get("mua_aspect_ratio") == "16:9");
  CHECK(settings.Get("mua_graphics_adapter") == "auto");
  CHECK(settings.Get("detailed_logging") == "true");
}

void TestWindowChange(const Scratch& scratch) {
  std::printf("window size, monitor and graphics card need a new window; the rest does not\n");
  rex::cvar::SetFlagByName("window_width", "1280");
  rex::cvar::SetFlagByName("window_height", "720");
  rex::cvar::SetFlagByName("monitor", "1");
  rex::cvar::SetFlagByName("mua_graphics_adapter", "auto");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.MarkWindowCreated();
  CHECK(!settings.WindowChanged());
  settings.Set("mua_msaa_compatibility", "true");
  settings.Set("mnk_mode", "false");
  CHECK(!settings.WindowChanged());
  settings.Set("monitor", "0");  // Default is the primary display, as 1 is
  CHECK(!settings.WindowChanged());
  settings.Set("monitor", "2");
  CHECK(settings.WindowChanged());
  settings.Set("monitor", "1");
  settings.Set("window_width", "1920");
  CHECK(settings.WindowChanged());
  settings.Set("window_width", "1280");
  settings.Set("mua_graphics_adapter", "1");
  CHECK(settings.WindowChanged());
}

void TestApply() {
  std::printf("applying sets the running cvars, but not the window ones\n");
  const fs::path unused;
  mua::GameSettings settings;
  settings.Capture(unused, unused);
  rex::cvar::SetFlagByName("window_width", "1280");
  rex::cvar::SetFlagByName("mua_show_render_resolution", "false");
  settings.Set("mua_show_render_resolution", "true");
  settings.Set("window_width", "1920");
  settings.Apply();
  CHECK(rex::cvar::GetFlagByName("mua_show_render_resolution") == "true");
  CHECK(rex::cvar::GetFlagByName("window_width") == "1280");
}

void TestDisplayModeStartup(const Scratch& scratch) {
  std::printf("the saved Display mode is the one the game starts in\n");
  for (const char* saved : {"false", "true"}) {
    fs::create_directories(scratch.player.parent_path());
    std::ofstream(scratch.player) << "fullscreen = " << saved << "\n";
    rex::cvar::ResetToDefault("fullscreen");
    mua::LoadPlayerSettings(scratch.player);
    mua::GameSettings settings;
    settings.Capture(scratch.player, scratch.shipped);
    rex::cvar::SetFlagByName("fullscreen", "false");  // the launcher's PrepareLauncherWindow, before the window opens
    CHECK(settings.Get("fullscreen", "") == saved);
    settings.Apply();
    CHECK(rex::cvar::GetFlagByName("fullscreen") == saved);
    fs::remove(scratch.player);
  }
}

void TestSelectedGamePersists(const Scratch& scratch) {
  std::printf("the saved selected game is the one the next start selects\n");
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.SelectGame(mua::GameId::kMua2);
  CHECK(settings.Save());
  CHECK(toml::parse_file(scratch.player.string())["mua_game"].value<std::string>() == "mua2");

  rex::cvar::LoadConfig(scratch.player);
  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  CHECK(reopened.SelectedGame() == mua::GameId::kMua2);

  rex::cvar::SetFlagByName("mua_game", "nonsense");
  mua::GameSettings unknown;
  unknown.Capture(scratch.player, scratch.shipped);
  CHECK(unknown.SelectedGame() == mua::DefaultGame().id);

  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  fs::remove(scratch.player);
}

void TestGameSettings(const Scratch& scratch) {
  std::printf("a game's own settings stay with that game and the shared ones do not move\n");
  const fs::path root = scratch.player.parent_path();
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  rex::cvar::SetFlagByName("mua_frame_rate", "60");
  rex::cvar::SetFlagByName("mua_show_render_resolution", "false");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  CHECK(settings.SelectedGame() == mua::GameId::kMua1Gold);

  settings.Set("mua_frame_rate", "120");
  settings.SelectGame(mua::GameId::kMua2);
  CHECK(settings.Get("mua_frame_rate") == "60");
  settings.Set("mua_frame_rate", "30");
  settings.Set("mua_show_render_resolution", "true");
  settings.SelectGame(mua::GameId::kMua1Gold);
  CHECK(settings.Get("mua_frame_rate") == "120");
  CHECK(settings.Get("mua_show_render_resolution") == "true");
  CHECK(settings.Get(mua::kSelectedGameKey) == "mua1-gold");

  CHECK(settings.Save());
  const toml::table gold = toml::parse_file((root / "mua1_gold" / "game_settings.toml").string());
  const toml::table mua2 = toml::parse_file((root / "mua2" / "game_settings.toml").string());
  CHECK(gold["mua_frame_rate"].value<int64_t>() == 120);
  CHECK(mua2["mua_frame_rate"].value<int64_t>() == 30);
  CHECK(!fs::exists(root / "mua1" / "game_settings.toml"));
  const toml::table shared = toml::parse_file(scratch.player.string());
  CHECK(shared["mua_show_render_resolution"].value<bool>() == true);
  CHECK(!shared.contains("mua_frame_rate"));

  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  CHECK(reopened.Get("mua_frame_rate") == "120");
  reopened.SelectGame(mua::GameId::kMua2);
  CHECK(reopened.Get("mua_frame_rate") == "30");
  reopened.SelectGame(mua::GameId::kMua1);
  CHECK(reopened.Get("mua_frame_rate") == "60");

  reopened.SelectGame(mua::GameId::kMua1Gold);
  reopened.Apply();
  CHECK(rex::cvar::GetFlagByName("mua_frame_rate") == "120");

  reopened.ResetToDefaults();
  CHECK(reopened.Get("mua_frame_rate") == "60");
  reopened.SelectGame(mua::GameId::kMua2);
  CHECK(reopened.Get("mua_frame_rate") == "60");
  CHECK(reopened.SelectedGame() == mua::GameId::kMua2);

  rex::cvar::SetFlagByName("mua_frame_rate", "60");
  rex::cvar::SetFlagByName("mua_show_render_resolution", "false");
  fs::remove_all(root / "mua1_gold");
  fs::remove_all(root / "mua2");
  fs::remove(scratch.player);
}

void TestGuestVideoModePin() {
  std::printf("the guest video mode does not follow the window\n");
  rex::cvar::SetFlagByName("window_width", "1920");
  rex::cvar::SetFlagByName("window_height", "1080");
  mua::PinGuestVideoMode();
  CHECK(rex::cvar::GetFlagByName("window_width") == "0");
  CHECK(rex::cvar::GetFlagByName("window_height") == "0");
  CHECK(rex::cvar::GetFlagByName("video_mode_width") == "1280");
  CHECK(rex::cvar::GetFlagByName("video_mode_height") == "720");
}

}  // namespace

int main() {
  const Scratch scratch;
  TestDefaults(scratch);
  TestSaveAndReload(scratch);
  TestUnregisteredIsRefused(scratch);
  TestReset(scratch);
  TestWindowChange(scratch);
  TestApply();
  TestDisplayModeStartup(scratch);
  TestSelectedGamePersists(scratch);
  TestGameSettings(scratch);
  TestGuestVideoModePin();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
