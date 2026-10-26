// Adapted from crackdown-xbox360-recompiled src/hang_watchdog.cpp.
// A freeze leaves nothing in the log: Windows only reports "stopped responding" with no stack. This watchdog logs
// the call stack of every guest thread when the window stops answering messages or the title stops producing frames,
// so the thread that holds things up and what it waits on can be read afterwards.

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
std::atomic<int64_t> g_last_frame_ns{0};
std::atomic<uint32_t> g_frame_thread{0};

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
constexpr size_t kLoggedFrames = 8;

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

bool CaptureThread(DWORD id, ThreadStack& stack) {
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
  if (!thread) {
    return false;
  }
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
  return true;
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
    ThreadStack stack;
    if (CaptureThread(id, stack)) {
      stacks.push_back(std::move(stack));
    }
  }
  return stacks;
}

// DbgHelp is single-threaded; the watchdog and the hitch sampler share it.
std::mutex g_dbghelp_mutex;

bool InitSymbols(HANDLE process) {
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  static const bool symbols = SymInitializeW(process, nullptr, TRUE) != FALSE;
  if (symbols) {
    SymRefreshModuleList(process);
  }
  return symbols;
}

// module!function+offset; without symbols (a release has no PDB) module+offset, which the build's PDB resolves later.
std::string FrameName(HANDLE process, DWORD64 address) {
  IMAGEHLP_MODULEW64 module{};
  module.SizeOfStruct = sizeof(module);
  const bool has_module = SymGetModuleInfoW64(process, address, &module) != FALSE;
  alignas(SYMBOL_INFOW) uint8_t buffer[sizeof(SYMBOL_INFOW) + 256 * sizeof(wchar_t)] = {};
  auto* symbol = reinterpret_cast<SYMBOL_INFOW*>(buffer);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
  symbol->MaxNameLen = 255;
  DWORD64 displacement = 0;
  if (!SymFromAddrW(process, address, &displacement, symbol)) {
    return has_module ? fmt::format("{}+0x{:X}", Utf8(module.ModuleName), address - module.BaseOfImage)
                      : fmt::format("{:016X}", address);
  }
  std::string text = Utf8(std::wstring(symbol->Name, symbol->NameLen)) + fmt::format("+0x{:X}", displacement);
  return has_module ? Utf8(module.ModuleName) + "!" + text : text;
}

bool IsGuestFrame(const std::string& name) {
  return name.find("!__imp__sub_8") != std::string::npos || name.find("!sub_8") != std::string::npos;
}

// A thread parked in a host wait has an ntdll, KERNELBASE or win32u frame on top.
bool IsWaiting(const std::string& top) {
  return top.starts_with("ntdll!") || top.starts_with("KERNELBASE!") || top.starts_with("win32u!");
}

std::string ThreadLabel(const ThreadStack& stack) {
  return std::to_string(stack.id) + (stack.name.empty() ? "" : " \"" + Utf8(stack.name) + "\"");
}

std::string StackText(HANDLE process, const ThreadStack& stack) {
  std::string text;
  for (size_t i = 0; i < std::min(stack.count, kLoggedFrames); ++i) {
    text += (i ? " <- " : "") + FrameName(process, stack.frames[i]);
  }
  return text.empty() ? "no frames" : text;
}

bool HasGuestFrame(HANDLE process, const ThreadStack& stack) {
  for (size_t i = 0; i < stack.count; ++i) {
    if (IsGuestFrame(FrameName(process, stack.frames[i]))) {
      return true;
    }
  }
  return false;
}

void LogHang(const char* reason) {
  const std::vector<ThreadStack> stacks = CaptureThreads();
  HANDLE process = GetCurrentProcess();
  std::lock_guard dbghelp_lock(g_dbghelp_mutex);
  InitSymbols(process);
  REXLOG_ERROR("[HANG] {} (guest frames so far: {})", reason, g_guest_frames.load());
  for (const ThreadStack& stack : stacks) {
    if (stack.id == g_frame_thread.load(std::memory_order_relaxed) || HasGuestFrame(process, stack)) {
      REXLOG_ERROR("[HANG]   thread {}: {}", ThreadLabel(stack), StackText(process, stack));
    }
  }
}

void Watch(HWND window) {
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
      LogHang("the window stopped answering messages for 5 s");
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
      LogHang("the title produced no frame for 10 s");
      ++stall_reports;
    }
  }
}

#if MUA_HAS_RECOMPILED_CODE
// Where the presenting thread is once a frame stays open kHitchSampleAfter, and its stack and the other threads
// running guest code once it stays open kHitchStackAfter.
constexpr auto kHitchPoll = std::chrono::milliseconds(20);
constexpr auto kHitchSampleAfter = std::chrono::milliseconds(150);
constexpr auto kHitchStackAfter = std::chrono::milliseconds(500);
constexpr int kMaxHitchSamples = 40;
constexpr int kMaxHitchStacks = 10;

void LogHitchSample(HANDLE process, DWORD thread, int64_t open_ms) {
  ThreadStack stack;
  if (!thread || !CaptureThread(thread, stack)) {
    return;
  }
  std::lock_guard lock(g_dbghelp_mutex);
  InitSymbols(process);
  std::string first_guest = "none";
  for (size_t i = 0; i < stack.count; ++i) {
    std::string name = FrameName(process, stack.frames[i]);
    if (IsGuestFrame(name)) {
      first_guest = std::move(name);
      break;
    }
  }
  REXLOG_WARN("Hitch sample: {} ms open | top {} | guest {}", open_ms,
              stack.count ? FrameName(process, stack.frames[0]) : std::string("unknown"), first_guest);
}

void LogHitchStacks(HANDLE process, DWORD thread, int64_t open_ms) {
  const std::vector<ThreadStack> stacks = CaptureThreads();
  std::lock_guard lock(g_dbghelp_mutex);
  InitSymbols(process);
  std::string busy;
  for (const ThreadStack& stack : stacks) {
    if (stack.id == thread) {
      REXLOG_WARN("Hitch stack ({} ms): {}", open_ms, StackText(process, stack));
    } else if (stack.count && !IsWaiting(FrameName(process, stack.frames[0]))) {
      for (size_t i = 0; i < stack.count; ++i) {
        const std::string name = FrameName(process, stack.frames[i]);
        if (IsGuestFrame(name)) {
          busy += (busy.empty() ? "" : "; ") + ThreadLabel(stack) + " in " + name;
          break;
        }
      }
    }
  }
  REXLOG_WARN("Hitch: other guest threads running: {}", busy.empty() ? "none" : busy);
}

void SampleHitches(HWND window) {
  HANDLE process = GetCurrentProcess();
  {
    // Loads the program's symbols now rather than in the middle of the first hitch.
    std::lock_guard lock(g_dbghelp_mutex);
    if (InitSymbols(process)) {
      FrameName(process, reinterpret_cast<DWORD64>(&SampleHitches));
    }
  }
  int samples = 0;
  int stacks = 0;
  uint64_t frame_seen = 0;
  int stage = 0;
  while (samples < kMaxHitchSamples || stacks < kMaxHitchStacks) {
    std::this_thread::sleep_for(kHitchPoll);
    if (!IsWindow(window)) {
      return;
    }
    const uint64_t frames = g_guest_frames.load(std::memory_order_relaxed);
    if (frames != frame_seen) {
      frame_seen = frames;
      stage = 0;
      continue;
    }
    if (frames == 0 || IsIconic(window) || GetForegroundWindow() != window) {
      continue;
    }
    const auto open = std::chrono::steady_clock::now().time_since_epoch() -
                      std::chrono::nanoseconds(g_last_frame_ns.load(std::memory_order_relaxed));
    const int64_t open_ms = std::chrono::duration_cast<std::chrono::milliseconds>(open).count();
    const DWORD thread = g_frame_thread.load(std::memory_order_relaxed);
    if (stage == 0 && open >= kHitchSampleAfter && samples < kMaxHitchSamples) {
      stage = 1;
      ++samples;
      LogHitchSample(process, thread, open_ms);
    } else if (stage == 1 && open >= kHitchStackAfter && stacks < kMaxHitchStacks) {
      stage = 2;
      ++stacks;
      LogHitchStacks(process, thread, open_ms);
    }
  }
}

#endif  // MUA_HAS_RECOMPILED_CODE
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
  g_last_frame_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count(),
                        std::memory_order_relaxed);
#if defined(_WIN32)
  g_frame_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
#endif
  g_guest_frames.fetch_add(1, std::memory_order_relaxed);
  RecordFrameStats();
}

void StartHangWatchdog(void* native_window) {
#if defined(_WIN32)
  if (!native_window) {
    return;
  }
  std::thread(Watch, static_cast<HWND>(native_window)).detach();
#if MUA_HAS_RECOMPILED_CODE
  std::thread(SampleHitches, static_cast<HWND>(native_window)).detach();
#endif
#else
  (void)native_window;
#endif
}

}  // namespace mua
