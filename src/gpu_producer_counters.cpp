// DEBUG_SAULO, temporary, off by default: calls and inclusive host time of the MUA1 Direct3D routines that build GPU
// commands (command buffer reserve, draw and state flush paths) and of the D3D physical allocator, one log line per
// 10 s.

#if defined(MUA_RUNTIME_MUA1)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <intrin.h>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "d3d_state_filter.h"

REXCVAR_DEFINE_BOOL(mua_debug_gpu_producers, false, "Marvel Ultimate Alliance",
                    "DEBUG_SAULO: time the D3D command producers and log them every 10 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

struct Producer {
  const char* name;
  std::atomic<uint64_t> calls{0};
  std::atomic<uint64_t> ticks{0};
};

std::array<Producer, 12> g_producers = {{
    {"82472E98 cmd reserve"},
    {"82487620"},
    {"824862A8"},
    {"824864D0"},
    {"82486590"},
    {"82486710"},
    {"82486870"},
    {"82486008"},
    {"82484DC0 draw_indx_2"},
    {"824767D8"},
    {"82468A78 phys free"},
    {"824689E0 phys alloc"},
}};

constexpr int kMaxReports = 90;
std::atomic<int> g_reports{0};
std::atomic<int64_t> g_window_start_ns{0};
std::atomic<uint64_t> g_window_start_tsc{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MaybeReport() {
  const int64_t now = NowNs();
  int64_t start = g_window_start_ns.load(std::memory_order_relaxed);
  if (start == 0) {
    if (g_window_start_ns.compare_exchange_strong(start, now)) {
      g_window_start_tsc.store(__rdtsc(), std::memory_order_relaxed);
    }
    return;
  }
  if (now - start < 10'000'000'000 || g_reports.load(std::memory_order_relaxed) >= kMaxReports ||
      !g_window_start_ns.compare_exchange_strong(start, now)) {
    return;
  }
  const uint64_t tsc = __rdtsc();
  const uint64_t window_ticks = tsc - g_window_start_tsc.exchange(tsc, std::memory_order_relaxed);
  const double seconds = (now - start) / 1e9;
  const double ms_per_tick = window_ticks ? seconds * 1000.0 / double(window_ticks) : 0.0;
  std::string text;
  for (Producer& producer : g_producers) {
    const uint64_t calls = producer.calls.exchange(0, std::memory_order_relaxed);
    const uint64_t ticks = producer.ticks.exchange(0, std::memory_order_relaxed);
    if (calls) {
      text += fmt::format("{}{}: {:.0f}/s {:.2f} ms/s", text.empty() ? "" : " | ", producer.name, calls / seconds,
                          ticks * ms_per_tick / seconds);
    }
  }
  g_reports.fetch_add(1, std::memory_order_relaxed);
  REXLOG_INFO("DEBUG_SAULO guest GPU producers: {}", text.empty() ? "none" : text);
}

template <size_t Index>
void Timed(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*)) {
  if (!REXCVAR_GET(mua_debug_gpu_producers)) [[likely]] {
    original(ctx, base);
    return;
  }
  const uint64_t start = __rdtsc();
  original(ctx, base);
  Producer& producer = g_producers[Index];
  producer.ticks.fetch_add(__rdtsc() - start, std::memory_order_relaxed);
  if (producer.calls.fetch_add(1, std::memory_order_relaxed) % 4096 == 0) {
    MaybeReport();
  }
}

}  // namespace

#define MUA_TIMED_PRODUCER(index, address)                      \
  REX_EXTERN(__imp__sub_##address);                             \
  REX_HOOK_RAW(sub_##address) {                                 \
    Timed<index>(ctx, base, &__imp__sub_##address);             \
  }

// The upload routines run natively (d3d_state_filter.cpp); this file owns their hooks while it times them.
#define MUA_TIMED_NATIVE(index, address, native) \
  REX_HOOK_RAW(sub_##address) {                  \
    Timed<index>(ctx, base, &native);            \
  }

MUA_TIMED_PRODUCER(0, 82472E98)
MUA_TIMED_PRODUCER(1, 82487620)
MUA_TIMED_PRODUCER(2, 824862A8)
MUA_TIMED_NATIVE(3, 824864D0, mua::d3d::UploadRegisters)
MUA_TIMED_PRODUCER(4, 82486590)
MUA_TIMED_NATIVE(5, 82486710, mua::d3d::UploadFetchConstants)
MUA_TIMED_NATIVE(6, 82486870, mua::d3d::UploadAluConstants)
MUA_TIMED_PRODUCER(7, 82486008)
MUA_TIMED_PRODUCER(8, 82484DC0)
MUA_TIMED_PRODUCER(9, 824767D8)
MUA_TIMED_PRODUCER(10, 82468A78)
MUA_TIMED_PRODUCER(11, 824689E0)

#endif  // MUA_RUNTIME_MUA1
