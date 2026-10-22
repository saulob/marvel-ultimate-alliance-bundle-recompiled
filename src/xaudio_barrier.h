#pragma once

#include <atomic>
#include <cstdint>

namespace mua {

// The rendezvous of the XAudio mix workers, kept on the host: the arrivals of the current round in the low byte (bit =
// processor) and the round number above them. Arrival and release are one compare-exchange, so a worker preempted
// after arriving can never be left behind in a round the others already released.
class XAudioBarrier {
 public:
  struct Arrival {
    bool released_round;
    uint64_t round;
  };

  // `expected` holds a bit per participating processor (0-7).
  Arrival Arrive(uint32_t processor, uint64_t expected) {
    uint64_t state = state_.load(std::memory_order_acquire);
    uint64_t next;
    do {
      const uint64_t arrived = (state & kArrivalMask) | (uint64_t(1) << processor);
      next = (arrived & expected) == expected ? (state | kArrivalMask) + 1 : (state & ~kArrivalMask) | arrived;
    } while (!state_.compare_exchange_weak(state, next, std::memory_order_acq_rel, std::memory_order_acquire));
    return {(next >> 8) != (state >> 8), state >> 8};
  }

  bool RoundOpen(uint64_t round) const { return (state_.load(std::memory_order_acquire) >> 8) == round; }

 private:
  static constexpr uint64_t kArrivalMask = 0xFF;
  std::atomic<uint64_t> state_{0};
};

}  // namespace mua
