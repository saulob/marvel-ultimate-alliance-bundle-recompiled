// Codegen midasm hooks (config/mua2/overrides.toml) on the mfmsr of Marvel Ultimate Alliance 2's job scheduler, spin
// lock and block allocator atomic sections: true skips to the instruction after the section.

#include "guest_atomics.h"

#if defined(MUA_RUNTIME_MUA2)

#include <chrono>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/thread/mutex.h>

#include "window_host.h"

REXCVAR_DEFINE_BOOL(mua_lockfree_job_atomics, true, "Marvel Ultimate Alliance",
                    "Run the job system's atomic sections without the recompiler's global interrupt lock")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

uint8_t* GuestBase() {
  static uint8_t* const base = rex::system::kernel_state()->memory()->virtual_membase();
  return base;
}

// DEBUG_SAULO: hook calls per frame, each a lock section the guest no longer takes, logged every 10 s.
void CountLockFreeSection() {
  constexpr uint32_t kBatch = 256;
  thread_local uint32_t pending = 0;
  if (++pending < kBatch) {
    return;
  }
  pending = 0;
  static std::atomic<uint64_t> calls{0};
  static std::atomic<int64_t> next_report_ns{0};
  static std::atomic<uint64_t> calls_at_report{0};
  static std::atomic<uint64_t> frames_at_report{0};
  const uint64_t total = calls.fetch_add(kBatch, std::memory_order_relaxed) + kBatch;
  const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  int64_t due = next_report_ns.load(std::memory_order_relaxed);
  if (now < due || !next_report_ns.compare_exchange_strong(due, now + 10'000'000'000, std::memory_order_relaxed)) {
    return;
  }
  const uint64_t frames = GetGuestFrameStats().frame_count;
  const uint64_t previous_calls = calls_at_report.exchange(total, std::memory_order_relaxed);
  const uint64_t previous_frames = frames_at_report.exchange(frames, std::memory_order_relaxed);
  if (due != 0 && frames > previous_frames) {
    REXLOG_INFO("DEBUG_SAULO MUA2 lock-free job atomics per frame: {:.1f} hook calls (lock sections avoided)",
                double(total - previous_calls) / double(frames - previous_frames));
  }
}

bool LockFree() {
  if (!REXCVAR_GET(mua_lockfree_job_atomics)) {
    return false;
  }
  CountLockFreeSection();  // DEBUG_SAULO
  return true;
}

}  // namespace

// lwarx value,0,address; addi value,value,1; stwcx. value,0,address
bool mua_atomic_increment_mua2(PPCRegister& msr, PPCRegister& value, PPCRegister& address, PPCCRRegister& cr0,
                               PPCRegister& reserved) {
  if (!LockFree()) {
    return false;
  }
  mua::atomics::AddSection<uint32_t>(GuestBase(), rex::thread::GuestInterruptMsr(), msr, value, value, 1, address.u32,
                                     cr0, reserved);
  return true;
}

// lwarx value,0,address; addi value,value,-1; stwcx. value,0,address
bool mua_atomic_decrement_mua2(PPCRegister& msr, PPCRegister& value, PPCRegister& address, PPCCRRegister& cr0,
                               PPCRegister& reserved) {
  if (!LockFree()) {
    return false;
  }
  mua::atomics::AddSection<uint32_t>(GuestBase(), rex::thread::GuestInterruptMsr(), msr, value, value,
                                     uint64_t(int64_t{-1}), address.u32, cr0, reserved);
  return true;
}

// lwarx old,0,address; add new,old,addend; stwcx. new,0,address
bool mua_atomic_add32_mua2(PPCRegister& msr, PPCRegister& old_value, PPCRegister& new_value, PPCRegister& addend,
                           PPCRegister& address, PPCCRRegister& cr0, PPCRegister& reserved) {
  if (!LockFree()) {
    return false;
  }
  mua::atomics::AddSection<uint32_t>(GuestBase(), rex::thread::GuestInterruptMsr(), msr, old_value, new_value,
                                     addend.u64, address.u32, cr0, reserved);
  return true;
}

// ldarx old,0,address; add new,old,addend; stdcx. new,0,address
bool mua_atomic_add64_mua2(PPCRegister& msr, PPCRegister& old_value, PPCRegister& new_value, PPCRegister& addend,
                           PPCRegister& address, PPCCRRegister& cr0, PPCRegister& reserved) {
  if (!LockFree()) {
    return false;
  }
  mua::atomics::AddSection<uint64_t>(GuestBase(), rex::thread::GuestInterruptMsr(), msr, old_value, new_value,
                                     addend.u64, address.u32, cr0, reserved);
  return true;
}

// ldarx observed,0,address; cmpd cr6,observed,expected; stdcx. desired (or observed back),0,address
bool mua_atomic_cas64_mua2(PPCRegister& msr, PPCRegister& observed, PPCRegister& expected, PPCRegister& desired,
                           PPCRegister& address, PPCCRRegister& cr6, PPCCRRegister& cr0, PPCXERRegister& xer,
                           PPCRegister& reserved) {
  if (!LockFree()) {
    return false;
  }
  mua::atomics::CompareExchangeSection64(GuestBase(), rex::thread::GuestInterruptMsr(), msr, observed, expected.u64,
                                         desired.u64, address.u32, cr6, cr0, xer, reserved);
  return true;
}

#endif
