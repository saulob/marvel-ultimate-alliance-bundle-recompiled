// Single-log crash reporting: the CRASH REPORT block layout, the recursion guard, and a child
// process that raises real Windows exceptions to prove the block lands in the current run's log
// while no crash-*.txt or *.dmp appears. Run: mua_crash_report_tests

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "crash_report.h"

#if defined(_WIN32)
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

std::string Format(const mua::CrashReport& report) {
  char buffer[8192];
  const size_t length = mua::FormatCrashReport(report, buffer, sizeof(buffer));
  return std::string(buffer, length);
}

mua::CrashReport BaseReport() {
  mua::CrashReport report;
  report.time = {2026, 10, 18, 23, 31, 2, 123};
  report.code = 0xC0000374u;
  report.address = 0x00007FF6A1B2C3D4ull;
  report.thread_id = 12345;
  report.process_id = 4321;
  report.product = "Marvel Ultimate Alliance";
  report.architecture = "x86-64";
  report.game_version = "0.6.1";
  report.build_config = "RelWithDebInfo";
  report.uptime_ms = 3723000;
  report.faulting_module = {"ntdll.dll", 0x00007FF6A1B00000ull, true};
  report.has_context = true;
  report.instruction_pointer = 0x00007FF6A1B2C3D4ull;
  report.stack_pointer = 0x000000A1B2C3D4E0ull;
  report.frame_pointer = 0x000000A1B2C3D500ull;
  return report;
}

std::string AccessViolationText(uint64_t kind) {
  mua::CrashReport report = BaseReport();
  report.code = 0xC0000005u;
  report.parameter_count = 2;
  report.parameters[0] = kind;
  report.parameters[1] = 0x1234;
  return Format(report);
}

void TestHeapCorruptionBlock() {
  std::printf("crash block formatting for STATUS_HEAP_CORRUPTION\n");
  const std::string text = Format(BaseReport());
  CHECK(Has(text, "============================================================\n"));
  CHECK(Has(text, "CRASH REPORT\n"));
  CHECK(Has(text, "[2026-10-18 23:31:02.123] [error] [core] [t12345] "));
  CHECK(Has(text, "Exception code: 0xC0000374 (STATUS_HEAP_CORRUPTION)"));
  CHECK(Has(text, "Exception address: 0x00007FF6A1B2C3D4"));
  CHECK(Has(text, "Exception flags: 0x00000000"));
  CHECK(Has(text, "Exception parameters: 0"));
  CHECK(Has(text, "Faulting module: ntdll.dll (base 0x00007FF6A1B00000, offset 0x000000000002C3D4)"));
  CHECK(Has(text, "Product: Marvel Ultimate Alliance"));
  CHECK(Has(text, "Version: 0.6.1"));
  CHECK(Has(text, "Build: RelWithDebInfo x86-64"));
  CHECK(Has(text, "Uptime: 01:02:03"));
  CHECK(Has(text, "Thread ID: 12345"));
  CHECK(Has(text, "Process ID: 4321"));
  CHECK(Has(text, "Instruction pointer: 0x00007FF6A1B2C3D4"));
  CHECK(Has(text, "Stack pointer: 0x000000A1B2C3D4E0"));
  CHECK(Has(text, "Frame pointer: 0x000000A1B2C3D500"));
  CHECK(Has(text, "Stack trace: unavailable"));
  CHECK(!Has(text, "parameter["));
}

void TestExceptionCodeNames() {
  std::printf("short exception-code symbol table\n");
  CHECK(std::strcmp(mua::ExceptionCodeName(0xC0000374u), "STATUS_HEAP_CORRUPTION") == 0);
  CHECK(std::strcmp(mua::ExceptionCodeName(0xC0000005u), "ACCESS_VIOLATION") == 0);
  CHECK(std::strcmp(mua::ExceptionCodeName(0xC0000409u), "STATUS_STACK_BUFFER_OVERRUN") == 0);
  CHECK(mua::ExceptionCodeName(0xDEADBEEFu) == nullptr);
}

void TestAccessViolations() {
  std::printf("access violation read/write/execute formatting\n");
  CHECK(Has(AccessViolationText(0), "Exception code: 0xC0000005 (ACCESS_VIOLATION)"));
  CHECK(Has(AccessViolationText(0), "Memory access: read"));
  CHECK(Has(AccessViolationText(0), "Referenced address: 0x0000000000001234"));
  CHECK(Has(AccessViolationText(1), "Memory access: write"));
  CHECK(Has(AccessViolationText(8), "Memory access: execute"));
}

void TestModuleOffset() {
  std::printf("module-relative offset calculation\n");
  mua::CrashReport report = BaseReport();
  report.address = 0x00007FF6A1B00000ull + 0x1234u;
  CHECK(Has(Format(report), "offset 0x0000000000001234"));
}

void TestUnknownModuleFallback() {
  std::printf("unknown module fallback\n");
  mua::CrashReport report = BaseReport();
  report.faulting_module = {};
  report.frames[0] = {0x00007FF6A1B2C3D4ull, {}};
  report.frame_count = 1;
  const std::string text = Format(report);
  CHECK(Has(text, "Faulting module: unknown"));
  CHECK(Has(text, "#00 0x00007FF6A1B2C3D4 [unknown module]"));
}

void TestStackFrames() {
  std::printf("native frames with module-relative offsets\n");
  mua::CrashReport report = BaseReport();
  report.frames[0] = {0x00007FF6A1B01234ull, {"marvel_ultimate_alliance_bundle.exe", 0x00007FF6A1B00000ull, true}};
  report.frames[1] = {0x00007FFB12345678ull, {"ntdll.dll", 0x00007FFB12340000ull, true}};
  report.frame_count = 2;
  const std::string text = Format(report);
  CHECK(Has(text, "Stack trace: 2 frames"));
  CHECK(Has(text, "#00 0x00007FF6A1B01234 marvel_ultimate_alliance_bundle.exe + 0x0000000000001234"));
  CHECK(Has(text, "#01 0x00007FFB12345678 ntdll.dll + 0x0000000000005678"));
}

void TestParametersBounded() {
  std::printf("exception parameters are bounded\n");
  mua::CrashReport report = BaseReport();
  report.parameter_count = 99;
  for (size_t i = 0; i < mua::kCrashReportMaxParameters; ++i) {
    report.parameters[i] = i + 1;
  }
  const std::string text = Format(report);
  CHECK(Has(text, "Exception parameters: 15"));
  CHECK(Has(text, "parameter[14]: 0x000000000000000F"));
  CHECK(!Has(text, "parameter[15]"));
}

void TestGuestAndRuntimeState() {
  std::printf("guest mapping and last runtime state\n");
  mua::CrashReport report = BaseReport();
  report.guest_function = {0x825520F8u, "sub_825520F8", true};
  snprintf(report.runtime_gpu, sizeof(report.runtime_gpu), "%s", "NVIDIA GeForce RTX 4070");
  snprintf(report.runtime_display, sizeof(report.runtime_display), "%s", "2560x1440 | window=1280x720");
  snprintf(report.runtime_timing, sizeof(report.runtime_timing), "%s",
           "guest 60Hz | host VBlank (VSync On)");
  snprintf(report.runtime_renderer, sizeof(report.runtime_renderer), "%s", "render=100% 1280x720 | msaa=2x native");
  const std::string text = Format(report);
  CHECK(Has(text, "Guest function: sub_825520F8"));
  CHECK(Has(text, "Guest address: 0x825520F8"));
  CHECK(Has(text, "Last runtime state:"));
  CHECK(Has(text, "gpu=NVIDIA GeForce RTX 4070"));
  CHECK(Has(text, "display=2560x1440 | window=1280x720"));
  CHECK(Has(text, "timing=guest 60Hz | host VBlank (VSync On)"));
  CHECK(Has(text, "renderer=render=100% 1280x720 | msaa=2x native"));
  CHECK(!Has(text, "Guest thread:"));
  CHECK(!Has(text, "Guest data address:"));
}

void TestGuestThreadState() {
  std::printf("guest thread registers and the guest address a fault referenced\n");
  mua::CrashReport report = BaseReport();
  report.guest_thread.known = true;
  report.guest_thread.thread_id = 0x1A;
  report.guest_thread.lr = 0x8248A2B8;
  report.guest_thread.r1 = 0x7004FC40;
  for (uint32_t i = 0; i < 10; ++i) {
    report.guest_thread.r3_to_r12[i] = 0x100 + i;
  }
  report.guest_thread.data_address_known = true;
  report.guest_thread.data_address = 0x00000010;
  std::string text = Format(report);
  CHECK(Has(text, "Guest thread: 0x0000001A"));
  CHECK(Has(text, "Guest registers: lr=0x8248A2B8 r1=0x7004FC40 r3=0x00000100 r4=0x00000101"));
  CHECK(Has(text, "r12=0x00000109\n"));
  CHECK(Has(text, "Guest data address: 0x00000010 (virtual)"));
  report.guest_thread.data_address_physical = true;
  CHECK(Has(Format(report), "Guest data address: 0x00000010 (physical)"));
}

void TestRecursionGuard() {
  std::printf("crash writer recursion guard\n");
  mua::crash_detail::ResetCrashWrite();
  CHECK(mua::crash_detail::EnterCrashWrite());
  CHECK(mua::crash_detail::CrashWriteActive());
  CHECK(!mua::crash_detail::EnterCrashWrite());
  mua::crash_detail::ResetCrashWrite();
  CHECK(!mua::crash_detail::CrashWriteActive());
  CHECK(mua::crash_detail::EnterCrashWrite());
  mua::crash_detail::ResetCrashWrite();
}

#if defined(_WIN32)

std::wstring SelfPath() {
  wchar_t path[MAX_PATH] = L"";
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::wstring Utf16(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring wide(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length);
  wide.resize(size_t(length) - 1);
  return wide;
}

std::string Utf8(const std::wstring& wide) {
  if (wide.empty()) {
    return {};
  }
  const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string utf8(size_t(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), length, nullptr, nullptr);
  utf8.resize(size_t(length) - 1);
  return utf8;
}

std::filesystem::path TempDir() {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                    ("mua_crash_report_tests_" + std::to_string(GetCurrentProcessId()));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void WriteText(const std::filesystem::path& file, const std::string& text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out.write(text.data(), std::streamsize(text.size()));
}

std::string ReadText(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool AnyFile(const std::filesystem::path& dir, const char* prefix, const char* extension) {
  std::error_code ec;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    if (entry.path().filename().string().starts_with(prefix) && entry.path().extension() == extension) {
      return true;
    }
  }
  return false;
}

// Runs this executable as the child mode; returns its exit code.
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

int RunChildMode(const std::wstring& mode, const std::string& log) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  mua::InstallCrashReporting(log.c_str());
  mua::InstallCrashReporting(log.c_str());
  mua::SetCrashRuntimeState("Test GPU", "1920x1080 | window=1280x720",
                            "guest 60Hz | host VBlank (VSync On)", "render=100% 1280x720 | msaa=2x native");
  if (mode == L"corruption") {
    RaiseException(0xC0000374u, 0, 0, nullptr);
  } else if (mode == L"access-violation") {
    ULONG_PTR parameters[2] = {1, 0x1234};
    RaiseException(0xC0000005u, 0, 2, parameters);
  } else if (mode == L"exit") {
    std::_Exit(0);
  }
  return 0;
}

void TestInstallOnce() {
  std::printf("crash reporter installs once and repeated install is harmless\n");
  CHECK(!mua::CrashReportingInstalled());
  mua::InstallCrashReporting("C:\\mua_crash_report_tests\\install.log");
  CHECK(mua::CrashReportingInstalled());
  mua::InstallCrashReporting("C:\\mua_crash_report_tests\\install.log");
  CHECK(mua::CrashReportingInstalled());
}

void TestRealCrashAppendsToActiveLog() {
  std::printf("a real unhandled exception appends one block to the active log\n");
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path active = dir / "marvel_ultimate_alliance_023.log";
  const std::filesystem::path older = dir / "marvel_ultimate_alliance_022.log";
  WriteText(active, "run started\n");
  WriteText(older, "previous run\n");

  CHECK(RunChild(L"--child corruption \"" + active.wstring() + L"\"") == 0xC0000374u);

  const std::string text = ReadText(active);
  CHECK(Has(text, "run started"));
  CHECK(CountOf(text, "CRASH REPORT") == 1);
  CHECK(Has(text, "Exception code: 0xC0000374 (STATUS_HEAP_CORRUPTION)"));
  CHECK(Has(text, "Exception address: 0x"));
  CHECK(Has(text, "Product: Marvel Ultimate Alliance"));
  CHECK(Has(text, "Build: RelWithDebInfo x86-64"));
  CHECK(Has(text, "Uptime: 00:00:00") || Has(text, "Uptime: 00:00:01"));
  CHECK(Has(text, "Faulting module:"));
  CHECK(Has(text, "Thread ID:"));
  CHECK(Has(text, "Process ID:"));
  CHECK(Has(text, "Instruction pointer: 0x"));
  CHECK(Has(text, "Last runtime state:"));
  CHECK(Has(text, "gpu=Test GPU"));
  CHECK(Has(text, "timing=guest 60Hz | host VBlank (VSync On)"));
  CHECK(Has(text, "renderer=render=100% 1280x720 | msaa=2x native"));
  CHECK(!Has(text, "parameter["));
  CHECK(ReadText(older) == "previous run\n");
  CHECK(!AnyFile(dir, "crash-", ".txt"));
  CHECK(!AnyFile(dir, "", ".dmp"));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

void TestRealAccessViolationBlock() {
  std::printf("a real access violation prints its classification and address\n");
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path log = dir / "run_av.log";

  CHECK(RunChild(L"--child access-violation \"" + log.wstring() + L"\"") == 0xC0000005u);

  const std::string text = ReadText(log);
  CHECK(CountOf(text, "CRASH REPORT") == 1);
  CHECK(Has(text, "Exception code: 0xC0000005 (ACCESS_VIOLATION)"));
  CHECK(Has(text, "Exception parameters: 2"));
  CHECK(Has(text, "Memory access: write"));
  CHECK(Has(text, "Referenced address: 0x0000000000001234"));
  CHECK(!AnyFile(dir, "crash-", ".txt"));
  CHECK(!AnyFile(dir, "", ".dmp"));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

void TestIntentionalExitHasNoBlock() {
  std::printf("intentional hard exit and normal return leave no crash block\n");
  const std::filesystem::path dir = TempDir();
  const std::filesystem::path hard_exit = dir / "hard_exit.log";
  const std::filesystem::path normal = dir / "normal_exit.log";
  WriteText(hard_exit, "Window closing, hard-exiting process.\n");

  CHECK(RunChild(L"--child exit \"" + hard_exit.wstring() + L"\"") == 0);
  CHECK(!Has(ReadText(hard_exit), "CRASH REPORT"));
  CHECK(Has(ReadText(hard_exit), "hard-exiting process"));

  CHECK(RunChild(L"--child normal \"" + normal.wstring() + L"\"") == 0);
  CHECK(!Has(ReadText(normal), "CRASH REPORT"));

  CHECK(!AnyFile(dir, "crash-", ".txt"));
  CHECK(!AnyFile(dir, "", ".dmp"));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

#endif

}  // namespace

int main() {
#if defined(_WIN32)
  int wide_argc = 0;
  LPWSTR* wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv != nullptr && wide_argc >= 4 && std::wstring_view(wide_argv[1]) == L"--child") {
    const std::wstring mode = wide_argv[2];
    const std::string log = Utf8(wide_argv[3]);
    LocalFree(wide_argv);
    return RunChildMode(mode, log);
  }
  if (wide_argv != nullptr) {
    LocalFree(wide_argv);
  }
#endif

  TestHeapCorruptionBlock();
  TestExceptionCodeNames();
  TestAccessViolations();
  TestModuleOffset();
  TestUnknownModuleFallback();
  TestStackFrames();
  TestParametersBounded();
  TestGuestAndRuntimeState();
  TestGuestThreadState();
  TestRecursionGuard();
#if defined(_WIN32)
  TestInstallOnce();
  TestRealCrashAppendsToActiveLog();
  TestRealAccessViolationBlock();
  TestIntentionalExitHasNoBlock();
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
