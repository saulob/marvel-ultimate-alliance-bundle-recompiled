#pragma once

#include <chrono>
#include <cstdint>

// The guest-frame totals the GPU plugin's Performance line and the audio history read (ReXGlue patch 0132).
#include <rex/perf/counter.h>

namespace mua {

enum class GuestFrameTime { kWorldUpdate, kLimiterWait, kFenceWait };

inline void AddGuestFrameTime(GuestFrameTime kind, std::chrono::steady_clock::duration elapsed) {
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
}

}  // namespace mua
