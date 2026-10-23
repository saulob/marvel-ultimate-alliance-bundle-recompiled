#pragma once

#include <cstdint>

#include "game_definition.h"

namespace mua {

struct GamePalette {
  uint32_t primary;  // 0xRRGGBB
  uint32_t hover;
  uint32_t border;
};

constexpr GamePalette PaletteOf(GameId id) {
  switch (id) {
    case GameId::kMua1Gold:
      return {0xC89A2B, 0xD7AA3A, 0xB68922};
    case GameId::kMua2:
      return {0x3B6FB6, 0x4A7DC3, 0x315F9C};
    case GameId::kMua1:
      break;
  }
  return {0xD73A3A, 0xE14A4A, 0xC83232};
}

}  // namespace mua
