#pragma once

#include <cstdint>
#include <functional>

#include "aspect_ratio.h"

inline constexpr char kWindowTitle[] = "Marvel Ultimate Alliance Recompiled";

// The latest one-second sample of the game's swaps; all zero until the first one is measured.
struct GuestFrameStats {
  double fps = 0;
  double frame_time_ms = 0;
  uint64_t frame_count = 0;
};

namespace rex::ui {
class ImGuiDrawer;
class Window;
class WindowedAppContext;
}  // namespace rex::ui

// `ui_wants_mouse` reports whether ImGui currently captures the mouse, so
// runtime menus force the cursor visible like the project's own overlays.
void AttachWindowHost(rex::ui::Window* window, rex::ui::WindowedAppContext& app_context,
                      std::function<bool()> ui_wants_mouse);
void DetachWindowHost();

// The current native client area in physical pixels: the extent the presenter draws into now, not the
// last size event cached by the window.
mua::WindowSize CurrentDrawableSize(rex::ui::Window* window);

// Keeps the cursor in step with the runtime's ImGui overlays.
void CreateCursorSync(rex::ui::ImGuiDrawer* drawer);

GuestFrameStats GetGuestFrameStats();
