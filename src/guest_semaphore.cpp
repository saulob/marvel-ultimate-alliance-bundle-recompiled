// Hooks on Marvel Ultimate Alliance 2's Alchemy semaphore, which the game's Havok memory allocator takes as a lock
// around every allocation and free.

#include "guest_semaphore.h"

#if defined(MUA_RUNTIME_MUA2)

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include "guest_map.h"

REXCVAR_DEFINE_INT32(mua_semaphore_spin_us, 50, "Marvel Ultimate Alliance",
                     "Microseconds a blocking wait on an Alchemy semaphore tries it without a timeout before the kernel "
                     "wait; 0 waits at once")
    .range(0, 200)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

using namespace mua::semaphore;
using Clock = std::chrono::steady_clock;

uint32_t Load32(uint8_t* base, uint32_t address) {
  return std::byteswap(
      std::atomic_ref<uint32_t>(*rex::memory::GuestPtr<uint32_t*>(base, address)).load(std::memory_order_acquire));
}

SpinScores g_scores;

// DEBUG_SAULO: semaphore waits and releases per world update, for the main thread and the others, every 10 s.
struct PathStats {
  std::array<uint64_t, size_t(WaitPath::kCount)> paths{};
  uint64_t waits = 0;
  uint64_t wait_ns = 0;
  uint64_t max_wait_ns = 0;
  uint64_t polls = 0;
  uint64_t failed_polls = 0;
  uint64_t non_blocking = 0;
  uint64_t releases = 0;
  uint64_t release_ns = 0;
  DurationHistogram waits_ns;
  DurationHistogram taken_spinning_ns;

  void Merge(const PathStats& other) {
    for (size_t i = 0; i < paths.size(); ++i) {
      paths[i] += other.paths[i];
    }
    waits += other.waits;
    wait_ns += other.wait_ns;
    max_wait_ns = std::max(max_wait_ns, other.max_wait_ns);
    polls += other.polls;
    failed_polls += other.failed_polls;
    non_blocking += other.non_blocking;
    releases += other.releases;
    release_ns += other.release_ns;
    waits_ns.Merge(other.waits_ns);
    taken_spinning_ns.Merge(other.taken_spinning_ns);
  }
};

struct ObjectStats {
  uint64_t waits = 0;
  uint64_t main_waits = 0;
  uint64_t wait_ns = 0;
  uint64_t gave_up = 0;
  uint64_t releases = 0;
  uint32_t caller = 0;
};

struct ThreadStats {
  PathStats paths;
  std::unordered_map<uint32_t, ObjectStats> objects;
  uint32_t events = 0;
};

constexpr uint32_t kFlushEvents = 512;
constexpr auto kReportPeriod = std::chrono::seconds(10);
constexpr size_t kTopObjects = 4;

std::mutex g_stats_mutex;
PathStats g_main;
PathStats g_others;
std::unordered_map<uint32_t, ObjectStats> g_objects;
Clock::time_point g_window_start;
int64_t g_window_updates = 0;

bool OnMainThread() {
  thread_local const bool main = [] {
    const auto* thread = rex::system::XThread::GetCurrentThread();
    return thread && thread->main_thread();
  }();
  return main;
}

std::string Duration(uint64_t ns) {
  return ns >= 1'000'000 ? std::format("{:.2f} ms", double(ns) / 1e6) : std::format("{:.1f} us", double(ns) / 1e3);
}

std::string PathLine(const PathStats& stats, double updates) {
  const auto per = [updates](uint64_t n) { return double(n) / updates; };
  return std::format(
      "{:.1f} blocking waits {:.3f} ms (taken at once {:.1f}, taken spinning {:.1f}, gave up {:.1f}, not spun {:.1f}; "
      "{:.1f} tries, {:.1f} failed) | wait p50 {} p90 {} p99 {} max {} | taken spinning p50 {} p90 {} | "
      "{:.1f} releases {:.3f} ms | {:.1f} non-blocking waits",
      per(stats.waits), per(stats.wait_ns) / 1e6, per(stats.paths[size_t(WaitPath::kFree)]),
      per(stats.paths[size_t(WaitPath::kSpinTaken)]), per(stats.paths[size_t(WaitPath::kSpinGaveUp)]),
      per(stats.paths[size_t(WaitPath::kSpinSkipped)]), per(stats.polls), per(stats.failed_polls),
      Duration(stats.waits_ns.Percentile(0.5)), Duration(stats.waits_ns.Percentile(0.9)),
      Duration(stats.waits_ns.Percentile(0.99)), Duration(stats.max_wait_ns),
      Duration(stats.taken_spinning_ns.Percentile(0.5)), Duration(stats.taken_spinning_ns.Percentile(0.9)),
      per(stats.releases), per(stats.release_ns) / 1e6, per(stats.non_blocking));
}

void Report(uint8_t* base, Clock::time_point now) {
  const int64_t updates_now = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdates);
  const int64_t updates = updates_now - g_window_updates;
  if (g_window_start != Clock::time_point() && updates > 0) {
    REXLOG_INFO("DEBUG_SAULO MUA2 semaphore waits per world update, main thread: {}", PathLine(g_main, double(updates)));
    REXLOG_INFO("DEBUG_SAULO MUA2 semaphore waits per world update, other threads: {}",
                PathLine(g_others, double(updates)));
    std::vector<std::pair<uint32_t, ObjectStats>> top(g_objects.begin(), g_objects.end());
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.second.wait_ns > b.second.wait_ns; });
    std::string objects;
    for (size_t i = 0; i < std::min(kTopObjects, top.size()); ++i) {
      const auto& [semaphore, stats] = top[i];
      objects += std::format(
          "{}{:08X} (max {}, spin score {}): {:.1f} waits ({:.1f} main) {:.3f} ms, {:.1f} gave up, {:.1f} releases, "
          "caller {:08X}",
          i ? "; " : "", semaphore, int32_t(Load32(base, semaphore + mua::guest::kSemaphoreMaxCountOffset)),
          g_scores.Score(semaphore), double(stats.waits) / double(updates), double(stats.main_waits) / double(updates),
          double(stats.wait_ns) / 1e6 / double(updates), double(stats.gave_up) / double(updates),
          double(stats.releases) / double(updates), stats.caller);
    }
    REXLOG_INFO("DEBUG_SAULO MUA2 semaphore top objects per world update: {}", objects.empty() ? "none" : objects);
  }
  g_main = {};
  g_others = {};
  g_objects.clear();
  g_window_start = now;
  g_window_updates = updates_now;
}

ThreadStats& LocalStats() {
  thread_local ThreadStats stats;
  return stats;
}

void Flush(uint8_t* base, ThreadStats& local) {
  const auto now = Clock::now();
  std::lock_guard lock(g_stats_mutex);
  (OnMainThread() ? g_main : g_others).Merge(local.paths);
  for (const auto& [semaphore, stats] : local.objects) {
    ObjectStats& total = g_objects[semaphore];
    total.waits += stats.waits;
    total.main_waits += stats.main_waits;
    total.wait_ns += stats.wait_ns;
    total.gave_up += stats.gave_up;
    total.releases += stats.releases;
    total.caller = stats.caller ? stats.caller : total.caller;
  }
  local = {};
  if (now - g_window_start >= kReportPeriod) {
    Report(base, now);
  }
}

void NoteEvent(uint8_t* base, ThreadStats& local) {
  if (++local.events >= kFlushEvents) {
    Flush(base, local);
  }
}

void NoteWait(uint8_t* base, uint32_t semaphore, uint32_t caller, const WaitOutcome& outcome, uint64_t ns) {
  ThreadStats& local = LocalStats();
  PathStats& stats = local.paths;
  ++stats.paths[size_t(outcome.path)];
  ++stats.waits;
  stats.wait_ns += ns;
  stats.max_wait_ns = std::max(stats.max_wait_ns, ns);
  stats.polls += outcome.polls;
  stats.failed_polls += outcome.failed_polls;
  stats.waits_ns.Add(ns);
  if (outcome.path == WaitPath::kSpinTaken) {
    stats.taken_spinning_ns.Add(ns);
  }
  ObjectStats& object = local.objects[semaphore];
  ++object.waits;
  object.main_waits += OnMainThread();
  object.wait_ns += ns;
  object.gave_up += outcome.path == WaitPath::kSpinGaveUp;
  object.caller = caller;
  NoteEvent(base, local);
}

void NoteNonBlockingWait(uint8_t* base) {
  ThreadStats& local = LocalStats();
  ++local.paths.non_blocking;
  NoteEvent(base, local);
}

void NoteRelease(uint8_t* base, uint32_t semaphore, uint64_t ns) {
  ThreadStats& local = LocalStats();
  ++local.paths.releases;
  local.paths.release_ns += ns;
  ++local.objects[semaphore].releases;
  NoteEvent(base, local);
}

uint64_t Since(Clock::time_point start) {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT));
// r3 result, r4 semaphore, r5 low byte: block (else a wait with no timeout). The original stores the failure result
// first and the success result once the kernel wait took the semaphore.
REX_HOOK_RAW(MUA_GUEST_SEMAPHORE_WAIT) {
  const uint32_t result = ctx.r3.u32;
  const uint32_t semaphore = ctx.r4.u32;
  const uint64_t blocking = ctx.r5.u64;
  const uint32_t success = Load32(base, mua::guest::kAlchemySuccess);
  if (!(blocking & 0xFF) || success == Load32(base, mua::guest::kAlchemyFailure)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT)(ctx, base);
    NoteNonBlockingWait(base);  // DEBUG_SAULO
    return;
  }
  const auto wait = [&](uint64_t flag) {
    ctx.r3.u64 = result;
    ctx.r4.u64 = semaphore;
    ctx.r5.u64 = flag;
    MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT)(ctx, base);
  };
  const uint32_t caller = uint32_t(ctx.lr);
  const auto start = Clock::now();
  const int spin_us = REXCVAR_GET(mua_semaphore_spin_us);
  const WaitOutcome outcome = WaitSpinningFirst(
      [&] { return int32_t(Load32(base, semaphore + mua::guest::kSemaphoreCountOffset)); },
      [&] {
        wait(blocking & ~uint64_t{0xFF});
        return Load32(base, result) == success;
      },
      [&] { wait(blocking); }, spin_us > 0 && g_scores.ShouldSpin(semaphore), std::chrono::microseconds(spin_us));
  g_scores.Record(semaphore, outcome.path);
  NoteWait(base, semaphore, caller, outcome, Since(start));  // DEBUG_SAULO
}

// DEBUG_SAULO: times the release, a kernel call that wakes the thread the semaphore goes to.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_SIGNAL));
REX_HOOK_RAW(MUA_GUEST_SEMAPHORE_SIGNAL) {
  const uint32_t semaphore = ctx.r4.u32;
  const auto start = Clock::now();
  MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_SIGNAL)(ctx, base);
  NoteRelease(base, semaphore, Since(start));
}

#endif
