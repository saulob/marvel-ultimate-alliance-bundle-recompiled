// Native copies of small, hot Alchemy helpers. The reference count helpers do the same compare-and-swap the guest's
// lwarx/stwcx. loop does, without the process-wide lock the recompiler puts around every mfmsr/mtmsrd pair: every
// section the game runs under that lock is a single reservation pair (guest_map_tests checks), so the lock adds no
// exclusion a lone compare-and-swap lacks.

#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/func.h>
#include <rex/system/xmemory.h>

#include "guest_map.h"

#if MUA_GUEST_HAS_ALCHEMY_HELPERS

REXCVAR_DEFINE_BOOL(mua_lockfree_refcount, true, "Marvel Ultimate Alliance",
                    "Update Alchemy reference counts without the recompiler's global interrupt lock")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_native_alchemy_helpers, true, "Marvel Ultimate Alliance",
                    "Run small Alchemy matrix compare, mask and index helpers natively")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_native_alchemy_matrix_and_type, false, "Marvel Ultimate Alliance",
                    "Run the Alchemy matrix multiply, matrix copy and type check natively")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_lockfree_string_and_resource_refs, false, "Marvel Ultimate Alliance",
                    "Update Alchemy string, D3D resource and object list reference counts without the global lock")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(MUA_GUEST_DESTROY_OBJECT);

namespace {

constexpr uint32_t kRefCountOffset = 4;
constexpr uint32_t kRefCountMask = 0x3FFFFF;

uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return std::byteswap(value);
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(rex::memory::GuestPtr(base, address), &value, sizeof(value));
}

float LoadFloat(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(Load32(base, address));
}

// Returns the new count, as the guest loop leaves it in r10.
uint32_t AddToRefCount(uint8_t* base, uint32_t object, int32_t delta) {
  std::atomic_ref<uint32_t> word(*rex::memory::GuestPtr<uint32_t*>(base, object + kRefCountOffset));
  uint32_t observed = word.load(std::memory_order_relaxed);
  uint32_t updated;
  do {
    updated = std::byteswap(std::byteswap(observed) + uint32_t(delta));
  } while (!word.compare_exchange_weak(observed, updated, std::memory_order_acq_rel, std::memory_order_relaxed));
  return std::byteswap(updated);
}

// Drops one reference and destroys the object when none is left; false when the object was destroyed (the guest
// call then owns r3).
bool Release(PPCContext& ctx, uint8_t* base, uint32_t object) {
  if (AddToRefCount(base, object, -1) & kRefCountMask) {
    return true;
  }
  ctx.r3.u64 = object;
  MUA_GUEST_DESTROY_OBJECT(ctx, base);
  return false;
}

// r3 slot, r4 new object (null allowed). The slot is written after the old object is released, as in the guest.
void AssignReference(PPCContext& ctx, uint8_t* base, bool add_reference) {
  const uint32_t slot = ctx.r3.u32;
  const uint32_t object = ctx.r4.u32;
  if (add_reference && object) {
    AddToRefCount(base, object, 1);
  }
  const uint32_t old = Load32(base, slot);
  ctx.r3.u64 = old;
  if (old) {
    Release(ctx, base, old);
  }
  Store32(base, slot, object);
}

}  // namespace

// r3 object.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_ADD_REF));
REX_HOOK_RAW(MUA_GUEST_ADD_REF) {
  if (!REXCVAR_GET(mua_lockfree_refcount)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_ADD_REF)(ctx, base);
    return;
  }
  AddToRefCount(base, ctx.r3.u32, 1);
}

// r3 object.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE));
REX_HOOK_RAW(MUA_GUEST_RELEASE) {
  if (!REXCVAR_GET(mua_lockfree_refcount)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE)(ctx, base);
    return;
  }
  Release(ctx, base, ctx.r3.u32);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_REF));
REX_HOOK_RAW(MUA_GUEST_ASSIGN_REF) {
  if (!REXCVAR_GET(mua_lockfree_refcount)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_REF)(ctx, base);
    return;
  }
  AssignReference(ctx, base, true);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_ADOPTED_REF));
REX_HOOK_RAW(MUA_GUEST_ASSIGN_ADOPTED_REF) {
  if (!REXCVAR_GET(mua_lockfree_refcount)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_ADOPTED_REF)(ctx, base);
    return;
  }
  AssignReference(ctx, base, false);
}

// r3 matrix a, r4 matrix b: 1 when every |b - a| is below the game's epsilon, computed in single precision with
// denormals kept, as the guest's fsubs/fcmpu sequence does.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_NEARLY_EQUAL));
REX_HOOK_RAW(MUA_GUEST_MATRIX_NEARLY_EQUAL) {
  if (!REXCVAR_GET(mua_native_alchemy_helpers)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_NEARLY_EQUAL)(ctx, base);
    return;
  }
  ctx.fpscr.disableFlushMode();
  const double epsilon = LoadFloat(base, mua::guest::kMatrixEpsilon);
  const uint32_t a = ctx.r3.u32;
  const uint32_t b = ctx.r4.u32;
  uint64_t equal = 1;
  for (uint32_t i = 0; i < 16; ++i) {
    const float difference = float(double(LoadFloat(base, b + i * 4)) - double(LoadFloat(base, a + i * 4)));
    if (!(double(std::fabs(difference)) < epsilon)) {
      equal = 0;
      break;
    }
  }
  ctx.r3.u64 = equal;
}

// r3 destination, r4 source: ANDs seven groups of three words in place, in the guest's load and store order.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_AND_MASKS));
REX_HOOK_RAW(MUA_GUEST_AND_MASKS) {
  if (!REXCVAR_GET(mua_native_alchemy_helpers)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_AND_MASKS)(ctx, base);
    return;
  }
  uint32_t dest = ctx.r3.u32;
  uint32_t source = ctx.r4.u32;
  uint32_t last = 0;
  for (int group = 0; group < 7; ++group, dest += 12, source += 12) {
    const uint32_t source0 = Load32(base, source);
    const uint32_t dest0 = Load32(base, dest);
    const uint32_t dest1 = Load32(base, dest + 4);
    last = Load32(base, dest + 8);
    Store32(base, dest, dest0 & source0);
    Store32(base, dest + 4, Load32(base, source + 4) & dest1);
    Store32(base, dest + 8, Load32(base, source + 8) & last);
  }
  ctx.r3.u64 = last;
  ctx.r4.u64 = source;
}

// r3 object, r4 index: 1 when the index is in range and not in the object's excluded list.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_INDEX_ALLOWED));
REX_HOOK_RAW(MUA_GUEST_INDEX_ALLOWED) {
  if (!REXCVAR_GET(mua_native_alchemy_helpers)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_INDEX_ALLOWED)(ctx, base);
    return;
  }
  const uint32_t object = ctx.r3.u32;
  const int32_t index = ctx.r4.s32;
  uint64_t allowed = 0;
  if (index >= 0 && index < int32_t(Load32(base, object + 12))) {
    allowed = 1;
    const uint32_t list = Load32(base, object + 20);
    const int32_t count = int32_t(Load32(base, list + 8));
    const uint32_t entries = Load32(base, list + 16);
    for (int32_t i = 0; i < count; ++i) {
      uint16_t entry = 0;
      std::memcpy(&entry, rex::memory::GuestPtr<const uint8_t*>(base, entries + i * 2), sizeof(entry));
      if (int32_t(std::byteswap(entry)) == index) {
        allowed = 0;
        break;
      }
    }
  }
  ctx.r3.u64 = allowed;
}

REX_EXTERN(MUA_GUEST_FREE_STRING);
REX_EXTERN(MUA_GUEST_STRING_PREPARE);
REX_EXTERN(MUA_GUEST_STRING_INTERN);
REX_EXTERN(MUA_GUEST_DESTROY_RESOURCE);
REX_EXTERN(MUA_GUEST_RESIZE_ARRAY);
REX_EXTERN(MUA_GUEST_RELEASE_RESOURCE);

namespace {

// The recompiled module's layout (REX_IMAGE_BASE, REX_IMAGE_SIZE, REX_CODE_BASE, REX_CODE_SIZE and
// REX_THUNK_RESERVE_SIZE of the generated header), which locates the function table indirect calls go through.
constexpr uint64_t kImageBase = 0x82000000;
constexpr uint32_t kThunkReserveSize = 0x10000;

void CallIndirect(PPCContext& ctx, uint8_t* base, uint32_t target) {
  const uint32_t code_offset = target - mua::guest::kCodeBase;
  PPCFunc* function = nullptr;
  if (code_offset < mua::guest::kCodeSize + kThunkReserveSize) {
    function = *reinterpret_cast<PPCFunc**>(base + kImageBase + mua::guest::kImageSize + uint64_t(code_offset) * 2);
  }
  if (!function) {
    ctx.last_indirect_target = target;
    function = rex::runtime::ResolveIndirectFunction(target);
  }
  function(ctx, base);
}

// The frame and link register the replaced guest function would have had while it calls out.
class GuestFrame {
 public:
  GuestFrame(PPCContext& ctx, uint8_t* base, uint32_t size) : ctx_(ctx), caller_sp_(ctx.r1.u32), caller_lr_(ctx.lr) {
    Store32(base, caller_sp_ - size, caller_sp_);
    ctx.r1.u32 = caller_sp_ - size;
  }
  ~GuestFrame() {
    ctx_.r1.u32 = caller_sp_;
    ctx_.lr = caller_lr_;
  }
  GuestFrame(const GuestFrame&) = delete;
  GuestFrame& operator=(const GuestFrame&) = delete;

 private:
  PPCContext& ctx_;
  uint32_t caller_sp_;
  uint64_t caller_lr_;
};

double LoadSingle(uint8_t* base, uint32_t address) {
  return double(LoadFloat(base, address));
}

void StoreSingle(uint8_t* base, uint32_t address, double value) {
  Store32(base, address, std::bit_cast<uint32_t>(float(value)));
}

double RoundSingle(double value) {
  return double(float(value));
}

bool Overlaps(uint32_t a, uint32_t b) {
  constexpr uint32_t kMatrixBytes = 64;
  return a < b + kMatrixBytes && b < a + kMatrixBytes;
}

}  // namespace

// r3 object, r4 class meta: 1 when the object's meta or one of its parents is that meta.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_IS_OF_TYPE));
REX_HOOK_RAW(MUA_GUEST_IS_OF_TYPE) {
  if (!REXCVAR_GET(mua_native_alchemy_matrix_and_type)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_IS_OF_TYPE)(ctx, base);
    return;
  }
  const uint32_t object = ctx.r3.u32;
  const uint32_t meta = ctx.r4.u32;
  uint32_t current = 0;
  {
    GuestFrame frame(ctx, base, 96);
    const uint32_t get_meta = Load32(base, Load32(base, object) + 80);
    ctx.ctr.u64 = get_meta;
    ctx.lr = mua::guest::kIsOfTypeMetaSite;
    CallIndirect(ctx, base, get_meta);
    current = ctx.r3.u32;
  }
  while (current && current != meta) {
    current = Load32(base, current + 56);
  }
  ctx.r3.u64 = current ? 1 : 0;
}

// r3 destination, r4 a, r5 b: destination = a * b, each element rounded exactly as the guest's fmuls/fmadds chain
// in its own accumulation order. Overlapping operands keep the guest's interleaved loads and stores.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_MULTIPLY));
REX_HOOK_RAW(MUA_GUEST_MATRIX_MULTIPLY) {
  const uint32_t dest = ctx.r3.u32;
  const uint32_t a = ctx.r4.u32;
  const uint32_t b = ctx.r5.u32;
  if (!REXCVAR_GET(mua_native_alchemy_matrix_and_type) || Overlaps(dest, a) || Overlaps(dest, b)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_MULTIPLY)(ctx, base);
    return;
  }
  ctx.fpscr.disableFlushMode();
  double m[16];
  double n[16];
  for (uint32_t i = 0; i < 16; ++i) {
    m[i] = LoadSingle(base, a + i * 4);
    n[i] = LoadSingle(base, b + i * 4);
  }
  for (uint32_t j = 0; j < 4; ++j) {
    const double b0 = n[j];
    const double b1 = n[4 + j];
    const double b2 = n[8 + j];
    const double b3 = n[12 + j];
    double row = RoundSingle(m[0] * b0);
    row = RoundSingle(std::fma(m[3], b3, row));
    row = RoundSingle(std::fma(m[1], b1, row));
    row = RoundSingle(std::fma(b2, m[2], row));
    StoreSingle(base, dest + j * 4, row);
    row = RoundSingle(m[4] * b0);
    row = RoundSingle(std::fma(m[7], b3, row));
    row = RoundSingle(std::fma(b2, m[6], row));
    row = RoundSingle(std::fma(b1, m[5], row));
    StoreSingle(base, dest + 16 + j * 4, row);
    row = RoundSingle(m[9] * b1);
    row = RoundSingle(std::fma(m[8], b0, row));
    row = RoundSingle(std::fma(m[11], b3, row));
    row = RoundSingle(std::fma(b2, m[10], row));
    StoreSingle(base, dest + 32 + j * 4, row);
    row = RoundSingle(m[12] * b0);
    row = RoundSingle(std::fma(m[15], b3, row));
    row = RoundSingle(std::fma(b2, m[14], row));
    row = RoundSingle(std::fma(b1, m[13], row));
    StoreSingle(base, dest + 48 + j * 4, row);
  }
}

// r3 destination, r4 source: sixteen single-precision loads and stores, one element at a time as in the guest.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_COPY));
REX_HOOK_RAW(MUA_GUEST_MATRIX_COPY) {
  if (!REXCVAR_GET(mua_native_alchemy_matrix_and_type)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_MATRIX_COPY)(ctx, base);
    return;
  }
  ctx.fpscr.disableFlushMode();
  const uint32_t dest = ctx.r3.u32;
  const uint32_t source = ctx.r4.u32;
  for (uint32_t i = 0; i < 16; ++i) {
    StoreSingle(base, dest + i * 4, LoadSingle(base, source + i * 4));
  }
}

// r3 string slot. The count sits in the header 8 bytes before the string; the last reference frees it.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE_STRING));
REX_HOOK_RAW(MUA_GUEST_RELEASE_STRING) {
  if (!REXCVAR_GET(mua_lockfree_string_and_resource_refs)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE_STRING)(ctx, base);
    return;
  }
  const uint32_t string = Load32(base, ctx.r3.u32);
  if (!string) {
    return;
  }
  const uint32_t header = string - 8;
  ctx.r4.u64 = header;
  if (AddToRefCount(base, header, -1)) {
    return;
  }
  ctx.r3.u64 = Load32(base, header);
  MUA_GUEST_FREE_STRING(ctx, base);
}

// r3 string slot, r4 text: stores the interned string and releases the one it replaces.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_STRING));
REX_HOOK_RAW(MUA_GUEST_ASSIGN_STRING) {
  if (!REXCVAR_GET(mua_lockfree_string_and_resource_refs)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_ASSIGN_STRING)(ctx, base);
    return;
  }
  const uint32_t slot = ctx.r3.u32;
  const uint32_t text = ctx.r4.u32;
  GuestFrame frame(ctx, base, 112);
  ctx.lr = mua::guest::kAssignStringPrepareSite;
  MUA_GUEST_STRING_PREPARE(ctx, base);
  ctx.r4.u64 = text;
  ctx.lr = mua::guest::kAssignStringInternSite;
  MUA_GUEST_STRING_INTERN(ctx, base);
  const uint32_t interned = ctx.r3.u32;
  if (const uint32_t old = Load32(base, slot)) {
    const uint32_t header = old - 8;
    ctx.r4.u64 = header;
    if (!AddToRefCount(base, header, -1)) {
      ctx.r3.u64 = Load32(base, header);
      ctx.lr = mua::guest::kAssignStringFreeSite;
      MUA_GUEST_FREE_STRING(ctx, base);
    }
  }
  ctx.r3.u64 = slot;
  Store32(base, slot, interned);
}

// r3 D3D resource: returns the remaining count; the last reference releases a texture's parent first and then frees
// the resource.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE_RESOURCE));
REX_HOOK_RAW(MUA_GUEST_RELEASE_RESOURCE) {
  if (!REXCVAR_GET(mua_lockfree_string_and_resource_refs)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_RELEASE_RESOURCE)(ctx, base);
    return;
  }
  const uint32_t resource = ctx.r3.u32;
  const uint32_t remaining = AddToRefCount(base, resource, -1);
  ctx.r3.u64 = remaining;
  if (remaining) {
    return;
  }
  GuestFrame frame(ctx, base, 96);
  const uint32_t header = Load32(base, resource);
  if ((header & 0xF) == 4 && (header & 0x40000000)) {
    ctx.r3.u64 = Load32(base, resource + 24);
    ctx.lr = mua::guest::kReleaseResourceParentSite;
    MUA_GUEST_RELEASE_RESOURCE(ctx, base);
  }
  ctx.r3.u64 = resource;
  ctx.lr = mua::guest::kReleaseResourceDestroySite;
  MUA_GUEST_DESTROY_RESOURCE(ctx, base);
  ctx.r3.u64 = 0;
}

// r3 object array, r4 object (null allowed): appends a counted reference, growing the array when full.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_APPEND_REF));
REX_HOOK_RAW(MUA_GUEST_APPEND_REF) {
  if (!REXCVAR_GET(mua_lockfree_string_and_resource_refs)) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_APPEND_REF)(ctx, base);
    return;
  }
  const uint32_t array = ctx.r3.u32;
  const uint32_t object = ctx.r4.u32;
  if (object) {
    AddToRefCount(base, object, 1);
  }
  const int32_t count = int32_t(Load32(base, array + 8));
  if (count < int32_t(Load32(base, array + 12))) {
    Store32(base, array + 8, uint32_t(count + 1));
  } else {
    GuestFrame frame(ctx, base, 112);
    ctx.r3.u64 = array;
    ctx.r4.u64 = uint32_t(count + 1);
    ctx.r5.u64 = 4;
    ctx.lr = mua::guest::kAppendRefResizeSite;
    MUA_GUEST_RESIZE_ARRAY(ctx, base);
  }
  Store32(base, Load32(base, array + 16) + uint32_t(count) * 4, object);
}

#endif  // MUA_GUEST_HAS_ALCHEMY_HELPERS
