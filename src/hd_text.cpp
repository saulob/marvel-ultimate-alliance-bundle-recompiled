#include "hd_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "igb_image.h"
#include "xmlb.h"

namespace mua::hd_text {
namespace {

using font_hd::FaceButton;
using font_hd::IconRule;

constexpr IconRule kMediumIcons[] = {
    {162, FaceButton::kA}, {163, FaceButton::kB}, {164, FaceButton::kX}, {165, FaceButton::kY},
    {166, {}}, {167, {}}, {168, {}}, {169, {}}, {170, {}}, {171, {}}, {172, {}}, {173, {}}, {174, {}},
    {175, {}}, {176, {}}, {177, {}}, {178, {}}, {179, {}}, {180, {}}, {181, {}}, {182, {}}, {183, {}},
    {184, {}}, {185, {}}, {186, {}}, {187, {}}, {188, {}}, {194, {}}, {195, {}}, {196, {}},
};

constexpr uint32_t kRgbaComponents = 4;

std::string FontPath(std::string_view folder, std::string_view name, std::string_view extension) {
  std::string path(folder);
  path += name;
  path += extension;
  return path;
}

}  // namespace

const GameAssets* AssetsFor(GameId game) {
  for (const GameAssets& assets : kGameAssets) {
    if (assets.game == game) {
      return &assets;
    }
  }
  return nullptr;
}

bool ParseSetting(std::string_view value) {
  std::string lower(value);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return lower != "false" && lower != "0" && lower != "off" && lower != "no";
}

uint32_t AtlasScaleFor(int render_scale) {
  if (render_scale >= 3) {
    return 4;
  }
  return render_scale == 2 ? 2 : 0;
}

Plan PlanFor(GameId game, bool on, int render_scale) {
  const GameAssets* assets = AssetsFor(game);
  if (!assets || !on) {
    return {assets, 0};
  }
  return {assets, AtlasScaleFor(render_scale)};
}

std::span<const IconRule> IconRulesFor(std::string_view font_index) {
  if (font_index == kIconFontIndex) {
    return kMediumIcons;
  }
  return {};
}

Result BuildFontOverlay(const zip::ReadAt& read, uint64_t file_size, std::string_view font_set, uint32_t scale) {
  Result result;
  if (scale < 2) {
    result.error = "render resolution 1x needs no rebuilt atlases";
    return result;
  }
  const auto directory = zip::ReadDirectory(read, file_size);
  if (!directory) {
    result.error = "the archive's central directory could not be read";
    return result;
  }
  auto read_entry = [&](std::string_view name) -> std::optional<std::vector<uint8_t>> {
    const zip::CentralEntry* entry = zip::Find(*directory, name);
    return entry ? zip::ReadEntry(read, *entry) : std::nullopt;
  };
  const auto set_data = read_entry(font_set);
  const auto fonts = set_data ? xmlb::ParseFontFiles(*set_data) : std::nullopt;
  if (!fonts || fonts->empty()) {
    result.error = std::string("no font list in ") + std::string(font_set);
    return result;
  }

  std::vector<zip::Replacement> replacements;
  for (const xmlb::FontFile& font : *fonts) {
    const std::string texture_name = FontPath("textures/fonts/", font.texture, ".igb");
    if (std::any_of(replacements.begin(), replacements.end(),
                    [&](const zip::Replacement& r) { return zip::SameName(r.name, texture_name); })) {
      continue;
    }
    const std::string table_name = FontPath("ui/fonts/", font.data, ".xmlb");
    const auto table_data = read_entry(table_name);
    const auto table = table_data ? xmlb::ParseFontTable(*table_data) : std::nullopt;
    const zip::CentralEntry* texture_entry = zip::Find(*directory, texture_name);
    const auto igb = texture_entry ? zip::ReadEntry(read, *texture_entry) : std::nullopt;
    const auto image = igb ? igb::FindImage(*igb) : std::nullopt;
    if (!table || !image || image->components != kRgbaComponents) {
      result.error = "font " + font.texture + " is not a single RGBA atlas with a font table";
      return result;
    }
    if (uint64_t(image->width) * scale > kMaxTextureDimension ||
        uint64_t(image->height) * scale > kMaxTextureDimension) {
      result.error = "font " + font.texture + " would exceed the texture size limit";
      return result;
    }
    const font_hd::Atlas atlas = font_hd::FromBottomUp(
        image->width, image->height,
        std::span<const uint8_t>(*igb).subspan(image->pixels_offset, image->image_size));
    FontResult info{font.texture, image->width * scale, image->height * scale, scale, {}};
    const auto hd = font_hd::BuildHdAtlas(atlas, *table, scale, IconRulesFor(font.index), &info.report);
    if (!hd) {
      result.error = "font " + font.texture + " has no two-tone text glyphs";
      return result;
    }
    const std::vector<uint8_t> pixels = font_hd::ToBottomUp(*hd);
    auto rebuilt = igb::ReplaceImage(*igb, hd->width, hd->height, pixels);
    if (!rebuilt) {
      result.error = "font " + font.texture + " could not be rewritten";
      return result;
    }
    replacements.push_back({texture_entry->name, std::move(*rebuilt)});
    result.fonts.push_back(std::move(info));
  }

  result.overlay = zip::BuildOverlay(*directory, replacements);
  if (!result.overlay) {
    result.error = "the archive overlay could not be built";
    result.fonts.clear();
  }
  return result;
}

std::string SummaryOn(uint32_t scale, size_t atlases, uint64_t added_bytes, long long milliseconds) {
  return "HD text: On | scale=" + std::to_string(scale) + "x | atlases=" + std::to_string(atlases) +
         " | added=" + std::to_string(added_bytes / 1024) + " KiB | built in " + std::to_string(milliseconds) + " ms";
}

std::string DescribeTone(const font_hd::TwoTone& tone) {
  auto hex = [](const font_hd::Color& c) {
    char text[8];
    auto byte = [](float v) { return unsigned(std::lround(std::clamp(v, 0.0f, 255.0f))); };
    std::snprintf(text, sizeof(text), "%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
    return std::string(text);
  };
  return "fill #" + hex(tone.fill) + " outline #" + hex(tone.outline);
}

}  // namespace mua::hd_text
