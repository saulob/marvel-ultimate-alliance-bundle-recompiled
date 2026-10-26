#pragma once

#include <atomic>
#include <bit>
#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/system/xmemory.h>

// One guest lwarx/ldarx ... stwcx./stdcx. section done as a single host atomic, leaving the registers the section
// leaves when its store-conditional succeeds. The recompiler's global lock around such a section adds no exclusion:
// its store-conditional is already a compare-and-swap, and no section holds any other memory access.
namespace mua::atomics {

template <typename Word>
std::atomic_ref<Word> GuestWord(uint8_t* base, uint32_t address) {
  return std::atomic_ref<Word>(*rex::memory::GuestPtr<Word*>(base, address));
}

// Returns the big-endian word before the add, which wraps at the word's width.
template <typename Word>
Word AddToGuestWord(uint8_t* base, uint32_t address, Word addend) {
  std::atomic_ref<Word> word = GuestWord<Word>(base, address);
  Word observed = word.load(std::memory_order_relaxed);
  while (!word.compare_exchange_weak(observed, std::byteswap(Word(std::byteswap(observed) + addend)),
                                     std::memory_order_seq_cst, std::memory_order_relaxed)) {
  }
  return std::byteswap(observed);
}

// Stores desired only over expected; returns the word it found.
template <typename Word>
Word CompareExchangeGuestWord(uint8_t* base, uint32_t address, Word expected, Word desired) {
  Word observed = std::byteswap(expected);
  GuestWord<Word>(base, address).compare_exchange_strong(observed, std::byteswap(desired), std::memory_order_seq_cst);
  return std::byteswap(observed);
}

inline void SetStoreConditionalSucceeded(PPCCRRegister& cr0) {
  cr0.lt = 0;
  cr0.gt = 0;
  cr0.eq = 1;
}

// mfmsr msr; mtmsrd r13,1; lwarx/ldarx old,0,address; addi/add new,old,addend; stwcx./stdcx. new,0,address;
// mtmsrd msr,1. The add is the register's full 64 bits, as in the guest; old and new may be one register.
template <typename Word>
void AddSection(uint8_t* base, uint64_t msr_value, PPCRegister& msr, PPCRegister& old_value, PPCRegister& new_value,
                uint64_t addend, uint32_t address, PPCCRRegister& cr0, PPCRegister& reserved) {
  msr.u64 = msr_value;
  const Word old = AddToGuestWord<Word>(base, address, Word(addend));
  if constexpr (sizeof(Word) == 4) {
    reserved.u32 = std::byteswap(old);
  } else {
    reserved.u64 = std::byteswap(old);
  }
  old_value.u64 = old;
  new_value.u64 = addend + uint64_t(old);
  SetStoreConditionalSucceeded(cr0);
}

// mfmsr msr; mtmsrd r13,1; ldarx observed,0,address; cmpd cr6,observed,expected; bne cr6,keep;
// stdcx. desired,0,address; mtmsrd msr,1; bne retry; b done; keep: stdcx. observed,0,address; mtmsrd msr,1; done:
inline void CompareExchangeSection64(uint8_t* base, uint64_t msr_value, PPCRegister& msr, PPCRegister& observed,
                                     uint64_t expected, uint64_t desired, uint32_t address, PPCCRRegister& cr6,
                                     PPCCRRegister& cr0, const PPCXERRegister& xer, PPCRegister& reserved) {
  msr.u64 = msr_value;
  const uint64_t found = CompareExchangeGuestWord<uint64_t>(base, address, expected, desired);
  reserved.u64 = std::byteswap(found);
  observed.u64 = found;
  cr6.compare<int64_t>(int64_t(found), int64_t(expected), xer);
  SetStoreConditionalSucceeded(cr0);
}

}  // namespace mua::atomics
