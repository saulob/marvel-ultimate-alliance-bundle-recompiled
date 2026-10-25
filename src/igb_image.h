#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Alchemy .igb with one igImage. Memory blocks follow the objects in reference order, each sized by its descriptor.
namespace mua::igb {

inline constexpr uint32_t kMagic = 0xFADA;
inline constexpr size_t kHeaderSize = 48;

struct Image {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t pixel_format = 0;
  uint32_t components = 0;
  uint32_t bytes_per_row = 0;
  uint32_t image_size = 0;
  // Where the pixels start in the file. Rows run bottom to top, as the title's glyph coordinates expect.
  size_t pixels_offset = 0;
};

std::optional<Image> FindImage(std::span<const uint8_t> file);

std::optional<std::vector<uint8_t>> ReplaceImage(std::span<const uint8_t> file, uint32_t width, uint32_t height,
                                                 std::span<const uint8_t> pixels);

}  // namespace mua::igb
