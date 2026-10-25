#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>

namespace mua {

inline constexpr char kLauncherXKey[] = "mua_launcher_x";
inline constexpr char kLauncherYKey[] = "mua_launcher_y";
inline constexpr char kLauncherWidthKey[] = "mua_launcher_width";
inline constexpr char kLauncherHeightKey[] = "mua_launcher_height";
inline constexpr char kLauncherMaximizedKey[] = "mua_launcher_maximized";
inline constexpr char kLauncherFullscreenKey[] = "mua_launcher_fullscreen";

using SettingValues = std::map<std::string, std::string, std::less<>>;

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  int width() const { return right - left; }
  int height() const { return bottom - top; }
  bool operator==(const Rect&) const = default;
};

// The launcher window's restored rectangle in screen pixels, and whether it is maximized.
struct WindowPlacement {
  Rect normal;
  bool maximized = false;

  bool operator==(const WindowPlacement&) const = default;
};

// The rectangle means nothing while minimized.
struct WindowState {
  Rect rect;
  bool maximized = false;
  bool minimized = false;
};

inline constexpr int kMinPlacementWidth = 320;
inline constexpr int kMinPlacementHeight = 240;
inline constexpr int kMinVisibleWidth = 100;
inline constexpr int kMinVisibleHeight = 50;
inline constexpr double kPlacementSaveDelaySeconds = 0.5;

// A launcher window is never left minimized at startup; a maximized one stays maximized when restored.
inline bool StartupNeedsRestore(const WindowState& state) {
  return state.minimized;
}

inline bool PlacementIsSet(const WindowPlacement& placement) {
  return placement.normal.width() >= kMinPlacementWidth && placement.normal.height() >= kMinPlacementHeight;
}

// Enough of the window must lie on a display to be grabbed. Display coordinates may be negative.
inline bool PlacementIsVisible(const WindowPlacement& placement, std::span<const Rect> work_areas) {
  if (!PlacementIsSet(placement)) {
    return false;
  }
  for (const Rect& area : work_areas) {
    const int width = std::min(placement.normal.right, area.right) - std::max(placement.normal.left, area.left);
    const int height = std::min(placement.normal.bottom, area.bottom) - std::max(placement.normal.top, area.top);
    if (width >= kMinVisibleWidth && height >= kMinVisibleHeight) {
      return true;
    }
  }
  return false;
}

// A maximized window with an unusable rectangle is maximized where the window already is.
struct PreShowPlan {
  std::optional<Rect> normal;
  bool maximized = false;

  bool Needed() const { return normal.has_value() || maximized; }
};

inline PreShowPlan PlanPreShow(const WindowPlacement& saved, std::span<const Rect> work_areas) {
  PreShowPlan plan;
  if (PlacementIsVisible(saved, work_areas)) {
    plan.normal = saved.normal;
  }
  plan.maximized = saved.maximized;
  return plan;
}

template <typename Settings>
WindowPlacement ReadPlacement(const Settings& settings) {
  WindowPlacement placement;
  placement.normal.left = settings.GetInt(kLauncherXKey, 0);
  placement.normal.top = settings.GetInt(kLauncherYKey, 0);
  placement.normal.right = placement.normal.left + settings.GetInt(kLauncherWidthKey, 0);
  placement.normal.bottom = placement.normal.top + settings.GetInt(kLauncherHeightKey, 0);
  placement.maximized = settings.GetBool(kLauncherMaximizedKey, false);
  return placement;
}

inline SettingValues PlacementValues(const WindowPlacement& placement) {
  return {
      {kLauncherXKey, std::to_string(placement.normal.left)},
      {kLauncherYKey, std::to_string(placement.normal.top)},
      {kLauncherWidthKey, std::to_string(placement.normal.width())},
      {kLauncherHeightKey, std::to_string(placement.normal.height())},
      {kLauncherMaximizedKey, placement.maximized ? "true" : "false"},
  };
}

template <typename Settings>
bool ReadLauncherFullscreen(const Settings& settings) {
  return settings.GetBool(kLauncherFullscreenKey, false);
}

inline SettingValues LauncherFullscreenValues(bool fullscreen) {
  return {{kLauncherFullscreenKey, fullscreen ? "true" : "false"}};
}

// Whether the launcher reopens in its own fullscreen. Only the player's F11 changes it; leaving fullscreen for the
// game does not.
class LauncherFullscreenPreference {
 public:
  explicit LauncherFullscreenPreference(bool saved = false) : wanted_(saved) {}

  bool wanted() const { return wanted_; }
  void UserSet(bool fullscreen) { wanted_ = fullscreen; }

 private:
  bool wanted_;
};

// Says when the placement has changed and settled, so it is saved once and not on every move.
// A maximized window keeps the restored rectangle; a minimized or fullscreen one changes nothing.
class PlacementTracker {
 public:
  explicit PlacementTracker(std::optional<WindowPlacement> saved = std::nullopt) : saved_(saved), current_(saved) {}

  void Observe(const WindowState& state, bool fullscreen, double now) {
    if (fullscreen) {
      return;
    }
    WindowPlacement next = current_.value_or(WindowPlacement{});
    next.maximized = state.maximized;
    if (!state.minimized && !state.maximized) {
      next.normal = state.rect;
    }
    if (!saved_) {
      saved_ = next;
    }
    if (!current_ || next != *current_) {
      current_ = next;
      changed_at_ = now;
    }
  }

  std::optional<WindowPlacement> TakeSettled(double now) {
    if (!Dirty() || now - changed_at_ < kPlacementSaveDelaySeconds) {
      return std::nullopt;
    }
    return Flush();
  }

  std::optional<WindowPlacement> Current() const { return current_; }

  std::optional<WindowPlacement> Flush() {
    if (!Dirty()) {
      return std::nullopt;
    }
    saved_ = current_;
    return saved_;
  }

 private:
  bool Dirty() const { return current_ && current_ != saved_ && PlacementIsSet(*current_); }

  std::optional<WindowPlacement> saved_;
  std::optional<WindowPlacement> current_;
  double changed_at_ = 0.0;
};

// Follows a requested fullscreen change of the launcher window. The change lands on a later UI tick, so the window
// is not observed until it has landed or the wait has run out.
class FullscreenGate {
 public:
  static constexpr double kTimeoutSeconds = 1.5;

  struct Step {
    bool observe = true;   // the window may be read for its placement
    bool restore = false;  // fullscreen was just left: the tracked placement has to be applied again
  };

  void Request(bool fullscreen) {
    target_ = fullscreen;
    requested_at_.reset();
  }

  bool Pending() const { return target_.has_value(); }

  Step Update(bool fullscreen, double now) {
    if (!target_) {
      return {!fullscreen, false};
    }
    if (!requested_at_) {
      requested_at_ = now;
    }
    if (fullscreen == *target_) {
      const bool left = !*target_;
      target_.reset();
      return {false, left};
    }
    if (now - *requested_at_ >= kTimeoutSeconds) {
      target_.reset();
      return {!fullscreen, false};
    }
    return {false, false};
  }

 private:
  std::optional<bool> target_;
  std::optional<double> requested_at_;
};

}  // namespace mua
