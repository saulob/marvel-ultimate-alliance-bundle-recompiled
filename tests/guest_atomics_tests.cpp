// The native atomic sections of src/guest_atomics.h against the locked sections they replace: the locked section run
// statement for statement as the recompiler emits it, compared register for register and under contention, and the
// hook sites in config/mua2/overrides.toml checked against the generated Marvel Ultimate Alliance 2 code.
// Run: mua_guest_atomics_tests

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "guest_atomics.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

using rex::ppc::CRRegister;
using rex::ppc::Register;
using rex::ppc::XERRegister;

constexpr uint64_t kMsr = 0x8000;
constexpr uint32_t kMemorySize = 0x10000;

// Guest memory below 0xE0000000, where a guest address is the host offset from the base.
struct GuestMemory {
  std::vector<uint64_t> words = std::vector<uint64_t>(kMemorySize / 8);
  uint8_t* base() { return reinterpret_cast<uint8_t*>(words.data()); }
  template <typename Word>
  Word Load(uint32_t address) {
    Word value;
    std::memcpy(&value, base() + address, sizeof(value));
    return std::byteswap(value);
  }
  template <typename Word>
  void Store(uint32_t address, Word value) {
    value = std::byteswap(value);
    std::memcpy(base() + address, &value, sizeof(value));
  }
};

uint8_t* RawAddress(uint8_t* base, uint32_t ea) {
  return rex::memory::GuestPtr(base, ea);
}

std::mutex g_global_lock;

// The generated lwarx/ldarx, addi/add, stwcx./stdcx. section with REX_ENTER/LEAVE_GLOBAL_LOCK, statement for
// statement; addend null means addi with imm.
template <typename Word>
void LockedAddSection(uint8_t* base, uint64_t msr_value, Register& msr, Register& old_reg, Register& new_reg,
                      const Register* addend, int64_t imm, Register& address, CRRegister& cr0, Register& reserved) {
  uint32_t ea{};
  for (;;) {
    msr.u64 = msr_value;
    g_global_lock.lock();
    ea = address.u32;
    if constexpr (sizeof(Word) == 4) {
      reserved.u32 = *(uint32_t*)RawAddress(base, ea);
      old_reg.u64 = __builtin_bswap32(reserved.u32);
    } else {
      reserved.u64 = *(uint64_t*)RawAddress(base, ea);
      old_reg.u64 = __builtin_bswap64(reserved.u64);
    }
    if (addend) {
      new_reg.u64 = addend->u64 + old_reg.u64;
    } else {
      new_reg.s64 = old_reg.s64 + imm;
    }
    ea = address.u32;
    cr0.lt = 0;
    cr0.gt = 0;
    if constexpr (sizeof(Word) == 4) {
      cr0.eq = reinterpret_cast<std::atomic<uint32_t>*>(RawAddress(base, ea))
                   ->compare_exchange_strong(reserved.u32, __builtin_bswap32(new_reg.u32), std::memory_order_acq_rel,
                                             std::memory_order_acquire);
    } else {
      cr0.eq = reinterpret_cast<std::atomic<uint64_t>*>(RawAddress(base, ea))
                   ->compare_exchange_strong(reserved.u64, __builtin_bswap64(new_reg.u64), std::memory_order_acq_rel,
                                             std::memory_order_acquire);
    }
    g_global_lock.unlock();
    if (cr0.eq) {
      return;
    }
  }
}

// The generated ldarx, cmpd cr6, stdcx. desired or stdcx. observed back section, statement for statement.
void LockedCompareExchangeSection64(uint8_t* base, uint64_t msr_value, Register& msr, Register& observed,
                                    const Register& expected, const Register& desired, Register& address,
                                    CRRegister& cr6, CRRegister& cr0, const XERRegister& xer, Register& reserved) {
  uint32_t ea{};
  for (;;) {
    msr.u64 = msr_value;
    g_global_lock.lock();
    ea = address.u32;
    reserved.u64 = *(uint64_t*)RawAddress(base, ea);
    observed.u64 = __builtin_bswap64(reserved.u64);
    cr6.compare<int64_t>(observed.s64, expected.s64, xer);
    if (!cr6.eq) {
      ea = address.u32;
      cr0.lt = 0;
      cr0.gt = 0;
      cr0.eq = reinterpret_cast<std::atomic<uint64_t>*>(RawAddress(base, ea))
                   ->compare_exchange_strong(reserved.u64, __builtin_bswap64(observed.u64), std::memory_order_acq_rel,
                                             std::memory_order_acquire);
      g_global_lock.unlock();
      return;
    }
    ea = address.u32;
    cr0.lt = 0;
    cr0.gt = 0;
    cr0.eq = reinterpret_cast<std::atomic<uint64_t>*>(RawAddress(base, ea))
                 ->compare_exchange_strong(reserved.u64, __builtin_bswap64(desired.u64), std::memory_order_acq_rel,
                                           std::memory_order_acquire);
    g_global_lock.unlock();
    if (cr0.eq) {
      return;
    }
  }
}

// What a section may leave: its registers, cr0, cr6, the reservation and memory around the word.
struct State {
  Register msr, old_reg, new_reg, addend, address, reserved;
  CRRegister cr0, cr6;
  XERRegister xer;
  std::vector<uint64_t> memory;

  bool operator==(const State& other) const {
    return msr.u64 == other.msr.u64 && old_reg.u64 == other.old_reg.u64 && new_reg.u64 == other.new_reg.u64 &&
           addend.u64 == other.addend.u64 && address.u64 == other.address.u64 &&
           reserved.u64 == other.reserved.u64 && cr0.raw() == other.cr0.raw() && cr6.raw() == other.cr6.raw() &&
           xer.so == other.xer.so && xer.ov == other.xer.ov && xer.ca == other.xer.ca && memory == other.memory;
  }
};

State Garbage(uint64_t seed) {
  std::mt19937_64 random(seed);
  State state;
  state.msr.u64 = random();
  state.old_reg.u64 = random();
  state.new_reg.u64 = random();
  state.reserved.u64 = random();
  state.cr0.set_raw(uint32_t(random() & 0xF) | 1);
  state.cr6.set_raw(uint32_t(random() & 0xF));
  state.xer = {uint8_t(random() & 1), uint8_t(random() & 1), uint8_t(random() & 1)};
  return state;
}

const std::vector<uint32_t> kWords32 = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF,
                                        0x12345678, 0x7FFFF, 0x200000};
const std::vector<uint64_t> kWords64 = {0,
                                        1,
                                        0x7FFFFFFFFFFFFFFF,
                                        0x8000000000000000,
                                        0xFFFFFFFFFFFFFFFF,
                                        0x0001000000000001,
                                        0xFFFF000000000000,
                                        0x00000000FFFFFFFF,
                                        0x0000000100000000,
                                        0x40000000000012AC,
                                        0x8000000000000001,
                                        0x0123456789ABCDEF};
const std::vector<uint64_t> kAddends = {0,
                                        1,
                                        uint64_t(int64_t{-1}),
                                        0x7FFFFFFF,
                                        0x80000000,
                                        0xFFFFFFFF,
                                        0x100000001,
                                        0xFFFFFFFF80000000,
                                        0x0001000000000001,
                                        0xFFFF000000000000,
                                        128,
                                        uint64_t(int64_t{-128})};

constexpr uint32_t kWordAddress = 0x1238;

template <typename Word>
State RunAdd(bool native, const State& start, Word word, uint64_t addend, std::optional<int64_t> imm, bool alias,
             bool alias_addend) {
  GuestMemory memory;
  for (size_t i = 0; i < memory.words.size(); ++i) {
    memory.words[i] = 0xA5A5A5A5A5A5A5A5ull ^ i;
  }
  memory.Store<Word>(kWordAddress, word);
  State state = start;
  state.address.u64 = 0xFFFFFFFF00000000ull | kWordAddress;
  state.addend.u64 = imm ? uint64_t(*imm) : addend;
  Register& new_reg = alias ? state.old_reg : (alias_addend ? state.addend : state.new_reg);
  if (native) {
    mua::atomics::AddSection<Word>(memory.base(), kMsr, state.msr, state.old_reg, new_reg, state.addend.u64,
                                   state.address.u32, state.cr0, state.reserved);
  } else {
    const Register addend_copy = state.addend;
    LockedAddSection<Word>(memory.base(), kMsr, state.msr, state.old_reg, new_reg, imm ? nullptr : &addend_copy,
                           imm.value_or(0), state.address, state.cr0, state.reserved);
  }
  state.memory = memory.words;
  return state;
}

void TestAddSectionsMatchLockedSections() {
  std::printf("native add sections leave what the locked sections leave, wraparound included\n");
  uint64_t seed = 1;
  size_t compared = 0;
  for (uint32_t word : kWords32) {
    for (int64_t imm : {int64_t{1}, int64_t{-1}}) {
      const State start = Garbage(seed++);
      CHECK(RunAdd<uint32_t>(true, start, word, 0, imm, true, false) ==
            RunAdd<uint32_t>(false, start, word, 0, imm, true, false));
      ++compared;
    }
    for (uint64_t addend : kAddends) {
      for (bool alias_addend : {false, true}) {
        const State start = Garbage(seed++);
        CHECK(RunAdd<uint32_t>(true, start, word, addend, std::nullopt, false, alias_addend) ==
              RunAdd<uint32_t>(false, start, word, addend, std::nullopt, false, alias_addend));
        ++compared;
      }
    }
  }
  for (uint64_t word : kWords64) {
    for (uint64_t addend : kAddends) {
      const State start = Garbage(seed++);
      CHECK(RunAdd<uint64_t>(true, start, word, addend, std::nullopt, false, false) ==
            RunAdd<uint64_t>(false, start, word, addend, std::nullopt, false, false));
      ++compared;
    }
  }
  std::mt19937_64 random(99);
  for (int i = 0; i < 2000; ++i) {
    const State start = Garbage(seed++);
    const uint64_t word = random();
    const uint64_t addend = random();
    CHECK(RunAdd<uint32_t>(true, start, uint32_t(word), addend, std::nullopt, false, false) ==
          RunAdd<uint32_t>(false, start, uint32_t(word), addend, std::nullopt, false, false));
    CHECK(RunAdd<uint64_t>(true, start, word, addend, std::nullopt, false, false) ==
          RunAdd<uint64_t>(false, start, word, addend, std::nullopt, false, false));
    compared += 2;
  }
  CHECK(compared > 4000);

  // The exact values, not only agreement: 0xFFFFFFFF + 1 wraps in memory and carries into bit 32 of the register.
  GuestMemory memory;
  memory.Store<uint32_t>(kWordAddress, 0xFFFFFFFF);
  Register msr{}, value{}, reserved{};
  reserved.u64 = 0x1122334455667788;
  CRRegister cr0{};
  cr0.so = 1;
  mua::atomics::AddSection<uint32_t>(memory.base(), kMsr, msr, value, value, 1, kWordAddress, cr0, reserved);
  CHECK(memory.Load<uint32_t>(kWordAddress) == 0 && value.u64 == 0x100000000ull && msr.u64 == kMsr);
  CHECK(reserved.u64 == 0x11223344FFFFFFFFull && !cr0.lt && !cr0.gt && cr0.eq && cr0.so);
  memory.Store<uint32_t>(kWordAddress, 0);
  mua::atomics::AddSection<uint32_t>(memory.base(), kMsr, msr, value, value, uint64_t(int64_t{-1}), kWordAddress, cr0,
                                     reserved);
  CHECK(memory.Load<uint32_t>(kWordAddress) == 0xFFFFFFFF && value.s64 == -1);
}

State RunCompareExchange(bool native, const State& start, uint64_t word, uint64_t expected, uint64_t desired) {
  GuestMemory memory;
  for (size_t i = 0; i < memory.words.size(); ++i) {
    memory.words[i] = 0x5A5A5A5A5A5A5A5Aull ^ (i * 3);
  }
  memory.Store<uint64_t>(kWordAddress & ~7u, word);
  State state = start;
  state.address.u64 = kWordAddress & ~7u;
  state.addend.u64 = expected;
  state.new_reg.u64 = desired;
  if (native) {
    mua::atomics::CompareExchangeSection64(memory.base(), kMsr, state.msr, state.old_reg, expected, desired,
                                           state.address.u32, state.cr6, state.cr0, state.xer, state.reserved);
  } else {
    LockedCompareExchangeSection64(memory.base(), kMsr, state.msr, state.old_reg, state.addend, state.new_reg,
                                   state.address, state.cr6, state.cr0, state.xer, state.reserved);
  }
  state.memory = memory.words;
  return state;
}

void TestCompareExchangeMatchesLockedSection() {
  std::printf("native compare-exchange leaves what the locked section leaves, signed cr6 and xer.so included\n");
  uint64_t seed = 1000;
  for (uint64_t word : kWords64) {
    for (uint64_t expected : kWords64) {
      for (uint64_t desired : {uint64_t{0}, uint64_t{0x40000000000012AC}, ~word}) {
        for (uint8_t so : {uint8_t{0}, uint8_t{1}}) {
          State start = Garbage(seed++);
          start.xer.so = so;
          const State native = RunCompareExchange(true, start, word, expected, desired);
          CHECK(native == RunCompareExchange(false, start, word, expected, desired));
          CHECK(native.old_reg.u64 == word && native.cr6.so == so && native.cr6.eq == (word == expected));
          CHECK(native.cr6.lt == (int64_t(word) < int64_t(expected)));
          const uint64_t stored = std::byteswap(native.memory[(kWordAddress & ~7u) / 8]);
          CHECK(stored == (word == expected ? desired : word));
        }
      }
    }
  }
}

// The block claim of sub_822EB4F0 and the bit release of sub_822EB5C8 on one bitmap, threads mixing native and
// locked sections: every block is claimed exactly once and every bit comes back.
void TestBitmapUnderContention() {
  std::printf("contended bitmap claims and releases, native and locked sections mixed\n");
  constexpr uint32_t kWords = 64;
  constexpr int kThreads = 4;
  GuestMemory memory;
  for (uint32_t i = 0; i < kWords; ++i) {
    memory.Store<uint64_t>(0x100 + i * 8, ~uint64_t{0});
  }
  std::vector<std::vector<uint32_t>> claimed(kThreads);
  auto cas = [&](bool native, uint32_t address, uint64_t expected, uint64_t desired) {
    Register msr{}, observed{}, expected_reg{}, desired_reg{}, address_reg{}, reserved{};
    CRRegister cr6{}, cr0{};
    XERRegister xer{};
    expected_reg.u64 = expected;
    desired_reg.u64 = desired;
    address_reg.u64 = address;
    if (native) {
      mua::atomics::CompareExchangeSection64(memory.base(), kMsr, msr, observed, expected, desired, address, cr6, cr0,
                                             xer, reserved);
    } else {
      LockedCompareExchangeSection64(memory.base(), kMsr, msr, observed, expected_reg, desired_reg, address_reg, cr6,
                                     cr0, xer, reserved);
    }
    return cr6.eq;
  };
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      const bool native = t % 2 == 0;
      for (uint32_t i = 0; i < kWords; ++i) {
        const uint32_t address = 0x100 + i * 8;
        for (;;) {
          const uint64_t word = std::byteswap(std::atomic_ref<uint64_t>(memory.words[address / 8]).load());
          if (!word) {
            break;
          }
          const uint32_t bit = 63 - uint32_t(std::countl_zero(word));
          if (cas(native, address, word, word & ~(uint64_t{1} << bit))) {
            claimed[t].push_back(i * 64 + bit);
          }
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  std::vector<uint32_t> all;
  for (const auto& blocks : claimed) {
    all.insert(all.end(), blocks.begin(), blocks.end());
  }
  std::sort(all.begin(), all.end());
  CHECK(all.size() == kWords * 64 && std::adjacent_find(all.begin(), all.end()) == all.end());

  threads.clear();
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      const bool native = t % 2 == 1;
      for (uint32_t block : claimed[t]) {
        const uint32_t address = 0x100 + (block / 64) * 8;
        for (;;) {
          const uint64_t word = std::byteswap(std::atomic_ref<uint64_t>(memory.words[address / 8]).load());
          if (cas(native, address, word, word | (uint64_t{1} << (block % 64)))) {
            break;
          }
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  bool all_back = true;
  for (uint32_t i = 0; i < kWords; ++i) {
    all_back = all_back && memory.Load<uint64_t>(0x100 + i * 8) == ~uint64_t{0};
  }
  CHECK(all_back);
}

// Counters of sub_822EC940, sub_822EC478 and sub_822EBAF0 added from several threads, native and locked mixed: no
// add is lost, at 32 and 64 bits, through wraparound.
void TestCountersUnderContention() {
  std::printf("contended 32- and 64-bit adds, native and locked mixed, lose nothing\n");
  constexpr int kThreads = 4;
  constexpr int kAdds = 50000;
  GuestMemory memory;
  memory.Store<uint32_t>(0x200, 0xFFFFFF00);
  memory.Store<uint64_t>(0x208, 0xFFFFFFFFFFFFFF00);
  std::vector<std::thread> threads;
  std::atomic<uint64_t> added32{0};
  std::atomic<uint64_t> added64{0};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      std::mt19937_64 random(t);
      for (int i = 0; i < kAdds; ++i) {
        const bool native = (i + t) % 2 == 0;
        const uint64_t addend = (random() % 5 == 0) ? uint64_t(int64_t{-1}) : (random() % 3 + 1);
        Register msr{}, old_reg{}, new_reg{}, addend_reg{}, address{}, reserved{};
        CRRegister cr0{};
        addend_reg.u64 = addend;
        address.u64 = 0x200;
        if (native) {
          mua::atomics::AddSection<uint32_t>(memory.base(), kMsr, msr, old_reg, new_reg, addend, 0x200, cr0, reserved);
        } else {
          LockedAddSection<uint32_t>(memory.base(), kMsr, msr, old_reg, new_reg, &addend_reg, 0, address, cr0,
                                     reserved);
        }
        added32 += addend;
        address.u64 = 0x208;
        if (native) {
          mua::atomics::AddSection<uint64_t>(memory.base(), kMsr, msr, old_reg, new_reg, addend, 0x208, cr0, reserved);
        } else {
          LockedAddSection<uint64_t>(memory.base(), kMsr, msr, old_reg, new_reg, &addend_reg, 0, address, cr0,
                                     reserved);
        }
        added64 += addend;
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  CHECK(memory.Load<uint32_t>(0x200) == uint32_t(0xFFFFFF00 + added32.load()));
  CHECK(memory.Load<uint64_t>(0x208) == 0xFFFFFFFFFFFFFF00 + added64.load());
}

// The spin lock of sub_822EB1E0 (compare-exchange 0 to the owner) released as sub_822EC478 does (lwsync, then a plain
// store of 0): it still excludes, and what one holder wrote is what the next one reads.
void TestSpinLockOrdering() {
  std::printf("spin lock taken by native and locked compare-exchange still excludes and orders\n");
  constexpr int kThreads = 4;
  constexpr int kRounds = 20000;
  GuestMemory memory;
  constexpr uint32_t kLock = 0x300;
  constexpr uint32_t kData = 0x340;
  std::vector<std::thread> threads;
  std::atomic<bool> ordered{true};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      const uint64_t owner = 0x4000000000000000ull | uint64_t(t + 1);
      for (int i = 0; i < kRounds; ++i) {
        const bool native = (i + t) % 2 == 0;
        for (;;) {
          Register msr{}, observed{}, expected{}, desired{}, address{}, reserved{};
          CRRegister cr6{}, cr0{};
          XERRegister xer{};
          desired.u64 = owner;
          address.u64 = kLock;
          if (native) {
            mua::atomics::CompareExchangeSection64(memory.base(), kMsr, msr, observed, 0, owner, kLock, cr6, cr0, xer,
                                                   reserved);
          } else {
            LockedCompareExchangeSection64(memory.base(), kMsr, msr, observed, expected, desired, address, cr6, cr0,
                                           xer, reserved);
          }
          if (observed.u64 == 0) {
            break;
          }
          std::this_thread::yield();
        }
        volatile uint64_t* data = reinterpret_cast<volatile uint64_t*>(memory.base() + kData);
        const uint64_t before = data[0];
        if (data[1] != before * 3) {
          ordered = false;
        }
        data[0] = before + 1;
        data[1] = (before + 1) * 3;
        std::atomic_thread_fence(std::memory_order_acq_rel);
        *reinterpret_cast<volatile uint64_t*>(memory.base() + kLock) = 0;
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
  CHECK(ordered.load());
  CHECK(memory.words[kData / 8] == uint64_t(kThreads) * kRounds);
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string result = text.str();
  std::erase(result, '\r');
  return result;
}

struct HookSite {
  uint32_t address = 0;
  std::string name;
  std::vector<std::string> registers;
  uint32_t jump = 0;
  bool after_instruction = false;
};

std::vector<HookSite> ReadHooks(const std::string& config) {
  std::vector<HookSite> hooks;
  size_t at = 0;
  while ((at = config.find("[[midasm_hook]]", at)) != std::string::npos) {
    const size_t end = std::min(config.find("\n[", at + 1), config.size());
    const std::string block = config.substr(at, end - at);
    HookSite hook;
    std::smatch match;
    if (std::regex_search(block, match, std::regex(R"(\naddress = 0x([0-9A-Fa-f]+))"))) {
      hook.address = uint32_t(std::stoul(match[1], nullptr, 16));
    }
    if (std::regex_search(block, match, std::regex(R"re(\nname = "([^"]+)")re"))) {
      hook.name = match[1];
    }
    if (std::regex_search(block, match, std::regex(R"(\nregisters = \[([^\]]*)\])"))) {
      const std::string list = match[1];
      const std::regex quoted(R"re("([^"]+)")re");
      for (std::sregex_iterator it(list.begin(), list.end(), quoted), last; it != last; ++it) {
        hook.registers.push_back((*it)[1]);
      }
    }
    if (std::regex_search(block, match, std::regex(R"(\njump_address_on_true = 0x([0-9A-Fa-f]+))"))) {
      hook.jump = uint32_t(std::stoul(match[1], nullptr, 16));
    }
    hook.after_instruction = block.find("after_instruction = true") != std::string::npos;
    hooks.push_back(hook);
    at = end;
  }
  return hooks;
}

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%08X", value);
  return text;
}

std::string LowerHex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%x", value);
  return text;
}

std::string Mfmsr(const std::string& msr) {
  return "\t// mfmsr " + msr + "\n\tctx." + msr + ".u64 = REX_CHECK_GLOBAL_LOCK();\n";
}

std::string EnterLock() {
  return "\t// mtmsrd r13,1\n\tctx.msr = (ctx.r13.u32 & 0x8020) | (ctx.msr & ~0x8020);\n\tREX_ENTER_GLOBAL_LOCK();\n";
}

std::string LeaveLock(const std::string& msr) {
  return "\t// mtmsrd " + msr + ",1\n\tctx.msr = (ctx." + msr + ".u32 & 0x8020) | (ctx.msr & ~0x8020);\n" +
         "\tREX_LEAVE_GLOBAL_LOCK();\n";
}

std::string LoadReserved(bool wide, const std::string& value, const std::string& address) {
  const std::string bits = wide ? "64" : "32";
  return "\t// " + std::string(wide ? "ldarx " : "lwarx ") + value + ",0," + address + "\n\tea = ctx." + address +
         ".u32;\n\tctx.reserved.u" + bits + " = *(uint" + bits + "_t*)REX_RAW_ADDR(ea);\n\tctx." + value +
         ".u64 = __builtin_bswap" + bits + "(ctx.reserved.u" + bits + ");\n";
}

std::string StoreConditional(bool wide, const std::string& value, const std::string& address) {
  const std::string bits = wide ? "64" : "32";
  return "\t// " + std::string(wide ? "stdcx. " : "stwcx. ") + value + ",0," + address + "\n\tea = ctx." + address +
         ".u32;\n\tctx.cr0.lt = 0;\n\tctx.cr0.gt = 0;\n\tctx.cr0.eq = reinterpret_cast<std::atomic<uint" + bits +
         "_t>*>(REX_RAW_ADDR(ea))->compare_exchange_strong(ctx.reserved.u" + bits + ", __builtin_bswap" + bits +
         "(ctx." + value + ".u" + bits + "), std::memory_order_acq_rel, std::memory_order_acquire);\n";
}

// The generated text of the section a hook replaces, from its mfmsr to the instruction it jumps to, as the reference
// sections above run it.
std::optional<std::string> ExpectedSection(const HookSite& hook) {
  const auto& r = hook.registers;
  const uint32_t a = hook.address;
  const std::string retry = "\t// bne " + LowerHex(a) + "\n\tif (!ctx.cr0.eq) goto loc_" + Hex(a) + ";\n";
  if ((hook.name == "mua_atomic_increment_mua2" || hook.name == "mua_atomic_decrement_mua2") && r.size() == 5 &&
      r[3] == "cr0" && r[4] == "reserved" && hook.jump == a + 28) {
    const std::string imm = hook.name == "mua_atomic_increment_mua2" ? "1" : "-1";
    return Mfmsr(r[0]) + EnterLock() + LoadReserved(false, r[1], r[2]) + "\t// addi " + r[1] + "," + r[1] + "," +
           imm + "\n\tctx." + r[1] + ".s64 = ctx." + r[1] + ".s64 + " + imm + ";\n" +
           StoreConditional(false, r[1], r[2]) + LeaveLock(r[0]) + retry;
  }
  if ((hook.name == "mua_atomic_add32_mua2" || hook.name == "mua_atomic_add64_mua2") && r.size() == 7 &&
      r[5] == "cr0" && r[6] == "reserved" && hook.jump == a + 28) {
    const bool wide = hook.name == "mua_atomic_add64_mua2";
    return Mfmsr(r[0]) + EnterLock() + LoadReserved(wide, r[1], r[4]) + "\t// add " + r[2] + "," + r[3] + "," + r[1] +
           "\n\tctx." + r[2] + ".u64 = ctx." + r[3] + ".u64 + ctx." + r[1] + ".u64;\n" +
           StoreConditional(wide, r[2], r[4]) + LeaveLock(r[0]) + retry;
  }
  if (hook.name == "mua_atomic_cas64_mua2" && r.size() == 9 && r[5] == "cr6" && r[6] == "cr0" && r[7] == "xer" &&
      r[8] == "reserved" && hook.jump == a + 44) {
    const uint32_t keep = a + 36;
    return Mfmsr(r[0]) + EnterLock() + LoadReserved(true, r[1], r[4]) + "\t// cmpd cr6," + r[1] + "," + r[2] +
           "\n\tctx.cr6.compare<int64_t>(ctx." + r[1] + ".s64, ctx." + r[2] + ".s64, ctx.xer);\n\t// bne cr6," +
           LowerHex(keep) + "\n\tif (!ctx.cr6.eq) goto loc_" + Hex(keep) + ";\n" + StoreConditional(true, r[3], r[4]) +
           LeaveLock(r[0]) + retry + "\t// b " + LowerHex(hook.jump) + "\n\tgoto loc_" + Hex(hook.jump) + ";\nloc_" +
           Hex(keep) + ":\n" + StoreConditional(true, r[1], r[4]) + LeaveLock(r[0]);
  }
  return std::nullopt;
}

std::string HookCall(const HookSite& hook) {
  std::string arguments;
  for (const std::string& reg : hook.registers) {
    arguments += (arguments.empty() ? "ctx." : ", ctx.") + reg;
  }
  return "\tif (" + hook.name + "(" + arguments + ")) {\n\t\tgoto loc_" + Hex(hook.jump) + ";\n\t}\n\telse {\n\t}\n";
}

constexpr const char* kHookedFunctions[] = {"822EB1E0", "822EB4F0", "822EB5C8", "822EB6B8",
                                            "822EB7E0", "822EBAF0", "822EC478", "822EC940"};

// Each hook in config/mua2/overrides.toml sits on the mfmsr of the section its name says, with the section's own
// registers, and jumps to the instruction after it; every locked section of the hooked functions has one, and no
// other hook or function hook overlaps them.
void TestHookSitesMatchGeneratedSections() {
#ifdef MUA_GENERATED_ROOT
  std::printf("MUA2 atomic section hooks match the generated sections they skip\n");
  const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / "mua2";
  if (ReadText(module / "marvel_ultimate_alliance_register.cpp").empty()) {
    std::printf("  mua2: module not generated, skipped\n");
    return;
  }
  const std::string config = ReadText(std::filesystem::path(MUA_SRC_DIR) / ".." / "config" / "mua2" / "overrides.toml");
  const std::vector<HookSite> hooks = ReadHooks(config);
  std::map<std::string, std::string> functions;
  for (const auto& entry : std::filesystem::directory_iterator(module)) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    const std::string text = ReadText(entry.path());
    for (const char* name : kHookedFunctions) {
      const std::string head = std::string("DEFINE_REX_FUNC(sub_") + name + ") {\n";
      const size_t start = text.find(head);
      if (start != std::string::npos) {
        functions[name] = text.substr(start, text.find("\n}\n", start) - start);
      }
    }
  }
  CHECK(functions.size() == std::size(kHookedFunctions));

  std::set<uint32_t> addresses;
  size_t atomic_hooks = 0;
  size_t regenerated = 0;
  for (const HookSite& hook : hooks) {
    CHECK(addresses.insert(hook.address).second);
    if (!hook.name.starts_with("mua_atomic_")) {
      continue;
    }
    ++atomic_hooks;
    CHECK(hook.name.ends_with("_mua2") && !hook.after_instruction);
    const std::optional<std::string> expected = ExpectedSection(hook);
    CHECK(expected.has_value());
    if (!expected) {
      continue;
    }
    std::string body;
    for (const auto& [name, text] : functions) {
      if (text.find("loc_" + Hex(hook.address) + ":\n") != std::string::npos) {
        body = text;
      }
    }
    CHECK(!body.empty());
    const size_t label = body.find("loc_" + Hex(hook.address) + ":\n");
    if (label == std::string::npos) {
      std::printf("  no section at 0x%08X\n", hook.address);
      continue;
    }
    std::string section = body.substr(label + Hex(hook.address).size() + 6, expected->size() + 512);
    const std::string mfmsr = "\t// mfmsr " + hook.registers[0] + "\n";
    const std::string call = HookCall(hook);
    if (section.starts_with(mfmsr + call)) {
      section.erase(mfmsr.size(), call.size());
      CHECK(body.find("loc_" + Hex(hook.jump) + ":\n") != std::string::npos);
      ++regenerated;
    }
    const bool matches = section.starts_with(*expected);
    CHECK(matches);
    if (!matches) {
      std::printf("  section at 0x%08X does not match %s\n", hook.address, hook.name.c_str());
    }
    for (const HookSite& other : hooks) {
      CHECK(&other == &hook || other.address < hook.address || other.address >= hook.jump);
    }
  }
  CHECK(atomic_hooks == 21);
  CHECK(regenerated == 0 || regenerated == atomic_hooks);
  if (!regenerated) {
    std::printf("  mua2 not regenerated with the hooks yet: sections checked, hook calls not\n");
  }

  size_t sections = 0;
  for (const auto& [name, text] : functions) {
    for (size_t at = text.find("\t// mfmsr "); at != std::string::npos; at = text.find("\t// mfmsr ", at + 1)) {
      const size_t label = text.rfind("\nloc_", at);
      const uint32_t address = uint32_t(std::stoul(text.substr(label + 5, 8), nullptr, 16));
      CHECK(text.compare(label + 13, 2, ":\n") == 0 && label + 15 == at);
      CHECK(addresses.contains(address));
      ++sections;
    }
  }
  CHECK(sections == atomic_hooks);

  const std::string map = ReadText(std::filesystem::path(MUA_SRC_DIR) / "guest_map_mua2.h");
  for (const char* name : kHookedFunctions) {
    CHECK(map.find(std::string("sub_") + name) == std::string::npos);
  }
#endif
}

// The hooks exist only in the program that carries Marvel Ultimate Alliance 2, behind a cvar that falls back to the
// locked section.
void TestHooksAreMua2Only() {
  std::printf("the atomic section hooks are built for MUA2 only and can fall back to the locked section\n");
  const std::string source = ReadText(std::filesystem::path(MUA_SRC_DIR) / "guest_atomics.cpp");
  CHECK(source.find("#if defined(MUA_RUNTIME_MUA2)") != std::string::npos);
  CHECK(source.find("REXCVAR_DEFINE_BOOL(mua_lockfree_job_atomics, true,") != std::string::npos);
  for (const char* name : {"mua_atomic_increment_mua2", "mua_atomic_decrement_mua2", "mua_atomic_add32_mua2",
                           "mua_atomic_add64_mua2", "mua_atomic_cas64_mua2"}) {
    const size_t at = source.find(std::string("bool ") + name + "(");
    CHECK(at != std::string::npos && source.find("  if (!LockFree()) {\n    return false;\n  }", at) ==
                                         source.find("{\n", at) + 2);
  }
  for (const char* other : {"mua1", "mua1-gold"}) {
    const std::string config =
        ReadText(std::filesystem::path(MUA_SRC_DIR) / ".." / "config" / other / "overrides.toml");
    CHECK(config.find("mua_atomic_") == std::string::npos);
  }
}

}  // namespace

int main() {
  TestAddSectionsMatchLockedSections();
  TestCompareExchangeMatchesLockedSection();
  TestBitmapUnderContention();
  TestCountersUnderContention();
  TestSpinLockOrdering();
  TestHookSitesMatchGeneratedSections();
  TestHooksAreMua2Only();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
