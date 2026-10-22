#pragma once

#include <cstdint>

#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 1
#define MUA_GUEST_HAS_AUDIO_BARRIER 1
#define MUA_GUEST_HAS_RESET_EVENT 1

#define MUA_GUEST_PRESENT_CALLBACK sub_82497D00
#define MUA_GUEST_SWAP sub_82498340
#define MUA_GUEST_WORLD_UPDATE sub_821BAE40
#define MUA_GUEST_OPTIONS_INIT sub_821212A8
#define MUA_GUEST_FENCE_BEGIN sub_824844C0
#define MUA_GUEST_FENCE_POLL sub_82484670
#define MUA_GUEST_AUDIO_RENDER sub_824AF110
#define MUA_GUEST_AUDIO_BARRIER sub_824A6BA8
#define MUA_GUEST_RESET_EVENT sub_8282DE78
#define MUA_GUEST_INDIRECT_TAIL_CALL sub_82839360
#define MUA_GUEST_MEMCPY sub_828564E0
#define MUA_GUEST_MEMSET sub_82857020
#define MUA_GUEST_PROFILE_READ sub_824CB818
#define MUA_GUEST_PROFILE_WRITE sub_824CB880
#define MUA_GUEST_CONTENT_CREATE sub_8247EDF0
#define MUA_GUEST_CONTENT_CLOSE sub_8247EE58
#define MUA_GUEST_CONTENT_ENUMERATE sub_8247EE60
#define MUA_GUEST_DEVICE_SELECTOR sub_8247F6C8

namespace mua::guest {

inline constexpr uint32_t kFenceBeginSite = 0x8248AFAC;
inline constexpr uint32_t kFencePollSite = 0x8248AFB8;
inline constexpr uint32_t kDeviceFenceOffset = 10768;
inline constexpr uint32_t kDeviceFlagsOffset = 10813;
inline constexpr uint32_t kRepeatPresentSite = 0x8249EFE0;
inline constexpr uint32_t kWorldDeltaOffset = 1000;
inline constexpr uint32_t kIndirectTailTable = 0x820C8878;

// The callback's argument: swap interval in bits 8-11; zero releases the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t argument) {
  return argument & ~uint32_t{0xF00};
}

}  // namespace mua::guest
