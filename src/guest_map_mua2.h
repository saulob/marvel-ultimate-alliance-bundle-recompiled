#pragma once

#include <cstdint>

// This title has no runtime-filled tail-call table, no spinning mix-worker rendezvous and no inline event reset.
#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 0
#define MUA_GUEST_HAS_AUDIO_BARRIER 0
#define MUA_GUEST_HAS_RESET_EVENT 0
#define MUA_GUEST_HAS_MEMMOVE 1

#define MUA_GUEST_PRESENT_CALLBACK sub_829C6410
#define MUA_GUEST_SWAP sub_829C6A98
#define MUA_GUEST_WORLD_UPDATE sub_826011D8
#define MUA_GUEST_OPTIONS_INIT sub_82533548
#define MUA_GUEST_FENCE_BEGIN sub_829BB820
#define MUA_GUEST_FENCE_POLL sub_829BB918
#define MUA_GUEST_AUDIO_RENDER sub_82A25E50
#define MUA_GUEST_MEMCPY sub_82A616F0
#define MUA_GUEST_MEMSET sub_82A61C20
#define MUA_GUEST_MEMMOVE sub_82A65700
#define MUA_GUEST_PROFILE_READ sub_82A4EF08
#define MUA_GUEST_PROFILE_WRITE sub_82A4EF70
#define MUA_GUEST_CONTENT_CREATE sub_829AC488
#define MUA_GUEST_CONTENT_CLOSE sub_829AC518
#define MUA_GUEST_CONTENT_ENUMERATE sub_829AC528
#define MUA_GUEST_DEVICE_SELECTOR sub_829ACA80

namespace mua::guest {

inline constexpr uint32_t kFenceBeginSite = 0x829C2CCC;
inline constexpr uint32_t kFencePollSite = 0x829C2CD8;
inline constexpr uint32_t kDeviceFenceOffset = 10896;
inline constexpr uint32_t kDeviceFlagsOffset = 10941;
inline constexpr uint32_t kRepeatPresentSite = 0x829D81E0;
inline constexpr uint32_t kWorldDeltaOffset = 1676;

// The callback's argument: swap interval in bits 8-11; zero releases the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t argument) {
  return argument & ~uint32_t{0xF00};
}

}  // namespace mua::guest
