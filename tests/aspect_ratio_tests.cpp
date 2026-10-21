// Aspect Ratio: formats, the game-side widths, the supported formats, the setting key and its fallback, and
// the launcher and settings wiring. Run: mua_aspect_ratio_tests

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "aspect_ratio.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestFormats() {
  std::printf("formats, labels and ratios\n");
  CHECK(std::size(mua::kAspectRatios) == 6);
  const char* keys[] = {"16:9", "16:10", "21:9", "32:9", "4:3", "5:4"};
  const char* labels[] = {"16:9 Original", "16:10", "21:9 Ultrawide", "32:9 Super Ultrawide", "4:3", "5:4"};
  const double ratios[] = {16.0 / 9.0, 16.0 / 10.0, 21.0 / 9.0, 32.0 / 9.0, 4.0 / 3.0, 5.0 / 4.0};
  for (std::size_t i = 0; i < 6; ++i) {
    const mua::AspectRatioInfo& info = mua::kAspectRatios[i];
    CHECK(std::string(info.key) == keys[i]);
    CHECK(std::string(info.label) == labels[i]);
    CHECK(info.ratio == ratios[i]);
    CHECK(mua::InfoFor(info.aspect).aspect == info.aspect);
  }
}

void TestSupportedFormats() {
  std::printf("only the formats the title renders are selectable\n");
  CHECK(std::size(mua::kSupportedAspectRatios) == 1);
  CHECK(mua::IsSupportedAspectRatio(mua::AspectRatio::k16x9));
  CHECK(!mua::IsSupportedAspectRatio(mua::AspectRatio::k21x9));
  CHECK(mua::ParseAspectRatio("16:9") == mua::AspectRatio::k16x9);
  CHECK(mua::ParseAspectRatio("21:9") == mua::AspectRatio::k16x9);
  CHECK(mua::ParseAspectRatio("32:9") == mua::AspectRatio::k16x9);
}

void TestParseFallback() {
  std::printf("setting value parsing and fallback\n");
  CHECK(mua::ParseAspectRatio("") == mua::AspectRatio::k16x9);
  CHECK(mua::ParseAspectRatio("auto") == mua::AspectRatio::k16x9);
  CHECK(mua::ParseAspectRatio("16:9 ") == mua::AspectRatio::k16x9);
  CHECK(mua::ParseAspectRatio("2.33") == mua::AspectRatio::k16x9);
}

void TestWindowSizes() {
  std::printf("window sizes\n");
  const mua::WindowSize required[] = {
      {1280, 720, mua::AspectRatio::k16x9},   {1600, 900, mua::AspectRatio::k16x9},
      {1920, 1080, mua::AspectRatio::k16x9},  {2560, 1440, mua::AspectRatio::k16x9},
      {1280, 800, mua::AspectRatio::k16x10},  {1440, 900, mua::AspectRatio::k16x10},
      {1260, 540, mua::AspectRatio::k21x9},   {1680, 720, mua::AspectRatio::k21x9},
      {2100, 900, mua::AspectRatio::k21x9},   {1600, 450, mua::AspectRatio::k32x9},
      {1920, 540, mua::AspectRatio::k32x9},   {1024, 768, mua::AspectRatio::k4x3},
      {1280, 960, mua::AspectRatio::k4x3},    {1280, 1024, mua::AspectRatio::k5x4},
  };
  for (const mua::WindowSize& want : required) {
    bool found = false;
    for (const mua::WindowSize& size : mua::kWindowSizes) {
      found = found || (size.width == want.width && size.height == want.height && size.aspect == want.aspect);
    }
    CHECK(found);
  }
  for (const mua::WindowSize& size : mua::kWindowSizes) {
    const double ratio = double(size.width) / double(size.height);
    CHECK(std::fabs(ratio - mua::InfoFor(size.aspect).ratio) < 0.001);
  }
}

void TestGameWidths() {
  std::printf("game-side widths: 720 lines at every format\n");
  const int guest[] = {1280, 1152, 1680, 2560, 960, 900};
  for (std::size_t i = 0; i < 6; ++i) {
    const mua::AspectRatio aspect = mua::kAspectRatios[i].aspect;
    CHECK(mua::GuestWidth(aspect) == guest[i]);
    CHECK(std::fabs(double(mua::GuestWidth(aspect)) / mua::kGuestHeight - mua::kAspectRatios[i].ratio) < 1e-3);
  }
}

void TestWiring() {
  std::printf("setting and launcher wiring\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(mua_aspect_ratio, \"16:9\""));
  CHECK(Has(settings, "      \"mua_aspect_ratio\",\n") || Has(settings, "      \"mua_aspect_ratio\",\r\n"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "ui.Combo(\"Aspect Ratio\""));
  CHECK(Has(launcher, "\"mua_aspect_ratio\", \"16:9\", aspects"));
  CHECK(Has(launcher, "if (IsSupportedAspectRatio(info.aspect))"));
  CHECK(Has(launcher, "RenderResolutionOptions(SelectedAspectRatio())"));
}

}  // namespace

int main() {
  TestFormats();
  TestSupportedFormats();
  TestParseFallback();
  TestWindowSizes();
  TestGameWidths();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
