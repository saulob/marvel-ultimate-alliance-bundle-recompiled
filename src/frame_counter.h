#pragma once

#include <charconv>
#include <string>

#include "game_theme.h"
#include "window_host.h"

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace mua {

// Adds the in-game counter and its F2 toggle.
void CreateFrameCounter(rex::ui::ImGuiDrawer* drawer);

// Shows the Skip intro startup notice once, below the counter's place.
void CreateSkipIntroNotice(rex::ui::ImGuiDrawer* drawer);
void QueueStartupSkipIntroNotice(bool skip_intro);

// The counter's border follows the running game's launcher accent.
inline uint32_t FrameCounterAccent(const GameDefinition* game) {
  return PaletteOf(game ? game->id : GameId::kMua1).primary;
}

// "60 FPS  16.7 ms": the rate as a whole number and the average frame time to a tenth of a millisecond.
inline std::string FrameCounterText(const GuestFrameStats& stats) {
  char fps[32];
  char frame_time[32];
  const auto fps_end = std::to_chars(fps, fps + sizeof(fps), stats.fps, std::chars_format::fixed, 0).ptr;
  const auto frame_time_end =
      std::to_chars(frame_time, frame_time + sizeof(frame_time), stats.frame_time_ms, std::chars_format::fixed, 1).ptr;
  return std::string(fps, fps_end) + " FPS  " + std::string(frame_time, frame_time_end) + " ms";
}

// The counter is drawn when it is on and the game has a measured rate.
inline bool FrameCounterVisible(bool enabled, const GuestFrameStats& stats) {
  return enabled && stats.frame_count > 0;
}

}  // namespace mua
