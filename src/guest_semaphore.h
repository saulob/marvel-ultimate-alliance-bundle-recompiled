#pragma once

// A blocking wait on a guest semaphore that is used as a lock. A kernel semaphore hands its count to a sleeping waiter
// on release, so once one thread sleeps in the wait, short critical sections taken from several threads turn into a
// chain of thread wake-ups. Trying without a timeout and spinning a few microseconds before the kernel wait keeps
// threads out of it, so a release finds no sleeper to hand the semaphore to.

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace mua::semaphore {

inline void Pause() {
#if defined(__x86_64__) || defined(_M_X64)
  _mm_pause();
#endif
}

enum class WaitPath : uint8_t { kFree, kSpinTaken, kSpinGaveUp, kSpinSkipped, kCount };

struct WaitOutcome {
  WaitPath path = WaitPath::kFree;
  uint32_t polls = 0;
  uint32_t failed_polls = 0;
};

// count: the guest's count, which runs ahead of the kernel's on release and stays up until a woken waiter lowers it,
// so a positive value is only a hint; try_take: the guest's own wait with no timeout, true when it took the
// semaphore; blocking_take: the guest's original wait.
template <typename Count, typename TryTake, typename BlockingTake, typename Clock = std::chrono::steady_clock>
WaitOutcome WaitSpinningFirst(Count&& count, TryTake&& try_take, BlockingTake&& blocking_take, bool spin,
                              std::chrono::nanoseconds budget) {
  WaitOutcome outcome;
  if (!spin || budget <= std::chrono::nanoseconds::zero()) {
    outcome.path = count() > 0 ? WaitPath::kFree : WaitPath::kSpinSkipped;
    blocking_take();
    return outcome;
  }
  constexpr int kMaxPausesAfterFailedTry = 64;
  int pauses = 1;
  const auto try_once = [&] {
    ++outcome.polls;
    if (try_take()) {
      return true;
    }
    ++outcome.failed_polls;
    // The count went up before the kernel release, or the kernel gave the semaphore to a waiter that has not woken.
    pauses = pauses < kMaxPausesAfterFailedTry / 2 ? pauses * 2 : kMaxPausesAfterFailedTry;
    return false;
  };
  if (count() > 0 && try_once()) {
    return outcome;
  }
  const auto deadline = Clock::now() + budget;
  do {
    if (count() > 0 && try_once()) {
      outcome.path = WaitPath::kSpinTaken;
      return outcome;
    }
    for (int i = 0; i < pauses; ++i) {
      Pause();
    }
  } while (Clock::now() < deadline);
  outcome.path = WaitPath::kSpinGaveUp;
  blocking_take();
  return outcome;
}

// Learns per semaphore whether spinning pays: a lock held for microseconds is taken while spinning, a semaphore
// signalled milliseconds later never is, so it stops costing a spin on every wait and is only probed now and then.
class SpinScores {
 public:
  static constexpr int kTaken = 4;
  static constexpr int kGaveUp = -1;
  static constexpr int kMax = 32;
  static constexpr int kMin = -32;
  static constexpr int kSkipBelow = -8;
  static constexpr uint32_t kProbeEvery = 32;

  bool ShouldSpin(uint32_t semaphore) {
    Slot& slot = slots_[Index(semaphore)];
    return slot.score.load(std::memory_order_relaxed) >= kSkipBelow ||
           slot.skipped.fetch_add(1, std::memory_order_relaxed) % kProbeEvery == kProbeEvery - 1;
  }

  void Record(uint32_t semaphore, WaitPath path) {
    if (path != WaitPath::kSpinTaken && path != WaitPath::kSpinGaveUp) {
      return;
    }
    std::atomic<int8_t>& score = slots_[Index(semaphore)].score;
    const int next = score.load(std::memory_order_relaxed) + (path == WaitPath::kSpinTaken ? kTaken : kGaveUp);
    score.store(int8_t(next < kMin ? kMin : next > kMax ? kMax : next), std::memory_order_relaxed);
  }

  int Score(uint32_t semaphore) const { return slots_[Index(semaphore)].score.load(std::memory_order_relaxed); }

 private:
  struct Slot {
    std::atomic<int8_t> score{0};
    std::atomic<uint32_t> skipped{0};
  };
  static constexpr int kSlotBits = 10;
  static constexpr size_t kSlots = size_t{1} << kSlotBits;

  static size_t Index(uint32_t semaphore) { return size_t(uint32_t(semaphore * 2654435761u) >> (32 - kSlotBits)); }

  std::array<Slot, kSlots> slots_{};
};

}  // namespace mua::semaphore
