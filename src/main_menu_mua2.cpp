#include "main_menu_mua2.h"

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_map.h"

#if defined(MUA_RUNTIME_MUA2)

namespace menu = mua::main_menu_mua2;

REX_EXTERN(__imp__XamLoaderTerminateTitle);

// The pak reader inflates the menu's Lua and scene data through this zlib call.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_TEXT_INFLATE));
REX_HOOK_RAW(MUA_GUEST_TEXT_INFLATE) {
  const uint32_t stream = ctx.r3.u32;
  const uint32_t next_out = stream + menu::kStreamNextOutOffset;
  const uint32_t begin = stream != 0 ? menu::LoadU32(base, next_out) : 0;
  MUA_GUEST_ORIGINAL(MUA_GUEST_TEXT_INFLATE)(ctx, base);
  if (begin != 0) {
    menu::PatchMenuText(base, begin, menu::LoadU32(base, next_out));
  }
}

// The Lua global the Quit Game tab calls when it is activated; it ends the title and returns no value, so the menu
// never moves on to the online screens.
REX_HOOK_RAW(MUA_GUEST_MENU_SIGNIN) {
  __imp__XamLoaderTerminateTitle(ctx, base);
  ctx.r3.u64 = 0;
}

#endif
