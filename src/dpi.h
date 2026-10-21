#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mua {

inline constexpr int kBaseDpi = 96;

// Unusable DPI and scale values fall back to 1 so callers never divide by zero.
inline float DpiScaleFromDpi(std::uint32_t dpi) {
  return dpi ? float(dpi) / float(kBaseDpi) : 1.0f;
}

inline float ValidDpiScale(float scale) {
  return scale > 0.0f ? scale : 1.0f;
}

inline int ScaleLogicalToPhysical(int logical, float scale) {
  return int(std::lround(double(logical) * double(ValidDpiScale(scale))));
}

inline int ScalePhysicalToLogical(int physical, float scale) {
  return int(std::lround(double(physical) / double(ValidDpiScale(scale))));
}

struct DpiWindowSize {
  int width = 0;
  int height = 0;

  bool operator==(const DpiWindowSize&) const = default;
};

// The window size is logical; the work area is in physical screen pixels.
inline DpiWindowSize FitWindowToWorkArea(int width, int height, int work_width, int work_height, float scale,
                                         double max_share) {
  const float valid_scale = ValidDpiScale(scale);
  const int max_width = int(double(work_width) * max_share / double(valid_scale));
  const int max_height = int(double(work_height) * max_share / double(valid_scale));
  return {std::min(width, max_width), std::min(height, max_height)};
}

}  // namespace mua
