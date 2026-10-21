#pragma once

#include <charconv>
#include <optional>
#include <string>

#include "aspect_ratio.h"
#include "window_host.h"

namespace mua {

// The launcher's Render resolution: 3360x1440 at 2x in 21:9. The internal render scale is not part of it.
inline std::string RenderResolutionText(int resolution_scale, AspectRatio aspect) {
  return std::to_string(GuestWidth(aspect) * resolution_scale) + "x" +
         std::to_string(kGuestHeight * resolution_scale);
}

// The game title, then the render resolution and the frame rate when they are shown, separated by " | ".
inline std::string WindowTitleText(std::optional<std::string> resolution, std::optional<double> fps) {
  std::string title = kWindowTitle;
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
