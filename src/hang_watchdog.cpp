// Adapted from crackdown-xbox360-recompiled src/hang_watchdog.cpp.
// A freeze leaves nothing in the log: Windows only reports "stopped responding" with no stack. This watchdog writes
// the call stack of every thread when the window stops answering messages or the title stops producing frames, so
// the thread that holds things up and what it waits on can be read afterwards.

#include "hang_watchdog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <optional>
#include <vector>

#include <rex/logging.h>

#if defined(_WIN32)
#include <fstream>
#include <string>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format off
#include <dbghelp.h>
#include <tlhelp32.h>
// clang-format on

#include <fmt/format.h>

#include "path_utf8.h"
#endif

namespace mua {
namespace {

std::atomic<uint64_t> g_guest_frames{0};

// Frame pacing, independent of the Windows-only hang detection below: every kFrameStatsWindow, the average and
// 1%-low FPS and the worst frame; a single frame at or above kHitchThresholdMs logs on its own, right away.
constexpr std::chrono::seconds kFrameStatsWindow{10};
constexpr double kHitchThresholdMs = 50.0;

std::mutex g_frame_stats_mutex;
std::vector<double> g_frame_times_ms;
std::chrono::steady_clock::time_point g_frame_stats_window_start;
std::optional<std::chrono::steady_clock::time_point> g_last_frame_time;

void FlushFrameStatsWindow(std::chrono::steady_clock::time_point now) {
  if (!g_frame_times_ms.empty()) {
    const FrameStatsSummary summary = SummarizeFrameTimesMs(g_frame_times_ms);
    REXLOG_INFO(
        "Frame pacing: {:.1f} fps average, {:.1f} fps 1% low, {:.1f} ms worst, {} frame(s) >= 50 ms, {} >= 100 ms",
        summary.average_fps, summary.low_1_percent_fps, summary.worst_ms, summary.frames_over_50ms,
        summary.frames_over_100ms);
    g_frame_times_ms.clear();
  }
  g_frame_stats_window_start = now;
}

void RecordFrameStats() {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(g_frame_stats_mutex);
  if (!g_last_frame_time) {
    g_frame_stats_window_start = now;
  } else {
    const double frame_ms = std::chrono::duration<double, std::milli>(now - *g_last_frame_time).count();
    if (frame_ms >= kHitchThresholdMs) {
      REXLOG_WARN("Hitch: {:.1f} ms frame", frame_ms);
    }
    g_frame_times_ms.push_back(frame_ms);
  }
  g_last_frame_time = now;
  if (now - g_frame_stats_window_start >= kFrameStatsWindow) {
    FlushFrameStatsWindow(now);
  }
}

#if defined(_WIN32)
constexpr DWORD kWindowTimeoutMs = 5000;
constexpr auto kFrameStall = std::chrono::seconds(10);
constexpr auto kPoll = std::chrono::seconds(1);
constexpr int kMaxReports = 3;
constexpr size_t kMaxFrames = 48;

struct ThreadStack {
  DWORD id = 0;
  std::wstring name;
  size_t count = 0;
  DWORD64 frames[kMaxFrames] = {};
};

bool Readable(DWORD64 address) {
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info)) {
    return false;
  }
  constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                              PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  return info.State == MEM_COMMIT && (info.Protect & kReadable) && !(info.Protect & PAGE_GUARD) &&
         address + 64 <= reinterpret_cast<DWORD64>(info.BaseAddress) + info.RegionSize;
}

// Unwinds a suspended thread's context into return addresses. Allocation-free: the thread may hold the heap lock.
size_t Unwind(CONTEXT context, DWORD64* frames) {
  size_t count = 0;
  while (count < kMaxFrames && context.Rip != 0) {
    frames[count++] = context.Rip;
    if (!Readable(context.Rsp)) {
      break;
    }
    const DWORD64 rsp = context.Rsp;
    DWORD64 image_base = 0;
    if (PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr)) {
      void* handler_data = nullptr;
      DWORD64 frame = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry, &context, &handler_data, &frame, nullptr);
    } else {
      // A leaf function: the return address is on top of the stack.
      context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
      context.Rsp += 8;
    }
    if (context.Rsp <= rsp) {
      break;
    }
  }
  return count;
}

std::vector<ThreadStack> CaptureThreads() {
  std::vector<ThreadStack> stacks;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return stacks;
  }
  const DWORD process = GetCurrentProcessId();
  const DWORD self = GetCurrentThreadId();
  std::vector<DWORD> ids;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID == process && entry.th32ThreadID != self) {
      ids.push_back(entry.th32ThreadID);
    }
  }
  CloseHandle(snapshot);
  stacks.reserve(ids.size());
  for (DWORD id : ids) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!thread) {
      continue;
    }
    ThreadStack& stack = stacks.emplace_back();
    stack.id = id;
    PWSTR description = nullptr;
    if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
      stack.name = description;
      LocalFree(description);
    }
    if (SuspendThread(thread) != DWORD(-1)) {
      CONTEXT context{};
      context.ContextFlags = CONTEXT_FULL;
      if (GetThreadContext(thread, &context)) {
        stack.count = Unwind(context, stack.frames);
      }
      ResumeThread(thread);
    }
    CloseHandle(thread);
  }
  return stacks;
}

std::string Describe(HANDLE process, DWORD64 address) {
  alignas(SYMBOL_INFOW) uint8_t buffer[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
  symbol->MaxNameLen = 255;
  std::string text = fmt::format("{:016X}", address);
  IMAGEHLP_MODULEW64 module{};
  module.SizeOfStruct = sizeof(module);
  if (SymGetModuleInfoW64(process, address, &module)) {
    text += " " + Utf8(module.ModuleName);
  }
  DWORD64 displacement = 0;
  if (SymFromAddrW(process, address, &displacement, symbol)) {
    text += "!" + Utf8(std::wstring(symbol->Name, symbol->NameLen)) +
            fmt::format("+0x{:X}", displacement);
  }
  IMAGEHLP_LINEW64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD column = 0;
  if (SymGetLineFromAddrW64(process, address, &column, &line)) {
    text += fmt::format(" ({}:{})", Utf8(std::filesystem::path(line.FileName).filename()), line.LineNumber);
  }
  return text;
}

void WriteReport(const std::filesystem::path& folder, const char* reason) {
  const std::vector<ThreadStack> stacks = CaptureThreads();
  HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  static const bool symbols = SymInitializeW(process, nullptr, TRUE) != FALSE;
  if (symbols) {
    SymRefreshModuleList(process);
  }

  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  std::filesystem::path path;
  for (int n = 1; n < 1000; ++n) {
    path = folder / ("hang_" + std::to_string(n) + ".txt");
    if (!std::filesystem::exists(path, ec)) {
      break;
    }
  }
  std::ofstream out(path);
  out << "Reason: " << reason << "\nGuest frames so far: " << g_guest_frames.load() << "\n";
  for (const ThreadStack& stack : stacks) {
    out << "\nThread " << stack.id;
    if (!stack.name.empty()) {
      out << " \"" << Utf8(stack.name) << "\"";
    }
    out << "\n";
    for (size_t i = 0; i < stack.count; ++i) {
      out << "  " << i << "  " << (symbols ? Describe(process, stack.frames[i]) : fmt::format("{:016X}", stack.frames[i]))
          << "\n";
    }
  }
  out.close();
  REXLOG_ERROR("[HANG] {}: thread stacks written to {}", reason, Utf8(path));
}

void Watch(HWND window, std::filesystem::path folder) {
  int window_reports = 0;
  int stall_reports = 0;
  uint64_t last_frames = g_guest_frames.load();
  auto frames_changed = std::chrono::steady_clock::now();
  bool window_hung = false;
  bool frames_stalled = false;
  while (window_reports < kMaxReports || stall_reports < kMaxReports) {
    std::this_thread::sleep_for(kPoll);
    if (!IsWindow(window)) {
      return;
    }
    DWORD_PTR result = 0;
    const bool answered =
        SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, kWindowTimeoutMs, &result) != 0;
    if (!answered && !window_hung && window_reports < kMaxReports) {
      WriteReport(folder, "the window stopped answering messages for 5 s");
      ++window_reports;
    }
    window_hung = !answered;

    const auto now = std::chrono::steady_clock::now();
    const uint64_t frames = g_guest_frames.load();
    // A minimized or background window may legitimately stop the title.
    if (frames != last_frames || IsIconic(window) || GetForegroundWindow() != window) {
      last_frames = frames;
      frames_changed = now;
      frames_stalled = false;
    } else if (frames > 0 && !frames_stalled && stall_reports < kMaxReports && now - frames_changed >= kFrameStall) {
      frames_stalled = true;
      WriteReport(folder, "the title produced no frame for 10 s");
      ++stall_reports;
    }
  }
}
#endif

}  // namespace

FrameStatsSummary SummarizeFrameTimesMs(std::vector<double> times_ms) {
  FrameStatsSummary summary;
  if (times_ms.empty()) {
    return summary;
  }
  std::sort(times_ms.begin(), times_ms.end());
  const double total_ms = std::accumulate(times_ms.begin(), times_ms.end(), 0.0);
  const size_t low_count = std::max<size_t>(1, times_ms.size() / 100);
  const double low_total_ms = std::accumulate(times_ms.end() - low_count, times_ms.end(), 0.0);
  summary.average_fps = times_ms.size() * 1000.0 / total_ms;
  summary.low_1_percent_fps = low_count * 1000.0 / low_total_ms;
  summary.worst_ms = times_ms.back();
  summary.frames_over_50ms =
      size_t(std::count_if(times_ms.begin(), times_ms.end(), [](double v) { return v >= 50.0; }));
  summary.frames_over_100ms =
      size_t(std::count_if(times_ms.begin(), times_ms.end(), [](double v) { return v >= 100.0; }));
  return summary;
}

void NoteGuestFrame() {
  g_guest_frames.fetch_add(1, std::memory_order_relaxed);
  RecordFrameStats();
}

void StartHangWatchdog(void* native_window, const std::filesystem::path& logs_folder) {
#if defined(_WIN32)
  if (!native_window) {
    return;
  }
  std::thread(Watch, static_cast<HWND>(native_window), logs_folder).detach();
#else
  (void)native_window;
  (void)logs_folder;
#endif
}

}  // namespace mua
