#pragma once

#include <cstdint>
#include <cstdlib>
#include <optional>

#include <rex/input/state_merge.h>

namespace mua {

enum class CursorInputDevice { kMouse, kKeyboard, kController };

// The last meaningful gameplay input device decides the cursor. Overlays and a
// lost focus force it visible until they go away.
class CursorHider {
 public:
  bool visible() const { return visible_; }
  bool overlay_open() const { return overlay_open_; }

  std::optional<bool> MouseActivity() { return Note(CursorInputDevice::kMouse); }
  std::optional<bool> KeyboardActivity() { return Note(CursorInputDevice::kKeyboard); }
  std::optional<bool> ControllerActivity() { return Note(CursorInputDevice::kController); }

  std::optional<bool> SetOverlayOpen(bool open) {
    overlay_open_ = open;
    return Recalculate();
  }

  std::optional<bool> SetFocused(bool focused) {
    focused_ = focused;
    return Recalculate();
  }

 private:
  std::optional<bool> Note(CursorInputDevice device) {
    last_device_ = device;
    return Recalculate();
  }

  std::optional<bool> Recalculate() {
    const bool visible = overlay_open_ || !focused_ || last_device_ == CursorInputDevice::kMouse;
    if (visible == visible_) {
      return std::nullopt;
    }
    visible_ = visible;
    return visible;
  }

  CursorInputDevice last_device_ = CursorInputDevice::kMouse;
  bool overlay_open_ = false;
  bool focused_ = true;
  bool visible_ = true;
};

// Reports only the start of meaningful controller input, not a held button,
// stick, or trigger, so mouse movement is not overridden by input already down.
class ControllerActivityFilter {
 public:
  bool Update(const rex::input::X_INPUT_GAMEPAD& gamepad);

 private:
  uint16_t buttons_ = 0;
  bool left_trigger_ = false;
  bool right_trigger_ = false;
  bool thumb_lx_ = false;
  bool thumb_ly_ = false;
  bool thumb_rx_ = false;
  bool thumb_ry_ = false;
};

inline bool ControllerActivityFilter::Update(const rex::input::X_INPUT_GAMEPAD& gamepad) {
  const uint16_t buttons = static_cast<uint16_t>(gamepad.buttons);
  bool started = (buttons & ~buttons_) != 0;
  buttons_ = buttons;

  const auto trigger_started = [&started](uint8_t value, bool& was_active) {
    const bool active = value >= rex::input::kTriggerThreshold;
    started = started || (active && !was_active);
    was_active = active;
  };
  const auto thumb_started = [&started](rex::be<int16_t> value, bool& was_active) {
    const bool active = std::abs(static_cast<int>(value.get())) >= rex::input::kThumbDeadzone;
    started = started || (active && !was_active);
    was_active = active;
  };

  trigger_started(gamepad.left_trigger, left_trigger_);
  trigger_started(gamepad.right_trigger, right_trigger_);
  thumb_started(gamepad.thumb_lx, thumb_lx_);
  thumb_started(gamepad.thumb_ly, thumb_ly_);
  thumb_started(gamepad.thumb_rx, thumb_rx_);
  thumb_started(gamepad.thumb_ry, thumb_ry_);
  return started;
}

}  // namespace mua
