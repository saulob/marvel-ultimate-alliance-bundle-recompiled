#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"

namespace mua {

enum class Upscaler { kOff, kFsr1, kDlss, kFsr3 };

using UpscalerMask = uint32_t;

constexpr UpscalerMask UpscalerBit(Upscaler upscaler) {
  return UpscalerMask(1) << unsigned(upscaler);
}

inline constexpr UpscalerMask kFsr1Upscalers = UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1);
inline constexpr UpscalerMask kAllUpscalers =
    kFsr1Upscalers | UpscalerBit(Upscaler::kFsr3) | UpscalerBit(Upscaler::kDlss);

inline constexpr char kUpscalerOff[] = "off";
inline constexpr char kUpscalerFsr1[] = "fsr";
inline constexpr char kUpscalerDlss[] = "dlss";
inline constexpr char kUpscalerFsr3[] = "fsr3";

struct UpscalerInfo {
  Upscaler upscaler;
  const char* key;  // the mua_upscaler value
  const char* label;
  const char* name;            // in sentences: "FSR 1"
  const char* present_effect;  // null leaves the player's own present_effect alone
  bool lowers_resolution;      // the Upscaling quality shrinks the game's back buffer
  bool scene_only;             // the Upscaling quality sizes only the 3D scene, which the GPU plugin upscales
};

inline constexpr UpscalerInfo kUpscalers[] = {
    {Upscaler::kOff, kUpscalerOff, "Off", "", nullptr, false, false},
    {Upscaler::kFsr1, kUpscalerFsr1, "AMD FSR 1", "FSR 1", "fsr", true, false},
    // Plain presentation, so FSR 1's sharpening never runs on top of FSR 3.1's.
    {Upscaler::kFsr3, kUpscalerFsr3, "AMD FSR 3.1", "FSR 3.1", "bilinear", false, true},
    {Upscaler::kDlss, kUpscalerDlss, "NVIDIA DLSS", "DLSS", nullptr, false, true},
};

// mua_upscaler values earlier versions offered; they mean Off now.
struct DeprecatedUpscaler {
  const char* key;
  const char* label;
};

inline constexpr DeprecatedUpscaler kDeprecatedUpscalers[] = {
    {"nis", "NVIDIA NIS"},
    {"cas", "AMD CAS"},
};

inline const DeprecatedUpscaler* FindDeprecatedUpscaler(std::string_view value) {
  for (const DeprecatedUpscaler& deprecated : kDeprecatedUpscalers) {
    if (value == deprecated.key) {
      return &deprecated;
    }
  }
  return nullptr;
}

enum class UpscaleQuality { kNative, kQuality, kBalanced, kPerformance, kUltraPerformance };

struct UpscaleQualityInfo {
  UpscaleQuality quality;
  const char* key;
  const char* label;
  // AMD's FSR modes: output size over render size along each axis.
  double ratio;
};

inline constexpr UpscaleQualityInfo kUpscaleQualities[] = {
    {UpscaleQuality::kNative, "native", "Native", 1.0},
    {UpscaleQuality::kQuality, "quality", "Quality", 1.5},
    {UpscaleQuality::kBalanced, "balanced", "Balanced", 1.7},
    {UpscaleQuality::kPerformance, "performance", "Performance", 2.0},
    {UpscaleQuality::kUltraPerformance, "ultra_performance", "Ultra Performance", 3.0},
};

// Only FSR 3.1 offers Ultra Performance.
inline bool OffersQuality(Upscaler upscaler, UpscaleQuality quality) {
  return quality != UpscaleQuality::kUltraPerformance || upscaler == Upscaler::kFsr3;
}

// The game's render targets and resolves keep its back buffer width a multiple of 16 pixels, and resolves work in
// 8-line rows.
inline constexpr int kBackBufferWidthAlignment = 16;
inline constexpr int kBackBufferHeightAlignment = 8;

// FSR 1 (SDK patch 0127) runs in the Direct3D 12 presenter and DLSS (0161) in its command processor; the Vulkan
// presenter only has bilinear.
#ifdef _WIN32
inline constexpr bool kUpscalerAvailable = true;
#else
inline constexpr bool kUpscalerAvailable = false;
#endif

inline const UpscalerInfo& UpscalerInfoFor(Upscaler upscaler) {
  for (const UpscalerInfo& info : kUpscalers) {
    if (info.upscaler == upscaler) {
      return info;
    }
  }
  return kUpscalers[0];
}

// An upscaler the game doesn't offer, or one this version no longer has, is Off.
inline Upscaler ParseUpscaler(std::string_view value, UpscalerMask offered) {
  for (const UpscalerInfo& info : kUpscalers) {
    if (value == info.key) {
      return kUpscalerAvailable && (offered & UpscalerBit(info.upscaler)) ? info.upscaler : Upscaler::kOff;
    }
  }
  return Upscaler::kOff;
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

// Off draws natively at every quality, and a quality the upscaler doesn't offer is its Performance.
inline UpscaleQuality EffectiveQuality(Upscaler upscaler, UpscaleQuality quality) {
  const UpscalerInfo& info = UpscalerInfoFor(upscaler);
  if (!info.lowers_resolution && !info.scene_only) {
    return UpscaleQuality::kNative;
  }
  return OffersQuality(upscaler, quality) ? quality : UpscaleQuality::kPerformance;
}

// DLSS at Native is DLAA.
inline const char* QualityLabel(Upscaler upscaler, UpscaleQuality quality) {
  return upscaler == Upscaler::kDlss && quality == UpscaleQuality::kNative ? "DLAA" : QualityInfoFor(quality).label;
}

// The GPU plugin's d3d12_dlss_mode for a quality.
inline const char* DlssModeFor(UpscaleQuality quality) {
  switch (quality) {
    case UpscaleQuality::kQuality:
      return "quality";
    case UpscaleQuality::kBalanced:
      return "balanced";
    case UpscaleQuality::kPerformance:
      return "performance";
    default:
      return "dlaa";
  }
}

// The GPU plugin's d3d12_fsr3_mode for a quality.
inline const char* Fsr3ModeFor(UpscaleQuality quality) {
  return QualityInfoFor(quality).key;
}

// FSR 3.1's sharpening (RCAS), 0 (off) to 100; AMD's samples use 80.
inline constexpr int kDefaultFsr3Sharpness = 80;

inline int ParseFsr3Sharpness(std::string_view value) {
  int sharpness = -1;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), sharpness);
  const bool valid = error == std::errc() && end == value.data() + value.size() && sharpness >= 0 && sharpness <= 100;
  return valid ? sharpness : kDefaultFsr3Sharpness;
}

// The GPU plugin's render width for a scene upscaler's size: EDRAM tile edges (80 pixels apart in the guest's canvas)
// land on whole render pixels (patches/rexglue-sdk/0161, SnapRenderWidth).
inline int SnapSceneWidth(int width, int canvas_width, int max_width) {
  const int step = canvas_width / std::gcd(canvas_width, 80);
  int snapped = std::max(step, (width + step / 2) / step * step);
  if (max_width && snapped > max_width) {
    snapped -= step;
  }
  return snapped;
}

// The canvas a game draws its 3D scene in: its own scene size, or the video mode's when it has none.
inline WindowSize SceneCanvas(AspectRatio aspect, WindowSize scene_size) {
  return scene_size.width > 0 ? scene_size : VideoModeSize(aspect);
}

// The 3D scene FSR 3.1 draws for an output size: AMD's render size of the quality
// (ffxFsr3UpscalerGetRenderResolutionFromQualityMode), with the GPU plugin's width snap.
inline WindowSize Fsr3SceneSize(WindowSize output, int canvas_width, UpscaleQuality quality) {
  if (quality == UpscaleQuality::kNative) {
    return output;
  }
  const float ratio = float(QualityInfoFor(quality).ratio);
  const int width = int(uint32_t(float(output.width) / ratio));
  const int height = int(uint32_t(float(output.height) / ratio));
  return {SnapSceneWidth(width, canvas_width, output.width), height};
}

// The present_effect the upscaler needs; none leaves the player's own value alone.
inline std::optional<std::string_view> PresentEffectFor(Upscaler upscaler) {
  if (const char* effect = UpscalerInfoFor(upscaler).present_effect) {
    return effect;
  }
  return std::nullopt;
}

inline int AlignedDivide(int size, double ratio, int alignment) {
  const int aligned = int(std::lround(double(size) / ratio / alignment)) * alignment;
  return aligned < alignment ? alignment : aligned;
}

// The back buffer the game draws into for a canvas: smaller by the quality's ratio, so the GPU draws fewer pixels and
// the upscaler brings the picture back to the output size.
inline WindowSize UpscaledBackBuffer(WindowSize canvas, Upscaler upscaler, UpscaleQuality quality) {
  const double ratio =
      UpscalerInfoFor(upscaler).lowers_resolution ? QualityInfoFor(EffectiveQuality(upscaler, quality)).ratio : 1.0;
  if (ratio == 1.0) {
    return canvas;
  }
  return {AlignedDivide(canvas.width, ratio, kBackBufferWidthAlignment),
          AlignedDivide(canvas.height, ratio, kBackBufferHeightAlignment)};
}

// The upscaler reads the internal render and writes the window, whose size only the presenter knows.
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
  const UpscalerInfo& info = UpscalerInfoFor(upscaler);
  if (!info.lowers_resolution && !info.scene_only) {
    return info.label;
  }
  return std::string(info.label) + " " + QualityLabel(upscaler, quality);
}

}  // namespace mua
