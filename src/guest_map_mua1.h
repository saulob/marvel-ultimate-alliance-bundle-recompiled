#pragma once

#include <cstdint>

// Codegen emits a dynamic call for the runtime-filled table that Gold Edition has to hook.
#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 0
#define MUA_GUEST_HAS_AUDIO_BARRIER 1
#define MUA_GUEST_HAS_RESET_EVENT 1
#define MUA_GUEST_HAS_MEMMOVE 1

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
#define MUA_GUEST_MEMMOVE sub_8282A150
#define MUA_GUEST_PROFILE_READ sub_824B25C8
#define MUA_GUEST_PROFILE_WRITE sub_824B2630
#define MUA_GUEST_CONTENT_CREATE sub_82466A18
#define MUA_GUEST_CONTENT_CLOSE sub_82466A80
#define MUA_GUEST_CONTENT_ENUMERATE sub_82466A88
#define MUA_GUEST_DEVICE_SELECTOR sub_82467240
#define MUA_GUEST_INTRO_GATE sub_82111868
#define MUA_GUEST_MOVIE_MENU_UPDATE sub_823F51E8
#define MUA_GUEST_OPEN_MENU sub_8240DC10
#define MUA_GUEST_MENU_MANAGER sub_82409678
#define MUA_GUEST_MOVIE_PLAYING sub_82402150
#define MUA_GUEST_STOP_MOVIE sub_82403A98
#define MUA_GUEST_SET_ACTIVE_PAD sub_82408A20
#define MUA_GUEST_USER_MANAGER sub_821E7070
#define MUA_GUEST_USER_SET_CONTROLLER sub_821E8CC8
#define MUA_GUEST_USER_REFRESH sub_821E6708
#define MUA_GUEST_USER_GET_SLOT sub_821E6E20
#define MUA_GUEST_USER_SET_SLOT sub_82619520
#define MUA_GUEST_INPUT_MANAGER sub_82456B48
#define MUA_GUEST_INPUT_SET_NAME sub_82454558
#define MUA_GUEST_INPUT_SET_FLAGS sub_82454420
#define MUA_GUEST_INPUT_CLEAR_FLAGS sub_82454450
#define MUA_GUEST_PROFILE_MANAGER sub_82355F50
#define MUA_GUEST_PROFILE_REFRESH_PAD sub_82355F60
#define MUA_GUEST_PROFILE_RESET_ALL sub_823556B0
#define MUA_GUEST_PROFILE_QUERY_ALL sub_82355708
#define MUA_GUEST_PROFILE_SIGNED_IN sub_82355768
#define MUA_GUEST_PROFILE_ONLINE sub_82355788
#define MUA_GUEST_PROFILE_NAME sub_82355E70
#define MUA_GUEST_SIGNIN_PUMP sub_82356E58
#define MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A sub_82356D68
#define MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B sub_82356D70
#define MUA_GUEST_CONSOLE sub_823453A8
#define MUA_GUEST_CONSOLE_QUEUE sub_82344CB0
#define MUA_GUEST_PAD_MANAGER sub_8233C588
#define MUA_GUEST_PAD_CONNECTED sub_8233AD90
#define MUA_GUEST_SCRIPT_DISPATCH sub_82344378
#define MUA_GUEST_MENU_ITEM_CREATE sub_823CF578
#define MUA_GUEST_ITEM_USE sub_823DC040
#define MUA_GUEST_STRING_DATA sub_82390570

namespace mua::guest {

inline constexpr uint32_t kFenceBeginSite = 0x824728E0;
inline constexpr uint32_t kFencePollSite = 0x824728EC;
inline constexpr uint32_t kDeviceFenceOffset = 10768;
inline constexpr uint32_t kDeviceFlagsOffset = 10809;
inline constexpr uint32_t kRepeatPresentSite = 0x82484914;
inline constexpr uint32_t kWorldDeltaOffset = 1000;
inline constexpr uint32_t kInputUserSlotOffset = 1488;
inline constexpr uint32_t kMainMenuExitCommand = 0x82015DE8;
inline constexpr uint32_t kMainMenuVtable = 0x8203F634;
inline constexpr uint32_t kMainMenuLiveNameEntry = 0x8288D5F8;
inline constexpr uint32_t kEmptyString = 0x820034A0;

// The callback's argument: swap interval in the high half, scanline threshold in percent in the low half. No
// interval and a threshold the scanline never passes release the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t) {
  return 100;
}

}  // namespace mua::guest
