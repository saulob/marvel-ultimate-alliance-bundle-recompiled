#pragma once

#include <algorithm>

namespace mua {

constexpr float kOverlayViewportFraction = 0.7f;
constexpr float kOverlayViewportMargin = 16.0f;
constexpr float kStickDeadzone = 0.25f;

constexpr float kAchievementsMinWidth = 420.0f;
constexpr float kAchievementsMaxWidth = 550.0f;
constexpr float kAchievementsMinHeight = 320.0f;
constexpr float kAchievementsMaxHeight = 450.0f;
constexpr float kScrollPixelsPerSecond = 480.0f;

// Multiples of the launcher font's design size.
constexpr float kAchievementsBodyFontScale = 1.00f;
constexpr float kAchievementsHeadingFontScale = 1.05f;
constexpr float kAchievementsFooterFontScale = 1.05f;

// Window size along one axis: about 70% of the viewport, clamped to [min_size, max_size] and kept inside it.
inline float FitWindowExtent(float viewport, float min_size, float max_size) {
  const float wanted = std::clamp(viewport * kOverlayViewportFraction, min_size, max_size);
  return std::max(0.0f, std::min(wanted, viewport - 2.0f * kOverlayViewportMargin));
}

// Direction the list scrolls from the D-pad and the left stick: -1 up .. 1 down. The D-pad wins over the stick.
inline float ScrollDirection(bool dpad_up, bool dpad_down, float stick_y) {
  if (dpad_up != dpad_down) return dpad_up ? -1.0f : 1.0f;
  const float magnitude = stick_y < 0.0f ? -stick_y : stick_y;
  if (magnitude <= kStickDeadzone) return 0.0f;
  const float amount = std::min(1.0f, (magnitude - kStickDeadzone) / (1.0f - kStickDeadzone));
  return stick_y > 0.0f ? -amount : amount;
}

// A button that is never reported released (a controller unplugged as the dialog closes) cannot block the game forever.
constexpr double kReleaseTimeoutSeconds = 1.0;

// Whether the modal may release the game's input: nothing it received is still held, or the wait ran out.
inline bool InputReleased(bool anything_held, double waited_seconds, double timeout_seconds) {
  return !anything_held || waited_seconds >= timeout_seconds;
}

// Closing a modal: asking to close (B, Escape, F7, X) starts a wait, and the game gets its input back only after
// what was pressed in the dialog is released, so the closing press never also reaches the game.
class CloseGate {
 public:
  void Request() { requested_ = true; }
  bool requested() const { return requested_; }

  // Once per frame. True when the dialog may go and the game may read its input again.
  bool Step(bool anything_held, double now) {
    if (!requested_) return false;
    if (since_ < 0.0) since_ = now;
    return InputReleased(anything_held, now - since_, kReleaseTimeoutSeconds);
  }

 private:
  bool requested_ = false;
  double since_ = -1.0;
};

}  // namespace mua
