#pragma once

#include <cstdint>

#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 1
#define MUA_GUEST_HAS_AUDIO_BARRIER 1
#define MUA_GUEST_HAS_RESET_EVENT 1
#define MUA_GUEST_HAS_MEMMOVE 1
#define MUA_GUEST_HAS_D3D_STATE_FILTER 1
#define MUA_GUEST_HAS_ALCHEMY_CORE 1
#define MUA_GUEST_HAS_ALCHEMY_HELPERS 1
#define MUA_GUEST_HAS_OBJECT_HANDLES 0

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
#define MUA_GUEST_MEMMOVE sub_82859500
#define MUA_GUEST_SET_VERTEX_DECLARATION sub_8248F378
#define MUA_GUEST_SET_VERTEX_SHADER sub_8248F160
#define MUA_GUEST_SET_PIXEL_SHADER sub_8248EE60
#define MUA_GUEST_SET_STREAM_SOURCE sub_82487220
#define MUA_GUEST_SET_TEXTURE sub_8248A0A0
#define MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS sub_8248EA90
#define MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS sub_8248EB98
#define MUA_GUEST_UPLOAD_REGISTERS sub_8249FC30
#define MUA_GUEST_UPLOAD_ALU_CONSTANTS sub_8249FFD0
#define MUA_GUEST_UPLOAD_FETCH_CONSTANTS sub_8249FE70
#define MUA_GUEST_WRITE_PACKETS sub_8249F5F8
#define MUA_GUEST_RESERVE_COMMANDS sub_8248B578
#define MUA_GUEST_ADD_REF sub_82540AE0
#define MUA_GUEST_RELEASE sub_82563BB8
#define MUA_GUEST_ASSIGN_REF sub_8256B0D0
#define MUA_GUEST_ASSIGN_ADOPTED_REF sub_8256B170
#define MUA_GUEST_DESTROY_OBJECT sub_82561018
#define MUA_GUEST_MATRIX_NEARLY_EQUAL sub_825B3330
#define MUA_GUEST_AND_MASKS sub_823A2C20
#define MUA_GUEST_INDEX_ALLOWED sub_825027A8
#define MUA_GUEST_IS_OF_TYPE sub_82541718
#define MUA_GUEST_MATRIX_MULTIPLY sub_825B9288
#define MUA_GUEST_MATRIX_COPY sub_825B35C8
#define MUA_GUEST_RELEASE_STRING sub_82371918
#define MUA_GUEST_ASSIGN_STRING sub_82389FE0
#define MUA_GUEST_RELEASE_RESOURCE sub_82494898
#define MUA_GUEST_APPEND_REF sub_82578180
#define MUA_GUEST_FREE_STRING sub_8256B3C8
#define MUA_GUEST_STRING_PREPARE sub_823715F0
#define MUA_GUEST_STRING_INTERN sub_82570A48
#define MUA_GUEST_DESTROY_RESOURCE sub_824942F8
#define MUA_GUEST_RESIZE_ARRAY sub_8256F050
#define MUA_GUEST_PROFILE_READ sub_824CB818
#define MUA_GUEST_PROFILE_WRITE sub_824CB880
#define MUA_GUEST_CONTENT_CREATE sub_8247EDF0
#define MUA_GUEST_CONTENT_CLOSE sub_8247EE58
#define MUA_GUEST_CONTENT_ENUMERATE sub_8247EE60
#define MUA_GUEST_DEVICE_SELECTOR sub_8247F6C8
#define MUA_GUEST_INTRO_GATE sub_82121898
#define MUA_GUEST_MOVIE_MENU_UPDATE sub_8240B8B8
#define MUA_GUEST_OPEN_MENU sub_82424738
#define MUA_GUEST_MENU_MANAGER sub_82420268
#define MUA_GUEST_MOVIE_PLAYING sub_82418C50
#define MUA_GUEST_STOP_MOVIE sub_8241A600
#define MUA_GUEST_SET_ACTIVE_PAD sub_8241F730
#define MUA_GUEST_USER_MANAGER sub_821F8688
#define MUA_GUEST_USER_SET_CONTROLLER sub_821FA2C8
#define MUA_GUEST_USER_REFRESH sub_821F7D28
#define MUA_GUEST_USER_GET_SLOT sub_8263AE38
#define MUA_GUEST_USER_SET_SLOT sub_82633D00
#define MUA_GUEST_INPUT_MANAGER sub_8246EFD0
#define MUA_GUEST_INPUT_SET_NAME sub_8246C4C8
#define MUA_GUEST_INPUT_SET_FLAGS sub_8246C390
#define MUA_GUEST_INPUT_CLEAR_FLAGS sub_8246C3C0
#define MUA_GUEST_PROFILE_MANAGER sub_8236ABB8
#define MUA_GUEST_PROFILE_REFRESH_PAD sub_8236ABC8
#define MUA_GUEST_PROFILE_RESET_ALL sub_8236A258
#define MUA_GUEST_PROFILE_QUERY_ALL sub_8236A2B0
#define MUA_GUEST_PROFILE_SIGNED_IN sub_8236A310
#define MUA_GUEST_PROFILE_ONLINE sub_8236A330
#define MUA_GUEST_PROFILE_NAME sub_8236AAD8
#define MUA_GUEST_PLAYER_NUMBER_NAME sub_821FB9F8
#define MUA_GUEST_SIGNIN_PUMP sub_8236BBB0
#define MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A sub_8236BAC0
#define MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B sub_8236BAC8
#define MUA_GUEST_CONSOLE sub_82359770
#define MUA_GUEST_CONSOLE_QUEUE sub_82359080
#define MUA_GUEST_PAD_MANAGER sub_823508A0
#define MUA_GUEST_PAD_CONNECTED sub_8234F090
#define MUA_GUEST_SCRIPT_DISPATCH sub_82358780
#define MUA_GUEST_MENU_ITEM_CREATE sub_823E5290
#define MUA_GUEST_ITEM_USE sub_823F1F78
#define MUA_GUEST_STRING_DATA sub_823A5948

namespace mua::guest {

inline constexpr uint32_t kFencePollSite = 0x8248AFB8;
inline constexpr uint32_t kRingSpacePollSite = 0x8248A38C;
inline constexpr uint32_t kRingProgressPollSite = 0x8248A2E8;
inline constexpr uint32_t kDeviceFenceOffset = 10768;
inline constexpr uint32_t kDeviceFlagsOffset = 10813;
inline constexpr uint32_t kRepeatPresentSite = 0x8249EFE0;
inline constexpr uint32_t kWorldDeltaOffset = 1000;
inline constexpr uint32_t kInputUserSlotOffset = 1520;
inline constexpr uint32_t kMainMenuExitCommand = 0x82015A10;
inline constexpr uint32_t kMainMenuVtable = 0x8203E4CC;
inline constexpr uint32_t kMainMenuLiveNameEntry = 0x828BE2E0;
inline constexpr uint32_t kEmptyString = 0x820034D8;
inline constexpr uint32_t kIndirectTailTable = 0x820C8878;
inline constexpr uint32_t kRefCountMask = 0x3FFFFF;
inline constexpr bool kAssignRefReturnsSlot = false;
inline constexpr uint32_t kMatrixEpsilon = 0x8200B67C;
inline constexpr uint32_t kIsOfTypeMetaSite = 0x8254173C;
inline constexpr uint32_t kAssignStringPrepareSite = 0x8238A000;
inline constexpr uint32_t kAssignStringInternSite = 0x8238A008;
inline constexpr uint32_t kAssignStringFreeSite = 0x8238A058;
inline constexpr uint32_t kReleaseResourceParentSite = 0x824948F8;
inline constexpr uint32_t kReleaseResourceDestroySite = 0x82494900;
inline constexpr uint32_t kAppendRefResizeSite = 0x825781F8;
inline constexpr uint32_t kImageSize = 0xD00000;
inline constexpr uint32_t kCodeBase = 0x82120000;
inline constexpr uint32_t kCodeSize = 0x760ECC;

// The callback's argument: swap interval in bits 8-11; zero releases the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t argument) {
  return argument & ~uint32_t{0xF00};
}

}  // namespace mua::guest
