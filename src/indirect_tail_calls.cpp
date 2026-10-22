// Functions whose bctr is a tail call through a table the title fills at run time. Codegen reads the table from the
// image, where it is still zero, and emits a switch whose every case traps.

#include <bit>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/ppc.h>

namespace {

uint32_t LoadGuest32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return std::byteswap(value);
}

}  // namespace

// sub_82839360(_, index): lwz r11, 0x820C8878 + index * 12; mtctr r11; bctr.
REX_HOOK_RAW(sub_82839360) {
  constexpr uint32_t kTable = 0x820C8878;
  constexpr uint32_t kEntryBytes = 12;
  ctx.r10.u64 = uint64_t(ctx.r4.u32) * kEntryBytes;
  ctx.r11.u64 = LoadGuest32(base, kTable + ctx.r10.u32);
  ctx.ctr.u64 = ctx.r11.u64;
  rex::runtime::ResolveIndirectFunction(ctx.r11.u32)(ctx, base);
}
