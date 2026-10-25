// Launcher settings: defaults from the shipped file, pending edits, saving to the player's settings.toml, reading
// them back, reset, applying to the running cvars, the window-change rule, the guest video mode pin and the player
// names every game shares.
// Scratch folders only. Run: mua_settings_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <vector>

#include <toml++/toml.hpp>

#include <rex/cvar.h>

#include "graphics_adapters.h"
#include "platform.h"
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
  CHECK(!settings.WindowChanged());
  settings.Set(mua::kTearingSetting, rex::cvar::GetFlagByName(mua::kTearingSetting) == "true" ? "false" : "true");
  CHECK(settings.WindowChanged());
  settings.Set(mua::kTearingSetting, rex::cvar::GetFlagByName(mua::kTearingSetting));
  CHECK(!settings.WindowChanged());

  const std::vector<mua::GraphicsAdapter> adapters = mua::ListGraphicsAdapters();
  if (adapters.empty()) {
    return;
  }
  const std::string first = std::to_string(adapters.front().index);
  const std::string other = std::to_string(adapters.back().index);
  for (const std::string& created : {std::string("auto"), std::string(), first}) {
    rex::cvar::SetFlagByName("mua_graphics_adapter", created);
    mua::GameSettings gpu;
    gpu.Capture(scratch.player, scratch.shipped);
    gpu.MarkWindowCreated();
    for (const std::string& equivalent :
         {std::string("auto"), std::string(), std::string("invalid"), std::string("99"), first}) {
      gpu.Set("mua_graphics_adapter", equivalent);
      CHECK(!gpu.WindowChanged());
    }
    if (adapters.size() > 1) {
      gpu.Set("mua_graphics_adapter", other);
      CHECK(gpu.WindowChanged());
    }
  }
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

void TestLauncherHandoffIsOneShot(const Scratch& scratch) {
  std::printf("the launcher handoff is consumed once and never saved\n");
  CHECK(!mua::TakeLauncherHandoff());  // a program started directly has no launcher
  rex::cvar::SetFlagByName("mua_launcher_handoff", "true");
  CHECK(mua::TakeLauncherHandoff());
  CHECK(!mua::TakeLauncherHandoff());
  CHECK(rex::cvar::GetFlagByName("mua_launcher_handoff") == "false");

  fs::create_directories(scratch.player.parent_path());
  rex::cvar::SetFlagByName("mua_launcher_handoff", "true");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.Set("mua_show_fps", "false");
  CHECK(settings.Save());
  std::string text;
  {
    std::ifstream saved(scratch.player);
    text.assign((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
  }
  CHECK(text.find("mua_show_fps") != std::string::npos);
  CHECK(text.find("mua_launcher_handoff") == std::string::npos);
  rex::cvar::ResetToDefault("mua_launcher_handoff");
  rex::cvar::ResetToDefault("mua_show_fps");
  fs::remove(scratch.player);
}

void TestFailedStartReportsError() {
  std::printf("a program that cannot start reports it\n");
  CHECK(!mua::RelaunchExecutable(fs::temp_directory_path() / "mua_missing_program.exe", L"--mua_skip_launcher=true"));
}

void TestSelectedGamePersists(const Scratch& scratch) {
  std::printf("the saved selected game is the one the next start selects\n");
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  {
    mua::GameSettings fresh;
    fresh.Capture(scratch.player, scratch.shipped);
    CHECK(fresh.SelectedGame() == mua::DefaultGame().id);
  }
  struct Choice {
    mua::GameId id;
    const char* key;
  };
  for (const Choice choice : {Choice{mua::GameId::kMua1, "mua1"}, Choice{mua::GameId::kMua1Gold, "mua1-gold"},
                              Choice{mua::GameId::kMua2, "mua2"}}) {
    mua::GameSettings settings;
    settings.Capture(scratch.player, scratch.shipped);
    settings.SelectGame(choice.id);
    CHECK(settings.Save());
    CHECK(toml::parse_file(scratch.player.string())["mua_game"].value<std::string>() == choice.key);

    // A fresh start has not set mua_game itself, so the saved file decides.
    rex::cvar::ResetToDefault("mua_game");
    rex::cvar::LoadConfig(scratch.player);
    mua::GameSettings reopened;
    reopened.Capture(scratch.player, scratch.shipped);
    CHECK(reopened.SelectedGame() == choice.id);
  }

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

void TestAspectRatioPerGame(const Scratch& scratch) {
  std::printf("each game keeps its own aspect ratio\n");
  const fs::path root = scratch.player.parent_path();
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  rex::cvar::SetFlagByName("mua_aspect_ratio", "16:9");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  settings.MarkWindowCreated();

  settings.SelectGame(mua::GameId::kMua1);
  CHECK(settings.Get("mua_aspect_ratio") == "16:9");
  settings.Set("mua_aspect_ratio", "21:9");
  settings.SelectGame(mua::GameId::kMua1Gold);
  CHECK(settings.Get("mua_aspect_ratio") == "16:9");
  settings.Set("mua_aspect_ratio", "32:9");
  settings.SelectGame(mua::GameId::kMua2);
  CHECK(settings.Get("mua_aspect_ratio") == "16:9");
  settings.Set("mua_aspect_ratio", "4:3");
  settings.SelectGame(mua::GameId::kMua1);
  CHECK(settings.Get("mua_aspect_ratio") == "21:9");
  CHECK(!settings.WindowChanged());

  CHECK(settings.Save());
  CHECK(toml::parse_file((root / "mua1" / "game_settings.toml").string())["mua_aspect_ratio"].value<std::string>() ==
        "21:9");
  CHECK(toml::parse_file((root / "mua1_gold" / "game_settings.toml").string())["mua_aspect_ratio"]
            .value<std::string>() == "32:9");
  CHECK(toml::parse_file((root / "mua2" / "game_settings.toml").string())["mua_aspect_ratio"].value<std::string>() ==
        "4:3");
  CHECK(!toml::parse_file(scratch.player.string()).contains("mua_aspect_ratio"));

  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  reopened.SelectGame(mua::GameId::kMua1Gold);
  CHECK(reopened.Get("mua_aspect_ratio") == "32:9");
  reopened.SelectGame(mua::GameId::kMua2);
  CHECK(reopened.Get("mua_aspect_ratio") == "4:3");
  reopened.SelectGame(mua::GameId::kMua1);
  CHECK(reopened.Get("mua_aspect_ratio") == "21:9");

  reopened.SelectGame(mua::GameId::kMua2);
  reopened.Apply();
  CHECK(rex::cvar::GetFlagByName("mua_aspect_ratio") == "4:3");
  reopened.SelectGame(mua::GameId::kMua1Gold);
  reopened.Apply();
  CHECK(rex::cvar::GetFlagByName("mua_aspect_ratio") == "32:9");

  reopened.ResetToDefaults();
  for (const mua::GameId game : {mua::GameId::kMua1, mua::GameId::kMua1Gold, mua::GameId::kMua2}) {
    reopened.SelectGame(game);
    CHECK(reopened.Get("mua_aspect_ratio") == "16:9");
  }

  std::ofstream(root / "mua2" / "game_settings.toml", std::ios::trunc) << "mua_aspect_ratio = \"garbage\"\n";
  mua::GameSettings invalid;
  invalid.Capture(scratch.player, scratch.shipped);
  invalid.SelectGame(mua::GameId::kMua2);
  CHECK(invalid.Get("mua_aspect_ratio") == "16:9");

  rex::cvar::SetFlagByName("mua_aspect_ratio", "16:9");
  fs::remove_all(root / "mua1");
  fs::remove_all(root / "mua1_gold");
  fs::remove_all(root / "mua2");
  fs::remove(scratch.player);
}

void TestAspectRatioVideoMode() {
  std::printf("the guest video mode takes the aspect ratio's shape after the pin\n");
  struct Row {
    const char* aspect;
    const char* width;
    const char* height;
  };
  const Row rows[] = {{"16:9", "1280", "720"}, {"16:10", "1152", "720"}, {"21:9", "1680", "720"},
                      {"32:9", "2560", "720"}, {"4:3", "960", "720"},    {"5:4", "896", "720"},
                      {"garbage", "1280", "720"}};
  for (const Row& row : rows) {
    rex::cvar::SetFlagByName("window_width", "1920");
    rex::cvar::SetFlagByName("window_height", "1080");
    rex::cvar::SetFlagByName("mua_aspect_ratio", row.aspect);
    mua::PinGuestVideoMode();
    mua::ApplyAspectRatio();
    CHECK(rex::cvar::GetFlagByName("video_mode_width") == row.width);
    CHECK(rex::cvar::GetFlagByName("video_mode_height") == row.height);
    CHECK(rex::cvar::GetFlagByName("window_width") == "0");
  }
  rex::cvar::SetFlagByName("mua_aspect_ratio", "16:9");
  mua::PinGuestVideoMode();
}

void TestUpscalingPerGame(const Scratch& scratch) {
  std::printf("the upscaler is the game's own and sets the presenter effect only when on\n");
  const fs::path root = scratch.player.parent_path();
  rex::cvar::SetFlagByName("mua_game", "mua1");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  CHECK(settings.SelectedGame() == mua::GameId::kMua1);
  CHECK(settings.Get("mua_upscaler") == "off");
  CHECK(settings.Get("mua_upscale_quality") == "native");
  settings.Set("mua_upscaler", "fsr");
  settings.Set("mua_upscale_quality", "balanced");
  settings.SelectGame(mua::GameId::kMua1Gold);
  CHECK(settings.Get("mua_upscaler") == "off");
  CHECK(settings.Get("mua_upscale_quality") == "native");
  settings.SelectGame(mua::GameId::kMua2);
  CHECK(settings.Get("mua_upscaler") == "off");
  CHECK(settings.Get("mua_upscale_quality") == "native");
  settings.SelectGame(mua::GameId::kMua1Gold);

  const std::string effect = rex::cvar::GetFlagByName("present_effect");
  const std::string window_width = rex::cvar::GetFlagByName("window_width");
  mua::ApplyUpscaler(settings);
  CHECK(rex::cvar::GetFlagByName("present_effect") == effect);

  CHECK(settings.Save());
  const toml::table mua1 = toml::parse_file((root / "mua1" / "game_settings.toml").string());
  CHECK(mua1["mua_upscaler"].value<std::string>() == "fsr");
  CHECK(mua1["mua_upscale_quality"].value<std::string>() == "balanced");
  CHECK(!fs::exists(root / "mua1_gold" / "game_settings.toml"));
  CHECK(!toml::parse_file(scratch.player.string()).contains("mua_upscaler"));

  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  reopened.SelectGame(mua::GameId::kMua1);
  CHECK(reopened.Get("mua_upscale_quality") == "balanced");
  mua::ApplyUpscaler(reopened);
  CHECK(rex::cvar::GetFlagByName("present_effect") == "fsr");
  reopened.ApplyGameSettings();
  mua::LogUpscaling();
  CHECK(rex::cvar::GetFlagByName("window_width") == window_width);
  CHECK(rex::cvar::GetFlagByName("mua_upscale_quality") == "balanced");

  rex::cvar::SetFlagByName("present_effect", effect);
  rex::cvar::SetFlagByName("mua_upscaler", "off");
  rex::cvar::SetFlagByName("mua_upscale_quality", "native");
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  fs::remove_all(root / "mua1");
  fs::remove(scratch.player);
}

void TestPlayerNamesAreShared(const Scratch& scratch) {
  std::printf("the player names are shared by the three games, saved once in settings.toml\n");
  const fs::path root = scratch.player.parent_path();
  const char* const keys[] = {"mua_player_1_name", "mua_player_2_name", "mua_player_3_name", "mua_player_4_name"};
  const mua::GameId games[] = {mua::GameId::kMua1, mua::GameId::kMua1Gold, mua::GameId::kMua2};
  const auto names_in_every_game = [&](mua::GameSettings& settings, const std::vector<std::string>& expected) {
    for (const mua::GameId game : games) {
      settings.SelectGame(game);
      for (size_t i = 0; i < 4; ++i) {
        CHECK(settings.Get(keys[i]) == expected[i]);
      }
    }
  };
  rex::cvar::SetFlagByName("mua_game", "mua1");
  mua::GameSettings settings;
  settings.Capture(scratch.player, scratch.shipped);
  CHECK(settings.SelectedGame() == mua::GameId::kMua1);
  names_in_every_game(settings, {"Player 1", "Player 2", "Player 3", "Player 4"});

  settings.SelectGame(mua::GameId::kMua1);
  settings.Set("mua_player_1_name", "Saulo");
  settings.SelectGame(mua::GameId::kMua1Gold);
  CHECK(settings.Get("mua_player_1_name") == "Saulo");
  settings.SelectGame(mua::GameId::kMua2);
  CHECK(settings.Get("mua_player_1_name") == "Saulo");
  settings.Set("mua_player_2_name", "Jo\xC3\xA3o");
  settings.SelectGame(mua::GameId::kMua1);
  CHECK(settings.Get("mua_player_2_name") == "Jo\xC3\xA3o");
  settings.Set("mua_player_3_name", "Peter");
  settings.Set("mua_player_4_name", "Logan");
  CHECK(settings.Save());

  const toml::table shared = toml::parse_file(scratch.player.string());
  CHECK(shared["mua_player_1_name"].value<std::string>() == "Saulo");
  CHECK(shared["mua_player_2_name"].value<std::string>() == "Jo\xC3\xA3o");
  CHECK(shared["mua_player_3_name"].value<std::string>() == "Peter");
  CHECK(shared["mua_player_4_name"].value<std::string>() == "Logan");
  for (const char* folder : {"mua1", "mua1_gold", "mua2"}) {
    const fs::path own = root / folder / "game_settings.toml";
    if (fs::exists(own)) {
      const toml::table table = toml::parse_file(own.string());
      for (const char* key : keys) {
        CHECK(!table.contains(key));
      }
    }
  }

  rex::cvar::LoadConfig(scratch.player);
  mua::GameSettings reopened;
  reopened.Capture(scratch.player, scratch.shipped);
  names_in_every_game(reopened, {"Saulo", "Jo\xC3\xA3o", "Peter", "Logan"});

  reopened.SelectGame(mua::GameId::kMua2);
  reopened.Set("mua_player_2_name", "Tony");
  CHECK(reopened.Save());
  names_in_every_game(reopened, {"Saulo", "Tony", "Peter", "Logan"});
  CHECK(toml::parse_file(scratch.player.string())["mua_player_2_name"].value<std::string>() == "Tony");

  // A game's own program starts from the saved file alone.
  rex::cvar::ResetToDefault("mua_player_2_name");
  mua::LoadPlayerSettings(scratch.player);
  CHECK(rex::cvar::GetFlagByName("mua_player_2_name") == "Tony");
  mua::ApplyPlayerNames();
  if (rex::cvar::GetFlagInfo("local_player_1_name")) {
    CHECK(rex::cvar::GetFlagByName("local_player_1_name") == "Saulo");
    CHECK(rex::cvar::GetFlagByName("local_player_2_name") == "Tony");
    CHECK(rex::cvar::GetFlagByName("local_player_3_name") == "Peter");
    CHECK(rex::cvar::GetFlagByName("local_player_4_name") == "Logan");
  } else {
    std::printf("  local_player_<n>_name: not in this runtime, skipped\n");
  }

  reopened.Set("mua_player_3_name", std::string(16, 'a'));
  CHECK(reopened.Save());
  CHECK(toml::parse_file(scratch.player.string())["mua_player_3_name"].value<std::string>() == "Player 3");
  CHECK(reopened.Get("mua_player_3_name") == "Player 3");

  reopened.ResetToDefaults();
  names_in_every_game(reopened, {"Player 1", "Player 2", "Player 3", "Player 4"});

  for (const char* key : keys) {
    rex::cvar::ResetToDefault(key);
  }
  rex::cvar::SetFlagByName("mua_game", "mua1-gold");
  fs::remove_all(root / "mua1");
  fs::remove_all(root / "mua1_gold");
  fs::remove_all(root / "mua2");
  fs::remove(scratch.player);
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
  TestLauncherHandoffIsOneShot(scratch);
  TestFailedStartReportsError();
  TestSelectedGamePersists(scratch);
  TestGameSettings(scratch);
  TestGuestVideoModePin();
  TestAspectRatioPerGame(scratch);
  TestAspectRatioVideoMode();
  TestUpscalingPerGame(scratch);
  TestPlayerNamesAreShared(scratch);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
