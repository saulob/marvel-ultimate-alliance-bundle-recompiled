// Times World::Update for the GPU plugin's Performance line and logs, every 10 seconds, the delta the game stepped by
// (variable, from the wall clock), so a report shows whether game time keeps up with real time.

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#include "frame_limiter.h"
#include "frame_pacing.h"
#include "guest_frame_counters.h"
#include "guest_map.h"
#include "save_diagnostics.h"

namespace {

constexpr int kMaxPresentReports = 12;
constexpr uint32_t kGameFrameIntervalOffset = 24;
constexpr double kSlowWorldUpdateMs = 50.0;
constexpr int kMaxSlowWorldUpdateReports = 40;

std::mutex g_report_mutex;
mua::PacingReport g_report;
std::atomic<int> g_slow_update_reports{0};

std::mutex g_present_mutex;
mua::CallRateReport g_present_report;
int g_present_reports = 0;
double g_fastest_present_seconds = 1e9;
thread_local double t_last_present_seconds = 0.0;

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A slow update next to a Hitch line puts the hitch on the game thread rather than the GPU.
void NoteWorldUpdateTime(std::chrono::steady_clock::duration elapsed) {
  mua::AddGuestFrameTime(mua::GuestFrameTime::kWorldUpdate, elapsed);
  const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
  if (ms >= kSlowWorldUpdateMs &&
      g_slow_update_reports.fetch_add(1, std::memory_order_relaxed) < kMaxSlowWorldUpdateReports) {
    REXLOG_WARN("Slow world update: {:.1f} ms", ms);
  }
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE));
REX_HOOK_RAW(MUA_GUEST_WORLD_UPDATE) {
  const uint32_t world = ctx.r3.u32;
  const auto update_start = std::chrono::steady_clock::now();
  MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE)(ctx, base);
  NoteWorldUpdateTime(std::chrono::steady_clock::now() - update_start);
  const double delta = std::bit_cast<float>(mua::LoadBE32(base, world + mua::guest::kWorldDeltaOffset));
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard lock(g_report_mutex);
  if (const auto line = g_report.Add(delta, now)) {
    REXLOG_INFO("Game timing: frame rate={} | {}",
                mua::FrameRateLabel(mua::ConfiguredFrameRate(), mua::RuntimeFramePolicy()), *line);
  }
}

// The game's max_fps option follows the launcher rate; the interval is stored at options + 24. A game paced by the
// host limiter keeps its own interval.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_OPTIONS_INIT));
REX_HOOK_RAW(MUA_GUEST_OPTIONS_INIT) {
  const uint32_t options = ctx.r3.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_OPTIONS_INIT)(ctx, base);
  const mua::FramePolicy& policy = mua::RuntimeFramePolicy();
  const int launcher = mua::ConfiguredFrameRate();
  if (policy.host_limiter) {
    REXLOG_INFO("Game max_fps: launcher={} game interval={:.6f} s (host limiter at {})",
                mua::FrameRateLabel(launcher, policy),
                std::bit_cast<float>(mua::LoadBE32(base, options + kGameFrameIntervalOffset)),
                mua::ProjectLimiterRate(launcher, policy));
    return;
  }
  const int effective = mua::GameMaxFpsFor(launcher, policy);
  const float interval = mua::GameFrameInterval(effective);
  mua::StoreBE32(base, options + kGameFrameIntervalOffset, std::bit_cast<uint32_t>(interval));
  REXLOG_INFO("Game max_fps: launcher={} native={} effective={} interval={:.6f} s",
              mua::FrameRateLabel(launcher, policy), policy.native_max_fps,
              effective > 0 ? std::to_string(effective) : std::string("Unlimited"), interval);
}

namespace mua {

void CountPresentForDiagnostics() {
  std::lock_guard lock(g_present_mutex);
  if (g_present_reports >= kMaxPresentReports) {
    return;
  }
  const double now = NowSeconds();
  if (t_last_present_seconds > 0.0) {
    g_fastest_present_seconds = std::min(g_fastest_present_seconds, now - t_last_present_seconds);
  }
  t_last_present_seconds = now;
  if (const auto rate = g_present_report.Add(now)) {
    ++g_present_reports;
    REXLOG_INFO("Presents: presents/s={:.1f} | fastest present interval={:.1f} ms", *rate,
                1000.0 * g_fastest_present_seconds);
    g_fastest_present_seconds = 1e9;
  }
}

}  // namespace mua
