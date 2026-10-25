#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "font_atlas_hd.h"
#include "game_definition.h"
#include "zip_overlay.h"

// HD Text: the HD font set's atlases, rebuilt at startup from the game's own archive and served through an in-memory
// overlay of it. The game files are not changed.
namespace mua::hd_text {

struct GameAssets {
  GameId game;
  std::string_view archive;
  std::string_view font_set;
};

// Both titles ask for z\assetsfb.wad; Gold Edition ships z\assetsfb.zip, which its loader opens when the .wad is
// missing. Their font sets, tables and atlases are identical.
inline constexpr GameAssets kGameAssets[] = {
    {GameId::kMua1, "game:\\z\\assetsfb.wad", "ui/fonts/fonts_360_HD.xmlb"},
    {GameId::kMua1Gold, "game:\\z\\assetsfb.zip", "ui/fonts/fonts_360_HD.xmlb"},
};

inline constexpr char kSettingKey[] = "mua_hd_text";

const GameAssets* AssetsFor(GameId game);

// Off only for an explicit false value; anything else keeps the default, On.
bool ParseSetting(std::string_view value);

// 0 keeps the game's atlases: 1x draws them texel for pixel.
uint32_t AtlasScaleFor(int render_scale);

struct Plan {
  const GameAssets* assets = nullptr;
  uint32_t scale = 0;

  bool Builds() const { return assets && scale > 0; }
};

Plan PlanFor(GameId game, bool on, int render_scale);

// Icons of the medium font, in 24-texel cells; 162 to 165 are the A, B, X and Y buttons.
inline constexpr std::string_view kIconFontIndex = "medium";
std::span<const font_hd::IconRule> IconRulesFor(std::string_view font_index);

inline constexpr uint32_t kMaxTextureDimension = 8192;

struct FontResult {
  std::string texture;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t scale = 0;
  font_hd::BuildReport report;
};

struct Result {
  std::optional<zip::Overlay> overlay;
  std::vector<FontResult> fonts;
  std::string error;
};

Result BuildFontOverlay(const zip::ReadAt& read, uint64_t file_size, std::string_view font_set, uint32_t scale);

std::string SummaryOn(uint32_t scale, size_t atlases, uint64_t added_bytes, long long milliseconds);

std::string DescribeTone(const font_hd::TwoTone& tone);

}  // namespace mua::hd_text
