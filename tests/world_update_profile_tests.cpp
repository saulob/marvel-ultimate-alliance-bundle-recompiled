// DEBUG_SAULO: the world update profiler's host-to-guest lookup, sample counts and report lines, and (Windows) the
// stack sampler against a running thread whose call chain is known.
// Run: mua_world_update_profile_tests

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "world_update_profile.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <malloc.h>
#include <windows.h>
#endif

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

using namespace mua::profile;

bool Has(const std::vector<std::string>& lines, const std::string& text) {
  return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line == text; });
}

void TestIndex() {
  std::printf("host code maps back to guest functions\n");
  const GuestFunctionIndex index({{0x1200, 0x1300, 0x82000020},
                                  {0x1000, 0x1100, 0x82000008},
                                  {0x1100, 0, 0x82000010},
                                  {0x1000, 0x1100, 0x82000000}});
  CHECK(index.size() == 3);
  CHECK(index.Find(0x1050, 0x1000) == 0x82000000);
  CHECK(index.Find(0x1250, 0x1200) == 0x82000020);
  CHECK(index.Find(0x1050, 0x1040) == 0);
  CHECK(index.Find(0x1100, 0) == 0x82000010);
  CHECK(index.Find(0x11F0, 0) == 0x82000010);
  CHECK(index.Find(0x1350, 0) == 0);
  CHECK(index.Find(0x0F00, 0) == 0);
  CHECK(index.Below(0x1350) == 0x82000020);
  CHECK(index.Below(0x1000) == 0x82000000);
  CHECK(index.Below(0x0500) == 0);
  CHECK(index.Folded(0x82000000) == 1);
  CHECK(index.Folded(0x82000020) == 0);
  const Naming naming{nullptr, &index};
  CHECK(naming.Guest(0x82000000) == "sub_82000000(+1 identical)");
  CHECK(naming.Guest(0x82000020) == "sub_82000020");
  CHECK(Naming{}.Guest(0x82000000) == "sub_82000000");
}

constexpr uint32_t kA = 0x82000000;
constexpr uint32_t kB = 0x82000010;
constexpr uint32_t kC = 0x82000020;
constexpr uint64_t kRuntimeFunction = 0x7FF000001000;

Profile ThreeSamples() {
  Profile profile;
  const Frame a{kA, 0x1000, CodeKind::kGuest};
  const Frame b{kB, 0x1100, CodeKind::kGuest};
  const Frame c{kC, 0x1200, CodeKind::kGuest};
  const Frame runtime{0, kRuntimeFunction, CodeKind::kRuntime};
  const Frame in_runtime[] = {runtime, c, b, a};
  const Frame in_c[] = {c, b, a};
  const Frame recursive_b[] = {b, b, a};
  profile.Add(in_runtime);
  profile.Add(in_c);
  profile.Add(recursive_b);
  return profile;
}

void TestProfile() {
  std::printf("samples count self, inclusive and callers\n");
  const Profile profile = ThreeSamples();
  CHECK(profile.samples() == 3);
  CHECK(profile.innermost(CodeKind::kGuest) == 2);
  CHECK(profile.innermost(CodeKind::kRuntime) == 1);
  CHECK(profile.guest().at(kC).self == 2);
  CHECK(profile.guest().at(kC).inclusive == 2);
  CHECK(profile.guest().at(kB).self == 1);
  CHECK(profile.guest().at(kB).inclusive == 3);
  CHECK(profile.guest().at(kA).self == 0);
  CHECK(profile.guest().at(kA).inclusive == 3);
  CHECK(profile.TopCaller(kC) == kB);
  CHECK(profile.TopCaller(kB) == kA);
  CHECK(profile.TopCaller(kA) == 0);
  CHECK(profile.host().at(kRuntimeFunction) == 1);
  CHECK(profile.TopHostCaller(kRuntimeFunction) == kC);

  Profile empty = profile;
  empty.Clear();
  CHECK(empty.samples() == 0 && empty.guest().empty() && empty.host().empty());
  const Frame* none = nullptr;
  empty.Add({none, size_t{0}});
  CHECK(empty.samples() == 1 && empty.innermost(CodeKind::kOther) == 1);
}

void TestReport() {
  std::printf("report lines scale samples to milliseconds per update\n");
  const Profile profile = ThreeSamples();
  const ReportScale scale{12.0, 60.0, 3};
  const Naming name{[](uint64_t host) {
    return host == kRuntimeFunction ? std::string("rexruntime.dll+0x1000") : std::string("?");
  }};
  const auto lines = ReportWorldUpdate(profile, scale, name, 15);
  CHECK(Has(lines, "world update profile: 12.00 ms per update, 60.0 updates/s, 3 samples | innermost: guest code 66.7%, "
                   "program host code 0.0%, ReXGlue runtime 33.3%, system 0.0%, other 0.0%"));
  CHECK(Has(lines, "world update path (>= 90% inclusive): sub_82000000 100.0% > sub_82000010 100.0%"));
  CHECK(Has(lines, "world update self #1: sub_82000020 8.000 ms (66.7%), inclusive 8.000 ms (66.7%) from sub_82000010"));
  CHECK(Has(lines, "world update self #2: sub_82000010 4.000 ms (33.3%), inclusive 12.000 ms (100.0%) from sub_82000000"));
  CHECK(Has(lines, "world update inclusive #1: sub_82000020 8.000 ms (66.7%), self 8.000 ms from sub_82000010"));
  CHECK(Has(lines, "world update innermost host code: rexruntime.dll+0x1000 4.00 ms (33.3%) from sub_82000020"));
  CHECK(lines.size() == 6);

  const auto top_one = ReportWorldUpdate(profile, scale, name, 1);
  CHECK(top_one.size() == 5);

  const auto outside = ReportOutsideWorldUpdate(profile, ReportScale{12.0, 60.0, 6}, name, 2);
  CHECK(Has(outside, "same thread outside the world update: 6.00 ms per update, 3 samples | innermost: guest code 66.7%, "
                     "program host code 0.0%, ReXGlue runtime 33.3%, system 0.0%, other 0.0%"));
  CHECK(Has(outside, "outside world update self: sub_82000020 4.000 ms; sub_82000010 2.000 ms"));
  CHECK(Has(outside, "outside world update inclusive: sub_82000000 6.000 ms; sub_82000010 6.000 ms from sub_82000000"));
  CHECK(Has(outside, "outside world update innermost host code: rexruntime.dll+0x1000 2.00 ms (33.3%) from sub_82000020"));

  const ThreadShare threads[] = {{40, 10, 2}, {41, 20, 1}};
  CHECK(ReportOtherThreads(profile, threads, name, 4) ==
        "other guest threads: 2 threads, 30 samples, busy in 10.0% (t40 20% of 10, t41 5% of 20) | busy self: "
        "sub_82000020 66.7%; sub_82000010 33.3% | busy inclusive: sub_82000000 100.0%; sub_82000010 100.0%; "
        "sub_82000020 66.7%");
  CHECK(ReportOtherThreads(Profile(), {}, name, 4) ==
        "other guest threads: 0 threads, 0 samples, busy in 0.0% | busy self: none | busy inclusive: none");
  CHECK(TopShares(profile, &Counts::self, name, 1) == "sub_82000020 66.7%");

  CHECK(ReportWorldUpdate(Profile(), scale, name, 15).empty());
  CHECK(ReportWorldUpdate(profile, ReportScale{12.0, 60.0, 0}, name, 15).empty());
}

void TestGuestThreadName() {
  std::printf("guest threads are recognized by their description\n");
  CHECK(IsGuestThreadName(L"Main XThread (F8000028)"));
  CHECK(IsGuestThreadName(L"XThread1A2C (F80000A4)"));
  CHECK(!IsGuestThreadName(L"Main XThread (F800002)"));
  CHECK(!IsGuestThreadName(L"Main XThread (f8000028)"));
  CHECK(!IsGuestThreadName(L"Audio mixer"));
  CHECK(!IsGuestThreadName(L""));
}

#if defined(_WIN32)
std::atomic<bool> g_stop{false};
std::atomic<uint64_t> g_work{0};
volatile uint32_t g_sink = 0;

// The leaf at the top of the sampled chain: no stack frame, no unwind entry.
__attribute__((noinline)) uint32_t Inner(uint32_t seed) {
  uint32_t value = seed;
  for (int i = 0; i < 2000; ++i) {
    value = value * 1664525u + 1013904223u;
  }
  return value;
}

__attribute__((noinline)) void Middle(uint32_t seed) {
  volatile uint8_t frame[4096];
  frame[seed & 4095] = uint8_t(seed);
  g_sink = g_sink + Inner(seed + frame[seed & 4095]);
}

std::atomic<uint64_t> g_stalled_ns{0};

// A dynamic allocation gives this frame a frame pointer, which the sampler has to move into its copy. A gap between
// two calls of a few microseconds' work is time the thread did not run.
__attribute__((noinline)) void Outer(size_t bytes) {
  auto* scratch = static_cast<volatile uint8_t*>(_alloca(bytes));
  scratch[0] = 1;
  constexpr auto kStall = std::chrono::microseconds(20);
  auto last = std::chrono::steady_clock::now();
  while (!g_stop.load(std::memory_order_relaxed)) {
    Middle(uint32_t(g_work.fetch_add(1, std::memory_order_relaxed)) + scratch[0]);
    const auto now = std::chrono::steady_clock::now();
    if (now - last > kStall) {
      g_stalled_ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now - last).count()),
                             std::memory_order_relaxed);
    }
    last = now;
  }
}

uint64_t StartOf(const void* function) {
  auto address = reinterpret_cast<uint64_t>(function);
  const auto* code = reinterpret_cast<const uint8_t*>(address);
  if (code[0] == 0xE9) {  // an incremental-link thunk
    int32_t offset;
    std::memcpy(&offset, code + 1, sizeof(offset));
    address += 5 + int64_t(offset);
  }
  return address;
}

// Nanoseconds per second the worker did not run.
double StalledPerSecond(uint64_t stalled_before, std::chrono::steady_clock::time_point start) {
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return double(g_stalled_ns.load() - stalled_before) / seconds;
}

void TestSamplerOnRunningThread() {
  std::printf("the stack sampler unwinds a copied stack of a running thread\n");
  std::thread worker([] { Outer(512); });
  HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, GetThreadId(worker.native_handle()));
  CHECK(thread != nullptr);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto alone_start = std::chrono::steady_clock::now();
  const uint64_t alone_stalled = g_stalled_ns.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  const double stalled_alone = StalledPerSecond(alone_stalled, alone_start);

  const uint64_t outer = StartOf(reinterpret_cast<const void*>(&Outer));
  const uint64_t middle = StartOf(reinterpret_cast<const void*>(&Middle));
  const uint64_t inner = StartOf(reinterpret_cast<const void*>(&Inner));
  StackSampler sampler;
  uint64_t stack_high = 0;
  int samples = 0;
  int ordered = 0;
  int in_inner = 0;
  uint64_t stopped_ns = 0;
  uint64_t stopped_max_ns = 0;
  uint64_t unwind_ns = 0;
  size_t copied = 0;
  const auto sampling_start = std::chrono::steady_clock::now();
  const uint64_t sampling_stalled = g_stalled_ns.load();
  for (int i = 0; i < 400; ++i) {
    std::this_thread::sleep_for(std::chrono::microseconds(500));
    const auto start = std::chrono::steady_clock::now();
    if (!sampler.Sample(thread, stack_high)) {
      continue;
    }
    const auto sampled = std::chrono::steady_clock::now() - start;
    unwind_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(sampled).count()) - sampler.stopped_ns();
    ++samples;
    stopped_ns += sampler.stopped_ns();
    stopped_max_ns = std::max(stopped_max_ns, sampler.stopped_ns());
    copied += sampler.copied();
    int middle_at = -1;
    int outer_at = -1;
    const auto frames = sampler.frames();
    for (int f = 0; f < int(frames.size()); ++f) {
      if (frames[f].function_start == middle && middle_at < 0) {
        middle_at = f;
      }
      if (frames[f].function_start == outer && outer_at < 0) {
        outer_at = f;
      }
    }
    if (!frames.empty() && (frames[0].function_start == inner ||
                            (frames[0].function_start == 0 && frames[0].address - inner < 0x400))) {
      ++in_inner;
    }
    if (middle_at >= 0 && outer_at > middle_at) {
      ++ordered;
    }
  }
  const double sampling_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - sampling_start).count();
  const double stalled_sampled = StalledPerSecond(sampling_stalled, sampling_start);
  g_stop = true;
  worker.join();
  CloseHandle(thread);

  CHECK(samples >= 350);
  CHECK(stack_high != 0);
  CHECK(ordered >= samples * 9 / 10);
  CHECK(in_inner > 0);
  const double stop_us = samples ? double(stopped_ns) / samples / 1000.0 : 0.0;
  CHECK(stop_us < 500.0);
  const double extra_stall_us = samples ? (stalled_sampled - stalled_alone) * sampling_seconds / samples / 1000.0 : 0.0;
  std::printf("  %d samples at %.0f/s, %d with Inner < Middle < Outer, %d stopped in the leaf; sampler held the thread "
              "%.1f us per sample (max %.1f us), unwound in %.1f us, %.1f KB copied; the worker lost %.1f us per sample "
              "(%.2f%% of its time alone, %.2f%% while sampled)\n",
              samples, samples / sampling_seconds, ordered, in_inner, stop_us, double(stopped_max_ns) / 1000.0,
              samples ? double(unwind_ns) / samples / 1000.0 : 0.0, samples ? double(copied) / samples / 1024.0 : 0.0,
              extra_stall_us, stalled_alone / 1e7, stalled_sampled / 1e7);
}

void TestHostModules() {
  std::printf("host modules classify program and system code\n");
  const HostModules modules;
  CHECK(modules.Classify(reinterpret_cast<uint64_t>(&TestHostModules)) == CodeKind::kProgram);
  const auto lookup = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlLookupFunctionEntry");
  CHECK(modules.Classify(reinterpret_cast<uint64_t>(lookup)) == CodeKind::kSystem);
  const std::string name = modules.Name(reinterpret_cast<uint64_t>(GetModuleHandleW(L"ntdll.dll")) + 0x10);
  CHECK(name == "ntdll.dll+0x10");
}
#endif

}  // namespace

int main() {
  TestIndex();
  TestProfile();
  TestReport();
  TestGuestThreadName();
#if defined(_WIN32)
  TestHostModules();
  TestSamplerOnRunningThread();
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
