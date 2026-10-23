// DEBUG_SAULO: checks that every guest function the hooks replace is the one the module's function table runs.

#include <cstdint>
#include <cstdlib>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "guest_functions.h"
#include "guest_map.h"

#define MUA_STRINGIZE_(x) #x
#define MUA_STRINGIZE(x) MUA_STRINGIZE_(x)
#define MUA_HOOK_ENTRY(hook) {#hook, MUA_STRINGIZE(hook), &hook},

REX_EXTERN(MUA_GUEST_PRESENT_CALLBACK);
REX_EXTERN(MUA_GUEST_SWAP);
REX_EXTERN(MUA_GUEST_WORLD_UPDATE);
REX_EXTERN(MUA_GUEST_OPTIONS_INIT);
REX_EXTERN(MUA_GUEST_FENCE_BEGIN);
REX_EXTERN(MUA_GUEST_FENCE_POLL);
REX_EXTERN(MUA_GUEST_AUDIO_RENDER);
REX_EXTERN(MUA_GUEST_MEMCPY);
REX_EXTERN(MUA_GUEST_MEMSET);
REX_EXTERN(MUA_GUEST_PROFILE_READ);
REX_EXTERN(MUA_GUEST_PROFILE_WRITE);
REX_EXTERN(MUA_GUEST_CONTENT_CREATE);
REX_EXTERN(MUA_GUEST_CONTENT_CLOSE);
REX_EXTERN(MUA_GUEST_CONTENT_ENUMERATE);
REX_EXTERN(MUA_GUEST_DEVICE_SELECTOR);
#if MUA_GUEST_HAS_AUDIO_BARRIER
REX_EXTERN(MUA_GUEST_AUDIO_BARRIER);
#endif
#if MUA_GUEST_HAS_RESET_EVENT
REX_EXTERN(MUA_GUEST_RESET_EVENT);
#endif
#if MUA_GUEST_HAS_INDIRECT_TAIL_CALLS
REX_EXTERN(MUA_GUEST_INDIRECT_TAIL_CALL);
#endif
#if MUA_GUEST_HAS_MEMMOVE
REX_EXTERN(MUA_GUEST_MEMMOVE);
#endif

namespace mua {
namespace {

struct HookEntry {
  const char* role;
  const char* symbol;
  PPCFunc* function;
};

const HookEntry kHooks[] = {
    MUA_HOOK_ENTRY(MUA_GUEST_PRESENT_CALLBACK)
    MUA_HOOK_ENTRY(MUA_GUEST_SWAP)
    MUA_HOOK_ENTRY(MUA_GUEST_WORLD_UPDATE)
    MUA_HOOK_ENTRY(MUA_GUEST_OPTIONS_INIT)
    MUA_HOOK_ENTRY(MUA_GUEST_FENCE_BEGIN)
    MUA_HOOK_ENTRY(MUA_GUEST_FENCE_POLL)
    MUA_HOOK_ENTRY(MUA_GUEST_AUDIO_RENDER)
    MUA_HOOK_ENTRY(MUA_GUEST_MEMCPY)
    MUA_HOOK_ENTRY(MUA_GUEST_MEMSET)
    MUA_HOOK_ENTRY(MUA_GUEST_PROFILE_READ)
    MUA_HOOK_ENTRY(MUA_GUEST_PROFILE_WRITE)
    MUA_HOOK_ENTRY(MUA_GUEST_CONTENT_CREATE)
    MUA_HOOK_ENTRY(MUA_GUEST_CONTENT_CLOSE)
    MUA_HOOK_ENTRY(MUA_GUEST_CONTENT_ENUMERATE)
    MUA_HOOK_ENTRY(MUA_GUEST_DEVICE_SELECTOR)
#if MUA_GUEST_HAS_AUDIO_BARRIER
    MUA_HOOK_ENTRY(MUA_GUEST_AUDIO_BARRIER)
#endif
#if MUA_GUEST_HAS_RESET_EVENT
    MUA_HOOK_ENTRY(MUA_GUEST_RESET_EVENT)
#endif
#if MUA_GUEST_HAS_INDIRECT_TAIL_CALLS
    MUA_HOOK_ENTRY(MUA_GUEST_INDIRECT_TAIL_CALL)
#endif
#if MUA_GUEST_HAS_MEMMOVE
    MUA_HOOK_ENTRY(MUA_GUEST_MEMMOVE)
#endif
};

bool IsRegistered(const PPCFuncMapping* mappings, const HookEntry& hook) {
  const size_t address = std::strtoul(hook.symbol + 4, nullptr, 16);
  for (const PPCFuncMapping* mapping = mappings; mapping->guest != 0 || mapping->host != nullptr; ++mapping) {
    if (mapping->guest == address) {
      return mapping->host == hook.function;
    }
  }
  return false;
}

}  // namespace

void LogGuestHooks(const PPCFuncMapping* mappings) {
  std::string registered;
  std::string missing;
  for (const HookEntry& hook : kHooks) {
    std::string& list = IsRegistered(mappings, hook) ? registered : missing;
    list += std::string(list.empty() ? "" : " ") + (hook.role + sizeof("MUA_GUEST_") - 1) + "=" + (hook.symbol + 4);
  }
  REXLOG_INFO("DEBUG_SAULO guest hooks registered: {}", registered);
  if (!missing.empty()) {
    REXLOG_WARN("DEBUG_SAULO guest hooks NOT registered: {}", missing);
  }
}

}  // namespace mua
