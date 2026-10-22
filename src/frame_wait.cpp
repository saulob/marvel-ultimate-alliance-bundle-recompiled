#include "frame_limiter.h"

#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mua {
namespace {

using namespace std::chrono_literals;

constexpr auto kSpinWindow = 500us;

}  // namespace

#if defined(_WIN32)

void WaitUntil(LimiterClock::time_point deadline) {
  thread_local const HANDLE timer =
      CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto coarse = deadline - LimiterClock::now() - kSpinWindow;
  if (coarse > LimiterClock::duration::zero()) {
    if (timer) {
      LARGE_INTEGER due;
      due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(coarse).count() / 100;
      if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer, INFINITE);
      }
    } else {
      std::this_thread::sleep_for(coarse);
    }
  }
  while (LimiterClock::now() < deadline) {
    std::this_thread::yield();
  }
}

#else

void WaitUntil(LimiterClock::time_point deadline) {
  std::this_thread::sleep_until(deadline - kSpinWindow);
  while (LimiterClock::now() < deadline) {
    std::this_thread::yield();
  }
}

#endif

}  // namespace mua
