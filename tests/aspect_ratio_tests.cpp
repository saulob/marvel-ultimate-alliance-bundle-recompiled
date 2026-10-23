// Aspect Ratio: the format table, the canvas and video mode sizes, the Window size choices and their repair, the guest
// hook maths, the per-game setting and the launcher wiring. No runtime and no game files. Run: mua_aspect_ratio_tests

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>

#include "aspect_ratio.h"

using namespace mua;
namespace fs = std::filesystem;

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

const fs::path kSrcDir = MUA_SRC_DIR;

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestTable() {
  std::printf("table\n");
  CHECK(std::size(kAspectRatios) == 6);
  const char* keys[] = {"16:9", "16:10", "21:9", "32:9", "4:3", "5:4"};
  const char* labels[] = {"16:9 Original", "16:10", "21:9 Ultrawide", "32:9 Super Ultrawide", "4:3", "5:4"};
  for (size_t i = 0; i < std::size(keys); ++i) {
    CHECK(std::string_view(kAspectRatios[i].key) == keys[i]);
    CHECK(std::string_view(kAspectRatios[i].label) == labels[i]);
    CHECK(ParseAspectRatio(keys[i]) == kAspectRatios[i].aspect);
    CHECK(InfoFor(kAspectRatios[i].aspect).key == kAspectRatios[i].key);
  }
  for (const char* odd : {"", "16:9 ", "21x9", "garbage", "true", "false", "0", "auto", "2.33"}) {
    CHECK(ParseAspectRatio(odd) == AspectRatio::k16x9);
  }
}

void TestCanvas() {
  std::printf("canvas and video mode\n");
  struct Row {
    AspectRatio aspect;
    int width;
  };
  const Row rows[] = {{AspectRatio::k16x9, 1280}, {AspectRatio::k16x10, 1152}, {AspectRatio::k21x9, 1680},
                      {AspectRatio::k32x9, 2560}, {AspectRatio::k4x3, 960},     {AspectRatio::k5x4, 896}};
  for (const Row& row : rows) {
    const AspectRatioInfo& info = InfoFor(row.aspect);
    CHECK(CanvasWidth(row.aspect) == row.width);
    CHECK(CanvasWidth(row.aspect) % kCanvasWidthStep == 0);
    const double exact = double(kCanvasHeight) * info.ratio_x / info.ratio_y;
    CHECK(std::abs(CanvasWidth(row.aspect) - exact) <= kCanvasWidthStep / 2.0);
    CHECK(std::abs(CanvasAspect(row.aspect) * info.ratio_y / info.ratio_x - 1.0) < 0.005);

    const WindowSize mode = VideoModeSize(row.aspect);
    CHECK(mode.width >= 640 && mode.width <= 0x0FFF && mode.height >= 480 && mode.height <= 0x0FFF);
    CHECK(mode.width == CanvasWidth(row.aspect) && mode.height == kCanvasHeight);
    // The title reads 1280x1024 as a 5:4 monitor and takes another path.
    CHECK(!(mode.width == 1280 && mode.height == 1024));
    CHECK(mode.height >= 720);
  }
  CHECK(CanvasWidth(AspectRatio::k16x9) == kHdBackBufferWidth && kCanvasHeight == kHdBackBufferHeight);
}

void TestRenderSize() {
  std::printf("render resolution size follows aspect and scale\n");
  struct Row {
    AspectRatio aspect;
    int scale, width, height;
  };
  const Row rows[] = {
      {AspectRatio::k16x9, 1, 1280, 720},  {AspectRatio::k16x9, 2, 2560, 1440}, {AspectRatio::k16x9, 4, 5120, 2880},
      {AspectRatio::k16x10, 1, 1152, 720}, {AspectRatio::k21x9, 1, 1680, 720},  {AspectRatio::k21x9, 2, 3360, 1440},
      {AspectRatio::k21x9, 4, 6720, 2880}, {AspectRatio::k32x9, 3, 7680, 2160}, {AspectRatio::k4x3, 2, 1920, 1440},
      {AspectRatio::k5x4, 4, 3584, 2880},
  };
  for (const Row& row : rows) {
    const WindowSize size = RenderSize(row.aspect, row.scale);
    CHECK(size.width == row.width && size.height == row.height);
  }
}

void TestWindowSizes() {
  std::printf("window sizes\n");
  for (const AspectRatioInfo& info : kAspectRatios) {
    int listed = 0;
    bool default_listed = false;
    for (const AspectWindowSize& size : kWindowSizes) {
      if (size.aspect != info.aspect) {
        CHECK(!WindowMatchesAspect({size.width, size.height}, info.aspect));
        continue;
      }
      ++listed;
      CHECK(WindowMatchesAspect({size.width, size.height}, info.aspect));
      default_listed =
          default_listed || (size.width == info.default_window.width && size.height == info.default_window.height);
    }
    CHECK(listed >= 2);
    CHECK(default_listed);
  }
  const AspectWindowSize earlier[] = {{853, 480, AspectRatio::k16x9},
                                      {1280, 720, AspectRatio::k16x9},
                                      {1600, 900, AspectRatio::k16x9},
                                      {1920, 1080, AspectRatio::k16x9},
                                      {2560, 1440, AspectRatio::k16x9}};
  for (size_t i = 0; i < std::size(earlier); ++i) {
    CHECK(kWindowSizes[i].width == earlier[i].width && kWindowSizes[i].height == earlier[i].height &&
          kWindowSizes[i].aspect == AspectRatio::k16x9);
  }
  CHECK(WindowMatchesAspect({1366, 768}, AspectRatio::k16x9));
  CHECK(!WindowMatchesAspect({1366, 768}, AspectRatio::k16x10));
  CHECK(!WindowMatchesAspect({0, 0}, AspectRatio::k16x9));
  CHECK(!WindowMatchesAspect({1280, -720}, AspectRatio::k16x9));
}

void TestWindowSizeRepair() {
  std::printf("window size repair\n");
  const auto fits_all = [](WindowSize) { return true; };
  const auto fits_none = [](WindowSize) { return false; };
  const auto fits_small = [](WindowSize size) { return size.width <= 1700; };

  CHECK(WindowSizeForAspect(AspectRatio::k21x9, {2100, 900}, fits_all).width == 2100);
  const WindowSize custom = WindowSizeForAspect(AspectRatio::k16x9, {1366, 768}, fits_all);
  CHECK(custom.width == 1366 && custom.height == 768);

  for (const AspectRatioInfo& info : kAspectRatios) {
    const WindowSize chosen = WindowSizeForAspect(info.aspect, {1280, 720 + 1000}, fits_all);
    CHECK(chosen.width == info.default_window.width && chosen.height == info.default_window.height);
  }
  CHECK(WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, fits_all).width == 1680);
  CHECK(WindowSizeForAspect(AspectRatio::k16x9, {1680, 720}, fits_all).width == 1280);

  for (const WindowSize bad : {WindowSize{0, 0}, WindowSize{-1, -1}, WindowSize{1280, 0}, WindowSize{0, 720}}) {
    const WindowSize chosen = WindowSizeForAspect(AspectRatio::k16x9, bad, fits_all);
    CHECK(chosen.width == 1280 && chosen.height == 720);
  }

  const WindowSize largest = WindowSizeForAspect(AspectRatio::k32x9, {1280, 720}, fits_small);
  CHECK(largest.width == 1600 && largest.height == 450);
  const WindowSize ultrawide = WindowSizeForAspect(AspectRatio::k21x9, {1280, 720}, [](WindowSize s) {
    return s.width < 1680;
  });
  CHECK(ultrawide.width == 1260 && ultrawide.height == 540);

  for (const AspectRatioInfo& info : kAspectRatios) {
    const WindowSize chosen = WindowSizeForAspect(info.aspect, {640, 100}, fits_none);
    CHECK(chosen.width > 0 && chosen.height > 0 && WindowMatchesAspect(chosen, info.aspect));
  }
}

void TestHookMaths() {
  std::printf("guest hook maths\n");
  for (const AspectRatioInfo& info : kAspectRatios) {
    CHECK(BackBufferWidthFor(info.aspect, kHdBackBufferWidth, kHdBackBufferHeight) == CanvasWidth(info.aspect));
    // The SD size and any other size are the title's own.
    CHECK(BackBufferWidthFor(info.aspect, 640, 480) == 640);
    CHECK(BackBufferWidthFor(info.aspect, kHdBackBufferWidth, 1024) == kHdBackBufferWidth);
    CHECK(BackBufferWidthFor(info.aspect, 1920, 1080) == 1920);

    const double display = DisplayAspectFor(info.aspect, kGameAspect);
    CHECK(display == (info.aspect == AspectRatio::k16x9 ? kGameAspect : CanvasAspect(info.aspect)));
    CHECK(DisplayAspectFor(info.aspect, 4.0 / 3.0) == 4.0 / 3.0);
    // The back buffer and the display aspect describe the same picture.
    const double picture = double(BackBufferWidthFor(info.aspect, 1280, 720)) / kCanvasHeight;
    CHECK(std::abs(picture / DisplayAspectFor(info.aspect, kGameAspect) - 1.0) < 1e-6);
  }
  CHECK(kGameAspect == double(float(16.0 / 9.0)));
}

void TestSources() {
  std::printf("setting, launcher and hook wiring\n");
  const std::string settings = ReadText(kSrcDir / "settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_STRING(mua_aspect_ratio, \"16:9\""));
  CHECK(Has(settings, "kGameScopedKeys[] = {\"mua_frame_rate\", \"mua_aspect_ratio\"}"));
  CHECK(Has(settings, "void ApplyAspectRatio()"));
  CHECK(Has(settings, "REXLOG_INFO(\"Aspect ratio: {} ({}x{})\""));

  const std::string app = ReadText(kSrcDir / "marvel_ultimate_alliance_app.h");
  const size_t pin = app.find("mua::PinGuestVideoMode();");
  const size_t apply = app.find("mua::ApplyAspectRatio();");
  CHECK(pin != std::string::npos && apply != std::string::npos && pin < apply);

  const std::string launcher = ReadText(kSrcDir / "launcher.cpp");
  const size_t window = launcher.find("ui.Row(\"Window size\"");
  const size_t monitor = launcher.find("ui.Row(\"Monitor\"");
  const size_t aspect = launcher.find("ui.Combo(\"Aspect Ratio\"");
  const size_t vsync = launcher.find("ui.Row(\"VSync\"");
  CHECK(window < monitor && monitor < aspect && aspect < vsync);
  CHECK(Has(launcher, "Sets the game's target rendering format and the Window size choices."));
  CHECK(Has(launcher, "\"mua_aspect_ratio\", \"16:9\", AspectRatioOptions()"));
  CHECK(Has(launcher, "RenderResolutionOptions(SelectedAspectRatio())"));
  CHECK(Has(launcher, "WindowSizeForAspect(SelectedAspectRatio(), current"));
  for (const char* retired : {"present_letterbox", "Letterbox", "letterbox", "Stretch"}) {
    CHECK(!Has(launcher, retired));
    CHECK(!Has(settings, retired));
  }

  const std::string hooks = ReadText(kSrcDir / "aspect_render.cpp");
  CHECK(Has(hooks, "void mua_aspect_back_buffer(PPCRegister& width, PPCRegister& height)"));
  CHECK(Has(hooks, "void mua_aspect_display_aspect(PPCRegister& game_aspect)"));
  CHECK(Has(hooks, "mua::BackBufferWidthFor("));
  CHECK(Has(hooks, "mua::DisplayAspectFor("));
  CHECK(!Has(hooks, "gw2"));
  CHECK(!Has(hooks, "DEBUG_SAULO"));
  CHECK(Has(ReadText(kSrcDir / ".." / "CMakeLists.txt"), "src/aspect_render.cpp"));
}

// The three games hook the same two places of their device creation, each at its own address.
void TestGuestSites() {
  std::printf("guest sites per game\n");
  struct Site {
    const char* game;
    const char* back_buffer;
    const char* display_aspect;
  };
  const Site sites[] = {{"mua1", "0x8243DFA8", "0x8243E02C"},
                        {"mua1-gold", "0x82455C30", "0x82455CB4"},
                        {"mua2", "0x8252D4EC", "0x8252D55C"}};
  std::set<std::string> addresses;
  for (const Site& site : sites) {
    const std::string overrides = ReadText(kSrcDir / ".." / "config" / site.game / "overrides.toml");
    const std::string back_buffer = std::string("address = ") + site.back_buffer +
                                    "\nname = \"mua_aspect_back_buffer\"\nregisters = [\"r11\", \"r10\"]";
    const std::string display = std::string("address = ") + site.display_aspect +
                                "\nname = \"mua_aspect_display_aspect\"\nregisters = [\"f13\"]";
    std::string normalized;
    for (const char c : overrides) {
      if (c != '\r') {
        normalized += c;
      }
    }
    CHECK(Has(normalized, back_buffer.c_str()));
    CHECK(Has(normalized, display.c_str()));
    addresses.insert(site.back_buffer);
    addresses.insert(site.display_aspect);
  }
  CHECK(addresses.size() == 6);

#ifdef MUA_GENERATED_ROOT
  // The generated module of each game holds the instructions the sites sit on.
  const char* back_buffer_hook = "mua_aspect_back_buffer(ctx.r11, ctx.r10);";
  const char* display_hook = "mua_aspect_display_aspect(ctx.f13);";
  for (const Site& site : sites) {
    const fs::path dir = fs::path(MUA_GENERATED_ROOT) / site.game;
    if (!fs::exists(dir)) {
      continue;
    }
    bool back_buffer = false;
    bool display = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() != ".cpp") {
        continue;
      }
      const std::string text = ReadText(entry.path());
      back_buffer = back_buffer || Has(text, back_buffer_hook);
      display = display || Has(text, display_hook);
    }
    if (back_buffer || display) {
      CHECK(back_buffer);
      CHECK(display);
    }
  }
#endif
}

}  // namespace

int main() {
  TestTable();
  TestCanvas();
  TestRenderSize();
  TestWindowSizes();
  TestWindowSizeRepair();
  TestHookMaths();
  TestSources();
  TestGuestSites();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
