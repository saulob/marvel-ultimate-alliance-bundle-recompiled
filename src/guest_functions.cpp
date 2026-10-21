#include "guest_functions.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/ppc/func.h>

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

}  // namespace

void InstallGuestFunctionTable(const PPCFuncMapping* mappings) {
  g_mappings = mappings;
  SetCrashGuestFunctionResolver(&ResolveGuestFunction);
}

}  // namespace mua
