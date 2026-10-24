// Frame model: the game steps by the wall-clock time of each frame (clamped to 0.1 s) and renders once per step, so
// no frame rate setting changes the game's speed and none needs interpolation. Checks the delta the game uses at
// every frame rate choice, the limiter's cadence in simulated time, the pacing report and the hooks.
// Run: mua_frame_pacing_tests

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "frame_limiter.h"
#include "frame_pacing.h"

using namespace mua;

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

double Seconds(LimiterClock::duration d) { return std::chrono::duration<double>(d).count(); }

LimiterClock::time_point At(double seconds) {
  return LimiterClock::time_point(std::chrono::duration_cast<LimiterClock::duration>(std::chrono::duration<double>(seconds)));
}

void TestWorldDelta() {
  std::printf("the delta of a world update\n");
  CHECK(WorldDelta(0.0) == kWorldDeltaMin);
  CHECK(WorldDelta(-1.0) == kWorldDeltaMin);
  CHECK(WorldDelta(1.0 / 240.0) == 1.0 / 240.0);
  CHECK(WorldDelta(1.0 / 60.0) == 1.0 / 60.0);
  CHECK(WorldDelta(0.1) == 0.1);
  CHECK(WorldDelta(0.5) == 0.1);
  CHECK(kWorldDeltaMax == 0.1 && kWorldDeltaMin == 1e-6);
  CHECK(std::fabs(kMenuFrameInterval - 1.0 / 31.0) < 1e-12);
}

void TestGameSpeedAtEveryRate() {
  std::printf("game speed at every frame rate\n");
  // A frame the guest renders every 1/rate s, for the rates that are presentable: the game advances one second per
  // real second, whatever the rate.
  for (const int rate : kFrameRates) {
    const int presented = rate == kFrameRateUnlimited ? 1000 : rate;
    const double interval = 1.0 / presented;
    CHECK(std::fabs(GameSecondsPlayed(interval, 10.0) - 10.0) < 1e-9);
    // The same on the limiter's schedule, with a frame that takes longer now and then.
    FrameLimiter limiter;
    double now = 100.0;
    double game_seconds = 0.0;
    double previous = now;
    const int limiter_rate = ProjectLimiterRate(rate == kFrameRateUnlimited ? 0 : rate);
    for (int frame = 0; frame < 3000; ++frame) {
      now += (frame % 97 == 0 ? 0.004 : 0.0005);
      if (const auto deadline = limiter.Next(limiter_rate == 0 ? presented : limiter_rate, At(now))) {
        now = Seconds(deadline->time_since_epoch());
      }
      game_seconds += WorldDelta(now - previous);
      previous = now;
    }
    CHECK(std::fabs(game_seconds - (now - 100.0)) < 1e-6);
  }
  // Only a frame beyond 0.1 s loses time: the guest drops it rather than catching up.
  CHECK(GameSecondsPlayed(0.5, 10.0) < 10.0 / 4.0);
  CHECK(std::fabs(GameSecondsPlayed(0.1, 10.0) - 10.0) < 1e-9);
}

// Marvel Ultimate Alliance 2 runs at every second vblank on the native rate and on the host limiter's schedule elsewhere.
void TestMua2GameSpeedAtEveryRate() {
  std::printf("MUA 2 game speed at every frame rate\n");
  const FramePolicy& policy = kMua2FramePolicy;
  CHECK(kMua2WorldDeltaMin == 1e-5 && WorldDelta(0.0, kMua2WorldDeltaMin) == kMua2WorldDeltaMin);
  CHECK(WorldDelta(0.5, kMua2WorldDeltaMin) == kWorldDeltaMax);
  for (const int rate : kFrameRates) {
    const int limiter_rate = ProjectLimiterRate(rate, policy);
    // The native rate presents at the guest vblank; an unlimited rate is bound by the render loop alone.
    const int presented = rate == policy.native ? 30 : rate == kFrameRateUnlimited ? 1000 : rate;
    FrameLimiter limiter;
    double now = 100.0;
    double game_seconds = 0.0;
    double previous = now;
    for (int frame = 0; frame < 3000; ++frame) {
      now += (frame % 97 == 0 ? 0.004 : 0.0005);
      if (const auto deadline = limiter.Next(limiter_rate == 0 ? presented : limiter_rate, At(now))) {
        now = Seconds(deadline->time_since_epoch());
      }
      game_seconds += WorldDelta(now - previous, kMua2WorldDeltaMin);
      previous = now;
    }
    CHECK(std::fabs(game_seconds - (now - 100.0)) < 1e-6);
    CHECK(std::fabs(GameSecondsPlayed(1.0 / presented, 10.0, kMua2WorldDeltaMin) - 10.0) < 1e-9);
  }
  CHECK(ProjectLimiterRate(30, policy) == kFrameRateUnlimited);
  for (const int rate : {60, 90, 120, 144, 165, 240}) {
    CHECK(ProjectLimiterRate(rate, policy) == rate);
  }
  CHECK(ProjectLimiterRate(kFrameRateUnlimited, policy) == kFrameRateUnlimited);
  CHECK(PresentModeFor(30, policy) == PresentMode::kOriginal);
  for (const int rate : {60, 90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(PresentModeFor(rate, policy) == PresentMode::kImmediate);
  }
}

void TestLimiterCadence() {
  std::printf("limiter cadence at the fixed rates\n");
  for (const int rate : {30, 60, 90, 120, 144, 165, 240}) {
    FrameLimiter limiter;
    double now = 50.0;
    double first = 0.0;
    for (int frame = 0; frame < 2000; ++frame) {
      // The guest is ready sooner than the period.
      now += 0.1 / rate;
      if (const auto deadline = limiter.Next(rate, At(now))) {
        now = Seconds(deadline->time_since_epoch());
      }
      if (frame == 0) {
        first = now;
      }
    }
    CHECK(std::fabs((now - first) / 1999.0 - 1.0 / rate) < 1e-7);
  }
  // The rate never depends on the monitor: the schedule takes the rate and the time and nothing else.
  FrameLimiter a;
  FrameLimiter b;
  CHECK(a.Next(120, At(1.0)) == b.Next(120, At(1.0)));
  CHECK(a.Next(120, At(1.001)) == b.Next(120, At(1.001)));
  // 30 is the native rate and keeps the vblank pacing; every other rate leaves it.
  CHECK(PresentModeFor(30) == PresentMode::kOriginal);
  for (const int rate : {60, 90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(PresentModeFor(rate) == PresentMode::kImmediate);
  }
  CHECK(FrameRateFromText("") == 60 && FrameRateFromText("75") == 60 && FrameRateFromText("-1") == 60);
}

void TestReport() {
  std::printf("pacing report\n");
  PacingReport report;
  double now = 1000.0;
  int lines = 0;
  std::string last;
  for (int i = 0; i < 2000; ++i) {
    now += 1.0 / 144.0;
    if (const auto line = report.Add(1.0 / 144.0, now)) {
      ++lines;
      last = *line;
    }
  }
  CHECK(lines == 1);
  CHECK(Has(last, "world updates/s=144."));
  CHECK(Has(last, "game seconds per real second=1.00"));
  CHECK(Has(last, "clamped=0"));
  PacingReport slow;
  std::string slow_line;
  now = 0.0;
  for (int i = 0; i < 200 && slow_line.empty(); ++i) {
    now += 0.2;
    if (const auto line = slow.Add(WorldDelta(0.2), now)) {
      slow_line = *line;
    }
  }
  CHECK(Has(slow_line, "clamped=5"));
  CHECK(Has(slow_line, "game seconds per real second=0.5"));
  CallRateReport calls;
  std::optional<double> rate;
  double at = 0.0;
  for (int i = 0; i < 2000 && !rate.has_value(); ++i) {
    at += 1.0 / 120.0;
    rate = calls.Add(at);
  }
  CHECK(rate.has_value() && std::fabs(*rate - 120.0) < 1.0);
}

void TestWorldUpdateTiming() {
  std::printf("world update timing\n");
  // Calls 0.125 s apart: the window closes on the 81st, 10 s after the first.
  WorldUpdateTiming timing;
  std::string line;
  double now = 0.0;
  for (int i = 0; i < 1000 && line.empty(); ++i) {
    now += 0.125;
    const double ms = i % 4 == 0 ? 12.0 : (i % 4 == 1 ? 6.0 : 2.0);
    if (const auto report = timing.Add(ms, now)) {
      line = *report;
    }
  }
  CHECK(Has(line, "world update: calls/s=8.1"));
  CHECK(Has(line, "avg=5.58 ms | max=12.00 ms"));
  CHECK(Has(line, ">5 ms=41 | >10 ms=21"));
  CHECK(Has(line, "busy=4.5% of real time"));
  std::string next;
  for (int i = 0; i < 1000 && next.empty(); ++i) {
    now += 0.125;
    if (const auto report = timing.Add(1.0, now)) {
      next = *report;
    }
  }
  CHECK(Has(next, "max=1.00 ms | >5 ms=0 | >10 ms=0"));
}

void TestWiring() {
  std::printf("hooks\n");
  const std::string pacing = ReadSource("frame_pacing.cpp");
  CHECK(Has(pacing, "REX_HOOK_RAW(MUA_GUEST_WORLD_UPDATE)"));
  CHECK(Has(pacing, "mua::guest::kWorldDeltaOffset"));
  CHECK(Has(pacing, "DEBUG_SAULO frame pacing"));
  CHECK(Has(pacing, "REX_HOOK_RAW(MUA_GUEST_OPTIONS_INIT)"));
  CHECK(Has(pacing, "kMaxReports = 12"));
  const std::string limiter = ReadSource("frame_limiter.cpp");
  CHECK(Has(limiter, "REX_HOOK_RAW(MUA_GUEST_PRESENT_CALLBACK)"));
  const std::string gold = ReadSource("guest_map_mua1_gold.h");
  CHECK(Has(gold, "#define MUA_GUEST_WORLD_UPDATE sub_821BAE40"));
  CHECK(Has(gold, "#define MUA_GUEST_OPTIONS_INIT sub_821212A8"));
  CHECK(Has(gold, "kWorldDeltaOffset = 1000"));
  const std::string mua1 = ReadSource("guest_map_mua1.h");
  CHECK(Has(mua1, "#define MUA_GUEST_WORLD_UPDATE sub_821AA278"));
  CHECK(Has(mua1, "#define MUA_GUEST_OPTIONS_INIT sub_82111298"));
  CHECK(Has(mua1, "kWorldDeltaOffset = 1000"));
  const std::string mua2 = ReadSource("guest_map_mua2.h");
  CHECK(Has(mua2, "#define MUA_GUEST_WORLD_UPDATE sub_826011D8"));
  CHECK(Has(mua2, "#define MUA_GUEST_OPTIONS_INIT sub_82533548"));
  CHECK(Has(mua2, "kWorldDeltaOffset = 1676"));
  CHECK(Has(pacing, "policy.host_limiter"));
  CHECK(!Has(limiter, "interpolat"));
  CHECK(!Has(limiter, "vsync"));

  // DEBUG_SAULO: MUA1 times World::Update and publishes it with the limiter and guest fence waits.
  CHECK(Has(pacing, "const auto update_start = std::chrono::steady_clock::now();"));
  CHECK(Has(pacing, "NoteWorldUpdateTime(std::chrono::steady_clock::now() - update_start);"));
  CHECK(Has(pacing, "mua::AddGuestFrameTime(mua::GuestFrameTime::kWorldUpdate, elapsed);"));
  CHECK(Has(pacing, "DEBUG_SAULO slow world update"));
  CHECK(Has(limiter, "AddGuestFrameTime(GuestFrameTime::kLimiterWait, LimiterClock::now() - wait_start);"));
  const std::string fence = ReadSource("gpu_fence_wait.cpp");
  CHECK(Has(fence, "mua::AddGuestFrameTime(mua::GuestFrameTime::kFenceWait"));
  const std::string counters = ReadSource("guest_frame_counters.h");
  CHECK(Has(counters, "#if defined(MUA_RUNTIME_MUA1) && defined(_WIN32)"));
  CHECK(Has(counters, "rex::perf::IncrementCounter(CounterId::kGuestUpdates, 1);"));
  const std::string watchdog = ReadSource("hang_watchdog.cpp");
  CHECK(Has(watchdog, "std::thread(SampleHitches, static_cast<HWND>(native_window)).detach();"));
  CHECK(Has(watchdog, "kHitchSampleAfter = std::chrono::milliseconds(150)"));
  CHECK(Has(watchdog, "kHitchStackAfter = std::chrono::milliseconds(500)"));
  CHECK(Has(watchdog, "DEBUG_SAULO hitch: {} ms open | top {} | guest {}"));
  // Everything goes to the session log: no hang_N.txt or hitch_stacks_N.txt.
  CHECK(!Has(watchdog, "ofstream"));
  CHECK(!Has(watchdog, "hang_\""));
  CHECK(!Has(watchdog, "hitch_stacks_"));
}

}  // namespace

int main() {
  TestWorldDelta();
  TestGameSpeedAtEveryRate();
  TestMua2GameSpeedAtEveryRate();
  TestLimiterCadence();
  TestReport();
  TestWorldUpdateTiming();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
