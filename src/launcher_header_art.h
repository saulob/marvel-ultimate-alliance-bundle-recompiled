#pragma once

#include "game_definition.h"

namespace mua {

constexpr const char* HeaderArtFile(GameId id) {
  switch (id) {
    case GameId::kMua1Gold:
      return "header-art-mua1-gold.png";
    case GameId::kMua2:
      return "header-art-mua2.png";
    case GameId::kMua1:
      break;
  }
  return "header-art-mua1.png";
}

}  // namespace mua
