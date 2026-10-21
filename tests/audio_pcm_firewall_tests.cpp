#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "audio_pcm_firewall.h"

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

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void StoreBE(std::vector<uint8_t>& bytes, size_t index, float value) {
  const uint32_t word = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(bytes.data() + index * sizeof(word), &word, sizeof(word));
}

float LoadBE(const std::vector<uint8_t>& bytes, size_t index) {
  uint32_t word;
  std::memcpy(&word, bytes.data() + index * sizeof(word), sizeof(word));
  return std::bit_cast<float>(std::byteswap(word));
}

void TestSampleRules() {
  PcmFirewallCounts counts;
  CHECK(SanitizePcmSample(kNan, counts) == 0.0f);
  CHECK(SanitizePcmSample(kInf, counts) == 0.0f);
  CHECK(SanitizePcmSample(-kInf, counts) == 0.0f);
  CHECK(SanitizePcmSample(2.0f, counts) == 1.0f);
  CHECK(SanitizePcmSample(-2.0f, counts) == -1.0f);
  CHECK(SanitizePcmSample(0.5f, counts) == 0.5f);
  CHECK(SanitizePcmSample(-1.0f, counts) == -1.0f);
  CHECK(counts.sanitized_nan == 1 && counts.sanitized_inf == 2);
  CHECK(counts.clamped_high == 1 && counts.clamped_low == 1);
}

void TestMixedFrame() {
  std::vector<uint8_t> bytes(kPcmFirewallSamples * sizeof(uint32_t) + 4, 0xAB);
  for (size_t i = 0; i < kPcmFirewallSamples; ++i) StoreBE(bytes, i, 0.5f);
  StoreBE(bytes, 0, kNan);
  StoreBE(bytes, 1, kInf);
  StoreBE(bytes, 2, 2.0f);
  StoreBE(bytes, 3, -2.0f);
  StoreBE(bytes, 4, -0.25f);

  PcmFirewallCounts counts;
  SanitizeBigEndianPcm(bytes.data(), kPcmFirewallSamples, counts);

  CHECK(LoadBE(bytes, 0) == 0.0f && LoadBE(bytes, 1) == 0.0f);
  CHECK(LoadBE(bytes, 2) == 1.0f && LoadBE(bytes, 3) == -1.0f);
  CHECK(LoadBE(bytes, 4) == -0.25f && LoadBE(bytes, 5) == 0.5f);
  bool finite_and_bounded = true;
  for (size_t i = 0; i < kPcmFirewallSamples; ++i) {
    const float value = LoadBE(bytes, i);
    finite_and_bounded &= std::isfinite(value) && value >= -1.0f && value <= 1.0f;
  }
  CHECK(finite_and_bounded);
  CHECK(counts.sanitized_nan == 1 && counts.sanitized_inf == 1);
  CHECK(counts.clamped_high == 1 && counts.clamped_low == 1);
  CHECK(counts.sanitized_frames == 1);
  for (size_t i = kPcmFirewallSamples * sizeof(uint32_t); i < bytes.size(); ++i) CHECK(bytes[i] == 0xAB);
}

void TestAllInvalidFrameIsSilence() {
  std::vector<uint8_t> bytes(kPcmFirewallSamples * sizeof(uint32_t));
  for (size_t i = 0; i < kPcmFirewallSamples; ++i) StoreBE(bytes, i, kNan);

  PcmFirewallCounts counts;
  SanitizeBigEndianPcm(bytes.data(), kPcmFirewallSamples, counts);

  bool silent = true;
  for (size_t i = 0; i < kPcmFirewallSamples; ++i) silent &= LoadBE(bytes, i) == 0.0f;
  CHECK(silent);
  CHECK(counts.sanitized_nan == kPcmFirewallSamples);
  CHECK(counts.sanitized_frames == 1);
}

void TestCleanFrameUnchanged() {
  std::vector<uint8_t> bytes(kPcmFirewallSamples * sizeof(uint32_t));
  for (size_t i = 0; i < kPcmFirewallSamples; ++i) StoreBE(bytes, i, (i % 3) * 0.25f - 0.25f);
  const std::vector<uint8_t> original = bytes;

  PcmFirewallCounts counts;
  SanitizeBigEndianPcm(bytes.data(), kPcmFirewallSamples, counts);

  CHECK(bytes == original);
  CHECK(counts.Replaced() == 0);
  CHECK(counts.sanitized_frames == 0);
}

}  // namespace

int main() {
  TestSampleRules();
  TestMixedFrame();
  TestAllInvalidFrameIsSilence();
  TestCleanFrameUnchanged();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
