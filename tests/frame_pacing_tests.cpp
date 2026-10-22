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

void TestWiring() {
  std::printf("hooks\n");
  const std::string pacing = ReadSource("frame_pacing.cpp");
  CHECK(Has(pacing, "REX_HOOK_RAW(sub_821BAE40)"));
  CHECK(Has(pacing, "kWorldDeltaOffset = 1000"));
  CHECK(Has(pacing, "DEBUG_SAULO frame pacing"));
  CHECK(Has(pacing, "REX_HOOK_RAW(sub_821212A8)"));
  CHECK(Has(pacing, "kMaxReports = 12"));
  const std::string limiter = ReadSource("frame_limiter.cpp");
  CHECK(Has(limiter, "REX_HOOK_RAW(sub_82497D00)"));
  CHECK(!Has(limiter, "interpolat"));
  CHECK(!Has(limiter, "vsync"));
}

}  // namespace

int main() {
  TestWorldDelta();
  TestGameSpeedAtEveryRate();
  TestLimiterCadence();
  TestReport();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
