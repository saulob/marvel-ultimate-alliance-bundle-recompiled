#include "recomp_image.h"

#if MUA_HAS_RECOMPILED_CODE
#include "generated/default/marvel_ultimate_alliance_init.h"
#endif

namespace mua {

const RecompiledImage& GetRecompiledImage() {
#if MUA_HAS_RECOMPILED_CODE
  static const RecompiledImage image{PPCImageConfig, true};
#else
  static const RecompiledImage image{};
#endif
  return image;
}

}  // namespace mua
