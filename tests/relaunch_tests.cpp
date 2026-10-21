// Relaunch tests: the launcher starts the same program again with the saved settings. Source checks; the restart
// itself needs a running game.
// Run: mua_relaunch_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestWiring() {
  std::printf("relaunch wiring\n");
  const std::string platform = ReadFile(std::string(MUA_SRC_DIR) + "/platform.cpp");
  CHECK(Has(platform, "std::wstring command_line = GetCommandLineW();"));
  CHECK(Has(platform, "command_line += extra_args;"));
  CHECK(Has(platform, "CreateProcessW(exe, command_line.data()"));  // no shell
  CHECK(Has(platform, "BeginSessionLogHandoff();"));
  CHECK(Has(platform, "CancelSessionLogHandoff();"));
  CHECK(!Has(platform, "system("));
  CHECK(!Has(platform, "popen("));

  const std::string app = ReadFile(std::string(MUA_SRC_DIR) + "/marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "mua::RelaunchSelf(L\"--mua_skip_launcher=true\")"));
  CHECK(Has(app, "std::_Exit(0);"));  // the old process leaves once the new one is started
}

void TestSkipLauncherFlag() {
  std::printf("the relaunch flag skips the launcher once\n");
  const std::string settings = ReadFile(std::string(MUA_SRC_DIR) + "/settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_skip_launcher, false"));
  const std::string launcher = ReadFile(std::string(MUA_SRC_DIR) + "/launcher.cpp");
  CHECK(Has(launcher, "rex::cvar::ResetToDefault(\"mua_skip_launcher\");"));
  CHECK(Has(launcher, "REXCVAR_GET(mua_skip_launcher)"));
}

}  // namespace

int main() {
  TestWiring();
  TestSkipLauncherFlag();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
