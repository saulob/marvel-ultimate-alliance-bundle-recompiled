// Launcher to game handoff: the game's own program is found and started on each platform, starts restored, keeps its
// own window settings and takes the foreground once. Policy checks plus source checks; the native calls need a real
// window and a second process.
// Run: mua_launcher_handoff_tests

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game_definition.h"
#include "game_handoff.h"
#include "relaunch_command.h"
#include "window_placement.h"

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

const std::filesystem::path kSrcDir = MUA_SRC_DIR;

// Working trees check sources out with CRLF line endings; the markers below use LF.
std::string ReadSource(const char* name) {
  std::ifstream in(kSrcDir / name, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::erase(text, '\r');
  return text;
}

bool Has(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

// The text from the first `from` to the next `to` after it; empty when either is missing.
std::string Region(const std::string& text, std::string_view from, std::string_view to) {
  const size_t begin = text.find(from);
  if (begin == std::string::npos) {
    return {};
  }
  const size_t end = text.find(to, begin + from.size());
  return end == std::string::npos ? std::string() : text.substr(begin, end - begin);
}

// True when every needle is present and they appear in the given order.
bool InOrder(const std::string& text, std::initializer_list<std::string_view> needles) {
  size_t at = 0;
  for (const std::string_view needle : needles) {
    const size_t found = text.find(needle, at);
    if (found == std::string::npos) {
      return false;
    }
    at = found + needle.size();
  }
  return true;
}

void TestShowCommand() {
  std::printf("the game's program starts restored, never minimized\n");
  CHECK(kHandoffShowCommand == 1);  // SW_SHOWNORMAL
  CHECK(!ShowCommandMinimizes(kHandoffShowCommand));
  for (const int minimizing : {2, 6, 7, 11}) {  // SHOWMINIMIZED, MINIMIZE, SHOWMINNOACTIVE, FORCEMINIMIZE
    CHECK(ShowCommandMinimizes(minimizing));
  }
  for (const int other : {0, 1, 3, 4, 5, 8, 9, 10}) {
    CHECK(!ShowCommandMinimizes(other));
  }

  const std::string platform = ReadSource("platform.cpp");
  const std::string relaunch = Region(platform, "bool RelaunchExecutable(", "void CompleteLauncherHandoff(");
  CHECK(!relaunch.empty());
  CHECK(Has(relaunch, "startup.dwFlags = STARTF_USESHOWWINDOW;"));
  CHECK(Has(relaunch, "startup.wShowWindow = kHandoffShowCommand;"));
  CHECK(Has(platform, "static_assert(kHandoffShowCommand == SW_SHOWNORMAL);"));
  for (const char* minimizing : {"SW_SHOWMINIMIZED", "SW_MINIMIZE", "SW_SHOWMINNOACTIVE", "SW_FORCEMINIMIZE"}) {
    CHECK(!Has(platform, minimizing));
  }
}

void TestForegroundHandoff() {
  std::printf("foreground permission is granted once the game's program exists\n");
  const std::string platform = ReadSource("platform.cpp");
  const std::string relaunch = Region(platform, "bool RelaunchExecutable(", "void CompleteLauncherHandoff(");
  CHECK(InOrder(relaunch, {"CreateProcessW(", "AllowSetForegroundWindow(process.dwProcessId)",
                           "foreground_granted=", "CloseHandle(process.hProcess);"}));
  CHECK(Count(platform, "AllowSetForegroundWindow(") == 1);

  const std::string complete = Region(platform, "void CompleteLauncherHandoff(", "\n}\n");
  CHECK(!complete.empty());
  CHECK(Count(complete, "SetForegroundWindow(") == 1);
  CHECK(Has(complete, "if (restored) {"));
  CHECK(InOrder(complete, {"BringWindowToTop(window);", "SetForegroundWindow(window)"}));
  CHECK(!Has(complete, "for (") && !Has(complete, "while (") && !Has(complete, "Sleep("));
  CHECK(!Has(platform, "SetTimer(") && !Has(platform, "AttachThreadInput("));
}

void TestNoWaitForTheGame() {
  std::printf("the launcher never waits for the game's window\n");
  const std::string platform = ReadSource("platform.cpp");
  const std::string relaunch = Region(platform, "bool RelaunchExecutable(", "void CompleteLauncherHandoff(");
  for (const char* wait : {"WaitForSingleObject", "WaitForMultipleObjects", "WaitForInputIdle", "MsgWaitFor", "Sleep(",
                           "CreateEventW", "OpenEventW", "SetEvent"}) {
    CHECK(!Has(relaunch, wait));
    CHECK(!Has(platform, wait));
  }
  CHECK(!Has(platform, "window_wait") && !Has(platform, "GameWindowReady"));
  CHECK(!Has(ReadSource("game_handoff.h"), "Wait"));
}

void TestShutdownOrder() {
  std::printf("a started program sends the launcher straight to its hide and exit\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string start = Region(app, "callbacks.start_other_executable", "callbacks.quit");
  CHECK(!start.empty());
  CHECK(InOrder(start, {"if (!StartRuntime(game)) {", "return false;",
                        "mua::HideNativeWindow(window()->GetNativeWindowHandle())", "ExitLauncher()"}));
  CHECK(!Has(start, "chrono") && !Has(start, "Wait"));
  CHECK(Has(app, "mua::RelaunchExecutable(program, L\"--mua_skip_launcher=true\")"));
  CHECK(Has(app, "std::_Exit(0);"));
  CHECK(Count(app, "mua::RelaunchExecutable(") == 1);
  CHECK(Has(app, "StartRuntimeAndExit(library_->State(selected));"));
}

void TestFailedStartLeavesLauncherUsable() {
  std::printf("a program that cannot start leaves the launcher open with an error\n");
  const std::string platform = ReadSource("platform.cpp");
  const std::string relaunch = Region(platform, "bool RelaunchExecutable(", "void CompleteLauncherHandoff(");
  CHECK(InOrder(relaunch, {"if (!CreateProcessW(", "REXLOG_ERROR(\"Relaunch failed ({})\"",
                           "CancelSessionLogHandoff();", "return false;", "AllowSetForegroundWindow("}));

  const std::string launcher = ReadSource("launcher.cpp");
  const std::string start = Region(launcher, "void StartGame() {", "// Drawn outside the disabled frame");
  CHECK(!start.empty());
  CHECK(InOrder(start, {"cb_.start_other_executable(Selected())", "Finish(nullptr);", "starting_ = false;",
                        "Could not start the game's program. Press Play to try again."}));
}

std::set<std::string> GamePrograms() {
  std::set<std::string> programs;
  for (const GameDefinition& game : AllGames()) {
    programs.emplace(game.program);
  }
  return programs;
}

// The names between `case "${MUA_APPIMAGE_PROGRAM:-}" in` and the `)` that selects them.
std::set<std::string> AppRunPrograms() {
  const std::string apprun = ReadSource("../packaging/linux/AppRun");
  const std::string head = "case \"${MUA_APPIMAGE_PROGRAM:-}\" in";
  const std::string cases = Region(apprun, head, ")");
  std::set<std::string> programs;
  std::string name;
  for (const char c : cases.substr(std::min(cases.size(), head.size()))) {
    if (c == '|' || c == ' ' || c == '\n') {
      if (!name.empty()) {
        programs.insert(std::exchange(name, {}));
      }
    } else {
      name += c;
    }
  }
  if (!name.empty()) {
    programs.insert(name);
  }
  return programs;
}

void TestGamePrograms() {
  std::printf("each game starts its own program, named for each platform\n");
  CHECK(Game(GameId::kMua1).program == "marvel_ultimate_alliance_mua1");
  CHECK(Game(GameId::kMua1Gold).program == "marvel_ultimate_alliance_mua1_gold");
  CHECK(Game(GameId::kMua2).program == "marvel_ultimate_alliance_mua2");
  for (const GameDefinition& game : AllGames()) {
    CHECK(!game.program.empty() && game.program != "marvel_ultimate_alliance_bundle");
    CHECK(game.program.find('.') == std::string_view::npos && game.program.find('/') == std::string_view::npos);
    CHECK(ProgramFileName(game.program, ProgramPlatform::kWindows) == std::string(game.program) + ".exe");
    CHECK(ProgramFileName(game.program, ProgramPlatform::kLinux) == game.program);
  }
  CHECK(GamePrograms().size() == AllGames().size());
  CHECK(ProgramFileName("", ProgramPlatform::kWindows).empty());
#if defined(_WIN32)
  CHECK(kHostProgramPlatform == ProgramPlatform::kWindows);
#else
  CHECK(kHostProgramPlatform == ProgramPlatform::kLinux);
#endif
  CHECK(GameProgramPath(Game(GameId::kMua1Gold), "C:/Games/MUA", ProgramPlatform::kWindows) ==
        std::filesystem::path("C:/Games/MUA") / "marvel_ultimate_alliance_mua1_gold.exe");
  CHECK(GameProgramPath(Game(GameId::kMua2), "/home/deck/mua", ProgramPlatform::kLinux) ==
        std::filesystem::path("/home/deck/mua") / "marvel_ultimate_alliance_mua2");

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "mua::LauncherRuntimeBlocker(game, mua::ProgramFolder(), mua::kHostProgramPlatform)"));
  CHECK(Has(app, "mua::GameProgramPath(*game.game, mua::ProgramFolder(), mua::kHostProgramPlatform)"));
  CHECK(!Has(app, "PortFolder() / std::string(game"));
  CHECK(!Has(app, "marvel_ultimate_alliance_mua"));
}

struct ScratchFolder {
  std::filesystem::path root = std::filesystem::temp_directory_path() / "mua_launcher_handoff_tests";
  ScratchFolder() {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~ScratchFolder() { std::filesystem::remove_all(root); }
};

void Touch(const std::filesystem::path& file) {
  std::filesystem::create_directories(file.parent_path());
  std::ofstream(file) << "program";
}

void TestRuntimeAvailability() {
  std::printf("a game is playable when its program is beside the launcher's, and only then\n");
  const ScratchFolder scratch;
  // The AppImage file and the game data live in the port folder; the programs run from the mount.
  const std::filesystem::path port = scratch.root / "Games";
  const std::filesystem::path mount = scratch.root / ".mount_MarvelAbCdEf" / "usr" / "bin";
  Touch(port / "MarvelUltimateAlliance-v0.1.0-x86_64.AppImage");
  std::filesystem::create_directories(port / "game");
  for (const GameDefinition& game : AllGames()) {
    Touch(GameProgramPath(game, mount, ProgramPlatform::kLinux));
  }
  for (const GameDefinition& game : AllGames()) {
    CHECK(LauncherRuntimeBlocker(game, mount, ProgramPlatform::kLinux).empty());
    CHECK(LauncherRuntimeBlocker(game, port, ProgramPlatform::kLinux) ==
          "The " + std::string(game.display_name) + " runtime is not part of this build.");
    // Windows names never stand for the Linux programs, nor the other way round.
    CHECK(!LauncherRuntimeBlocker(game, mount, ProgramPlatform::kWindows).empty());
  }

  const std::filesystem::path windows = scratch.root / "windows";
  for (const GameDefinition& game : AllGames()) {
    Touch(GameProgramPath(game, windows, ProgramPlatform::kWindows));
  }
  for (const GameDefinition& game : AllGames()) {
    CHECK(LauncherRuntimeBlocker(game, windows, ProgramPlatform::kWindows).empty());
    CHECK(!LauncherRuntimeBlocker(game, windows, ProgramPlatform::kLinux).empty());
  }

  std::filesystem::remove(GameProgramPath(Game(GameId::kMua2), mount, ProgramPlatform::kLinux));
  CHECK(LauncherRuntimeBlocker(Game(GameId::kMua2), mount, ProgramPlatform::kLinux) ==
        "The Marvel Ultimate Alliance 2 runtime is not part of this build.");
  CHECK(LauncherRuntimeBlocker(Game(GameId::kMua1), mount, ProgramPlatform::kLinux).empty());
  std::filesystem::create_directories(GameProgramPath(Game(GameId::kMua2), mount, ProgramPlatform::kLinux));
  CHECK(!LauncherRuntimeBlocker(Game(GameId::kMua2), mount, ProgramPlatform::kLinux).empty());

  GameDefinition no_runtime = Game(GameId::kMua1);
  no_runtime.program = "";
  CHECK(LauncherRuntimeBlocker(no_runtime, mount, ProgramPlatform::kLinux) ==
        "Marvel Ultimate Alliance has no runtime yet.");
}

void TestLinuxLaunch() {
  std::printf("Linux starts the sibling program, or the AppImage again with the program's name\n");
  const std::filesystem::path mount = "/tmp/.mount_MarvelAbCdEf/usr/bin";
  const std::string launcher = (mount / "marvel_ultimate_alliance_bundle").generic_string();
  const char* appimage = "/home/deck/Games/MarvelUltimateAlliance-v0.1.0-x86_64.AppImage";
  const std::vector<std::string> extra = {"--mua_skip_launcher=true", "--mua_launcher_handoff=true"};
  const std::set<std::string> apprun = AppRunPrograms();
  CHECK(apprun == GamePrograms());
  for (const GameDefinition& game : AllGames()) {
    const std::string program = GameProgramPath(game, mount, ProgramPlatform::kLinux).generic_string();
    const auto packaged = MakeRelaunchCommand({launcher}, appimage, program, extra);
    CHECK(packaged && packaged->executable == appimage);
    CHECK(packaged && packaged->appimage_program == game.program);
    CHECK(packaged && apprun.contains(packaged->appimage_program));

    const std::filesystem::path dev = "/home/user/mua/out/build/linux-amd64-release";
    const std::string sibling = GameProgramPath(game, dev, ProgramPlatform::kLinux).generic_string();
    const auto direct =
        MakeRelaunchCommand({(dev / "marvel_ultimate_alliance_bundle").generic_string()}, nullptr, sibling, extra);
    CHECK(direct && direct->executable == sibling && direct->appimage_program.empty());
    CHECK(direct && direct->argv.front() == sibling);
  }
}

void TestPackagedPrograms() {
  std::printf("the packages carry exactly the programs the launcher looks for\n");
  const std::string package = ReadSource("../tools/package-linux.sh");
  const std::string list = Region(package, "GAME_PROGRAMS=(", ")");
  const std::string cmake = ReadSource("../CMakeLists.txt");
  const std::string release = ReadSource("../tools/package-release.ps1");
  for (const GameDefinition& game : AllGames()) {
    const std::string program(game.program);
    CHECK(Has(list + " ", program + " "));
    CHECK(Has(cmake, "mua_add_game_executable(" + program + " "));
    CHECK(Has(release, "'" + ProgramFileName(program, ProgramPlatform::kWindows) + "'"));
  }
  CHECK(Count(list, "marvel_ultimate_alliance_") == AllGames().size());
  CHECK(Has(package, "\"$ROOT/src/game_definition.cpp\""));
}

void TestPlayablePages() {
  std::printf("every game offers the same pages on every platform\n");
  for (const GameDefinition& game : AllGames()) {
    for (const LauncherPage page : {LauncherPage::kPlay, LauncherPage::kDisplay, LauncherPage::kGraphics,
                                    LauncherPage::kGameplay, LauncherPage::kAchievements, LauncherPage::kControls,
                                    LauncherPage::kAbout}) {
      CHECK(game.HasPage(page));
    }
  }
  CHECK(!Has(ReadSource("launcher.cpp"), "_WIN32"));
  CHECK(!Has(ReadSource("game_definition.cpp"), "_WIN32"));
}

void TestDirectLaunch() {
  std::printf("a program started directly needs no launcher\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string game_branch = Region(app, "if (!IsLauncher()) {", "return routed;");
  CHECK(!game_branch.empty());
  CHECK(InOrder(game_branch, {"if (mua::TakeLauncherHandoff() && window()) {",
                              "mua::CompleteLauncherHandoff(window()->GetNativeWindowHandle());",
                              "ActivateGame(selected);"}));
  CHECK(Count(app, "TakeLauncherHandoff()") == 1);
  CHECK(Count(app, "CompleteLauncherHandoff(") == 1);
  CHECK(!Has(game_branch, "StartRuntime") && !Has(game_branch, "RelaunchExecutable"));
}

void TestOneShotFlag() {
  std::printf("the handoff flag is consumed once and never starts another program\n");
  CHECK(std::wstring_view(kHandoffArgument) == L"--mua_launcher_handoff=true");

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_launcher_handoff, false,"));
  const std::string take = Region(settings, "bool TakeLauncherHandoff() {", "\n}\n");
  CHECK(InOrder(take, {"REXCVAR_GET(mua_launcher_handoff)", "rex::cvar::ResetToDefault(kLauncherHandoffKey);",
                       "return handoff;"}));
  CHECK(!Has(settings, "\"mua_launcher_handoff\""));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string game_branch = Region(app, "if (!IsLauncher()) {", "return routed;");
  CHECK(!Has(game_branch, "StartRuntime") && !Has(game_branch, "mua_skip_launcher"));
}

void TestPlacementIsNotPropagated() {
  std::printf("launcher placement, minimized or not, never reaches the game's startup\n");
  const std::wstring args = std::wstring(kHandoffArgument) + L" --mua_skip_launcher=true";
  for (const wchar_t* key : {L"launcher_x", L"launcher_y", L"width", L"height", L"maximized", L"minimized", L"fullscreen"}) {
    CHECK(args.find(key) == std::wstring::npos);
  }

  const std::string platform = ReadSource("platform.cpp");
  const std::string relaunch = Region(platform, "bool RelaunchExecutable(", "void CompleteLauncherHandoff(");
  CHECK(!Has(relaunch, "Placement") && !Has(relaunch, "mua_launcher_x") && !Has(relaunch, "GetWindowRect"));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string game_branch = Region(app, "if (!IsLauncher()) {", "return routed;");
  for (const char* placement : {"SavedLauncherPlacement", "ApplyWindowPlacement", "InstallPreShowPlacement"}) {
    CHECK(!Has(game_branch, placement));
  }

  const WindowPlacement saved{{100, 80, 1380, 800}, false};
  PlacementTracker tracker(saved);
  tracker.Observe({{-32000, -32000, -31840, -31972}, false, true}, false, 1.0);
  CHECK(tracker.Current() == std::optional<WindowPlacement>(saved));
  CHECK(!tracker.TakeSettled(10.0));
  CHECK(StartupNeedsRestore({{}, false, true}));
  CHECK(!StartupNeedsRestore({{0, 0, 1920, 1040}, true, false}));
  CHECK(!StartupNeedsRestore({{100, 80, 1380, 800}, false, false}));
}

}  // namespace

int main() {
  TestShowCommand();
  TestForegroundHandoff();
  TestNoWaitForTheGame();
  TestShutdownOrder();
  TestFailedStartLeavesLauncherUsable();
  TestGamePrograms();
  TestRuntimeAvailability();
  TestLinuxLaunch();
  TestPackagedPrograms();
  TestPlayablePages();
  TestDirectLaunch();
  TestOneShotFlag();
  TestPlacementIsNotPropagated();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
