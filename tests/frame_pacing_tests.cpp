// Frame model: the game steps by the wall-clock time of each frame (clamped to 0.1 s) and renders once per step, so
// no frame rate setting changes the game's speed and none needs interpolation. Checks the delta the game uses at
// every frame rate choice, the limiter's cadence in simulated time, the pacing report and the hooks.
// Run: mua_frame_pacing_tests

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

// The game clock's own 10 ms floor adds 10 ms for every faster frame, so 150 FPS played 15 game seconds in 10; the hook
// at 0x827ECCB0 (sub_827ECB38) replaces that floor in f0 before the fsubs/fsel that apply it.
void TestMua2ClockStepFloor() {
  std::printf("MUA 2's clock step floor keeps game time at real time above 100 FPS\n");
  constexpr double kGameClockFloor = 0.01;
  CHECK(std::fabs(GameSecondsPlayed(1.0 / 150.0, 10.0, kGameClockFloor) - 15.0) < 1e-6);
  for (const int presented : {120, 144, 165, 240, 1000}) {
    CHECK(std::fabs(GameSecondsPlayed(1.0 / presented, 10.0, kMua2WorldDeltaMin) - 10.0) < 1e-9);
  }

  std::string pacing = ReadSource("frame_pacing.cpp");
  std::erase(pacing, '\r');
  CHECK(Has(pacing, "void mua_clock_step_floor_mua2(PPCRegister& floor) {\n"
                    "  floor.f64 = double(float(mua::kMua2WorldDeltaMin));\n}"));
  std::string overrides = ReadSource("../config/mua2/overrides.toml");
  std::erase(overrides, '\r');
  CHECK(Has(overrides, "address = 0x827ECCB0\nname = \"mua_clock_step_floor_mua2\"\nregisters = [\"f0\"]\n"
                       "after_instruction = false\n"));
  for (const char* other : {"../config/mua1/overrides.toml", "../config/mua1-gold/overrides.toml"}) {
    CHECK(!Has(ReadSource(other), "mua_clock_step_floor"));
  }

#ifdef MUA_GENERATED_ROOT
  std::string body;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MUA_GENERATED_ROOT) / "mua2")) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t start = source.find("DEFINE_REX_FUNC(sub_827ECB38) {");
    if (start != std::string::npos) {
      body = source.substr(start, source.find("\n}\n", start) - start);
      break;
    }
  }
  const size_t load = body.find("// lfs f0,21840(r11)");
  const size_t floor = body.find("// fsubs f11,f1,f0", load);
  const size_t hook = body.find("mua_clock_step_floor_mua2(ctx.f0);", floor);
  const size_t apply = body.find("ctx.f11.f64 = double(float(ctx.f1.f64 - ctx.f0.f64));", floor);
  const size_t select = body.find("// fsel f10,f11,f1,f0", floor);
  const size_t ceiling = body.find("// lfs f0,4(r9)", select);
  CHECK(load != std::string::npos && floor != std::string::npos && select != std::string::npos &&
        ceiling != std::string::npos);
  CHECK(hook != std::string::npos && hook < apply && apply < select);
#endif
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
  CHECK(Has(pacing, "REX_HOOK_RAW(MUA_GUEST_WORLD_UPDATE)"));
  CHECK(Has(pacing, "mua::guest::kWorldDeltaOffset"));
  CHECK(Has(pacing, "Game timing: frame rate={} | {}"));
  CHECK(Has(pacing, "REX_HOOK_RAW(MUA_GUEST_OPTIONS_INIT)"));
  CHECK(Has(pacing, "kMaxPresentReports = 12"));
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

  // Every game times World::Update, publishes it with the limiter and guest fence waits and logs a slow one.
  CHECK(Has(pacing, "const auto update_start = std::chrono::steady_clock::now();"));
  CHECK(Has(pacing, "NoteWorldUpdateTime(std::chrono::steady_clock::now() - update_start);"));
  CHECK(Has(pacing, "mua::AddGuestFrameTime(mua::GuestFrameTime::kWorldUpdate, elapsed);"));
  CHECK(Has(pacing, "REXLOG_WARN(\"Slow world update: {:.1f} ms\", ms);"));
  CHECK(!Has(pacing, "MUA_RUNTIME_"));
  CHECK(!Has(pacing, "DEBUG_SAULO"));
  CHECK(Has(limiter, "AddGuestFrameTime(GuestFrameTime::kLimiterWait, LimiterClock::now() - wait_start);"));
  const std::string fence = ReadSource("gpu_fence_wait.cpp");
  CHECK(Has(fence, "mua::AddGuestFrameTime(mua::GuestFrameTime::kFenceWait"));
  const std::string counters = ReadSource("guest_frame_counters.h");
  CHECK(Has(counters, "#include <rex/perf/counter.h>"));
  CHECK(!Has(counters, "_WIN32"));
  CHECK(!Has(counters, "MUA_RUNTIME_"));
  const size_t hook = pacing.find("REX_HOOK_RAW(MUA_GUEST_WORLD_UPDATE)");
  const size_t timed = pacing.find("const auto update_start", hook);
  CHECK(hook != std::string::npos && timed != std::string::npos &&
        pacing.substr(hook, timed - hook).find("#if") == std::string::npos);
  CHECK(pacing.find("#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)") == std::string::npos);
  CHECK(Has(counters, "rex::perf::IncrementCounter(CounterId::kGuestUpdates, 1);"));
  const std::string watchdog = ReadSource("hang_watchdog.cpp");
  CHECK(Has(watchdog, "std::thread(run, \"Hitch sampler\", SampleHitches, static_cast<HWND>(native_window)).detach();"));
  CHECK(Has(watchdog, "kHitchSampleAfter = std::chrono::milliseconds(150)"));
  CHECK(Has(watchdog, "kHitchStackAfter = std::chrono::milliseconds(500)"));
  CHECK(Has(watchdog, "Hitch sample: {} ms open | top {} | guest {}"));
  // The hitch sampler runs in every game program, never in the launcher; no profiler is left behind.
  const size_t sampler = watchdog.find("std::thread(run, \"Hitch sampler\", SampleHitches");
  CHECK(sampler != std::string::npos && watchdog.rfind("#if MUA_HAS_RECOMPILED_CODE", sampler) + 40 > sampler);
  CHECK(!Has(watchdog, "MUA_RUNTIME_MUA1"));
  CHECK(!Has(watchdog, "ProfileRenderThread"));
  CHECK(!Has(watchdog, "DEBUG_SAULO"));
  // Without a PDB a frame still names its module and offset.
  CHECK(Has(watchdog, "LogText(module.ModuleName)"));
  CHECK(Has(watchdog, "fmt::format(\"{}+0x{:X}\", module_name, address - module.BaseOfImage)"));
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
  TestMua2ClockStepFloor();
  TestLimiterCadence();
  TestReport();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
