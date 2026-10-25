#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "xmlb.h"

// Rebuilds a 720p font atlas `scale` times larger with the same layout; the font tables use fractional texture
// coordinates, so only the texels change. Text glyphs are a fill colour inside a one-texel outline colour.
namespace mua::font_hd {

struct Atlas {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // top row first, 4 bytes per texel

  uint8_t* At(uint32_t x, uint32_t y) { return rgba.data() + (size_t(y) * width + x) * 4; }
  const uint8_t* At(uint32_t x, uint32_t y) const { return rgba.data() + (size_t(y) * width + x) * 4; }
};

// Texels, top row 0, end exclusive.
struct Rect {
  uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Color {
  float r = 0, g = 0, b = 0;
};

struct TwoTone {
  Color outline;
  Color fill;
};

enum class FaceButton : uint8_t { kA, kB, kX, kY };

struct IconRule {
  uint32_t code;
  std::optional<FaceButton> face;
};

struct BuildReport {
  TwoTone tone;
  size_t text_glyphs = 0;
  size_t icons = 0;
  size_t face_buttons = 0;
  // Glyphs not in the icon list whose colours do not fit the two tones; rebuilt as icons.
  size_t unlisted_icons = 0;
};

// IGB images store rows bottom first.
Atlas FromBottomUp(uint32_t width, uint32_t height, std::span<const uint8_t> pixels);
std::vector<uint8_t> ToBottomUp(const Atlas& atlas);

Rect GlyphRect(const xmlb::Glyph& glyph, uint32_t width, uint32_t height);

std::optional<TwoTone> DetectTwoTone(const Atlas& atlas, std::span<const Rect> excluded);

// True when at most a tenth of the rectangle's opaque texels lie off the line between the two colours.
bool FitsTwoTone(const Atlas& atlas, Rect rect, const TwoTone& tone);

Atlas RebuildText(const Atlas& atlas, uint32_t scale, const TwoTone& tone);
void RebuildIcon(const Atlas& atlas, Atlas& out, uint32_t scale, Rect cell);
void DrawFaceButton(const Atlas& atlas, Atlas& out, uint32_t scale, Rect cell, FaceButton button);

std::optional<Atlas> BuildHdAtlas(const Atlas& atlas, const xmlb::FontTable& table, uint32_t scale,
                                  std::span<const IconRule> icons, BuildReport* report = nullptr);

}  // namespace mua::font_hd
