#include "recomp_image.h"

#if MUA_HAS_RECOMPILED_CODE
#include "marvel_ultimate_alliance_init.h"
#endif

namespace mua {
namespace {

RecompiledImage MakeRecompiledImage() {
  RecompiledImage image;
#if MUA_HAS_RECOMPILED_CODE
  image.info = PPCImageConfig;
  image.available = true;
#endif
  if (const GameDefinition* game = FindGame(MUA_RUNTIME_KEY)) {
    image.game = game->id;
  }
  return image;
}

}  // namespace

const RecompiledImage& GetRecompiledImage() {
  static const RecompiledImage image = MakeRecompiledImage();
  return image;
}

}  // namespace mua
