#pragma once

#include <cstdint>

#include <imgui.h>

#include <rex/ui/style.h>

#include "game_theme.h"

namespace mua {

struct AchievementsTheme {
  rex::ui::AchievementsStyle style;
  ImVec4 border;
  ImVec4 title_bar;
  ImVec4 scrollbar;
  ImVec4 scrollbar_hovered;
};

inline ImVec4 ThemeColor(uint32_t rgb, float alpha = 1.0f) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

inline ImVec4 ThemeBlend(const ImVec4& from, const ImVec4& to, float amount) {
  return ImVec4(from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
                from.z + (to.z - from.z) * amount, from.w);
}

// The SDK's achievements layout with the running game's launcher accent in place of its green.
inline AchievementsTheme AchievementsThemeFor(const GameDefinition* game) {
  const GamePalette palette = PaletteOf(game ? game->id : GameId::kMua1);
  const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
  const ImVec4 black(0.0f, 0.0f, 0.0f, 1.0f);
  const ImVec4 primary = ThemeColor(palette.primary);
  const ImVec4 hover = ThemeColor(palette.hover);

  AchievementsTheme theme;
  theme.style.header_text = ThemeBlend(hover, white, 0.2f);
  theme.style.progress_bar = primary;
  theme.style.unlocked_title = ThemeBlend(primary, white, 0.45f);
  theme.style.unlocked_desc = ImVec4(0.78f, 0.80f, 0.84f, 1.0f);
  theme.style.locked_desc = ImVec4(0.66f, 0.68f, 0.72f, 1.0f);
  theme.style.row_unlocked_bg = ThemeBlend(primary, black, 0.7f);
  theme.style.row_unlocked_bg.w = 0.55f;
  theme.border = ThemeColor(palette.border);
  theme.title_bar = ThemeBlend(primary, black, 0.55f);
  theme.scrollbar = theme.border;
  theme.scrollbar_hovered = hover;
  return theme;
}

}  // namespace mua
