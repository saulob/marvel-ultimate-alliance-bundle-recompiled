#include "main_menu_items.h"

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_map.h"

#if !defined(MUA_RUNTIME_MUA2)

namespace {

constexpr mua::main_menu::GuestAddresses kGuest = {
    mua::guest::kMainMenuVtable,
    mua::guest::kMainMenuLiveNameEntry,
    mua::guest::kEmptyString,
};

}  // namespace

// Runs once per item node while the main menu parses its XML; the Xbox Live item is dropped before the items that
// follow it are created.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_ITEM_CREATE));
REX_HOOK_RAW(MUA_GUEST_MENU_ITEM_CREATE) {
  const uint32_t menu = ctx.r3.u32;
  const uint32_t node = ctx.r4.u32;
  if (mua::main_menu::IsMenu(base, menu, kGuest.menu_vtable)) {
    mua::main_menu::RemoveLiveItem(base, node, kGuest);
    if (mua::main_menu::ShouldSkipItem(base, node, kGuest)) {
      ctx.r3.u64 = 0;
      return;
    }
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_ITEM_CREATE)(ctx, base);
}

#endif
