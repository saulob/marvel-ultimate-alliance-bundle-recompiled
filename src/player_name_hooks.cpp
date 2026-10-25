#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_map.h"
#include "player_names.h"

namespace {

constexpr uint32_t kNameSize = rex::system::xam::kUserNameBufferSize;

struct GuestPlayerNames {
  uint32_t block = 0;
  bool custom[mua::kPlayerNames.size()] = {};
};

// The names are applied before the guest starts and never change while it runs, so they are copied into guest
// memory once.
const GuestPlayerNames& PlayerNames(uint8_t* base) {
  static const GuestPlayerNames names = [base] {
    GuestPlayerNames result;
    char bytes[mua::kPlayerNames.size()][kNameSize];
    bool any = false;
    for (size_t i = 0; i < mua::kPlayerNames.size(); ++i) {
      const mua::PlayerNameSetting& player = mua::kPlayerNames[i];
      result.custom[i] = mua::CustomPlayerNameBytes(player, rex::cvar::GetFlagByName(player.sdk_key), bytes[i]);
      any = any || result.custom[i];
    }
    if (any) {
      result.block = rex::system::kernel_state()->memory()->SystemHeapAlloc(sizeof(bytes));
    }
    if (result.block == 0) {
      return GuestPlayerNames{};
    }
    std::memcpy(base + result.block, bytes, sizeof(bytes));
    return result;
  }();
  return names;
}

uint32_t PlayerName(uint8_t* base, uint32_t user) {
  const GuestPlayerNames& names = PlayerNames(base);
  return user < mua::kPlayerNames.size() && names.custom[user] ? names.block + user * kNameSize : 0;
}

}  // namespace

// The game's name for a local user that is not signed in, "Player <user + 1>"; signed-in users get XAM's name.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_PLAYER_NUMBER_NAME));
REX_HOOK_RAW(MUA_GUEST_PLAYER_NUMBER_NAME) {
  if (const uint32_t name = PlayerName(base, ctx.r3.u32)) {
    ctx.r3.u64 = name;
    return;
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_PLAYER_NUMBER_NAME)(ctx, base);
}

#if !defined(MUA_RUNTIME_MUA2)

// Codegen midasm hook (overrides.toml) in the pause menu's Players list, after a row's text is formatted and before
// the list takes it: an active controller's row shows its user's name instead of "Player <port>".
void mua_player_list_name(PPCRegister& stack) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const char* port = reinterpret_cast<const char*>(base + stack.u32 + mua::kPlayerListPortTextOffset);
  const int user = mua::PlayerListRowUser(port);
  if (user < 0) {
    return;
  }
  if (const uint32_t name = PlayerName(base, uint32_t(user))) {
    static_assert(kNameSize <= mua::kPlayerListRowTextSize);
    std::memcpy(base + stack.u32 + mua::kPlayerListRowTextOffset, base + name, kNameSize);
  }
}

#endif
