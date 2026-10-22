// Cursor last-input behavior and window title tests: the state machine and controller filter with
// synthetic input, the title text and its wiring. The window calls need the running game.
// Run: mua_window_host_tests

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "aspect_ratio.h"
#include "cursor_input.h"
#include "frame_counter.h"
#include "window_host.h"
#include "window_title.h"

using namespace mua;

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string Between(const std::string& source, const char* from, const char* to) {
  const size_t start = source.find(from);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find(to, start + 1);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

rex::input::X_INPUT_GAMEPAD Pad(uint16_t buttons = 0, int16_t thumb_lx = 0, int16_t thumb_ly = 0,
                                int16_t thumb_rx = 0, int16_t thumb_ry = 0,
                                uint8_t left_trigger = 0, uint8_t right_trigger = 0) {
  rex::input::X_INPUT_GAMEPAD pad{};
  pad.buttons.set(buttons);
  pad.thumb_lx.set(thumb_lx);
  pad.thumb_ly.set(thumb_ly);
  pad.thumb_rx.set(thumb_rx);
  pad.thumb_ry.set(thumb_ry);
  pad.left_trigger = left_trigger;
  pad.right_trigger = right_trigger;
  return pad;
}

void TestCursorHider() {
  std::printf("cursor hider\n");
  const std::optional<bool> hide = false, show = true;

  CursorHider initial;
  CHECK(initial.visible());
  CHECK(initial.KeyboardActivity() == hide);
  CHECK(initial.ControllerActivity() == std::nullopt);

  CursorHider last;
  CHECK(last.MouseActivity() == std::nullopt);
  CHECK(last.KeyboardActivity() == hide);
  CHECK(last.ControllerActivity() == std::nullopt);
  CHECK(last.MouseActivity() == show);
  CHECK(last.ControllerActivity() == hide);

  CursorHider overlay;
  CHECK(overlay.KeyboardActivity() == hide);
  CHECK(overlay.SetOverlayOpen(true) == show);
  CHECK(overlay.SetOverlayOpen(false) == hide);
  CHECK(overlay.MouseActivity() == show);
  CHECK(overlay.SetOverlayOpen(true) == std::nullopt);
  CHECK(overlay.visible());

  CursorHider focus;
  CHECK(focus.ControllerActivity() == hide);
  CHECK(focus.SetFocused(false) == show);
  CHECK(focus.SetFocused(true) == hide);
  CHECK(focus.MouseActivity() == show);

  CursorHider combined;
  CHECK(combined.KeyboardActivity() == hide);
  CHECK(combined.SetOverlayOpen(true) == show);
  CHECK(combined.SetFocused(false) == std::nullopt);
  CHECK(combined.SetFocused(true) == std::nullopt);
  CHECK(combined.visible());
  CHECK(combined.SetOverlayOpen(false) == hide);
  CHECK(combined.MouseActivity() == show);
}

void TestControllerActivityFilter() {
  std::printf("controller activity filter\n");
  using namespace rex::input;

  ControllerActivityFilter buttons;
  CHECK(!buttons.Update(Pad()));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_A)));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_A | X_INPUT_GAMEPAD_B)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(buttons.Update(Pad(X_INPUT_GAMEPAD_B | X_INPUT_GAMEPAD_DPAD_UP)));
  CHECK(!buttons.Update(Pad(X_INPUT_GAMEPAD_B)));
  CHECK(!buttons.Update(Pad()));

  ControllerActivityFilter thumbs;
  CHECK(!thumbs.Update(Pad()));
  CHECK(!thumbs.Update(Pad(0, kThumbDeadzone - 1, 0, 0, 0)));
  CHECK(thumbs.Update(Pad(0, kThumbDeadzone, 0, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, kThumbDeadzone + 2000, 0, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, 0, 0, 0, 0)));
  CHECK(thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, 0)));
  CHECK(!thumbs.Update(Pad(0, 0, -kThumbDeadzone, kThumbDeadzone - 1, 0)));
  CHECK(thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, kThumbDeadzone)));
  CHECK(!thumbs.Update(Pad(0, 0, -kThumbDeadzone, 0, kThumbDeadzone + 4000)));

  ControllerActivityFilter triggers;
  CHECK(!triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold - 1, kTriggerThreshold - 1)));
  CHECK(triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!triggers.Update(Pad(0, 0, 0, 0, 0, kTriggerThreshold, 0)));
  CHECK(!triggers.Update(Pad()));
  CHECK(triggers.Update(Pad(0, 0, 0, 0, 0, 0, kTriggerThreshold)));

  ControllerActivityFilter drift;
  for (int16_t value : {0, 100, -100, 1000, -1000, kThumbDeadzone - 1, -(kThumbDeadzone - 1), 0}) {
    CHECK(!drift.Update(Pad(0, value, 0, 0, 0)));
  }
}

void TestWindowTitle() {
  std::printf("window title\n");
  const std::string game = "Marvel Ultimate Alliance Gold Edition Recompiled";
  const std::optional<std::string> resolution = "3360x1440";
  CHECK(WindowTitleText(game, std::nullopt, std::nullopt) == game);
  CHECK(WindowTitleText(game, resolution, std::nullopt) == game + " | 3360x1440");
  CHECK(WindowTitleText(game, std::nullopt, 60.0) == game + " | FPS: 60.00");
  CHECK(WindowTitleText(game, resolution, 59.994) == game + " | 3360x1440 | FPS: 59.99");
}

void TestFrameCounter() {
  std::printf("frame counter text\n");
  GuestFrameStats stats;
  CHECK(!FrameCounterVisible(true, stats));
  stats = {60.0, 16.67, 120};
  CHECK(FrameCounterText(stats) == "60 FPS  16.7 ms");
  CHECK(FrameCounterVisible(true, stats));
  CHECK(!FrameCounterVisible(false, stats));
}

void TestRenderResolution() {
  std::printf("render resolution text\n");
  CHECK(RenderResolutionText(1, AspectRatio::k16x9) == "1280x720");
  CHECK(RenderResolutionText(2, AspectRatio::k16x9) == "2560x1440");
  CHECK(RenderResolutionText(3, AspectRatio::k16x9) == "3840x2160");
  CHECK(RenderResolutionText(4, AspectRatio::k16x9) == "5120x2880");
}

void TestWindowModes() {
  std::printf("window modes\n");
  // The same rule runs in Windowed and Borderless Fullscreen: the host must not gate hiding on the mode.
  const std::string host = ReadSource("window_host.cpp");
  CHECK(!Has(Between(host, "void UpdateCursor()", "void RestoreCursor()"), "IsFullscreen"));

  CursorHider windowed;
  CursorHider fullscreen;
  const std::optional<bool> hide = false, show = true;
  CHECK(windowed.ControllerActivity() == hide);
  CHECK(fullscreen.ControllerActivity() == hide);
  CHECK(windowed.MouseActivity() == show);
  CHECK(fullscreen.MouseActivity() == show);
}

void TestCursorWiring() {
  std::printf("cursor wiring\n");
  const std::string host = ReadSource("window_host.cpp");
  CHECK(Has(host, "void OnMouseMove(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseDown(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnMouseWheel(rex::ui::MouseEvent& e) override"));
  CHECK(Has(host, "void OnKeyDown(rex::ui::KeyEvent& e) override"));
  CHECK(Has(host, "void OnLostFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "void OnGotFocus(rex::ui::UISetupEvent&) override"));
  CHECK(Has(host, "SetInputActivityCallback(OnControllerActivity)"));
  CHECK(Has(host, "CreateCursorSync"));
  CHECK(!Has(host, "kCursorHideDelay"));
  CHECK(!Has(host, "CursorHider::Clock"));
  CHECK(!Has(host, "last_motion_"));
  CHECK(!Has(host, "CursorTick"));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "AttachWindowHost(window(), app_context(), [this]"));
  CHECK(Has(app, "WantCaptureMouse"));
  CHECK(Has(app, "CreateCursorSync(imgui_drawer())"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(!Has(launcher, "AttachWindowHost"));
}

void TestInputActivityPatch() {
  std::printf("input activity patch\n");
  std::ifstream in(std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/0047-input-activity-callback.patch",
                   std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const std::string patch = text.str();
  CHECK(Has(patch, "SetInputActivityCallback"));
  CHECK(Has(patch, "NotifyInputActivity"));
  CHECK(Has(patch, "std::memcmp"));
  CHECK(Has(patch, "g_input_activity_callback(id, gamepad)"));
}

void TestWiring() {
  std::printf("render resolution title wiring\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_show_render_resolution, false"));
  CHECK(Has(settings, "\"mua_show_render_resolution\","));  // saved and reset with the other game settings

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"mua_show_render_resolution\", false, \"Off\", \"On\""));
  CHECK(Has(launcher, "\"mua_show_fps\", true, \"Off\", \"On\""));
  CHECK(Has(launcher, "\"mua_show_frame_counter\", false, \"Hidden\", \"Shown\""));
  CHECK(Has(launcher, "{\"Gameplay\", \"What the window shows while you play.\""));
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_show_fps, true, \"Marvel Ultimate Alliance\""));
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_show_frame_counter, false, \"Marvel Ultimate Alliance\""));
}

void TestHotkeyWiring() {
  std::printf("hotkey wiring: F11 is a host listener above the guest input, F2 toggles the frame counter\n");
  const std::string host = ReadSource("window_host.cpp");
  CHECK(Has(host, "constexpr size_t kWindowHostZOrder = 1000;"));
  CHECK(Has(host, "window->AddInputListener(&window_host, kWindowHostZOrder)"));
  CHECK(Has(host, "e.virtual_key() == rex::ui::VirtualKey::kF11"));
  CHECK(Has(host, "e.set_handled(true);"));
  CHECK(Has(host, "const bool fullscreen = !g_window->IsFullscreen();"));
  CHECK(!Has(host, "mnk_mode"));

  const std::string frame = ReadSource("frame_counter.cpp");
  CHECK(Has(frame, "RegisterBind(\"bind_mua_frame_counter\", \"F2\""));
  CHECK(Has(frame, "SetFlagByName(\"mua_show_frame_counter\""));
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "mua::CreateFrameCounter(imgui_drawer())"));

  CHECK(Has(host, "REX_HOOK_RAW(__imp__VdSwap)"));
  CHECK(Has(host, "HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>"));
  CHECK(Has(host, "CountGuestFrame();"));
  CHECK(Has(host, "REXCVAR_GET(mua_show_fps)"));
}

}  // namespace

int main() {
  TestCursorHider();
  TestControllerActivityFilter();
  TestWindowTitle();
  TestFrameCounter();
  TestRenderResolution();
  TestWindowModes();
  TestCursorWiring();
  TestInputActivityPatch();
  TestWiring();
  TestHotkeyWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
