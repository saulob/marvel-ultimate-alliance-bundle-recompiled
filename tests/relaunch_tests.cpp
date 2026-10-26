// Relaunch tests: the launcher starts the game's own program with the saved settings, through the AppImage on Linux.
// Pure logic and source checks; the restart itself needs a running game.
// Run: mua_relaunch_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "relaunch_command.h"

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
  CHECK(!Has(app, "RelaunchSelf"));
  CHECK(Has(app, "mua::RelaunchExecutable(executable, L\"--mua_skip_launcher=true\")"));
  CHECK(Has(app, "mua::HideNativeWindow(window()->GetNativeWindowHandle())"));
  CHECK(Has(app, "std::_Exit(0);"));  // the launcher leaves once the game's program is started
}

using Args = std::vector<std::string>;

constexpr const char* kMountedLauncher = "/tmp/.mount_MarvelAbCdEf/usr/bin/marvel_ultimate_alliance_bundle";
constexpr const char* kMountedGame = "/tmp/.mount_MarvelAbCdEf/usr/bin/marvel_ultimate_alliance_mua2";
constexpr const char* kAppImage = "/home/deck/Games/Marvel Ultimate Alliance/MarvelUltimateAlliance-v0.1.0-x86_64.AppImage";
const Args kExtra = {"--mua_skip_launcher=true", "--mua_launcher_handoff=true"};

void TestAppImageSet() {
  std::printf("a game started from the AppImage runs from a new mount of the same file\n");
  const auto command = mua::MakeRelaunchCommand({kMountedLauncher, "--mua_launcher=true"}, kAppImage, kMountedGame, kExtra);
  CHECK(command.has_value());
  CHECK(command->executable == kAppImage);  // the file the player started, not the mount that closes
  CHECK((command->argv == Args{kAppImage, "--mua_launcher=true", kExtra[0], kExtra[1]}));
  CHECK(command->appimage_program == "marvel_ultimate_alliance_mua2");
  CHECK(!Has(command->executable, ".mount_"));
  CHECK(!Has(command->argv.front(), ".mount_"));
}

void TestAppImageUnset() {
  std::printf("outside an AppImage the game's program starts directly\n");
  const std::string game = "/home/user/my games/mua/marvel_ultimate_alliance_mua1";
  for (const char* appimage : {static_cast<const char*>(nullptr), ""}) {
    const auto command = mua::MakeRelaunchCommand({"./marvel_ultimate_alliance_bundle"}, appimage, game, kExtra);
    CHECK(command.has_value());
    CHECK(command->executable == game);
    CHECK((command->argv == Args{game, kExtra[0], kExtra[1]}));
    CHECK(command->appimage_program.empty());
  }
}

void TestNoCommand() {
  std::printf("no command line or no program starts nothing\n");
  CHECK(!mua::MakeRelaunchCommand({}, kAppImage, kMountedGame, kExtra).has_value());
  CHECK(!mua::MakeRelaunchCommand({kMountedLauncher}, kAppImage, "", kExtra).has_value());
}

void TestAppRunSelectsOnlyBundlePrograms() {
  std::printf("AppRun runs a named game program, else the launcher, and clears the name\n");
  const std::string apprun = ReadFile(std::string(MUA_SRC_DIR) + "/../packaging/linux/AppRun");
  CHECK(Has(apprun, mua::kAppImageProgramVariable));
  for (const char* program : {"marvel_ultimate_alliance_mua1", "marvel_ultimate_alliance_mua1_gold",
                              "marvel_ultimate_alliance_mua2"}) {
    CHECK(Has(apprun, program));
  }
  CHECK(Has(apprun, "*) PROGRAM=marvel_ultimate_alliance_bundle ;;"));
  CHECK(Has(apprun, "unset MUA_APPIMAGE_PROGRAM"));
  CHECK(Has(apprun, "exec \"$HERE/usr/bin/$PROGRAM\" \"$@\""));
  const std::string platform = ReadFile(std::string(MUA_SRC_DIR) + "/platform.cpp");
  CHECK(Has(platform, "posix_spawn(&pid, command->executable.c_str()"));  // no shell
  CHECK(Has(platform, "std::getenv(\"APPIMAGE\")"));
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
  TestAppImageSet();
  TestAppImageUnset();
  TestNoCommand();
  TestAppRunSelectsOnlyBundlePrograms();
  TestSkipLauncherFlag();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
