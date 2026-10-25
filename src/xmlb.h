#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Raven binary XML: node = (name, next sibling, first child, attribute count, (name, value)...), all file offsets.
namespace mua::xmlb {

struct Node {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attributes;
  std::vector<Node> children;

  std::string_view Get(std::string_view key) const;
};

std::optional<Node> Parse(std::span<const uint8_t> file);

// One <glyph> of a FONT_TABLE: texture coordinates are fractions of the atlas, measured from its top row.
struct Glyph {
  uint32_t code = 0;
  float s = 0, t = 0, s2 = 0, t2 = 0;
  int width = 0;
  int height = 0;
};

struct FontTable {
  int height = 0;
  std::vector<Glyph> glyphs;  // only glyphs with a non-empty rectangle
};

std::optional<FontTable> ParseFontTable(std::span<const uint8_t> file);

struct FontFile {
  std::string data;
  std::string texture;
  std::string index;
  std::string language;
};

std::optional<std::vector<FontFile>> ParseFontFiles(std::span<const uint8_t> file);

}  // namespace mua::xmlb
