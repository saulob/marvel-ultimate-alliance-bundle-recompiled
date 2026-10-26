#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"

namespace mua {

enum class Upscaler { kOff, kFsr1 };

enum class UpscaleQuality { kNative, kQuality, kBalanced, kPerformance };

struct UpscaleQualityInfo {
  UpscaleQuality quality;
  const char* key;
  const char* label;
  // AMD's FSR 1 modes: output size over render size along each axis.
  double ratio;
};

inline constexpr UpscaleQualityInfo kUpscaleQualities[] = {
    {UpscaleQuality::kNative, "native", "Native", 1.0},
    {UpscaleQuality::kQuality, "quality", "Quality", 1.5},
    {UpscaleQuality::kBalanced, "balanced", "Balanced", 1.7},
    {UpscaleQuality::kPerformance, "performance", "Performance", 2.0},
};

inline constexpr char kUpscalerOff[] = "off";
inline constexpr char kUpscalerFsr1[] = "fsr";

// The game's render targets and resolves keep its back buffer width a multiple of 16 pixels, and resolves work in
// 8-line rows.
inline constexpr int kBackBufferWidthAlignment = 16;
inline constexpr int kBackBufferHeightAlignment = 8;

// FSR 1 runs in the Direct3D 12 presenter (SDK patch 0127); the Vulkan presenter only has bilinear.
#ifdef _WIN32
inline constexpr bool kUpscalerAvailable = true;
#else
inline constexpr bool kUpscalerAvailable = false;
#endif

inline Upscaler ParseUpscaler(std::string_view value) {
  return kUpscalerAvailable && value == kUpscalerFsr1 ? Upscaler::kFsr1 : Upscaler::kOff;
}

inline const UpscaleQualityInfo& QualityInfoFor(UpscaleQuality quality) {
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    if (info.quality == quality) {
      return info;
    }
  }
  return kUpscaleQualities[0];
}

inline UpscaleQuality ParseUpscaleQuality(std::string_view value) {
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    if (value == info.key) {
      return info.quality;
    }
  }
  return UpscaleQuality::kNative;
}

// Without an upscaler every quality draws natively.
inline UpscaleQuality EffectiveQuality(Upscaler upscaler, UpscaleQuality quality) {
  return upscaler == Upscaler::kOff ? UpscaleQuality::kNative : quality;
}

// The present_effect the upscaler needs; none leaves the player's own value alone.
inline std::optional<std::string_view> PresentEffectFor(Upscaler upscaler) {
  if (upscaler == Upscaler::kFsr1) {
    return "fsr";
  }
  return std::nullopt;
}

inline int AlignedDivide(int size, double ratio, int alignment) {
  const int aligned = int(std::lround(double(size) / ratio / alignment)) * alignment;
  return aligned < alignment ? alignment : aligned;
}

// The back buffer the game draws into for a canvas: smaller by the quality's ratio, so the GPU draws fewer pixels and
// FSR 1 brings the picture back to the output size.
inline WindowSize UpscaledBackBuffer(WindowSize canvas, Upscaler upscaler, UpscaleQuality quality) {
  const double ratio = QualityInfoFor(EffectiveQuality(upscaler, quality)).ratio;
  if (ratio == 1.0) {
    return canvas;
  }
  return {AlignedDivide(canvas.width, ratio, kBackBufferWidthAlignment),
          AlignedDivide(canvas.height, ratio, kBackBufferHeightAlignment)};
}

// FSR 1 reads the internal render and writes the window, whose size only the presenter knows.
struct RenderSizes {
  WindowSize guest;     // the back buffer the game draws
  WindowSize internal;  // the back buffer at the Render resolution, what the GPU draws
  WindowSize native;    // the canvas at the Render resolution, what Upscaling Off draws
};

inline RenderSizes RenderSizesFor(AspectRatio aspect, int render_scale, Upscaler upscaler, UpscaleQuality quality) {
  const int scale = render_scale < 1 ? 1 : render_scale;
  const WindowSize canvas = VideoModeSize(aspect);
  const WindowSize back_buffer = UpscaledBackBuffer(canvas, upscaler, quality);
  return {back_buffer,
          {back_buffer.width * scale, back_buffer.height * scale},
          {canvas.width * scale, canvas.height * scale}};
}

inline double UpscaleRatio(const RenderSizes& sizes) {
  return sizes.internal.height > 0 ? double(sizes.native.height) / double(sizes.internal.height) : 1.0;
}

inline std::string SizeText(WindowSize size) {
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

inline std::string UpscalingText(Upscaler upscaler, UpscaleQuality quality) {
  if (upscaler == Upscaler::kOff) {
    return "Off";
  }
  return std::string("AMD FSR 1 ") + QualityInfoFor(quality).label;
}

}  // namespace mua
