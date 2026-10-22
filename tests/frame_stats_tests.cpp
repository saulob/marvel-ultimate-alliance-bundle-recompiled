// SummarizeFrameTimesMs (hang_watchdog.cpp/.h): average and 1%-low FPS, worst frame, and hitch bucket counts over a
// window of per-frame times. Pure function, no window, no thread, no log output.
// Run: mua_frame_stats_tests

#include <cmath>
#include <cstdio>
#include <vector>

#include "hang_watchdog.h"

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
    }                                                                \
  } while (0)

bool Near(double a, double b, double tol = 0.05) { return std::fabs(a - b) <= tol; }

void TestEmptyWindow() {
  std::printf("empty window\n");
  const FrameStatsSummary summary = SummarizeFrameTimesMs({});
  CHECK(summary.average_fps == 0.0);
  CHECK(summary.low_1_percent_fps == 0.0);
  CHECK(summary.worst_ms == 0.0);
  CHECK(summary.frames_over_50ms == 0);
  CHECK(summary.frames_over_100ms == 0);
}

void TestSteady60Fps() {
  std::printf("a steady 60 fps window has no hitches and a 1%% low equal to the average\n");
  std::vector<double> times(600, 1000.0 / 60.0);
  const FrameStatsSummary summary = SummarizeFrameTimesMs(times);
  CHECK(Near(summary.average_fps, 60.0));
  CHECK(Near(summary.low_1_percent_fps, 60.0));
  CHECK(Near(summary.worst_ms, 1000.0 / 60.0));
  CHECK(summary.frames_over_50ms == 0);
  CHECK(summary.frames_over_100ms == 0);
}

void TestOneHitchDragsDownTheLowPercentile() {
  std::printf("one long frame among many short ones is the worst frame and counts toward both buckets\n");
  std::vector<double> times(99, 1000.0 / 60.0);
  times.push_back(120.0);
  const FrameStatsSummary summary = SummarizeFrameTimesMs(times);
  CHECK(Near(summary.worst_ms, 120.0));
  CHECK(summary.frames_over_50ms == 1);
  CHECK(summary.frames_over_100ms == 1);
  // The slowest 1% of 100 frames is exactly the hitch itself, so the 1% low is far below the average.
  CHECK(summary.low_1_percent_fps < summary.average_fps);
}

void TestOrderIndependent() {
  std::printf("the input order does not change the result\n");
  const FrameStatsSummary forward = SummarizeFrameTimesMs({16.0, 17.0, 60.0, 16.0, 18.0});
  const FrameStatsSummary shuffled = SummarizeFrameTimesMs({60.0, 18.0, 16.0, 17.0, 16.0});
  CHECK(Near(forward.average_fps, shuffled.average_fps));
  CHECK(Near(forward.worst_ms, shuffled.worst_ms));
  CHECK(forward.frames_over_50ms == shuffled.frames_over_50ms);
}

}  // namespace

int main() {
  TestEmptyWindow();
  TestSteady60Fps();
  TestOneHitchDragsDownTheLowPercentile();
  TestOrderIndependent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
