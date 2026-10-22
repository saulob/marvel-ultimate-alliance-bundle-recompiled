// The C runtime's memcpy and memset. Found by compiled-body inspection (both have
// the standard PPC memcpy/memset calling convention: r3 destination, r4 source or fill byte, r5 size, with the
// prologue stashing the original r3 at r1-8 for the epilogue to reload as the return value) with no existing hook
// or patch covering either address. Interpreting them one PPC instruction at a time through the dispatcher is
// far slower than the host's own memcpy/memset for the large, frequent copies these run.
//
// A copy is safe to replace outright only where it stays inside guest memory the host already exposes as one flat
// pointer and never touches a device the runtime services through MMIO (GPU and XMA registers): a plain host
// memcpy/memset would bypass those devices' side effects, and a range split across the physical heap's host-page
// rounding (xmemory.h, PhysicalHostOffset) is not one contiguous host pointer to begin with.

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/system/mmio_handler.h>
#include <rex/system/xmemory.h>

#include "guest_map.h"

namespace {

// A forward byte-by-byte copy (what the guest's own routine does) matches memmove exactly when the destination
// does not start strictly inside the source span, i.e. dest <= source or the spans don't overlap at all.
bool CopyOrderIsForwardSafe(uint32_t dest, uint32_t source, uint32_t size) {
  return dest <= source || dest - source >= size;
}

// True once for every 64 KB-aligned block the range touches; the MMIO handler matches ranges by their top 16
// address bits (mmio_handler.cpp), so one lookup per block is enough regardless of a device's registered size.
bool RangeTouchesMmio(uint32_t address, uint32_t size) {
  rex::runtime::MMIOHandler* handler = rex::runtime::MMIOHandler::global_handler();
  if (!handler) {
    return true;  // Can't prove it's safe without a handler to ask.
  }
  const uint32_t first_block = address & 0xFFFF0000u;
  const uint32_t last_block = (address + size - 1) & 0xFFFF0000u;
  for (uint32_t block = first_block; block <= last_block; block += 0x10000u) {
    if (handler->LookupRange(block)) {
      return true;
    }
  }
  return false;
}

bool IsFlatHostRange(uint32_t address, uint32_t size) {
  if (size == 0) {
    return true;
  }
  const uint32_t last = address + size - 1;
  if (last < address) {
    return false;  // Wrapped past 0xFFFFFFFF.
  }
  return rex::memory::detail::PhysicalHostOffset(address) == rex::memory::detail::PhysicalHostOffset(last) &&
         !RangeTouchesMmio(address, size);
}

}  // namespace

// r3 destination, r4 source, r5 size; returns the destination, which the prologue also leaves at r1 - 8.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MEMCPY));
REX_HOOK_RAW(MUA_GUEST_MEMCPY) {
  const uint32_t dest = ctx.r3.u32;
  const uint32_t source = ctx.r4.u32;
  const uint32_t size = ctx.r5.u32;
  if (!CopyOrderIsForwardSafe(dest, source, size) || !IsFlatHostRange(dest, size) ||
      !IsFlatHostRange(source, size)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_MEMCPY)(ctx, base);
    return;
  }
  *rex::memory::GuestPtr<uint64_t*>(base, ctx.r1.u32 - 8) = std::byteswap(ctx.r3.u64);
  std::memmove(rex::memory::GuestPtr(base, dest), rex::memory::GuestPtr(base, source), size);
}

// r3 destination, r4 fill byte, r5 size; returns the destination.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MEMSET));
REX_HOOK_RAW(MUA_GUEST_MEMSET) {
  const uint32_t dest = ctx.r3.u32;
  const uint32_t size = ctx.r5.u32;
  if (!IsFlatHostRange(dest, size)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_MEMSET)(ctx, base);
    return;
  }
  std::memset(rex::memory::GuestPtr(base, dest), int(ctx.r4.u32 & 0xFF), size);
}
