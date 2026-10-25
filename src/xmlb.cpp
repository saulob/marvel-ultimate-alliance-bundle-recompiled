#include "xmlb.h"

#include <charconv>

namespace mua::xmlb {
namespace {

constexpr uint32_t kMagic = 0x11B1;
constexpr size_t kRootOffset = 8;
constexpr size_t kNodeHeaderSize = 16;
constexpr int kMaxDepth = 32;
constexpr size_t kMaxNodes = 1 << 16;

uint32_t Get32(std::span<const uint8_t> d, size_t p) {
  return uint32_t(d[p]) | (uint32_t(d[p + 1]) << 8) | (uint32_t(d[p + 2]) << 16) | (uint32_t(d[p + 3]) << 24);
}

class Parser {
 public:
  explicit Parser(std::span<const uint8_t> d) : d_(d) {}

  std::optional<std::string> String(uint32_t offset) const {
    if (offset >= d_.size()) {
      return std::nullopt;
    }
    for (size_t end = offset; end < d_.size(); ++end) {
      if (d_[end] == 0) {
        return std::string(reinterpret_cast<const char*>(d_.data() + offset), end - offset);
      }
    }
    return std::nullopt;
  }

  // Parses the node at `offset` and its children; returns the next sibling's offset through `next`.
  std::optional<Node> ParseNode(uint32_t offset, int depth, uint32_t& next) {
    if (depth > kMaxDepth || ++nodes_ > kMaxNodes || offset > d_.size() || d_.size() - offset < kNodeHeaderSize) {
      return std::nullopt;
    }
    const uint32_t count = Get32(d_, offset + 12);
    if (count > (d_.size() - offset - kNodeHeaderSize) / 8) {
      return std::nullopt;
    }
    Node node;
    auto name = String(Get32(d_, offset));
    if (!name) {
      return std::nullopt;
    }
    node.name = std::move(*name);
    for (uint32_t i = 0; i < count; ++i) {
      const size_t p = offset + kNodeHeaderSize + i * 8;
      auto key = String(Get32(d_, p));
      auto value = String(Get32(d_, p + 4));
      if (!key || !value) {
        return std::nullopt;
      }
      node.attributes.emplace_back(std::move(*key), std::move(*value));
    }
    uint32_t child = Get32(d_, offset + 8);
    while (child != 0xFFFFFFFF) {
      uint32_t sibling = 0xFFFFFFFF;
      auto parsed = ParseNode(child, depth + 1, sibling);
      if (!parsed) {
        return std::nullopt;
      }
      node.children.push_back(std::move(*parsed));
      child = sibling;
    }
    next = Get32(d_, offset + 4);
    return node;
  }

 private:
  std::span<const uint8_t> d_;
  size_t nodes_ = 0;
};

bool ParseFloat(std::string_view text, float& out) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool ParseInt(std::string_view text, int& out) {
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

}  // namespace

std::string_view Node::Get(std::string_view key) const {
  for (const auto& [k, v] : attributes) {
    if (k == key) {
      return v;
    }
  }
  return {};
}

std::optional<Node> Parse(std::span<const uint8_t> file) {
  if (file.size() < kRootOffset + kNodeHeaderSize || Get32(file, 0) != kMagic) {
    return std::nullopt;
  }
  Parser parser(file);
  uint32_t next = 0;
  return parser.ParseNode(uint32_t(kRootOffset), 0, next);
}

std::optional<FontTable> ParseFontTable(std::span<const uint8_t> file) {
  auto root = Parse(file);
  if (!root || root->name != "FONT_TABLE") {
    return std::nullopt;
  }
  FontTable table;
  if (!ParseInt(root->Get("height"), table.height)) {
    return std::nullopt;
  }
  for (const Node& node : root->children) {
    if (node.name != "glyph") {
      continue;
    }
    Glyph glyph;
    int code = 0;
    if (!ParseInt(node.Get("num"), code) || code < 0 || !ParseFloat(node.Get("s"), glyph.s) ||
        !ParseFloat(node.Get("t"), glyph.t) || !ParseFloat(node.Get("s2"), glyph.s2) ||
        !ParseFloat(node.Get("t2"), glyph.t2) || !ParseInt(node.Get("width"), glyph.width) ||
        !ParseInt(node.Get("height"), glyph.height)) {
      return std::nullopt;
    }
    glyph.code = uint32_t(code);
    if (glyph.width > 0 && glyph.height > 0 && glyph.s2 > glyph.s && glyph.t2 > glyph.t) {
      table.glyphs.push_back(glyph);
    }
  }
  return table;
}

std::optional<std::vector<FontFile>> ParseFontFiles(std::span<const uint8_t> file) {
  auto root = Parse(file);
  if (!root || root->name != "FONTFILES") {
    return std::nullopt;
  }
  std::vector<FontFile> fonts;
  for (const Node& node : root->children) {
    if (node.name != "font") {
      continue;
    }
    FontFile font{std::string(node.Get("data")), std::string(node.Get("texture")), std::string(node.Get("index")),
                  std::string(node.Get("language"))};
    if (font.data.empty() || font.texture.empty()) {
      return std::nullopt;
    }
    fonts.push_back(std::move(font));
  }
  return fonts;
}

}  // namespace mua::xmlb
