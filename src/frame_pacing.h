#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace mua {

// World::Update steps the game by the wall-clock time since its last call: at least 1 microsecond
// and at most 0.1 s, the rest of a longer frame is dropped. There is no fixed step or accumulator, so every update
// is followed by one render and a faster presentation changes how often the game steps, not how fast it runs.
inline constexpr double kWorldDeltaMin = 1e-6;
inline constexpr double kWorldDeltaMax = 0.1;
// Marvel Ultimate Alliance 2 floors the same delta at 10 microseconds.
inline constexpr double kMua2WorldDeltaMin = 1e-5;
// Outside a map the game spins until 1/max_fps has passed; max_fps defaults to 31.
inline constexpr double kMenuFrameInterval = 1.0 / 31.0;

inline double WorldDelta(double wall_clock_interval, double minimum = kWorldDeltaMin) {
  return std::clamp(wall_clock_interval, minimum, kWorldDeltaMax);
}

// Game time that passes while frames of the given wall-clock length are played.
inline double GameSecondsPlayed(double frame_interval, double real_seconds, double minimum = kWorldDeltaMin) {
  const double frames = real_seconds / frame_interval;
  return frames * WorldDelta(frame_interval, minimum);
}

// DEBUG_SAULO: a line about the world updates every kReportSeconds, to check the frame model against a real run.
class PacingReport {
 public:
  static constexpr double kReportSeconds = 10.0;

  std::optional<std::string> Add(double delta_seconds, double now_seconds) {
    if (!started_) {
      started_ = true;
      window_start_ = now_seconds;
    }
    ++updates_;
    delta_sum_ += delta_seconds;
    delta_max_ = std::max(delta_max_, delta_seconds);
    clamped_ += delta_seconds >= kWorldDeltaMax ? 1 : 0;
    const double elapsed = now_seconds - window_start_;
    if (elapsed < kReportSeconds) {
      return std::nullopt;
    }
    char text[160];
    std::snprintf(text, sizeof(text),
                  "world updates/s=%.1f | game seconds per real second=%.3f | delta avg=%.2f ms max=%.2f ms | clamped=%u",
                  updates_ / elapsed, delta_sum_ / elapsed, 1000.0 * delta_sum_ / updates_, 1000.0 * delta_max_,
                  clamped_);
    window_start_ = now_seconds;
    updates_ = 0;
    delta_sum_ = 0.0;
    delta_max_ = 0.0;
    clamped_ = 0;
    return std::string(text);
  }

 private:
  bool started_ = false;
  double window_start_ = 0.0;
  uint32_t updates_ = 0;
  double delta_sum_ = 0.0;
  double delta_max_ = 0.0;
  uint32_t clamped_ = 0;
};

// DEBUG_SAULO: calls per second of a candidate function, reported once per kReportSeconds window.
class CallRateReport {
 public:
  static constexpr double kReportSeconds = 10.0;

  std::optional<double> Add(double now_seconds) {
    if (!started_) {
      started_ = true;
      window_start_ = now_seconds;
    }
    ++calls_;
    const double elapsed = now_seconds - window_start_;
    if (elapsed < kReportSeconds) {
      return std::nullopt;
    }
    const double rate = calls_ / elapsed;
    window_start_ = now_seconds;
    calls_ = 0;
    return rate;
  }

 private:
  bool started_ = false;
  double window_start_ = 0.0;
  uint32_t calls_ = 0;
};

// DEBUG_SAULO: host time spent inside World::Update, reported once per kReportSeconds window.
class WorldUpdateTiming {
 public:
  static constexpr double kReportSeconds = 10.0;

  std::optional<std::string> Add(double update_ms, double now_seconds) {
    if (!started_) {
      started_ = true;
      window_start_ = now_seconds;
    }
    ++calls_;
    total_ms_ += update_ms;
    max_ms_ = std::max(max_ms_, update_ms);
    over_5ms_ += update_ms > 5.0 ? 1 : 0;
    over_10ms_ += update_ms > 10.0 ? 1 : 0;
    const double elapsed = now_seconds - window_start_;
    if (elapsed < kReportSeconds) {
      return std::nullopt;
    }
    char text[200];
    std::snprintf(text, sizeof(text),
                  "world update: calls/s=%.1f | avg=%.2f ms | max=%.2f ms | >5 ms=%u | >10 ms=%u | busy=%.1f%% of "
                  "real time",
                  calls_ / elapsed, total_ms_ / calls_, max_ms_, over_5ms_, over_10ms_,
                  100.0 * total_ms_ / (1000.0 * elapsed));
    *this = WorldUpdateTiming{};
    started_ = true;
    window_start_ = now_seconds;
    return std::string(text);
  }

 private:
  bool started_ = false;
  double window_start_ = 0.0;
  uint32_t calls_ = 0;
  double total_ms_ = 0.0;
  double max_ms_ = 0.0;
  uint32_t over_5ms_ = 0;
  uint32_t over_10ms_ = 0;
};

// DEBUG_SAULO: counts one genuine guest present for the presents/s line.
void CountPresentForDiagnostics();

}  // namespace mua
