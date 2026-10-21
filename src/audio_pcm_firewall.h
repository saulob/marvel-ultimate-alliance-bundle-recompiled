#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace mua {

// The guest mixer frame: 6 channels of 256 big-endian float32 samples.
inline constexpr size_t kPcmFirewallSamples = 6 * 256;

struct PcmFirewallCounts {
  uint64_t sanitized_nan = 0;
  uint64_t sanitized_inf = 0;
  uint64_t clamped_high = 0;
  uint64_t clamped_low = 0;
  uint64_t sanitized_frames = 0;

  uint64_t Replaced() const { return sanitized_nan + sanitized_inf + clamped_high + clamped_low; }
};

inline float SanitizePcmSample(float value, PcmFirewallCounts& counts) {
  if (std::isnan(value)) {
    ++counts.sanitized_nan;
    return 0.0f;
  }
  if (std::isinf(value)) {
    ++counts.sanitized_inf;
    return 0.0f;
  }
  if (value > 1.0f) {
    ++counts.clamped_high;
    return 1.0f;
  }
  if (value < -1.0f) {
    ++counts.clamped_low;
    return -1.0f;
  }
  return value;
}

// Replaces NaN and infinity with silence and clamps the rest to [-1, 1], in place.
inline void SanitizeBigEndianPcm(uint8_t* bytes, size_t sample_count, PcmFirewallCounts& counts) {
  const uint64_t replaced_before = counts.Replaced();
  for (size_t i = 0; i < sample_count; ++i) {
    uint8_t* slot = bytes + i * sizeof(uint32_t);
    uint32_t word;
    std::memcpy(&word, slot, sizeof(word));
    const float value = std::bit_cast<float>(std::byteswap(word));
    const float sanitized = SanitizePcmSample(value, counts);
    if (std::bit_cast<uint32_t>(sanitized) != std::bit_cast<uint32_t>(value)) {
      word = std::byteswap(std::bit_cast<uint32_t>(sanitized));
      std::memcpy(slot, &word, sizeof(word));
    }
  }
  if (counts.Replaced() != replaced_before) ++counts.sanitized_frames;
}

}  // namespace mua
