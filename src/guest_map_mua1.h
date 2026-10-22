#pragma once

#include <cstdint>

// Codegen emits a dynamic call for the runtime-filled table that Gold Edition has to hook.
#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 0

#define MUA_GUEST_PRESENT_CALLBACK sub_8248A0A0
#define MUA_GUEST_SWAP sub_8248A2B8
#define MUA_GUEST_WORLD_UPDATE sub_821AA278
#define MUA_GUEST_OPTIONS_INIT sub_82111298
#define MUA_GUEST_FENCE_BEGIN sub_8246C090
#define MUA_GUEST_FENCE_POLL sub_8246C240
#define MUA_GUEST_AUDIO_RENDER sub_82494B40
#define MUA_GUEST_AUDIO_BARRIER sub_8248C808
#define MUA_GUEST_RESET_EVENT sub_8248C688
#define MUA_GUEST_MEMCPY sub_82827120
#define MUA_GUEST_MEMSET sub_82827C40
#define MUA_GUEST_PROFILE_READ sub_824B25C8
#define MUA_GUEST_PROFILE_WRITE sub_824B2630
#define MUA_GUEST_CONTENT_CREATE sub_82466A18
#define MUA_GUEST_CONTENT_CLOSE sub_82466A80
#define MUA_GUEST_CONTENT_ENUMERATE sub_82466A88
#define MUA_GUEST_DEVICE_SELECTOR sub_82467240

namespace mua::guest {

inline constexpr uint32_t kFenceBeginSite = 0x824728E0;
inline constexpr uint32_t kFencePollSite = 0x824728EC;
inline constexpr uint32_t kDeviceFlagsOffset = 10809;
inline constexpr uint32_t kRepeatPresentSite = 0x82484914;

// The callback's argument: swap interval in the high half, scanline threshold in percent in the low half. No
// interval and a threshold the scanline never passes release the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t) {
  return 100;
}

}  // namespace mua::guest
