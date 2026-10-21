#pragma once

#include <cmath>
#include <string_view>

namespace mua {

enum class AspectRatio { k16x9, k16x10, k21x9, k32x9, k4x3, k5x4 };

// The title renders and queries 720 lines.
inline constexpr int kGuestHeight = 720;

struct AspectRatioInfo {
  AspectRatio aspect;
  const char* key;
  const char* label;
  double ratio;
  int default_width;
  int default_height;
};

inline constexpr AspectRatioInfo kAspectRatios[] = {
    {AspectRatio::k16x9, "16:9", "16:9 Original", 16.0 / 9.0, 1280, 720},
    {AspectRatio::k16x10, "16:10", "16:10", 16.0 / 10.0, 1280, 800},
    {AspectRatio::k21x9, "21:9", "21:9 Ultrawide", 21.0 / 9.0, 1680, 720},
    {AspectRatio::k32x9, "32:9", "32:9 Super Ultrawide", 32.0 / 9.0, 1920, 540},
    {AspectRatio::k4x3, "4:3", "4:3", 4.0 / 3.0, 1024, 768},
    {AspectRatio::k5x4, "5:4", "5:4", 5.0 / 4.0, 1280, 1024},
};

struct WindowSize {
  int width;
  int height;
  AspectRatio aspect;
};

inline constexpr WindowSize kWindowSizes[] = {
    {853, 480, AspectRatio::k16x9},
    {1280, 720, AspectRatio::k16x9},
    {1600, 900, AspectRatio::k16x9},
    {1920, 1080, AspectRatio::k16x9},
    {2560, 1440, AspectRatio::k16x9},
    {1280, 800, AspectRatio::k16x10},
    {1440, 900, AspectRatio::k16x10},
    {1260, 540, AspectRatio::k21x9},
    {1680, 720, AspectRatio::k21x9},
    {2100, 900, AspectRatio::k21x9},
    {1600, 450, AspectRatio::k32x9},
    {1920, 540, AspectRatio::k32x9},
    {1024, 768, AspectRatio::k4x3},
    {1280, 960, AspectRatio::k4x3},
    {1280, 1024, AspectRatio::k5x4},
};

inline const AspectRatioInfo& InfoFor(AspectRatio aspect) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (info.aspect == aspect) {
      return info;
    }
  }
  return kAspectRatios[0];
}

// The formats the title can render. The others stay in the tables for the day it can.
inline constexpr AspectRatio kSupportedAspectRatios[] = {AspectRatio::k16x9};

inline bool IsSupportedAspectRatio(AspectRatio aspect) {
  for (const AspectRatio supported : kSupportedAspectRatios) {
    if (supported == aspect) {
      return true;
    }
  }
  return false;
}

// Unknown, unsupported or missing values keep the original 16:9 picture.
inline AspectRatio ParseAspectRatio(std::string_view key) {
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (key == info.key && IsSupportedAspectRatio(info.aspect)) {
      return info.aspect;
    }
  }
  return AspectRatio::k16x9;
}

inline int GuestWidth(AspectRatio aspect) {
  return int(std::lround(kGuestHeight * InfoFor(aspect).ratio));
}

}  // namespace mua
