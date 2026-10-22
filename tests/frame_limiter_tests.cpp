// Frame rate choices, the frame limiter schedule and the wait, plus the settings and hook wiring. The hooks and the
// launcher need the running game.
// Run: mua_frame_limiter_tests

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

#include "frame_limiter.h"

using namespace mua;
using namespace std::chrono_literals;

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

LimiterClock::time_point At(double seconds) {
  return LimiterClock::time_point{} +
         std::chrono::duration_cast<LimiterClock::duration>(std::chrono::duration<double>(seconds));
}

double Seconds(LimiterClock::duration d) { return std::chrono::duration<double>(d).count(); }

void TestChoices() {
  std::printf("frame rate choices\n");
  const int expected[] = {30, 60, 90, 120, 144, 165, 240, 0};
  CHECK(std::size(kFrameRates) == std::size(expected));
  for (size_t i = 0; i < std::size(expected); ++i) {
    CHECK(kFrameRates[i] == expected[i]);
    CHECK(IsFrameRateChoice(expected[i]));
  }
  CHECK(kFrameRateDefault == 60);
  CHECK(kNativeFrameRate == 30);
  CHECK(kFrameRateUnlimited == 0);
  for (const int other : {-1, 1, 45, 59, 61, 100, 200, 360, 1000}) {
    CHECK(!IsFrameRateChoice(other));
    CHECK(FrameRateOrDefault(other) == kFrameRateDefault);
  }
  for (const int rate : kFrameRates) {
    CHECK(FrameRateOrDefault(rate) == rate);
  }
  const auto choices = FrameRateChoices();
  CHECK(choices.size() == std::size(kFrameRates));
  CHECK(choices[0] == std::make_pair(std::string("30"), std::string("30 - Original")));
  CHECK(choices[1] == std::make_pair(std::string("60"), std::string("60")));
  CHECK(choices[4] == std::make_pair(std::string("144"), std::string("144")));
  CHECK(choices[7] == std::make_pair(std::string("0"), std::string("Unlimited")));
}

void TestStoredText() {
  std::printf("stored frame rate text\n");
  CHECK(FrameRateFromText("60") == 60);
  CHECK(FrameRateFromText("144") == 144);
  CHECK(FrameRateFromText("0") == kFrameRateUnlimited);
  CHECK(FrameRateFromText("30") == 30);
  for (const char* bad : {"", " ", "abc", "-1", "61", "144x", "1.5", "99999999999999999999", "0x90"}) {
    CHECK(FrameRateFromText(bad) == kFrameRateDefault);
  }
}

void TestPolicy() {
  std::printf("rate policy\n");
  CHECK(!IsHighFrameRate(30));
  for (const int rate : {60, 90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(IsHighFrameRate(rate));
  }
  CHECK(PresentModeFor(30) == PresentMode::kOriginal);
  for (const int rate : {60, 90, 120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(PresentModeFor(rate) == PresentMode::kImmediate);
  }
  for (const int rate : kFrameRates) {
    CHECK(ProjectLimiterRate(rate) == kFrameRateUnlimited);
  }
}

void TestGameMaxFps() {
  std::printf("the game's max_fps follows the launcher rate
");
  CHECK(GameMaxFpsFor(30) == 31);
  CHECK(GameFrameInterval(31) == 1.0f / 31.0f);
  for (const int rate : {60, 90, 120, 144, 165, 240}) {
    CHECK(GameMaxFpsFor(rate) == rate);
    CHECK(GameFrameInterval(GameMaxFpsFor(rate)) == 1.0f / float(rate));
  }
  CHECK(GameMaxFpsFor(kFrameRateUnlimited) == 0);
  CHECK(GameFrameInterval(GameMaxFpsFor(kFrameRateUnlimited)) == 0.0f);
}

void TestShaderWait() {
  std::printf("pipeline wait is half a frame at the cap\n");
  CHECK(ShaderWaitMsFor(30) == 16);
  CHECK(ShaderWaitMsFor(60) == 8);
  CHECK(ShaderWaitMsFor(90) == 5);
  for (const int rate : {120, 144, 165, 240, kFrameRateUnlimited}) {
    CHECK(ShaderWaitMsFor(rate) == 4);
  }
}

void TestPeriod() {
  std::printf("frame periods\n");
  CHECK(FramePeriod(0) == LimiterClock::duration::zero());
  CHECK(FramePeriod(-5) == LimiterClock::duration::zero());
  for (const int rate : {30, 90, 120, 144, 165, 240}) {
    CHECK(std::fabs(Seconds(FramePeriod(rate)) - 1.0 / rate) < 1e-8);
  }
}

void TestLimiterSchedule() {
  std::printf("limiter schedule\n");
  FrameLimiter limiter;
  const double period = 1.0 / 144.0;
  // The first frame arms the limiter and never waits.
  CHECK(!limiter.Next(144, At(10.0)));
  // A frame that comes early waits for the next slot.
  const auto deadline = limiter.Next(144, At(10.0 + 0.2 * period));
  CHECK(deadline && std::fabs(Seconds(*deadline - At(10.0)) - period) < 1e-8);
  // Frames at the pace never drift: slot n is n periods from the start.
  double now = 10.0 + period;
  double target = 10.0 + 2 * period;
  for (int i = 0; i < 1000; ++i) {
    const auto wait = limiter.Next(144, At(now));
    CHECK(wait && std::fabs(Seconds(*wait - At(target))) < 1e-5);
    now = target;
    target += period;
  }
}

void TestLimiterLateAndUnlimited() {
  std::printf("late frames, rate changes and unlimited\n");
  FrameLimiter limiter;
  const double period = 1.0 / 90.0;
  limiter.Next(90, At(5.0));
  // A late frame does not wait, and the schedule is not owed the lost time.
  CHECK(!limiter.Next(90, At(5.0 + 3 * period)));
  const auto after = limiter.Next(90, At(5.0 + 3 * period + 0.1 * period));
  CHECK(after && Seconds(*after - At(5.0 + 3 * period)) <= period + 1e-8);
  // A long stall re-anchors instead of bursting.
  CHECK(!limiter.Next(90, At(8.0)));
  const auto resumed = limiter.Next(90, At(8.0 + 0.1 * period));
  CHECK(resumed && Seconds(*resumed - At(8.0)) <= 2 * period);

  // A rate change re-arms: the first frame at the new rate never waits.
  CHECK(!limiter.Next(144, At(9.0)));
  CHECK(limiter.Next(144, At(9.0 + 0.1 * period)).has_value());
  // Unlimited never waits and disarms.
  for (int i = 0; i < 10; ++i) {
    CHECK(!limiter.Next(kFrameRateUnlimited, At(9.5 + i * 0.001)));
  }
  CHECK(!limiter.Next(144, At(10.0)));
}

void TestWait() {
  std::printf("high-resolution wait\n");
  for (const int rate : {30, 90, 144, 240}) {
    const auto start = LimiterClock::now();
    const auto deadline = start + FramePeriod(rate);
    WaitUntil(deadline);
    const auto end = LimiterClock::now();
    CHECK(end >= deadline);
    CHECK(end - deadline < 100ms);
  }
  // A deadline in the past returns at once.
  const auto before = LimiterClock::now();
  WaitUntil(before - 1s);
  CHECK(LimiterClock::now() - before < 5ms);
  // Many short waits keep the average pace.
  const auto start = LimiterClock::now();
  FrameLimiter limiter;
  int frames = 0;
  while (LimiterClock::now() - start < 500ms) {
    if (const auto deadline = limiter.Next(240, LimiterClock::now())) {
      WaitUntil(*deadline);
    }
    ++frames;
  }
  CHECK(frames >= 100 && frames <= 125);
}

void TestWiring() {
  std::printf("frame rate wiring\n");
  const std::string limiter = ReadSource("frame_limiter.cpp");
  CHECK(Has(limiter, "REX_HOOK_RAW(sub_82497D00)"));
  CHECK(Has(limiter, "mua::PresentArgument(ctx.r3.u32)"));
  CHECK(Has(limiter, "argument & ~kPresentIntervalMask"));
  CHECK(Has(limiter, "kPresentIntervalMask = 0xF00"));
  CHECK(Has(limiter, "PresentModeFor(ConfiguredFrameRate())"));
  CHECK(Has(limiter, "REXCVAR_GET(mua_frame_rate)"));

  const std::string host = ReadSource("window_host.cpp");
  CHECK(Has(host, "REX_HOOK_RAW(__imp__VdSwap)"));
  CHECK(Has(host, "mua::PaceGuestFrame()"));
  CHECK(Has(host, "kRepeatPresentSite = 0x8249EFE0"));
  CHECK(Has(host, "REX_HOOK_RAW(sub_82498340)"));

  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_INT32(mua_frame_rate, mua::kFrameRateDefault"));
  CHECK(Has(settings, "\"mua_frame_rate\","));
  CHECK(Has(settings, "FrameRateFromText(frame_rate)"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Choice(\"Frame rate\", kFrameRateHelp, \"mua_frame_rate\", \"60\""));
  CHECK(Has(launcher, "30 FPS is the original game rate."));

}

}  // namespace

int main() {
  TestChoices();
  TestStoredText();
  TestPolicy();
  TestGameMaxFps();
  TestShaderWait();
  TestPeriod();
  TestLimiterSchedule();
  TestLimiterLateAndUnlimited();
  TestWait();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
