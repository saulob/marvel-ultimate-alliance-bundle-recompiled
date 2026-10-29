// High render resolution: the scaled dimensions at 1x to 4x (4x is 5120x2880, not 3840x2160), what the scale is
// multiplied into (EDRAM, scaled resolve, host textures, post effects, copies, views, descriptors), the 3D wrapper
// size that the 4x device removal came from, and the patches that carry the fixes. Run: mua_render_scale_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "render_scale.h"

using namespace mua;

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

void TestScaledExtents() {
  std::printf("scaled extents 1x to 4x\n");
  const Extent expected[] = {{1280, 720}, {2560, 1440}, {3840, 2160}, {5120, 2880}};
  for (int scale = 1; scale <= 4; ++scale) {
    const Extent extent = ScaledGuestExtent(AspectRatio::k16x9, scale);
    CHECK(extent == expected[scale - 1]);
    CHECK(FitsHostTexture(extent));
    CHECK(PixelCount(extent) == uint64_t(expected[scale - 1].width) * expected[scale - 1].height);
    // 32-bit pixel counts and 64-bit byte counts of the largest formats do not overflow.
    CHECK(PixelCount(extent) <= UINT32_MAX);
    CHECK(PixelCount(extent) * 16 < (uint64_t(1) << 62));
  }
  CHECK(ScaledGuestExtent(AspectRatio::k16x9, 4) == (Extent{5120, 2880}));
  CHECK(!(ScaledGuestExtent(AspectRatio::k16x9, 4) == (Extent{3840, 2160})));
  CHECK(ScaledGuestExtent(AspectRatio::k16x9, 3) == (Extent{3840, 2160}));
  CHECK(ScaledGuestExtent(AspectRatio::k16x9, 0) == (Extent{1280, 720}));
  CHECK(ScaledGuestExtent(AspectRatio::k16x9, 9) == (Extent{5120, 2880}));
  CHECK(kMaxRenderScale == 4 && kMinRenderScale == 1);
  // Even the largest guest surface scaled by 4 stays inside the host texture limit.
  CHECK(kGuestMaxSurfaceDimension * 2 <= kMaxHostTextureDimension);
  CHECK(4096u * 4 <= kMaxHostTextureDimension);
  CHECK(!FitsHostTexture({8192u * 4, 8192u * 4}));
}

void TestScaledExtentsPerAspect() {
  std::printf("scaled extents follow the aspect ratio\n");
  for (const AspectRatioInfo& info : kAspectRatios) {
    for (int scale = kMinRenderScale; scale <= kMaxRenderScale; ++scale) {
      const Extent extent = ScaledGuestExtent(info.aspect, scale);
      CHECK(extent.width == uint32_t(CanvasWidth(info.aspect) * scale));
      CHECK(extent.height == uint32_t(kCanvasHeight * scale));
      CHECK(FitsHostTexture(extent));
      CHECK(PixelCount(extent) <= UINT32_MAX);
      CHECK(CanvasWidth(info.aspect) <= int(kGuestMaxSurfaceDimension));
      const Extent groups = PostEffectGroups(extent);
      CHECK(groups.width <= kMaxDispatchGroups && groups.height <= kMaxDispatchGroups);
      CHECK(UnclippedViewportExtent(scale) <= kMaxViewportCoordinate);
    }
  }
  CHECK(ScaledGuestExtent(AspectRatio::k32x9, 4) == (Extent{10240, 2880}));
  CHECK(ScaledGuestExtent(AspectRatio::k21x9, 2) == (Extent{3360, 1440}));
  CHECK(ScaledGuestExtent(AspectRatio::k4x3, 3) == (Extent{2880, 2160}));
}

void TestEdramAndScaledResolve() {
  std::printf("EDRAM, scaled resolve and typed views\n");
  const uint64_t edram[] = {10485760, 41943040, 94371840, 167772160};
  for (int scale = 1; scale <= 4; ++scale) {
    CHECK(EdramBufferBytes(scale) == edram[scale - 1]);
    CHECK(EdramBufferBytes(scale) <= UINT32_MAX);
    // The R32, R32G32 and R32G32B32A32 views of the EDRAM buffer.
    CHECK(FitsTypedView(EdramBufferBytes(scale), 2));
    CHECK(FitsTypedView(EdramBufferBytes(scale), 3));
    CHECK(FitsTypedView(EdramBufferBytes(scale), 4));
  }
  CHECK(ScaledResolveAddressSpaceBytes(4) == (uint64_t(8) << 30));
  CHECK(ScaledResolveBufferCount(1) == 0);
  CHECK(ScaledResolveBufferCount(2) == 1);
  CHECK(ScaledResolveBufferCount(3) == 4);
  CHECK(ScaledResolveBufferCount(4) == 7);
  CHECK(ScaledResolveBufferCount(kMaxRenderScale) <= ScaledResolveBufferCapacity());
  CHECK(ScaledResolveHeapSlots(4) == 512);
  CHECK(ScaledResolveHeapSlots(1) == 32);
  // The 32 MB limit of an R32 view at 4x: a full 1280x720 resolve is far below it, a 64 MB one is not.
  CHECK(FitsTypedView(uint64_t(1280) * 720 * 4 * 16, 2));
  CHECK(FitsTypedView(uint64_t(32) << 20 << 4, 2));
  CHECK(!FitsTypedView(uint64_t(64) << 20 << 4, 2));
  CHECK(FitsTypedView(uint64_t(64) << 20 << 4, 4));
  CHECK(kMaxCopyBufferBytes == (uint64_t(2) << 30));
}

void TestPostEffects() {
  std::printf("post effects, viewports and descriptors\n");
  for (int scale = 1; scale <= 4; ++scale) {
    const Extent output = ScaledGuestExtent(AspectRatio::k16x9, scale);
    const Extent groups = PostEffectGroups(output);
    // FXAA input and output, the gamma pass and the intermediate texture all use the guest output size.
    CHECK(groups.width * 16 >= output.width && groups.height * 8 >= output.height);
    CHECK(groups.width <= kMaxDispatchGroups && groups.height <= kMaxDispatchGroups);
    CHECK(FitsHostTexture(output));
    CHECK(UnclippedViewportExtent(scale) <= kMaxViewportCoordinate);
    CHECK(UnclippedViewportExtent(scale) >= kGuestMaxSurfaceDimension * scale - uint32_t(scale));
  }
  CHECK(PostEffectGroups({5120, 2880}) == (Extent{320, 360}));
  CHECK(UnclippedViewportExtent(1) == 8192 && UnclippedViewportExtent(3) == 24576 && UnclippedViewportExtent(4) == 32764);
  // The descriptors of a frame do not depend on the scale; the bindless heap holds 262144.
  CHECK(SwapDescriptorCount(false, true) == 2);
  CHECK(SwapDescriptorCount(true, true) == 4);
  CHECK(SwapDescriptorCount(true, false) == 5);
  CHECK(SwapDescriptorCount(true, false) * 3 < 262144);
}

void TestVolumeWrapper() {
  std::printf("3D wrapper of a scaled volume texture (the 4x INVALID_CALL)\n");
  const Extent slice = {32, 32};
  for (int scale = 1; scale <= 4; ++scale) {
    const Extent wrapper = VolumeWrapperExtent(slice, true, scale);
    // The load writes the footprint of the scaled slice; the copy into the wrapper has to fit.
    CHECK(CopyFitsDestination(ScaledLoadFootprint(slice, scale, 0), wrapper));
    CHECK(CopyFitsDestination(ScaledLoadFootprint(slice, scale, 0), ScaledHostMip(slice, scale, 0)));
  }
  // The wrapper that kept the guest size is the one the runtime rejected: 128x128 into 32x32 at 4x.
  CHECK(!CopyFitsDestination(ScaledLoadFootprint(slice, 4, 0), VolumeWrapperExtent(slice, false, 4)));
  CHECK(CopyFitsDestination(ScaledLoadFootprint(slice, 1, 0), VolumeWrapperExtent(slice, false, 1)));
  CHECK(VolumeWrapperExtent(slice, true, 4) == (Extent{128, 128}));
  CHECK(VolumeWrapperExtent(slice, false, 4) == slice);

  // Mips of a scaled texture: the load footprint is the guest level size times the scale, which differs from the host
  // mip for sizes not divisible by 2^level. A level clamped to 1 texel at the guest size is a whole scale factor in
  // the footprint, more than the host mip: without packed mips, mip 10 of 1280x720 at 4x is 4x4 into 5x2.
  CHECK(!CopyFitsDestination(ScaledLoadFootprint({1280, 720}, 4, 10), ScaledHostMip({1280, 720}, 4, 10)));
  CHECK(ScaledLoadCopyExtent(ScaledLoadFootprint({1280, 720}, 4, 10), ScaledHostMip({1280, 720}, 4, 10), {1, 1}) ==
        (Extent{4, 2}));
  // Block-compressed: the copy stays block-aligned, up to the mip padded to whole blocks.
  const Extent bc_copy = ScaledLoadCopyExtent({4, 4}, ScaledHostMip({1280, 720}, 4, 10), {4, 4});
  CHECK(bc_copy == (Extent{4, 4}));
  CHECK(bc_copy.width % 4 == 0 && bc_copy.height % 4 == 0);
  CHECK(CopyFitsDestination(bc_copy, BlockPaddedExtent(ScaledHostMip({1280, 720}, 4, 10), {4, 4})));
  for (uint32_t level = 0; level <= 10; ++level) {
    for (int scale = 1; scale <= 4; ++scale) {
      const Extent host_mip = ScaledHostMip({1280, 720}, scale, level);
      CHECK(CopyFitsDestination(ScaledLoadCopyExtent(ScaledLoadFootprint({1280, 720}, scale, level), host_mip, {1, 1}),
                                host_mip));
    }
    // At 1x the copy is the footprint itself: unscaled loads keep copying without a box.
    CHECK(ScaledLoadCopyExtent(ScaledLoadFootprint({1280, 720}, 1, level), ScaledHostMip({1280, 720}, 1, level),
                               {1, 1}) == ScaledLoadFootprint({1280, 720}, 1, level));
  }
  CHECK(ScaledLoadFootprint({20, 20}, 4, 3) == (Extent{8, 8}));
  CHECK(ScaledHostMip({20, 20}, 4, 3) == (Extent{10, 10}));
  CHECK(CopyFitsDestination(ScaledLoadFootprint({1280, 720}, 4, 0), ScaledHostMip({1280, 720}, 4, 0)));
  for (uint32_t level = 0; level <= 6; ++level) {
    CHECK(CopyFitsDestination(ScaledLoadFootprint({1280, 720}, 4, level), ScaledHostMip({1280, 720}, 4, level)));
  }
}

void TestBlockCompressedTail() {
  std::printf("block-compressed mip tail of an unscaled texture\n");
  const Extent base{2048, 1024};
  const Extent mip9 = MipExtent(base, 9);
  const Extent mip10 = MipExtent(base, 10);
  CHECK(mip9 == (Extent{4, 2}));
  CHECK(mip10 == (Extent{2, 1}));
  // The load writes whole 4x4 blocks and the destination mip holds them, so the copy box is the block footprint.
  const Extent block{4, 4};
  for (const Extent mip : {mip9, mip10}) {
    const Extent footprint = BlockPaddedExtent(mip, block);
    CHECK(footprint == block);
    CHECK(CopyFitsDestination(footprint, BlockPaddedExtent(mip, block)));
    CHECK(footprint.width % block.width == 0 && footprint.height % block.height == 0);
    CHECK(!CopyFitsDestination(footprint, mip));
  }
  // Uncompressed formats have 1x1 blocks and keep the logical size.
  CHECK(BlockPaddedExtent(mip9, {1, 1}) == mip9);
  CHECK(BlockPaddedExtent(mip10, {1, 1}) == mip10);
  CHECK(BlockPaddedExtent({1280, 720}, block) == (Extent{1280, 720}));
  // With scale 1 the scaled helpers are the identity.
  for (uint32_t level = 0; level <= 10; ++level) {
    CHECK(ScaledLoadFootprint(base, 1, level) == MipExtent(base, level));
    CHECK(ScaledHostMip(base, 1, level) == MipExtent(base, level));
  }
}

void TestScaledPackedLevelExtent() {
  std::printf("a packed mip tail level's extent is never less than either mip-first or scale-first, equal at 1x\n");
  // 1280x720 at 4x, mip 6: scale-first (720 * 4) >> 6 = 45 wins over mip-first (720 >> 6) * 4 = 44 - the exact
  // submission 8241 shortfall.
  CHECK(ScaledPackedLevelExtent(720, 4, 6) == 45);
  // Mip 10 of 720 is already 1 texel at the guest size, so the load shader's whole scale factor (mip-first) wins
  // over scale-first, which has shifted away precision scale-first would otherwise have kept: (720 >> 10) * 4 = 4
  // against (720 * 4) >> 10 = 2.
  CHECK(ScaledPackedLevelExtent(720, 4, 10) == 4);
  for (const uint32_t base : {uint32_t(720), uint32_t(1280)}) {
    for (int scale = kMinRenderScale; scale <= kMaxRenderScale; ++scale) {
      for (uint32_t level = 0; level <= 10; ++level) {
        const uint32_t extent = ScaledPackedLevelExtent(base, scale, level);
        const uint32_t mip_first = std::max(base >> level, 1u) * uint32_t(scale);
        const uint32_t scale_first = std::max((base * uint32_t(scale)) >> level, 1u);
        CHECK(extent >= mip_first);
        CHECK(extent >= scale_first);
        if (scale == 1) {
          CHECK(extent == mip_first && extent == scale_first);
        }
      }
    }
  }
}

void TestPatchesAndLauncher() {
  std::printf("patches and launcher\n");
  const std::string dir = std::string(MUA_SRC_DIR) + "/../patches/rexglue-sdk/";
  const std::string wrapper = ReadFile(dir + "0090-d3d12-3d-as-2d-wrapper-scaled-size.windows.patch");
  CHECK(Has(wrapper, "+  if (key().scaled_resolve) {"));
  CHECK(Has(wrapper, "+    desc.Width *= d3d12_cache.draw_resolution_scale_x();"));
  CHECK(Has(wrapper, "+    desc.Height *= d3d12_cache.draw_resolution_scale_y();"));
  CHECK(Has(wrapper, "D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION"));

  CHECK(!std::ifstream(dir + "0091-d3d12-clamp-scaled-texture-copy.windows.patch").good());
  CHECK(!std::ifstream(dir + "0092-d3d12-scaled-resolve-tile-mapping-check.windows.patch").good());
  for (const char* name :
       {"0090-d3d12-3d-as-2d-wrapper-scaled-size.windows.patch", "0091-d3d12-scaled-buffer-and-view-size-guards.windows.patch"}) {
    const std::string text = ReadFile(dir + name);
    CHECK(!Has(text, "clamped_source_box"));
    CHECK(!Has(text, "D3DCopyTextureRegion"));
  }

  const std::string guards = ReadFile(dir + "0091-d3d12-scaled-buffer-and-view-size-guards.windows.patch");
  CHECK(Has(guards, "D3D12_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP"));
  CHECK(Has(guards, "copy_buffer_size > (UINT64(1) << 31)"));
  CHECK(Has(guards, "draw_resolution_scaled &&"));

  const std::string extent = ReadFile(dir + "0092-d3d12-scaled-mip-copy-extent.windows.patch");
  CHECK(Has(extent, "+      if (texture_resolution_scaled) {"));
  CHECK(Has(extent, "std::max((width * texture_resolution_scale_x) >> level, uint32_t(1)), host_block_width);"));
  CHECK(Has(extent, "+          source_box.right = std::min(footprint.Width, dest_width);"));
  CHECK(Has(extent, "texture_util::GetPackedMipOffset(width, height, depth, guest_format, packed_level, offset_x,"));
  CHECK(Has(extent, "level_host_slice_layout.Footprint.Width = packed_right;"));
  CHECK(!Has(extent, "+      level_host_slice_layout.Footprint.Width = guest_layout_packed.x_extent_blocks"));

  const std::string launcher = ReadFile(std::string(MUA_SRC_DIR) + "/launcher.cpp");
  CHECK(Has(launcher, "scale <= kMaxRenderScale"));
}

void TestLabels() {
  std::printf("the launcher's Render resolution labels\n");
  CHECK(RenderResolutionLabel(AspectRatio::k16x9, 1) == "1x - 1280\xC3\x97" "720 (720p)");
  CHECK(RenderResolutionLabel(AspectRatio::k16x9, 2) == "2x - 2560\xC3\x97" "1440 (1440p)");
  CHECK(RenderResolutionLabel(AspectRatio::k16x9, 3) == "3x - 3840\xC3\x97" "2160 (4K)");
  CHECK(RenderResolutionLabel(AspectRatio::k16x9, 4) == "4x - 5120\xC3\x97" "2880 (5K)");
  // Other shapes name the height: 5040x2160 isn't 4K.
  CHECK(RenderResolutionLabel(AspectRatio::k21x9, 3) == "3x - 5040\xC3\x97" "2160 (2160p)");
  CHECK(RenderResolutionLabel(AspectRatio::k4x3, 1) == "1x - 960\xC3\x97" "720 (720p)");
  const std::string launcher = ReadFile(std::string(MUA_SRC_DIR) + "/launcher.cpp");
  CHECK(launcher.find("options.push_back({std::to_string(scale), RenderResolutionLabel(aspect, scale)});") !=
        std::string::npos);
  CHECK(launcher.find("Recommended for 1440p") == std::string::npos);
}

}  // namespace

int main() {
  TestScaledExtents();
  TestScaledExtentsPerAspect();
  TestEdramAndScaledResolve();
  TestPostEffects();
  TestVolumeWrapper();
  TestBlockCompressedTail();
  TestScaledPackedLevelExtent();
  TestPatchesAndLauncher();
  TestLabels();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
