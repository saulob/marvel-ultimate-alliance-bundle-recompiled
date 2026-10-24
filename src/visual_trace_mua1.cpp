// DEBUG_SAULO: MUA1 sources for the F9 transition recorder. Read-only: every hook runs the original unchanged.

#include <bit>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <rex/hook.h>
#include <rex/ppc.h>

#include "guest_map.h"
#include "visual_trace.h"

#if defined(MUA_RUNTIME_MUA1)

namespace {

constexpr uint32_t kMenuManagerPointer = 0x82B42AE0;
constexpr uint32_t kUserManager = 0x8294FF90;
constexpr uint32_t kInputManager = 0x82BAB140;
constexpr uint32_t kProfileManager = 0x8287EAC8;
constexpr uint32_t kPadManager = 0x829D243C;

constexpr uint32_t kCurrentMenuOffset = 20376;
constexpr uint32_t kMenuNameOffset = 12;
constexpr uint32_t kTransitionOffset = 21972;
constexpr uint32_t kPendingNameOffset = 21976;
constexpr uint32_t kQueuedNameOffset = 2285;
constexpr uint32_t kActivePadOffset = 2216;
constexpr uint32_t kMovieFlagsOffset = 24580;
constexpr uint32_t kMovieNameOffset = 24412;
constexpr uint32_t kUserSlotOffset = 60;
constexpr uint32_t kInputUserSlotOffset = 1488;
constexpr uint32_t kPadActiveOffset = 52;
constexpr uint32_t kProfileRecordSize = 136;
constexpr uint32_t kProfileNameOffset = 72;

const uint8_t* g_base = nullptr;

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

uint16_t Load16(const uint8_t* base, uint32_t address) {
  uint16_t value = 0;
  std::memcpy(&value, base + address, sizeof(value));
  return std::byteswap(value);
}

bool Plausible(uint32_t address) { return address >= 0x10000 && address < 0xC0000000; }

std::string Cstr(const uint8_t* base, uint32_t address, uint32_t max_length) {
  std::string out;
  if (!Plausible(address)) {
    return out;
  }
  for (uint32_t i = 0; i < max_length; ++i) {
    const char c = static_cast<char>(base[address + i]);
    if (c == 0) {
      break;
    }
    out += (c >= 32 && c < 127) ? c : '?';
  }
  return out;
}

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));

std::string Format(const char* format, ...) {
  char buffer[320];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return buffer;
}

std::string MenuState(const uint8_t* base) {
  const uint32_t manager = Load32(base, kMenuManagerPointer);
  if (!Plausible(manager)) {
    return "ui=none";
  }
  const uint32_t current = Load32(base, manager + kCurrentMenuOffset);
  const uint32_t vtable = Plausible(current) ? Load32(base, current) : 0;
  return Format("current=%08X vtable=%08X name=%s transition=%d pending=%s queued=%s", current, vtable,
                Cstr(base, current + kMenuNameOffset, 24).c_str(), int32_t(Load32(base, manager + kTransitionOffset)),
                Cstr(base, manager + kPendingNameOffset, 24).c_str(),
                Cstr(base, manager + kQueuedNameOffset, 24).c_str());
}

std::string MovieState(const uint8_t* base) {
  const uint32_t manager = Load32(base, kMenuManagerPointer);
  if (!Plausible(manager)) {
    return "ui=none";
  }
  return Format("playing=%d name=%s", (base[manager + kMovieFlagsOffset] >> 7) & 1,
                Cstr(base, manager + kMovieNameOffset, 12).c_str());
}

std::string ProfileState(const uint8_t* base) {
  const uint32_t manager = Load32(base, kMenuManagerPointer);
  const uint32_t active_pad = Plausible(manager) ? Load16(base, manager + kActivePadOffset) : 0;
  return Format("g_pad=%04X pad_mgr=%d user=%d input_user=%d signin=%d/%d/%d/%d name0=%s", active_pad,
                int32_t(Load32(base, kPadManager + kPadActiveOffset)),
                int32_t(Load32(base, kUserManager + kUserSlotOffset)),
                int32_t(Load32(base, kInputManager + kInputUserSlotOffset)),
                int32_t(Load32(base, kProfileManager + 8)),
                int32_t(Load32(base, kProfileManager + kProfileRecordSize + 8)),
                int32_t(Load32(base, kProfileManager + 2 * kProfileRecordSize + 8)),
                int32_t(Load32(base, kProfileManager + 3 * kProfileRecordSize + 8)),
                Cstr(base, kProfileManager + kProfileNameOffset, 16).c_str());
}

std::string CurrentState() {
  if (g_base == nullptr) {
    return "not_started";
  }
  return "menu{" + MenuState(g_base) + "} movie{" + MovieState(g_base) + "} profile{" + ProfileState(g_base) + "}";
}

void Sample(const uint8_t* base) {
  static std::string menu, movie, profile;
  const auto note = [](std::string& last, std::string now, const char* kind) {
    if (now != last) {
      last = std::move(now);
      mua::visual_trace::Event(kind, last);
    }
  };
  note(menu, MenuState(base), "menu_state");
  note(movie, MovieState(base), "movie_state");
  note(profile, ProfileState(base), "profile_state");
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_UI_UPDATE));
REX_HOOK_RAW(MUA_GUEST_UI_UPDATE) {
  if (g_base == nullptr) {
    g_base = base;
    mua::visual_trace::SetStateProvider(CurrentState);
  }
  Sample(base);
  MUA_GUEST_ORIGINAL(MUA_GUEST_UI_UPDATE)(ctx, base);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MAIN_MENU_EXIT));
REX_HOOK_RAW(MUA_GUEST_MAIN_MENU_EXIT) {
  mua::visual_trace::Event("mainmenuexit_enter");
  MUA_GUEST_ORIGINAL(MUA_GUEST_MAIN_MENU_EXIT)(ctx, base);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_RESET_GAME));
REX_HOOK_RAW(MUA_GUEST_RESET_GAME) {
  mua::visual_trace::Event("resetgame");
  MUA_GUEST_ORIGINAL(MUA_GUEST_RESET_GAME)(ctx, base);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_CONSOLE_QUEUE));
REX_HOOK_RAW(MUA_GUEST_CONSOLE_QUEUE) {
  const std::string command = Cstr(base, ctx.r4.u32, 40);
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONSOLE_QUEUE)(ctx, base);
  mua::visual_trace::Event("console_queue", Format("cmd=\"%s\" accepted=%d", command.c_str(), ctx.r3.u8 != 0));
}

#endif

#if defined(MUA_RUNTIME_MUA1_GOLD)

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_CONSOLE_QUEUE));
REX_HOOK_RAW(MUA_GUEST_CONSOLE_QUEUE) {
  const char* command = reinterpret_cast<const char*>(base + ctx.r4.u32);
  const std::string text(command, strnlen(command, 40));
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONSOLE_QUEUE)(ctx, base);
  mua::visual_trace::Event("console_queue", text + (ctx.r3.u8 != 0 ? " accepted=1" : " accepted=0"));
}

#endif
