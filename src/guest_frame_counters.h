#pragma once

#include <chrono>

// The guest-frame totals the GPU plugin's Performance line reads (ReXGlue patch 0132). MUA1 only for now.
#if defined(MUA_RUNTIME_MUA1) && defined(_WIN32)
#define MUA_GUEST_FRAME_COUNTERS 1
#include <rex/perf/counter.h>
#else
#define MUA_GUEST_FRAME_COUNTERS 0
#endif

namespace mua {

enum class GuestFrameTime { kWorldUpdate, kLimiterWait, kFenceWait };

inline void AddGuestFrameTime([[maybe_unused]] GuestFrameTime kind,
                              [[maybe_unused]] std::chrono::steady_clock::duration elapsed) {
#if MUA_GUEST_FRAME_COUNTERS
  using rex::perf::CounterId;
  const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  switch (kind) {
    case GuestFrameTime::kWorldUpdate:
      rex::perf::IncrementCounter(CounterId::kGuestUpdateNs, ns);
      rex::perf::IncrementCounter(CounterId::kGuestUpdates, 1);
      break;
    case GuestFrameTime::kLimiterWait:
      rex::perf::IncrementCounter(CounterId::kGuestLimiterWaitNs, ns);
      break;
    case GuestFrameTime::kFenceWait:
      rex::perf::IncrementCounter(CounterId::kGuestFenceWaitNs, ns);
      break;
  }
#endif
}

}  // namespace mua
