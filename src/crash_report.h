#pragma once

#include <cstddef>
#include <cstdint>

namespace mua {

// Single-Log Windows Crash Reporting: an unhandled exception appends one CRASH REPORT block to the
// log file of the current run (Windows Crash Reporting, SetUnhandledExceptionFilter / Unhandled
// Exception Filter). A bug report only needs that same log, so no crash-*.txt and no minidump is
// written. The Windows write path avoids the logger, the heap and application locks; the block
// formatter stays pure so its layout is tested without a real crash. A no-op outside Windows.

struct CrashLogTime {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int millis = 0;
};

struct CrashModuleRef {
  char name[64] = {};
  uint64_t base = 0;
  bool known = false;
};

constexpr size_t kCrashReportMaxParameters = 15;
constexpr size_t kCrashReportMaxFrames = 16;

struct CrashFrame {
  uint64_t address = 0;
  CrashModuleRef module;
};

struct CrashGuestRef {
  uint32_t address = 0;
  char name[32] = {};
  bool known = false;
};

struct CrashReport {
  CrashLogTime time;
  uint32_t code = 0;
  uint32_t flags = 0;
  uint64_t address = 0;
  uint32_t parameter_count = 0;
  uint64_t parameters[kCrashReportMaxParameters] = {};
  CrashModuleRef faulting_module;
  uint32_t thread_id = 0;
  uint32_t process_id = 0;
  const char* product = nullptr;
  const char* architecture = nullptr;
  const char* game_version = nullptr;
  const char* build_config = nullptr;
  uint64_t uptime_ms = 0;
  CrashGuestRef guest_function;
  bool has_context = false;
  uint64_t instruction_pointer = 0;
  uint64_t stack_pointer = 0;
  uint64_t frame_pointer = 0;
  uint32_t frame_count = 0;
  CrashFrame frames[kCrashReportMaxFrames];
  char runtime_gpu[96] = {};
  char runtime_display[96] = {};
  char runtime_timing[128] = {};
  char runtime_renderer[160] = {};
};

// Last runtime state shown in the crash report. Written once the startup diagnostics are logged.
void SetCrashRuntimeState(const char* gpu, const char* display, const char* timing, const char* renderer);

// Resolves a native instruction address to a recompiled guest function. Installed by the guest-function lookup.
using CrashGuestFunctionResolver = void (*)(uint64_t host_address, CrashGuestRef* out);
void SetCrashGuestFunctionResolver(CrashGuestFunctionResolver resolver);

// Symbolic name of a common Windows exception code ("STATUS_HEAP_CORRUPTION" for 0xC0000374), or
// nullptr for codes outside the short table.
const char* ExceptionCodeName(uint32_t code);

// Writes the CRASH REPORT block. `buffer` is NUL-terminated when `size` is not zero; returns the
// number of characters written.
size_t FormatCrashReport(const CrashReport& report, char* buffer, size_t size);

// Windows: caches `utf8_log_file` as the target of crash reports and installs the unhandled
// exception filter once. An empty path disables crash reporting. A no-op outside Windows.
void InstallCrashReporting(const char* utf8_log_file);

// Windows: true once the unhandled exception filter is installed.
bool CrashReportingInstalled();

namespace crash_detail {

// Recursion and double-fault guard of the crash writer: the first caller enters, later callers
// fail and let the unhandled exception terminate the process.
bool EnterCrashWrite();
bool CrashWriteActive();
void ResetCrashWrite();

}  // namespace crash_detail

}  // namespace mua
