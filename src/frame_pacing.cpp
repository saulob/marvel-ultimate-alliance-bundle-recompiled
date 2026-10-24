// DEBUG_SAULO: samples the delta the game's World::Update used, bounded to one log line per 10 seconds and at most
// 12 lines, to check the frame model (variable delta from the wall clock) in a real run at each frame rate.

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

constexpr int kMaxReports = 12;
constexpr uint32_t kGameFrameIntervalOffset = 24;

std::mutex g_report_mutex;
mua::PacingReport g_report;
int g_reports = 0;

std::mutex g_present_mutex;
mua::CallRateReport g_present_report;
int g_present_reports = 0;
double g_fastest_present_seconds = 1e9;
thread_local double t_last_present_seconds = 0.0;

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

#if defined(MUA_RUNTIME_MUA1)
namespace {

constexpr int kMaxTimingReports = 90;
constexpr double kSlowWorldUpdateMs = 50.0;
constexpr int kMaxSlowWorldUpdateReports = 40;

std::mutex g_timing_mutex;
mua::WorldUpdateTiming g_timing;
int g_timing_reports = 0;
int g_slow_update_reports = 0;

// DEBUG_SAULO: World::Update host time, also published for the GPU plugin's Performance line.
void NoteWorldUpdateTime(std::chrono::steady_clock::duration elapsed) {
  mua::AddGuestFrameTime(mua::GuestFrameTime::kWorldUpdate, elapsed);
  const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
  std::lock_guard lock(g_timing_mutex);
  if (ms >= kSlowWorldUpdateMs && g_slow_update_reports < kMaxSlowWorldUpdateReports) {
    ++g_slow_update_reports;
    REXLOG_WARN("DEBUG_SAULO slow world update: {:.1f} ms", ms);
  }
  if (g_timing_reports >= kMaxTimingReports) {
    return;
  }
  if (const auto line = g_timing.Add(ms, NowSeconds())) {
    ++g_timing_reports;
    REXLOG_INFO("DEBUG_SAULO {}", *line);
  }
}

}  // namespace
#endif

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE));
REX_HOOK_RAW(MUA_GUEST_WORLD_UPDATE) {
  const uint32_t world = ctx.r3.u32;
#if defined(MUA_RUNTIME_MUA1)
  const auto update_start = std::chrono::steady_clock::now();
  MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE)(ctx, base);
  NoteWorldUpdateTime(std::chrono::steady_clock::now() - update_start);
#else
  MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE)(ctx, base);
#endif
  if (g_reports >= kMaxReports) {
    return;
  }
  const double delta = std::bit_cast<float>(mua::LoadBE32(base, world + mua::guest::kWorldDeltaOffset));
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard lock(g_report_mutex);
  if (const auto line = g_report.Add(delta, now)) {
    ++g_reports;
    REXLOG_INFO("DEBUG_SAULO frame pacing: frame rate={} | {}",
                mua::FrameRateLabel(mua::ConfiguredFrameRate(), mua::RuntimeFramePolicy()), *line);
  }
}

// DEBUG_SAULO: the game's max_fps option follows the launcher rate; the interval is stored at options + 24. A game
// paced by the host limiter keeps its own interval.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_OPTIONS_INIT));
REX_HOOK_RAW(MUA_GUEST_OPTIONS_INIT) {
  const uint32_t options = ctx.r3.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_OPTIONS_INIT)(ctx, base);
  const mua::FramePolicy& policy = mua::RuntimeFramePolicy();
  const int launcher = mua::ConfiguredFrameRate();
  if (policy.host_limiter) {
    REXLOG_INFO("DEBUG_SAULO game max_fps: launcher={} game interval={:.6f} s (host limiter at {})",
                mua::FrameRateLabel(launcher, policy),
                std::bit_cast<float>(mua::LoadBE32(base, options + kGameFrameIntervalOffset)),
                mua::ProjectLimiterRate(launcher, policy));
    return;
  }
  const int effective = mua::GameMaxFpsFor(launcher, policy);
  const float interval = mua::GameFrameInterval(effective);
  mua::StoreBE32(base, options + kGameFrameIntervalOffset, std::bit_cast<uint32_t>(interval));
  REXLOG_INFO("DEBUG_SAULO game max_fps: launcher={} native={} effective={} interval={:.6f} s",
              mua::FrameRateLabel(launcher, policy), policy.native_max_fps,
              effective > 0 ? std::to_string(effective) : std::string("Unlimited"), interval);
}

namespace mua {

void CountPresentForDiagnostics() {
  std::lock_guard lock(g_present_mutex);
  if (g_present_reports >= kMaxReports) {
    return;
  }
  const double now = NowSeconds();
  if (t_last_present_seconds > 0.0) {
    g_fastest_present_seconds = std::min(g_fastest_present_seconds, now - t_last_present_seconds);
  }
  t_last_present_seconds = now;
  if (const auto rate = g_present_report.Add(now)) {
    ++g_present_reports;
    REXLOG_INFO("DEBUG_SAULO frame pacing: presents/s={:.1f} | fastest present interval={:.1f} ms", *rate,
                1000.0 * g_fastest_present_seconds);
    g_fastest_present_seconds = 1e9;
  }
}

}  // namespace mua
