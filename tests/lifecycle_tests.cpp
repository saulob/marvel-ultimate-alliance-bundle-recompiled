// Window-close lifecycle policy: a host window close hard-exits without runtime teardown, while a normal
// guest exit keeps the graceful teardown in OnShutdown. The decision is read from the app source (the
// window calls need the running game).
// Run: mua_lifecycle_tests

#include <cstdio>
#include <filesystem>
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

// The text of the member function that starts at `declaration`, up to its column-2 closing brace.
std::string FunctionBody(const std::string& source, const char* declaration) {
  const size_t start = source.find(declaration);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = source.find("\n  }\n", start);
  return source.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestHostWindowCloseHardExits() {
  std::printf("host window close hard-exits without runtime teardown\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
  const std::string body = FunctionBody(app, "void OnClosing(");
  CHECK(Has(body, "rex::FlushLogging();"));
  CHECK(Has(body, "std::_Exit(0);"));
  CHECK(!Has(body, "TerminateTitle"));
  CHECK(!Has(body, "StopTitleThreads"));
  CHECK(!Has(body, "Shutdown("));
  CHECK(!Has(body, "DetachWindowHost"));
}

void TestNormalExitKeepsTeardown() {
  std::printf("normal guest exit keeps the graceful teardown\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  const std::string body = FunctionBody(app, "void OnShutdown(");
  CHECK(Has(body, "input->Shutdown();"));
  CHECK(Has(body, "DetachWindowHost();"));
}

void TestCrashReportsInstalled() {
  std::printf("single-log crash reporting is installed once logging starts\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string post_init = FunctionBody(app, "void OnPostInitLogging(");
  CHECK(Has(post_init, "mua::InstallCrashReports();"));
  CHECK(!Has(FunctionBody(app, "void OnPostSetup("), "mua::InstallCrashReports();"));

  const std::string platform = ReadSource("platform.cpp");
  const std::string install = Between(platform, "void InstallCrashReports(", "UiFontFiles FindUiFonts(");
  CHECK(Has(install, "ActiveLogFile()"));
  CHECK(Has(install, "InstallCrashReporting("));
  CHECK(Has(install, "\"Windows crash reporting enabled for current log.\""));
  CHECK(!Has(platform, "trg::InstallCrashReports("));
  CHECK(!Has(platform, "MiniDump"));

  const std::string header = ReadSource("platform.h");
  CHECK(Has(header, "void InstallCrashReports();"));

  const std::string report = ReadSource("crash_report.cpp");
  CHECK(Has(report, "STATUS_HEAP_CORRUPTION"));
  CHECK(Has(report, "SetUnhandledExceptionFilter("));
  CHECK(!Has(report, "MiniDump"));
}

void TestSessionLogFinalizationWiring() {
  std::printf("clean-session log deletion is wired to the normal and hard exits\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string post_init = Between(app, "void OnPostInitLogging(", "void OnConfigureFonts(");
  CHECK(Has(post_init, "mua::InstallSessionLogCleanup();"));
  CHECK(Has(post_init, "mua::RemoveDiscardedSessionLogs();"));
  // Old sessions go only after the active log exists and the discarded ones are handled.
  const size_t discarded = post_init.find("mua::RemoveDiscardedSessionLogs();");
  const size_t old_sessions = post_init.find("mua::RemoveOldSessionLogs();");
  CHECK(discarded != std::string::npos && old_sessions != std::string::npos && discarded < old_sessions);

  const std::string closing =
      Between(app, "void OnClosing(", "std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay");
  CHECK(Has(closing, "mua::DiscardSessionLogBeforeHardExit();"));
  CHECK(!Has(closing, "FinalizeSessionLog"));
  CHECK(Has(closing, "rex::FlushLogging();"));
  CHECK(Has(closing, "std::_Exit(0);"));
  const size_t finalize = closing.find("mua::DiscardSessionLogBeforeHardExit();");
  const size_t hard_exit = closing.find("std::_Exit(0);");
  CHECK(finalize != std::string::npos && hard_exit != std::string::npos && finalize < hard_exit);
  CHECK(!Has(Between(app, "void OnShutdown() override", "void OnClosing("), "mua::FinalizeSessionLog();"));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "ActiveLogFile()"));
  CHECK(Has(session, "DetailedLoggingEnabled()"));
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "std::atexit("));
  CHECK(Has(session, "ShutdownLogging()"));
  CHECK(Has(session, "std::filesystem::remove("));
}

void TestNoSeparateCrashArtifacts() {
  std::printf("no separate crash text or minidump is written\n");
  for (const char* source : {"platform.cpp", "crash_report.cpp", "marvel_ultimate_alliance_app.h"}) {
    const std::string text = ReadSource(source);
    CHECK(!Has(text, "MiniDump"));
    CHECK(!Has(text, "crash-"));
    CHECK(!Has(text, ".dmp"));
  }
}

// DEBUG_SAULO marks temporary investigation code; a release carries none in the port or its runtime patches.
void TestNoTemporaryDiagnostics() {
  std::printf("no DEBUG_SAULO instrumentation in the sources or the runtime patches\n");
  const std::filesystem::path root = std::filesystem::path(MUA_SRC_DIR) / "..";
  size_t scanned = 0;
  for (const char* folder : {"src", "patches/rexglue-sdk", "patches/trg-launcher", "config", "packaging"}) {
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root / folder, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file()) {
        continue;
      }
      std::ifstream in(it->path(), std::ios::binary);
      std::ostringstream text;
      text << in.rdbuf();
      ++scanned;
      if (Has(text.str(), "DEBUG_SAULO") || Has(text.str(), "DebugSaulo")) {
        std::printf("  temporary diagnostics in %s\n", it->path().string().c_str());
        CHECK(false);
      }
    }
  }
  CHECK(scanned > 100);
}

void TestIntentionalExitsWriteNoCrashBlock() {
  std::printf("window close and normal shutdown never write a crash block\n");
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::string closing = FunctionBody(app, "void OnClosing(");
  CHECK(Has(closing, "rex::FlushLogging();"));
  CHECK(Has(closing, "std::_Exit(0);"));
  CHECK(!Has(closing, "CrashReport"));
  const std::string shutdown = FunctionBody(app, "void OnShutdown(");
  CHECK(!Has(shutdown, "CrashReport"));
}

}  // namespace

int main() {
  TestHostWindowCloseHardExits();
  TestNormalExitKeepsTeardown();
  TestCrashReportsInstalled();
  TestSessionLogFinalizationWiring();
  TestNoSeparateCrashArtifacts();
  TestIntentionalExitsWriteNoCrashBlock();
  TestNoTemporaryDiagnostics();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
