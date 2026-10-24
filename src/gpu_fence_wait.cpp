// The Direct3D GPU waits that poll a word of the device's writeback block (the fence at +0, the ring read pointer at
// +60, the ring progress word at +4) block on that word instead of spinning; the command processor wakes them when
// it stores a guest word (patches 0089 and 0100). Adapted from crackdown-xbox360-recompiled src/gpu_fence_wait.cpp.

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_frame_counters.h"
#include "guest_map.h"

#if defined(_WIN32)

namespace {

constexpr uint8_t kDeviceStoppedFlag = 2;
// The wait object: the device at +0, the time base when the wait began at +20.
constexpr uint32_t kWaitStartOffset = 20;
// Blocks that ended by timeout with the word already changed before a site's blocking is given up.
constexpr uint32_t kMissedWakeLimit = 8;
// Timed-out blocks in a row after which one wait goes back to the guest's own polling.
constexpr uint32_t kWaitTimeoutLimit = 8;

struct PollSite {
  const char* name;
  uint32_t return_address;
  uint32_t word_offset;
  // Upper bound on one block, for writers that do not wake the waiter.
  uint32_t timeout_ms;
};

// return_address is where each site's call to the poll returns; 0 is a site this game's map does not have.
constexpr std::array<PollSite, 3> kSites = {{
    {"fence", mua::guest::kFencePollSite, 0, 2},
    {"ring_space", mua::guest::kRingSpacePollSite, 60, 1},
    {"ring_progress", mua::guest::kRingProgressPollSite, 4, 1},
}};
constexpr size_t kOtherSite = kSites.size();

struct alignas(64) SiteState {
  std::atomic<bool> blocking{true};
  std::atomic<uint32_t> missed_wakes{0};
};
SiteState g_site_state[kSites.size()];

struct WaitState {
  uint32_t object = 0;
  uint32_t device = 0;
  uint32_t start = 0;
  uint32_t block = 0;
  const uint32_t* word = nullptr;
  const uint32_t* guest_word = nullptr;
  uint32_t seen = 0;
  uint32_t timeouts = 0;
  bool polling = false;
};
thread_local WaitState t_wait;

std::atomic<int> g_gave_up_reports{0};  // DEBUG_SAULO

size_t SiteIndex(uint32_t return_address) {
  for (size_t i = 0; i < kSites.size(); ++i) {
    if (kSites[i].return_address && kSites[i].return_address == return_address) {
      return i;
    }
  }
  return kOtherSite;
}

template <typename T>
inline T LoadBE(const uint8_t* base, uint32_t addr) {
  T v;
  std::memcpy(&v, base + addr, sizeof(v));
  return std::byteswap(v);
}

constexpr uint32_t HostOffset(uint32_t address) {
  return rex::memory::detail::PhysicalHostOffset(address);
}

inline uint32_t LoadGuest32(const uint8_t* base, uint32_t address) {
  return LoadBE<uint32_t>(base, address + HostOffset(address));
}

inline uint32_t LoadWord(const uint32_t* word) {
  return __atomic_load_n(word, __ATOMIC_ACQUIRE);
}

bool DeviceStopped(const uint8_t* base, uint32_t device) {
  const uint32_t address = device + mua::guest::kDeviceFlagsOffset;
  return LoadBE<uint8_t>(base, address + HostOffset(address)) & kDeviceStoppedFlag;
}

// Both views are the same pages; a difference that outlasts one re-read means the physical
// address is not the one the guest reads.
bool ViewsAgree(const uint32_t* physical_word, const uint32_t* guest_word) {
  const uint32_t physical = LoadWord(physical_word);
  return LoadWord(guest_word) == physical || LoadWord(physical_word) != physical;
}

void DisableBlocking(size_t site, const char* reason) {
  if (g_site_state[site].blocking.exchange(false, std::memory_order_relaxed)) {
    REXLOG_WARN("GPU wait {}: blocking disabled ({}), the guest polls again", kSites[site].name, reason);
  }
}

// The command processor stores to and wakes physical_membase + (address & 0x1FFFFFFF), where address
// is the hardware form of the guest address that the guest emits into the packet.
bool ResolveWaitWord(const uint8_t* base, uint32_t device, uint32_t word_offset, WaitState& wait) {
  if (!device || DeviceStopped(base, device)) {
    return false;
  }
  const uint32_t block = LoadGuest32(base, device + mua::guest::kDeviceFenceOffset);
  if (!block || (block & 3)) {
    return false;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  rex::memory::Memory* memory = kernel ? kernel->memory() : nullptr;
  if (!memory) {
    return false;
  }
  const uint32_t address = block + word_offset;
  const uint32_t physical = (address & 0x1FFFFFFF) + ((((address >> 20) & 0xFFF) + 512) & 0x1000);
  wait.word = memory->TranslatePhysical<const uint32_t*>(physical);
  wait.guest_word = reinterpret_cast<const uint32_t*>(base + address + HostOffset(address));
  wait.device = device;
  wait.block = block;
  return ViewsAgree(wait.word, wait.guest_word);
}

enum class WaitOutcome { kReturned, kTimedOut, kFailed };

// Returns at once if the word no longer holds `undesired`; the comparison and the sleep are one
// atomic step, so a store between the caller's read and the sleep is never slept through.
WaitOutcome WaitWhileEqual(const uint32_t* word, uint32_t undesired, uint32_t timeout_ms) {
  const auto wait_start = std::chrono::steady_clock::now();
  const bool woke = WaitOnAddress(const_cast<uint32_t*>(word), &undesired, sizeof(undesired), timeout_ms);
  const DWORD error = woke ? ERROR_SUCCESS : GetLastError();
  mua::AddGuestFrameTime(mua::GuestFrameTime::kFenceWait, std::chrono::steady_clock::now() - wait_start);
  if (woke) {
    return WaitOutcome::kReturned;
  }
  return error == ERROR_TIMEOUT ? WaitOutcome::kTimedOut : WaitOutcome::kFailed;
}

// Blocks while the word still holds the value read before the guest last re-checked its condition,
// so a write after that read is never slept through.
void BlockUntilWordWritten(size_t site, WaitState& wait) {
  const WaitOutcome outcome = WaitWhileEqual(wait.word, wait.seen, kSites[site].timeout_ms);
  const uint32_t current = LoadWord(wait.word);
  if (outcome == WaitOutcome::kFailed) {
    DisableBlocking(site, "wait failed");
  } else if (!ViewsAgree(wait.word, wait.guest_word)) {
    DisableBlocking(site, "the guest reads another address");
  }
  SiteState& state = g_site_state[site];
  if (outcome == WaitOutcome::kTimedOut) {
    if (current != wait.seen &&
        state.missed_wakes.fetch_add(1, std::memory_order_relaxed) + 1 >= kMissedWakeLimit) {
      DisableBlocking(site, "its writer does not wake it");
    }
    if (current == wait.seen && ++wait.timeouts >= kWaitTimeoutLimit) {
      wait.polling = true;
      if (g_gave_up_reports.fetch_add(1, std::memory_order_relaxed) < 20) {
        REXLOG_INFO("DEBUG_SAULO GPU wait {}: word {:08X} unchanged for {} blocks, the guest polls this wait",
                    kSites[site].name, current, wait.timeouts);
      }
    }
  } else {
    wait.timeouts = 0;
    if (outcome == WaitOutcome::kReturned && current != wait.seen) {
      state.missed_wakes.store(0, std::memory_order_relaxed);
    }
  }
  wait.seen = current;
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_FENCE_BEGIN));
REX_HOOK_RAW(MUA_GUEST_FENCE_BEGIN) {
  t_wait = {};
  MUA_GUEST_ORIGINAL(MUA_GUEST_FENCE_BEGIN)(ctx, base);
  t_wait = {};
}

// Returns nonzero while the wait should go on. The first call for a wait only reads the word, so a block always
// follows a check of the word by the guest.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_FENCE_POLL));
REX_HOOK_RAW(MUA_GUEST_FENCE_POLL) {
  const uint32_t object = ctx.r3.u32;
  const size_t site = SiteIndex(uint32_t(ctx.lr));
  MUA_GUEST_ORIGINAL(MUA_GUEST_FENCE_POLL)(ctx, base);
  if (site == kOtherSite || !ctx.r3.u32 || !g_site_state[site].blocking.load(std::memory_order_relaxed)) {
    return;
  }
  WaitState& wait = t_wait;
  const uint32_t device = LoadGuest32(base, object);
  const uint32_t start = LoadGuest32(base, object + kWaitStartOffset);
  if (!wait.word || wait.object != object || wait.device != device || wait.start != start) {
    wait = {};
    if (!ResolveWaitWord(base, device, kSites[site].word_offset, wait)) {
      wait = {};
      return;
    }
    wait.object = object;
    wait.start = start;
    wait.seen = LoadWord(wait.word);
    return;
  }
  if (wait.polling) {
    return;
  }
  if (DeviceStopped(base, device) || LoadGuest32(base, device + mua::guest::kDeviceFenceOffset) != wait.block) {
    wait = {};
    return;
  }
  const uint32_t current = LoadWord(wait.word);
  if (current != wait.seen) {
    wait.seen = current;
    return;
  }
  BlockUntilWordWritten(site, wait);
}

#endif  // _WIN32
