#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "xaudio_barrier.h"

using mua::XAudioBarrier;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr uint64_t kProcessors45 = (uint64_t(1) << 4) | (uint64_t(1) << 5);

void TestLastArrivalReleases() {
  XAudioBarrier barrier;
  const XAudioBarrier::Arrival first = barrier.Arrive(4, kProcessors45);
  CHECK(!first.released_round && first.round == 0);
  CHECK(barrier.RoundOpen(0));
  const XAudioBarrier::Arrival second = barrier.Arrive(5, kProcessors45);
  CHECK(second.released_round && second.round == 0);
  CHECK(!barrier.RoundOpen(0));
  CHECK(barrier.RoundOpen(1));
}

// A worker that arrives again before its partner releases the round stays in that round.
void TestRepeatedArrivalKeepsRound() {
  XAudioBarrier barrier;
  barrier.Arrive(4, kProcessors45);
  const XAudioBarrier::Arrival again = barrier.Arrive(4, kProcessors45);
  CHECK(!again.released_round && again.round == 0);
  CHECK(barrier.Arrive(5, kProcessors45).released_round);
}

// The case the guest's byte mask loses: the late worker arrives after the round was released and must not be left
// waiting in it.
void TestLateWorkerNeverWaitsOnReleasedRound() {
  XAudioBarrier barrier;
  const XAudioBarrier::Arrival early = barrier.Arrive(4, kProcessors45);
  barrier.Arrive(5, kProcessors45);
  CHECK(!barrier.RoundOpen(early.round));
  const XAudioBarrier::Arrival next = barrier.Arrive(4, kProcessors45);
  CHECK(!next.released_round && next.round == 1);
}

void TestSingleParticipantReleasesAtOnce() {
  XAudioBarrier barrier;
  CHECK(barrier.Arrive(2, uint64_t(1) << 2).released_round);
  CHECK(barrier.Arrive(2, uint64_t(1) << 2).released_round);
  CHECK(barrier.RoundOpen(2));
}

void TestTwoThreadsManyRounds() {
  constexpr int kRounds = 20000;
  XAudioBarrier barrier;
  std::atomic<int> completed[2] = {0, 0};
  auto worker = [&](uint32_t processor, int index) {
    for (int round = 0; round < kRounds; ++round) {
      const XAudioBarrier::Arrival arrival = barrier.Arrive(processor, kProcessors45);
      while (!arrival.released_round && barrier.RoundOpen(arrival.round)) {
        std::this_thread::yield();
      }
      completed[index].fetch_add(1, std::memory_order_relaxed);
    }
  };
  std::thread a(worker, 4, 0);
  std::thread b(worker, 5, 1);
  a.join();
  b.join();
  CHECK(completed[0] == kRounds && completed[1] == kRounds);
  CHECK(barrier.RoundOpen(kRounds));
}

}  // namespace

int main() {
  TestLastArrivalReleases();
  TestRepeatedArrivalKeepsRound();
  TestLateWorkerNeverWaitsOnReleasedRound();
  TestSingleParticipantReleasesAtOnce();
  TestTwoThreadsManyRounds();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
