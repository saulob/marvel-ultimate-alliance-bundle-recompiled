#include "crash_report.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(MUA_BUILD_CONFIG)
#define MUA_CRASH_BUILD_CONFIG MUA_BUILD_CONFIG
#else
#define MUA_CRASH_BUILD_CONFIG "unknown"
#endif

namespace mua {
namespace {

constexpr size_t kReportBufferBytes = 16384;
constexpr size_t kLogPathChars = 1024;
constexpr int kSeparatorWidth = 60;
constexpr char kHexDigits[] = "0123456789ABCDEF";
constexpr const char* kProduct = "Marvel Ultimate Alliance";
constexpr const char* kBuildConfig = MUA_CRASH_BUILD_CONFIG;

char g_game_version[32] = "unknown";
char g_runtime_gpu[96] = {};
char g_runtime_display[96] = {};
char g_runtime_timing[128] = {};
char g_runtime_renderer[160] = {};
std::atomic<CrashGuestFunctionResolver> g_guest_resolver{nullptr};
std::atomic<CrashGuestThreadResolver> g_guest_thread_resolver{nullptr};

void CopyText(char* out, size_t size, const char* text) {
  if (out == nullptr || size == 0) {
    return;
  }
  if (text == nullptr) {
    out[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < size && text[i] != '\0'; ++i) {
    out[i] = text[i];
  }
  out[i] = '\0';
}

void FillRuntimeState(CrashReport& report) {
  CopyText(report.runtime_gpu, sizeof(report.runtime_gpu), g_runtime_gpu);
  CopyText(report.runtime_display, sizeof(report.runtime_display), g_runtime_display);
  CopyText(report.runtime_timing, sizeof(report.runtime_timing), g_runtime_timing);
  CopyText(report.runtime_renderer, sizeof(report.runtime_renderer), g_runtime_renderer);
}

#if defined(_WIN32)
constexpr const char* kArchitecture =
#if defined(_M_ARM64)
    "arm64";
#elif defined(_M_X64)
    "x86-64";
#elif defined(_M_IX86)
    "x86";
#else
    "unknown";
#endif

wchar_t g_log_path[kLogPathChars];
char g_report_buffer[kReportBufferBytes];
CrashReport g_report;
volatile LONG g_filter_installed = 0;
uint64_t g_process_start_tick = 0;
#endif

#if defined(_WIN32)
using CrashGuard = volatile LONG;
#else
using CrashGuard = volatile int;
#endif

CrashGuard g_crash_write = 0;

class CrashWriter {
 public:
  CrashWriter(char* buffer, size_t size) : buffer_(buffer), size_(size) {}

  void Char(char value) {
    if (used_ + 1 < size_) {
      buffer_[used_] = value;
      ++used_;
    }
  }

  void Text(const char* text) {
    while (*text != '\0') {
      Char(*text++);
    }
  }

  void Hex(uint64_t value, int digits) {
    Char('0');
    Char('x');
    for (int digit = digits - 1; digit >= 0; --digit) {
      Char(kHexDigits[(value >> (digit * 4)) & 0xF]);
    }
  }

  void Dec(uint64_t value) {
    char digits[20];
    int count = 0;
    do {
      digits[count++] = char('0' + value % 10);
      value /= 10;
    } while (value != 0);
    while (count > 0) {
      Char(digits[--count]);
    }
  }

  void DecPadded(uint64_t value, int width) {
    char digits[20];
    int count = 0;
    do {
      digits[count++] = char('0' + value % 10);
      value /= 10;
    } while (value != 0);
    for (int pad = count; pad < width; ++pad) {
      Char('0');
    }
    while (count > 0) {
      Char(digits[--count]);
    }
  }

  void Duration(uint64_t milliseconds) {
    const uint64_t seconds = milliseconds / 1000;
    DecPadded(seconds / 3600, 2);
    Char(':');
    DecPadded((seconds / 60) % 60, 2);
    Char(':');
    DecPadded(seconds % 60, 2);
  }

  size_t Finish() {
    if (size_ > 0) {
      buffer_[used_] = '\0';
    }
    return used_;
  }

 private:
  char* buffer_;
  size_t size_;
  size_t used_ = 0;
};

void Prefix(CrashWriter& writer, const CrashReport& report) {
  writer.Char('[');
  writer.DecPadded(report.time.year, 4);
  writer.Char('-');
  writer.DecPadded(report.time.month, 2);
  writer.Char('-');
  writer.DecPadded(report.time.day, 2);
  writer.Char(' ');
  writer.DecPadded(report.time.hour, 2);
  writer.Char(':');
  writer.DecPadded(report.time.minute, 2);
  writer.Char(':');
  writer.DecPadded(report.time.second, 2);
  writer.Char('.');
  writer.DecPadded(report.time.millis, 3);
  writer.Text("] [error] [core] [t");
  writer.Dec(report.thread_id);
  writer.Text("] ");
}

void Separator(CrashWriter& writer, const CrashReport& report) {
  Prefix(writer, report);
  for (int i = 0; i < kSeparatorWidth; ++i) {
    writer.Char('=');
  }
  writer.Char('\n');
}

uint64_t ModuleOffset(uint64_t address, uint64_t base) {
  return address >= base ? address - base : 0;
}

uint32_t BoundedParameters(const CrashReport& report) {
  return report.parameter_count < kCrashReportMaxParameters ? report.parameter_count
                                                            : static_cast<uint32_t>(kCrashReportMaxParameters);
}

const char* AccessKind(const CrashReport& report) {
  if (report.code != 0xC0000005u || report.parameter_count < 1) {
    return nullptr;
  }
  switch (report.parameters[0]) {
    case 0:
      return "read";
    case 1:
      return "write";
    case 8:
      return "execute";
    default:
      return nullptr;
  }
}

#if defined(_WIN32)
void NarrowModuleName(char* out, size_t size, const wchar_t* name) {
  if (WideCharToMultiByte(CP_UTF8, 0, name, -1, out, static_cast<int>(size), nullptr, nullptr) > 0) {
    return;
  }
  size_t i = 0;
  while (name[i] != L'\0' && i + 1 < size) {
    out[i] = name[i] < 0x80 ? char(name[i]) : '?';
    ++i;
  }
  out[i] = '\0';
}

void FillModule(CrashModuleRef& module, const void* address) {
  HMODULE handle = nullptr;
  if (address == nullptr ||
      !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCWSTR>(address), &handle) ||
      handle == nullptr) {
    return;
  }
  wchar_t path[MAX_PATH] = L"";
  const DWORD length = GetModuleFileNameW(handle, path, MAX_PATH);
  const wchar_t* name = path;
  for (DWORD i = 0; i < length; ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      name = path + i + 1;
    }
  }
  module.known = true;
  module.base = reinterpret_cast<uint64_t>(handle);
  NarrowModuleName(module.name, sizeof(module.name), name);
}

void FillContext(CrashReport& report, const CONTEXT* context) {
  if (context == nullptr) {
    return;
  }
#if defined(_M_X64)
  report.has_context = true;
  report.instruction_pointer = context->Rip;
  report.stack_pointer = context->Rsp;
  report.frame_pointer = context->Rbp;
#elif defined(_M_IX86)
  report.has_context = true;
  report.instruction_pointer = context->Eip;
  report.stack_pointer = context->Esp;
  report.frame_pointer = context->Ebp;
#elif defined(_M_ARM64)
  report.has_context = true;
  report.instruction_pointer = context->Pc;
  report.stack_pointer = context->Sp;
  report.frame_pointer = context->Fp;
#endif
}

void FillFrames(CrashReport& report) {
  void* addresses[kCrashReportMaxFrames] = {};
  const USHORT count = RtlCaptureStackBackTrace(0, kCrashReportMaxFrames, addresses, nullptr);
  report.frame_count = count < kCrashReportMaxFrames ? count : static_cast<uint32_t>(kCrashReportMaxFrames);
  for (uint32_t i = 0; i < report.frame_count; ++i) {
    report.frames[i].address = reinterpret_cast<uint64_t>(addresses[i]);
    FillModule(report.frames[i].module, addresses[i]);
  }
}

void FillReport(CrashReport& report, const EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD& record = *info->ExceptionRecord;
  SYSTEMTIME now{};
  GetLocalTime(&now);
  report.time = {now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds};
  report.code = record.ExceptionCode;
  report.flags = record.ExceptionFlags;
  report.address = reinterpret_cast<uint64_t>(record.ExceptionAddress);
  report.parameter_count = record.NumberParameters < kCrashReportMaxParameters
                               ? record.NumberParameters
                               : static_cast<uint32_t>(kCrashReportMaxParameters);
  for (uint32_t i = 0; i < report.parameter_count; ++i) {
    report.parameters[i] = record.ExceptionInformation[i];
  }
  report.thread_id = GetCurrentThreadId();
  report.process_id = GetCurrentProcessId();
  report.product = kProduct;
  report.architecture = kArchitecture;
  report.game_version = g_game_version;
  report.build_config = kBuildConfig;
  report.uptime_ms = g_process_start_tick != 0 ? GetTickCount64() - g_process_start_tick : 0;
  FillModule(report.faulting_module, record.ExceptionAddress);
  FillContext(report, info->ContextRecord);
  FillFrames(report);
  if (const CrashGuestFunctionResolver resolver = g_guest_resolver.load(std::memory_order_acquire);
      resolver != nullptr) {
    resolver(report.address, &report.guest_function);
  }
  if (const CrashGuestThreadResolver resolver = g_guest_thread_resolver.load(std::memory_order_acquire);
      resolver != nullptr) {
    const bool access_violation = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2;
    resolver(access_violation ? record.ExceptionInformation[1] : 0, &report.guest_thread);
  }
  FillRuntimeState(report);
}

void WriteReport(const EXCEPTION_POINTERS* info) {
  g_report = CrashReport{};
  FillReport(g_report, info);
  const size_t length = FormatCrashReport(g_report, g_report_buffer, sizeof(g_report_buffer));
  if (length == 0) {
    return;
  }
  const HANDLE file = CreateFileW(g_log_path, FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  size_t offset = 0;
  while (offset < length) {
    const size_t remaining = length - offset;
    const DWORD chunk = static_cast<DWORD>(remaining < (1u << 20) ? remaining : (1u << 20));
    DWORD written = 0;
    if (!WriteFile(file, g_report_buffer + offset, chunk, &written, nullptr) || written == 0) {
      break;
    }
    offset += written;
  }
  FlushFileBuffers(file);
  CloseHandle(file);
}

LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* info) {
  if (!crash_detail::EnterCrashWrite()) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_log_path[0] != L'\0') {
    WriteReport(info);
  }
  // The default handler's ExitProcess would run DLL detach code in a faulted process.
  TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
  return EXCEPTION_EXECUTE_HANDLER;
}

void CacheLogFile(const char* utf8_log_file) {
  g_log_path[0] = L'\0';
  if (utf8_log_file == nullptr || *utf8_log_file == '\0') {
    return;
  }
  wchar_t path[kLogPathChars];
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_log_file, -1, path,
                                   static_cast<int>(kLogPathChars));
  if (length <= 0) {
    length = MultiByteToWideChar(CP_ACP, 0, utf8_log_file, -1, path, static_cast<int>(kLogPathChars));
  }
  if (length <= 0) {
    return;
  }
  for (int i = 0; i < length; ++i) {
    g_log_path[i] = path[i];
  }
}
#endif

}  // namespace

const char* ExceptionCodeName(uint32_t code) {
  switch (code) {
    case 0xC0000005u:
      return "ACCESS_VIOLATION";
    case 0xC0000006u:
      return "IN_PAGE_ERROR";
    case 0xC0000017u:
      return "STATUS_NO_MEMORY";
    case 0xC000001Du:
      return "ILLEGAL_INSTRUCTION";
    case 0xC0000025u:
      return "STATUS_NONCONTINUABLE_EXCEPTION";
    case 0xC0000094u:
      return "INTEGER_DIVIDE_BY_ZERO";
    case 0xC0000095u:
      return "INTEGER_OVERFLOW";
    case 0xC0000096u:
      return "PRIVILEGED_INSTRUCTION";
    case 0xC00000FDu:
      return "STATUS_STACK_OVERFLOW";
    case 0xC000013Au:
      return "STATUS_CONTROL_C_EXIT";
    case 0xC0000374u:
      return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409u:
      return "STATUS_STACK_BUFFER_OVERRUN";
    case 0xE06D7363u:
      return "C++ EXCEPTION";
    default:
      return nullptr;
  }
}

size_t FormatCrashReport(const CrashReport& report, char* buffer, size_t size) {
  CrashWriter writer(buffer, size);
  const uint32_t parameters = BoundedParameters(report);

  Separator(writer, report);
  Prefix(writer, report);
  writer.Text("CRASH REPORT\n");
  Separator(writer, report);

  Prefix(writer, report);
  writer.Text("Windows unhandled exception\n");

  if (report.product != nullptr) {
    Prefix(writer, report);
    writer.Text("Product: ");
    writer.Text(report.product);
    writer.Char('\n');
  }
  if (report.game_version != nullptr) {
    Prefix(writer, report);
    writer.Text("Version: ");
    writer.Text(report.game_version);
    writer.Char('\n');
  }
  if (report.build_config != nullptr) {
    Prefix(writer, report);
    writer.Text("Build: ");
    writer.Text(report.build_config);
    if (report.architecture != nullptr) {
      writer.Char(' ');
      writer.Text(report.architecture);
    }
    writer.Char('\n');
  }
  Prefix(writer, report);
  writer.Text("Uptime: ");
  writer.Duration(report.uptime_ms);
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Exception code: ");
  writer.Hex(report.code, 8);
  if (const char* name = ExceptionCodeName(report.code); name != nullptr) {
    writer.Text(" (");
    writer.Text(name);
    writer.Char(')');
  }
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Exception address: ");
  writer.Hex(report.address, 16);
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Exception flags: ");
  writer.Hex(report.flags, 8);
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Exception parameters: ");
  writer.Dec(parameters);
  writer.Char('\n');
  for (uint32_t i = 0; i < parameters; ++i) {
    Prefix(writer, report);
    writer.Text("  parameter[");
    writer.Dec(i);
    writer.Text("]: ");
    writer.Hex(report.parameters[i], 16);
    writer.Char('\n');
  }

  if (const char* access = AccessKind(report); access != nullptr) {
    Prefix(writer, report);
    writer.Text("Memory access: ");
    writer.Text(access);
    writer.Char('\n');
    if (parameters >= 2) {
      Prefix(writer, report);
      writer.Text("Referenced address: ");
      writer.Hex(report.parameters[1], 16);
      writer.Char('\n');
    }
  }

  Prefix(writer, report);
  writer.Text("Faulting module: ");
  if (report.faulting_module.known && report.faulting_module.name[0] != '\0') {
    writer.Text(report.faulting_module.name);
    writer.Text(" (base ");
    writer.Hex(report.faulting_module.base, 16);
    writer.Text(", offset ");
    writer.Hex(ModuleOffset(report.address, report.faulting_module.base), 16);
    writer.Char(')');
  } else {
    writer.Text("unknown");
  }
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Thread ID: ");
  writer.Dec(report.thread_id);
  writer.Char('\n');

  Prefix(writer, report);
  writer.Text("Process ID: ");
  writer.Dec(report.process_id);
  writer.Char('\n');

  if (report.guest_function.known) {
    Prefix(writer, report);
    writer.Text("Guest function: ");
    writer.Text(report.guest_function.name[0] != '\0' ? report.guest_function.name : "unknown");
    writer.Char('\n');
    Prefix(writer, report);
    writer.Text("Guest address: ");
    writer.Hex(report.guest_function.address, 8);
    writer.Char('\n');
  }
  if (report.guest_thread.known) {
    Prefix(writer, report);
    writer.Text("Guest thread: ");
    writer.Hex(report.guest_thread.thread_id, 8);
    writer.Char('\n');
    Prefix(writer, report);
    writer.Text("Guest registers: lr=");
    writer.Hex(report.guest_thread.lr, 8);
    writer.Text(" r1=");
    writer.Hex(report.guest_thread.r1, 8);
    for (uint32_t i = 0; i < 10; ++i) {
      writer.Text(" r");
      writer.Dec(i + 3);
      writer.Char('=');
      writer.Hex(report.guest_thread.r3_to_r12[i], 8);
    }
    writer.Char('\n');
  }
  if (report.guest_thread.data_address_known) {
    Prefix(writer, report);
    writer.Text("Guest data address: ");
    writer.Hex(report.guest_thread.data_address, 8);
    writer.Text(report.guest_thread.data_address_physical ? " (physical)" : " (virtual)");
    writer.Char('\n');
  }

  if (report.has_context) {
    Prefix(writer, report);
    writer.Text("Instruction pointer: ");
    writer.Hex(report.instruction_pointer, 16);
    writer.Char('\n');
    Prefix(writer, report);
    writer.Text("Stack pointer: ");
    writer.Hex(report.stack_pointer, 16);
    writer.Char('\n');
    if (report.frame_pointer != 0) {
      Prefix(writer, report);
      writer.Text("Frame pointer: ");
      writer.Hex(report.frame_pointer, 16);
      writer.Char('\n');
    }
  }

  if (report.frame_count == 0) {
    Prefix(writer, report);
    writer.Text("Stack trace: unavailable\n");
  } else {
    Prefix(writer, report);
    writer.Text("Stack trace: ");
    writer.Dec(report.frame_count);
    writer.Text(" frames\n");
    for (uint32_t i = 0; i < report.frame_count && i < kCrashReportMaxFrames; ++i) {
      const CrashFrame& frame = report.frames[i];
      Prefix(writer, report);
      writer.Text("  #");
      if (i < 10) {
        writer.Char('0');
      }
      writer.Dec(i);
      writer.Char(' ');
      writer.Hex(frame.address, 16);
      if (frame.module.known && frame.module.name[0] != '\0') {
        writer.Char(' ');
        writer.Text(frame.module.name);
        writer.Text(" + ");
        writer.Hex(ModuleOffset(frame.address, frame.module.base), 16);
      } else {
        writer.Text(" [unknown module]");
      }
      writer.Char('\n');
    }
  }

  const bool has_runtime_state =
      report.runtime_gpu[0] != '\0' || report.runtime_display[0] != '\0' || report.runtime_timing[0] != '\0' ||
      report.runtime_renderer[0] != '\0';
  if (has_runtime_state) {
    Prefix(writer, report);
    writer.Text("Last runtime state:\n");
    const auto field = [&](const char* name, const char* value) {
      if (value[0] == '\0') {
        return;
      }
      Prefix(writer, report);
      writer.Text("  ");
      writer.Text(name);
      writer.Char('=');
      writer.Text(value);
      writer.Char('\n');
    };
    field("gpu", report.runtime_gpu);
    field("display", report.runtime_display);
    field("timing", report.runtime_timing);
    field("renderer", report.runtime_renderer);
  }

  Separator(writer, report);
  return writer.Finish();
}

void InstallCrashReporting(const char* utf8_log_file) {
#if defined(_WIN32)
  CacheLogFile(utf8_log_file);
  if (g_log_path[0] == L'\0') {
    return;
  }
  if (g_process_start_tick == 0) {
    g_process_start_tick = GetTickCount64();
  }
  if (InterlockedCompareExchange(&g_filter_installed, 1, 0) == 0) {
    SetUnhandledExceptionFilter(&HandleUnhandledException);
  }
#else
  (void)utf8_log_file;
#endif
}

void SetCrashGameVersion(const char* version) {
  CopyText(g_game_version, sizeof(g_game_version), version);
}

void SetCrashRuntimeState(const char* gpu, const char* display, const char* timing, const char* renderer) {
  CopyText(g_runtime_gpu, sizeof(g_runtime_gpu), gpu);
  CopyText(g_runtime_display, sizeof(g_runtime_display), display);
  CopyText(g_runtime_timing, sizeof(g_runtime_timing), timing);
  CopyText(g_runtime_renderer, sizeof(g_runtime_renderer), renderer);
}

void SetCrashGuestFunctionResolver(CrashGuestFunctionResolver resolver) {
  g_guest_resolver.store(resolver, std::memory_order_release);
}

void SetCrashGuestThreadResolver(CrashGuestThreadResolver resolver) {
  g_guest_thread_resolver.store(resolver, std::memory_order_release);
}

bool CrashReportingInstalled() {
#if defined(_WIN32)
  return g_filter_installed != 0;
#else
  return false;
#endif
}

namespace crash_detail {

bool EnterCrashWrite() {
#if defined(_WIN32)
  return InterlockedExchange(&g_crash_write, 1) == 0;
#else
  return __atomic_exchange_n(&g_crash_write, 1, __ATOMIC_ACQ_REL) == 0;
#endif
}

bool CrashWriteActive() {
  return g_crash_write != 0;
}

void ResetCrashWrite() {
  g_crash_write = 0;
}

}  // namespace crash_detail

}  // namespace mua
