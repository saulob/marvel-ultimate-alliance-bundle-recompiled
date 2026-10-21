// The ResetEvent hook is adapted from Unowa001/crackdown-recomp (BSD 3-Clause, see packaging/licenses/THIRD-PARTY-LICENSES.txt).

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/ppc.h>

REX_EXTERN(__imp__KeResetEvent);

namespace {

constexpr uint32_t kShadowedObjectSignature = 0x52455800;
constexpr uint8_t kSynchronizationEventType = 1;

uint32_t LoadGuest32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return __builtin_bswap32(value);
}

}  // namespace

// sub_8282DE78 is the sound streamer's inline ResetEvent (`stw 0,4(r3)`), which never reaches the kernel, so the host
// event the consumer waits on stays signaled and the wait returns with an empty ring. Only events the runtime already
// shadows are reset on the host too.
REX_HOOK_RAW(sub_8282DE78) {
  const uint32_t object = ctx.r3.u32;
  ctx.r11.s64 = 0;
  std::memset(rex::memory::GuestPtr<uint8_t*>(base, object + 4), 0, sizeof(uint32_t));
  if (*rex::memory::GuestPtr<const uint8_t*>(base, object) > kSynchronizationEventType ||
      LoadGuest32(base, object + 8) != kShadowedObjectSignature) {
    return;
  }
  __imp__KeResetEvent(ctx, base);
  ctx.r3.u64 = object;
  ctx.r11.s64 = 0;
}
