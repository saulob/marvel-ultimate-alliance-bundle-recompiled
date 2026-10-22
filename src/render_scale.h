#pragma once

#include <algorithm>
#include <cstdint>

#include "aspect_ratio.h"

namespace mua {

// The launcher's Render resolution choices: 1x to 4x of the guest's 1280x720.
inline constexpr int kMinRenderScale = 1;
inline constexpr int kMaxRenderScale = 4;

// Limits of Direct3D 12 and of the runtime's resolution-scaled resources that the scale is multiplied into.
inline constexpr uint32_t kMaxHostTextureDimension = 16384;
inline constexpr uint64_t kMaxTypedViewElements = uint64_t(1) << 27;
inline constexpr uint64_t kMaxCopyBufferBytes = uint64_t(1) << 31;
inline constexpr int64_t kMaxViewportCoordinate = 32767;
inline constexpr uint32_t kMaxDispatchGroups = 65535;
inline constexpr uint64_t kEdramBytes = uint64_t(10) << 20;
inline constexpr uint64_t kGuestPhysicalBytes = uint64_t(512) << 20;
inline constexpr uint32_t kGuestMaxSurfaceDimension = 8192;
inline constexpr uint64_t kScaledResolveHeapBytes = uint64_t(16) << 20;
// Most scale the runtime's scaled resolve arrays are sized for.
inline constexpr int kRuntimeMaxScale = 7;

struct Extent {
  uint32_t width = 0;
  uint32_t height = 0;

  bool operator==(const Extent&) const = default;
};

inline int ClampRenderScale(int scale) {
  return std::clamp(scale, kMinRenderScale, kMaxRenderScale);
}

inline Extent ScaledGuestExtent(AspectRatio aspect, int scale) {
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  return {uint32_t(GuestWidth(aspect)) * s, uint32_t(kGuestHeight) * s};
}

inline uint64_t PixelCount(Extent extent) {
  return uint64_t(extent.width) * extent.height;
}

inline bool FitsHostTexture(Extent extent) {
  return extent.width <= kMaxHostTextureDimension && extent.height <= kMaxHostTextureDimension;
}

inline uint64_t EdramBufferBytes(int scale) {
  const uint64_t s = uint64_t(ClampRenderScale(scale));
  return kEdramBytes * s * s;
}

// The scaled resolve address space is the guest's physical memory times the scale squared.
inline uint64_t ScaledResolveAddressSpaceBytes(int scale) {
  const uint64_t s = uint64_t(ClampRenderScale(scale));
  return kGuestPhysicalBytes * s * s;
}

// 2 GB buffers sliding by 1 GB, as the runtime's GetScaledResolveBufferCount counts them.
inline uint64_t ScaledResolveBufferCount(int scale) {
  return (ScaledResolveAddressSpaceBytes(scale) - 1) >> 30;
}

inline uint64_t ScaledResolveBufferCapacity() {
  const uint64_t s = kRuntimeMaxScale;
  return (kGuestPhysicalBytes * s * s - 1) >> 30;
}

inline uint64_t ScaledResolveHeapSlots(int scale) {
  return ScaledResolveAddressSpaceBytes(scale) / kScaledResolveHeapBytes;
}

// The 2D wrapper of a volume texture holds one slice, sized like the texture it is loaded as.
inline Extent VolumeWrapperExtent(Extent guest_slice, bool scaled_resolve, int scale) {
  if (!scaled_resolve) {
    return guest_slice;
  }
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  return {guest_slice.width * s, guest_slice.height * s};
}

// The copy of a loaded level has to lie inside the destination mip.
inline bool CopyFitsDestination(Extent footprint, Extent destination_mip) {
  return footprint.width <= destination_mip.width && footprint.height <= destination_mip.height;
}

// A block-compressed level is stored and copied in whole blocks.
inline Extent BlockPaddedExtent(Extent extent, Extent block) {
  return {(extent.width + block.width - 1) / block.width * block.width,
          (extent.height + block.height - 1) / block.height * block.height};
}

inline Extent MipExtent(Extent base, uint32_t level) {
  return {std::max<uint32_t>(base.width >> level, 1), std::max<uint32_t>(base.height >> level, 1)};
}

// The host mip of a scaled texture, and the footprint the load shader writes for the same level.
inline Extent ScaledHostMip(Extent guest_base, int scale, uint32_t level) {
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  return MipExtent({guest_base.width * s, guest_base.height * s}, level);
}

inline Extent ScaledLoadFootprint(Extent guest_base, int scale, uint32_t level) {
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  const Extent mip = MipExtent(guest_base, level);
  return {mip.width * s, mip.height * s};
}

// The part of a scaled level's footprint copied into the host mip (patch 0092): at most the mip padded to whole blocks,
// since CopyTextureRegion takes block-aligned boxes and lets them cover a partial block at the mip's edge.
inline Extent ScaledLoadCopyExtent(Extent footprint, Extent host_mip, Extent block) {
  const Extent padded = BlockPaddedExtent(host_mip, block);
  return {std::min(footprint.width, padded.width), std::min(footprint.height, padded.height)};
}

// A packed mip tail level's row or column count past its guest-scaled offset (texture_cache.cpp, patch 0092): the
// larger of the two genuine lower bounds on what the load shader writes there. Mip-first, max(base >> level, 1) *
// scale, is what it writes once the guest mip has collapsed to 1 texel (a whole scale factor, regardless of level).
// Scale-first, max((base * scale) >> level, 1), is larger while the guest mip is still more than 1 texel, since
// shifting the already-scaled dimension loses less precision. The two are equal at scale 1.
inline uint32_t ScaledPackedLevelExtent(uint32_t base, int scale, uint32_t level) {
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  const uint32_t mip_first = std::max(base >> level, uint32_t(1)) * s;
  const uint32_t scale_first = std::max((base * s) >> level, uint32_t(1));
  return std::max(mip_first, scale_first);
}

// The swap gamma and FXAA passes run 16x8 threads per group over the guest output.
inline Extent PostEffectGroups(Extent output) {
  return {(output.width + 15) / 16, (output.height + 7) / 8};
}

// The viewport of a draw without clipping spans the largest guest surface, kept inside the Direct3D 12 viewport range.
inline uint32_t UnclippedViewportExtent(int scale) {
  const uint32_t s = uint32_t(ClampRenderScale(scale));
  return std::min<uint32_t>(kGuestMaxSurfaceDimension, uint32_t(kMaxViewportCoordinate / s)) * s;
}

// A typed view over a range of a resolution-scaled buffer.
inline bool FitsTypedView(uint64_t range_bytes, uint32_t element_bytes_log2) {
  return (range_bytes >> element_bytes_log2) <= kMaxTypedViewElements;
}

// One-use view descriptors of the swap: the gamma pass takes a destination and a source (and the ramp without
// bindless), FXAA a destination and a source more. The scale changes the sizes behind them, not their number.
inline uint32_t SwapDescriptorCount(bool fxaa, bool bindless) {
  return (bindless ? 2u : 3u) + (fxaa ? 2u : 0u);
}

}  // namespace mua
