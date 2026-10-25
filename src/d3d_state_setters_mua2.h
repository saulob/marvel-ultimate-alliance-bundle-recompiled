#pragma once

#include <array>
#include <cstdint>

// MUA 2's D3D device fields that differ between D3D builds (its later D3D build moves the register shadows and most
// state up), and its render state and sampler state setters, in the order of MUA1's tables (its D3D state tables list
// them in the same order with the same defaults). guest_map_tests checks them against the generated code.

namespace mua::d3d {

inline constexpr uint32_t kShaderConstants = 1152;
inline constexpr uint32_t kFetchConstants = 1152;
inline constexpr uint32_t kVertexFetchConstants = 1776;
inline constexpr uint32_t kVsAluConstants = 1920;
inline constexpr uint32_t kPsAluConstants = 6016;
inline constexpr uint32_t kCurrentFence = 10908;
inline constexpr uint32_t kCommandCursor = 48;
inline constexpr uint32_t kCommandEnd = 52;
inline constexpr uint32_t kCommandReserveLimit = 56;
inline constexpr uint32_t kTextureDirty = 24;
inline constexpr uint32_t kFlags = 10942;
inline constexpr uint32_t kVertexDeclaration = 11824;
inline constexpr uint32_t kCommittedStrides = 11840;
inline constexpr uint32_t kStreamSources = 12468;
inline constexpr uint32_t kStreamStrides = 12536;
inline constexpr uint32_t kTextures = 12552;
inline constexpr uint32_t kPixelShader = 12700;
inline constexpr uint32_t kVertexShader = 12704;
inline constexpr std::array<uint32_t, 4> kSamplerBytes = {11932, 11958, 11984, 12010};
// In a shader's literal table: the first entry and the size of all entries.
inline constexpr uint32_t kLiteralEntries = 20;
inline constexpr uint32_t kLiteralBytes = 16;

}  // namespace mua::d3d

#define MUA_D3D_LEAF_SETTERS(X) \
  X(829BC380, MUA_FIELD(10568, 4)) \
  X(829BC3B0, MUA_FIELD(10568, 4)) \
  X(829BC3E0, MUA_FIELD(10556, 4)) \
  X(829BC418, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11860, 4)) \
  X(829BC4A8, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC538, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC5C8, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC658, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC6C8, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC738, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11856, 4)) \
  X(829BC7A8, MUA_FIELD(10552, 4), MUA_FIELD(10584, 4), MUA_FIELD(10588, 4), MUA_FIELD(10592, 4), MUA_FIELD(11860, 4)) \
  X(829BC840, MUA_FIELD(10500, 4)) \
  X(829BC8A0, MUA_FIELD(10556, 4)) \
  X(829BC8D0, MUA_FIELD(10464, 4), MUA_FIELD(10468, 4), MUA_FIELD(10472, 4), MUA_FIELD(10476, 4)) \
  X(829BCAA8, MUA_FIELD(10548, 4), MUA_FIELD(11884, 4)) \
  X(829BCAE8, MUA_FIELD(10548, 4)) \
  X(829BCB18, MUA_FIELD(10548, 4)) \
  X(829BCB50, MUA_FIELD(10548, 4), MUA_FIELD(11888, 4)) \
  X(829BCB90, MUA_FIELD(10548, 4)) \
  X(829BCBC8, MUA_FIELD(10548, 4)) \
  X(829BCBF8, MUA_FIELD(10548, 4)) \
  X(829BCC30, MUA_FIELD(10548, 4)) \
  X(829BCC68, MUA_FIELD(10548, 4)) \
  X(829BCC98, MUA_FIELD(10548, 4)) \
  X(829BCCC8, MUA_FIELD(10548, 4)) \
  X(829BCD00, MUA_FIELD(10548, 4)) \
  X(829BCD38, MUA_FIELD(10548, 4)) \
  X(829BCD68, MUA_FIELD(10499, 1)) \
  X(829BCD88, MUA_FIELD(10498, 1)) \
  X(829BCDA8, MUA_FIELD(10497, 1)) \
  X(829BCDC8, MUA_FIELD(10495, 1)) \
  X(829BCDE8, MUA_FIELD(10494, 1)) \
  X(829BCE08, MUA_FIELD(10493, 1)) \
  X(829BCE28, MUA_FIELD(10420, 4), MUA_FIELD(10564, 4)) \
  X(829BCE88, MUA_FIELD(10568, 4), MUA_FIELD(10832, 4), MUA_FIELD(10840, 4)) \
  X(829BCF50, MUA_FIELD(10568, 4), MUA_FIELD(10836, 4), MUA_FIELD(10844, 4)) \
  X(829BCFF8, MUA_FIELD(10568, 4)) \
  X(829BD028, MUA_FIELD(10752, 4)) \
  X(829BD048, MUA_FIELD(10460, 4), MUA_FIELD(11868, 4)) \
  X(829BD088, MUA_FIELD(10460, 4), MUA_FIELD(11872, 4)) \
  X(829BD0C8, MUA_FIELD(10460, 4), MUA_FIELD(11876, 4)) \
  X(829BD108, MUA_FIELD(10460, 4), MUA_FIELD(11880, 4)) \
  X(829BD250, MUA_FIELD(10540, 4)) \
  X(829BD270, MUA_FIELD(10540, 4)) \
  X(829BD298, MUA_FIELD(10540, 4)) \
  X(829BD2C0, MUA_FIELD(10540, 4)) \
  X(829BD2E8, MUA_FIELD(10540, 4)) \
  X(829BD310, MUA_FIELD(10540, 4)) \
  X(829BD338, MUA_FIELD(10540, 4)) \
  X(829BD360, MUA_FIELD(10540, 4)) \
  X(829BD380, MUA_FIELD(10544, 4)) \
  X(829BD3A0, MUA_FIELD(10544, 4)) \
  X(829BD3C8, MUA_FIELD(10544, 4)) \
  X(829BD3F0, MUA_FIELD(10544, 4)) \
  X(829BD418, MUA_FIELD(10544, 4)) \
  X(829BD440, MUA_FIELD(10544, 4)) \
  X(829BD468, MUA_FIELD(10544, 4)) \
  X(829BD490, MUA_FIELD(10544, 4)) \
  X(829BD5B0, MUA_FIELD(10564, 4), MUA_FIELD(10572, 4)) \
  X(829BD860, MUA_FIELD(10624, 4)) \
  X(829BD898, MUA_FIELD(10620, 4)) \
  X(829BD8D0, MUA_FIELD(10616, 4)) \
  X(829BD908, MUA_FIELD(10688, 4)) \
  X(829BD940, MUA_FIELD(10568, 4)) \
  X(829BD970, MUA_FIELD(10456, 4)) \
  X(829BD998, MUA_FIELD(10556, 4)) \
  X(829BD9C8, MUA_FIELD(10556, 4)) \
  X(829BD9F0, MUA_FIELD(10700, 4)) \
  X(829BDA20, MUA_FIELD(10692, 4)) \
  X(829BDA50, MUA_FIELD(10704, 4)) \
  X(829BDA78, MUA_FIELD(10696, 4)) \
  X(829BDAA8, MUA_FIELD(11860, 4)) \
  X(829BDB40, MUA_FIELD(10560, 4)) \
  X(829BDB70, MUA_FIELD(10560, 4)) \
  X(829BDBA0, MUA_FIELD(10560, 4)) \
  X(829BDBD0, MUA_FIELD(10562, 1))

#define MUA_D3D_SAMPLER_SETTERS(X) \
  X(829BDC20) \
  X(829BDD20) \
  X(829BDDC8) \
  X(829BDEC8) \
  X(829BDF68) \
  X(829BDFC0) \
  X(829BE068) \
  X(829BE0E0) \
  X(829BE188) \
  X(829BE228) \
  X(829BE2A8) \
  X(829BE328) \
  X(829BE398) \
  X(829BE3E8) \
  X(829BE438) \
  X(829BE488) \
  X(829BE4E0) \
  X(829BE538) \
  X(829BE590) \
  X(829BE5E8)
