#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "aspect_ratio.h"

namespace mua {

enum class Upscaler { kOff, kFsr1, kNis, kCas };

using UpscalerMask = uint32_t;

constexpr UpscalerMask UpscalerBit(Upscaler upscaler) {
  return UpscalerMask(1) << unsigned(upscaler);
}

inline constexpr UpscalerMask kFsr1Upscalers = UpscalerBit(Upscaler::kOff) | UpscalerBit(Upscaler::kFsr1);
inline constexpr UpscalerMask kAllUpscalers =
    kFsr1Upscalers | UpscalerBit(Upscaler::kNis) | UpscalerBit(Upscaler::kCas);

inline constexpr char kUpscalerOff[] = "off";
inline constexpr char kUpscalerFsr1[] = "fsr";
inline constexpr char kUpscalerNis[] = "nis";
inline constexpr char kUpscalerCas[] = "cas";

struct UpscalerInfo {
  Upscaler upscaler;
  const char* key;  // the mua_upscaler value
  const char* label;
  const char* name;            // in sentences: "FSR 1"
  const char* present_effect;  // null leaves the player's own present_effect alone
  bool lowers_resolution;      // the Upscaling quality applies
  const char* sharpness_key;   // the game's sharpness in percent, or null
  const char* sharpness_cvar;  // the presenter's, from 0 to 1
  int sharpness_default;
};

// NIS starts at NVIDIA's default sharpness; CAS at the presenter's, AMD's plain CAS.
inline constexpr UpscalerInfo kUpscalers[] = {
    {Upscaler::kOff, kUpscalerOff, "Off", "", nullptr, false, nullptr, nullptr, 0},
    {Upscaler::kFsr1, kUpscalerFsr1, "AMD FSR 1", "FSR 1", "fsr", true, nullptr, nullptr, 0},
    {Upscaler::kNis, kUpscalerNis, "NVIDIA NIS", "NIS", "nis", true, "mua_nis_sharpness", "present_nis_sharpness", 50},
    {Upscaler::kCas, kUpscalerCas, "AMD CAS", "CAS", "cas", false, "mua_cas_sharpness",
     "present_cas_additional_sharpness", 0},
};

enum class UpscaleQuality { kNative, kQuality, kBalanced, kPerformance };

struct UpscaleQualityInfo {
  UpscaleQuality quality;
  const char* key;
  const char* label;
  // AMD's FSR 1 modes, used for NIS too: output size over render size along each axis.
  double ratio;
};

inline constexpr UpscaleQualityInfo kUpscaleQualities[] = {
    {UpscaleQuality::kNative, "native", "Native", 1.0},
    {UpscaleQuality::kQuality, "quality", "Quality", 1.5},
    {UpscaleQuality::kBalanced, "balanced", "Balanced", 1.7},
    {UpscaleQuality::kPerformance, "performance", "Performance", 2.0},
};

// The game's render targets and resolves keep its back buffer width a multiple of 16 pixels, and resolves work in
// 8-line rows.
inline constexpr int kBackBufferWidthAlignment = 16;
inline constexpr int kBackBufferHeightAlignment = 8;

// FSR 1, CAS (SDK patch 0127) and NIS (0159) run in the Direct3D 12 presenter; the Vulkan presenter only has bilinear.
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

// An upscaler the game doesn't offer is Off.
inline Upscaler ParseUpscaler(std::string_view value, UpscalerMask offered) {
  for (const UpscalerInfo& info : kUpscalers) {
    if (value == info.key) {
      return kUpscalerAvailable && (offered & UpscalerBit(info.upscaler)) ? info.upscaler : Upscaler::kOff;
    }
  }
  return Upscaler::kOff;
}

inline int ParseSharpness(std::string_view value, int fallback) {
  int percent = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), percent);
  return error == std::errc() && end == value.data() + value.size() ? std::clamp(percent, 0, 100) : fallback;
}

// The presenter cvar's value for a sharpness in percent.
inline std::string PresenterSharpness(int percent) {
  return std::to_string(std::clamp(percent, 0, 100) / 100.0);
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

// Off and CAS, which only sharpens, draw natively at every quality.
inline UpscaleQuality EffectiveQuality(Upscaler upscaler, UpscaleQuality quality) {
  return UpscalerInfoFor(upscaler).lowers_resolution ? quality : UpscaleQuality::kNative;
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
  const double ratio = QualityInfoFor(EffectiveQuality(upscaler, quality)).ratio;
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
  if (!info.lowers_resolution) {
    return info.label;
  }
  return std::string(info.label) + " " + QualityInfoFor(quality).label;
}

}  // namespace mua
