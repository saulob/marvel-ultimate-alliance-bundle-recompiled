// HD Text: ZIP overlay, IGB rewrite, font tables, atlas rebuild, plan, launcher row and setting; the real MUA1 and
// Gold archives when game\ holds them. No runtime. Run: mua_hd_text_tests

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "font_atlas_hd.h"
#include "hd_text.h"
#include "igb_image.h"
#include "updater/third_party/miniz/miniz.h"
#include "xmlb.h"
#include "zip_overlay.h"

using namespace mua;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

const fs::path kSrcDir = MUA_SRC_DIR;
const fs::path kRepoDir = kSrcDir.parent_path();

using Bytes = std::vector<uint8_t>;

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

void Put32(Bytes& d, size_t p, uint32_t v) {
  if (d.size() < p + 4) {
    d.resize(p + 4);
  }
  d[p] = uint8_t(v);
  d[p + 1] = uint8_t(v >> 8);
  d[p + 2] = uint8_t(v >> 16);
  d[p + 3] = uint8_t(v >> 24);
}

void Append32(Bytes& d, uint32_t v) {
  Put32(d, d.size(), v);
}

void Append16(Bytes& d, uint16_t v) {
  d.push_back(uint8_t(v));
  d.push_back(uint8_t(v >> 8));
}

uint32_t Get32(const Bytes& d, size_t p) {
  return uint32_t(d[p]) | (uint32_t(d[p + 1]) << 8) | (uint32_t(d[p + 2]) << 16) | (uint32_t(d[p + 3]) << 24);
}

Bytes Text(std::string_view text) {
  return Bytes(text.begin(), text.end());
}

zip::ReadAt ReaderFor(const Bytes& data) {
  return [&data](uint64_t offset, std::span<uint8_t> out) {
    if (offset > data.size() || out.size() > data.size() - offset) {
      return false;
    }
    std::memcpy(out.data(), data.data() + offset, out.size());
    return true;
  };
}

// ---- builders -------------------------------------------------------------------------------------------------------

struct ZipFile {
  std::string name;
  Bytes data;
  bool store = false;
};

// A ZIP written by miniz, optionally ending with the bare end signature the games' archives carry after their record.
Bytes BuildZip(const std::vector<ZipFile>& files, bool trailing_signature) {
  mz_zip_archive zip{};
  mz_zip_writer_init_heap(&zip, 0, 0);
  for (const ZipFile& file : files) {
    mz_zip_writer_add_mem(&zip, file.name.c_str(), file.data.data(), file.data.size(),
                          file.store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL);
  }
  void* buffer = nullptr;
  size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
  Bytes out(static_cast<uint8_t*>(buffer), static_cast<uint8_t*>(buffer) + size);
  mz_zip_writer_end(&zip);
  if (trailing_signature) {
    Append32(out, zip::kEndOfCentralDirectorySignature);
  }
  return out;
}

struct XNode {
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  std::vector<XNode> kids;
};

Bytes BuildXmlb(const XNode& root) {
  std::map<const XNode*, uint32_t> offsets;
  uint32_t p = 8;
  std::function<void(const XNode&)> place = [&](const XNode& n) {
    offsets[&n] = p;
    p += 16 + 8 * uint32_t(n.attrs.size());
    for (const XNode& k : n.kids) {
      place(k);
    }
  };
  place(root);
  Bytes out(p, 0);
  Put32(out, 0, 0x11B1);
  Put32(out, 4, 1);
  auto str = [&](const std::string& s) {
    const uint32_t o = uint32_t(out.size());
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
    return o;
  };
  std::function<void(const XNode&, uint32_t)> write = [&](const XNode& n, uint32_t next) {
    const uint32_t o = offsets[&n];
    Put32(out, o, str(n.name));
    Put32(out, o + 4, next);
    Put32(out, o + 8, n.kids.empty() ? 0xFFFFFFFF : offsets[&n.kids[0]]);
    Put32(out, o + 12, uint32_t(n.attrs.size()));
    for (size_t i = 0; i < n.attrs.size(); ++i) {
      Put32(out, o + 16 + 8 * uint32_t(i), str(n.attrs[i].first));
      Put32(out, o + 20 + 8 * uint32_t(i), str(n.attrs[i].second));
    }
    for (size_t i = 0; i < n.kids.size(); ++i) {
      write(n.kids[i], i + 1 < n.kids.size() ? offsets[&n.kids[i + 1]] : 0xFFFFFFFF);
    }
  };
  write(root, 0xFFFFFFFF);
  return out;
}

// An .igb laid out like the games' font atlases: one igImage object whose pixels are the last of two memory blocks.
// `shared_image_descriptor` makes the small block use the image's memory descriptor too.
Bytes BuildIgb(uint32_t width, uint32_t height, const Bytes& pixels, bool shared_image_descriptor = false) {
  const std::vector<std::string> meta_fields = {"igIntMetaField",       "igUnsignedIntMetaField", "igEnumMetaField",
                                                "igMemoryRefMetaField", "igStringMetaField",      "igBoolMetaField",
                                                "igObjectRefMetaField"};
  enum { kInt, kUInt, kEnum, kMemRef, kString, kBool, kObjRef };
  struct F {
    uint16_t type, slot, size;
  };
  struct M {
    std::string name;
    std::vector<F> fields;
  };
  const std::vector<M> metas = {
      {"igObjectDirEntry", {{kString, 2, 4}, {kInt, 11, 4}, {kInt, 12, 4}}},
      {"igMemoryDirEntry", {{kString, 2, 4}, {kInt, 7, 4}, {kInt, 10, 4}, {kBool, 11, 1}, {kInt, 12, 4}, {kInt, 13, 4}}},
      {"igImage",
       {{kUInt, 2, 4},    {kUInt, 3, 4},  {kUInt, 4, 4},   {kUInt, 5, 4},  {kEnum, 6, 4},  {kUInt, 7, 4},
        {kUInt, 8, 4},    {kUInt, 9, 4},  {kUInt, 10, 4},  {kEnum, 11, 4}, {kInt, 12, 4},  {kMemRef, 13, 4},
        {kMemRef, 14, 4}, {kBool, 15, 1}, {kUInt, 16, 4},  {kObjRef, 17, 4}, {kUInt, 18, 4}, {kInt, 19, 4},
        {kBool, 20, 1},   {kUInt, 21, 4}, {kString, 22, 4}}},
  };
  const uint32_t image_size = width * height * 4;
  Bytes d(48, 0);
  // Strings.
  const std::string png = "u:\\font.png";
  Append32(d, 8 + 4 + uint32_t(png.size() + 1));
  Append32(d, 1);
  Append32(d, uint32_t(png.size() + 1));
  d.insert(d.end(), png.begin(), png.end());
  d.push_back(0);
  // Meta-fields.
  const size_t mf_start = d.size();
  for (const std::string& name : meta_fields) {
    Append32(d, uint32_t(name.size() + 1));
    Append32(d, 1);
    Append32(d, 0);
  }
  for (const std::string& name : meta_fields) {
    d.insert(d.end(), name.begin(), name.end());
    d.push_back(0);
  }
  const uint32_t mf_size = uint32_t(d.size() - mf_start);
  Append32(d, 4);  // alignment types
  // Meta-objects.
  const size_t mo_start = d.size();
  for (const M& m : metas) {
    Append32(d, uint32_t(m.name.size() + 1));
    Append32(d, 1);
    Append32(d, 0);
    Append32(d, uint32_t(m.fields.size()));
    Append32(d, 0xFFFFFFFF);
    Append32(d, 23);
  }
  for (const M& m : metas) {
    d.insert(d.end(), m.name.begin(), m.name.end());
    d.push_back(0);
    for (const F& f : m.fields) {
      Append16(d, f.type);
      Append16(d, f.slot);
      Append16(d, f.size);
    }
  }
  const uint32_t mo_size = uint32_t(d.size() - mo_start);
  Append32(d, 4);  // system pool
  Append32(d, 4);  // pool names
  // Directory: the image object, a 4-byte block, the image block.
  const size_t dir_start = d.size();
  for (uint32_t v : {0u, 20u, 0u, 2u, 0u}) {
    Append32(d, v);
  }
  for (uint32_t v : {1u, 32u, 0u, 4u, 1u, 1u, 0xFFFFFFFFu, 0xFFFFFFFFu}) {
    Append32(d, v);
  }
  for (uint32_t v : {1u, 32u, 0u, image_size, 0u, 1u, 1u, 0xFFFFFFFFu}) {
    Append32(d, v);
  }
  const uint32_t dir_size = uint32_t(d.size() - dir_start);
  // Reference index: object, small block, image block.
  Append32(d, 8 + 3 * 2);
  Append32(d, 3);
  Append16(d, 0);
  Append16(d, shared_image_descriptor ? 2 : 1);
  Append16(d, 2);
  Append32(d, 12);
  // The igImage object.
  const size_t obj_start = d.size();
  Append32(d, 2);
  Append32(d, 8 + 21 * 4);
  const uint32_t values[] = {width, height, 4, 1, 100, 8, 8, 8, 8, 7, image_size, 2, 0xFFFFFFFF, 1, 0, 0xFFFFFFFF, 0,
                             width * 4, 0, 0, 0};
  for (uint32_t v : values) {
    Append32(d, v);
  }
  const uint32_t obj_size = uint32_t(d.size() - obj_start);
  // Memory: the small block, then the pixels.
  const uint32_t small_size = shared_image_descriptor ? image_size : 4;
  for (uint32_t i = 0; i < small_size; ++i) {
    d.push_back(uint8_t(7 + i));
  }
  d.insert(d.end(), pixels.begin(), pixels.end());
  const uint32_t header[12] = {dir_size, 3, mo_size, 3, obj_size, 1, small_size + image_size, 2, mf_size,
                               uint32_t(meta_fields.size()), 0xFADA, 0xF0000008};
  for (int i = 0; i < 12; ++i) {
    Put32(d, size_t(i) * 4, header[i]);
  }
  return d;
}

// A top-down RGBA atlas.
struct Canvas {
  uint32_t w, h;
  Bytes rgba;
  Canvas(uint32_t width, uint32_t height, std::array<uint8_t, 4> fill) : w(width), h(height), rgba(size_t(width) * height * 4) {
    for (size_t i = 0; i < rgba.size(); i += 4) {
      std::memcpy(&rgba[i], fill.data(), 4);
    }
  }
  void Set(uint32_t x, uint32_t y, std::array<uint8_t, 4> c) { std::memcpy(&rgba[(size_t(y) * w + x) * 4], c.data(), 4); }
  // White box with a one-texel outline in `outline`.
  void Glyph(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, std::array<uint8_t, 4> outline) {
    for (uint32_t y = y0; y < y1; ++y) {
      for (uint32_t x = x0; x < x1; ++x) {
        const bool edge = x == x0 || y == y0 || x == x1 - 1 || y == y1 - 1;
        Set(x, y, edge ? outline : std::array<uint8_t, 4>{255, 255, 255, 255});
      }
    }
  }
  // A face-button-like disc: grey rim, green disc.
  void Button(uint32_t x0, uint32_t y0) {
    for (uint32_t y = 0; y < 24; ++y) {
      for (uint32_t x = 0; x < 24; ++x) {
        const float r = std::hypot(x + 0.5f - 12.5f, y + 0.5f - 12.0f);
        if (r < 9.0f) {
          Set(x0 + x, y0 + y, {100, 160, 60, 255});
        } else if (r < 10.7f) {
          Set(x0 + x, y0 + y, {200, 200, 205, 255});
        }
      }
    }
  }
  font_hd::Atlas Atlas() const { return {w, h, rgba}; }
  Bytes BottomUp() const { return font_hd::ToBottomUp(Atlas()); }
};

XNode FontTableNode(int height, const std::vector<std::tuple<int, float, float, float, float, int, int>>& glyphs) {
  XNode table{"FONT_TABLE", {{"ascender", "10"}, {"descender", "3"}, {"height", std::to_string(height)}, {"pointsize", "12"}}, {}};
  table.kids.push_back({"glyph", {{"baseline", "0"}, {"height", "0"}, {"horizadvance", "0"}, {"horizoffset", "0"},
                                  {"num", "0"}, {"s", "0"}, {"s2", "0"}, {"t", "0"}, {"t2", "0"}, {"width", "0"}}, {}});
  for (const auto& [code, s, t, s2, t2, w, h] : glyphs) {
    auto f = [](float v) {
      char text[32];
      std::snprintf(text, sizeof(text), "%.9g", v);
      return std::string(text);
    };
    table.kids.push_back({"glyph",
                          {{"baseline", "8"}, {"height", std::to_string(h)}, {"horizadvance", "10"},
                           {"horizoffset", "0"}, {"num", std::to_string(code)}, {"s", f(s)}, {"s2", f(s2)},
                           {"t", f(t)}, {"t2", f(t2)}, {"width", std::to_string(w)}},
                          {}});
  }
  return table;
}

// ---- tests ----------------------------------------------------------------------------------------------------------

void TestZipOverlay() {
  std::printf("zip overlay\n");
  const Bytes a = Text("first entry, deflated: the quick brown fox jumps over the lazy dog, the quick brown fox");
  const Bytes b = Text("second entry, stored");
  const Bytes c(5000, 0x5A);
  for (bool trailing : {true, false}) {
    const Bytes archive = BuildZip({{"dir/a.txt", a}, {"dir/B.bin", b, true}, {"c.dat", c}}, trailing);
    const auto read = ReaderFor(archive);
    const auto directory = zip::ReadDirectory(read, archive.size());
    CHECK(directory.has_value());
    if (!directory) {
      continue;
    }
    // The real end record, not the bare signature after it.
    CHECK(directory->entries.size() == 3);
    CHECK(directory->end_offset + zip::kEndOfCentralDirectorySize + (trailing ? 4 : 0) == archive.size());
    CHECK(directory->end.size() == zip::kEndOfCentralDirectorySize + (trailing ? 4 : 0));
    CHECK(zip::Find(*directory, "DIR\\b.bin") != nullptr);
    CHECK(zip::Find(*directory, "dir/missing") == nullptr);
    CHECK(zip::ReadEntry(read, *zip::Find(*directory, "dir/a.txt")) == a);
    CHECK(zip::ReadEntry(read, *zip::Find(*directory, "dir/b.bin")) == b);
    CHECK(zip::ReadEntry(read, *zip::Find(*directory, "c.dat")) == c);

    const Bytes replacement(70000, 0x33);
    const auto overlay = zip::BuildOverlay(*directory, {{"DIR/A.TXT", replacement}});
    CHECK(overlay.has_value());
    if (!overlay) {
      continue;
    }
    CHECK(overlay->replaced == 1);
    CHECK(overlay->prefix_size == directory->central_offset);
    // The whole overlay as bytes, read across the prefix and tail boundary in uneven pieces.
    Bytes whole(size_t(overlay->size()));
    for (size_t offset = 0; offset < whole.size();) {
      const size_t n = std::min<size_t>(777, whole.size() - offset);
      CHECK(zip::ReadOverlay(*overlay, read, offset, std::span<uint8_t>(whole).subspan(offset, n)));
      offset += n;
    }
    CHECK(!zip::ReadOverlay(*overlay, read, overlay->size() - 2, std::span<uint8_t>(whole).first(4)));
    CHECK(std::equal(archive.begin(), archive.begin() + directory->central_offset, whole.begin()));
    if (trailing) {
      CHECK(Get32(whole, whole.size() - 4) == zip::kEndOfCentralDirectorySignature);
    }
    const auto reread = zip::ReadDirectory(ReaderFor(whole), whole.size());
    CHECK(reread.has_value() && reread->entries.size() == 3);
    if (reread) {
      const auto* replaced = zip::Find(*reread, "dir/a.txt");
      CHECK(replaced && replaced->method == 0 && replaced->size == replacement.size());
      CHECK(replaced && zip::ReadEntry(ReaderFor(whole), *replaced) == replacement);
      for (const char* kept : {"dir/B.bin", "c.dat"}) {
        const auto* before = zip::Find(*directory, kept);
        const auto* after = zip::Find(*reread, kept);
        CHECK(after && after->local_offset == before->local_offset && after->crc == before->crc);
        CHECK(after && zip::ReadEntry(ReaderFor(whole), *after) == zip::ReadEntry(read, *before));
      }
      // The local header carries the same name, method, CRC and sizes as the central record (the title checks them).
      CHECK(replaced && Get32(whole, replaced->local_offset) == zip::kLocalHeaderSignature);
      CHECK(replaced && Get32(whole, replaced->local_offset + 14) == replaced->crc);
      CHECK(replaced && Get32(whole, replaced->local_offset + 18) == replaced->compressed_size);
    }
    if (!trailing) {
      // An independent reader agrees.
      mz_zip_archive check{};
      CHECK(mz_zip_reader_init_mem(&check, whole.data(), whole.size(), 0));
      CHECK(mz_zip_validate_archive(&check, 0));
      size_t size = 0;
      void* data = mz_zip_reader_extract_file_to_heap(&check, "dir/a.txt", &size, 0);
      CHECK(data && size == replacement.size() && std::memcmp(data, replacement.data(), size) == 0);
      mz_free(data);
      mz_zip_reader_end(&check);
    }
    CHECK(!zip::BuildOverlay(*directory, {{"missing.txt", replacement}}));
    CHECK(!zip::BuildOverlay(*directory, {{"dir/a.txt", replacement}, {"DIR/A.TXT", replacement}}));
  }
  // A damaged entry fails its CRC.
  Bytes archive = BuildZip({{"x.bin", Bytes(64, 1), true}}, false);
  const auto directory = zip::ReadDirectory(ReaderFor(archive), archive.size());
  CHECK(directory.has_value());
  archive[zip::kLocalHeaderSize + 5 + 3] ^= 0xFF;
  CHECK(directory && !zip::ReadEntry(ReaderFor(archive), directory->entries[0]));
  CHECK(!zip::ReadDirectory(ReaderFor(Bytes(10, 0)), 10));
}

void TestIgb() {
  std::printf("igb image\n");
  Bytes pixels(8 * 4 * 4);
  for (size_t i = 0; i < pixels.size(); ++i) {
    pixels[i] = uint8_t(i);
  }
  const Bytes file = BuildIgb(8, 4, pixels);
  const auto image = igb::FindImage(file);
  CHECK(image.has_value());
  if (!image) {
    return;
  }
  CHECK(image->width == 8 && image->height == 4 && image->components == 4 && image->pixel_format == 100);
  CHECK(image->bytes_per_row == 32 && image->image_size == pixels.size());
  CHECK(image->pixels_offset + pixels.size() == file.size());
  CHECK(std::equal(pixels.begin(), pixels.end(), file.begin() + image->pixels_offset));

  Bytes bigger(16 * 8 * 4, 0xAB);
  const auto rebuilt = igb::ReplaceImage(file, 16, 8, bigger);
  CHECK(rebuilt.has_value());
  if (rebuilt) {
    const auto again = igb::FindImage(*rebuilt);
    CHECK(again && again->width == 16 && again->height == 8 && again->bytes_per_row == 64 &&
          again->image_size == bigger.size());
    CHECK(again && std::equal(bigger.begin(), bigger.end(), rebuilt->begin() + again->pixels_offset));
    // Everything before the memory section is the same apart from the rewritten size fields, and the small block
    // that precedes the pixels is kept.
    CHECK(rebuilt->size() == file.size() - pixels.size() + bigger.size());
    CHECK(Get32(*rebuilt, 6 * 4) == Get32(file, 6 * 4) - pixels.size() + bigger.size());
    CHECK(std::equal(file.begin() + image->pixels_offset - 4, file.begin() + image->pixels_offset,
                     rebuilt->begin() + again->pixels_offset - 4));
    size_t differing = 0;
    for (size_t i = 0; i < image->pixels_offset; ++i) {
      differing += file[i] != (*rebuilt)[i] ? 1 : 0;
    }
    CHECK(differing > 0 && differing <= 6 * 4);
  }
  CHECK(!igb::ReplaceImage(file, 16, 8, Bytes(10)));
  CHECK(!igb::ReplaceImage(file, 0, 8, Bytes()));
  // Refused: a memory descriptor the image shares, a damaged magic, a truncated file.
  CHECK(!igb::FindImage(BuildIgb(8, 4, pixels, true)));
  Bytes damaged = file;
  damaged[40] = 0;
  CHECK(!igb::FindImage(damaged));
  CHECK(!igb::FindImage(std::span<const uint8_t>(file).first(file.size() - 1)));
  CHECK(!igb::FindImage(std::span<const uint8_t>(file).first(20)));
}

void TestXmlb() {
  std::printf("xmlb\n");
  const Bytes set = BuildXmlb({"FONTFILES",
                               {},
                               {{"font", {{"data", "ng_big"}, {"index", "big"}, {"texture", "ng_big"}}, {}},
                                {"font", {{"data", "med"}, {"index", "medium"}, {"language", "eng"}, {"texture", "med_eng"}}, {}},
                                {"font", {{"data", "med"}, {"index", "medium"}, {"language", "ita"}, {"texture", "med_ita"}}, {}}}});
  const auto fonts = xmlb::ParseFontFiles(set);
  CHECK(fonts && fonts->size() == 3);
  if (fonts && fonts->size() == 3) {
    CHECK((*fonts)[0].data == "ng_big" && (*fonts)[0].index == "big" && (*fonts)[0].language.empty());
    CHECK((*fonts)[2].texture == "med_ita" && (*fonts)[2].language == "ita");
  }
  const Bytes table = BuildXmlb(FontTableNode(29, {{65, 0.5f, 0.125f, 0.75f, 0.625f, 16, 16}, {162, 0.0f, 0.25f, 0.375f, 1.0f, 24, 24}}));
  const auto parsed = xmlb::ParseFontTable(table);
  CHECK(parsed && parsed->height == 29 && parsed->glyphs.size() == 2);  // the empty glyph 0 is dropped
  if (parsed && parsed->glyphs.size() == 2) {
    CHECK(parsed->glyphs[0].code == 65 && parsed->glyphs[0].s == 0.5f && parsed->glyphs[0].t2 == 0.625f);
    const font_hd::Rect r = font_hd::GlyphRect(parsed->glyphs[1], 64, 32);
    CHECK(r.x0 == 0 && r.y0 == 8 && r.x1 == 24 && r.y1 == 32);
  }
  CHECK(!xmlb::ParseFontTable(set));
  CHECK(!xmlb::ParseFontFiles(table));
  // A child pointing back at its parent, a string past the end, a wrong magic.
  Bytes loop = set;
  Put32(loop, 8 + 8, 8);
  CHECK(!xmlb::Parse(loop));
  Bytes past = set;
  Put32(past, 8, uint32_t(past.size() + 10));
  CHECK(!xmlb::Parse(past));
  Bytes magic = set;
  magic[0] = 0;
  CHECK(!xmlb::Parse(magic));
}

void TestAtlas() {
  std::printf("atlas rebuild\n");
  constexpr std::array<uint8_t, 4> kBlue{98, 172, 255, 255};
  constexpr std::array<uint8_t, 4> kClear{98, 172, 255, 0};
  Canvas canvas(32, 16, kClear);
  canvas.Glyph(8, 4, 20, 12, kBlue);
  const font_hd::Atlas atlas = canvas.Atlas();
  CHECK(font_hd::FromBottomUp(32, 16, font_hd::ToBottomUp(atlas)).rgba == atlas.rgba);

  const auto tone = font_hd::DetectTwoTone(atlas, {});
  CHECK(tone.has_value());
  if (!tone) {
    return;
  }
  CHECK(tone->fill.r == 255 && tone->fill.g == 255 && tone->fill.b == 255);
  CHECK(tone->outline.r == 98 && tone->outline.g == 172 && tone->outline.b == 255);
  CHECK(font_hd::FitsTwoTone(atlas, {8, 4, 20, 12}, *tone));

  const font_hd::Atlas hd = font_hd::RebuildText(atlas, 4, *tone);
  CHECK(hd.width == 128 && hd.height == 64 && hd.rgba.size() == 128 * 64 * 4);
  const uint32_t y = 32;  // middle row of the glyph
  // The outer edge stays at texel 8 (x = 32), the outline is one texel (4 pixels) wide, the inside is white.
  CHECK(hd.At(30, y)[3] == 0);
  CHECK(hd.At(31, y)[3] < 128 && hd.At(32, y)[3] > 128);
  CHECK(hd.At(33, y)[3] == 255 && hd.At(33, y)[0] == 98 && hd.At(33, y)[2] == 255);
  CHECK(hd.At(34, y)[0] < 140);
  CHECK(hd.At(38, y)[0] > 240 && hd.At(38, y)[1] > 240);
  CHECK(hd.At(64, y)[0] == 255 && hd.At(64, y)[3] == 255);
  CHECK(hd.At(81, y)[3] == 0);
  // Transparent texels keep the outline colour.
  CHECK(hd.At(2, 2)[0] == 98 && hd.At(2, 2)[1] == 172 && hd.At(2, 2)[3] == 0);
  // Same input, same output.
  CHECK(font_hd::RebuildText(atlas, 4, *tone).rgba == hd.rgba);
  const font_hd::Atlas hd2 = font_hd::RebuildText(atlas, 2, *tone);
  CHECK(hd2.width == 64 && hd2.At(15, 16)[3] < 128 && hd2.At(16, 16)[3] > 128);

  // A coloured square does not fit the two tones.
  Canvas red(8, 8, kClear);
  for (uint32_t yy = 2; yy < 6; ++yy) {
    for (uint32_t xx = 2; xx < 6; ++xx) {
      red.Set(xx, yy, {220, 20, 20, 255});
    }
  }
  CHECK(!font_hd::FitsTwoTone(red.Atlas(), {0, 0, 8, 8}, *tone));
  CHECK(!font_hd::DetectTwoTone(Canvas(8, 8, kClear).Atlas(), {}));
}

void TestIcons() {
  std::printf("icons\n");
  // A two-tone glyph next to a button-like disc in the cell of code 162 (texels 0..24 x 8..32). As in the games'
  // atlases, text covers more texels than the icons (the second box is outside the table).
  Canvas canvas(64, 32, {0, 0, 0, 0});
  canvas.Glyph(32, 4, 48, 20, {0, 0, 0, 255});
  canvas.Glyph(49, 2, 64, 32, {0, 0, 0, 255});
  canvas.Button(0, 8);
  const font_hd::Atlas atlas = canvas.Atlas();
  const auto table = xmlb::ParseFontTable(
      BuildXmlb(FontTableNode(29, {{65, 0.5f, 0.125f, 0.75f, 0.625f, 16, 16}, {162, 0.0f, 0.25f, 0.375f, 1.0f, 24, 24}})));
  CHECK(table.has_value());
  if (!table) {
    return;
  }
  const font_hd::IconRule face[] = {{162, font_hd::FaceButton::kA}};
  font_hd::BuildReport report;
  const auto hd = font_hd::BuildHdAtlas(atlas, *table, 4, face, &report);
  CHECK(hd.has_value());
  if (!hd) {
    return;
  }
  CHECK(report.text_glyphs == 1 && report.face_buttons == 1 && report.icons == 0 && report.unlisted_icons == 0);
  CHECK(report.tone.fill.r == 255 && report.tone.outline.r == 0);
  // The button cell (texels 0..24 x 8..32, pixels 0..96 x 32..128): corners clear, rim and disc opaque, the disc green.
  CHECK(hd->At(1, 33)[3] == 0 && hd->At(1, 127)[3] == 0);
  const uint8_t* rim = hd->At(50, 32 + 4 * 2);
  CHECK(rim[3] == 255 && rim[0] > 180 && rim[1] > 180 && rim[2] > 180);
  const uint8_t* disc = hd->At(50 - 4 * 6, 32 + 4 * 12);
  CHECK(disc[3] == 255 && disc[1] > disc[0] && disc[1] > disc[2]);
  // Unlisted, the same disc is still recognised as a picture and rebuilt as an icon, not as two-tone text.
  font_hd::BuildReport unlisted;
  const auto generic = font_hd::BuildHdAtlas(atlas, *table, 4, {}, &unlisted);
  CHECK(generic && unlisted.unlisted_icons == 1 && unlisted.text_glyphs == 1);
  if (generic) {
    const uint8_t* g = generic->At(48, 32 + 48);
    CHECK(g[3] == 255 && g[1] > g[0] && g[1] > g[2]);
  }
  // A listed plain icon keeps its colours.
  const font_hd::IconRule plain[] = {{162, {}}};
  font_hd::BuildReport icon_report;
  const auto icon = font_hd::BuildHdAtlas(atlas, *table, 2, plain, &icon_report);
  CHECK(icon && icon_report.icons == 1 && icon->width == 128 && icon->height == 64);
}

Bytes FontArchive(bool with_icon_font) {
  Canvas big(64, 32, {98, 172, 255, 0});
  big.Glyph(8, 4, 24, 20, {98, 172, 255, 255});
  big.Glyph(32, 8, 56, 28, {98, 172, 255, 255});
  Canvas med(64, 32, {0, 0, 0, 0});
  med.Glyph(32, 4, 48, 20, {0, 0, 0, 255});
  med.Button(0, 8);
  std::vector<XNode> fonts = {{"font", {{"data", "tst_big"}, {"index", "big"}, {"texture", "tst_big"}}, {}}};
  if (with_icon_font) {
    fonts.push_back({"font", {{"data", "tst_med"}, {"index", "medium"}, {"language", "eng"}, {"texture", "tst_med_eng"}}, {}});
    fonts.push_back({"font", {{"data", "tst_med"}, {"index", "medium"}, {"language", "ita"}, {"texture", "tst_med_eng"}}, {}});
  }
  std::vector<ZipFile> files = {
      {"data/other.bin", Bytes(3000, 9)},
      {"ui/fonts/fonts_360_hd.xmlb", BuildXmlb({"FONTFILES", {}, fonts})},
      {"ui/fonts/tst_big.xmlb",
       BuildXmlb(FontTableNode(72, {{65, 0.125f, 0.125f, 0.375f, 0.625f, 16, 16}, {66, 0.5f, 0.25f, 0.875f, 0.875f, 24, 20}}))},
      {"textures/fonts/tst_big.igb", BuildIgb(64, 32, big.BottomUp())},
  };
  if (with_icon_font) {
    files.push_back({"ui/fonts/tst_med.xmlb",
                     BuildXmlb(FontTableNode(29, {{65, 0.5f, 0.125f, 0.75f, 0.625f, 16, 16},
                                                  {162, 0.0f, 0.25f, 0.375f, 1.0f, 24, 24}}))});
    files.push_back({"textures/fonts/tst_med_eng.igb", BuildIgb(64, 32, med.BottomUp())});
  }
  return BuildZip(files, true);
}

struct GameArchive {
  const char* key;
  GameId game;
  fs::path path;
};

std::vector<GameArchive> RealArchives() {
  return {{"mua1", GameId::kMua1, kRepoDir / "game" / "mua1" / "z" / "assetsfb.wad"},
          {"mua1-gold", GameId::kMua1Gold, kRepoDir / "game" / "mua1-gold" / "z" / "assetsfb.zip"}};
}

class FileReader {
 public:
  explicit FileReader(const fs::path& path) : file_(path, std::ios::binary), size_(fs::file_size(path)) {}
  uint64_t size() const { return size_; }
  zip::ReadAt reader() {
    return [this](uint64_t offset, std::span<uint8_t> out) {
      file_.clear();
      file_.seekg(std::streamoff(offset));
      file_.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
      return file_.gcount() == std::streamsize(out.size());
    };
  }

 private:
  std::ifstream file_;
  uint64_t size_;
};

// CRCs of the rebuilt atlas entries Saulo approved in MUA1; Gold must produce the same bytes.
struct Golden {
  const char* texture;
  uint32_t crc_2x;
  uint32_t crc_4x;
};
constexpr Golden kGolden[] = {
    {"ng_360_big_hd", 0xE9982B51, 0x5D4AFDC8},
    {"360_med_hd_eng", 0x9E3FC77E, 0x9783B0D3},
    {"360_med_hd_ita", 0x90B16D37, 0x234BC3A6},
};

void TestPlan() {
  std::printf("plan\n");
  const auto* mua1 = hd_text::AssetsFor(GameId::kMua1);
  const auto* gold = hd_text::AssetsFor(GameId::kMua1Gold);
  CHECK(mua1 && mua1->archive == "game:\\z\\assetsfb.wad");
  CHECK(gold && gold->archive == "game:\\z\\assetsfb.zip" && gold->font_set == mua1->font_set);
  CHECK(hd_text::AssetsFor(GameId::kMua2) == nullptr);
  CHECK(std::size(hd_text::kGameAssets) == 2);
  CHECK(std::string_view(hd_text::kSettingKey) == "mua_hd_text");

  for (GameId game : {GameId::kMua1, GameId::kMua1Gold}) {
    for (int scale : {0, 1, 2, 3, 4}) {
      CHECK(!hd_text::PlanFor(game, false, scale).Builds());
    }
    CHECK(!hd_text::PlanFor(game, true, 0).Builds() && !hd_text::PlanFor(game, true, 1).Builds());
    CHECK(hd_text::PlanFor(game, true, 2).Builds() && hd_text::PlanFor(game, true, 2).scale == 2);
    CHECK(hd_text::PlanFor(game, true, 3).scale == 4 && hd_text::PlanFor(game, true, 4).scale == 4);
    CHECK(hd_text::PlanFor(game, true, 4).assets == hd_text::AssetsFor(game));
  }
  for (bool on : {false, true}) {
    for (int scale : {1, 2, 4}) {
      CHECK(!hd_text::PlanFor(GameId::kMua2, on, scale).Builds() && !hd_text::PlanFor(GameId::kMua2, on, scale).assets);
    }
  }

  for (const char* off : {"false", "False", "FALSE", "0", "off", "no"}) {
    CHECK(!hd_text::ParseSetting(off));
  }
  for (const char* on : {"true", "1", "on", "", "maybe", "2"}) {
    CHECK(hd_text::ParseSetting(on));
  }
  CHECK(hd_text::SummaryOn(2, 3, 6408814, 109) == "HD text: On | scale=2x | atlases=3 | added=6258 KiB | built in 109 ms");
}

void TestPipeline() {
  std::printf("pipeline\n");
  CHECK(hd_text::AtlasScaleFor(0) == 0 && hd_text::AtlasScaleFor(1) == 0);
  CHECK(hd_text::AtlasScaleFor(2) == 2 && hd_text::AtlasScaleFor(3) == 4 && hd_text::AtlasScaleFor(4) == 4);
  const auto medium = hd_text::IconRulesFor("medium");
  CHECK(medium.size() == 30 && hd_text::IconRulesFor("big").empty());
  size_t faces = 0;
  for (const auto& rule : medium) {
    faces += rule.face ? 1 : 0;
    CHECK((rule.code >= 162 && rule.code <= 188) || (rule.code >= 194 && rule.code <= 196));
  }
  CHECK(faces == 4 && medium[0].face == font_hd::FaceButton::kA && medium[1].face == font_hd::FaceButton::kB &&
        medium[2].face == font_hd::FaceButton::kX && medium[3].face == font_hd::FaceButton::kY);

  const Bytes archive = FontArchive(true);
  const auto read = ReaderFor(archive);
  CHECK(!hd_text::BuildFontOverlay(read, archive.size(), "ui/fonts/fonts_360_HD.xmlb", 1).overlay);
  CHECK(!hd_text::BuildFontOverlay(read, archive.size(), "ui/fonts/missing.xmlb", 2).overlay);
  const hd_text::Result result = hd_text::BuildFontOverlay(read, archive.size(), "ui/fonts/fonts_360_HD.xmlb", 2);
  CHECK(result.overlay.has_value() && result.error.empty());
  if (!result.overlay) {
    std::printf("  error: %s\n", result.error.c_str());
    return;
  }
  // Two textures: the Italian medium font names the same texture and is rebuilt once.
  CHECK(result.fonts.size() == 2 && result.overlay->replaced == 2);
  CHECK(result.fonts[0].texture == "tst_big" && result.fonts[0].width == 128 && result.fonts[0].height == 64);
  CHECK(result.fonts[0].report.text_glyphs == 2 && result.fonts[0].report.face_buttons == 0);
  CHECK(result.fonts[1].report.face_buttons == 1 && result.fonts[1].report.text_glyphs == 1);
  Bytes whole(size_t(result.overlay->size()));
  CHECK(zip::ReadOverlay(*result.overlay, read, 0, whole));
  const auto directory = zip::ReadDirectory(ReaderFor(whole), whole.size());
  CHECK(directory && directory->entries.size() == 6);
  if (directory) {
    for (const char* name : {"textures/fonts/tst_big.igb", "textures/fonts/tst_med_eng.igb"}) {
      const auto data = zip::ReadEntry(ReaderFor(whole), *zip::Find(*directory, name));
      const auto image = data ? igb::FindImage(*data) : std::nullopt;
      CHECK(image && image->width == 128 && image->height == 64);
    }
    // The font tables and everything else stay the game's.
    const auto original = zip::ReadDirectory(read, archive.size());
    for (const char* name : {"data/other.bin", "ui/fonts/tst_big.xmlb", "ui/fonts/fonts_360_hd.xmlb"}) {
      CHECK(zip::Find(*directory, name)->local_offset == zip::Find(*original, name)->local_offset);
      CHECK(zip::ReadEntry(ReaderFor(whole), *zip::Find(*directory, name)) ==
            zip::ReadEntry(read, *zip::Find(*original, name)));
    }
  }
  // Too big for the title's textures.
  const Bytes small = FontArchive(false);
  const auto huge = hd_text::BuildFontOverlay(ReaderFor(small), small.size(), "ui/fonts/fonts_360_HD.xmlb", 256);
  CHECK(!huge.overlay && Has(huge.error, "texture size limit"));
}

// The real archives, when the game folders hold them.
void TestRealArchives() {
  std::map<std::string, std::map<std::string, uint32_t>> font_crcs;
  for (const GameArchive& game : RealArchives()) {
    std::error_code ec;
    if (!fs::exists(game.path, ec)) {
      std::printf("real archive %s: skipped (no %s)\n", game.key, game.path.string().c_str());
      continue;
    }
    std::printf("real archive %s\n", game.key);
    const auto* assets = hd_text::AssetsFor(game.game);
    CHECK(assets && Has(std::string(assets->archive), game.path.filename().string()));
    FileReader file(game.path);
    const auto read = file.reader();
    const auto original = zip::ReadDirectory(read, file.size());
    CHECK(original.has_value());
    if (!original) {
      continue;
    }
    const auto set = zip::ReadEntry(read, *zip::Find(*original, assets->font_set));
    const auto fonts = set ? xmlb::ParseFontFiles(*set) : std::nullopt;
    CHECK(fonts && fonts->size() == 3);
    for (const char* name : {"ui/fonts/fonts_360_HD.xmlb", "ui/fonts/ng_360_big_hd.xmlb", "ui/fonts/360_med_hd.xmlb",
                             "textures/fonts/ng_360_big_hd.igb", "textures/fonts/360_med_hd_eng.igb",
                             "textures/fonts/360_med_hd_ita.igb"}) {
      const auto* entry = zip::Find(*original, name);
      CHECK(entry != nullptr);
      if (entry) {
        font_crcs[name][game.key] = entry->crc;
      }
    }

    for (uint32_t scale : {2u, 4u}) {
      const hd_text::Result result = hd_text::BuildFontOverlay(read, file.size(), assets->font_set, scale);
      CHECK(result.overlay.has_value() && result.fonts.size() == 3 && result.overlay->replaced == 3);
      if (!result.overlay) {
        std::printf("  error: %s\n", result.error.c_str());
        continue;
      }
      for (const hd_text::FontResult& font : result.fonts) {
        CHECK(font.width == 512 * scale && font.height == 256 * scale && font.scale == scale);
        if (font.texture == "ng_360_big_hd") {
          CHECK(font.report.text_glyphs > 80 && font.report.icons == 0 && font.report.face_buttons == 0);
          CHECK(font.report.tone.outline.r == 98 && font.report.tone.outline.g == 172 && font.report.tone.fill.r == 255);
        } else {
          CHECK(font.texture == "360_med_hd_eng" || font.texture == "360_med_hd_ita");
          CHECK(font.report.face_buttons == 4 && font.report.icons == 26 && font.report.unlisted_icons == 0);
          CHECK(font.report.tone.outline.r == 0 && font.report.tone.fill.r == 255);
        }
      }
      // Three RGBA atlases of (512 x 256) * scale^2 texels plus small ZIP records.
      const uint64_t atlases = 3ull * 512 * 256 * 4 * scale * scale;
      CHECK(result.overlay->tail.size() > atlases && result.overlay->tail.size() < atlases + atlases / 16);

      const zip::Overlay& overlay = *result.overlay;
      const zip::ReadAt read_overlay = [&](uint64_t offset, std::span<uint8_t> out) {
        return zip::ReadOverlay(overlay, read, offset, out);
      };
      const auto directory = zip::ReadDirectory(read_overlay, overlay.size());
      CHECK(directory && directory->entries.size() == original->entries.size());
      if (!directory) {
        continue;
      }
      for (const Golden& golden : kGolden) {
        const auto* entry = zip::Find(*directory, std::string("textures/fonts/") + golden.texture + ".igb");
        CHECK(entry && entry->crc == (scale == 2 ? golden.crc_2x : golden.crc_4x));
        const auto data = entry ? zip::ReadEntry(read_overlay, *entry) : std::nullopt;
        const auto image = data ? igb::FindImage(*data) : std::nullopt;
        CHECK(image && image->width == 512 * scale && image->height == 256 * scale && image->pixel_format == 100);
      }
      const auto* table = zip::Find(*directory, "ui/fonts/ng_360_big_hd.xmlb");
      CHECK(table && table->local_offset == zip::Find(*original, "ui/fonts/ng_360_big_hd.xmlb")->local_offset);
    }
  }
  // Gold's font set, tables and atlases are MUA1's.
  for (const auto& [name, crcs] : font_crcs) {
    if (crcs.size() == 2) {
      CHECK(crcs.at("mua1") == crcs.at("mua1-gold"));
    }
  }
  // Gold's loader asks for z\assetsfb.wad and falls back to the .zip; its disc only has the .zip.
  const fs::path gold_dir = kRepoDir / "game" / "mua1-gold" / "z";
  std::error_code ec;
  if (fs::exists(gold_dir, ec)) {
    CHECK(fs::exists(gold_dir / "assetsfb.zip") && !fs::exists(gold_dir / "assetsfb.wad"));
  }
}

size_t Count(const std::string& text, std::string_view needle) {
  size_t n = 0;
  for (size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + 1)) {
    ++n;
  }
  return n;
}

void TestLauncherAndSetting() {
  std::printf("launcher and setting\n");
  const std::string launcher = ReadText(kSrcDir / "launcher.cpp");
  const std::string settings = ReadText(kSrcDir / "settings.cpp");
  CHECK(Count(launcher, "\"HD Text\"") == 1);
  CHECK(Count(launcher, "hd_text::kSettingKey") == 1);
  CHECK(Has(launcher,
            "ui.Toggle(\"HD Text\",\n"
            "                \"Rebuilds in-game text and controller icons at higher resolution. Uses additional memory "
            "at higher \"\n"
            "                \"render resolutions.\",\n"
            "                hd_text::kSettingKey, true);") ||
        Has(launcher,
            "ui.Toggle(\"HD Text\",\r\n"
            "                \"Rebuilds in-game text and controller icons at higher resolution. Uses additional memory "
            "at higher \"\r\n"
            "                \"render resolutions.\",\r\n"
            "                hd_text::kSettingKey, true);"));
  const size_t page = launcher.find("void PageGraphics(trg::Ui& ui)");
  const size_t row = launcher.find("ui.Toggle(\"HD Text\"");
  const size_t next = launcher.find("bool UpscalingApplies()");
  CHECK(page != std::string::npos && row > page && row < next);
  CHECK(Has(launcher, "bool HdTextApplies() const { return Selected().game && hd_text::AssetsFor(Selected().game->id); }"));

  // One global key: saved by the launcher, not per game, so MUA1 and Gold share it.
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_hd_text, true,"));
  CHECK(Count(settings, "\"mua_hd_text\"") == 1);
  const size_t keys = settings.find("std::vector<std::string> GameSettingKeys()");
  const size_t keys_end = settings.find("};", keys);
  CHECK(settings.substr(keys, keys_end - keys).find("\"mua_hd_text\"") != std::string::npos);
  const size_t scoped = settings.find("kGameScopedKeys[]");
  const size_t scoped_end = settings.find(";", scoped);
  CHECK(settings.substr(scoped, scoped_end - scoped).find("mua_hd_text") == std::string::npos);

  // No old name anywhere.
  for (const fs::path& path : {kSrcDir / "launcher.cpp", kSrcDir / "settings.cpp", kSrcDir / "settings.h",
                               kSrcDir / "marvel_ultimate_alliance_app.h", kSrcDir / "hd_text.h", kSrcDir / "hd_text.cpp",
                               kSrcDir / "hd_text_vfs.cpp", kRepoDir / "README.md",
                               kRepoDir / "config" / "marvel_ultimate_alliance.toml"}) {
    const std::string text = ReadText(path);
    CHECK(!Has(text, "HD Menu Text") && !Has(text, "HD menu text") && !Has(text, "mua_hd_menu_text"));
  }
}

void TestWiring() {
  std::printf("wiring\n");
  const std::string app = ReadText(kSrcDir / "marvel_ultimate_alliance_app.h");
  const std::string cmake = ReadText(kRepoDir / "CMakeLists.txt");
  const std::string vfs = ReadText(kSrcDir / "hd_text_vfs.cpp");
  const size_t post_setup = app.find("void OnPostSetup() override");
  const size_t install = app.find("mua::hd_text::InstallHdText(runtime()->file_system()");
  CHECK(post_setup != std::string::npos && install != std::string::npos && install > post_setup);
  CHECK(Has(app, "mua::hd_text::ParseSetting(rex::cvar::GetFlagByName(mua::hd_text::kSettingKey))"));
  for (const char* source : {"src/font_atlas_hd.cpp", "src/hd_text.cpp", "src/hd_text_vfs.cpp", "src/igb_image.cpp",
                             "src/xmlb.cpp", "src/zip_overlay.cpp"}) {
    CHECK(Has(cmake, source));
  }
  // Off and 1x return before the archive is opened, so nothing is built, allocated or swapped.
  const size_t plan = vfs.find("const Plan plan = PlanFor(game, on, render_scale);");
  const size_t off = vfs.find("REXLOG_INFO(\"HD text: Off\");");
  const size_t one = vfs.find("REXLOG_INFO(\"HD text: On | scale=1x | original atlases\");");
  const size_t resolve = vfs.find("vfs->ResolvePath(");
  const size_t build = vfs.find("BuildFontOverlay(");
  const size_t swap = vfs.find("*slot = std::make_unique<OverlayEntry>");
  CHECK(plan != std::string::npos && plan < off && off < one && one < resolve && resolve < build && build < swap);
  CHECK(Count(vfs, "REXLOG_INFO(") == 3);
  CHECK(Has(vfs, "return X_STATUS_ACCESS_DENIED;"));
  CHECK(Has(vfs, "if (desired_access & kWriteAccess)"));
  // MUA2 keeps its own menus.
  CHECK(!Has(ReadText(kSrcDir / "main_menu_mua2.cpp"), "hd_text"));
}

}  // namespace

int main() {
  TestZipOverlay();
  TestIgb();
  TestXmlb();
  TestAtlas();
  TestIcons();
  TestPlan();
  TestPipeline();
  TestRealArchives();
  TestLauncherAndSetting();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
