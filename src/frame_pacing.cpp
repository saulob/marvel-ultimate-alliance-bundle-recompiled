// DEBUG_SAULO: samples the delta the game's World::Update used, bounded to one log line per 10 seconds and at most
// 12 lines, to check the frame model (variable delta from the wall clock) in a real run at each frame rate.

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
#include "save_diagnostics.h"

namespace {

// World singleton fields: the delta in seconds of the last update.
constexpr uint32_t kWorldDeltaOffset = 1000;
constexpr int kMaxReports = 12;
constexpr uint32_t kGameFrameIntervalOffset = 24;

std::mutex g_report_mutex;
mua::PacingReport g_report;
int g_reports = 0;

std::mutex g_present_mutex;
mua::CallRateReport g_present_report;
int g_present_reports = 0;

double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

REX_EXTERN(__imp__sub_821BAE40);
REX_HOOK_RAW(sub_821BAE40) {
  const uint32_t world = ctx.r3.u32;
  __imp__sub_821BAE40(ctx, base);
  if (g_reports >= kMaxReports) {
    return;
  }
  const double delta = std::bit_cast<float>(mua::LoadBE32(base, world + kWorldDeltaOffset));
  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  std::lock_guard lock(g_report_mutex);
  if (const auto line = g_report.Add(delta, now)) {
    ++g_reports;
    REXLOG_INFO("DEBUG_SAULO frame pacing: frame rate={} | {}", mua::FrameRateLabel(mua::ConfiguredFrameRate()),
                *line);
  }
}

// DEBUG_SAULO: the game's max_fps option follows the launcher rate; the interval is stored at options + 24.
REX_EXTERN(__imp__sub_821212A8);
REX_HOOK_RAW(sub_821212A8) {
  const uint32_t options = ctx.r3.u32;
  __imp__sub_821212A8(ctx, base);
  const int launcher = mua::ConfiguredFrameRate();
  const int effective = mua::GameMaxFpsFor(launcher);
  const float interval = mua::GameFrameInterval(effective);
  mua::StoreBE32(base, options + kGameFrameIntervalOffset, std::bit_cast<uint32_t>(interval));
  REXLOG_INFO("DEBUG_SAULO game max_fps: launcher={} native={} effective={} interval={:.6f} s",
              mua::FrameRateLabel(launcher), mua::kGameNativeMaxFps,
              effective > 0 ? std::to_string(effective) : std::string("Unlimited"), interval);
}

namespace mua {

void CountPresentForDiagnostics() {
  std::lock_guard lock(g_present_mutex);
  if (g_present_reports >= kMaxReports) {
    return;
  }
  if (const auto rate = g_present_report.Add(NowSeconds())) {
    ++g_present_reports;
    REXLOG_INFO("DEBUG_SAULO frame pacing: presents/s={:.1f}", *rate);
  }
}

}  // namespace mua
