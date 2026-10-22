#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mua {

inline constexpr int kFrameRateDefault = 60;
inline constexpr int kFrameRateUnlimited = 0;
inline constexpr int kNativeFrameRate = 30;
// The game's own max_fps option defaults to 31; above 60 it means uncapped.
inline constexpr int kGameNativeMaxFps = 31;
inline constexpr int kGameMaxFpsCeiling = 60;
inline constexpr int kFrameRates[] = {kNativeFrameRate, 60, 90, 120, 144, 165, 240, kFrameRateUnlimited};

inline bool IsFrameRateChoice(int fps) {
  return std::find(std::begin(kFrameRates), std::end(kFrameRates), fps) != std::end(kFrameRates);
}

// The setting's value, or the default when it holds anything else.
inline int FrameRateOrDefault(int fps) {
  return IsFrameRateChoice(fps) ? fps : kFrameRateDefault;
}

// The stored text as a rate; anything that is not one of the choices, an empty value included, reads as 60.
inline int FrameRateFromText(const std::string& text) {
  char* end = nullptr;
  const long value = std::strtol(text.c_str(), &end, 10);
  return !text.empty() && end != nullptr && *end == '\0' && value >= 0 && value <= 100000
             ? FrameRateOrDefault(static_cast<int>(value))
             : kFrameRateDefault;
}

inline bool IsHighFrameRate(int fps) {
  return fps == kFrameRateUnlimited || fps > kNativeFrameRate;
}

// Original: the game flips at its guest vblank. Immediate: the game's present callback flips at once, clearing
// the present-interval bits so a render loop faster than the guest vblank is not held back by it.
enum class PresentMode { kOriginal, kImmediate };

inline PresentMode PresentModeFor(int fps) {
  return IsHighFrameRate(fps) ? PresentMode::kImmediate : PresentMode::kOriginal;
}

// The game's max_fps for a launcher rate; 0 is uncapped.
inline int GameMaxFpsFor(int fps) {
  return fps == kNativeFrameRate ? kGameNativeMaxFps : fps;
}

// The game's frame interval, written over the one it derives itself, which drops to none above 60.
inline float GameFrameInterval(int max_fps) {
  return max_fps <= 0 ? 0.0f : 1.0f / float(max_fps);
}

// The game's own interval paces every rate, so the host limiter never waits.
inline int ProjectLimiterRate(int) {
  return kFrameRateUnlimited;
}

// Half a frame at the cap, from 16 ms at 30 FPS down to 4 ms: the wait for pipelines prepared in the background
// has to fit in the frame's spare time (king-kong-recompiled 8702562).
inline int ShaderWaitMsFor(int fps) {
  return fps > 0 ? std::clamp(500 / fps, 4, 16) : 4;
}

inline std::string FrameRateLabel(int fps) {
  if (fps == kFrameRateUnlimited) {
    return "Unlimited";
  }
  return fps == kNativeFrameRate ? "30 - Original" : std::to_string(fps);
}

// The launcher's choices: the stored value and its label.
inline std::vector<std::pair<std::string, std::string>> FrameRateChoices() {
  std::vector<std::pair<std::string, std::string>> choices;
  for (const int fps : kFrameRates) {
    choices.emplace_back(std::to_string(fps), FrameRateLabel(fps));
  }
  return choices;
}

using LimiterClock = std::chrono::steady_clock;

inline LimiterClock::duration FramePeriod(int fps) {
  if (fps <= 0) {
    return LimiterClock::duration::zero();
  }
  return std::chrono::duration_cast<LimiterClock::duration>(std::chrono::duration<double>(1.0 / fps));
}

// The schedule of the frame limiter. Next() gives the time the caller has to wait for, if any.
class FrameLimiter {
 public:
  std::optional<LimiterClock::time_point> Next(int fps, LimiterClock::time_point now) {
    const auto period = FramePeriod(fps);
    if (period == LimiterClock::duration::zero()) {
      armed_ = false;
      return std::nullopt;
    }
    if (!armed_ || fps != fps_) {
      armed_ = true;
      fps_ = fps;
      target_ = now + period;
      return std::nullopt;
    }
    const auto deadline = target_;
    target_ = now - deadline > period ? now + period : deadline + period;
    if (deadline <= now) {
      return std::nullopt;
    }
    return deadline;
  }

 private:
  bool armed_ = false;
  int fps_ = 0;
  LimiterClock::time_point target_;
};

// Blocks the calling thread until the deadline: a high-resolution timer for most of it, then a short spin.
void WaitUntil(LimiterClock::time_point deadline);

// The selected frame rate, or the default when the cvar holds something else.
int ConfiguredFrameRate();

// Called by the swap hook before the game's swap: waits for the next frame time.
void PaceGuestFrame();

// The present callback's argument with the interval cleared when the selected rate leaves the guest vblank.
uint32_t PresentArgument(uint32_t argument);

}  // namespace mua
