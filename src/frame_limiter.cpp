#include "frame_limiter.h"

#include <cstdint>
#include <mutex>
#include <optional>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "guest_map.h"
#include "settings.h"

namespace mua {
namespace {

FrameLimiter g_limiter;
std::mutex g_limiter_mutex;

}  // namespace

int ConfiguredFrameRate() {
  return FrameRateOrDefault(REXCVAR_GET(mua_frame_rate));
}

// A change of rate re-arms the limiter, so time spent at another rate is never waited for or caught up.
void PaceGuestFrame() {
  std::optional<LimiterClock::time_point> deadline;
  {
    std::lock_guard lock(g_limiter_mutex);
    deadline = g_limiter.Next(ProjectLimiterRate(ConfiguredFrameRate()), LimiterClock::now());
  }
  if (deadline) {
    WaitUntil(*deadline);
  }
}

uint32_t PresentArgument(uint32_t argument) {
  return PresentModeFor(ConfiguredFrameRate()) == PresentMode::kImmediate ? guest::ImmediatePresentArgument(argument)
                                                                          : argument;
}

}  // namespace mua

// The D3D present callback: dropping its interval flips at once instead of at the next guest vblank, so rates above 60
// do not need a faster vblank.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_PRESENT_CALLBACK));
REX_HOOK_RAW(MUA_GUEST_PRESENT_CALLBACK) {
  ctx.r3.u32 = mua::PresentArgument(ctx.r3.u32);
  MUA_GUEST_ORIGINAL(MUA_GUEST_PRESENT_CALLBACK)(ctx, base);
}
