// Hooks on Marvel Ultimate Alliance 2's Alchemy semaphore, which the game's Havok memory allocator takes as a lock
// around every allocation and free.

#include "guest_semaphore.h"

#if defined(MUA_RUNTIME_MUA2)

#include <bit>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

#include "guest_map.h"

REXCVAR_DEFINE_INT32(mua_semaphore_spin_us, 50, "Marvel Ultimate Alliance",
                     "Microseconds a blocking wait on an Alchemy semaphore tries it without a timeout before the kernel "
                     "wait; 0 waits at once")
    .range(0, 200)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

using namespace mua::semaphore;

uint32_t Load32(uint8_t* base, uint32_t address) {
  return std::byteswap(
      std::atomic_ref<uint32_t>(*rex::memory::GuestPtr<uint32_t*>(base, address)).load(std::memory_order_acquire));
}

SpinScores g_scores;

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT));
// r3 result, r4 semaphore, r5 low byte: block (else a wait with no timeout). The original stores the failure result
// first and the success result once the kernel wait took the semaphore.
REX_HOOK_RAW(MUA_GUEST_SEMAPHORE_WAIT) {
  const uint32_t result = ctx.r3.u32;
  const uint32_t semaphore = ctx.r4.u32;
  const uint64_t blocking = ctx.r5.u64;
  const uint32_t success = Load32(base, mua::guest::kAlchemySuccess);
  if (!(blocking & 0xFF) || success == Load32(base, mua::guest::kAlchemyFailure)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT)(ctx, base);
    return;
  }
  const auto wait = [&](uint64_t flag) {
    ctx.r3.u64 = result;
    ctx.r4.u64 = semaphore;
    ctx.r5.u64 = flag;
    MUA_GUEST_ORIGINAL(MUA_GUEST_SEMAPHORE_WAIT)(ctx, base);
  };
  const int spin_us = REXCVAR_GET(mua_semaphore_spin_us);
  const WaitOutcome outcome = WaitSpinningFirst(
      [&] { return int32_t(Load32(base, semaphore + mua::guest::kSemaphoreCountOffset)); },
      [&] {
        wait(blocking & ~uint64_t{0xFF});
        return Load32(base, result) == success;
      },
      [&] { wait(blocking); }, spin_us > 0 && g_scores.ShouldSpin(semaphore), std::chrono::microseconds(spin_us));
  g_scores.Record(semaphore, outcome.path);
}

#endif
