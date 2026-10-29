// DLSS scene upscaler geometry and math (patches/rexglue-sdk/0161 and 0162, include/rex/graphics/d3d12/scene_layout.h
// in the staged ReXGlue source): resolve rectangles recovered from draw_util::GetResolveInfo's fields, MUA1's two
// predicated tiles composing one DLSS input, the write-back placing each resolve where the resolve copy shaders read
// it, D24S8 and D24FS8 depth decoding, camera reprojection giving motion only when the camera moves, and the targets
// drawn before the scene: their whole scale, which surfaces qualify, and their resolves stretched back to full size;
// and the DLSS depth (0163): the ROV's D24S8 and D24FS8 read back with the renderer's own conversion, which resolve it
// is taken from, and its composition from both tiles at the scene's scale. AMD FSR 3.1 (0165): AMD's render sizes,
// jitter phases and offsets, the camera planes recovered from the game's matrices and read back through FSR's own depth
// conversion, and the motion vectors reprojected the way FSR's shaders do. Scene profiles (0166 to 0168): MUA1's keeps
// 0161's scene surface rule, Gold Edition's adds its Direct3D's 4x MSAA draws over the scene's memory to the scene's
// pass, and MUA2's pins its 1040x624 scene surface, modelled on its gameplay frame in log 268; 0169 draws MUA2's
// shadow pass at 1x and keeps the same surface after the scene apart.
// Run: mua_scene_upscaler_math_tests

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rex/graphics/d3d12/scene_layout.h>
#include <rex/graphics/xenos.h>

#include "game_definition.h"
#include "upscaling.h"

namespace {

namespace scene = rex::graphics::d3d12::scene;

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

struct ResolveFields {
  uint32_t base_tiles, edram_offset_x_div_8, edram_offset_y_div_8, width_div_8, height_div_8;
};

// draw_util::GetResolveInfo's EDRAM fields for a rectangle at x0, y0 in the surface (window offset applied).
ResolveFields GuestResolveFields(uint32_t base, uint32_t pitch_tiles, uint32_t log2_x, uint32_t log2_y, int32_t x0,
                                 int32_t y0, uint32_t width, uint32_t height) {
  const uint32_t x0_samples = uint32_t(x0) << log2_x;
  const uint32_t y0_samples = uint32_t(y0) << log2_y;
  return {base + (y0_samples / 16) * pitch_tiles + x0_samples / 80, (x0_samples % 80) >> (log2_x + 3),
          (y0_samples % 16) >> (log2_y + 3), width / 8, height / 8};
}

// The dword a 32bpp resolve copy shader reads for pixel (x, y) of a single-sampled source.
uint32_t ResolveSourceDword(uint32_t base_tiles, uint32_t offset_x_div_8, uint32_t offset_y_div_8,
                            uint32_t pitch_tiles, uint32_t x, uint32_t y) {
  const uint32_t tile_x = base_tiles % pitch_tiles, tile_y = base_tiles / pitch_tiles;
  const uint32_t sample_x = tile_x * 80 + offset_x_div_8 * 8 + x;
  const uint32_t sample_y = tile_y * 16 + offset_y_div_8 * 8 + y;
  return (sample_y / 16) * pitch_tiles * 1280 + (sample_x / 80) * 1280 + (sample_y % 16) * 80 + sample_x % 80;
}

// scene_upscaler.hlsli XeSceneEdramSampleOffset for color.
uint32_t StagingDword(uint32_t x, uint32_t y, uint32_t pitch_tiles) {
  return (y / 16) * pitch_tiles * 1280 + (x / 80) * 1280 + (y % 16) * 80 + x % 80;
}

void TestResolveRects() {
  std::printf("resolve rectangles from GetResolveInfo's fields\n");
  // MUA1 at 1280x720, 2x MSAA, surface pitch 800: the two predicated tiles of log 245.
  const scene::ResolveRect left = scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 672 / 8, 720 / 8, 0, 0);
  CHECK(left.edram_x == 0 && left.edram_y == 0 && left.screen_x == 0 && left.screen_y == 0);
  CHECK(left.width == 672 && left.height == 720);
  const scene::ResolveRect right = scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 640 / 8, 720 / 8, -640, 0);
  CHECK(right.edram_x == 0 && right.screen_x == 640 && right.width == 640);
  // Every 8-aligned rectangle survives the round trip at 1x, 2x and 4x, with any base and window offset.
  int round_trips = 0, exact = 0;
  for (uint32_t msaa = 0; msaa < 3; ++msaa) {
    const uint32_t log2_x = msaa >= 2, log2_y = msaa >= 1;
    for (uint32_t base : {0u, 1128u, 2040u}) {
      for (int32_t x0 = 0; x0 < 400; x0 += 56) {
        for (int32_t y0 = 0; y0 < 300; y0 += 40) {
          const ResolveFields fields = GuestResolveFields(base, 10, log2_x, log2_y, x0, y0, 64, 32);
          const scene::ResolveRect rect =
              scene::ResolveRectFor(fields.base_tiles & 2047, base, 10, log2_x, log2_y, fields.edram_offset_x_div_8,
                                    fields.edram_offset_y_div_8, fields.width_div_8, fields.height_div_8, -24, 16);
          ++round_trips;
          exact += rect.edram_x == x0 && rect.edram_y == y0 && rect.screen_x == x0 + 24 &&
                   rect.screen_y == y0 - 16 && rect.width == 64 && rect.height == 32;
        }
      }
    }
  }
  CHECK(round_trips > 0 && exact == round_trips);
}

// The scene's render pixels from MUA1's two tiles, against the scene as it should be.
std::vector<scene::CaptureRegion> ComposeTwoTiles(uint32_t render_width, uint32_t render_height, uint64_t& wrong_out) {
  const uint32_t guest_width = 1280, guest_height = 720;
  const double scale_x = double(render_width) / guest_width, scale_y = double(render_height) / guest_height;

  // The scene as it should be, per render pixel; scaled draws put render pixel (x, y) of a tile with window offset w
  // at EDRAM render pixel (x + scale * w, y).
  const auto ideal = [](int32_t x, int32_t y) { return uint32_t(x * 1000 + y + 1); };
  struct Tile {
    scene::ResolveRect rect;
    int32_t window_x;
  };
  const std::array<Tile, 2> tiles = {{
      {scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 672 / 8, 720 / 8, 0, 0), 0},
      {scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 640 / 8, 720 / 8, -640, 0), -640},
  }};
  std::vector<uint32_t> composed(size_t(render_width) * render_height, 0);
  std::vector<scene::CaptureRegion> regions;
  for (const Tile& tile : tiles) {
    const scene::CaptureRegion region = scene::CaptureRegionFor(tile.rect, scale_x, scale_y, render_width,
                                                                render_height);
    regions.push_back(region);
    const int32_t edram_shift = int32_t(std::lround(scale_x * tile.window_x));
    for (uint32_t y = 0; y < region.height; ++y) {
      for (uint32_t x = 0; x < region.width; ++x) {
        const int32_t edram_x = region.source_x + int32_t(x), edram_y = region.source_y + int32_t(y);
        composed[size_t(region.dest_y + y) * render_width + region.dest_x + x] = ideal(edram_x - edram_shift, edram_y);
      }
    }
  }
  wrong_out = 0;
  for (uint32_t y = 0; y < render_height; ++y) {
    for (uint32_t x = 0; x < render_width; ++x) {
      wrong_out += composed[size_t(y) * render_width + x] != ideal(int32_t(x), int32_t(y));
    }
  }
  return regions;
}

void TestTwoTilesCompose() {
  std::printf("MUA1's two predicated tiles compose one coherent scene: Quality at 1x, Performance at 4x\n");
  const uint32_t render_width = scene::SnapRenderWidth(853, 1280, 640, 1280);
  CHECK(render_width == 848);
  // The tile seam at guest x 640 is a whole render pixel.
  CHECK(std::fmod(640.0 * render_width / 1280.0, 1.0) == 0.0);
  uint64_t wrong = 1;
  std::vector<scene::CaptureRegion> regions = ComposeTwoTiles(render_width, 480, wrong);
  CHECK(regions[0].dest_x == 0 && regions[0].width == 446 && regions[0].height == 480);
  CHECK(regions[1].source_x == 0 && regions[1].dest_x == 424 && regions[1].width == 424);
  CHECK(wrong == 0);
  // Log 249: 2560x1440 for a 5120x2880 output.
  CHECK(scene::SnapRenderWidth(2560, 1280, 2560, 5120) == 2560);
  wrong = 1;
  regions = ComposeTwoTiles(2560, 1440, wrong);
  CHECK(regions[0].dest_x == 0 && regions[0].width == 1344 && regions[0].height == 1440);
  CHECK(regions[1].source_x == 0 && regions[1].dest_x == 1280 && regions[1].width == 1280);
  CHECK(wrong == 0);
}

void TestWriteback() {
  std::printf("write-back regions: staging placement and which resolves get the DLSS output\n");
  const uint32_t pitch_tiles = 16;
  // Each resolve's pixels come from where the encode shader put the same screen pixels.
  int mismatches = 0;
  for (const auto& [screen_x, screen_y] : std::array<std::pair<uint32_t, uint32_t>, 4>{
           {{0, 0}, {640, 0}, {672, 8}, {1192, 704}}}) {
    const scene::StagingPlacement placement = scene::StagingPlacementFor(screen_x, screen_y, pitch_tiles);
    for (uint32_t y = 0; y < 16; ++y) {
      for (uint32_t x = 0; x < 88; ++x) {
        if (screen_x + x >= 1280 || screen_y + y >= 720) {
          continue;
        }
        mismatches += ResolveSourceDword(placement.base_tiles, placement.edram_offset_x_div_8,
                                         placement.edram_offset_y_div_8, pitch_tiles, x, y) !=
                      StagingDword(screen_x + x, screen_y + y, pitch_tiles);
      }
    }
  }
  CHECK(mismatches == 0);
  const scene::StagingPlacement right = scene::StagingPlacementFor(640, 0, pitch_tiles);
  CHECK(right.base_tiles == 8 && right.edram_offset_x_div_8 == 0 && right.edram_offset_y_div_8 == 0);

  const auto rect = [](int32_t x, int32_t y, uint32_t width, uint32_t height) {
    scene::ResolveRect r;
    r.screen_x = x;
    r.screen_y = y;
    r.width = width;
    r.height = height;
    return r;
  };
  // The two tiles overlap by 32 pixels; neither covers the other, so both textures get the DLSS output.
  CHECK((scene::CoveredByLater({rect(0, 0, 672, 720), rect(640, 0, 640, 720)}) == std::vector<bool>{false, false}));
  // Each tile resolved twice: the first resolve of each is covered by its second.
  CHECK((scene::CoveredByLater({rect(0, 0, 672, 720), rect(0, 0, 672, 720), rect(640, 0, 640, 720),
                                rect(640, 0, 640, 720)}) == std::vector<bool>{true, false, true, false}));
  // A full-screen pass drawn into the scene surface first (a reflection) is covered by the two tiles.
  CHECK((scene::CoveredByLater({rect(0, 0, 1280, 720), rect(0, 0, 672, 720), rect(640, 0, 640, 720)}) ==
         std::vector<bool>{true, false, false}));
  // Not when it reaches past them.
  CHECK((scene::CoveredByLater({rect(0, 0, 1280, 736), rect(0, 0, 672, 720), rect(640, 0, 640, 720)}) ==
         std::vector<bool>{false, false, false}));
  CHECK(scene::CoveredByLater({}).empty());
}

void TestDepthDecode() {
  std::printf("D24S8 and D24FS8 depth from EDRAM words, stencil ignored\n");
  CHECK(scene::DecodeDepth(0xFFFFFF00u | 0x5Au, false) == 1.0f);
  CHECK(scene::DecodeDepth(0x00000017u, false) == 0.0f);
  CHECK(std::abs(scene::DecodeDepth(0x80000000u, false) - float(0x800000) / float(0xFFFFFF)) < 1.0e-7f);
  CHECK(scene::DecodeDepth(0x80000000u, false) == scene::DecodeDepth(0x800000FFu, false));
  CHECK(scene::DecodeDepth(0xF00000u << 8, true) == 1.0f);
  CHECK(scene::DecodeDepth(0xE00000u << 8, true) == 0.5f);
  CHECK(scene::DecodeDepth(0xE80000u << 8, true) == 0.75f);
  CHECK(scene::DecodeDepth(0x000000FFu, true) == 0.0f);
  CHECK(scene::DecodeDepth(1u << 8, true) == std::ldexp(1.0f, -34));
  CHECK(scene::DecodeDepth(0x080000u << 8, true) == std::ldexp(1.0f, -15));
  // Monotonic over the whole unorm range, so near and far stay ordered.
  float previous = -1.0f;
  bool monotonic = true;
  for (uint32_t value = 0; value <= 0xFFFFFF; value += 0x10101) {
    const float depth = scene::DecodeDepth(value << 8, false);
    monotonic &= depth > previous;
    previous = depth;
  }
  CHECK(monotonic);
}

scene::Matrix Perspective() {
  const double near_z = 1.0, far_z = 1000.0, f = 1.0 / std::tan(0.5);
  return {f / (16.0 / 9.0), 0, 0, 0, 0, f, 0, 0, 0, 0, far_z / (far_z - near_z), -near_z * far_z / (far_z - near_z),
          0, 0, 1, 0};
}

scene::Matrix Translation(double x, double y, double z) { return {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1}; }

scene::Matrix RotationY(double angle) {
  const double c = std::cos(angle), s = std::sin(angle);
  return {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1};
}

// Where a world point lands, in render pixels, and its depth, through an object-to-clip matrix and MUA1's viewport.
std::array<double, 3> Project(const scene::Matrix& object_to_clip, const std::array<double, 3>& point,
                              const std::array<float, 6>& viewport, const std::array<float, 2>& render_scale) {
  double clip[4] = {};
  for (size_t r = 0; r < 4; ++r) {
    clip[r] = object_to_clip[r * 4 + 0] * point[0] + object_to_clip[r * 4 + 1] * point[1] +
              object_to_clip[r * 4 + 2] * point[2] + object_to_clip[r * 4 + 3];
  }
  return {(clip[0] / clip[3] * viewport[0] + viewport[1]) * render_scale[0],
          (clip[1] / clip[3] * viewport[2] + viewport[3]) * render_scale[1],
          clip[2] / clip[3] * viewport[4] + viewport[5]};
}

void TestCameraMotion() {
  std::printf("camera reprojection: motion when the camera moves, none when it doesn't\n");
  const std::array<float, 6> viewport = {640.0f, 640.0f, -360.0f, 360.0f, 1.0f, 0.0f};
  const std::array<float, 2> render_scale = {848.0f / 1280.0f, 480.0f / 720.0f};
  const scene::Matrix projection = Perspective();
  const scene::Matrix view_previous = scene::Multiply(RotationY(0.00), Translation(0.0, -2.0, 10.0));
  const scene::Matrix view_current = scene::Multiply(RotationY(0.02), Translation(0.3, -2.0, 10.0));
  std::vector<scene::Matrix> worlds;
  for (int i = 0; i < 12; ++i) {
    worlds.push_back(scene::Multiply(Translation(i * 1.5 - 8.0, 0.0, i * 2.0), RotationY(i * 0.3)));
  }

  // Static objects: previous and current object-to-clip matrices only differ by the camera.
  std::vector<std::pair<scene::Matrix, scene::Matrix>> pairs;
  for (const scene::Matrix& world : worlds) {
    pairs.emplace_back(scene::Multiply(projection, scene::Multiply(view_current, world)),
                       scene::Multiply(projection, scene::Multiply(view_previous, world)));
  }
  // Two objects that moved on their own.
  for (int i = 0; i < 2; ++i) {
    const scene::Matrix world = Translation(i * 3.0, 1.0, 5.0);
    pairs.emplace_back(scene::Multiply(projection, scene::Multiply(view_current, world)),
                       scene::Multiply(projection, scene::Multiply(view_previous, Translation(i * 3.0 + 0.5, 1, 5))));
  }
  uint32_t matched = 0, agreeing = 0;
  const std::optional<scene::Matrix> reprojection = scene::EstimateCameraReprojection(pairs, matched, agreeing);
  CHECK(reprojection.has_value());
  CHECK(matched == 14 && agreeing == 12);
  CHECK(reprojection && scene::DistanceFromIdentity(*reprojection) > 1.0e-3);

  // Each pixel's surface, put back in the world through the real current camera and seen by the previous one: the
  // motion is where it was minus where it is, in render pixels, without the jitter.
  if (reprojection) {
    const scene::Matrix current = scene::Multiply(projection, view_current);
    const scene::Matrix previous = scene::Multiply(projection, view_previous);
    scene::Matrix clip_to_world;
    CHECK(scene::Invert(current, clip_to_world));
    double worst = 0.0, largest = 0.0;
    for (const std::array<float, 2>& jitter : std::array<std::array<float, 2>, 2>{{{0.0f, 0.0f}, {0.3f, -0.2f}}}) {
      for (uint32_t y = 7; y < 480; y += 53) {
        for (uint32_t x = 5; x < 848; x += 71) {
          for (float depth : {0.2f, 0.9f, 0.995f}) {
            const double center_x = x + 0.5 - jitter[0], center_y = y + 0.5 - jitter[1];
            const double clip[4] = {(center_x / render_scale[0] - viewport[1]) / viewport[0],
                                    (center_y / render_scale[1] - viewport[3]) / viewport[2],
                                    (depth - viewport[5]) / viewport[4], 1.0};
            std::array<double, 4> world = {};
            for (size_t r = 0; r < 4; ++r) {
              for (size_t c = 0; c < 4; ++c) {
                world[r] += clip_to_world[r * 4 + c] * clip[c];
              }
            }
            const std::array<double, 3> point = {world[0] / world[3], world[1] / world[3], world[2] / world[3]};
            const std::array<double, 3> before = Project(previous, point, viewport, render_scale);
            const std::array<float, 2> motion =
                scene::CameraMotion(*reprojection, viewport, render_scale, jitter, x, y, depth);
            const double expected_x = before[0] - center_x, expected_y = before[1] - center_y;
            worst = std::max({worst, std::abs(motion[0] - expected_x), std::abs(motion[1] - expected_y)});
            largest = std::max({largest, std::abs(expected_x), std::abs(expected_y)});
          }
        }
      }
    }
    CHECK(largest > 1.0);
    CHECK(worst < 1.0e-3);
  }

  // A camera that stands still: the matrix is identity and every pixel's motion is zero.
  pairs.clear();
  for (const scene::Matrix& world : worlds) {
    const scene::Matrix matrix = scene::Multiply(projection, scene::Multiply(view_current, world));
    pairs.emplace_back(matrix, matrix);
  }
  const std::optional<scene::Matrix> still = scene::EstimateCameraReprojection(pairs, matched, agreeing);
  CHECK(still && scene::DistanceFromIdentity(*still) < 1.0e-9);
  float worst_still = 0.0f;
  if (still) {
    for (uint32_t y = 0; y < 480; y += 37) {
      for (uint32_t x = 0; x < 848; x += 41) {
        const std::array<float, 2> motion =
            scene::CameraMotion(*still, viewport, render_scale, {0.25f, -0.4f}, x, y, 0.37f + x * 1.0e-4f);
        worst_still = std::max({worst_still, std::abs(motion[0]), std::abs(motion[1])});
      }
    }
  }
  CHECK(worst_still < 1.0e-4f);

  // Too few static draws agreeing is no camera.
  pairs.resize(1);
  CHECK(!scene::EstimateCameraReprojection(pairs, matched, agreeing));
}

void TestJitterAndSnap() {
  std::printf("Halton jitter and render width snapping\n");
  CHECK(scene::Halton(1, 2) == 0.5f && scene::Halton(2, 2) == 0.25f && scene::Halton(3, 2) == 0.75f);
  CHECK(std::abs(scene::Halton(1, 3) - 1.0f / 3.0f) < 1.0e-6f && std::abs(scene::Halton(2, 3) - 2.0f / 3.0f) < 1.0e-6f);
  for (uint32_t recommended : {853u, 742u, 640u, 427u}) {
    const uint32_t width = scene::SnapRenderWidth(recommended, 1280, 0, 1280);
    CHECK(width % 16 == 0 && std::abs(int32_t(width) - int32_t(recommended)) <= 8);
  }
  // A 1680-wide guest (21:9) snaps to multiples of 21.
  CHECK(scene::SnapRenderWidth(1120, 1680, 0, 1680) % 21 == 0);
}

void TestPreSceneScale() {
  std::printf("targets drawn before the scene: the smallest whole scale with the scene's detail\n");
  // Render resolution 4x (5120x2880): Performance 2560x1440 is exactly 2x.
  CHECK(scene::PreSceneScale(2560, 1280, 4) == 2 && scene::PreSceneScale(1440, 720, 4) == 2);
  // Quality and Balanced ask for about 2.67x and 2.32x: 3x, not 4x.
  const uint32_t quality = scene::SnapRenderWidth(3413, 1280, 2560, 5120);
  CHECK(scene::PreSceneScale(quality, 1280, 4) == 3 && scene::PreSceneScale(1920, 720, 4) == 3);
  const uint32_t balanced = scene::SnapRenderWidth(2969, 1280, 2560, 5120);
  CHECK(scene::PreSceneScale(balanced, 1280, 4) == 3 && scene::PreSceneScale(1670, 720, 4) == 3);
  // DLAA keeps everything at the Render resolution, and so does a scale nothing smaller can match.
  CHECK(scene::PreSceneScale(5120, 1280, 4) == 4);
  CHECK(scene::PreSceneScale(1280, 1280, 2) == 1);
  CHECK(scene::PreSceneScale(scene::SnapRenderWidth(1707, 1280, 1280, 2560), 1280, 2) == 2);
  CHECK(scene::PreSceneScale(1920, 1280, 3) == 2 && scene::PreSceneScale(2560, 1280, 3) == 2);
  CHECK(scene::PreSceneScale(848, 1280, 1) == 1 && scene::PreSceneScale(1280, 1280, 1) == 1);
  // Never less detail than the scene.
  bool enough = true;
  for (uint32_t output_scale = 1; output_scale <= 4; ++output_scale) {
    for (uint32_t render = 320; render <= 1280 * output_scale; render += 16) {
      const uint32_t scale = scene::PreSceneScale(render, 1280, output_scale);
      enough &= scale <= output_scale && (scale * 1280 >= render || scale == output_scale);
    }
  }
  CHECK(enough);

  // Every full-scale host pixel takes the smaller-scale pixel of its own guest pixel, and every one of those is used.
  bool inside = true, all_used = true;
  for (uint32_t output_scale = 2; output_scale <= 4; ++output_scale) {
    for (uint32_t scale = 1; scale < output_scale; ++scale) {
      std::vector<bool> used(64 * scale, false);
      for (uint32_t pixel = 0; pixel < 64 * output_scale; ++pixel) {
        const uint32_t reduced = scene::ReducedPixel(pixel, scale, output_scale);
        const uint32_t guest = pixel / output_scale;
        inside &= reduced >= guest * scale && reduced < (guest + 1) * scale;
        if (reduced < used.size()) {
          used[reduced] = true;
        }
      }
      all_used &= std::find(used.begin(), used.end(), false) == used.end();
    }
  }
  CHECK(inside && all_used);
}

void TestPreSceneTargets() {
  std::printf("which surfaces are drawn at the smaller scale\n");
  using Targets = scene::PreSceneTargets;
  // MUA1's frame in log 249: the k_32_FLOAT 512x512 target (pitch 560) and the 512x512 atlas tiles with their
  // 1024x1024 depth (pitch 1040) before the scene (pitch 800, 2x MSAA); glow (pitch 320) and the composite with the
  // HUD (pitch 1280) after it.
  const uint32_t shadow = Targets::Key(560, 0), atlas = Targets::Key(1040, 0), scene_key = Targets::Key(800, 1);
  const uint32_t glow = Targets::Key(320, 0), composite = Targets::Key(1280, 0);
  Targets targets;
  const auto frame = [&](bool with_scene) {
    targets.NoteDraw(shadow, false);
    targets.NoteDraw(atlas, false);
    targets.NoteDraw(atlas, false);
    targets.NoteDraw(glow, with_scene);
    targets.NoteDraw(composite, with_scene);
    return targets.EndFrame(with_scene, scene_key);
  };
  CHECK(frame(true));
  CHECK(!targets.IsReduced(shadow) && !targets.IsReduced(atlas));
  CHECK(!frame(true));
  // Frames without the scene say nothing about what comes before it.
  for (int i = 0; i < 5; ++i) {
    CHECK(!frame(false));
  }
  CHECK(!targets.IsReduced(shadow));
  CHECK(frame(true));
  CHECK(targets.IsReduced(shadow) && targets.IsReduced(atlas));
  CHECK(!targets.IsReduced(glow) && !targets.IsReduced(composite) && !targets.IsReduced(scene_key));
  // Changes wait for the end of the frame.
  targets.NoteDraw(shadow, true);
  CHECK(targets.IsReduced(shadow));
  CHECK(targets.EndFrame(true, scene_key));
  CHECK(!targets.IsReduced(shadow) && targets.IsReduced(atlas));
  // Once drawn after the scene, never smaller again.
  for (int i = 0; i < 5; ++i) {
    frame(true);
  }
  CHECK(!targets.IsReduced(shadow) && targets.IsReduced(atlas));
  // A texture of it sampled after the scene keeps it too.
  targets.NoteSampledAfterScene(atlas);
  CHECK(targets.EndFrame(true, scene_key));
  CHECK(!targets.IsReduced(atlas));

  // The scene's own surface and pitch 0 never qualify; only so many surfaces are tracked.
  Targets other;
  for (int i = 0; i < 4; ++i) {
    other.NoteDraw(scene_key, false);
    other.NoteDraw(Targets::Key(0, 0), false);
    other.EndFrame(true, scene_key);
  }
  CHECK(!other.IsReduced(scene_key) && !other.IsReduced(Targets::Key(0, 0)));
  for (uint32_t pitch = 16; pitch < 16 * 50; pitch += 16) {
    other.NoteDraw(Targets::Key(pitch, 0), false);
  }
  CHECK(other.surfaces().size() == Targets::kMaxSurfaces);
}

void TestSurfaceRects64bpp() {
  std::printf("pre-scene resolve rectangles of 32bpp and 64bpp surfaces\n");
  int checked = 0, exact = 0;
  for (bool is_64bpp : {false, true}) {
    for (uint32_t msaa = 0; msaa < 3; ++msaa) {
      const uint32_t log2_x = msaa >= 2, log2_y = msaa >= 1;
      for (uint32_t base : {0u, 1216u, 2040u}) {
        for (int32_t x0 : {0, 40, 512, 600}) {
          for (int32_t y0 : {0, 8, 512, 1000}) {
            const uint32_t pitch_tiles = 13;
            const ResolveFields fields = GuestResolveFields(0, pitch_tiles, log2_x, log2_y, x0, y0, 64, 32);
            if ((fields.base_tiles << uint32_t(is_64bpp)) >= 2048 || (uint32_t(x0) + 64) << log2_x > pitch_tiles * 80) {
              continue;  // Past the EDRAM's end or the surface pitch, which GetResolveInfo doesn't produce.
            }
            const uint32_t base_tiles = (base + (fields.base_tiles << uint32_t(is_64bpp))) & 4095;
            const scene::ResolveRect rect = scene::SurfaceRectFor(
                base_tiles, base, pitch_tiles << uint32_t(is_64bpp), is_64bpp, log2_x, log2_y,
                fields.edram_offset_x_div_8, fields.edram_offset_y_div_8, fields.width_div_8, fields.height_div_8);
            ++checked;
            exact += rect.edram_x == x0 && rect.edram_y == y0 && rect.width == 64 && rect.height == 32;
          }
        }
      }
    }
  }
  CHECK(checked > 0 && exact == checked);
}

// The ROV pixel shader's EDRAM dword for a sample, written out step by step after dxbc_translator_om.cpp
// (StartPixelShader_LoadROVParameters): 40-sample half-tiles, depth with them swapped, 64bpp in whole tiles of 40.
uint32_t RovSampleAddress(uint32_t base_tiles, uint32_t pitch_tiles, uint32_t scale, bool depth, bool is_64bpp,
                          uint32_t sample_x, uint32_t sample_y) {
  const uint32_t tile_width = 80 * scale, tile_height = 16 * scale, tile_size = tile_width * tile_height;
  const uint32_t half_width = tile_width / 2;
  const uint32_t half_tile = sample_x / half_width, x_in_half = sample_x % half_width;
  const uint32_t row_origin = sample_y / tile_height * pitch_tiles * tile_size;
  const uint32_t row = sample_y % tile_height * tile_width;
  uint32_t offset;
  if (is_64bpp) {
    offset = row_origin * 2 + half_tile * tile_size + row + x_in_half * 2;
  } else {
    offset = row_origin + half_tile / 2 * tile_size + row + (half_tile & 1) * half_width + x_in_half;
    if (depth) {
      offset = (half_tile & 1) ? offset - half_width : offset + half_width;
    }
  }
  return (base_tiles * tile_size + offset) % (2048 * tile_size);
}

void TestPreSceneStretch() {
  std::printf("a pre-scene resolve stretched to full size reads like the target drawn at full size\n");
  struct Case {
    const char* name;
    uint32_t base;
    uint32_t pitch_tiles;
    uint32_t log2_x, log2_y;
    bool depth, is_64bpp;
    int32_t x0, y0;
    uint32_t width, height;
    uint32_t output_scale, scale;
  };
  const Case cases[] = {
      {"atlas tile at 512,0 (k_8_8_8_8, pitch 1040), 4x drawn at 2x", 0, 13, 0, 0, false, false, 512, 0, 64, 32, 4, 2},
      {"k_32_FLOAT 512x512 corner (pitch 560), 4x drawn at 3x", 0, 7, 0, 0, false, false, 0, 0, 64, 64, 4, 3},
      {"depth at base 1216 (pitch 1040) across the EDRAM's end, 4x drawn at 2x", 1216, 13, 0, 0, true, false, 0, 1008,
       64, 32, 4, 2},
      {"64bpp 2x MSAA, 3x drawn at 2x", 100, 4, 0, 1, false, true, 40, 16, 32, 16, 3, 2},
      {"32bpp 4x MSAA, 2x drawn at 1x", 7, 16, 1, 1, false, false, 24, 8, 32, 16, 2, 1},
      {"depth 2x MSAA, 4x drawn at 2x", 1128, 10, 0, 1, true, false, 640, 0, 40, 16, 4, 2},
      {"k_32_FLOAT 512x512 corner (pitch 560), 4x drawn at 1x", 0, 7, 0, 0, false, false, 0, 0, 64, 64, 4, 1},
      {"atlas tile at 512,512 (k_8_8_8_8, pitch 1040), 4x drawn at 1x", 0, 13, 0, 0, false, false, 512, 512, 64, 32,
       4, 1},
      {"atlas depth at base 1216 (pitch 1040) across the EDRAM's end, 4x drawn at 1x", 1216, 13, 0, 0, true, false, 0,
       1008, 64, 32, 4, 1},
  };
  for (const Case& c : cases) {
    const uint32_t s = c.output_scale, words = c.is_64bpp ? 2 : 1;
    const uint32_t samples_x = 1u << c.log2_x, samples_y = 1u << c.log2_y;
    const ResolveFields fields =
        GuestResolveFields(0, c.pitch_tiles, c.log2_x, c.log2_y, c.x0, c.y0, c.width, c.height);
    const uint32_t shift = uint32_t(c.is_64bpp);
    const scene::StretchLayout layout = scene::StretchLayoutFor(
        (c.base + (fields.base_tiles << shift)) & 4095, c.base, c.pitch_tiles << shift, c.depth, c.is_64bpp, c.log2_x,
        c.log2_y, fields.edram_offset_x_div_8, fields.edram_offset_y_div_8, fields.width_div_8, fields.height_div_8, s,
        s);
    const bool rect_right = layout.rect_origin[0] == uint32_t(c.x0) * s &&
                            layout.rect_origin[1] == uint32_t(c.y0) * s && layout.rect_size[0] == c.width * s &&
                            layout.rect_size[1] == c.height * s;
    // Each guest pixel's content, per sample and word.
    const auto value = [](uint32_t guest_x, uint32_t guest_y, uint32_t sample, uint32_t word) {
      return guest_x | guest_y << 12 | sample << 24 | word << 28;
    };
    // Draws at the smaller scale fill each guest pixel's scale x scale host pixels from the surface's origin.
    std::unordered_map<uint32_t, uint32_t> edram;
    const uint32_t guest_x_end = uint32_t(c.x0) + c.width + 8, guest_y_end = uint32_t(c.y0) + c.height + 8;
    for (uint32_t gy = uint32_t(std::max(c.y0 - 8, 0)); gy < guest_y_end; ++gy) {
      for (uint32_t gx = uint32_t(std::max(c.x0 - 8, 0)); gx < guest_x_end; ++gx) {
        for (uint32_t hy = gy * c.scale; hy < (gy + 1) * c.scale; ++hy) {
          for (uint32_t hx = gx * c.scale; hx < (gx + 1) * c.scale; ++hx) {
            for (uint32_t sy = 0; sy < samples_y; ++sy) {
              for (uint32_t sx = 0; sx < samples_x; ++sx) {
                const uint32_t address = RovSampleAddress(c.base, c.pitch_tiles, s, c.depth, c.is_64bpp,
                                                          hx * samples_x + sx, hy * samples_y + sy);
                for (uint32_t word = 0; word < words; ++word) {
                  edram[address + word] = value(gx, gy, sy * 2 + sx, word);
                }
              }
            }
          }
        }
      }
    }
    // scene_edram_stretch.cs.hlsl, then what the resolve reads at full scale.
    std::unordered_map<uint32_t, uint32_t> stretched;
    for (uint32_t y = 0; y < layout.rect_size[1]; ++y) {
      for (uint32_t x = 0; x < layout.rect_size[0]; ++x) {
        const uint32_t px = layout.rect_origin[0] + x, py = layout.rect_origin[1] + y;
        const uint32_t qx = scene::ReducedPixel(px, c.scale, s), qy = scene::ReducedPixel(py, c.scale, s);
        for (uint32_t sy = 0; sy < samples_y; ++sy) {
          for (uint32_t sx = 0; sx < samples_x; ++sx) {
            const uint32_t dest = scene::StretchSampleAddress(layout, s, s, px * samples_x + sx, py * samples_y + sy);
            const uint32_t source =
                scene::StretchSampleAddress(layout, s, s, qx * samples_x + sx, qy * samples_y + sy);
            for (uint32_t word = 0; word < words; ++word) {
              const auto it = edram.find(source + word);
              stretched[dest + word] = it == edram.end() ? 0xFFFFFFFFu : it->second;
            }
          }
        }
      }
    }
    uint64_t wrong = 0, read = 0;
    for (uint32_t py = uint32_t(c.y0) * s; py < (uint32_t(c.y0) + c.height) * s; ++py) {
      for (uint32_t px = uint32_t(c.x0) * s; px < (uint32_t(c.x0) + c.width) * s; ++px) {
        for (uint32_t sy = 0; sy < samples_y; ++sy) {
          for (uint32_t sx = 0; sx < samples_x; ++sx) {
            const uint32_t address = RovSampleAddress(c.base, c.pitch_tiles, s, c.depth, c.is_64bpp,
                                                      px * samples_x + sx, py * samples_y + sy);
            for (uint32_t word = 0; word < words; ++word) {
              const auto it = stretched.find(address + word);
              ++read;
              wrong += it == stretched.end() || it->second != value(px / s, py / s, sy * 2 + sx, word);
            }
          }
        }
      }
    }
    uint64_t sampled_wrong = 0;
    for (uint32_t i = 0; i < 997; ++i) {
      const double u = (i + 0.37) / 997.0, v = (i * 7 % 997 + 0.61) / 997.0;
      const uint32_t native_x = uint32_t(c.x0) + uint32_t(u * c.width), native_y = uint32_t(c.y0) + uint32_t(v * c.height);
      const uint32_t px = uint32_t(c.x0) * s + uint32_t(u * c.width * s);
      const uint32_t py = uint32_t(c.y0) * s + uint32_t(v * c.height * s);
      const auto it = stretched.find(
          RovSampleAddress(c.base, c.pitch_tiles, s, c.depth, c.is_64bpp, px * samples_x, py * samples_y));
      sampled_wrong += it == stretched.end() || it->second != value(native_x, native_y, 0, 0);
    }
    CHECK(sampled_wrong == 0);
    std::printf("  %s: %llu samples read, %llu wrong\n", c.name, (unsigned long long)read, (unsigned long long)wrong);
    CHECK(rect_right);
    CHECK(read > 0 && wrong == 0);
  }
}

void TestDepthPacking() {
  std::printf("D24S8 and D24FS8 as the ROV writes them, read back by the DLSS depth decode\n");
  // dxbc_translator_om.cpp: unorm depth is round_ne(z * 0xFFFFFF), float depth xenos::Float32To20e4 with rounding to
  // nearest even, inserted in bits 8:31 (BFI 24 at 8) above the stencil (BFI 8 at 0).
  const auto pack = [](uint32_t depth24, uint32_t stencil) { return depth24 << 8 | (stencil & 0xFF); };
  double worst_unorm = 0.0, worst_float = 0.0;
  bool float_exact = true;
  for (uint32_t i = 0; i <= 4096; ++i) {
    const float z = float(i) / 4096.0f;
    const uint32_t unorm = uint32_t(std::nearbyint(double(z) * 16777215.0));
    worst_unorm = std::max(worst_unorm, std::abs(double(scene::DecodeDepth(pack(unorm, i), false)) - z));
    const uint32_t f24 = rex::graphics::xenos::Float32To20e4(z, true);
    const float decoded = scene::DecodeDepth(pack(f24, i * 7), true);
    float_exact &= decoded == rex::graphics::xenos::Float20e4To32(f24);
    worst_float = std::max(worst_float, std::abs(double(decoded) - z) / std::max(double(z), 1.0e-6));
  }
  // Half a unit of the 24 bits, plus the float the decode works in.
  CHECK(worst_unorm <= 1.0 / 16777215.0);
  CHECK(float_exact);
  CHECK(worst_float < 1.0e-5);
  // The whole float24 range decodes like the renderer's own conversion.
  bool all_float_exact = true;
  for (uint32_t f24 = 0; f24 < 0x1000000; f24 += 0x3FF) {
    all_float_exact &= scene::DecodeDepth(pack(f24, 0x5A), true) == rex::graphics::xenos::Float20e4To32(f24);
  }
  CHECK(all_float_exact);
  CHECK(scene::DecodeDepth(pack(0xFFFFFF, 0), false) == 1.0f && scene::DecodeDepth(pack(0, 0xFF), false) == 0.0f);
}

void TestScenePlanes() {
  std::printf("which resolve the DLSS depth comes from: MUA1 resolves each tile twice\n");
  using Planes = scene::ScenePlanes;
  const uint32_t both = Planes::kColor | Planes::kDepth;
  // Log 251: per tile S199 C S9 C. The 9 draws between write depth at the clear value over the whole tile, and the second
  // resolve clears both; depth comes from the first resolve, color from the last.
  for (const bool between_writes_depth : {true, false}) {
    Planes planes;
    planes.NoteSceneDraw(true);
    planes.NoteSceneDraw(false);
    CHECK(planes.Take(false) == both);
    planes.AfterResolve(false, false);
    for (int i = 0; i < 9; ++i) {
      planes.NoteSceneDraw(between_writes_depth);
    }
    CHECK(planes.Take(false) == Planes::kColor);
    planes.AfterResolve(true, true);
    // The second tile.
    planes.NoteSceneDraw(true);
    CHECK(planes.Take(false) == both);
    planes.AfterResolve(false, false);
    planes.NoteSceneDraw(between_writes_depth);
    CHECK(planes.Take(false) == Planes::kColor);
    planes.AfterResolve(true, true);
    planes.EndFrame();
  }
  // A first resolve that clears depth: draws writing it again are taken at the next resolve.
  Planes cleared;
  cleared.NoteSceneDraw(true);
  CHECK(cleared.Take(false) == both);
  cleared.AfterResolve(false, true);
  cleared.NoteSceneDraw(true);
  CHECK(cleared.Take(false) == both);
  // A frame that writes no depth takes what the EDRAM holds once after each clear.
  Planes no_depth;
  no_depth.NoteSceneDraw(false);
  CHECK(no_depth.Take(false) == both);
  no_depth.NoteSceneDraw(false);
  CHECK(no_depth.Take(false) == Planes::kColor);
  no_depth.AfterResolve(true, true);
  no_depth.NoteSceneDraw(false);
  CHECK(no_depth.Take(false) == both);
  // A depth resolve takes only depth.
  Planes depth_resolve;
  depth_resolve.NoteSceneDraw(true);
  CHECK(depth_resolve.Take(true) == Planes::kDepth);
}

// The scene's depth as the ROV writes it at the scene's scale inside a larger EDRAM scale, then taken into the DLSS
// depth by the capture: two predicated tiles, each resolved twice, with draws between that rewrite depth to the clear
// value (log 251), and the second resolve clearing both.
struct DepthComposeResult {
  uint64_t wrong = 0;
  uint64_t uncovered = 0;
  uint64_t non_clear = 0;
};

enum class DepthRule { kScenePlanes, kAfterAnySceneDraw, kAfterDepthWrites };

DepthComposeResult ComposeDepth(uint32_t output_scale, uint32_t render_width, uint32_t render_height, bool geometry,
                                DepthRule rule) {
  const uint32_t guest_width = 1280, guest_height = 720, depth_base = 1128, pitch_tiles = 10, log2_y = 1;
  const double scale_x = double(render_width) / guest_width, scale_y = double(render_height) / guest_height;
  const auto expected = [&](uint32_t x, uint32_t y) {
    return geometry ? 0.25f + 0.5f * float(x * 7 + y * 13) / float(render_width * 7 + render_height * 13) : 1.0f;
  };
  const uint32_t clear_word = 0xFFFFFF00u;
  scene::StretchLayout layout;
  const uint32_t tile_dwords = 80 * output_scale * 16 * output_scale;
  layout.base_dwords = depth_base * tile_dwords;
  layout.pitch_dwords = pitch_tiles * tile_dwords;
  layout.layout = 2;
  struct Tile {
    scene::ResolveRect rect;
    int32_t window_x;
  };
  const std::array<Tile, 2> tiles = {{
      {scene::ResolveRectFor(0, 0, pitch_tiles, 0, log2_y, 0, 0, 672 / 8, 720 / 8, 0, 0), 0},
      {scene::ResolveRectFor(0, 0, pitch_tiles, 0, log2_y, 0, 0, 640 / 8, 720 / 8, -640, 0), -640},
  }};
  std::vector<float> depth(size_t(render_width) * render_height, -1.0f);
  scene::ScenePlanes planes;
  bool written_since_clear = false;
  for (const Tile& tile : tiles) {
    std::unordered_map<uint32_t, uint32_t> edram;
    const scene::CaptureRegion region =
        scene::CaptureRegionFor(tile.rect, scale_x, scale_y, render_width, render_height);
    const int32_t shift = int32_t(std::lround(scale_x * tile.window_x));
    // The tile's scene draws, at the scene's scale from the EDRAM origin, both samples of each render pixel.
    planes.NoteSceneDraw(true);
    written_since_clear = true;
    for (uint32_t y = 0; y < region.height; ++y) {
      for (uint32_t x = 0; x < region.width; ++x) {
        const uint32_t edram_x = uint32_t(region.source_x) + x, edram_y = uint32_t(region.source_y) + y;
        const float z = expected(uint32_t(int32_t(edram_x) - shift), edram_y);
        const uint32_t word = uint32_t(std::nearbyint(double(z) * 16777215.0)) << 8 | 0x3C;
        for (uint32_t sample = 0; sample < 2; ++sample) {
          edram[RovSampleAddress(depth_base, pitch_tiles, output_scale, true, false, edram_x,
                                 (edram_y << log2_y) + sample)] = word;
        }
      }
    }
    for (int resolve = 0; resolve < 2; ++resolve) {
      const uint32_t planes_taken = planes.Take(false);
      const bool take = rule == DepthRule::kScenePlanes    ? (planes_taken & scene::ScenePlanes::kDepth) != 0
                        : rule == DepthRule::kAfterDepthWrites ? written_since_clear
                                                               : true;
      if (take) {
        // scene_capture.cs.hlsl: sample 0 of each render pixel.
        for (uint32_t y = 0; y < region.height; ++y) {
          for (uint32_t x = 0; x < region.width; ++x) {
            const auto it = edram.find(scene::StretchSampleAddress(layout, output_scale, output_scale,
                                                                   uint32_t(region.source_x) + x,
                                                                   (uint32_t(region.source_y) + y) << log2_y));
            depth[size_t(region.dest_y + int32_t(y)) * render_width + uint32_t(region.dest_x) + x] =
                scene::DecodeDepth(it == edram.end() ? clear_word : it->second, false);
          }
        }
      }
      planes.AfterResolve(resolve == 1, resolve == 1);
      if (resolve == 1) {
        written_since_clear = false;
        edram.clear();
      } else {
        // The draws between: depth at the clear value over the whole tile.
        for (int i = 0; i < 9; ++i) {
          planes.NoteSceneDraw(true);
        }
        written_since_clear = true;
        edram.clear();
      }
    }
  }
  DepthComposeResult result;
  for (uint32_t y = 0; y < render_height; ++y) {
    for (uint32_t x = 0; x < render_width; ++x) {
      const float value = depth[size_t(y) * render_width + x];
      result.uncovered += value < 0.0f;
      result.non_clear += value >= 0.0f && value != 1.0f;
      result.wrong += value >= 0.0f && std::abs(value - expected(x, y)) > 1.0e-6f;
    }
  }
  return result;
}

void TestDepthCompose() {
  std::printf("the DLSS depth composed from MUA1's two tiles at the scene's scale\n");
  struct Size {
    uint32_t output_scale, render_width, render_height;
  };
  // 1280x720 output (Performance and Quality), 5120x2880 output with the 2560x1440 scene, and DLAA at 4x.
  for (const Size& size : {Size{1, 640, 360}, Size{1, 848, 480}, Size{4, 2560, 1440}, Size{4, 5120, 2880}}) {
    const DepthComposeResult result =
        ComposeDepth(size.output_scale, size.render_width, size.render_height, true, DepthRule::kScenePlanes);
    std::printf("  %ux scale, %ux%u: %llu wrong, %llu uncovered, %llu with geometry\n", size.output_scale,
                size.render_width, size.render_height, (unsigned long long)result.wrong,
                (unsigned long long)result.uncovered, (unsigned long long)result.non_clear);
    CHECK(result.wrong == 0 && result.uncovered == 0);
    CHECK(result.non_clear == uint64_t(size.render_width) * size.render_height);
  }
  // Without geometry the DLSS depth is the clear value everywhere.
  const DepthComposeResult clear = ComposeDepth(4, 2560, 1440, false, DepthRule::kScenePlanes);
  CHECK(clear.wrong == 0 && clear.uncovered == 0 && clear.non_clear == 0);
  // The two earlier rules, after any scene draw (logs 249, 250) and after draws writing depth (log 251), both took the
  // rewritten depth from the second resolve.
  for (const DepthRule rule : {DepthRule::kAfterAnySceneDraw, DepthRule::kAfterDepthWrites}) {
    const DepthComposeResult before = ComposeDepth(4, 2560, 1440, true, rule);
    CHECK(before.uncovered == 0 && before.non_clear == 0);
  }
}

void TestNativePreSceneTargets() {
  std::printf("pre-scene targets only the scene samples go to 1x in Performance; the others stay\n");
  using Targets = scene::PreSceneTargets;
  const uint32_t shadow = Targets::Key(560, 0), atlas = Targets::Key(1040, 0), unsampled = Targets::Key(640, 0);
  const uint32_t shared = Targets::Key(720, 0), scene_key = Targets::Key(800, 1), composite = Targets::Key(1280, 0);
  for (const bool native_allowed : {true, false}) {
    Targets targets;
    for (int frame = 0; frame < 8; ++frame) {
      for (const uint32_t key : {shadow, atlas, unsampled, shared}) {
        targets.NoteDraw(key, false);
      }
      targets.NoteSampledByScene(shadow);
      targets.NoteSampledByScene(atlas);
      targets.NoteSampledByScene(shared);
      targets.NoteDraw(composite, true);
      if (frame == 5) {
        targets.NoteSampledAfterScene(shared);
      }
      targets.EndFrame(true, scene_key, native_allowed);
      if (frame == 2) {
        // Reduced from here, and native once the scene sampled them in three reduced frames.
        CHECK(targets.IsReduced(shadow) && !targets.IsNative(shadow));
        CHECK(targets.AwaitsSceneSampling() == native_allowed);
      }
    }
    CHECK(targets.IsNative(shadow) == native_allowed && targets.IsNative(atlas) == native_allowed);
    CHECK(targets.IsReduced(shadow) && targets.IsReduced(atlas));
    // Never sampled by the scene: reduced, not native.
    CHECK(targets.IsReduced(unsampled) && !targets.IsNative(unsampled));
    // Sampled after the scene once: back to the full scale for the session.
    CHECK(!targets.IsReduced(shared) && !targets.IsNative(shared));
    CHECK(!targets.IsNative(composite) && !targets.IsNative(scene_key));
    // Still waiting only for the one the scene never samples.
    CHECK(targets.AwaitsSceneSampling() == native_allowed);
  }
}

// AMD's halton() of ffx_fsr3upscaler.cpp (FidelityFX SDK 1.1.4), which ffxFsr3UpscalerGetJitterOffset uses.
float AmdHalton(int32_t index, int32_t base) {
  float f = 1.0f, result = 0.0f;
  for (int32_t current = index; current > 0;) {
    f /= float(base);
    result = result + f * float(current % base);
    current = int32_t(std::floor(float(current) / float(base)));
  }
  return result;
}

// FSR 3.1's view depth from a depth buffer value (setupDeviceDepthToViewSpaceDepthParams and GetViewSpaceDepth of
// FidelityFX SDK 1.1.4), for a context without DEPTH_INFINITE.
float AmdViewDepth(float device_depth, float camera_near, float camera_far, bool inverted) {
  float min_plane = std::min(camera_near, camera_far);
  float max_plane = std::max(camera_near, camera_far);
  if (inverted) {
    std::swap(min_plane, max_plane);
  }
  const float q = max_plane / (min_plane - max_plane);
  const float factor0 = -1.0f * q;
  const float factor1 = q * min_plane;
  return factor1 / (device_depth - factor0);
}

// A projection whose depth buffer value is alpha + beta / view depth, after a viewport z scale and offset of 1 and 0.
scene::Matrix DepthProjection(double alpha, double beta, double fov_y, double aspect) {
  const double f = 1.0 / std::tan(fov_y / 2.0);
  return {f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, alpha, beta, 0, 0, 1, 0};
}

void TestFsr3RenderSizes() {
  std::printf("FSR 3.1's scene sizes: AMD's ratios, snapped like DLSS's, the same as the launcher shows\n");
  CHECK(scene::kFsr3UpscaleRatios[0] == 1.0f && scene::kFsr3UpscaleRatios[1] == 1.5f &&
        scene::kFsr3UpscaleRatios[2] == 1.7f && scene::kFsr3UpscaleRatios[3] == 2.0f &&
        scene::kFsr3UpscaleRatios[4] == 3.0f);
  // FfxApiUpscaleQualityMode's order is the launcher's.
  CHECK(uint32_t(mua::UpscaleQuality::kNative) == 0 && uint32_t(mua::UpscaleQuality::kQuality) == 1 &&
        uint32_t(mua::UpscaleQuality::kBalanced) == 2 && uint32_t(mua::UpscaleQuality::kPerformance) == 3 &&
        uint32_t(mua::UpscaleQuality::kUltraPerformance) == 4);
  CHECK(scene::Fsr3RenderSize(3840, 3) == 1920 && scene::Fsr3RenderSize(2160, 3) == 1080);
  CHECK(scene::Fsr3RenderSize(3840, 2) == 2258 && scene::Fsr3RenderSize(2160, 2) == 1270);
  CHECK(scene::Fsr3RenderSize(5120, 1) == 3413 && scene::Fsr3RenderSize(1280, 4) == 426);
  for (uint32_t scale = 1; scale <= 4; ++scale) {
    const uint32_t output_width = 1280 * scale, output_height = 720 * scale;
    for (uint32_t mode = 0; mode < 5; ++mode) {
      const uint32_t width = mode ? scene::SnapRenderWidth(scene::Fsr3RenderSize(output_width, mode), 1280, 0,
                                                           output_width)
                                  : output_width;
      const uint32_t height = scene::Fsr3RenderSize(output_height, mode);
      const mua::WindowSize launcher =
          mua::Fsr3SceneSize({int(output_width), int(output_height)}, 1280, mua::UpscaleQuality(mode));
      CHECK(int(width) == launcher.width && int(height) == launcher.height);
      CHECK(width <= output_width && height <= output_height);
      // MUA1's tile seam at guest x 640 is a whole render pixel.
      CHECK(std::fmod(640.0 * width / 1280.0, 1.0) == 0.0);
    }
  }
}

void TestFsr3Jitter() {
  std::printf("FSR 3.1's jitter: AMD's phase count and Halton(2,3) offsets\n");
  CHECK(scene::Fsr3JitterPhaseCount(3840, 3840) == 8);
  CHECK(scene::Fsr3JitterPhaseCount(2560, 3840) == 18);
  CHECK(scene::Fsr3JitterPhaseCount(2256, 3840) == 23);
  CHECK(scene::Fsr3JitterPhaseCount(1920, 3840) == 32);
  CHECK(scene::Fsr3JitterPhaseCount(1280, 3840) == 72);
  CHECK(scene::Fsr3JitterPhaseCount(1712, 5120) == 71);
  for (int32_t index = 1; index <= 256; ++index) {
    CHECK(scene::Halton(uint32_t(index), 2) == AmdHalton(index, 2));
    CHECK(scene::Halton(uint32_t(index), 3) == AmdHalton(index, 3));
  }
  // The scene upscaler's offset of frame n is AMD's ffxFsr3UpscalerGetJitterOffset(n, phases): in (-0.5, 0.5), in render
  // pixels, the sequence repeating after the phase count.
  const uint32_t phases = scene::Fsr3JitterPhaseCount(1920, 3840);
  for (uint64_t frame = 0; frame < 3 * phases; ++frame) {
    const uint32_t index = uint32_t(frame % phases) + 1;
    const float x = scene::Halton(index, 2) - 0.5f, y = scene::Halton(index, 3) - 0.5f;
    CHECK(x == AmdHalton(int32_t(frame % phases) + 1, 2) - 0.5f && y == AmdHalton(int32_t(frame % phases) + 1, 3) - 0.5f);
    CHECK(std::abs(x) < 0.5f && std::abs(y) < 0.5f);
  }
}

void TestFsr3Depth() {
  std::printf("FSR 3.1's camera: near, far and field of view from the game's matrices, read back by FSR's own depth\n");
  const double near_z = 4.0, far_z = 6000.0, fov = 0.9, aspect = 16.0 / 9.0;
  const scene::Matrix world = scene::Multiply(scene::Multiply(Translation(30.0, -5.0, 200.0), RotationY(0.7)),
                                              scene::Matrix{2.5, 0, 0, 0, 0, 2.5, 0, 0, 0, 0, 2.5, 0, 0, 0, 0, 1});
  const scene::Matrix view = scene::Multiply(RotationY(-0.3), Translation(-12.0, 3.0, 40.0));
  struct Case {
    const char* name;
    double alpha, beta;
    bool inverted;
    bool infinite;
  };
  const Case cases[] = {
      {"0 near, 1 far", far_z / (far_z - near_z), -near_z * far_z / (far_z - near_z), false, false},
      {"1 near, 0 far", -near_z / (far_z - near_z), near_z * far_z / (far_z - near_z), true, false},
      {"0 near, infinite far", 1.0, -near_z, false, true},
      {"1 near, infinite far", 0.0, near_z, true, true},
  };
  for (const Case& c : cases) {
    const scene::Matrix object_to_clip =
        scene::Multiply(DepthProjection(c.alpha, c.beta, fov, aspect), scene::Multiply(view, world));
    const std::optional<scene::CameraProjection> camera = scene::CameraProjectionFor(object_to_clip, 1.0, 0.0, c.inverted);
    CHECK(camera.has_value());
    if (!camera) {
      std::printf("    %s: no camera\n", c.name);
      continue;
    }
    CHECK(std::abs(camera->near_depth - near_z) < 1.0e-6 * near_z);
    CHECK(c.infinite ? std::isinf(camera->far_depth) : std::abs(camera->far_depth - far_z) < 1.0e-6 * far_z);
    CHECK(std::abs(camera->fov_y - fov) < 1.0e-9);
    // What the scene upscaler passes: FLT_MAX for an infinite far plane.
    const float camera_far = std::isfinite(camera->far_depth) ? float(camera->far_depth) : FLT_MAX;
    double worst = 0.0;
    for (const double view_depth : {5.0, 12.0, 80.0, 400.0, 2500.0, 5900.0}) {
      const float device_depth = float(c.alpha + c.beta / view_depth);
      const float fsr = AmdViewDepth(device_depth, float(camera->near_depth), camera_far, c.inverted);
      worst = std::max(worst, std::abs(fsr - view_depth) / view_depth);
    }
    std::printf("    %s: near %.4g, far %.4g, FSR view depth error %.2g\n", c.name, camera->near_depth,
                camera->far_depth, worst);
    CHECK(worst < 2.0e-3);
  }
  // A viewport that maps depth 0 to 1 the other way round is the same camera with inverted depth.
  const scene::Matrix standard = scene::Multiply(DepthProjection(cases[0].alpha, cases[0].beta, fov, aspect), view);
  const std::optional<scene::CameraProjection> flipped = scene::CameraProjectionFor(standard, -1.0, 1.0, true);
  CHECK(flipped && std::abs(flipped->near_depth - near_z) < 1.0e-6 * near_z &&
        std::abs(flipped->far_depth - far_z) < 1.0e-6 * far_z);
  // An orthographic or degenerate matrix gives no camera.
  CHECK(!scene::CameraProjectionFor(scene::kIdentity, 1.0, 0.0, false));
  // The projection most draws share; a sky drawn with its own far plane and a stretched object don't win.
  const scene::CameraProjection shared = {near_z, far_z, fov};
  const std::vector<scene::CameraProjection> projections = {shared, {near_z, 1.0e6, fov}, shared, {near_z, far_z, 1.2},
                                                            shared};
  const std::optional<scene::CameraProjection> consensus = scene::ConsensusCameraProjection(projections);
  CHECK(consensus && scene::NearlyEqual(*consensus, shared));
  CHECK(!scene::ConsensusCameraProjection({}));
}

void TestFsr3MotionVectors() {
  std::printf("FSR 3.1 reprojects the camera motion vectors to where each surface was, whatever the jitter\n");
  const std::array<float, 6> viewport = {640.0f, 640.0f, -360.0f, 360.0f, 1.0f, 0.0f};
  const uint32_t render_width = 1920, render_height = 1080;
  const std::array<float, 2> render_scale = {float(render_width) / 1280.0f, float(render_height) / 720.0f};
  const scene::Matrix projection = Perspective();
  const scene::Matrix previous = scene::Multiply(projection, scene::Multiply(RotationY(0.0), Translation(0, -2, 10)));
  const scene::Matrix current = scene::Multiply(projection, scene::Multiply(RotationY(0.03), Translation(0.4, -2, 9.5)));
  scene::Matrix current_inverse;
  CHECK(scene::Invert(current, current_inverse));
  const scene::Matrix clip_to_previous_clip = scene::Multiply(previous, current_inverse);
  double worst = 0.0;
  for (const std::array<float, 2>& jitter :
       std::array<std::array<float, 2>, 3>{{{0.0f, 0.0f}, {0.4375f, -0.2963f}, {-0.3125f, 0.1852f}}}) {
    for (uint32_t y = 3; y < render_height; y += 97) {
      for (uint32_t x = 11; x < render_width; x += 131) {
        const float depth = 0.97f + 0.02f * float(x) / float(render_width);
        const std::array<float, 2> motion =
            scene::CameraMotion(clip_to_previous_clip, viewport, render_scale, jitter, x, y, depth);
        // FSR: the sample of render pixel (x, y) holds the scene at the pixel center minus the jitter; its previous
        // position is that UV plus motion * motionVectorScale / renderSize, with motionVectorScale 1.
        const double uv_x = (x + 0.5 - jitter[0]) / render_width, uv_y = (y + 0.5 - jitter[1]) / render_height;
        const double reprojected_x = (uv_x + motion[0] * 1.0 / render_width) * render_width;
        const double reprojected_y = (uv_y + motion[1] * 1.0 / render_height) * render_height;
        // The same surface point seen by the previous camera.
        const double ndc[4] = {((x + 0.5 - jitter[0]) / render_scale[0] - viewport[1]) / viewport[0],
                               ((y + 0.5 - jitter[1]) / render_scale[1] - viewport[3]) / viewport[2],
                               (depth - viewport[5]) / viewport[4], 1.0};
        double world[4] = {};
        for (size_t r = 0; r < 4; ++r) {
          for (size_t c = 0; c < 4; ++c) {
            world[r] += current_inverse[r * 4 + c] * ndc[c];
          }
        }
        const std::array<double, 3> before =
            Project(previous, {world[0] / world[3], world[1] / world[3], world[2] / world[3]}, viewport, render_scale);
        worst = std::max({worst, std::abs(reprojected_x - before[0]), std::abs(reprojected_y - before[1])});
      }
    }
  }
  CHECK(worst < 2.0e-2);
}

void TestFsr3SceneComposition() {
  std::printf("FSR 3.1's scene sizes compose MUA1's two tiles and their depth without gaps\n");
  struct Size {
    uint32_t output_scale, render_width, render_height;
  };
  // 4K Performance (the first manual test), Quality, Balanced and Ultra Performance; 5K and 1440p Ultra Performance.
  for (const Size& size : {Size{3, 1920, 1080}, Size{3, 2560, 1440}, Size{3, 2256, 1270}, Size{3, 1280, 720},
                           Size{4, 1712, 960}, Size{2, 848, 480}}) {
    uint64_t wrong = 1;
    const std::vector<scene::CaptureRegion> regions = ComposeTwoTiles(size.render_width, size.render_height, wrong);
    CHECK(wrong == 0);
    CHECK(regions.size() == 2 && regions[1].dest_x == size.render_width / 2);
    const DepthComposeResult depth =
        ComposeDepth(size.output_scale, size.render_width, size.render_height, true, DepthRule::kScenePlanes);
    CHECK(depth.wrong == 0 && depth.uncovered == 0);
    CHECK(depth.non_clear == uint64_t(size.render_width) * size.render_height);
  }
}

void TestFsr3SceneAlpha() {
  std::printf("FSR 3.1's write-back takes the scene's own alpha at each output pixel's unjittered point\n");
  const uint32_t render_width = 1920, render_height = 1080, output_width = 3840, output_height = 2160;
  const std::array<float, 2> source_scale = {float(render_width) / output_width, float(render_height) / output_height};
  const std::array<float, 4> source_clamp = {0.0f, 0.0f, float(render_width), float(render_height)};
  // The game's alpha over the screen, in unjittered render pixels; a linear ramp, which bilinear sampling keeps exactly.
  const auto scene_alpha = [](double x, double y) { return 0.2 + 0.003 * x + 0.001 * y; };
  double worst = 0.0, worst_unjittered = 0.0;
  for (const std::array<float, 2>& jitter :
       std::array<std::array<float, 2>, 3>{{{0.0f, 0.0f}, {0.4375f, -0.2963f}, {-0.3125f, 0.1852f}}}) {
    // Each render pixel holds the scene at its center minus the jitter.
    const auto rendered = [&](int32_t x, int32_t y) {
      x = std::clamp<int32_t>(x, 0, render_width - 1);
      y = std::clamp<int32_t>(y, 0, render_height - 1);
      return scene_alpha(x + 0.5 - jitter[0], y + 0.5 - jitter[1]);
    };
    const auto bilinear = [&](float px, float py) {
      const double fx = px - 0.5, fy = py - 0.5;
      const int32_t x0 = int32_t(std::floor(fx)), y0 = int32_t(std::floor(fy));
      const double tx = fx - x0, ty = fy - y0;
      return (rendered(x0, y0) * (1 - tx) + rendered(x0 + 1, y0) * tx) * (1 - ty) +
             (rendered(x0, y0 + 1) * (1 - tx) + rendered(x0 + 1, y0 + 1) * tx) * ty;
    };
    for (uint32_t y = 4; y < output_height - 4; y += 113) {
      for (uint32_t x = 4; x < output_width - 4; x += 157) {
        const std::array<float, 2> source = scene::SceneAlphaSourcePosition(x, y, source_scale, jitter, source_clamp);
        const double expected = scene_alpha((x + 0.5) * source_scale[0], (y + 0.5) * source_scale[1]);
        worst = std::max(worst, std::abs(bilinear(source[0], source[1]) - expected));
        const std::array<float, 2> unjittered =
            scene::SceneAlphaSourcePosition(x, y, source_scale, {0.0f, 0.0f}, source_clamp);
        worst_unjittered = std::max(worst_unjittered, std::abs(bilinear(unjittered[0], unjittered[1]) - expected));
      }
    }
  }
  CHECK(worst < 1.0e-4);
  // Ignoring the jitter would read the alpha up to half a render pixel off.
  CHECK(worst_unjittered > 5.0e-4);
  // Within the render size at the edges, like the stretched copies (mode 1) without jitter.
  const std::array<float, 2> corner = scene::SceneAlphaSourcePosition(0, 0, source_scale, {-0.4f, -0.4f}, source_clamp);
  CHECK(corner[0] == 0.5f && corner[1] == 0.5f);
  const std::array<float, 2> far_corner =
      scene::SceneAlphaSourcePosition(output_width - 1, output_height - 1, source_scale, {0.4f, 0.4f}, source_clamp);
  CHECK(far_corner[0] == render_width - 0.5f && far_corner[1] == render_height - 0.5f);
  const std::array<float, 2> center =
      scene::SceneAlphaSourcePosition(1000, 600, source_scale, {0.0f, 0.0f}, source_clamp);
  CHECK(center[0] == 1000.5f * source_scale[0] && center[1] == 600.5f * source_scale[1]);
}

void TestSceneProfiles() {
  std::printf("scene profiles: mua1 keeps 0161's scene rule, mua1-gold finds the same surface, mua2 its own\n");
  using Format = rex::graphics::xenos::ColorRenderTargetFormat;
  using Msaa = rex::graphics::xenos::MsaaSamples;
  const scene::SceneProfile* mua1 = scene::FindSceneProfile("mua1");
  const scene::SceneProfile* gold = scene::FindSceneProfile("mua1-gold");
  const scene::SceneProfile* mua2 = scene::FindSceneProfile("mua2");
  CHECK(mua1 != nullptr && gold != nullptr && mua2 != nullptr && mua1 != gold && gold != mua2 &&
        scene::kSceneProfiles.size() == 3);
  CHECK(!scene::FindSceneProfile("") && !scene::FindSceneProfile("MUA1") && !scene::FindSceneProfile("gold") &&
        !scene::FindSceneProfile("mua2-x360"));
  if (!mua1 || !gold || !mua2) {
    return;
  }
  // Only mua1-gold takes its Direct3D's 4x MSAA draws into the scene's pass.
  CHECK(!mua1->scene_memory_4x_draws && gold->scene_memory_4x_draws && !mua2->scene_memory_4x_draws);
  // Everything 0168 adds stays off for both, so their scene, pre-scene targets, depth and viewport are 0167's.
  for (const scene::SceneProfile* profile : {mua1, gold}) {
    CHECK(!profile->pinned_surface && profile->reduce_pre_scene_targets && !profile->depth_at_each_resolve &&
          !profile->depth_partitions && !profile->camera_required && !profile->native_pre_scene_key);
  }
  for (const scene::SceneProfile* profile : {mua1, gold}) {
    CHECK(profile->min_draws == 20 && profile->min_msaa_samples_log2 == mua1->min_msaa_samples_log2 &&
          profile->color_formats == mua1->color_formats);
    // 0161's rule over every sample count and color format: 2x or more, k_8_8_8_8 or k_8_8_8_8_GAMMA.
    for (uint32_t samples = 0; samples <= uint32_t(Msaa::k4X); ++samples) {
      for (uint32_t format = 0; format < 16; ++format) {
        const bool rule = samples >= uint32_t(Msaa::k2X) &&
                          (format == uint32_t(Format::k_8_8_8_8) || format == uint32_t(Format::k_8_8_8_8_GAMMA));
        CHECK(scene::IsSceneSurfaceCandidate(*profile, samples, format) == rule);
      }
    }
  }
  // MUA1's targets in a 3x FSR 3.1 Performance run: only the scene (2x, k_8_8_8_8) qualifies, not the composite,
  // glow and atlas (1x, k_8_8_8_8) or the shadow maps (1x, k_32_FLOAT).
  CHECK(scene::IsSceneSurfaceCandidate(*mua1, uint32_t(Msaa::k2X), uint32_t(Format::k_8_8_8_8)));
  CHECK(!scene::IsSceneSurfaceCandidate(*mua1, uint32_t(Msaa::k1X), uint32_t(Format::k_8_8_8_8)));
  CHECK(!scene::IsSceneSurfaceCandidate(*mua1, uint32_t(Msaa::k1X), uint32_t(Format::k_32_FLOAT)));
  // The profiles the app passes as d3d12_scene_profile.
  const auto profile_of = [](mua::GameId id) { return scene::FindSceneProfile(mua::Game(id).scene_profile); };
  CHECK(profile_of(mua::GameId::kMua1) == mua1);
  CHECK(profile_of(mua::GameId::kMua1Gold) == gold);
  CHECK(profile_of(mua::GameId::kMua2) == mua2);
}

using Targets = scene::PreSceneTargets;

// Gold's targets in log 264 (3x DLSS Performance): the scene is EDRAM color base 0 and depth base 1128, 2x MSAA,
// pitch 800 (10 tiles); its Direct3D also draws at 4x MSAA, pitch 400, on base 0 (color and depth-only) and 1128
// (depth-only). MUA1's (log 263) are the same without the 4x draws.
constexpr uint32_t kSceneColorBase = 0, kSceneDepthBase = 1128;
const uint32_t kSceneKey = Targets::Key(800, 1);

void TestSceneMemoryDraws() {
  std::printf("mua1-gold: 4x MSAA draws over the scene's memory are the scene's; nothing else is\n");
  const scene::SceneProfile& mua1 = *scene::FindSceneProfile("mua1");
  const scene::SceneProfile& gold = *scene::FindSceneProfile("mua1-gold");
  CHECK(scene::PitchSamples(kSceneKey) == 800 && scene::PitchSamples(Targets::Key(400, 2)) == 800);
  CHECK(scene::PitchSamples(Targets::Key(1280, 0)) == 1280 && scene::PitchSamples(Targets::Key(640, 2)) == 1280);
  const auto memory_draw = [](const scene::SceneProfile& profile, uint32_t key, uint32_t base) {
    return scene::IsSceneMemoryDraw(profile, kSceneKey, key, base, kSceneColorBase, kSceneDepthBase);
  };
  for (const uint32_t base : {kSceneColorBase, kSceneDepthBase}) {
    CHECK(memory_draw(gold, Targets::Key(400, 2), base));
    CHECK(!memory_draw(mua1, Targets::Key(400, 2), base));
  }
  // Another base, another width (a 4x draw over the 1280-wide composite), or another sample count.
  CHECK(!memory_draw(gold, Targets::Key(400, 2), 500));
  CHECK(!memory_draw(gold, Targets::Key(640, 2), kSceneColorBase));
  CHECK(!memory_draw(gold, Targets::Key(800, 0), kSceneColorBase));
  CHECK(!memory_draw(gold, Targets::Key(1600, 0), kSceneColorBase));
  // Every other target of both logs: the composite, HUD and menus (pitch 1280), the glow (320), the shadow maps (560)
  // and the light atlas (1040, depth base 1216).
  for (const auto& [key, base] : std::array<std::pair<uint32_t, uint32_t>, 5>{
           {{Targets::Key(1280, 0), 0},
            {Targets::Key(320, 0), 0},
            {Targets::Key(560, 0), 0},
            {Targets::Key(1040, 0), 0},
            {Targets::Key(1040, 0), 1216}}}) {
    CHECK(!memory_draw(gold, key, base) && !memory_draw(mua1, key, base));
  }
  // Gold's 4x draws over the light atlas (depth base 1216) and the shadow maps (1824) aren't the scene's.
  CHECK(!memory_draw(gold, Targets::Key(520, 2), 1216) && !memory_draw(gold, Targets::Key(280, 2), 1824));
  // A scene drawn at 4x has no 4x draws over it.
  CHECK(!scene::IsSceneMemoryDraw(gold, Targets::Key(400, 2), Targets::Key(400, 2), kSceneColorBase, kSceneColorBase,
                                  kSceneDepthBase));
  CHECK(scene::ScenePlanesAt(kSceneColorBase, kSceneColorBase, kSceneDepthBase) == scene::ScenePlanes::kColor);
  CHECK(scene::ScenePlanesAt(kSceneDepthBase, kSceneColorBase, kSceneDepthBase) == scene::ScenePlanes::kDepth);
  CHECK(scene::ScenePlanesAt(500, kSceneColorBase, kSceneDepthBase) == 0);
}

void TestScenePlanesClear() {
  std::printf("a resolve's clears and Gold's 4x draws over a plane clear it the same way\n");
  using Planes = scene::ScenePlanes;
  const uint32_t both = Planes::kColor | Planes::kDepth;
  // AfterResolve is Clear: the same takes after every combination, in MUA1's sequence of log 251.
  for (uint32_t clears = 0; clears < 4; ++clears) {
    Planes resolved, cleared;
    for (Planes* planes : {&resolved, &cleared}) {
      planes->NoteSceneDraw(true);
      CHECK(planes->Take(false) == both);
    }
    resolved.AfterResolve(clears & 1, clears & 2);
    cleared.Clear((clears & 1 ? Planes::kColor : 0) | (clears & 2 ? Planes::kDepth : 0));
    for (const bool writes_depth : {false, true}) {
      Planes resolved_copy = resolved, cleared_copy = cleared;
      resolved_copy.NoteSceneDraw(writes_depth);
      cleared_copy.NoteSceneDraw(writes_depth);
      CHECK(resolved_copy.Take(false) == cleared_copy.Take(false));
      CHECK(resolved_copy.Take(true) == cleared_copy.Take(true));
    }
    CHECK(resolved.Take(false) == cleared.Take(false));
  }
}

// A draw of the model: its surface (pitch and sample count) and the EDRAM bases it writes color and depth at.
constexpr uint32_t kNoBase = UINT32_MAX;
struct ModelDraw {
  uint32_t key;
  uint32_t color_base;
  uint32_t depth_base;
};

const ModelDraw kSceneDraw{kSceneKey, kSceneColorBase, kSceneDepthBase};
const ModelDraw kSceneDepthDraw{kSceneKey, kNoBase, kSceneDepthBase};
// Gold's 4x MSAA quads over the scene's memory (logs 265 and 266): color and depth, depth-only over the color, and
// depth-only over the depth.
const ModelDraw kGold4xColorDepth{Targets::Key(400, 2), kSceneColorBase, kSceneDepthBase};
const ModelDraw kGold4xOverColor{Targets::Key(400, 2), kNoBase, kSceneColorBase};
const ModelDraw kGold4xDepth{Targets::Key(400, 2), kNoBase, kSceneDepthBase};
const ModelDraw kGlowDraw{Targets::Key(320, 0), 0, kNoBase};
const ModelDraw kCompositeDraw{Targets::Key(1280, 0), 0, kNoBase};
const ModelDraw kShadowDraw{Targets::Key(560, 0), 0, 1824};
const ModelDraw kShadow4xDraw{Targets::Key(280, 2), kNoBase, 1824};
const ModelDraw kAtlasDraw{Targets::Key(1040, 0), 0, 1216};
const ModelDraw kAtlas4xDraw{Targets::Key(520, 2), kNoBase, 1216};

struct ModelResolve {
  scene::ResolveRect rect;
  uint32_t dest;
  uint32_t taken;
};

// SceneUpscaler's scene pass bookkeeping (BeginDraw, BeforeResolve, AfterResolve, FinishScene, BeforeSwap) for the
// scene at kSceneKey: a scene resolve takes its planes and leaves the pass pending; a draw that is neither a scene
// draw nor, before the frame's scene is done, one of the profile's draws over its memory finishes it; a second
// finished pass in a frame turns the upscaler off. Other draws and resolves go through its PreSceneTargets
// (NoteOtherDraw, NoteSampledByScene, NoteSampledAfterScene, BeforeResolve), with reads naming the resolve
// destinations a draw samples.
struct ScenePassModel {
  const scene::SceneProfile& profile;
  uint32_t scene_key = kSceneKey;
  uint32_t color_base = kSceneColorBase;
  uint32_t depth_base = kSceneDepthBase;
  bool pending = false;
  uint32_t finished = 0;
  bool turned_off = false;
  // Drawn smaller and jittered.
  uint32_t scene_draws = 0;
  scene::ScenePlanes planes;
  std::vector<ModelResolve> resolves;
  std::vector<uint32_t> depth_taken;
  std::vector<std::vector<ModelResolve>> passes;
  // d3d12_dlss_pre_scene_native and AllowsNativePreScene; for profiles without a native surface, whether the
  // pre-scene scale is below the output's.
  bool native_allowed = false;
  bool pre_scene_scale_reduced = true;
  Targets targets;
  bool scene_drawn = false;
  bool pre_scene_frame = false;
  std::map<uint32_t, uint32_t> other_resolves;
  // A frame's other draws by surface and base, drawn smaller or at the output scale, and its stretched resolves; the
  // last swapped frame's in last_*.
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> reduced, full, last_reduced, last_full;
  std::vector<uint32_t> stretched, last_stretched;

  // IsSceneDraw's and BeforeResolve's surface checks.
  bool IsSceneSurface(uint32_t key) const {
    return Targets::SamplesLog2(key) == Targets::SamplesLog2(scene_key) &&
           scene::IsScenePitch(profile, Targets::Pitch(key));
  }
  void Draw(const ModelDraw& draw, uint32_t count = 1, std::initializer_list<uint32_t> reads = {}) {
    const bool writes_color = draw.color_base != kNoBase;
    const uint32_t base = writes_color ? draw.color_base : draw.depth_base;
    for (uint32_t i = 0; i < count; ++i) {
      if (IsSceneSurface(draw.key) && base == (writes_color ? color_base : depth_base)) {
        planes.NoteSceneDraw(draw.depth_base != kNoBase);
        ++scene_draws;
        scene_drawn = true;
        if (targets.AwaitsSceneSampling()) {
          for (const uint32_t read : reads) {
            if (const auto resolve = other_resolves.find(read); resolve != other_resolves.end()) {
              targets.NoteSampledByScene(resolve->second);
            }
          }
        }
      } else if (!finished && scene::IsSceneMemoryDraw(profile, scene_key, draw.key, base, color_base, depth_base)) {
        planes.Clear((writes_color ? scene::ScenePlanesAt(draw.color_base, color_base, depth_base) : 0) |
                     (draw.depth_base != kNoBase ? scene::ScenePlanesAt(draw.depth_base, color_base, depth_base)
                                                 : 0));
      } else {
        if (pending) {
          Finish();
        }
        NoteOtherDraw(draw.key, base, reads);
      }
    }
  }
  uint32_t PreSceneKey(uint32_t key) const {
    return scene::PreSceneKeyFor(profile, Targets::Pitch(key), Targets::SamplesLog2(key), scene_drawn);
  }
  void NoteOtherDraw(uint32_t surface, uint32_t base, std::initializer_list<uint32_t> reads) {
    const uint32_t key = PreSceneKey(surface);
    targets.NoteDraw(key, scene_drawn);
    if (scene_drawn) {
      for (const uint32_t read : reads) {
        if (const auto resolve = other_resolves.find(read); resolve != other_resolves.end()) {
          targets.NoteSampledAfterScene(resolve->second);
        }
      }
    }
    const bool smaller = pre_scene_frame && scene::IsPreSceneReduced(profile, targets, key);
    ++(smaller ? reduced : full)[{surface, base}];
  }
  // A resolve of another surface is no scene resolve.
  void Resolve(const scene::ResolveRect& rect, uint32_t dest, bool clears, uint32_t key = 0) {
    if (!IsSceneSurface(key ? key : scene_key)) {
      const uint32_t other_key = PreSceneKey(key);
      if (pre_scene_frame && scene::IsPreSceneReduced(profile, targets, other_key)) {
        stretched.push_back(dest);
      }
      other_resolves[dest] = other_key;
      return;
    }
    other_resolves.erase(dest);
    resolves.push_back({rect, dest, planes.Take(false, profile.depth_at_each_resolve)});
    pending = true;
    planes.AfterResolve(clears, clears);
  }
  void ResolveDepth() {
    depth_taken.push_back(planes.Take(true, profile.depth_at_each_resolve));
    pending = true;
  }
  void Finish() {
    pending = false;
    if (resolves.empty()) {
      return;
    }
    turned_off |= finished > 0;
    if (profile.pinned_surface) {
      std::vector<scene::ResolveRect> rects;
      for (const ModelResolve& resolve : resolves) {
        rects.push_back(resolve.rect);
      }
      turned_off |= !scene::PinnedOutputResolve(*profile.pinned_surface, rects);
    }
    ++finished;
    passes.push_back(std::exchange(resolves, {}));
  }
  void Swap() {
    if (pending) {
      Finish();
    }
    finished = 0;
    planes.EndFrame();
    targets.EndFrame(scene_drawn, scene_key, native_allowed);
    pre_scene_frame = profile.native_pre_scene_key ? native_allowed && scene_drawn : pre_scene_scale_reduced;
    scene_drawn = false;
    last_reduced = std::exchange(reduced, {});
    last_full = std::exchange(full, {});
    last_stretched = std::exchange(stretched, {});
  }
};

// The destinations of a pass that get the output: those of the resolves no later one covers.
std::vector<uint32_t> WrittenDests(const std::vector<ModelResolve>& pass) {
  std::vector<scene::ResolveRect> rects;
  for (const ModelResolve& resolve : pass) {
    rects.push_back(resolve.rect);
  }
  const std::vector<bool> covered = scene::CoveredByLater(rects);
  std::vector<uint32_t> dests;
  for (size_t i = 0; i < pass.size(); ++i) {
    if (!covered[i]) {
      dests.push_back(pass[i].dest);
    }
  }
  return dests;
}

// Whether the output's resolves cover the whole 1280x720 screen.
bool WrittenCoverScreen(const std::vector<ModelResolve>& pass) {
  std::vector<scene::ResolveRect> rects;
  for (const ModelResolve& resolve : pass) {
    rects.push_back(resolve.rect);
  }
  const std::vector<bool> covered = scene::CoveredByLater(rects);
  std::vector<uint8_t> screen(size_t(1280 / 8) * (720 / 8), 0);
  for (size_t i = 0; i < rects.size(); ++i) {
    for (int32_t y = rects[i].screen_y / 8; !covered[i] && y < (rects[i].screen_y + int32_t(rects[i].height)) / 8;
         ++y) {
      for (int32_t x = rects[i].screen_x / 8; x < (rects[i].screen_x + int32_t(rects[i].width)) / 8; ++x) {
        screen[size_t(y) * (1280 / 8) + x] = 1;
      }
    }
  }
  return std::all_of(screen.begin(), screen.end(), [](uint8_t cell) { return cell != 0; });
}

std::vector<uint32_t> Taken(const std::vector<ModelResolve>& pass) {
  std::vector<uint32_t> taken;
  for (const ModelResolve& resolve : pass) {
    taken.push_back(resolve.taken);
  }
  return taken;
}

// Both games' predicated tiles: screen 0..672, and 640..1280 at window offset -640.
const scene::ResolveRect kLeftTile = scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 672 / 8, 720 / 8, 0, 0);
const scene::ResolveRect kRightTile = scene::ResolveRectFor(0, 0, 10, 0, 1, 0, 0, 640 / 8, 720 / 8, -640, 0);
// Their scene textures in logs 265 and 266: the early copies, and the later ones the glow and composite read.
constexpr uint32_t kGoldEarlyLeft = 0x1CC06000, kGoldEarlyRight = 0x1CC1A000;
constexpr uint32_t kGoldLeft = 0x1E9E0000, kGoldRight = 0x1E9F4000;

// Gold's title screen (logs 265 and 266, first trace): each tile resolved once, clearing color and depth.
void GoldTitleFrame(ScenePassModel& model) {
  model.Draw(kSceneDraw);
  model.Draw(kGold4xColorDepth);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xOverColor);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw, 15);
  model.Draw(kGold4xDepth);
  model.Resolve(kLeftTile, kGoldLeft, true);
  model.Draw(kGold4xOverColor);
  model.Draw(kGold4xDepth, 2);
  model.Draw(kSceneDraw, 14);
  model.Draw(kGold4xDepth);
  model.Resolve(kRightTile, kGoldRight, true);
  model.Draw(kGlowDraw, 3);
  model.Draw(kCompositeDraw, 25);
  model.Swap();
}

// Gold's gameplay (second trace): each tile resolved early without clears, then again into the texture the glow and
// composite read, clearing color and depth.
void GoldGameplayFrame(ScenePassModel& model) {
  for (const ModelDraw& atlas : {kAtlasDraw, kAtlas4xDraw}) {
    model.Draw(atlas);
  }
  model.Draw(kAtlasDraw, 171);
  model.Draw(kShadowDraw);
  model.Draw(kShadow4xDraw);
  model.Draw(kShadowDraw, 205);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xColorDepth);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xOverColor);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw, 435);
  model.Resolve(kLeftTile, kGoldEarlyLeft, false);
  model.Draw(kSceneDraw, 2);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw, 4);
  model.Draw(kGold4xDepth);
  model.Resolve(kLeftTile, kGoldLeft, true);
  model.Draw(kGold4xOverColor);
  model.Draw(kGold4xDepth);
  model.Draw(kSceneDraw, 435);
  model.Resolve(kRightTile, kGoldEarlyRight, false);
  model.Draw(kSceneDraw);
  model.Draw(kGold4xDepth, 2);
  model.Draw(kSceneDraw, 3);
  model.Draw(kGold4xDepth);
  model.Resolve(kRightTile, kGoldRight, true);
  model.Draw(kGlowDraw, 3);
  model.Draw(kCompositeDraw, 25);
  model.Swap();
}

// MUA1's gameplay (log 250): each tile S199 C S8 C, the second resolve clearing color and depth.
void Mua1GameplayFrame(ScenePassModel& model) {
  model.Draw(kAtlasDraw, 171);
  model.Draw(kShadowDraw, 205);
  for (const auto& [tile, early, later] : {std::tuple{kLeftTile, 1u, 2u}, std::tuple{kRightTile, 3u, 4u}}) {
    model.Draw(kSceneDraw, 199);
    model.Resolve(tile, early, false);
    model.Draw(kSceneDepthDraw, 8);
    model.Resolve(tile, later, true);
  }
  model.Draw(kGlowDraw, 3);
  model.Draw(kCompositeDraw, 25);
  model.Swap();
}

void TestGoldScenePasses() {
  std::printf("Gold's scene passes: title 2 of 2, gameplay 2 of 4 into the later copies, 4x draws inside the pass\n");
  const scene::SceneProfile& gold = *scene::FindSceneProfile("mua1-gold");
  const uint32_t both = scene::ScenePlanes::kColor | scene::ScenePlanes::kDepth;
  // The right tile is drawn at window offset -640: it sits at the EDRAM origin and lands at screen x 640.
  CHECK(kRightTile.edram_x == 0 && kRightTile.screen_x == 640 && kRightTile.width == 640);
  CHECK(kLeftTile.edram_x == 0 && kLeftTile.screen_x == 0 && kLeftTile.width == 672 && kLeftTile.height == 720);

  ScenePassModel title{gold};
  GoldTitleFrame(title);
  CHECK(!title.turned_off && title.passes.size() == 1 && title.passes[0].size() == 2);
  if (title.passes.size() == 1) {
    CHECK((WrittenDests(title.passes[0]) == std::vector<uint32_t>{kGoldLeft, kGoldRight}));
    CHECK(WrittenCoverScreen(title.passes[0]));
    // The 4x depth quad right before each resolve leaves its depth at the quad's.
    CHECK((Taken(title.passes[0]) == std::vector<uint32_t>{scene::ScenePlanes::kColor, scene::ScenePlanes::kColor}));
  }

  ScenePassModel gameplay{gold};
  GoldGameplayFrame(gameplay);
  CHECK(!gameplay.turned_off && gameplay.passes.size() == 1 && gameplay.passes[0].size() == 4);
  if (gameplay.passes.size() == 1) {
    const std::vector<ModelResolve>& pass = gameplay.passes[0];
    std::vector<scene::ResolveRect> rects;
    for (const ModelResolve& resolve : pass) {
      rects.push_back(resolve.rect);
    }
    CHECK((scene::CoveredByLater(rects) == std::vector<bool>{true, false, true, false}));
    CHECK((WrittenDests(pass) == std::vector<uint32_t>{kGoldLeft, kGoldRight}));
    CHECK(WrittenCoverScreen(pass));
    // Each tile's depth from its early resolve, color from both: the trace's "takes".
    CHECK((Taken(pass) == std::vector<uint32_t>{both, scene::ScenePlanes::kColor, both, scene::ScenePlanes::kColor}));
  }
  // At the logged 1920x1080 scene, the tiles' captures meet with no gap: 0..1008 and 960..1920.
  const scene::CaptureRegion left = scene::CaptureRegionFor(kLeftTile, 1.5, 1.5, 1920, 1080);
  const scene::CaptureRegion right = scene::CaptureRegionFor(kRightTile, 1.5, 1.5, 1920, 1080);
  CHECK(left.source_x == 0 && left.dest_x == 0 && left.width == 1008 && left.height == 1080);
  CHECK(right.source_x == 0 && right.dest_x == 960 && right.width == 960 && right.height == 1080);
  CHECK(right.dest_x <= int32_t(left.width) && uint32_t(right.dest_x) + right.width == 1920);

  // With mua1's rule the same frames split, as before the profile: off after the first pass of 1 resolve.
  const scene::SceneProfile& mua1 = *scene::FindSceneProfile("mua1");
  for (const auto frame : {GoldTitleFrame, GoldGameplayFrame}) {
    ScenePassModel as_mua1{mua1};
    frame(as_mua1);
    CHECK(as_mua1.turned_off && as_mua1.passes.size() >= 2 && as_mua1.passes[0].size() == 1);
  }
}

void TestScenePasses() {
  std::printf("scene passes: MUA1's frames as before, any other draw still splits the pass and turns it off\n");
  const scene::SceneProfile& mua1 = *scene::FindSceneProfile("mua1");
  const scene::SceneProfile& gold = *scene::FindSceneProfile("mua1-gold");
  const uint32_t both = scene::ScenePlanes::kColor | scene::ScenePlanes::kDepth;
  // MUA1's gameplay comes out the same under either profile: one pass of 4, the output into each tile's second copy.
  for (const scene::SceneProfile* profile : {&mua1, &gold}) {
    ScenePassModel model{*profile};
    Mua1GameplayFrame(model);
    CHECK(!model.turned_off && model.passes.size() == 1 && model.passes[0].size() == 4);
    if (model.passes.size() == 1) {
      CHECK((WrittenDests(model.passes[0]) == std::vector<uint32_t>{2, 4}));
      CHECK(WrittenCoverScreen(model.passes[0]));
      CHECK((Taken(model.passes[0]) ==
             std::vector<uint32_t>{both, scene::ScenePlanes::kColor, both, scene::ScenePlanes::kColor}));
    }
  }
  // MUA1's title (log 263): each tile resolved once, 2 of 2.
  {
    ScenePassModel model{mua1};
    model.Draw(kSceneDraw, 15);
    model.Resolve(kLeftTile, 1, true);
    model.Draw(kSceneDraw, 14);
    model.Resolve(kRightTile, 2, true);
    model.Draw(kGlowDraw);
    CHECK(!model.turned_off && model.passes.size() == 1);
    CHECK(model.passes.size() == 1 && (WrittenDests(model.passes[0]) == std::vector<uint32_t>{1, 2}));
  }
  // Anything else between the scene's resolves, such as a glow draw, still makes a second pass and turns it off.
  for (const scene::SceneProfile* profile : {&mua1, &gold}) {
    ScenePassModel model{*profile};
    model.Draw(kSceneDraw, 15);
    model.Resolve(kLeftTile, 1, true);
    model.Draw(kGlowDraw);
    model.Draw(kSceneDraw, 15);
    model.Resolve(kRightTile, 2, true);
    model.Draw(kCompositeDraw);
    CHECK(model.turned_off && model.passes.size() == 2);
  }
  // Once the frame's scene is done, a 4x draw over its memory is no longer the scene's: it finishes a later pass.
  {
    ScenePassModel model{gold};
    model.Draw(kSceneDraw, 15);
    model.Resolve(kLeftTile, 1, true);
    model.Draw(kCompositeDraw);
    model.Draw(kSceneDraw, 15);
    model.Resolve(kRightTile, 2, true);
    model.Draw(kGold4xDepth);
    CHECK(model.turned_off && model.passes.size() == 2);
  }
  // The HUD and menus (pitch 1280, 1x) are never the scene's, so they're drawn over the finished output.
  {
    ScenePassModel model{gold};
    model.Draw(kSceneDraw, 15);
    model.Resolve(kLeftTile, 1, true);
    model.Draw(kSceneDraw, 15);
    model.Resolve(kRightTile, 2, true);
    CHECK(model.pending);
    model.Draw(kCompositeDraw);
    CHECK(!model.pending && model.passes.size() == 1);
  }
}

// MUA2's scene in log 268: color base 0 and D24FS8 depth base 1034, 2x MSAA, pitch 1040 (13 tiles), 1040x624. Its
// 1280x720 framebuffer is pitch 880 (11 tiles), resolved left (0..864) and right (864..1280 at window offset -864).
const uint32_t kMua2SceneKey = Targets::Key(1040, 1);
const scene::ResolveRect kMua2Scene = scene::ResolveRectFor(0, 0, 13, 0, 1, 0, 0, 1040 / 8, 624 / 8, 0, 0);
const scene::ResolveRect kMua2FinalLeft = scene::ResolveRectFor(0, 0, 11, 0, 1, 0, 0, 864 / 8, 720 / 8, 0, 0);
const scene::ResolveRect kMua2FinalRight = scene::ResolveRectFor(0, 0, 11, 0, 1, 0, 0, 416 / 8, 720 / 8, -864, 0);
// The scene's textures in log 268: the copy draw 763 reads (r7), and the one the bloom chain reads (r8).
constexpr uint32_t kMua2SceneCopy = 0x0A000000, kMua2SceneFinal = 0x0A100000;
const ModelDraw kMua2Composite{Targets::Key(880, 1), 0, 1058};
const ModelDraw kMua2SceneDepthDraw{kMua2SceneKey, kNoBase, 1034};
const ModelDraw kMua2SceneColorDraw{kMua2SceneKey, 0, kNoBase};

ScenePassModel Mua2Model(const scene::SceneProfile& profile) {
  ScenePassModel model{profile};
  model.scene_key = kMua2SceneKey;
  model.color_base = 0;
  model.depth_base = 1034;
  return model;
}

// The shadow maps' textures in log 268 (depth bases 1216 and 1632), and their filtered copies the scene samples.
constexpr uint32_t kMua2ShadowDepth[2] = {0x0B000000, 0x0B100000};
constexpr uint32_t kMua2ShadowFiltered[2] = {0x0C000000, 0x0C100000};
constexpr uint32_t kMua2Bloom = 0x0D000000, kMua2Lookup = 0x0D100000;

struct Mua2Frame {
  // Shadow casters that also write color at base 0, such as alpha-tested ones in heavier gameplay than log 268's.
  uint32_t color_shadow_casters = 0;
  bool shadow_sampled_after_scene = false;
};

// Log 268's gameplay frame (draws d0 to d808, resolves r0 to r17), in order.
void Mua2GameplayFrame(ScenePassModel& model, const Mua2Frame& frame = {}) {
  const scene::ResolveRect any{};
  model.Draw({Targets::Key(880, 1), kNoBase, 1058}, 24);
  model.Draw(kMua2Composite);
  model.Draw({Targets::Key(440, 2), 0, 1058});
  // The shadow maps (depth bases 1216 and 1632, pitch 1040 1x), each cleared by a 4x quad first.
  for (int i = 0; i < 2; ++i) {
    const uint32_t shadow = i ? 1632 : 1216;
    model.Draw({Targets::Key(1040, 0), kNoBase, shadow});
    model.Draw({Targets::Key(520, 2), kNoBase, shadow});
    model.Draw({Targets::Key(1040, 0), kNoBase, shadow}, 60);
    if (i) {
      model.Draw({Targets::Key(1040, 0), 0, shadow}, frame.color_shadow_casters);
    }
    model.Resolve(any, kMua2ShadowDepth[i], false, Targets::Key(1040, 0));
  }
  // Their filtered copies (k_16_16, pitch 1040 1x at color base 0), two passes each.
  for (int i = 0; i < 2; ++i) {
    model.Draw({Targets::Key(1040, 0), 0, kNoBase}, 1, {kMua2ShadowDepth[i]});
    model.Resolve(any, kMua2ShadowFiltered[i], false, Targets::Key(1040, 0));
    model.Draw({Targets::Key(1040, 0), 0, kNoBase}, 1, {kMua2ShadowFiltered[i]});
    model.Resolve(any, kMua2ShadowFiltered[i], false, Targets::Key(1040, 0));
  }
  model.Draw(kMua2Composite);
  model.Draw({Targets::Key(440, 2), 0, 1058});
  // 4x quads over the scene's memory, before the scene.
  model.Draw({Targets::Key(520, 2), 0, 1034});
  model.Draw({Targets::Key(520, 2), kNoBase, 1034});
  model.Draw(kMua2SceneDepthDraw, 260);
  model.ResolveDepth();
  // The sky's depth, below the scene's range.
  model.Draw(kMua2SceneDepthDraw, 3);
  model.Draw(kMua2SceneColorDraw, 265, {kMua2ShadowFiltered[0], kMua2ShadowFiltered[1]});
  model.Draw(kMua2SceneColorDraw, 5);
  model.Resolve(kMua2Scene, kMua2SceneCopy, false);
  // Draw 763, a 3D draw of the scene that reads r7.
  model.Draw(kMua2SceneColorDraw);
  model.Resolve(kMua2Scene, kMua2SceneFinal, true);
  // The bloom chain: pitch 320 1x, then pitch 160 at 2x MSAA and color base 0, like the scene.
  for (int i = 0; i < 2; ++i) {
    model.Draw({Targets::Key(320, 0), 0, kNoBase});
    model.Resolve(any, 0x0E000000, true, Targets::Key(320, 0));
  }
  for (int i = 0; i < 4; ++i) {
    model.Draw({Targets::Key(160, 1), 0, kNoBase});
    model.Resolve(any, kMua2Bloom, true, Targets::Key(160, 1));
  }
  // The 1024x32 texture, then the composite, HUD and menus into the framebuffer.
  model.Draw({Targets::Key(1040, 0), 0, kNoBase});
  model.Resolve(any, kMua2Lookup, true, Targets::Key(1040, 0));
  model.Draw({Targets::Key(440, 2), 0, 1058});
  if (frame.shadow_sampled_after_scene) {
    model.Draw(kMua2Composite, 1, {kMua2ShadowFiltered[0]});
  }
  model.Draw(kMua2Composite, 36, {kMua2Lookup, kMua2Bloom});
  model.Resolve(kMua2FinalLeft, 0x0E100000, true, Targets::Key(880, 1));
  model.Resolve(kMua2FinalRight, 0x0E200000, true, Targets::Key(880, 1));
  model.Swap();
}

void TestMua2Profile() {
  std::printf("mua2: the pinned 1040x624 scene surface and its own output domain, beside the 1280x720 framebuffer\n");
  using Format = rex::graphics::xenos::ColorRenderTargetFormat;
  using Msaa = rex::graphics::xenos::MsaaSamples;
  using Depth = rex::graphics::xenos::DepthRenderTargetFormat;
  const scene::SceneProfile& mua2 = *scene::FindSceneProfile("mua2");
  const scene::SceneProfile& mua1 = *scene::FindSceneProfile("mua1");
  const scene::SceneProfile& gold = *scene::FindSceneProfile("mua1-gold");
  CHECK(mua2.pinned_surface.has_value());
  if (!mua2.pinned_surface) {
    return;
  }
  const scene::PinnedSceneSurface& pinned = *mua2.pinned_surface;
  CHECK(pinned.color_base == 0 && pinned.depth_base == 1034 && pinned.depth_format == uint32_t(Depth::kD24FS8) &&
        pinned.pitch == 1040 && pinned.width == 1040 && pinned.height == 624);
  CHECK(scene::IsSceneSurfaceCandidate(mua2, uint32_t(Msaa::k2X), uint32_t(Format::k_8_8_8_8)));
  CHECK(!scene::IsSceneSurfaceCandidate(mua2, uint32_t(Msaa::k2X), uint32_t(Format::k_8_8_8_8_GAMMA)));
  CHECK(!scene::IsSceneSurfaceCandidate(mua2, uint32_t(Msaa::k1X), uint32_t(Format::k_8_8_8_8)));
  CHECK(!mua2.reduce_pre_scene_targets && mua2.depth_at_each_resolve && mua2.depth_partitions &&
        mua2.camera_required && scene::kCameraMissingFrames == 600 &&
        mua2.native_pre_scene_key == Targets::Key(1040, 0));

  // Only the scene pair is the scene: not the composite (pitch 880, depth 1058), its 4x quads, the shadow maps
  // (1216, 1632), the bloom chain (pitch 320 and 160) or the same bases with D24S8.
  CHECK(scene::IsPinnedSceneSurface(mua2, 1040, 0, 1034, uint32_t(Depth::kD24FS8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 880, 0, 1058, uint32_t(Depth::kD24FS8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 880, 0, 1034, uint32_t(Depth::kD24FS8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 1040, 0, 1216, uint32_t(Depth::kD24S8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 1040, 0, 1632, uint32_t(Depth::kD24S8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 1040, 0, 1034, uint32_t(Depth::kD24S8)));
  CHECK(!scene::IsPinnedSceneSurface(mua2, 1040, 4, 1034, uint32_t(Depth::kD24FS8)));
  CHECK(scene::IsScenePitch(mua2, 1040));
  for (const uint32_t pitch : {880u, 440u, 520u, 320u, 160u}) {
    CHECK(!scene::IsScenePitch(mua2, pitch));
  }
  // mua1 and mua1-gold pin nothing.
  for (const scene::SceneProfile* profile : {&mua1, &gold}) {
    CHECK(scene::IsScenePitch(*profile, 1280) && scene::IsScenePitch(*profile, 800) &&
          scene::IsPinnedSceneSurface(*profile, 800, 0, 1128, uint32_t(Depth::kD24S8)));
  }

  // The scene's own domain at 3x: output 3120x1872, beside the 3840x2160 framebuffer the HUD and menus draw into.
  CHECK(pinned.width * 3 == 3120 && pinned.height * 3 == 1872);
  CHECK(1280 * 3 == 3840 && 720 * 3 == 2160 && pinned.width != 1280 && pinned.height != 720);
  // FSR 3.1 Performance from that output: AMD's 2x, and a width whose EDRAM tile edges land on whole render pixels.
  const uint32_t render_width = scene::SnapRenderWidth(scene::Fsr3RenderSize(3120, 3), pinned.width, 0, 3120);
  CHECK(scene::Fsr3RenderSize(3120, 3) == 1560 && scene::Fsr3RenderSize(1872, 3) == 936 && render_width == 1560);
  CHECK(80 * render_width % pinned.width == 0);
  // The scene's resolves capture the render-size scene from the EDRAM origin into the whole input.
  const scene::CaptureRegion region = scene::CaptureRegionFor(kMua2Scene, 1.5, 1.5, 1560, 936);
  CHECK(region.source_x == 0 && region.source_y == 0 && region.dest_x == 0 && region.dest_y == 0 &&
        region.width == 1560 && region.height == 936);

  // The scene's viewport (depth 1 to 0.1) over the sky's (0.05 to 0), whichever is drawn first.
  CHECK(scene::IsWiderDepthRange(-0.9f, -0.05f) && !scene::IsWiderDepthRange(-0.05f, -0.9f) &&
        !scene::IsWiderDepthRange(-0.9f, -0.9f));
  for (const std::vector<float>& order : {std::vector<float>{-0.9f, -0.05f, -0.05f}, {-0.05f, -0.9f, -0.05f}}) {
    float known = 0.0f;
    bool known_set = false;
    for (const float z_scale : order) {
      if (!known_set || scene::IsWiderDepthRange(z_scale, known)) {
        known = z_scale;
        known_set = true;
      }
    }
    CHECK(known == -0.9f);
  }
}

void TestMua2ScenePass() {
  std::printf("mua2: log 268's frame is one pass of 2 resolves with the output in the last, and nothing else is its\n");
  const scene::SceneProfile& mua2 = *scene::FindSceneProfile("mua2");
  const uint32_t both = scene::ScenePlanes::kColor | scene::ScenePlanes::kDepth;
  CHECK(kMua2Scene.screen_x == 0 && kMua2Scene.screen_y == 0 && kMua2Scene.width == 1040 &&
        kMua2Scene.height == 624);
  CHECK(kMua2FinalRight.edram_x == 0 && kMua2FinalRight.screen_x == 864 && kMua2FinalRight.width == 416);

  ScenePassModel model = Mua2Model(mua2);
  Mua2GameplayFrame(model);
  CHECK(!model.turned_off && model.passes.size() == 1);
  if (model.passes.size() == 1) {
    const std::vector<ModelResolve>& pass = model.passes[0];
    CHECK(pass.size() == 2);
    // r7, which draw 763 reads, keeps the scene's input stretched; r8, which the bloom chain reads, gets the output.
    CHECK((WrittenDests(pass) == std::vector<uint32_t>{kMua2SceneFinal}));
    std::vector<scene::ResolveRect> rects;
    for (const ModelResolve& resolve : pass) {
      rects.push_back(resolve.rect);
    }
    CHECK(scene::PinnedOutputResolve(*mua2.pinned_surface, rects) == std::optional<size_t>(1));
    // Color and depth at each: r8's are the final scene's, after draw 763 and with the sky's depth.
    CHECK((Taken(pass) == std::vector<uint32_t>{both, both}));
  }
  // r6, the depth resolve before the sky, takes depth for its own copy only.
  CHECK((model.depth_taken == std::vector<uint32_t>{scene::ScenePlanes::kDepth}));
  // Drawn smaller and jittered: the pre-pass, the sky and the scene's 271 color draws, 534 as in the log, and none of
  // the shadow maps, bloom, 1024x32 texture, composite, HUD or menus.
  CHECK(model.scene_draws == 534);

  // Without the pin (mua1's rule) the composite and the bloom chain, 2x MSAA at color base 0, count as the scene and
  // split it into a second pass; depth would come once, from r6, without the sky.
  ScenePassModel unpinned = Mua2Model(*scene::FindSceneProfile("mua1"));
  Mua2GameplayFrame(unpinned);
  CHECK(unpinned.turned_off && unpinned.passes.size() >= 2 && unpinned.scene_draws > 534);
  CHECK(!unpinned.passes.empty() && unpinned.passes[0].size() == 2 &&
        unpinned.passes[0][0].taken == scene::ScenePlanes::kColor);

  // The whole scene resolved once is a pass of 1 into that resolve.
  ScenePassModel single = Mua2Model(mua2);
  single.Draw(kMua2SceneColorDraw, 300);
  single.Resolve(kMua2Scene, 1, true);
  single.Draw({Targets::Key(320, 0), 0, kNoBase});
  CHECK(!single.turned_off && single.passes.size() == 1 && (WrittenDests(single.passes[0]) == std::vector<uint32_t>{1}));
  // A last resolve that isn't the whole scene turns it off, as does a scene resolved in two passes.
  ScenePassModel partial = Mua2Model(mua2);
  partial.Draw(kMua2SceneColorDraw, 300);
  partial.Resolve(kMua2Scene, 1, false);
  partial.Resolve(scene::ResolveRectFor(0, 0, 13, 0, 1, 0, 0, 520 / 8, 624 / 8, 0, 0), 2, true);
  partial.Draw({Targets::Key(320, 0), 0, kNoBase});
  CHECK(partial.turned_off);
  ScenePassModel twice = Mua2Model(mua2);
  for (int i = 0; i < 2; ++i) {
    twice.Draw(kMua2SceneColorDraw, 100);
    twice.Resolve(kMua2Scene, 1, true);
    twice.Draw({Targets::Key(320, 0), 0, kNoBase});
  }
  CHECK(twice.turned_off);
  // PinnedOutputResolve itself: the last one, the whole surface, every other covered.
  const scene::PinnedSceneSurface& pinned = *mua2.pinned_surface;
  const scene::ResolveRect left_half = scene::ResolveRectFor(0, 0, 13, 0, 1, 0, 0, 520 / 8, 624 / 8, 0, 0);
  CHECK(!scene::PinnedOutputResolve(pinned, {}));
  CHECK(!scene::PinnedOutputResolve(pinned, {kMua2Scene, left_half}));
  CHECK(scene::PinnedOutputResolve(pinned, {left_half, kMua2Scene}) == std::optional<size_t>(1));
  CHECK(!scene::PinnedOutputResolve(pinned, {kMua2FinalLeft}));
}

// An object-to-clip matrix from log 268's columns c0 to c3.
scene::Matrix FromColumns(const std::array<std::array<double, 4>, 4>& columns) {
  scene::Matrix matrix;
  for (size_t column = 0; column < 4; ++column) {
    for (size_t row = 0; row < 4; ++row) {
      matrix[row * 4 + column] = columns[column][row];
    }
  }
  return matrix;
}

void TestMua2Camera() {
  std::printf("mua2: the scene's camera is near 32, infinite far, 37.992 degrees, depth 1 near, over the others\n");
  // Draw 230 (the scene), draw 490 (the sky, without the camera's translation) and draw 758 (particles in view space).
  const scene::Matrix scene_matrix = FromColumns({{{0.186757, -0.453768, 0.988814, 0.981089},
                                                   {-1.62328, -0.0522057, 0.113762, 0.112873},
                                                   {-8.8292e-10, 2.86873, 0.158478, 0.15724},
                                                   {588.044, -1997.97, 1377.49, 1398.72}}});
  const scene::Matrix sky_matrix = FromColumns({{{0.186757, -0.453768, 0.988814, 0.981089},
                                                 {-1.62328, -0.0522057, 0.113762, 0.112873},
                                                 {-8.8292e-10, 2.86873, 0.158478, 0.15724},
                                                 {0.0, 0.0, -32.252, 0.0}}});
  const scene::Matrix particle_matrix =
      FromColumns({{{1.63399, 0, 0, 0}, {0, 2.90487, 0, 0}, {0, 0, -1.00787, -1}, {0, 0, -32.252, 0}}});
  const double degrees = 57.29577951308232;
  // The scene's viewport z (scale -0.9, offset 1) with depth 1 near (zfunc 6, kGreaterEqual).
  const auto scene_projection = [](const scene::Matrix& matrix) {
    return scene::CameraProjectionFor(matrix, -0.9, 1.0, true);
  };
  std::vector<scene::CameraProjection> projections;
  for (const scene::Matrix* matrix : {&scene_matrix, &sky_matrix, &particle_matrix}) {
    const std::optional<scene::CameraProjection> projection = scene_projection(*matrix);
    CHECK(projection.has_value());
    if (projection) {
      CHECK(std::abs(projection->near_depth - 32.0) < 0.05 && std::isinf(projection->far_depth));
      CHECK(std::abs(projection->fov_y * degrees - 37.992) < 0.01);
      projections.push_back(*projection);
    }
  }
  // The sky through its own viewport (0.05 to 0) is the log's 1.612/4096 group.
  const std::optional<scene::CameraProjection> sky_own = scene::CameraProjectionFor(sky_matrix, -0.05, 0.05, true);
  CHECK(sky_own && std::abs(sky_own->near_depth - 1.612) < 0.01 && std::abs(sky_own->far_depth - 4096.0) < 2.0);
  // The log's other groups, from passes before the scene with normal depth.
  const scene::CameraProjection shadow_a{64.0, 362.564, 131.224 / degrees};
  const scene::CameraProjection shadow_b{64.0, 607.08, 119.0 / degrees};
  if (projections.size() == 3 && sky_own) {
    std::vector<scene::CameraProjection> frame;
    for (int i = 0; i < 6; ++i) {
      frame.insert(frame.end(), projections.begin(), projections.end());
    }
    frame.insert(frame.end(), {shadow_a, shadow_a, shadow_b, shadow_b, *sky_own});
    const std::optional<scene::CameraProjection> consensus = scene::ConsensusCameraProjection(frame);
    CHECK(consensus && std::abs(consensus->near_depth - 32.0) < 0.05 && std::isinf(consensus->far_depth) &&
          std::abs(consensus->fov_y * degrees - 37.992) < 0.01);
  }
}

void TestMua2CameraMotion() {
  std::printf("mua2: camera motion vectors in render pixels, current to previous, without jitter, depth 1 near\n");
  const std::array<float, 6> viewport = {520.0f, 520.0f, -312.0f, 312.0f, -0.9f, 1.0f};
  const std::array<float, 2> render_scale = {1560.0f / 1040.0f, 936.0f / 624.0f};
  // Its projection: clip w is the view depth and z / w = 1.00787 - 32.252 / depth, so the buffer holds 1 at 32.
  const scene::Matrix projection = {1.63399, 0, 0, 0, 0, 2.90487, 0, 0, 0, 0, 1.00787, -32.252, 0, 0, 1, 0};
  // The camera moves 4 units to the right and turns slightly between the frames.
  const scene::Matrix view_previous = scene::Multiply(RotationY(0.00), Translation(0.0, -20.0, 300.0));
  const scene::Matrix view_current = scene::Multiply(RotationY(0.01), Translation(-4.0, -20.0, 300.0));
  std::vector<std::pair<scene::Matrix, scene::Matrix>> pairs;
  for (int i = 0; i < 12; ++i) {
    const scene::Matrix world = scene::Multiply(Translation(i * 15.0 - 80.0, 0.0, i * 20.0), RotationY(i * 0.3));
    pairs.emplace_back(scene::Multiply(projection, scene::Multiply(view_current, world)),
                       scene::Multiply(projection, scene::Multiply(view_previous, world)));
  }
  uint32_t matched = 0, agreeing = 0;
  const std::optional<scene::Matrix> reprojection = scene::EstimateCameraReprojection(pairs, matched, agreeing);
  CHECK(reprojection && matched == 12 && agreeing == 12);
  if (!reprojection) {
    return;
  }
  const scene::Matrix current = scene::Multiply(projection, view_current);
  const scene::Matrix previous = scene::Multiply(projection, view_previous);
  scene::Matrix clip_to_world;
  CHECK(scene::Invert(current, clip_to_world));
  double worst = 0.0, largest = 0.0;
  for (const std::array<float, 2>& jitter : std::array<std::array<float, 2>, 2>{{{0.0f, 0.0f}, {0.4f, -0.3f}}}) {
    for (uint32_t y = 11; y < 936; y += 97) {
      for (uint32_t x = 9; x < 1560; x += 131) {
        for (const float depth : {0.15f, 0.5f, 0.95f}) {
          const double center_x = x + 0.5 - jitter[0], center_y = y + 0.5 - jitter[1];
          const double clip[4] = {(center_x / render_scale[0] - viewport[1]) / viewport[0],
                                  (center_y / render_scale[1] - viewport[3]) / viewport[2],
                                  (depth - viewport[5]) / viewport[4], 1.0};
          std::array<double, 4> world = {};
          for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
              world[r] += clip_to_world[r * 4 + c] * clip[c];
            }
          }
          const std::array<double, 3> point = {world[0] / world[3], world[1] / world[3], world[2] / world[3]};
          const std::array<double, 3> before = Project(previous, point, viewport, render_scale);
          const std::array<float, 2> motion =
              scene::CameraMotion(*reprojection, viewport, render_scale, jitter, x, y, depth);
          worst = std::max({worst, std::abs(motion[0] - (before[0] - center_x)),
                            std::abs(motion[1] - (before[1] - center_y))});
          largest = std::max({largest, std::abs(before[0] - center_x), std::abs(before[1] - center_y)});
        }
      }
    }
  }
  CHECK(largest > 1.0 && worst < 1.0e-2);
  // Previous minus current: the camera moved right, so a near surface was further right in the previous frame.
  const scene::Matrix translated = scene::Multiply(projection, Translation(-4.0, -20.0, 300.0));
  scene::Matrix translated_inverse;
  CHECK(scene::Invert(translated, translated_inverse));
  const scene::Matrix shift_only = scene::Multiply(previous, translated_inverse);
  CHECK(scene::CameraMotion(shift_only, viewport, render_scale, {0.0f, 0.0f}, 780, 468, 0.9f)[0] > 1.0f);
  // The sky's depth (0.05 to 0) lies past the infinite far plane (1 - 0.9 * 1.00787), where it would move against the
  // camera's translation; MotionDepthRange takes it as infinitely far, so it moves with the camera's turn only.
  const std::optional<scene::CameraProjection> camera = scene::CameraProjectionFor(current, -0.9, 1.0, true);
  CHECK(camera && std::abs(camera->infinity_depth - (1.0 - 0.9 * 1.00787)) < 1.0e-6);
  const std::array<float, 2> range = scene::MotionDepthRange(camera, true);
  CHECK(std::abs(range[0] - (1.0 - 0.9 * 1.00787)) < 1.0e-6 && range[1] == FLT_MAX);
  const std::array<float, 2> at_infinity =
      scene::CameraMotion(*reprojection, viewport, render_scale, {}, 780, 100, range[0]);
  const std::array<float, 2> sky =
      scene::CameraMotion(*reprojection, viewport, render_scale, {}, 780, 100, 0.03f, range);
  const std::array<float, 2> sky_unclamped =
      scene::CameraMotion(*reprojection, viewport, render_scale, {}, 780, 100, 0.03f);
  CHECK(std::abs(at_infinity[0]) > 5.0f && sky == at_infinity);
  CHECK(std::abs(sky_unclamped[0] - at_infinity[0]) > 1.0f);
  // The scene's own depths are untouched.
  for (const float depth : {0.1f, 0.5f, 1.0f}) {
    CHECK(scene::CameraMotion(*reprojection, viewport, render_scale, {}, 780, 468, depth, range) ==
          scene::CameraMotion(*reprojection, viewport, render_scale, {}, 780, 468, depth));
  }
  // No camera, or the default of the other profiles: nothing is clamped; with normal depth the far side is.
  CHECK((scene::MotionDepthRange(std::nullopt, true) == std::array<float, 2>{-FLT_MAX, FLT_MAX}));
  CHECK((scene::MotionDepthRange(camera, false) == std::array<float, 2>{-FLT_MAX, range[0]}));
}

void TestMua2SceneAlpha() {
  std::printf("mua2: FSR 3.1's output keeps the scene's own alpha, unjittered and clamped to the render size\n");
  // 3x Performance: render 1560x936, output 3120x1872.
  const std::array<float, 2> source_scale = {1560.0f / 3120.0f, 936.0f / 1872.0f};
  const std::array<float, 4> source_clamp = {0.0f, 0.0f, 1560.0f, 936.0f};
  const std::array<float, 2> center = scene::SceneAlphaSourcePosition(1560, 936, source_scale, {0.0f, 0.0f}, source_clamp);
  CHECK(center[0] == 780.25f && center[1] == 468.25f);
  const std::array<float, 2> jittered =
      scene::SceneAlphaSourcePosition(1560, 936, source_scale, {0.3f, -0.2f}, source_clamp);
  CHECK(std::abs(jittered[0] - 780.55f) < 1.0e-4f && std::abs(jittered[1] - 468.05f) < 1.0e-4f);
  const std::array<float, 2> corner = scene::SceneAlphaSourcePosition(0, 0, source_scale, {-0.4f, -0.4f}, source_clamp);
  CHECK(corner[0] == 0.5f && corner[1] == 0.5f);
  const std::array<float, 2> far_corner =
      scene::SceneAlphaSourcePosition(3119, 1871, source_scale, {0.4f, 0.4f}, source_clamp);
  CHECK(far_corner[0] == 1559.5f && far_corner[1] == 935.5f);
}

}  // namespace

void TestMua2ShadowPass() {
  std::printf("mua2: the shadow pass (pitch 1040 1x before the scene) at 1x in every mode; the rest keeps its scale\n");
  const scene::SceneProfile& mua2 = *scene::FindSceneProfile("mua2");
  const scene::SceneProfile& mua1 = *scene::FindSceneProfile("mua1");
  const scene::SceneProfile& gold = *scene::FindSceneProfile("mua1-gold");
  const uint32_t shadow = Targets::Key(1040, 0);
  using Draws = std::map<std::pair<uint32_t, uint32_t>, uint32_t>;

  // The same pitch and sample count after the scene is another surface for mua2 only.
  const uint32_t after = scene::PreSceneKeyFor(mua2, 1040, 0, true);
  CHECK(scene::PreSceneKeyFor(mua2, 1040, 0, false) == shadow && after != shadow);
  CHECK(Targets::Pitch(after) == 1040 && Targets::SamplesLog2(after) == 0 && Targets::SamplesLog2(shadow) == 0);
  for (const scene::SceneProfile* profile : {&mua1, &gold}) {
    for (const bool after_scene : {false, true}) {
      CHECK(scene::PreSceneKeyFor(*profile, 1040, 0, after_scene) == shadow &&
            scene::PreSceneKeyFor(*profile, 560, 0, after_scene) == Targets::Key(560, 0));
    }
  }
  // mua2's goes to 1x in every mode above 1x; mua1's and Gold's only in Performance with a pre-scene scale between 1x
  // and the output's.
  CHECK(scene::AllowsNativePreScene(mua2, false, {3, 3}, {3, 3}) &&
        scene::AllowsNativePreScene(mua2, true, {3, 3}, {3, 3}) &&
        scene::AllowsNativePreScene(mua2, false, {2, 2}, {2, 2}) &&
        !scene::AllowsNativePreScene(mua2, true, {1, 1}, {1, 1}));
  for (const scene::SceneProfile* profile : {&mua1, &gold}) {
    CHECK(scene::AllowsNativePreScene(*profile, true, {2, 2}, {3, 3}) &&
          scene::AllowsNativePreScene(*profile, true, {2, 2}, {4, 4}));
    CHECK(!scene::AllowsNativePreScene(*profile, false, {2, 2}, {3, 3}) &&
          !scene::AllowsNativePreScene(*profile, true, {3, 3}, {3, 3}) &&
          !scene::AllowsNativePreScene(*profile, true, {1, 1}, {2, 2}));
  }
  // mua1's and Gold's surfaces reduce exactly as PreSceneTargets reduces them.
  {
    Targets targets;
    for (int frame = 0; frame < 8; ++frame) {
      targets.NoteDraw(Targets::Key(560, 0), false);
      targets.NoteSampledByScene(Targets::Key(560, 0));
      targets.NoteDraw(Targets::Key(640, 0), false);
      targets.EndFrame(true, Targets::Key(800, 1), true);
      for (const uint32_t key : {Targets::Key(560, 0), Targets::Key(640, 0), Targets::Key(800, 1)}) {
        CHECK(scene::IsPreSceneReduced(mua1, targets, key) == targets.IsReduced(key) &&
              scene::IsPreSceneReduced(gold, targets, key) == targets.IsReduced(key));
      }
    }
  }

  for (const uint32_t casters : {0u, 120u}) {
    const Mua2Frame frame{.color_shadow_casters = casters};
    ScenePassModel model = Mua2Model(mua2);
    model.native_allowed = true;
    // Drawn before the scene in three frames, then sampled by it in three: proven only an input of the scene.
    for (int i = 0; i < 6; ++i) {
      Mua2GameplayFrame(model, frame);
      CHECK(model.last_reduced.empty() && model.last_stretched.empty());
      CHECK(model.targets.IsNative(shadow) == (i == 5));
    }
    Mua2GameplayFrame(model, frame);
    // The shadow maps' depth (1216, 1632), the casters' color at base 0 and the filtered copies at base 0, at 1x.
    CHECK((model.last_reduced == Draws{{{shadow, 1216}, 61}, {{shadow, 1632}, 61}, {{shadow, 0}, 4 + casters}}));
    // Their six resolves stretched to the textures' full size; none of the scene's, bloom's, the 1024x32 texture's
    // or the framebuffer's.
    CHECK((model.last_stretched == std::vector<uint32_t>{kMua2ShadowDepth[0], kMua2ShadowDepth[1],
                                                         kMua2ShadowFiltered[0], kMua2ShadowFiltered[0],
                                                         kMua2ShadowFiltered[1], kMua2ShadowFiltered[1]}));
    // Base 0 after the scene keeps the output scale: the 1024x32 texture (pitch 1040 1x, like the shadow pass), the
    // bloom chain (pitch 160 2x and 320 1x) and the framebuffer (pitch 880 2x and its 4x quads). So do the 4x quads
    // clearing the shadow maps, which cover the 1x ones.
    CHECK(model.last_full.at({shadow, 0}) == 1 && model.last_full.at({Targets::Key(160, 1), 0}) == 4 &&
          model.last_full.at({Targets::Key(320, 0), 0}) == 2 && model.last_full.at({Targets::Key(880, 1), 0}) == 38 &&
          model.last_full.at({Targets::Key(440, 2), 0}) == 3);
    CHECK(model.last_full.at({Targets::Key(520, 2), 1216}) == 1 &&
          model.last_full.at({Targets::Key(520, 2), 1632}) == 1);
    CHECK(!model.last_full.contains({shadow, 1216}) && !model.last_full.contains({shadow, 1632}));
    // The scene keeps its pass: 534 draws a frame at the scene's input scale, the output in r8.
    CHECK(!model.turned_off && model.scene_draws == 534 * 7 && model.passes.size() == 7);
    CHECK(!model.passes.empty() && (WrittenDests(model.passes.back()) == std::vector<uint32_t>{kMua2SceneFinal}));
    for (const auto& [draw, count] : model.last_reduced) {
      CHECK(draw.first != kMua2SceneKey);
    }
  }

  // Taken as one surface, the 1024x32 texture after the scene would keep the shadow pass at the output scale.
  {
    scene::SceneProfile unsplit = mua2;
    unsplit.native_pre_scene_key = 0;
    ScenePassModel model = Mua2Model(unsplit);
    model.native_allowed = true;
    model.pre_scene_scale_reduced = false;
    for (int i = 0; i < 8; ++i) {
      Mua2GameplayFrame(model);
    }
    CHECK(!model.targets.IsReduced(shadow) && !model.targets.IsNative(shadow) && model.last_reduced.empty());
  }

  // A shadow texture sampled after the scene: back to the output scale for the session.
  {
    ScenePassModel model = Mua2Model(mua2);
    model.native_allowed = true;
    for (int i = 0; i < 7; ++i) {
      Mua2GameplayFrame(model);
    }
    CHECK(model.targets.IsNative(shadow) && !model.last_reduced.empty());
    Mua2GameplayFrame(model, {.shadow_sampled_after_scene = true});
    for (int i = 0; i < 6; ++i) {
      Mua2GameplayFrame(model);
      CHECK(!model.targets.IsNative(shadow) && model.last_reduced.empty() && model.last_stretched.empty());
    }
  }

  // A frame without the scene, such as a menu, was decided at the previous swap; the next one draws everything at the
  // output scale, and 1x returns after the next scene.
  {
    ScenePassModel model = Mua2Model(mua2);
    model.native_allowed = true;
    for (int i = 0; i < 7; ++i) {
      Mua2GameplayFrame(model);
    }
    const auto menu_frame = [&] {
      model.Draw({shadow, 0, kNoBase});
      model.Resolve({}, 0x0F000000, true, shadow);
      model.Draw(kMua2Composite, 20, {0x0F000000});
      model.Swap();
    };
    menu_frame();
    CHECK((model.last_reduced == Draws{{{shadow, 0}, 1}}));
    menu_frame();
    CHECK(model.last_reduced.empty() && model.last_stretched.empty());
    Mua2GameplayFrame(model);
    CHECK(model.last_reduced.empty());
    Mua2GameplayFrame(model);
    CHECK(!model.last_reduced.empty() && model.targets.IsNative(shadow));
  }

  // d3d12_dlss_pre_scene_native off, or Render resolution 1x: nothing changes scale.
  {
    ScenePassModel model = Mua2Model(mua2);
    for (int i = 0; i < 10; ++i) {
      Mua2GameplayFrame(model);
      CHECK(model.last_reduced.empty() && model.last_stretched.empty());
    }
    CHECK(!model.targets.IsNative(shadow));
  }
}

int main() {
  TestResolveRects();
  TestTwoTilesCompose();
  TestWriteback();
  TestDepthDecode();
  TestCameraMotion();
  TestJitterAndSnap();
  TestPreSceneScale();
  TestPreSceneTargets();
  TestSurfaceRects64bpp();
  TestPreSceneStretch();
  TestDepthPacking();
  TestScenePlanes();
  TestDepthCompose();
  TestNativePreSceneTargets();
  TestFsr3RenderSizes();
  TestFsr3Jitter();
  TestFsr3Depth();
  TestFsr3MotionVectors();
  TestFsr3SceneComposition();
  TestFsr3SceneAlpha();
  TestSceneProfiles();
  TestSceneMemoryDraws();
  TestScenePlanesClear();
  TestGoldScenePasses();
  TestScenePasses();
  TestMua2Profile();
  TestMua2ScenePass();
  TestMua2Camera();
  TestMua2CameraMotion();
  TestMua2SceneAlpha();
  TestMua2ShadowPass();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
