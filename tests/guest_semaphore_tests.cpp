// The spin before the blocking wait of Marvel Ultimate Alliance 2's Alchemy semaphore (src/guest_semaphore.h): the
// paths a wait takes, the adaptive spin policy, the duration histogram, the waits against a semaphore model kept like
// the guest keeps it (kernel count plus its own count) under real threads, and the hooked guest code.
// Run: mua_guest_semaphore_tests

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <semaphore>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "guest_semaphore.h"

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

using namespace mua::semaphore;
using namespace std::chrono_literals;

// Advances 1 us per reading, so a spin's length is its number of checks.
struct FakeClock {
  using duration = std::chrono::nanoseconds;
  using time_point = std::chrono::time_point<FakeClock, duration>;
  static inline int64_t ticks = 0;
  static time_point now() { return time_point(duration(1000 * ticks++)); }
};

struct Calls {
  int counts = 0;
  int tries = 0;
  int blocks = 0;
};

void TestPaths() {
  std::printf("a wait takes the free, spin and blocking paths\n");
  const auto run = [](int free_after, int fail_tries, bool spin, std::chrono::nanoseconds budget, Calls& calls) {
    int failures_left = fail_tries;
    return WaitSpinningFirst<std::function<int32_t()>, std::function<bool()>, std::function<void()>, FakeClock>(
        [&] { return ++calls.counts > free_after ? 1 : 0; },
        [&] {
          ++calls.tries;
          return failures_left-- <= 0;
        },
        [&] { ++calls.blocks; }, spin, budget);
  };

  Calls free_now;
  WaitOutcome outcome = run(0, 0, true, 30us, free_now);
  CHECK(outcome.path == WaitPath::kFree && free_now.blocks == 0 && free_now.tries == 1 && outcome.polls == 1 &&
        outcome.failed_polls == 0);

  Calls not_spun;
  outcome = run(1000, 0, false, 30us, not_spun);
  CHECK(outcome.path == WaitPath::kSpinSkipped && not_spun.blocks == 1 && not_spun.tries == 0);
  Calls not_spun_free;
  outcome = run(0, 0, false, 30us, not_spun_free);
  CHECK(outcome.path == WaitPath::kFree && not_spun_free.blocks == 1 && not_spun_free.tries == 0);
  Calls no_budget;
  outcome = run(1000, 0, true, 0us, no_budget);
  CHECK(outcome.path == WaitPath::kSpinSkipped && no_budget.blocks == 1 && no_budget.tries == 0);

  Calls taken;
  outcome = run(5, 0, true, 30us, taken);
  CHECK(outcome.path == WaitPath::kSpinTaken && taken.blocks == 0 && taken.tries == 1 && outcome.polls == 1 &&
        outcome.failed_polls == 0);

  // The count says free while the kernel has handed the semaphore to a sleeping waiter: the first try fails, a later
  // one takes it.
  Calls handed_off;
  outcome = run(0, 1, true, 30us, handed_off);
  CHECK(outcome.path == WaitPath::kSpinTaken && handed_off.blocks == 0 && handed_off.tries == 2 &&
        outcome.polls == 2 && outcome.failed_polls == 1);

  Calls raced;
  outcome = run(5, 2, true, 30us, raced);
  CHECK(outcome.path == WaitPath::kSpinTaken && raced.blocks == 0 && raced.tries == 3 && outcome.polls == 3 &&
        outcome.failed_polls == 2);

  Calls held;
  FakeClock::ticks = 0;
  outcome = run(1000000, 0, true, 30us, held);
  CHECK(outcome.path == WaitPath::kSpinGaveUp && held.blocks == 1 && held.tries == 0 && outcome.polls == 0);
  CHECK(FakeClock::ticks <= 32);

  Calls always_fails;
  outcome = run(0, 1000000, true, 30us, always_fails);
  CHECK(outcome.path == WaitPath::kSpinGaveUp && always_fails.blocks == 1 && outcome.polls > 1 &&
        outcome.failed_polls == outcome.polls);
}

void TestSpinScores() {
  std::printf("spinning stops paying on a semaphore that is never taken while spinning\n");
  SpinScores scores;
  constexpr uint32_t kLock = 0x40001000;
  constexpr uint32_t kSignal = 0x40002000;
  CHECK(scores.ShouldSpin(kLock) && scores.ShouldSpin(kSignal));
  for (int i = 0; i < 100; ++i) {
    scores.Record(kSignal, WaitPath::kSpinGaveUp);
    scores.Record(kLock, WaitPath::kSpinTaken);
  }
  CHECK(scores.Score(kSignal) == SpinScores::kMin && scores.Score(kLock) == SpinScores::kMax);
  int spun = 0;
  for (uint32_t i = 0; i < 10 * SpinScores::kProbeEvery; ++i) {
    spun += scores.ShouldSpin(kSignal);
  }
  CHECK(spun == 10);
  for (int i = 0; i < 100; ++i) {
    CHECK(scores.ShouldSpin(kLock));
  }
  scores.Record(kSignal, WaitPath::kFree);
  scores.Record(kSignal, WaitPath::kSpinSkipped);
  CHECK(scores.Score(kSignal) == SpinScores::kMin);
  for (int i = 0; i < 6; ++i) {
    scores.Record(kSignal, WaitPath::kSpinTaken);
  }
  CHECK(scores.Score(kSignal) == SpinScores::kMin + 6 * SpinScores::kTaken && scores.ShouldSpin(kSignal));
}

void TestHistogram() {
  std::printf("the duration histogram keeps quarter-octave percentiles\n");
  for (uint64_t ns : {0ull, 1ull, 2ull, 3ull, 4ull, 5ull, 7ull, 1000ull, 1023ull, 1024ull, 999999ull, 1ull << 40}) {
    const size_t bucket = DurationHistogram::Bucket(ns);
    CHECK(bucket < DurationHistogram::kBuckets);
    CHECK(ns <= DurationHistogram::UpperBound(bucket) || bucket == DurationHistogram::kBuckets - 1);
    if (bucket > 0 && ns > 3) {
      CHECK(ns > DurationHistogram::UpperBound(bucket - 1));
    }
  }
  for (size_t i = 1; i < DurationHistogram::kBuckets; ++i) {
    CHECK(DurationHistogram::UpperBound(i) >= DurationHistogram::UpperBound(i - 1));
  }
  DurationHistogram histogram;
  CHECK(histogram.Percentile(0.5) == 0);
  for (int i = 0; i < 89; ++i) {
    histogram.Add(1000);
  }
  for (int i = 0; i < 10; ++i) {
    histogram.Add(100000);
  }
  histogram.Add(5000000);
  CHECK(histogram.count() == 100);
  const uint64_t p50 = histogram.Percentile(0.5);
  const uint64_t p90 = histogram.Percentile(0.9);
  const uint64_t p99 = histogram.Percentile(0.99);
  CHECK(p50 >= 1000 && p50 < 1250);
  CHECK(p90 >= 100000 && p90 < 125000);
  CHECK(p99 >= 100000 && p99 < 125000);
  CHECK(histogram.Percentile(1.0) >= 5000000 && histogram.Percentile(1.0) < 6250000);
  DurationHistogram other;
  other.Add(1000);
  histogram.Merge(other);
  CHECK(histogram.count() == 101);
}

// The guest semaphore as the game keeps it: a kernel count, and its own count that is lowered after a taken wait and
// raised before the release.
class GuestSemaphoreModel {
 public:
  GuestSemaphoreModel(int32_t initial) : kernel_(initial), count_(initial) {}

  WaitOutcome Wait(bool spin, std::chrono::nanoseconds budget) {
    return WaitSpinningFirst(
        [this] { return count_.load(); },
        [this] {
          if (!kernel_.try_acquire()) {
            return false;
          }
          count_.fetch_sub(1);
          return true;
        },
        [this] {
          kernel_.acquire();
          count_.fetch_sub(1);
        },
        spin, budget);
  }

  void Release() {
    count_.fetch_add(1);
    kernel_.release();
  }

  int32_t count() const { return count_.load(); }
  bool TryKernel() { return kernel_.try_acquire(); }

 private:
  std::counting_semaphore<1 << 20> kernel_;
  std::atomic<int32_t> count_;
};

void TestSemaphoreSemantics() {
  std::printf("waits keep the semaphore's counts and exclusion\n");
  {
    GuestSemaphoreModel semaphore(0);
    semaphore.Release();
    CHECK(semaphore.Wait(true, 30us).path == WaitPath::kFree);
    CHECK(semaphore.count() == 0 && !semaphore.TryKernel());
  }
  {
    GuestSemaphoreModel semaphore(3);
    for (int i = 0; i < 3; ++i) {
      CHECK(semaphore.Wait(true, 30us).path == WaitPath::kFree);
    }
    CHECK(semaphore.count() == 0);
    std::atomic<bool> done{false};
    std::thread waiter([&] {
      semaphore.Wait(true, 30us);
      done = true;
    });
    std::this_thread::sleep_for(20ms);
    CHECK(!done);
    semaphore.Release();
    waiter.join();
    CHECK(done && semaphore.count() == 0 && !semaphore.TryKernel());
  }
  {
    // Released during the spin: taken without blocking.
    GuestSemaphoreModel semaphore(0);
    WaitOutcome outcome;
    std::thread waiter([&] { outcome = semaphore.Wait(true, 200ms); });
    std::this_thread::sleep_for(2ms);
    semaphore.Release();
    waiter.join();
    CHECK(outcome.path == WaitPath::kSpinTaken);
    CHECK(semaphore.count() == 0 && !semaphore.TryKernel());
  }
  {
    // Released after the spin: the original blocking wait takes it, after about the spin's budget of spinning.
    GuestSemaphoreModel semaphore(0);
    WaitOutcome outcome;
    std::chrono::steady_clock::duration waited{};
    std::thread waiter([&] {
      const auto start = std::chrono::steady_clock::now();
      outcome = semaphore.Wait(true, 50us);
      waited = std::chrono::steady_clock::now() - start;
    });
    std::this_thread::sleep_for(30ms);
    semaphore.Release();
    waiter.join();
    CHECK(outcome.path == WaitPath::kSpinGaveUp);
    CHECK(waited >= 20ms);
    CHECK(semaphore.count() == 0 && !semaphore.TryKernel());
  }
}

// Threads take a binary semaphore around a counter they increment without atomics, spinning or not.
void TestMutualExclusion() {
  std::printf("a binary semaphore stays a lock under contention\n");
  for (const bool spin : {false, true}) {
    GuestSemaphoreModel semaphore(1);
    uint64_t counter = 0;
    std::atomic<uint64_t> taken_spinning{0};
    constexpr int kThreads = 4;
    constexpr int kIterations = 20000;
    std::vector<std::thread> threads;
    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&] {
        for (int i = 0; i < kIterations; ++i) {
          if (semaphore.Wait(spin, 30us).path == WaitPath::kSpinTaken) {
            taken_spinning.fetch_add(1, std::memory_order_relaxed);
          }
          const uint64_t value = counter;
          for (int k = 0; k < 20; ++k) {
            Pause();
          }
          counter = value + 1;
          semaphore.Release();
        }
      });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    CHECK(counter == uint64_t(kThreads) * kIterations);
    CHECK(semaphore.count() == 1 && semaphore.TryKernel() && !semaphore.TryKernel());
    std::printf("  %s: %d lock/unlock in %.1f ms, %llu taken spinning\n", spin ? "spin 30 us" : "no spin  ",
                kThreads * kIterations, ms, static_cast<unsigned long long>(taken_spinning.load()));
  }
}

#ifdef MUA_GENERATED_ROOT
std::string GeneratedFunction(const std::string& name) {
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MUA_GENERATED_ROOT) / "mua2")) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    std::string source = text.str();
    std::erase(source, '\r');
    const size_t start = source.find("DEFINE_REX_FUNC(" + name + ") {");
    if (start != std::string::npos) {
      return source.substr(start, source.find("\n}\n", start) - start);
    }
  }
  return {};
}

// The instruction comments of a generated function, in order.
std::vector<std::string> Instructions(const std::string& body) {
  std::vector<std::string> instructions;
  std::istringstream lines(body);
  for (std::string line; std::getline(lines, line);) {
    const size_t comment = line.find("// ");
    if (comment != std::string::npos && line.find_first_not_of('\t') == comment) {
      std::string instruction = line.substr(comment + 3);
      instruction.erase(instruction.find_last_not_of(' ') + 1);
      instructions.push_back(instruction);
    }
  }
  return instructions;
}

bool InOrder(const std::vector<std::string>& instructions, const std::vector<std::string>& expected) {
  size_t at = 0;
  for (const std::string& instruction : instructions) {
    if (at < expected.size() && instruction == expected[at]) {
      ++at;
    }
  }
  return at == expected.size();
}

void TestGeneratedCode() {
  std::printf("the hooked guest functions are the semaphore wait and release\n");
  const auto wait = Instructions(GeneratedFunction("sub_822E7400"));
  CHECK(wait.size() == 37);
  // Failure result first, timeout INFINITE when r5's low byte is set and 0 otherwise, then the kernel wait; once it
  // is taken, the count at +24 goes down and the success result is stored.
  CHECK(InOrder(wait, {"lis r11,-32073", "lwz r3,20(r4)", "clrlwi r10,r5,24", "li r9,-1", "subfic r8,r10,0",
                       "lwz r11,-23580(r11)", "subfe r6,r7,r7", "and r4,r6,r9", "stw r11,0(r31)", "bl 0x829ae140",
                       "cmplwi cr6,r3,0", "bne cr6,0x822e7478", "addi r11,r30,24", "lwarx r10,0,r11",
                       "addi r10,r10,-1", "stwcx. r10,0,r11", "lis r8,-31946", "lwz r11,7152(r8)", "stw r11,0(r31)",
                       "mr r3,r31", "blr"}));
  CHECK(InOrder(Instructions(GeneratedFunction("sub_829AE140")), {"li r5,0", "b 0x829b46a0"}));
  CHECK(InOrder(Instructions(GeneratedFunction("sub_829B46A0")),
                {"bl 0x829baea0", "li r4,1", "bl 0x82ad24ec", "cmpwi cr6,r3,257", "beq cr6,0x829b46c4"}));
  CHECK(InOrder(Instructions(GeneratedFunction("sub_829BAEA0")), {"cmpwi cr6,r4,-1", "li r3,0", "mulli r11,r11,-10000"}));

  const auto signal = Instructions(GeneratedFunction("sub_822E7358"));
  CHECK(InOrder(signal, {"addi r30,r4,24", "lwarx r9,0,r30", "addi r9,r9,1", "stwcx. r9,0,r30", "li r5,0",
                         "lwz r3,20(r11)", "li r4,1", "bl 0x829b2348"}));
  CHECK(InOrder(Instructions(GeneratedFunction("sub_829B2348")), {"bl 0x82ad27fc"}));
  // Creation sets the count at +24 from the initial count at +12 and keeps the kernel handle at +20.
  CHECK(InOrder(Instructions(GeneratedFunction("sub_822E7090")),
                {"lwz r11,12(r4)", "addi r10,r4,24", "stwcx. r11,0,r10", "lwz r5,16(r31)", "lwz r4,12(r31)",
                 "stw r3,20(r31)"}));
  // The Havok allocator takes its semaphore (vtable slot 116 with r5 = 1) around the pool allocation.
  CHECK(InOrder(Instructions(GeneratedFunction("sub_821F8D30")),
                {"lwz r31,2596(r3)", "li r5,1", "lwz r10,116(r11)", "bctrl", "bl 0x822bb600", "lwz r10,108(r11)",
                 "bctrl"}));
}
#endif

void TestWiring() {
  std::printf("the hooks are MUA 2's only and keep a fallback\n");
  const std::filesystem::path src(MUA_SRC_DIR);
  std::ifstream in(src / "guest_semaphore.cpp", std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  const std::string source = text.str();
  CHECK(source.find("#if defined(MUA_RUNTIME_MUA2)") != std::string::npos);
  CHECK(source.find("REX_HOOK_RAW(MUA_GUEST_SEMAPHORE_WAIT)") != std::string::npos);
  CHECK(source.find("REXCVAR_DEFINE_INT32(mua_semaphore_spin_us, 50,") != std::string::npos);
  CHECK(source.find(".range(0, 200)") != std::string::npos);
  std::ifstream map_in(src / "guest_map_mua2.h", std::ios::binary);
  std::ostringstream map;
  map << map_in.rdbuf();
  CHECK(map.str().find("#define MUA_GUEST_SEMAPHORE_WAIT sub_822E7400") != std::string::npos);
  CHECK(map.str().find("#define MUA_GUEST_SEMAPHORE_SIGNAL sub_822E7358") != std::string::npos);
  for (const char* other : {"guest_map_mua1.h", "guest_map_mua1_gold.h"}) {
    std::ifstream other_in(src / other, std::ios::binary);
    std::ostringstream other_map;
    other_map << other_in.rdbuf();
    CHECK(other_map.str().find("MUA_GUEST_SEMAPHORE") == std::string::npos);
  }
}

}  // namespace

int main() {
  TestPaths();
  TestSpinScores();
  TestHistogram();
  TestSemaphoreSemantics();
  TestMutualExclusion();
#ifdef MUA_GENERATED_ROOT
  TestGeneratedCode();
#endif
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
