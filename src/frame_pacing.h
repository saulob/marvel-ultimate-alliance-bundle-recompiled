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

// One line about the world updates every kReportSeconds: how fast game time runs against real time.
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

// Calls per second of a guest function, reported once per kReportSeconds window.
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

// Counts one genuine guest present for the presents/s line.
void CountPresentForDiagnostics();

}  // namespace mua
