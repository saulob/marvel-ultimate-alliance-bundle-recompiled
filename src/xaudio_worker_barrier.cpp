// The XAudio mix-worker rendezvous run on the host. Adapted from crackdown-xbox360-recompiled src/audio_guest_sync.cpp,
// itself from Unowa001/crackdown-recomp (BSD 3-Clause, see packaging/licenses/THIRD-PARTY-LICENSES.txt).

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>

#include "guest_map.h"
#include "xaudio_barrier.h"

#if MUA_GUEST_HAS_AUDIO_BARRIER

bool mua_audio_barrier_escape();

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_AUDIO_BARRIER));

namespace {

constexpr uint32_t kEngineActiveOffset = 304;
constexpr uint32_t kEngineProcessorSlotsOffset = 308;
constexpr uint32_t kEngineProcessorSlots = 6;
constexpr uint32_t kPcrProcessorNumberOffset = 268;
constexpr uint32_t kSpinsBeforeYield = 4096;

uint32_t LoadGuest32(uint8_t* base, uint32_t address) {
  uint32_t value = 0;
  std::memcpy(&value, rex::memory::GuestPtr<const uint8_t*>(base, address), sizeof(value));
  return std::byteswap(value);
}

uint8_t LoadGuest8(uint8_t* base, uint32_t address) {
  return *rex::memory::GuestPtr<const uint8_t*>(base, address);
}

void SpinPause() {
#if defined(_M_X64) || defined(__x86_64__)
  _mm_pause();
#endif
}

// XAudio has one engine with two barrier words that alternate per pass, so a few are enough.
struct BarrierSlot {
  std::atomic<uint32_t> word{0};
  mua::XAudioBarrier barrier;
};
BarrierSlot g_barriers[8];

mua::XAudioBarrier* BarrierFor(uint32_t word) {
  for (BarrierSlot& slot : g_barriers) {
    uint32_t current = slot.word.load(std::memory_order_acquire);
    if (current == 0 && slot.word.compare_exchange_strong(current, word, std::memory_order_acq_rel)) {
      current = word;
    }
    if (current == word) {
      return &slot.barrier;
    }
  }
  return nullptr;
}

uint64_t ExpectedProcessors(uint8_t* base, uint32_t engine) {
  uint64_t expected = 0;
  for (uint32_t i = 0; i < kEngineProcessorSlots; ++i) {
    if (LoadGuest32(base, engine + kEngineProcessorSlotsOffset + 4 * i)) {
      expected |= uint64_t(1) << i;
    }
  }
  return expected;
}

void WaitForRelease(const mua::XAudioBarrier& barrier, uint64_t round) {
  uint64_t spins = 0;
  while (barrier.RoundOpen(round)) {
    if (++spins % kSpinsBeforeYield == 0) {
      if (mua_audio_barrier_escape()) {
        break;
      }
      std::this_thread::yield();
    } else {
      SpinPause();
    }
  }
}

}  // namespace

// Codegen hook at the spin of the rendezvous (overrides.toml), also polled by the host rendezvous. The workers
// wait there for each other without a kernel wait, so one whose partner already stopped for title termination would
// wait forever.
bool mua_audio_barrier_escape() {
  const auto* kernel = rex::system::kernel_state();
  return kernel && kernel->is_terminating_title();
}

// (engine, word): the XAudio mix workers' rendezvous once per audio pass. The title's version marks its
// processor's byte in the guest word and spins until all are in or the word is reset, so a worker preempted in between
// can miss the reset and spin forever with its partner. Only this function uses the word.
REX_HOOK_RAW(MUA_GUEST_AUDIO_BARRIER) {
  const uint32_t engine = ctx.r3.u32;
  if (!LoadGuest32(base, engine + kEngineActiveOffset)) {
    return;
  }
  const uint8_t processor = LoadGuest8(base, ctx.r13.u32 + kPcrProcessorNumberOffset);
  mua::XAudioBarrier* barrier = processor < 8 ? BarrierFor(ctx.r4.u32) : nullptr;
  if (!barrier) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_AUDIO_BARRIER)(ctx, base);
    return;
  }
  const mua::XAudioBarrier::Arrival arrival = barrier->Arrive(processor, ExpectedProcessors(base, engine));
  if (!arrival.released_round) {
    WaitForRelease(*barrier, arrival.round);
  }
}

#endif  // MUA_GUEST_HAS_AUDIO_BARRIER
