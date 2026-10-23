#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

#include "aspect_ratio.h"
#include "upscaling.h"
#include "window_host.h"

namespace mua {

// The resolution the game draws at: 3360x1440 at 2x in 21:9.
inline std::string RenderResolutionText(int resolution_scale, AspectRatio aspect) {
  const WindowSize size = RenderSize(aspect, resolution_scale);
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// With an upscaler, what the GPU draws before FSR 1 scales it to the window: "1120x480 (AMD FSR 1 Quality)".
inline std::string RenderResolutionText(int resolution_scale, AspectRatio aspect, Upscaler upscaler,
                                        UpscaleQuality quality) {
  if (upscaler == Upscaler::kOff) {
    return RenderResolutionText(resolution_scale, aspect);
  }
  const RenderSizes sizes = RenderSizesFor(aspect, resolution_scale, upscaler, quality);
  return SizeText(sizes.internal) + " (" + UpscalingText(upscaler, quality) + ")";
}

// The game title, then the render resolution and the frame rate when they are shown, separated by " | ".
inline std::string WindowTitleText(std::string_view game_title, std::optional<std::string> resolution,
                                   std::optional<double> fps) {
  std::string title(game_title);
  if (resolution) {
    title += " | " + *resolution;
  }
  if (fps) {
    char text[32];
    const auto end = std::to_chars(text, text + sizeof(text), *fps, std::chars_format::fixed, 2).ptr;
    title += " | FPS: " + std::string(text, end);
  }
  return title;
}

}  // namespace mua
