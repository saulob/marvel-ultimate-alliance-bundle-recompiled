// The Upscaling choice: the back buffer the game draws into for each quality, the internal and native render sizes
// at each Render resolution and aspect ratio, the parsing of the saved values (NIS and CAS from earlier versions mean
// Off), the presenter effect, and AMD FSR 3.1's modes, scene sizes, sharpness and availability.
// Run: mua_upscaling_tests

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "aspect_ratio.h"
#include "fsr3_support.h"
#include "upscaling.h"

using namespace mua;

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

constexpr UpscaleQuality kReduced[] = {UpscaleQuality::kQuality, UpscaleQuality::kBalanced,
                                       UpscaleQuality::kPerformance};

uint64_t Pixels(WindowSize size) {
  return uint64_t(size.width) * uint64_t(size.height);
}

bool Smaller(WindowSize a, WindowSize b) {
  return a.width < b.width && a.height < b.height;
}

double AspectError(WindowSize size, WindowSize reference) {
  const double aspect = double(size.width) / size.height;
  const double expected = double(reference.width) / reference.height;
  return std::fabs(aspect - expected) / expected;
}

bool Equal(WindowSize a, WindowSize b) {
  return a.width == b.width && a.height == b.height;
}

void TestSixteenNine() {
  std::printf("16:9 internal sizes at Original and 2x\n");
  const auto internal = [](int scale, UpscaleQuality quality) {
    return RenderSizesFor(AspectRatio::k16x9, scale, Upscaler::kFsr1, quality).internal;
  };
  CHECK(Equal(internal(1, UpscaleQuality::kNative), {1280, 720}));
  CHECK(Equal(internal(1, UpscaleQuality::kQuality), {848, 480}));
  CHECK(Equal(internal(1, UpscaleQuality::kBalanced), {752, 424}));
  CHECK(Equal(internal(1, UpscaleQuality::kPerformance), {640, 360}));
  CHECK(Equal(internal(2, UpscaleQuality::kNative), {2560, 1440}));
  CHECK(Equal(internal(2, UpscaleQuality::kQuality), {1696, 960}));
  CHECK(Equal(internal(2, UpscaleQuality::kBalanced), {1504, 848}));
  CHECK(Equal(internal(2, UpscaleQuality::kPerformance), {1280, 720}));
}

void TestQualitiesDiffer() {
  std::printf("each quality draws fewer pixels than the one before, at every scale and aspect ratio\n");
  for (const AspectRatioInfo& aspect : kAspectRatios) {
    for (int scale = 1; scale <= 4; ++scale) {
      WindowSize previous = RenderSizesFor(aspect.aspect, scale, Upscaler::kFsr1, UpscaleQuality::kNative).internal;
      for (const UpscaleQuality quality : kReduced) {
        const RenderSizes sizes = RenderSizesFor(aspect.aspect, scale, Upscaler::kFsr1, quality);
        CHECK(Smaller(sizes.internal, previous));
        CHECK(Smaller(sizes.internal, sizes.native));
        previous = sizes.internal;
      }
    }
  }
}

void TestAlignmentAndAspect() {
  std::printf("back buffers stay aligned and keep the format's shape\n");
  for (const AspectRatioInfo& aspect : kAspectRatios) {
    const WindowSize canvas = VideoModeSize(aspect.aspect);
    for (const UpscaleQualityInfo& info : kUpscaleQualities) {
      if (!OffersQuality(Upscaler::kFsr1, info.quality)) {
        continue;
      }
      const WindowSize back_buffer = UpscaledBackBuffer(canvas, Upscaler::kFsr1, info.quality);
      CHECK(back_buffer.width % kBackBufferWidthAlignment == 0);
      CHECK(back_buffer.height % kBackBufferHeightAlignment == 0);
      CHECK(AspectError(back_buffer, canvas) <= 0.01);
      CHECK(std::fabs(double(canvas.height) / back_buffer.height - info.ratio) <= 0.02);
      for (int scale = 1; scale <= 4; ++scale) {
        const RenderSizes sizes = RenderSizesFor(aspect.aspect, scale, Upscaler::kFsr1, info.quality);
        CHECK(Equal(sizes.guest, back_buffer));
        CHECK(Equal(sizes.internal, {back_buffer.width * scale, back_buffer.height * scale}));
        CHECK(AspectError(sizes.internal, sizes.native) <= 0.01);
      }
    }
  }
}

void TestUltrawide() {
  std::printf("21:9 and 32:9 internal sizes\n");
  const auto internal = [](AspectRatio aspect, int scale, UpscaleQuality quality) {
    return RenderSizesFor(aspect, scale, Upscaler::kFsr1, quality).internal;
  };
  CHECK(Equal(internal(AspectRatio::k21x9, 1, UpscaleQuality::kQuality), {1120, 480}));
  CHECK(Equal(internal(AspectRatio::k21x9, 1, UpscaleQuality::kBalanced), {992, 424}));
  CHECK(Equal(internal(AspectRatio::k21x9, 1, UpscaleQuality::kPerformance), {848, 360}));
  CHECK(Equal(internal(AspectRatio::k21x9, 2, UpscaleQuality::kQuality), {2240, 960}));
  CHECK(Equal(RenderSizesFor(AspectRatio::k21x9, 2, Upscaler::kFsr1, UpscaleQuality::kQuality).native,
              {3360, 1440}));
  CHECK(Equal(internal(AspectRatio::k32x9, 1, UpscaleQuality::kQuality), {1712, 480}));
  CHECK(Equal(internal(AspectRatio::k32x9, 1, UpscaleQuality::kPerformance), {1280, 360}));
  CHECK(Equal(internal(AspectRatio::k32x9, 4, UpscaleQuality::kBalanced), {6016, 1696}));
}

void TestNativeAndOff() {
  std::printf("Off and Native draw at the Render resolution, and FSR never changes the Render resolution\n");
  for (const AspectRatioInfo& aspect : kAspectRatios) {
    const WindowSize canvas = VideoModeSize(aspect.aspect);
    for (int scale = 1; scale <= 4; ++scale) {
      const WindowSize output = RenderSize(aspect.aspect, scale);
      for (const UpscaleQualityInfo& info : kUpscaleQualities) {
        const RenderSizes off = RenderSizesFor(aspect.aspect, scale, Upscaler::kOff, info.quality);
        CHECK(Equal(off.internal, output) && Equal(off.native, output));
        CHECK(Equal(RenderSizesFor(aspect.aspect, scale, Upscaler::kFsr1, info.quality).native, output));
        CHECK(Equal(UpscaledBackBuffer(canvas, Upscaler::kOff, info.quality), canvas));
      }
      const RenderSizes native = RenderSizesFor(aspect.aspect, scale, Upscaler::kFsr1, UpscaleQuality::kNative);
      CHECK(Equal(native.internal, output));
      CHECK(UpscaleRatio(native) == 1.0);
    }
  }
  CHECK(EffectiveQuality(Upscaler::kOff, UpscaleQuality::kPerformance) == UpscaleQuality::kNative);
  CHECK(EffectiveQuality(Upscaler::kFsr1, UpscaleQuality::kBalanced) == UpscaleQuality::kBalanced);
  CHECK(Equal(RenderSizesFor(AspectRatio::k16x9, 0, Upscaler::kFsr1, UpscaleQuality::kNative).native, {1280, 720}));
}

void TestPixelSavings() {
  std::printf("the reduced qualities cut the pixels drawn\n");
  const WindowSize output = RenderSize(AspectRatio::k16x9, 2);
  const auto pixels = [](UpscaleQuality quality) {
    return Pixels(RenderSizesFor(AspectRatio::k16x9, 2, Upscaler::kFsr1, quality).internal);
  };
  CHECK(pixels(UpscaleQuality::kQuality) * 2 < Pixels(output));
  CHECK(pixels(UpscaleQuality::kBalanced) * 2.8 < double(Pixels(output)));
  CHECK(pixels(UpscaleQuality::kPerformance) * 4 == Pixels(output));
}

void TestText() {
  std::printf("labels and ratio\n");
  const RenderSizes sizes = RenderSizesFor(AspectRatio::k16x9, 2, Upscaler::kFsr1, UpscaleQuality::kQuality);
  CHECK(SizeText(sizes.guest) == "848x480" && SizeText(sizes.internal) == "1696x960" &&
        SizeText(sizes.native) == "2560x1440");
  CHECK(std::lround(UpscaleRatio(sizes) * 100) == 150);
  CHECK(std::lround(UpscaleRatio(RenderSizesFor(AspectRatio::k16x9, 1, Upscaler::kFsr1, UpscaleQuality::kBalanced)) *
                    100) == 170);
  CHECK(UpscalingText(Upscaler::kOff, UpscaleQuality::kQuality) == "Off");
  CHECK(UpscalingText(Upscaler::kFsr1, UpscaleQuality::kBalanced) == "AMD FSR 1 Balanced");
}

void TestParsing() {
  std::printf("saved values and presenter effect\n");
  const auto available = [](Upscaler upscaler) { return kUpscalerAvailable ? upscaler : Upscaler::kOff; };
  CHECK(ParseUpscaler("fsr", kFsr1Upscalers) == available(Upscaler::kFsr1));
  CHECK(ParseUpscaler("fsr", kAllUpscalers) == available(Upscaler::kFsr1));
  CHECK(ParseUpscaler("off", kAllUpscalers) == Upscaler::kOff);
  CHECK(ParseUpscaler("", kAllUpscalers) == Upscaler::kOff);
  CHECK(ParseUpscaler("dlss", kAllUpscalers) == available(Upscaler::kDlss));
  CHECK(ParseUpscaler("dlss", kFsr1Upscalers) == Upscaler::kOff);
  CHECK(ParseUpscaler("dlss_quality", kAllUpscalers) == Upscaler::kOff);
  CHECK(ParseUpscaleQuality("") == UpscaleQuality::kNative);
  CHECK(ParseUpscaleQuality("ultra") == UpscaleQuality::kNative);
  CHECK(ParseUpscaleQuality("ultra_performance") == UpscaleQuality::kUltraPerformance);
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    CHECK(ParseUpscaleQuality(info.key) == info.quality);
    CHECK(&QualityInfoFor(info.quality) == &info);
  }
  for (const UpscalerInfo& info : kUpscalers) {
    CHECK(&UpscalerInfoFor(info.upscaler) == &info);
  }
  CHECK(PresentEffectFor(Upscaler::kFsr1) == std::optional<std::string_view>("fsr"));
  CHECK(!PresentEffectFor(Upscaler::kOff));
}

void TestDeprecatedUpscalers() {
  std::printf("NVIDIA NIS and AMD CAS from earlier versions are Off, and no longer offered\n");
  for (const char* value : {"nis", "cas"}) {
    CHECK(ParseUpscaler(value, kAllUpscalers) == Upscaler::kOff);
    CHECK(ParseUpscaler(value, kFsr1Upscalers) == Upscaler::kOff);
    CHECK(FindDeprecatedUpscaler(value) != nullptr);
  }
  CHECK(std::string_view(FindDeprecatedUpscaler("nis")->label) == "NVIDIA NIS");
  CHECK(std::string_view(FindDeprecatedUpscaler("cas")->label) == "AMD CAS");
  for (const char* value : {"off", "fsr", "dlss", "", "NIS"}) {
    CHECK(FindDeprecatedUpscaler(value) == nullptr);
  }
  for (const UpscalerInfo& info : kUpscalers) {
    CHECK(!FindDeprecatedUpscaler(info.key));
    CHECK(!info.present_effect || std::string_view(info.present_effect) == "fsr" ||
          std::string_view(info.present_effect) == "bilinear");
  }
}

void TestDlss() {
  std::printf("DLSS keeps the back buffer and sizes only the 3D scene by its mode\n");
  for (const AspectRatioInfo& aspect : kAspectRatios) {
    const WindowSize canvas = VideoModeSize(aspect.aspect);
    for (int scale = 1; scale <= 4; ++scale) {
      for (const UpscaleQualityInfo& info : kUpscaleQualities) {
        const RenderSizes dlss = RenderSizesFor(aspect.aspect, scale, Upscaler::kDlss, info.quality);
        CHECK(Equal(dlss.internal, RenderSize(aspect.aspect, scale)) && Equal(dlss.internal, dlss.native));
        CHECK(Equal(UpscaledBackBuffer(canvas, Upscaler::kDlss, info.quality), canvas));
        CHECK(EffectiveQuality(Upscaler::kDlss, info.quality) ==
              (info.quality == UpscaleQuality::kUltraPerformance ? UpscaleQuality::kPerformance : info.quality));
      }
    }
  }
  CHECK(std::string_view(DlssModeFor(UpscaleQuality::kNative)) == "dlaa");
  CHECK(std::string_view(DlssModeFor(UpscaleQuality::kQuality)) == "quality");
  CHECK(std::string_view(DlssModeFor(UpscaleQuality::kBalanced)) == "balanced");
  CHECK(std::string_view(DlssModeFor(UpscaleQuality::kPerformance)) == "performance");
  CHECK(UpscalingText(Upscaler::kDlss, UpscaleQuality::kNative) == "NVIDIA DLSS DLAA");
  CHECK(UpscalingText(Upscaler::kDlss, UpscaleQuality::kBalanced) == "NVIDIA DLSS Balanced");
  CHECK(std::string_view(QualityLabel(Upscaler::kFsr1, UpscaleQuality::kNative)) == "Native");
  CHECK(std::string_view(QualityLabel(Upscaler::kDlss, UpscaleQuality::kPerformance)) == "Performance");
  CHECK(!PresentEffectFor(Upscaler::kDlss));
  const UpscalerInfo& dlss = UpscalerInfoFor(Upscaler::kDlss);
  CHECK(dlss.scene_only && !dlss.lowers_resolution);
  CHECK(std::string_view(dlss.key) == "dlss" && std::string_view(dlss.label) == "NVIDIA DLSS");
  for (const UpscalerInfo& info : kUpscalers) {
    CHECK(info.scene_only == (info.upscaler == Upscaler::kDlss || info.upscaler == Upscaler::kFsr3));
  }
  // The launcher's order: Off, AMD FSR 1, AMD FSR 3.1, NVIDIA DLSS, which Marvel Ultimate Alliance offers all of; Gold
  // Edition and MUA 2 offer Off and FSR 1.
  CHECK(std::size(kUpscalers) == 4);
  CHECK(kUpscalers[0].upscaler == Upscaler::kOff && kUpscalers[1].upscaler == Upscaler::kFsr1 &&
        kUpscalers[2].upscaler == Upscaler::kFsr3 && kUpscalers[3].upscaler == Upscaler::kDlss);
  CHECK(kAllUpscalers == (UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1) | UpscalerBit(Upscaler::kFsr3) |
                          UpscalerBit(Upscaler::kDlss)));
  CHECK(kFsr1Upscalers == (UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1)));
}

void TestFsr3Choice() {
  std::printf("AMD FSR 3.1: offered with Marvel Ultimate Alliance's choices only, saved as fsr3, FSR 1 stays fsr\n");
  const auto available = [](Upscaler upscaler) { return kUpscalerAvailable ? upscaler : Upscaler::kOff; };
  const UpscalerInfo& fsr3 = UpscalerInfoFor(Upscaler::kFsr3);
  CHECK(std::string_view(fsr3.key) == "fsr3" && std::string_view(kUpscalerFsr3) == "fsr3");
  CHECK(std::string_view(fsr3.label) == "AMD FSR 3.1" && std::string_view(fsr3.name) == "FSR 3.1");
  CHECK(fsr3.scene_only && !fsr3.lowers_resolution);
  CHECK(ParseUpscaler("fsr3", kAllUpscalers) == available(Upscaler::kFsr3));
  CHECK(ParseUpscaler("fsr3", kFsr1Upscalers) == Upscaler::kOff);
  CHECK(ParseUpscaler("fsr", kAllUpscalers) == available(Upscaler::kFsr1));
  CHECK(ParseUpscaler("fsr", kFsr1Upscalers) == available(Upscaler::kFsr1));
  CHECK(ParseUpscaler("fsr31", kAllUpscalers) == Upscaler::kOff);
  CHECK(!FindDeprecatedUpscaler("fsr3"));
  // Plain presentation: FSR 1's RCAS never sharpens FSR 3.1's output again.
  CHECK(PresentEffectFor(Upscaler::kFsr3) == std::optional<std::string_view>("bilinear"));
  CHECK(UpscalingText(Upscaler::kFsr3, UpscaleQuality::kPerformance) == "AMD FSR 3.1 Performance");
  CHECK(UpscalingText(Upscaler::kFsr3, UpscaleQuality::kUltraPerformance) == "AMD FSR 3.1 Ultra Performance");
  CHECK(std::string_view(QualityLabel(Upscaler::kFsr3, UpscaleQuality::kNative)) == "Native");
}

void TestFsr3Modes() {
  std::printf("AMD FSR 3.1's five modes, AMD's ratios, and Ultra Performance only for it\n");
  constexpr std::pair<UpscaleQuality, const char*> kModes[] = {
      {UpscaleQuality::kNative, "native"},
      {UpscaleQuality::kQuality, "quality"},
      {UpscaleQuality::kBalanced, "balanced"},
      {UpscaleQuality::kPerformance, "performance"},
      {UpscaleQuality::kUltraPerformance, "ultra_performance"},
  };
  for (const auto& [quality, mode] : kModes) {
    CHECK(std::string_view(Fsr3ModeFor(quality)) == mode);
    CHECK(OffersQuality(Upscaler::kFsr3, quality));
    CHECK(EffectiveQuality(Upscaler::kFsr3, quality) == quality);
  }
  CHECK(QualityInfoFor(UpscaleQuality::kNative).ratio == 1.0 && QualityInfoFor(UpscaleQuality::kQuality).ratio == 1.5 &&
        QualityInfoFor(UpscaleQuality::kBalanced).ratio == 1.7 &&
        QualityInfoFor(UpscaleQuality::kPerformance).ratio == 2.0 &&
        QualityInfoFor(UpscaleQuality::kUltraPerformance).ratio == 3.0);
  for (const Upscaler upscaler : {Upscaler::kFsr1, Upscaler::kDlss}) {
    CHECK(!OffersQuality(upscaler, UpscaleQuality::kUltraPerformance));
    CHECK(EffectiveQuality(upscaler, UpscaleQuality::kUltraPerformance) == UpscaleQuality::kPerformance);
  }
  CHECK(EffectiveQuality(Upscaler::kOff, UpscaleQuality::kUltraPerformance) == UpscaleQuality::kNative);
  // FSR 1 never draws below its Performance size.
  CHECK(Equal(RenderSizesFor(AspectRatio::k16x9, 2, Upscaler::kFsr1, UpscaleQuality::kUltraPerformance).internal,
              {1280, 720}));
}

void TestFsr3SceneSizes() {
  std::printf("AMD FSR 3.1's 3D scene at each Render resolution and mode; output, HUD and menus stay full size\n");
  struct Expected {
    int scale;
    WindowSize scene[5];  // Native, Quality, Balanced, Performance, Ultra Performance
  };
  // AMD's floor(output / ratio), the width snapped to whole render pixels at EDRAM tile edges (16 at 1280 wide).
  constexpr Expected kExpected[] = {
      {1, {{1280, 720}, {848, 480}, {752, 423}, {640, 360}, {432, 240}}},
      {2, {{2560, 1440}, {1712, 960}, {1504, 847}, {1280, 720}, {848, 480}}},
      {3, {{3840, 2160}, {2560, 1440}, {2256, 1270}, {1920, 1080}, {1280, 720}}},
      {4, {{5120, 2880}, {3408, 1920}, {3008, 1694}, {2560, 1440}, {1712, 960}}},
  };
  for (const Expected& expected : kExpected) {
    const WindowSize output = RenderSize(AspectRatio::k16x9, expected.scale);
    for (size_t i = 0; i < std::size(kUpscaleQualities); ++i) {
      const UpscaleQuality quality = kUpscaleQualities[i].quality;
      const WindowSize scene = Fsr3SceneSize(output, CanvasWidth(AspectRatio::k16x9), quality);
      CHECK(Equal(scene, expected.scene[i]));
      if (!Equal(scene, expected.scene[i])) {
        std::printf("    %dx %s: %s\n", expected.scale, kUpscaleQualities[i].key, SizeText(scene).c_str());
      }
      CHECK(scene.width <= output.width && scene.height <= output.height);
      CHECK(scene.width % 16 == 0);
      // The Render resolution is the output: the game's back buffer, the HUD and the menus keep it.
      const RenderSizes sizes = RenderSizesFor(AspectRatio::k16x9, expected.scale, Upscaler::kFsr3, quality);
      CHECK(Equal(sizes.internal, output) && Equal(sizes.native, output));
      CHECK(Equal(sizes.guest, VideoModeSize(AspectRatio::k16x9)));
    }
  }
  // 4K Performance, the first manual test: 1920x1080 to 3840x2160, a quarter of the pixels.
  const WindowSize output = RenderSize(AspectRatio::k16x9, 3);
  const WindowSize scene = Fsr3SceneSize(output, CanvasWidth(AspectRatio::k16x9), UpscaleQuality::kPerformance);
  CHECK(Pixels(scene) * 4 == Pixels(output));
  for (const AspectRatioInfo& aspect : kAspectRatios) {
    for (int scale = 1; scale <= 4; ++scale) {
      const WindowSize out = RenderSize(aspect.aspect, scale);
      WindowSize previous = out;
      for (const UpscaleQualityInfo& info : kUpscaleQualities) {
        const WindowSize size = Fsr3SceneSize(out, CanvasWidth(aspect.aspect), info.quality);
        CHECK(size.width <= previous.width && size.height <= previous.height && size.width > 0);
        previous = size;
      }
    }
  }
}

void TestFsr3Sharpness() {
  std::printf("AMD FSR 3.1's sharpness: 0 to 100, 80 by default\n");
  CHECK(kDefaultFsr3Sharpness == 80);
  CHECK(ParseFsr3Sharpness("0") == 0);
  CHECK(ParseFsr3Sharpness("35") == 35);
  CHECK(ParseFsr3Sharpness("100") == 100);
  for (const char* invalid : {"", "101", "-1", "abc", "50%", "1000000000000"}) {
    CHECK(ParseFsr3Sharpness(invalid) == kDefaultFsr3Sharpness);
  }
}

void TestFsr3Support() {
  std::printf("AMD FSR 3.1 needs AMD's DLL and the rasterizer-ordered views path, not a particular graphics card\n");
  CHECK(Fsr3SupportFor(true, true, false).available);
  const Fsr3Support missing = Fsr3SupportFor(true, false, false);
  CHECK(!missing.available && missing.reason.find(kFidelityFxLibraryFile) != std::string::npos);
  const Fsr3Support host = Fsr3SupportFor(true, true, true);
  CHECK(!host.available && host.reason.find("rasterizer-ordered") != std::string::npos);
  CHECK(!Fsr3SupportFor(false, true, false).available);
  CHECK(std::string_view(kFidelityFxLibraryFile) == "amd_fidelityfx_dx12.dll");
  CHECK(std::string_view(kFidelityFxSdkVersion) == "1.1.4");
}

}  // namespace

int main() {
  TestSixteenNine();
  TestQualitiesDiffer();
  TestAlignmentAndAspect();
  TestUltrawide();
  TestNativeAndOff();
  TestPixelSavings();
  TestText();
  TestParsing();
  TestDeprecatedUpscalers();
  TestDlss();
  TestFsr3Choice();
  TestFsr3Modes();
  TestFsr3SceneSizes();
  TestFsr3Sharpness();
  TestFsr3Support();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
