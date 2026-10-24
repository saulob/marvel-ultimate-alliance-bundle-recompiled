#include "skip_intro.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_map.h"
#include "settings.h"
#include "visual_trace.h"

#if defined(MUA_RUNTIME_MUA2)

// Codegen midasm hook (overrides.toml), inside sub_8252A550: at this point the engine has just looked up whether the
// publisher/startup presentation script already ran (0 = not yet) and is about to decide whether to
// RunScript("menus/intro_normal"). Forcing the already-shown result here reproduces exactly the branch the game
// itself takes on a later call, once that script really has run; everything else in the function (the one-shot setup
// at self+46, the legal and ESRB checks) still runs unchanged.
void mua_skip_intro_mua2(PPCRegister& already_shown) {
  if (mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro))) {
    already_shown.u32 = 0;
  }
}

#else

REX_EXTERN(MUA_GUEST_MENU_MANAGER);
REX_EXTERN(MUA_GUEST_MOVIE_PLAYING);
REX_EXTERN(MUA_GUEST_STOP_MOVIE);
REX_EXTERN(MUA_GUEST_SET_ACTIVE_PAD);
REX_EXTERN(MUA_GUEST_USER_MANAGER);
REX_EXTERN(MUA_GUEST_USER_SET_CONTROLLER);
REX_EXTERN(MUA_GUEST_USER_REFRESH);
REX_EXTERN(MUA_GUEST_USER_GET_SLOT);
REX_EXTERN(MUA_GUEST_USER_SET_SLOT);
REX_EXTERN(MUA_GUEST_INPUT_MANAGER);
REX_EXTERN(MUA_GUEST_INPUT_SET_NAME);
REX_EXTERN(MUA_GUEST_INPUT_SET_FLAGS);
REX_EXTERN(MUA_GUEST_INPUT_CLEAR_FLAGS);
REX_EXTERN(MUA_GUEST_PROFILE_MANAGER);
REX_EXTERN(MUA_GUEST_PROFILE_REFRESH_PAD);
REX_EXTERN(MUA_GUEST_PROFILE_RESET_ALL);
REX_EXTERN(MUA_GUEST_PROFILE_QUERY_ALL);
REX_EXTERN(MUA_GUEST_PROFILE_SIGNED_IN);
REX_EXTERN(MUA_GUEST_PROFILE_ONLINE);
REX_EXTERN(MUA_GUEST_PROFILE_NAME);
REX_EXTERN(MUA_GUEST_SIGNIN_PUMP);
REX_EXTERN(MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A);
REX_EXTERN(MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B);
REX_EXTERN(MUA_GUEST_CONSOLE);
REX_EXTERN(MUA_GUEST_CONSOLE_QUEUE);
REX_EXTERN(MUA_GUEST_PAD_MANAGER);
REX_EXTERN(MUA_GUEST_PAD_CONNECTED);

namespace {

mua::BootIntroSkip g_boot_intro;

std::string_view GuestString(const uint8_t* base, uint32_t address) {
  constexpr size_t kMaxLen = 64;
  if (address == 0) {
    return {};
  }
  const char* text = reinterpret_cast<const char*>(base + address);
  return std::string_view(text, strnlen(text, kMaxLen));
}

float LoadGuestFloat(const uint8_t* base, uint32_t address) {
  uint32_t bits = 0;
  std::memcpy(&bits, base + address, sizeof(bits));
  return std::bit_cast<float>(std::byteswap(bits));
}

void StoreGuestFloat(uint8_t* base, uint32_t address, float value) {
  const uint32_t bits = std::byteswap(std::bit_cast<uint32_t>(value));
  std::memcpy(base + address, &bits, sizeof(bits));
}

// The gate's wait is the legal screen's display time, measured from a stamp taken right after that screen opens.
void ElapseLegalWait(uint8_t* base, uint32_t gate) {
  static uint32_t elapsed_bits = 0;
  const float stamp = LoadGuestFloat(base, gate + mua::kBootGateStampOffset);
  if (std::bit_cast<uint32_t>(stamp) == elapsed_bits) {
    return;
  }
  const float elapsed = mua::ElapsedBootStamp(stamp, LoadGuestFloat(base, mua::kBootGateSecondsAddress));
  StoreGuestFloat(base, gate + mua::kBootGateStampOffset, elapsed);
  elapsed_bits = std::bit_cast<uint32_t>(elapsed);
  mua::visual_trace::Event("legal_wait_elapsed");
}

bool MoviePlaying(PPCContext& ctx, uint8_t* base, uint32_t manager) {
  ctx.r3.u64 = manager;
  MUA_GUEST_MOVIE_PLAYING(ctx, base);
  return ctx.r3.u8 != 0;
}

uint32_t CallGetter(PPCFunc* getter, PPCContext& ctx, uint8_t* base) {
  getter(ctx, base);
  return ctx.r3.u32;
}

uint32_t CallMethod(PPCFunc* method, PPCContext& ctx, uint8_t* base, uint32_t self, uint32_t argument = 0) {
  ctx.r3.u64 = self;
  ctx.r4.u64 = argument;
  method(ctx, base);
  return ctx.r3.u32;
}

// The pad the start menu's own input query scan would report first.
int32_t FirstConnectedPad(PPCContext& ctx, uint8_t* base) {
  const uint32_t pads = CallGetter(MUA_GUEST_PAD_MANAGER, ctx, base);
  for (int32_t pad = 0; pad < mua::kMaxPads; ++pad) {
    if ((CallMethod(MUA_GUEST_PAD_CONNECTED, ctx, base, pads, uint32_t(pad)) & 0xFF) != 0) {
      return pad;
    }
  }
  return mua::kNoPad;
}

// Mirrors the start menu's activation (profile and sign-in bootstrap) and its confirm branch, minus the menu
// itself, the confirm sound and the pad-press query, then queues the same "mainmenuexit 1" it queues.
bool ConfirmStartScreen(PPCContext& ctx, uint8_t* base, int32_t pad) {
  const uint32_t users = CallGetter(MUA_GUEST_USER_MANAGER, ctx, base);
  const uint32_t pump = CallGetter(MUA_GUEST_SIGNIN_PUMP, ctx, base);
  const uint32_t profiles = CallGetter(MUA_GUEST_PROFILE_MANAGER, ctx, base);
  const uint32_t input = CallGetter(MUA_GUEST_INPUT_MANAGER, ctx, base);

  CallMethod(MUA_GUEST_USER_REFRESH, ctx, base, users, 1);
  CallMethod(MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B, ctx, base, pump, 0);
  CallMethod(MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A, ctx, base, pump, 1);
  CallMethod(MUA_GUEST_PROFILE_RESET_ALL, ctx, base, profiles);
  CallMethod(MUA_GUEST_PROFILE_QUERY_ALL, ctx, base, profiles);

  CallMethod(MUA_GUEST_SET_ACTIVE_PAD, ctx, base, CallGetter(MUA_GUEST_MENU_MANAGER, ctx, base), uint32_t(pad));
  CallMethod(MUA_GUEST_USER_SET_CONTROLLER, ctx, base, users, uint32_t(pad));
  CallMethod(MUA_GUEST_USER_SET_SLOT, ctx, base, users, uint32_t(pad));
  const uint32_t user_slot = CallMethod(MUA_GUEST_USER_GET_SLOT, ctx, base, users);
  const uint32_t swapped_slot = std::byteswap(user_slot);
  std::memcpy(base + input + mua::guest::kInputUserSlotOffset, &swapped_slot, sizeof(swapped_slot));

  CallMethod(MUA_GUEST_PROFILE_REFRESH_PAD, ctx, base, profiles, uint32_t(pad));
  if ((CallMethod(MUA_GUEST_PROFILE_SIGNED_IN, ctx, base, profiles, uint32_t(pad)) & 0xFF) != 0) {
    const uint32_t name = CallMethod(MUA_GUEST_PROFILE_NAME, ctx, base, profiles, uint32_t(pad));
    CallMethod(MUA_GUEST_INPUT_SET_NAME, ctx, base, input, name);
    const bool online = (CallMethod(MUA_GUEST_PROFILE_ONLINE, ctx, base, profiles, uint32_t(pad)) & 0xFF) != 0;
    CallMethod(online ? MUA_GUEST_INPUT_SET_FLAGS : MUA_GUEST_INPUT_CLEAR_FLAGS, ctx, base, input,
               mua::kOnlineProfileFlag);
  }

  const uint32_t console = CallGetter(MUA_GUEST_CONSOLE, ctx, base);
  return (CallMethod(MUA_GUEST_CONSOLE_QUEUE, ctx, base, console, mua::guest::kMainMenuExitCommand) & 0xFF) != 0;
}

// Skips the start menu when a controller can confirm it; otherwise the menu opens normally. The caller's argument
// registers are restored for the original OpenMenu in that case.
bool SkipStartMenu(PPCContext& ctx, uint8_t* base) {
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
  const auto restore = [&] {
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    ctx.r5.u64 = r5;
    ctx.r6.u64 = r6;
  };
  const int32_t pad = FirstConnectedPad(ctx, base);
  const bool skipped = pad != mua::kNoPad && ConfirmStartScreen(ctx, base, pad);
  restore();
  return skipped;
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_INTRO_GATE));
REX_HOOK_RAW(MUA_GUEST_INTRO_GATE) {
  const uint32_t self = ctx.r3.u32;
  const bool skip = mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro));
  const uint8_t script_flags_before = base[self + mua::kIntroScriptFlagOffset];
  if (skip) {
    base[self + mua::kLegalShownFlagOffset] |= mua::kLegalShownFlagBit;
    base[self + mua::kEsrbShownFlagOffset] |= mua::kEsrbShownFlagBit;
    if (g_boot_intro.before_intro()) {
      ElapseLegalWait(base, self);
    }
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_INTRO_GATE)(ctx, base);
  const bool was_skipping = g_boot_intro.skipping_movies();
  g_boot_intro.OnIntroGate(skip, script_flags_before, base[self + mua::kIntroScriptFlagOffset]);
  if (!was_skipping && g_boot_intro.skipping_movies()) {
    mua::visual_trace::Event("skip_intro", "begin");
  }
}

// The movie menu's update completes the movie through its own vt+228 on the frame playback is no longer running,
// which is how both the skip button and a movie's natural end reach it: they only stop playback first.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MOVIE_MENU_UPDATE));
REX_HOOK_RAW(MUA_GUEST_MOVIE_MENU_UPDATE) {
  if (g_boot_intro.skipping_movies()) {
    const uint32_t menu = ctx.r3.u32;
    MUA_GUEST_MENU_MANAGER(ctx, base);
    const uint32_t manager = ctx.r3.u32;
    if (MoviePlaying(ctx, base, manager)) {
      ctx.r3.u64 = manager;
      MUA_GUEST_STOP_MOVIE(ctx, base);
      mua::visual_trace::Event("movie_stop");
    }
    ctx.r3.u64 = menu;
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_MOVIE_MENU_UPDATE)(ctx, base);
}

// The boot gate runs the startup intro as one console command; with Skip intro on the command becomes the
// script's own final step, so no movie or blank menu is constructed.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SCRIPT_DISPATCH));
REX_HOOK_RAW(MUA_GUEST_SCRIPT_DISPATCH) {
  rex::ppc::stack_guard guard(ctx);
  if (g_boot_intro.before_intro() && mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro)) &&
      mua::IsIntroScriptCommand(GuestString(base, ctx.r4.u32))) {
    g_boot_intro.OnIntroScriptReplaced(true);
    ctx.r4.u32 = rex::ppc::stack_push_string(ctx, base, std::string(mua::kStartMenuCommand).c_str());
    mua::visual_trace::Event("intro_fast_forward");
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_SCRIPT_DISPATCH)(ctx, base);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_OPEN_MENU));
REX_HOOK_RAW(MUA_GUEST_OPEN_MENU) {
  const std::string_view requested = GuestString(base, ctx.r4.u32);
  mua::visual_trace::Event("open_menu", std::string(requested));
  if (g_boot_intro.before_intro() && mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro)) &&
      mua::IsLegalMenu(requested)) {
    mua::visual_trace::Event("legal_menu_skipped");
    return;
  }
  if (g_boot_intro.skipping_movies()) {
    const std::string_view menu = GuestString(base, ctx.r4.u32);
    g_boot_intro.OnOpenMenu(menu);
    if (menu == mua::kStartMenuName) {
      const bool skipped = SkipStartMenu(ctx, base);
      mua::visual_trace::Event("start_bypass", skipped ? "done" : "fallback");
      if (skipped) {
        return;
      }
    }
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_OPEN_MENU)(ctx, base);
}

#endif
