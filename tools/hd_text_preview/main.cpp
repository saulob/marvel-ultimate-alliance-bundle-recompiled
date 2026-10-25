// Builds the HD text overlay from a game folder exactly as the runtime does and writes what it made, without
// starting the game:
//   hd_text_preview <game folder> <output folder> [render scale 2..4] [mua1|mua1-gold]
// For each rebuilt font it writes <texture>_original.png and <texture>_x<scale>.png (rows top first, as the font tables
// measure them), reads the overlay back through the same ZIP and IGB readers and reports the timings.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "font_atlas_hd.h"
#include "hd_text.h"
#include "igb_image.h"
#include "updater/third_party/miniz/miniz.h"
#include "zip_overlay.h"

namespace fs = std::filesystem;

namespace {

bool WritePng(const fs::path& path, const mua::font_hd::Atlas& atlas) {
  size_t size = 0;
  void* png = tdefl_write_image_to_png_file_in_memory_ex(atlas.rgba.data(), int(atlas.width), int(atlas.height), 4,
                                                         &size, 6, MZ_FALSE);
  if (!png) {
    return false;
  }
  std::ofstream out(path, std::ios::binary);
  out.write(static_cast<const char*>(png), std::streamsize(size));
  mz_free(png);
  return bool(out);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: hd_text_preview <game folder> <output folder> [render scale 2..4] [mua1|mua1-gold]\n");
    return 2;
  }
  const fs::path game = argv[1];
  const fs::path out_dir = argv[2];
  const int render_scale = argc > 3 ? std::atoi(argv[3]) : 4;
  const std::string key = argc > 4 ? argv[4] : "mua1";
  const mua::hd_text::GameAssets* assets =
      mua::hd_text::AssetsFor(key == "mua1-gold" ? mua::GameId::kMua1Gold : mua::GameId::kMua1);
  std::string relative(assets->archive.substr(std::string_view("game:\\").size()));
  for (char& c : relative) {
    c = c == '\\' ? '/' : c;
  }
  const fs::path archive = game / relative;
  std::ifstream file(archive, std::ios::binary);
  if (!file) {
    std::printf("cannot open %s\n", archive.string().c_str());
    return 1;
  }
  const uint64_t file_size = fs::file_size(archive);
  const mua::zip::ReadAt read = [&](uint64_t offset, std::span<uint8_t> out) {
    file.clear();
    file.seekg(std::streamoff(offset));
    file.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    return file.gcount() == std::streamsize(out.size());
  };

  const uint32_t scale = mua::hd_text::AtlasScaleFor(render_scale);
  const auto start = std::chrono::steady_clock::now();
  const mua::hd_text::Result result = mua::hd_text::BuildFontOverlay(read, file_size, assets->font_set, scale);
  const double build_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  if (!result.overlay) {
    std::printf("no overlay: %s\n", result.error.c_str());
    return 1;
  }
  std::printf("archive %s: %llu bytes, overlay %llu bytes (+%zu tail, %zu entries replaced) in %.0f ms\n",
              archive.string().c_str(), (unsigned long long)file_size, (unsigned long long)result.overlay->size(),
              result.overlay->tail.size(), result.overlay->replaced, build_ms);

  // Read the overlay back the way the title will: directory, local headers, CRCs, then the IGB image.
  const mua::zip::Overlay& overlay = *result.overlay;
  const mua::zip::ReadAt read_overlay = [&](uint64_t offset, std::span<uint8_t> out) {
    return mua::zip::ReadOverlay(overlay, read, offset, out);
  };
  const auto directory = mua::zip::ReadDirectory(read_overlay, overlay.size());
  if (!directory) {
    std::printf("FAIL: the overlay's directory does not read back\n");
    return 1;
  }
  fs::create_directories(out_dir);
  int failures = 0;
  for (const mua::hd_text::FontResult& font : result.fonts) {
    const std::string name = "textures/fonts/" + font.texture + ".igb";
    const auto* hd_entry = mua::zip::Find(*directory, name);
    const auto hd = hd_entry ? mua::zip::ReadEntry(read_overlay, *hd_entry) : std::nullopt;
    const auto hd_image = hd ? mua::igb::FindImage(*hd) : std::nullopt;
    const auto original_directory = mua::zip::ReadDirectory(read, file_size);
    const auto* original_entry = mua::zip::Find(*original_directory, name);
    const auto original = mua::zip::ReadEntry(read, *original_entry);
    const auto original_image = mua::igb::FindImage(*original);
    if (!hd_image || hd_image->width != font.width || hd_image->height != font.height) {
      std::printf("FAIL: %s does not read back as a %ux%u image\n", name.c_str(), font.width, font.height);
      ++failures;
      continue;
    }
    std::printf("%s: crc %08X, %ux%u -> %ux%u, %s, %zu text glyphs, %zu icons, %zu face buttons, %zu unlisted icons\n",
                font.texture.c_str(), hd_entry->crc, original_image->width, original_image->height, hd_image->width,
                hd_image->height, mua::hd_text::DescribeTone(font.report.tone).c_str(), font.report.text_glyphs,
                font.report.icons, font.report.face_buttons, font.report.unlisted_icons);
    const auto before = mua::font_hd::FromBottomUp(
        original_image->width, original_image->height,
        std::span<const uint8_t>(*original).subspan(original_image->pixels_offset, original_image->image_size));
    const auto after = mua::font_hd::FromBottomUp(
        hd_image->width, hd_image->height,
        std::span<const uint8_t>(*hd).subspan(hd_image->pixels_offset, hd_image->image_size));
    WritePng(out_dir / (font.texture + "_original.png"), before);
    WritePng(out_dir / (font.texture + "_x" + std::to_string(font.scale) + ".png"), after);
  }
  // Every other entry must still read back unchanged.
  const auto original_directory = mua::zip::ReadDirectory(read, file_size);
  size_t checked = 0;
  for (const mua::zip::CentralEntry& entry : original_directory->entries) {
    if (entry.name.find("textures/fonts/") == 0 || entry.size == 0 || entry.size > (1u << 20) || checked >= 64) {
      continue;
    }
    const auto* copy = mua::zip::Find(*directory, entry.name);
    if (!copy || copy->local_offset != entry.local_offset || copy->crc != entry.crc ||
        !mua::zip::ReadEntry(read_overlay, *copy)) {
      std::printf("FAIL: %s changed\n", entry.name.c_str());
      ++failures;
    }
    ++checked;
  }
  std::printf("%zu untouched entries read back unchanged; %d failure(s)\n", checked, failures);
  return failures ? 1 : 0;
}
