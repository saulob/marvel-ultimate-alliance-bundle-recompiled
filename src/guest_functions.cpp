#include "guest_functions.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>

#include "crash_report.h"

namespace mua {
namespace {

const PPCFuncMapping* g_mappings = nullptr;

void ResolveGuestFunction(uint64_t host_address, CrashGuestRef* out) {
  if (out == nullptr || g_mappings == nullptr || host_address == 0) {
    return;
  }
  uint64_t function_start = host_address;
#if defined(_WIN32) && defined(_M_X64)
  DWORD64 image_base = 0;
  if (const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(host_address, &image_base, nullptr); entry != nullptr) {
    function_start = image_base + entry->BeginAddress;
  }
#endif
  const PPCFuncMapping* match = nullptr;
  bool ambiguous = false;
  for (const PPCFuncMapping* mapping = g_mappings; mapping->guest != 0 || mapping->host != nullptr; ++mapping) {
    if (mapping->host == nullptr || reinterpret_cast<uint64_t>(mapping->host) != function_start) {
      continue;
    }
    if (match != nullptr) {
      ambiguous = true;
      break;
    }
    match = mapping;
  }
  if (match == nullptr || ambiguous) {
    return;
  }
  out->address = static_cast<uint32_t>(match->guest);
  std::snprintf(out->name, sizeof(out->name), "sub_%08X", out->address);
  out->known = true;
}

// Thread-local and plain reads only: it runs inside the crash handler.
void ResolveGuestThread(uint64_t referenced_address, CrashGuestThread* out) {
  const rex::runtime::ThreadState* thread = out != nullptr ? rex::runtime::ThreadState::Get() : nullptr;
  if (thread == nullptr || thread->context() == nullptr) {
    return;
  }
  const PPCContext& ctx = *thread->context();
  out->known = true;
  out->thread_id = thread->thread_id();
  out->lr = static_cast<uint32_t>(ctx.lr);
  out->r1 = ctx.r1.u32;
  const PPCRegister* const registers[] = {&ctx.r3, &ctx.r4, &ctx.r5, &ctx.r6,  &ctx.r7,
                                          &ctx.r8, &ctx.r9, &ctx.r10, &ctx.r11, &ctx.r12};
  for (size_t i = 0; i < std::size(registers); ++i) {
    out->r3_to_r12[i] = registers[i]->u32;
  }
  const rex::memory::Memory* memory = thread->memory();
  if (referenced_address == 0 || memory == nullptr) {
    return;
  }
  constexpr uint64_t kGuestSpan = uint64_t(1) << 32;
  const uint64_t virtual_base = reinterpret_cast<uint64_t>(memory->virtual_membase());
  const uint64_t physical_base = reinterpret_cast<uint64_t>(memory->physical_membase());
  if (virtual_base != 0 && referenced_address - virtual_base < kGuestSpan) {
    out->data_address_known = true;
    out->data_address = static_cast<uint32_t>(referenced_address - virtual_base);
  } else if (physical_base != 0 && referenced_address - physical_base < kGuestSpan) {
    out->data_address_known = true;
    out->data_address_physical = true;
    out->data_address = static_cast<uint32_t>(referenced_address - physical_base);
  }
}

}  // namespace

void InstallGuestFunctionTable(const PPCFuncMapping* mappings) {
  g_mappings = mappings;
  SetCrashGuestFunctionResolver(&ResolveGuestFunction);
  SetCrashGuestThreadResolver(&ResolveGuestThread);
}

}  // namespace mua
