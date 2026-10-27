// The Upscaling choice: the back buffer the game draws into for each quality, the internal and native render sizes
// at each Render resolution and aspect ratio, the parsing of the saved values (NIS and CAS from earlier versions mean
// Off) and the presenter effect.
// Run: mua_upscaling_tests

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"
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
  CHECK(ParseUpscaleQuality("ultra_performance") == UpscaleQuality::kNative);
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
    CHECK(!info.present_effect || std::string_view(info.present_effect) == "fsr");
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
        CHECK(EffectiveQuality(Upscaler::kDlss, info.quality) == info.quality);
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
    CHECK(info.scene_only == (info.upscaler == Upscaler::kDlss));
  }
  // The launcher's order: Off, AMD FSR 1, NVIDIA DLSS, which Marvel Ultimate Alliance offers all of; Gold Edition and
  // MUA 2 offer Off and FSR 1.
  CHECK(std::size(kUpscalers) == 3);
  CHECK(kUpscalers[0].upscaler == Upscaler::kOff && kUpscalers[1].upscaler == Upscaler::kFsr1 &&
        kUpscalers[2].upscaler == Upscaler::kDlss);
  CHECK(kAllUpscalers == (UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1) | UpscalerBit(Upscaler::kDlss)));
  CHECK(kFsr1Upscalers == (UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1)));
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
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
