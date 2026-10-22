// Detailed logging: the setting value and fallback, the Minimal filter's classification and its
// effect on the rotating log file, and the session-log lifecycle that keeps a crash log but discards
// a normal-session log when Detailed logging is off. Run: mua_log_filter_tests

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "active_log.h"
#include "crash_report.h"
#include "log_filter.h"
#include "session_log.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

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

int CountOf(const std::string& text, const char* needle) {
  int count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteText(const std::filesystem::path& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

void SetDetailedLogging(bool enabled) {
  rex::cvar::SetFlagByName("detailed_logging", enabled ? "true" : "false");
  mua::ApplyDetailedLogging();
}

void TestClassification() {
  std::printf("Minimal mode keeps failures and first-line diagnostics\n");
  using spdlog::level::debug;
  using spdlog::level::err;
  using spdlog::level::info;
  using spdlog::level::trace;
  using spdlog::level::warn;

  CHECK(!mua::ShouldLogInMinimalMode(info, "core", "Launcher shown"));
  CHECK(!mua::ShouldLogInMinimalMode(info, "core", "Scheduling: high priority"));
  CHECK(!mua::ShouldLogInMinimalMode(info, "core", "Initializing shader storage for title 415607DA..."));
  CHECK(!mua::ShouldLogInMinimalMode(info, "gpu", "GPU wait for idle = 00000000"));
  CHECK(!mua::ShouldLogInMinimalMode(info, "krnl", "Function table initialized for module: code=..."));
  CHECK(!mua::ShouldLogInMinimalMode(debug, "core", "RexApp starting"));
  CHECK(!mua::ShouldLogInMinimalMode(trace, "core", "per-instruction detail"));

  CHECK(mua::ShouldLogInMinimalMode(warn, "core", "Window opened on another display"));
  CHECK(mua::ShouldLogInMinimalMode(err, "gpu", "Failed to create a Direct3D 12 device"));
  CHECK(mua::ShouldLogInMinimalMode(spdlog::level::critical, "core", "fatal"));

  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Marvel Ultimate Alliance port 0.1.0"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Build: RelWithDebInfo x86-64 | ReXGlue SDK 0.10.0"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "User data: portable path selected"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Player settings: settings.toml"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Game files: valid (profile mua-gold-x360, default.xex 3981312 bytes)"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Launcher Display: Display mode=Windowed"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Launcher Graphics: Graphics card=Automatic"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime configuration:"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime OS: Windows 11"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime CPU: Test CPU"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime GPU: selected=Automatic | resolved=Test GPU"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime display: 1920x1080"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime game: title_id=415607DA"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime game sha256: ABCDEF"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime timing: simulation=60Hz"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime renderer: render=100% 1280x720"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Runtime input: keyboard_mode=On"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Graphics card: Test GPU (adapter 0)"));
  CHECK(mua::ShouldLogInMinimalMode(info, "gpu", "DXGI adapter: Test GPU (vendor 0x1002, device 0x1234)"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Vulkan device: Test GPU (type DiscreteGpu)"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "SDL video driver: windows"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Windows crash reporting enabled for current log."));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Execution complete: clean exit, uptime 00:00:12"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Window closing, shutting down..."));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Launcher closed"));
  CHECK(mua::ShouldLogInMinimalMode(info, "core", "Restarted in a new window; hard-exiting this process."));
}

void TestSettingValue(const std::filesystem::path& dir) {
  std::printf("setting default, persistence and malformed fallback\n");
  const std::filesystem::path file = dir / "settings.toml";

  rex::cvar::ResetToDefault("detailed_logging");
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(mua::DetailedLoggingEnabled());

  WriteText(file, "detailed_logging = false\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "false");
  CHECK(!mua::DetailedLoggingEnabled());

  WriteText(file, "detailed_logging = true\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(mua::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("detailed_logging");
  WriteText(file, "# no detailed_logging here\nwindow_width = 1280\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(mua::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("detailed_logging");
  WriteText(file, "detailed_logging = \"garbage\"\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(mua::DetailedLoggingEnabled());

  rex::cvar::ResetToDefault("detailed_logging");
  WriteText(file, "detailed_logging = 1\n");
  rex::cvar::LoadConfig(file);
  CHECK(rex::cvar::GetFlagByName("detailed_logging") == "true");
  CHECK(mua::DetailedLoggingEnabled());
}

void TestCrashReportIndependent() {
  std::printf("CRASH REPORT formatting does not depend on the setting\n");
  mua::CrashReport report;
  report.time = {2026, 10, 19, 12, 0, 0, 0};
  report.code = 0xC0000005u;
  report.address = 0x00007FF6A1B2C3D4ull;
  report.thread_id = 7;
  report.process_id = 9;
  report.architecture = "x86-64";
  report.game_version = "0.1.0";

  char detailed[8192];
  const size_t detailed_length = mua::FormatCrashReport(report, detailed, sizeof(detailed));

  SetDetailedLogging(false);
  char minimal[8192];
  const size_t minimal_length = mua::FormatCrashReport(report, minimal, sizeof(minimal));
  CHECK(detailed_length == minimal_length);
  CHECK(std::string(detailed, detailed_length) == std::string(minimal, minimal_length));
  CHECK(Has(std::string(detailed, detailed_length), "CRASH REPORT\n"));
  CHECK(Has(std::string(detailed, detailed_length), "Exception code: 0xC0000005 (ACCESS_VIOLATION)"));
  CHECK(Has(std::string(detailed, detailed_length), "Thread ID: 7"));
}

spdlog::sink_ptr RegistryFileSink() {
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    for (const spdlog::sink_ptr& sink : entry.logger->sinks()) {
      const spdlog::sink_ptr unwrapped = mua::UnwrapLogSink(sink);
      if (dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(unwrapped.get()) != nullptr) {
        return unwrapped;
      }
    }
  }
  return {};
}

// One logging session: content filtering, then the session-log outcome when Detailed logging is
// on (retained) and off (the exact active log is discarded, a decoy is left alone).
void TestSessionLifecycle(const std::filesystem::path& dir) {
  std::printf("Detailed retains the session log, Minimal discards only the active log\n");
  rex::LogConfig config;
  config.app_name = "mua_session_test";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  REXLOG_INFO("Marvel Ultimate Alliance port 0.1.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
  rex::FlushLogging();

  const std::filesystem::path active = mua::ActiveLogFile();
  CHECK(!active.empty());
  CHECK(std::filesystem::exists(active));

  spdlog::sink_ptr file = RegistryFileSink();
  auto* rotating = dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(file.get());
  CHECK(rotating != nullptr);
  const size_t max_size = rotating ? rotating->get_max_size() : 0;
  const size_t max_files = rotating ? rotating->get_max_files() : 0;

  SetDetailedLogging(true);
  CHECK(!mua::MinimalLogFilterInstalled());
  REXLOG_INFO("launcher detailed segment");
  rex::FlushLogging();
  CHECK(mua::ActiveLogFile() == active);

  SetDetailedLogging(false);
  CHECK(mua::MinimalLogFilterInstalled());
  REXLOG_INFO("launcher minimal segment");
  REXLOG_INFO("Windows crash reporting enabled for current log.");
  REXLOG_INFO("ordinary detailed chatter two");

  // Play starts the game without replacing the session's log or its file sink.
  CHECK(mua::ActiveLogFile() == active);
  CHECK(RegistryFileSink() == file);
  REXLOG_INFO("game minimal segment");
  REXLOG_WARN("a warning survives");
  REXLOG_ERROR("an error survives");
  REXLOG_INFO("Graphics card: Test GPU (adapter 0)");

  SetDetailedLogging(true);
  CHECK(!mua::MinimalLogFilterInstalled());
  REXLOG_INFO("game detailed segment");
  REXLOG_INFO("ordinary detailed chatter one");
  REXLOG_INFO("Execution complete");
  REXLOG_INFO("Execution complete elsewhere");
  REXLOG_INFO("Execution complete: clean exit, uptime 00:00:01");
  rex::FlushLogging();

  CHECK(mua::ActiveLogFile() == active);
  CHECK(RegistryFileSink() == file);
  const std::string log = ReadText(active);
  CHECK(Has(log, "launcher detailed segment"));
  CHECK(!Has(log, "launcher minimal segment"));
  CHECK(Has(log, "game detailed segment"));
  CHECK(!Has(log, "game minimal segment"));
  CHECK(Has(log, "ordinary detailed chatter one"));
  CHECK(!Has(log, "ordinary detailed chatter two"));
  CHECK(CountOf(log, "Execution complete") == 2);
  CHECK(Has(log, "Execution complete elsewhere"));
  CHECK(Has(log, "Execution complete: clean exit, uptime 00:00:01"));
  CHECK(Has(log, "a warning survives"));
  CHECK(Has(log, "an error survives"));
  CHECK(Has(log, "Windows crash reporting enabled for current log."));
  CHECK(Has(log, "Graphics card: Test GPU"));
  CHECK(rotating && rotating->get_max_size() == max_size && rotating->get_max_files() == max_files);

  // Detailed on at exit: the same unified log is kept.
  SetDetailedLogging(true);
  mua::FinalizeSessionLog();
  CHECK(std::filesystem::exists(active));

  // Detailed off at exit: a clean finalization removes exactly the active log, never the decoy.
  const std::filesystem::path decoy = dir / "marvel_ultimate_alliance_900.log";
  WriteText(decoy, "decoy\n");
  file.reset();
  SetDetailedLogging(false);
  mua::FinalizeSessionLog();
  CHECK(!std::filesystem::exists(active));
  CHECK(std::filesystem::exists(decoy));
  CHECK(!mua::MinimalLogFilterInstalled());
  CHECK(mua::ActiveLogFile().empty());
}

void TestHardExitDiscard(const std::filesystem::path& dir) {
  std::printf("a window close discards a Minimal log without shutting logging down\n");
  const std::filesystem::path closed = dir / "marvel_ultimate_alliance_801.log";
  const std::filesystem::path crashed = dir / "marvel_ultimate_alliance_802.log";
  const std::filesystem::path next = dir / "marvel_ultimate_alliance_803.log";
  const std::filesystem::path previous = dir / "marvel_ultimate_alliance_804.log";
  const std::filesystem::path unrelated = dir / "notes.log";
  WriteText(closed, "normal session\n");
  WriteText(crashed, "run started\nCRASH REPORT\n");
  WriteText(next, "next session\n");
  WriteText(previous, "previous session\n");
  WriteText(unrelated, "unrelated\n");
  SetDetailedLogging(false);

  rex::cvar::SetFlagByName("log_file", closed.string());
  mua::DiscardSessionLogBeforeHardExit();
  rex::cvar::SetFlagByName("log_file", crashed.string());
  mua::DiscardSessionLogBeforeHardExit();
  CHECK(std::filesystem::exists(crashed));
  CHECK(!std::filesystem::exists(crashed.string() + ".discard"));
  CHECK(!std::filesystem::exists(closed));
  CHECK(!std::filesystem::exists(closed.string() + ".discard"));

  const std::filesystem::path marked = dir / "marvel_ultimate_alliance_805.log";
  WriteText(marked, "marked by an older build\n");
  WriteText(marked.string() + ".discard", "");
  WriteText(next.string() + ".discard", "");
  WriteText(unrelated.string() + ".discard", "");

  rex::cvar::SetFlagByName("log_file", next.string());
  mua::RemoveDiscardedSessionLogs();
  CHECK(!std::filesystem::exists(marked));
  CHECK(!std::filesystem::exists(marked.string() + ".discard"));
  CHECK(std::filesystem::exists(crashed));
  CHECK(std::filesystem::exists(next));
  CHECK(std::filesystem::exists(previous));
  CHECK(std::filesystem::exists(unrelated));
  CHECK(!std::filesystem::exists(next.string() + ".discard"));

  SetDetailedLogging(true);
  WriteText(closed, "normal session\n");
  rex::cvar::SetFlagByName("log_file", closed.string());
  mua::DiscardSessionLogBeforeHardExit();
  CHECK(std::filesystem::exists(closed));
  CHECK(!std::filesystem::exists(closed.string() + ".discard"));
  rex::cvar::SetFlagByName("log_file", "");
}

void TestMissingAndRepeatedFinalization() {
  std::printf("a missing active log and a repeated finalization are harmless\n");
  SetDetailedLogging(false);
  CHECK(mua::ActiveLogFile().empty());
  mua::FinalizeSessionLog();
  mua::FinalizeSessionLog();
  CHECK(mua::ActiveLogFile().empty());
}

#if defined(_WIN32)

std::wstring SelfPath() {
  wchar_t path[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

DWORD RunChild(const std::wstring& arguments) {
  std::wstring command = L"\"" + SelfPath() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    std::printf("  FAIL could not start child (%lu)\n", GetLastError());
    return 0xFFFFFFFFu;
  }
  DWORD exit_code = 0xFFFFFFFFu;
  if (WaitForSingleObject(process.hProcess, 30000) == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, exit_code);
    std::printf("  FAIL the child did not exit\n");
  } else {
    GetExitCodeProcess(process.hProcess, &exit_code);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return exit_code;
}

bool StartChild(const std::wstring& arguments) {
  std::wstring command = L"\"" + SelfPath() + L"\" " + arguments;
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) {
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

bool AnyFile(const std::filesystem::path& dir, const char* prefix, const char* extension) {
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.starts_with(prefix) && entry.path().extension() == extension) {
      return true;
    }
  }
  return false;
}

std::filesystem::path OnlyLog(const std::filesystem::path& dir) {
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      return entry.path();
    }
  }
  return {};
}

int CountLogs(const std::filesystem::path& dir) {
  int count = 0;
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".log") {
      ++count;
    }
  }
  return count;
}

void TestInheritedSessionValidation(const std::filesystem::path& root) {
  std::printf("an inherited session log is accepted only from an allowed folder and name\n");
  const std::filesystem::path allowed = root / "inherited_allowed";
  const std::filesystem::path elsewhere = root / "inherited_elsewhere";
  std::filesystem::create_directories(allowed);
  std::filesystem::create_directories(elsewhere);
  WriteText(allowed / "marvel_ultimate_alliance_042.log", "session\n");
  WriteText(elsewhere / "marvel_ultimate_alliance_043.log", "session\n");
  WriteText(allowed / "other_044.log", "session\n");

  const auto inherit = [](const std::filesystem::path& file) {
    SetEnvironmentVariableW(L"MUA_SESSION_LOG", file.wstring().c_str());
    SetEnvironmentVariableW(L"MUA_SESSION_PARENT", L"0");
  };

  inherit(allowed / "marvel_ultimate_alliance_042.log");
  const auto accepted = mua::TakeInheritedSessionLog({allowed});
  CHECK(accepted.has_value());
  CHECK(accepted && *accepted == allowed / "marvel_ultimate_alliance_042.log");

  inherit(elsewhere / "marvel_ultimate_alliance_043.log");
  CHECK(!mua::TakeInheritedSessionLog({allowed}).has_value());

  inherit(allowed / "other_044.log");
  CHECK(!mua::TakeInheritedSessionLog({allowed}).has_value());

  SetEnvironmentVariableW(L"MUA_SESSION_LOG", L"");
  CHECK(!mua::TakeInheritedSessionLog({allowed}).has_value());
}

bool WaitForLogText(const std::filesystem::path& dir, const char* needle) {
  for (int attempt = 0; attempt < 500; ++attempt) {
    const std::filesystem::path log = OnlyLog(dir);
    if (!log.empty() && Has(ReadText(log), needle)) {
      return true;
    }
    Sleep(20);
  }
  return false;
}

int RunChildMode(const std::wstring& mode, const std::filesystem::path& dir) {
  if (mode == L"handoff-child") {
    const auto inherited = mua::TakeInheritedSessionLog({dir});
    if (!inherited) {
      return 3;
    }
    rex::LogConfig config;
    const std::string inherited_path = inherited->string();
    config.log_file = inherited_path.c_str();
    config.default_level = spdlog::level::info;
    rex::InitLogging(config);
    mua::InstallSessionLogCleanup();
    SetDetailedLogging(true);
    REXLOG_INFO("game detailed segment");
    return 0;
  }

  rex::LogConfig config;
  config.app_name = "marvel_ultimate_alliance";
  config.log_dir = dir.string();
  config.default_level = spdlog::level::info;
  rex::InitLogging(config);
  mua::InstallSessionLogCleanup();

  const bool transition = mode.starts_with(L"transition-");
  if (!transition) {
    SetDetailedLogging(false);
    REXLOG_INFO("Marvel Ultimate Alliance port 0.1.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
    REXLOG_WARN("pre-crash warning");
    REXLOG_INFO("Graphics card: Test GPU (adapter 0)");
  }

  if (mode == L"clean") {
    return 0;
  }
  if (mode == L"handoff-parent") {
    SetDetailedLogging(true);
    REXLOG_INFO("launcher detailed segment");
    mua::BeginSessionLogHandoff();
    if (!StartChild(L"--child handoff-child \"" + dir.wstring() + L"\"")) {
      mua::CancelSessionLogHandoff();
      return 2;
    }
    return 0;
  }
  if (mode == L"hard-exit") {
    mua::FinalizeSessionLog();
    mua::FinalizeSessionLog();
    std::_Exit(0);
  }
  if (mode == L"window-close" || mode == L"window-close-held") {
    HANDLE held = INVALID_HANDLE_VALUE;
    if (mode == L"window-close-held") {
      held = CreateFileW(mua::ActiveLogFile().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
    }
    mua::DiscardSessionLogBeforeHardExit();
    REXLOG_WARN("straggler warning after window close");
    rex::FlushLogging();
    std::_Exit(0);
  }
  if (mode == L"delete-fail") {
    const std::filesystem::path active = mua::ActiveLogFile();
    const HANDLE held = CreateFileW(active.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    mua::FinalizeSessionLog();
    if (held != INVALID_HANDLE_VALUE) {
      CloseHandle(held);
    }
    return 0;
  }
  if (mode == L"transition-on-off") {
    SetDetailedLogging(true);
    REXLOG_INFO("launcher detailed segment");
    SetDetailedLogging(false);
    REXLOG_INFO("game minimal segment");
    REXLOG_INFO("Windows crash reporting enabled for current log.");
    REXLOG_WARN("game warning kept");
    return 0;
  }
  if (mode == L"transition-off-on") {
    SetDetailedLogging(false);
    REXLOG_INFO("launcher minimal segment");
    REXLOG_INFO("Marvel Ultimate Alliance port 0.1.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
    SetDetailedLogging(true);
    REXLOG_INFO("game detailed segment");
    return 0;
  }
  if (mode == L"transition-on-off-on") {
    SetDetailedLogging(true);
    REXLOG_INFO("launcher detailed segment");
    SetDetailedLogging(false);
    REXLOG_INFO("game minimal segment");
    SetDetailedLogging(true);
    REXLOG_INFO("game detailed segment");
    return 0;
  }
  if (mode == L"transition-off-on-off") {
    SetDetailedLogging(false);
    REXLOG_INFO("Marvel Ultimate Alliance port 0.1.0 (x86-64, Direct3D 12, ReXGlue SDK 0.10.0)");
    SetDetailedLogging(true);
    REXLOG_INFO("game detailed segment");
    SetDetailedLogging(false);
    REXLOG_INFO("game minimal segment");
    return 0;
  }
  if (mode == L"crash" || mode == L"crash-transition") {
    if (mode == L"crash-transition") {
      SetDetailedLogging(true);
      REXLOG_INFO("launcher detailed segment");
      SetDetailedLogging(false);
      REXLOG_INFO("game minimal segment");
      REXLOG_INFO("Windows crash reporting enabled for current log.");
    }
    std::error_code ec;
    const std::filesystem::path active = mua::ActiveLogFile();
    const std::filesystem::path absolute = std::filesystem::absolute(active, ec);
    mua::InstallCrashReporting(Utf8(ec ? active : absolute).c_str());
    rex::FlushLogging();
    RaiseException(0xC0000374u, 0, 0, nullptr);
  }
  return 0;
}

void TestChildProcesses(const std::filesystem::path& root) {
  std::printf("clean, hard-exit, failed-delete and crash children\n");

  const std::filesystem::path clean = root / "child_clean";
  std::filesystem::create_directories(clean);
  CHECK(RunChild(L"--child clean \"" + clean.wstring() + L"\"") == 0);
  CHECK(!AnyFile(clean, "", ".log"));

  const std::filesystem::path hard_exit = root / "child_hard_exit";
  std::filesystem::create_directories(hard_exit);
  CHECK(RunChild(L"--child hard-exit \"" + hard_exit.wstring() + L"\"") == 0);
  CHECK(!AnyFile(hard_exit, "", ".log"));

  const std::filesystem::path window_close = root / "child_window_close";
  std::filesystem::create_directories(window_close);
  const std::filesystem::path previous_log = window_close / "marvel_ultimate_alliance_900.log";
  const std::filesystem::path unrelated = window_close / "notes.txt";
  WriteText(previous_log, "previous session\n");
  WriteText(unrelated, "unrelated\n");
  for (int cycle = 0; cycle < 3; ++cycle) {
    CHECK(RunChild(L"--child window-close " + std::wstring(L"\"") + window_close.wstring() + L"\"") == 0);
    CHECK(CountLogs(window_close) == 1);
    CHECK(!AnyFile(window_close, "", ".discard"));
  }
  CHECK(std::filesystem::exists(previous_log));
  CHECK(std::filesystem::exists(unrelated));

  const std::filesystem::path window_close_held = root / "child_window_close_held";
  std::filesystem::create_directories(window_close_held);
  CHECK(RunChild(L"--child window-close-held \"" + window_close_held.wstring() + L"\"") == 0);
  CHECK(AnyFile(window_close_held, "", ".log"));
  CHECK(AnyFile(window_close_held, "", ".discard"));

  const std::filesystem::path delete_fail = root / "child_delete_fail";
  std::filesystem::create_directories(delete_fail);
  CHECK(RunChild(L"--child delete-fail \"" + delete_fail.wstring() + L"\"") == 0);
  CHECK(AnyFile(delete_fail, "", ".log"));

  const std::filesystem::path crash = root / "child_crash";
  std::filesystem::create_directories(crash);
  CHECK(RunChild(L"--child crash \"" + crash.wstring() + L"\"") == 0xC0000374u);
  const std::filesystem::path log = OnlyLog(crash);
  CHECK(!log.empty());
  CHECK(CountLogs(crash) == 1);
  CHECK(!AnyFile(crash, "crash-", ".log"));
  const std::string text = log.empty() ? std::string() : ReadText(log);
  CHECK(Has(text, "Marvel Ultimate Alliance port 0.1.0"));
  CHECK(Has(text, "Graphics card: Test GPU"));
  CHECK(Has(text, "pre-crash warning"));
  CHECK(Has(text, "CRASH REPORT"));
  CHECK(Has(text, "Exception code: 0xC0000374 (STATUS_HEAP_CORRUPTION)"));
  CHECK(!AnyFile(crash, "crash-", ".txt"));
  CHECK(!AnyFile(crash, "", ".dmp"));

  const std::filesystem::path on_off = root / "child_on_off";
  std::filesystem::create_directories(on_off);
  CHECK(RunChild(L"--child transition-on-off \"" + on_off.wstring() + L"\"") == 0);
  CHECK(!AnyFile(on_off, "", ".log"));

  const std::filesystem::path off_on = root / "child_off_on";
  std::filesystem::create_directories(off_on);
  CHECK(RunChild(L"--child transition-off-on \"" + off_on.wstring() + L"\"") == 0);
  const std::filesystem::path off_on_log = OnlyLog(off_on);
  CHECK(!off_on_log.empty());
  const std::string off_on_text = off_on_log.empty() ? std::string() : ReadText(off_on_log);
  CHECK(Has(off_on_text, "Marvel Ultimate Alliance port 0.1.0"));
  CHECK(Has(off_on_text, "game detailed segment"));
  CHECK(!Has(off_on_text, "launcher minimal segment"));

  const std::filesystem::path on_off_on = root / "child_on_off_on";
  std::filesystem::create_directories(on_off_on);
  CHECK(RunChild(L"--child transition-on-off-on \"" + on_off_on.wstring() + L"\"") == 0);
  const std::filesystem::path on_off_on_log = OnlyLog(on_off_on);
  CHECK(!on_off_on_log.empty());
  const std::string on_off_on_text = on_off_on_log.empty() ? std::string() : ReadText(on_off_on_log);
  CHECK(Has(on_off_on_text, "launcher detailed segment"));
  CHECK(Has(on_off_on_text, "game detailed segment"));
  CHECK(!Has(on_off_on_text, "game minimal segment"));

  const std::filesystem::path off_on_off = root / "child_off_on_off";
  std::filesystem::create_directories(off_on_off);
  CHECK(RunChild(L"--child transition-off-on-off \"" + off_on_off.wstring() + L"\"") == 0);
  CHECK(!AnyFile(off_on_off, "", ".log"));

  const std::filesystem::path crash_transition = root / "child_crash_transition";
  std::filesystem::create_directories(crash_transition);
  CHECK(RunChild(L"--child crash-transition \"" + crash_transition.wstring() + L"\"") == 0xC0000374u);
  const std::filesystem::path crash_transition_log = OnlyLog(crash_transition);
  CHECK(!crash_transition_log.empty());
  const std::string crash_transition_text =
      crash_transition_log.empty() ? std::string() : ReadText(crash_transition_log);
  CHECK(Has(crash_transition_text, "launcher detailed segment"));
  CHECK(!Has(crash_transition_text, "game minimal segment"));
  CHECK(Has(crash_transition_text, "Windows crash reporting enabled for current log."));
  CHECK(Has(crash_transition_text, "CRASH REPORT"));
  CHECK(!AnyFile(crash_transition, "crash-", ".txt"));
  CHECK(!AnyFile(crash_transition, "", ".dmp"));

  const std::filesystem::path handoff = root / "child_handoff";
  std::filesystem::create_directories(handoff);
  CHECK(RunChild(L"--child handoff-parent \"" + handoff.wstring() + L"\"") == 0);
  CHECK(WaitForLogText(handoff, "game detailed segment"));
  const std::filesystem::path handoff_log = OnlyLog(handoff);
  CHECK(!handoff_log.empty());
  CHECK(CountLogs(handoff) == 1);
  const std::string handoff_text = handoff_log.empty() ? std::string() : ReadText(handoff_log);
  CHECK(Has(handoff_text, "launcher detailed segment"));
  CHECK(Has(handoff_text, "game detailed segment"));
  CHECK(Has(handoff_log.filename().string(), "marvel_ultimate_alliance_"));

  std::error_code ec;
  std::filesystem::remove_all(root / "child_clean", ec);
  std::filesystem::remove_all(root / "child_hard_exit", ec);
  std::filesystem::remove_all(root / "child_delete_fail", ec);
  std::filesystem::remove_all(root / "child_crash", ec);
  std::filesystem::remove_all(root / "child_on_off", ec);
  std::filesystem::remove_all(root / "child_off_on", ec);
  std::filesystem::remove_all(root / "child_on_off_on", ec);
  std::filesystem::remove_all(root / "child_off_on_off", ec);
  std::filesystem::remove_all(root / "child_crash_transition", ec);
  std::filesystem::remove_all(root / "child_handoff", ec);
}

#endif

void TestWiring() {
  std::printf("setting, About page and lifecycle wiring\n");
  const std::string filter = ReadSource("log_filter.cpp");
  CHECK(Has(filter, "REXCVAR_DEFINE_BOOL(detailed_logging, true"));
  CHECK(Has(filter, "Windows crash reporting enabled for current log."));
  CHECK(Has(filter, "Marvel Ultimate Alliance port "));

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "      \"detailed_logging\",\n") || Has(settings, "      \"detailed_logging\",\r\n"));

  const std::string header = ReadSource("settings.h");
  CHECK(Has(header, "REXCVAR_DECLARE(bool, detailed_logging);"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Heading(\"Diagnostics\")"));
  CHECK(Has(launcher, "ui.Toggle(\"Detailed logging\""));
  CHECK(Has(launcher, "Keeps logs for normal sessions. Crash diagnostics are always preserved."));
  CHECK(Has(launcher, "\"detailed_logging\", true"));
  CHECK(!Has(launcher, "ImGui::Checkbox(\"##detailed_logging\""));

  const std::string session = ReadSource("session_log.cpp");
  CHECK(Has(session, "mua::ActiveLogFile()") || Has(session, "ActiveLogFile()"));
  CHECK(Has(session, "DetailedLoggingEnabled()"));
  CHECK(Has(session, "CRASH REPORT"));
  CHECK(Has(session, "ShutdownLogging()"));
  CHECK(Has(session, "MUA_SESSION_LOG"));
  CHECK(Has(session, "BeginSessionLogHandoff"));
  CHECK(Has(session, "TakeInheritedSessionLog"));
  CHECK(!Has(session, "remove_all"));

  const std::string diagnostics = ReadSource("diagnostics.cpp");
  CHECK(Has(diagnostics, "Runtime configuration:"));
  CHECK(Has(diagnostics, "Launcher Display:"));
  CHECK(Has(diagnostics, "Launcher Graphics:"));
  CHECK(Has(diagnostics, "Runtime GPU: selected="));
  CHECK(Has(diagnostics, "Execution complete: clean exit, uptime "));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "mua::ApplyDetailedLogging();"));
  CHECK(Has(app, "mua::InstallSessionLogCleanup();"));
  CHECK(Has(app, "mua::DiscardSessionLogBeforeHardExit();"));
  CHECK(Has(app, "mua::RemoveDiscardedSessionLogs();"));
  CHECK(!Has(app, "mua::FinalizeSessionLog();"));
  CHECK(Has(app, "mua::TakeInheritedSessionLog({"));
  CHECK(Has(app, "void LaunchModule() override"));
  CHECK(Has(app, "mua::LogStartupDiagnostics("));
  CHECK(Has(app, "mua::FormatCleanExit("));
  CHECK(Has(app, "Restarted in a new window; hard-exiting this process."));
  CHECK(Has(launcher, "Launcher closed"));
  CHECK(Has(filter, "Execution complete: clean exit"));
  CHECK(Has(app, "SetFlagFromCommandLine(\"log_file\""));
  CHECK(Has(app, "mua::LoadPlayerSettings("));
  CHECK(!Has(app, "rex::InitLogging("));
  CHECK(!Has(app, "ShutdownLogging("));
  CHECK(!Has(app, "crash-"));
  CHECK(!Has(app, ".dmp"));
  CHECK(!Has(app, "MiniDump"));

  const std::string platform = ReadSource("platform.cpp");
  CHECK(Has(platform, "BeginSessionLogHandoff();"));
  CHECK(Has(platform, "CancelSessionLogHandoff();"));
  CHECK(!Has(ReadSource("launcher_achievements.cpp"), "InitLogging("));
  CHECK(Has(launcher, "rex::cvar::SetFlagByName(\"detailed_logging\""));
  CHECK(Has(launcher, "mua::ApplyDetailedLogging();"));
}

}  // namespace

int main() {
#if defined(_WIN32)
  int wide_argc = 0;
  LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv != nullptr && wide_argc >= 4 && std::wstring_view(wide_argv[1]) == L"--child") {
    const std::wstring mode = wide_argv[2];
    const std::filesystem::path dir = wide_argv[3];
    LocalFree(wide_argv);
    return RunChildMode(mode, dir);
  }
  if (wide_argv != nullptr) {
    LocalFree(wide_argv);
  }
#endif

  std::error_code ec;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path(ec) / "mua_log_filter_tests";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  TestClassification();
  TestSettingValue(dir);
  TestCrashReportIndependent();
  TestSessionLifecycle(dir);
  TestHardExitDiscard(dir);
  TestMissingAndRepeatedFinalization();
#if defined(_WIN32)
  TestInheritedSessionValidation(dir);
  TestChildProcesses(dir);
#endif
  TestWiring();

  std::filesystem::remove_all(dir, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
