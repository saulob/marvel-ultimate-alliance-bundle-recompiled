#pragma once

#include <optional>

#include <rex/image_info.h>

#include "game_definition.h"

namespace mua {

// The module generated for this executable is the only source of the PPC image layout and function table. A game's
// program carries the module of that game; the launcher carries none and has no game.
struct RecompiledImage {
  rex::PPCImageInfo info{};
  bool available = false;
  std::optional<GameId> game;
};

const RecompiledImage& GetRecompiledImage();

}  // namespace mua
