// Launcher quit behaviour: Escape cancels pop-ups and the keyboard capture and must not quit the
// launcher; the Quit button, Alt+F4 and the window close button still quit. The decision is pure and
// the wiring is checked from source (the launcher needs the running window).
// Run: mua_launcher_quit_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "launcher_quit.h"

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

void TestDecision() {
  std::printf("Escape does not quit, an explicit Quit does\n");
  // An explicit Quit (button, Alt+F4, window close) with no Escape is honoured.
  CHECK(mua::LauncherQuitAllowed(false));
  // Escape closes a pop-up or the capture and must never quit the launcher.
  CHECK(!mua::LauncherQuitAllowed(true));
}

void TestQuitWiring() {
  std::printf("the Quit result is filtered by Escape\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "mua::LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))"));
  // The explicit Quit still runs the quit callback.
  CHECK(Has(launcher, "Finish(cb_.quit);"));
}

void TestFooterHint() {
  std::printf("the footer advertises Alt+F4, not Escape\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "c.hints = \"Enter  Play     Alt+F4  Quit     Ctrl+S  Save\";"));
  CHECK(!Has(launcher, "Esc  Quit"));
  // The key capture helper still tells the player Escape cancels.
  CHECK(Has(launcher, "Click a key, then press the new one. Esc cancels."));
}

void TestCaptureEscape() {
  std::printf("Escape still cancels the capture\n");
  const std::string launcher = ReadSource("launcher.cpp");
  // A cancelled capture returns no key, so the row is left unchanged.
  CHECK(Has(launcher, "const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);"));
  CHECK(Has(launcher, "if (!key) {"));
}

void TestWindowClose() {
  std::printf("the window close button still follows the normal Quit path\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "app_context().QuitFromUIThread();"));
}

}  // namespace

int main() {
  TestDecision();
  TestQuitWiring();
  TestFooterHint();
  TestCaptureEscape();
  TestWindowClose();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
