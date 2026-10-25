// Launcher header art: one SVG composition rendered per game in that game's launcher palette. The generated
// PNGs carry the palette they were rendered from, so a changed PaletteOf makes the assets stale here.
// Run: mua_launcher_header_art_tests

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

#include "game_theme.h"
#include "launcher_header_art.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

using mua::GameId;

constexpr GameId kGames[] = {GameId::kMua1, GameId::kMua1Gold, GameId::kMua2};

std::filesystem::path Root() {
  return std::filesystem::path(MUA_SRC_DIR).parent_path();
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string out = text.str();
  if (path.extension() != ".png") {
    std::erase(out, '\r');
  }
  return out;
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::string HexColor(uint32_t rgb) {
  char buffer[8];
  std::snprintf(buffer, sizeof buffer, "%06X", rgb);
  return buffer;
}

uint32_t BigEndian(const std::string& bytes, size_t at) {
  const auto* p = reinterpret_cast<const uint8_t*>(bytes.data()) + at;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

std::filesystem::path ArtPath(GameId game) {
  return Root() / "assets" / "launcher" / mua::HeaderArtFile(game);
}

std::string PngPaletteText(const std::string& png) {
  size_t pos = 8;
  while (pos + 12 <= png.size()) {
    const uint32_t length = BigEndian(png, pos);
    const std::string kind = png.substr(pos + 4, 4);
    if (kind == "tEXt") {
      const std::string data = png.substr(pos + 8, length);
      const std::string key = "mua-palette";
      if (data.compare(0, key.size() + 1, key + std::string(1, '\0')) == 0) {
        return data.substr(key.size() + 1);
      }
    }
    pos += 12 + length;
  }
  return {};
}

enum class Hue { kRed, kGold, kBlue, kOther };

Hue HueOf(uint32_t rgb) {
  const int r = int(rgb >> 16 & 255), g = int(rgb >> 8 & 255), b = int(rgb & 255);
  if (b > r && b > g) {
    return Hue::kBlue;
  }
  if (r > g + 60 && r > b + 60 && g < 100) {
    return Hue::kRed;
  }
  if (r > b + 60 && g > b + 40) {
    return Hue::kGold;
  }
  return Hue::kOther;
}

void TestPalettes() {
  std::printf("each game maps to its colour\n");
  CHECK(HueOf(mua::PaletteOf(GameId::kMua1).primary) == Hue::kRed);
  CHECK(HueOf(mua::PaletteOf(GameId::kMua1Gold).primary) == Hue::kGold);
  CHECK(HueOf(mua::PaletteOf(GameId::kMua2).primary) == Hue::kBlue);
}

void TestAssets() {
  std::printf("generated assets exist, are RGBA and match the palette\n");
  std::set<std::string> files;
  for (const GameId game : kGames) {
    files.insert(mua::HeaderArtFile(game));
    const std::string png = ReadFile(ArtPath(game));
    CHECK(png.size() > 33 && png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0);
    if (png.size() < 33) {
      continue;
    }
    CHECK(BigEndian(png, 16) == 3840);
    CHECK(BigEndian(png, 20) == 432);
    CHECK(png[24] == 8);
    CHECK(png[25] == 6);
    CHECK(PngPaletteText(png) == HexColor(mua::PaletteOf(game).primary));
  }
  CHECK(files.size() == std::size(kGames));
}

void TestSvgSource() {
  std::printf("the canonical SVG is one plain vector composition\n");
  const std::string svg = ReadFile(Root() / "assets" / "launcher" / "header-art.svg");
  CHECK(Has(svg, "viewBox=\"0 0 1600 180\""));
  CHECK(!Has(svg, "<image"));
  CHECK(!Has(svg, "data:"));
  CHECK(!Has(svg, "<text"));
  CHECK(!Has(svg, "<!--"));
  CHECK(!Has(svg, "<script"));
  CHECK(svg.size() < 16 * 1024);
  size_t rects = 0;
  for (size_t at = svg.find("<rect"); at != std::string::npos; at = svg.find("<rect", at + 1)) {
    ++rects;
  }
  CHECK(rects == 1);
  CHECK(Has(svg, "<mask id=\"fade\""));
  CHECK(Has(svg, "<g mask=\"url(#fade)\">"));
  CHECK(Has(svg, "#" + HexColor(mua::PaletteOf(GameId::kMua1).primary)));
}

void TestGenerator() {
  std::printf("the generator derives colours from PaletteOf\n");
  const std::string tool = ReadFile(Root() / "tools" / "make-launcher-header-art.py");
  CHECK(Has(tool, "game_primaries()"));
  CHECK(Has(tool, "header-art.svg"));
  for (const GameId game : {GameId::kMua1Gold, GameId::kMua2}) {
    CHECK(!Has(tool, HexColor(mua::PaletteOf(game).primary)));
  }
}

void TestSelection() {
  std::printf("the selected game picks its own art\n");
  std::set<std::string> names;
  for (const GameId game : kGames) {
    names.insert(mua::HeaderArtFile(game));
  }
  CHECK(names.size() == 3);
  CHECK(std::string(mua::HeaderArtFile(GameId::kMua1)) == "header-art-mua1.png");
  CHECK(std::string(mua::HeaderArtFile(GameId::kMua1Gold)) == "header-art-mua1-gold.png");
  CHECK(std::string(mua::HeaderArtFile(GameId::kMua2)) == "header-art-mua2.png");

  const std::string launcher = ReadFile(Root() / "src" / "launcher.cpp");
  CHECK(Has(launcher, "launcher_->config().branding.background = MakeHeaderPainter(game);"));
  CHECK(Has(launcher, "c.branding.background = MakeHeaderPainter(selected_);"));
}

void TestFailureAndIsolation() {
  std::printf("missing art never blocks the launcher or touches settings\n");
  const std::string launcher = ReadFile(Root() / "src" / "launcher.cpp");
  const size_t begin = launcher.find("trg::HeaderPainter MakeHeaderPainter(GameId game) {");
  const size_t end = launcher.find("trg::TabStrip MakeTabStrip()");
  CHECK(begin != std::string::npos && end != std::string::npos && begin < end);
  const std::string painter = launcher.substr(begin, end - begin);
  CHECK(Has(painter, "if (!art.tried) {"));
  CHECK(Has(painter, "LauncherHeader(ImTextureID{}, kHeaderArtAspect)"));
  CHECK(!Has(painter, "settings_"));
  CHECK(!Has(painter, "Save"));
  CHECK(Has(launcher, "if (art == ImTextureID{}) {"));
  CHECK(Has(launcher, "if (!in) {\n    return nullptr;"));

  const std::string header = ReadFile(Root() / "src" / "launcher_header_art.h");
  CHECK(!Has(header, "settings.h"));
}

void TestBuildWiring() {
  std::printf("the art is staged beside the programs and nothing else uses it\n");
  const std::string cmake = ReadFile(Root() / "CMakeLists.txt");
  for (const GameId game : kGames) {
    CHECK(Has(cmake, std::string("/") + mua::HeaderArtFile(game)));
  }
  CHECK(Has(cmake, "copy_if_different ${MUA_HEADER_ART_FILES}"));
  CHECK(Has(cmake, "/assets/launcher\""));

  for (const auto& entry : std::filesystem::directory_iterator(MUA_SRC_DIR)) {
    const std::string name = entry.path().filename().string();
    if (name == "launcher.cpp" || name == "launcher_header_art.h") {
      continue;
    }
    const std::string text = ReadFile(entry.path());
    CHECK(!Has(text, "header-art"));
    CHECK(!Has(text, "HeaderArtFile"));
  }
}

}  // namespace

int main() {
  TestPalettes();
  TestAssets();
  TestSvgSource();
  TestGenerator();
  TestSelection();
  TestFailureAndIsolation();
  TestBuildWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
