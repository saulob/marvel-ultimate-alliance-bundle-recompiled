// Codegen midasm hooks (config/mua2/overrides.toml) on the mfmsr of Marvel Ultimate Alliance 2's job scheduler, spin
// lock and block allocator atomic sections: true skips to the instruction after the section.

#include "guest_atomics.h"

#if defined(MUA_RUNTIME_MUA2)

#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/thread/mutex.h>

REXCVAR_DEFINE_BOOL(mua_lockfree_job_atomics, true, "Marvel Ultimate Alliance",
                    "Run the job system's atomic sections without the recompiler's global interrupt lock")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace {

uint8_t* GuestBase() {
  static uint8_t* const base = rex::system::kernel_state()->memory()->virtual_membase();
  return base;
}

bool LockFree() {
  return REXCVAR_GET(mua_lockfree_job_atomics);
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
