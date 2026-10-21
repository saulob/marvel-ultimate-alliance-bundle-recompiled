#pragma once

#include <rex/image_info.h>

namespace mua {

// The generated recompiled code is the only source of the PPC image layout and function table. Without it the
// runtime cannot be built, so `available` gates Play.
struct RecompiledImage {
  rex::PPCImageInfo info{};
  bool available = false;
};

const RecompiledImage& GetRecompiledImage();

}  // namespace mua
