#include "main_menu_items.h"

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "guest_map.h"

#if !defined(MUA_RUNTIME_MUA2)

REX_EXTERN(MUA_GUEST_STRING_DATA);
REX_EXTERN(__imp__XamLoaderTerminateTitle);

namespace {

uint32_t ExitStrings(uint8_t* base) {
  static const uint32_t strings = [base] {
    const uint32_t block = rex::system::kernel_state()->memory()->SystemHeapAlloc(mua::main_menu::kExitStringsSize);
    if (block != 0) {
      mua::main_menu::WriteExitStrings(base, block);
    }
    return block;
  }();
  return strings;
}

mua::main_menu::GuestAddresses Guest(uint8_t* base) {
  return {
      mua::guest::kMainMenuVtable,
      mua::guest::kMainMenuLiveNameEntry,
      mua::guest::kEmptyString,
      ExitStrings(base),
  };
}

bool IsExitItem(PPCContext& ctx, uint8_t* base, uint32_t item) {
  ctx.r3.u64 = item + mua::main_menu::kItemCommandOffset;
  MUA_GUEST_STRING_DATA(ctx, base);
  const bool exit = mua::main_menu::IsExitCommand(base, ctx.r3.u32);
  ctx.r3.u64 = item;
  return exit;
}

}  // namespace

// Runs once per item node while the main menu parses its XML; the Xbox Live item is replaced before the items that
// follow it are created.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_ITEM_CREATE));
REX_HOOK_RAW(MUA_GUEST_MENU_ITEM_CREATE) {
  const uint32_t menu = ctx.r3.u32;
  const uint32_t node = ctx.r4.u32;
  if (mua::main_menu::IsMenu(base, menu, mua::guest::kMainMenuVtable)) {
    mua::main_menu::ReplaceLiveItem(base, node, Guest(base));
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_ITEM_CREATE)(ctx, base);
}

// Runs the item's console command; the Exit Game item ends the title the way the game's own entry point does when
// its main function returns.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_ITEM_USE));
REX_HOOK_RAW(MUA_GUEST_ITEM_USE) {
  if (IsExitItem(ctx, base, ctx.r3.u32)) {
    __imp__XamLoaderTerminateTitle(ctx, base);
    return;
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_ITEM_USE)(ctx, base);
}

#endif
