#include "igb_image.h"

#include <cstring>
#include <string>
#include <string_view>

namespace mua::igb {
namespace {

// igImage fields by slot, as the meta-object table of the games' files lists them.
constexpr uint16_t kWidthSlot = 2;
constexpr uint16_t kHeightSlot = 3;
constexpr uint16_t kComponentsSlot = 4;
constexpr uint16_t kFormatSlot = 6;
constexpr uint16_t kImageSizeSlot = 12;
constexpr uint16_t kImageMemorySlot = 13;
constexpr uint16_t kBytesPerRowSlot = 19;

// Header words.
constexpr size_t kDirectorySize = 0;
constexpr size_t kMetaObjectSize = 2;
constexpr size_t kMetaObjectCount = 3;
constexpr size_t kObjectSize = 4;
constexpr size_t kObjectCount = 5;
constexpr size_t kMemorySize = 6;
constexpr size_t kMemoryCount = 7;
constexpr size_t kMetaFieldSize = 8;
constexpr size_t kMetaFieldCount = 9;
constexpr size_t kMagicWord = 10;

uint32_t Get32(std::span<const uint8_t> d, size_t p) {
  return uint32_t(d[p]) | (uint32_t(d[p + 1]) << 8) | (uint32_t(d[p + 2]) << 16) | (uint32_t(d[p + 3]) << 24);
}

uint16_t Get16(std::span<const uint8_t> d, size_t p) {
  return uint16_t(d[p] | (d[p + 1] << 8));
}

void Put32(std::vector<uint8_t>& d, size_t p, uint32_t v) {
  d[p] = uint8_t(v);
  d[p + 1] = uint8_t(v >> 8);
  d[p + 2] = uint8_t(v >> 16);
  d[p + 3] = uint8_t(v >> 24);
}

struct Field {
  uint16_t type;
  uint16_t slot;
  uint16_t size;
};

struct MetaObject {
  std::string name;
  std::vector<Field> fields;
};

struct Record {
  uint32_t meta;
  size_t offset;
  size_t size;
};

struct Layout {
  uint32_t header[12] = {};
  std::vector<std::string> meta_fields;
  std::vector<MetaObject> meta_objects;
  std::vector<Record> descriptors;
  std::vector<uint16_t> index;
  std::vector<Record> objects;
  size_t memory_offset = 0;
  // Per reference: the memory block's offset in the file, or 0 for an object.
  std::vector<size_t> memory_blocks;
};

class Reader {
 public:
  explicit Reader(std::span<const uint8_t> d) : d_(d) {}

  bool Has(size_t p, size_t n) const { return p <= d_.size() && n <= d_.size() - p; }
  // A section that starts with its own byte size.
  bool SkipSized(size_t& p) const {
    if (!Has(p, 4)) {
      return false;
    }
    const size_t size = Get32(d_, p);
    if (size < 4 || !Has(p, size)) {
      return false;
    }
    p += size;
    return true;
  }

 private:
  std::span<const uint8_t> d_;
};

std::string_view Name(std::span<const uint8_t> d, size_t p, size_t n) {
  std::string_view text(reinterpret_cast<const char*>(d.data() + p), n);
  return text.substr(0, text.find('\0'));
}

bool IsMeta(const Layout& layout, uint32_t meta, std::string_view name) {
  return meta < layout.meta_objects.size() && layout.meta_objects[meta].name == name;
}

std::optional<Layout> Parse(std::span<const uint8_t> d) {
  Reader r(d);
  Layout layout;
  if (!r.Has(0, kHeaderSize)) {
    return std::nullopt;
  }
  for (size_t i = 0; i < 12; ++i) {
    layout.header[i] = Get32(d, i * 4);
  }
  const uint32_t* h = layout.header;
  if (h[kMagicWord] != kMagic) {
    return std::nullopt;
  }
  size_t p = kHeaderSize;
  if (!r.SkipSized(p)) {
    return std::nullopt;
  }

  // Meta-fields: (name length, major, minor) per field type, then the names.
  if (!r.Has(p, h[kMetaFieldSize]) || size_t(h[kMetaFieldCount]) * 12 > h[kMetaFieldSize]) {
    return std::nullopt;
  }
  size_t q = p + size_t(h[kMetaFieldCount]) * 12;
  for (uint32_t i = 0; i < h[kMetaFieldCount]; ++i) {
    const size_t n = Get32(d, p + i * 12);
    if (q + n > p + h[kMetaFieldSize]) {
      return std::nullopt;
    }
    layout.meta_fields.emplace_back(Name(d, q, n));
    q += n;
  }
  p += h[kMetaFieldSize];
  if (!r.SkipSized(p)) {
    return std::nullopt;
  }

  // Meta-objects: (name length, major, minor, field count, parent, slot count), then each name and its fields.
  const size_t meta_end = p + h[kMetaObjectSize];
  if (!r.Has(p, h[kMetaObjectSize]) || size_t(h[kMetaObjectCount]) * 24 > h[kMetaObjectSize]) {
    return std::nullopt;
  }
  q = p + size_t(h[kMetaObjectCount]) * 24;
  for (uint32_t i = 0; i < h[kMetaObjectCount]; ++i) {
    const size_t n = Get32(d, p + i * 24);
    const size_t fields = Get32(d, p + i * 24 + 12);
    if (q + n + fields * 6 > meta_end) {
      return std::nullopt;
    }
    MetaObject meta{std::string(Name(d, q, n)), {}};
    q += n;
    for (size_t f = 0; f < fields; ++f, q += 6) {
      meta.fields.push_back({Get16(d, q), Get16(d, q + 2), Get16(d, q + 4)});
    }
    layout.meta_objects.push_back(std::move(meta));
  }
  if (q != meta_end) {
    return std::nullopt;
  }
  p = meta_end;
  if (!r.SkipSized(p) || !r.SkipSized(p)) {
    return std::nullopt;
  }

  // Directory: (meta-object, record size, fields...).
  const size_t dir_end = p + h[kDirectorySize];
  if (!r.Has(p, h[kDirectorySize])) {
    return std::nullopt;
  }
  while (p < dir_end) {
    if (!r.Has(p, 8)) {
      return std::nullopt;
    }
    const uint32_t meta = Get32(d, p);
    const size_t size = Get32(d, p + 4);
    if (size < 8 || p + size > dir_end) {
      return std::nullopt;
    }
    layout.descriptors.push_back({meta, p, size});
    p += size;
  }

  // Reference index: (byte size, count, descriptor per reference).
  if (!r.Has(p, 8)) {
    return std::nullopt;
  }
  const size_t index_size = Get32(d, p);
  const size_t index_count = Get32(d, p + 4);
  if (index_count * 2 + 8 > index_size || !r.Has(p, index_size)) {
    return std::nullopt;
  }
  for (size_t i = 0; i < index_count; ++i) {
    const uint16_t descriptor = Get16(d, p + 8 + i * 2);
    if (descriptor >= layout.descriptors.size()) {
      return std::nullopt;
    }
    layout.index.push_back(descriptor);
  }
  p += index_size + 4;

  // Objects: (meta-object, record size, fields...).
  const size_t objects_end = p + h[kObjectSize];
  if (!r.Has(p, h[kObjectSize])) {
    return std::nullopt;
  }
  while (p < objects_end) {
    if (!r.Has(p, 8)) {
      return std::nullopt;
    }
    const uint32_t meta = Get32(d, p);
    const size_t size = Get32(d, p + 4);
    if (size < 8 || p + size > objects_end || meta >= layout.meta_objects.size()) {
      return std::nullopt;
    }
    layout.objects.push_back({meta, p, size});
    p += size;
  }
  if (layout.objects.size() != h[kObjectCount]) {
    return std::nullopt;
  }

  // Memory blocks, in reference order; each takes its descriptor's size.
  layout.memory_offset = p;
  if (!r.Has(p, h[kMemorySize]) || p + h[kMemorySize] != d.size()) {
    return std::nullopt;
  }
  size_t objects = 0;
  size_t blocks = 0;
  for (uint16_t descriptor : layout.index) {
    const Record& entry = layout.descriptors[descriptor];
    if (IsMeta(layout, entry.meta, "igMemoryDirEntry")) {
      if (entry.size < 16) {
        return std::nullopt;
      }
      const size_t size = Get32(d, entry.offset + 12);
      if (p + size > d.size()) {
        return std::nullopt;
      }
      layout.memory_blocks.push_back(p);
      p += size;
      ++blocks;
    } else if (IsMeta(layout, entry.meta, "igObjectDirEntry")) {
      layout.memory_blocks.push_back(0);
      ++objects;
    } else {
      return std::nullopt;
    }
  }
  if (p != d.size() || objects != layout.objects.size() || blocks != h[kMemoryCount]) {
    return std::nullopt;
  }
  return layout;
}

struct ImageFields {
  size_t object = 0;
  size_t width = 0;
  size_t height = 0;
  size_t components = 0;
  size_t format = 0;
  size_t image_size = 0;
  size_t memory = 0;
  size_t bytes_per_row = 0;
};

// File offsets of the igImage fields this code reads or writes, checked against their meta-field types.
std::optional<ImageFields> LocateImageFields(const Layout& layout) {
  std::optional<ImageFields> found;
  for (size_t i = 0; i < layout.objects.size(); ++i) {
    const Record& object = layout.objects[i];
    if (!IsMeta(layout, object.meta, "igImage")) {
      continue;
    }
    if (found) {
      return std::nullopt;
    }
    ImageFields fields;
    fields.object = i;
    size_t p = object.offset + 8;
    for (const Field& field : layout.meta_objects[object.meta].fields) {
      if (field.type >= layout.meta_fields.size()) {
        return std::nullopt;
      }
      const std::string& type = layout.meta_fields[field.type];
      const size_t width = field.size < 4 ? 4 : field.size;
      auto take = [&](uint16_t slot, std::string_view expected, size_t& out) {
        if (field.slot == slot) {
          if (type != expected || field.size != 4) {
            return false;
          }
          out = p;
        }
        return true;
      };
      if (!take(kWidthSlot, "igUnsignedIntMetaField", fields.width) ||
          !take(kHeightSlot, "igUnsignedIntMetaField", fields.height) ||
          !take(kComponentsSlot, "igUnsignedIntMetaField", fields.components) ||
          !take(kFormatSlot, "igEnumMetaField", fields.format) ||
          !take(kImageSizeSlot, "igIntMetaField", fields.image_size) ||
          !take(kImageMemorySlot, "igMemoryRefMetaField", fields.memory) ||
          !take(kBytesPerRowSlot, "igIntMetaField", fields.bytes_per_row)) {
        return std::nullopt;
      }
      p += width;
    }
    if (p != object.offset + object.size || !fields.width || !fields.height || !fields.components ||
        !fields.format || !fields.image_size || !fields.memory || !fields.bytes_per_row) {
      return std::nullopt;
    }
    found = fields;
  }
  return found;
}

struct Located {
  Layout layout;
  ImageFields fields;
  Image image;
  size_t reference = 0;
};

std::optional<Located> Locate(std::span<const uint8_t> d) {
  auto layout = Parse(d);
  if (!layout) {
    return std::nullopt;
  }
  auto fields = LocateImageFields(*layout);
  if (!fields) {
    return std::nullopt;
  }
  Image image;
  image.width = Get32(d, fields->width);
  image.height = Get32(d, fields->height);
  image.components = Get32(d, fields->components);
  image.pixel_format = Get32(d, fields->format);
  image.image_size = Get32(d, fields->image_size);
  image.bytes_per_row = Get32(d, fields->bytes_per_row);
  const uint32_t reference = Get32(d, fields->memory);
  if (reference >= layout->index.size() || layout->memory_blocks[reference] == 0 || image.components == 0 ||
      uint64_t(image.width) * image.components != image.bytes_per_row ||
      uint64_t(image.bytes_per_row) * image.height != image.image_size) {
    return std::nullopt;
  }
  const uint16_t descriptor = layout->index[reference];
  size_t users = 0;
  for (uint16_t other : layout->index) {
    users += other == descriptor ? 1 : 0;
  }
  if (users != 1 || Get32(d, layout->descriptors[descriptor].offset + 12) != image.image_size) {
    return std::nullopt;
  }
  image.pixels_offset = layout->memory_blocks[reference];
  return Located{std::move(*layout), *fields, image, reference};
}

}  // namespace

std::optional<Image> FindImage(std::span<const uint8_t> file) {
  auto located = Locate(file);
  if (!located) {
    return std::nullopt;
  }
  return located->image;
}

std::optional<std::vector<uint8_t>> ReplaceImage(std::span<const uint8_t> file, uint32_t width, uint32_t height,
                                                 std::span<const uint8_t> pixels) {
  auto located = Locate(file);
  if (!located || width == 0 || height == 0) {
    return std::nullopt;
  }
  const Image& old = located->image;
  const uint64_t bytes_per_row = uint64_t(width) * old.components;
  const uint64_t image_size = bytes_per_row * height;
  if (pixels.size() != image_size || image_size > 0x7FFFFFFF) {
    return std::nullopt;
  }
  const uint64_t memory_size = uint64_t(located->layout.header[kMemorySize]) - old.image_size + image_size;
  if (memory_size > 0xFFFFFFFF) {
    return std::nullopt;
  }

  std::vector<uint8_t> out;
  out.reserve(file.size() - old.image_size + pixels.size());
  out.insert(out.end(), file.begin(), file.begin() + old.pixels_offset);
  out.insert(out.end(), pixels.begin(), pixels.end());
  out.insert(out.end(), file.begin() + old.pixels_offset + old.image_size, file.end());

  const ImageFields& f = located->fields;
  Put32(out, kMemorySize * 4, uint32_t(memory_size));
  Put32(out, f.width, width);
  Put32(out, f.height, height);
  Put32(out, f.image_size, uint32_t(image_size));
  Put32(out, f.bytes_per_row, uint32_t(bytes_per_row));
  Put32(out, located->layout.descriptors[located->layout.index[located->reference]].offset + 12, uint32_t(image_size));
  return out;
}

}  // namespace mua::igb
