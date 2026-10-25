#pragma once

#include <cstdint>

// This title has no runtime-filled tail-call table, no spinning mix-worker rendezvous and no inline event reset. Its
// later Alchemy build has none of MUA1's other helpers, but keeps weak object handles: a 19-bit reference count whose
// release notifies the handle manager, a null-checked AddRef and a reference swap.
#define MUA_GUEST_HAS_INDIRECT_TAIL_CALLS 0
#define MUA_GUEST_HAS_AUDIO_BARRIER 0
#define MUA_GUEST_HAS_RESET_EVENT 0
#define MUA_GUEST_HAS_MEMMOVE 1
#define MUA_GUEST_HAS_D3D_STATE_FILTER 1
#define MUA_GUEST_HAS_ALCHEMY_CORE 1
#define MUA_GUEST_HAS_ALCHEMY_HELPERS 0
#define MUA_GUEST_HAS_OBJECT_HANDLES 1

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
#define MUA_GUEST_SET_VERTEX_DECLARATION sub_829C1DB8
#define MUA_GUEST_SET_VERTEX_SHADER sub_829C1BE8
#define MUA_GUEST_SET_PIXEL_SHADER sub_829C1A28
#define MUA_GUEST_SET_STREAM_SOURCE sub_829BE740
#define MUA_GUEST_SET_TEXTURE sub_829C6108
#define MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS sub_829C16E0
#define MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS sub_829C17B8
#define MUA_GUEST_UPLOAD_REGISTERS sub_829D8C18
#define MUA_GUEST_UPLOAD_ALU_CONSTANTS sub_829D8FB0
#define MUA_GUEST_UPLOAD_FETCH_CONSTANTS sub_829D8E58
#define MUA_GUEST_WRITE_PACKETS sub_829D85D0
#define MUA_GUEST_RESERVE_COMMANDS sub_829C3A20
#define MUA_GUEST_ADD_REF sub_8228F700
#define MUA_GUEST_RELEASE sub_8228F638
#define MUA_GUEST_ASSIGN_REF sub_822163E8
#define MUA_GUEST_DESTROY_OBJECT sub_8228F440
#define MUA_GUEST_MATRIX_NEARLY_EQUAL sub_82282660
#define MUA_GUEST_ADD_REF_IF_ANY sub_8228F728
#define MUA_GUEST_SWAP_REF sub_822911E0
#define MUA_GUEST_HANDLE_MANAGER sub_82261A00
#define MUA_GUEST_RELEASE_HANDLE sub_82286D78
#define MUA_GUEST_PROFILE_READ sub_82A4EF08
#define MUA_GUEST_PROFILE_WRITE sub_82A4EF70
#define MUA_GUEST_CONTENT_CREATE sub_829AC488
#define MUA_GUEST_CONTENT_CLOSE sub_829AC518
#define MUA_GUEST_CONTENT_ENUMERATE sub_829AC528
#define MUA_GUEST_DEVICE_SELECTOR sub_829ACA80
#define MUA_GUEST_TEXT_INFLATE sub_8239C3C8
#define MUA_GUEST_MENU_SIGNIN sub_828FCA38
#define MUA_GUEST_PLAYER_NUMBER_NAME sub_8260BA78

namespace mua::guest {

inline constexpr uint32_t kFencePollSite = 0x829C2CD8;
inline constexpr uint32_t kRingSpacePollSite = 0x829C2604;
inline constexpr uint32_t kRingProgressPollSite = 0x829C2560;
inline constexpr uint32_t kDeviceFenceOffset = 10896;
inline constexpr uint32_t kDeviceFlagsOffset = 10941;
inline constexpr uint32_t kRepeatPresentSite = 0x829D81E0;
inline constexpr uint32_t kWorldDeltaOffset = 1676;
inline constexpr uint32_t kRefCountMask = 0x7FFFF;
inline constexpr bool kAssignRefReturnsSlot = true;
inline constexpr uint32_t kMatrixEpsilon = 0x8213BE64;
inline constexpr uint32_t kObjectHandleFlag = 0x200000;
inline constexpr uint32_t kObjectHandleManager = 0x8336CBF4;
inline constexpr uint32_t kReleaseHandleManagerSite = 0x8228F6CC;
inline constexpr uint32_t kReleaseHandleSite = 0x8228F6DC;

// The callback's argument: swap interval in bits 8-11; zero releases the swap at once.
inline constexpr uint32_t ImmediatePresentArgument(uint32_t argument) {
  return argument & ~uint32_t{0xF00};
}

}  // namespace mua::guest
