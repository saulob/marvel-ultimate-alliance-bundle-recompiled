#pragma once

#include "game_definition.h"

namespace mua {

inline constexpr double kSkipIntroStartupNoticeSeconds = 5.0;
inline constexpr char kSkipIntroNoticeText[] = "Skip intro: On";

// Every game with a runtime applies the Skip intro setting.
inline bool SkipIntroNoticeApplies(const GameDefinition* game, bool skip_intro) {
  return game && game->runtime_supported && skip_intro;
}

// A notice waits for the FPS counter when the counter is shown, so both appear on the same frame.
constexpr bool SkipIntroNoticeReady(bool frame_counter_shown, bool frame_counter_visible) {
  return !frame_counter_shown || frame_counter_visible;
}

class SkipIntroNotice {
 public:
  // Once per launch. Off queues nothing.
  void QueueStartup(bool enabled, double seconds) {
    if (startup_queued_) {
      return;
    }
    startup_queued_ = true;
    if (enabled) {
      seconds_ = seconds;
      state_ = State::kQueued;
    }
  }

  // The clock starts at the first frame where the notice is ready.
  bool Visible(double now, bool ready = true) {
    if (state_ == State::kQueued && ready) {
      shown_at_ = now;
      state_ = State::kShown;
    }
    return state_ == State::kShown && now >= shown_at_ && now - shown_at_ < seconds_;
  }

  const char* text() const { return kSkipIntroNoticeText; }

 private:
  enum class State { kHidden, kQueued, kShown };

  State state_ = State::kHidden;
  double seconds_ = 0.0;
  double shown_at_ = 0.0;
  bool startup_queued_ = false;
};

}  // namespace mua
