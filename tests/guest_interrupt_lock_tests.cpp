// The guest interrupt lock of patches/rexglue-sdk/0155 and 0157, on the patched runtime: mfmsr reads only the calling
// thread's state and never waits for another thread's section, while mtmsrd sections still exclude each other and host
// code holding the global critical region, also past the time a waiting section polls before it blocks. The polled
// acquire 0158 gives write-watch faults excludes them the same way.
// Run: mua_guest_interrupt_lock_tests

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <rex/thread/global_critical_region_polling.h>
#include <rex/thread/mutex.h>

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

using rex::thread::AcquireGlobalCriticalRegionPolling;
using rex::thread::EnterGuestInterruptLock;
using rex::thread::GuestInterruptMsr;
using rex::thread::LeaveGuestInterruptLock;

constexpr uint64_t kInterruptsEnabled = 0x8000;
constexpr auto kBlockedFor = std::chrono::milliseconds(50);
constexpr auto kTimeout = std::chrono::seconds(5);

bool WaitFor(const std::atomic<bool>& flag) {
  const auto deadline = std::chrono::steady_clock::now() + kTimeout;
  while (!flag.load()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

void TestNestedSections() {
  std::printf("mfmsr reports interrupts disabled inside a section, nested sections included\n");
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
  EnterGuestInterruptLock();
  CHECK(GuestInterruptMsr() == 0);
  EnterGuestInterruptLock();
  LeaveGuestInterruptLock();
  CHECK(GuestInterruptMsr() == 0);
  LeaveGuestInterruptLock();
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
}

void TestOtherThreadsSection() {
  std::printf("another thread's section neither changes nor blocks mfmsr, and still excludes this thread's section\n");
  std::atomic<bool> holding{false};
  std::atomic<bool> release{false};
  std::thread holder([&] {
    EnterGuestInterruptLock();
    holding = true;
    while (!release.load()) {
      std::this_thread::yield();
    }
    LeaveGuestInterruptLock();
  });
  CHECK(WaitFor(holding));
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);

  std::atomic<bool> entered{false};
  std::thread waiter([&] {
    EnterGuestInterruptLock();
    entered = true;
    LeaveGuestInterruptLock();
  });
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!entered.load());
  release = true;
  CHECK(WaitFor(entered));
  holder.join();
  waiter.join();
}

void TestHostCriticalRegion() {
  std::printf("guest sections and host code holding the global critical region exclude each other\n");
  auto host = rex::thread::global_critical_region::AcquireDirect();
  std::atomic<bool> entered{false};
  std::thread guest([&] {
    EnterGuestInterruptLock();
    entered = true;
    LeaveGuestInterruptLock();
  });
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!entered.load());
  host.unlock();
  CHECK(WaitFor(entered));
  guest.join();

  EnterGuestInterruptLock();
  bool host_acquired = true;
  std::thread other_host([&] { host_acquired = rex::thread::global_critical_region{}.TryAcquire().owns_lock(); });
  other_host.join();
  CHECK(!host_acquired);
  LeaveGuestInterruptLock();
}

// A host hold far longer than the polling: the waiting section blocks and still enters only after the host leaves.
void TestLongHostHold() {
  std::printf("a section waiting past its polling still waits for the host to leave\n");
  auto host = rex::thread::global_critical_region::AcquireDirect();
  std::atomic<bool> entered{false};
  std::thread guest([&] {
    EnterGuestInterruptLock();
    entered = true;
    LeaveGuestInterruptLock();
  });
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!entered.load());
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!entered.load());
  host.unlock();
  CHECK(WaitFor(entered));
  guest.join();
}

// Plain read-modify-writes inside sections from several threads, with host holds in between: none is lost, and a
// later section sees what an earlier one wrote.
void TestContendedSectionsExclude() {
  std::printf("contended sections exclude each other and order their writes\n");
  constexpr int kThreads = 4;
  constexpr int kSections = 20000;
  uint64_t counter = 0;
  uint64_t last_writer_value = 0;
  uint64_t host_increments = 0;
  uint64_t fault_increments = 0;
  bool ordered = true;
  std::atomic<bool> stop_host{false};
  std::thread host([&] {
    while (!stop_host.load()) {
      auto lock = rex::thread::global_critical_region::AcquireDirect();
      const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(30);
      while (std::chrono::steady_clock::now() < until) {
      }
      ++counter;
      ++host_increments;
    }
  });
  std::thread fault([&] {
    while (!stop_host.load()) {
      auto lock = AcquireGlobalCriticalRegionPolling();
      ++counter;
      ++fault_increments;
    }
  });
  std::vector<std::thread> guests;
  for (int t = 0; t < kThreads; ++t) {
    guests.emplace_back([&] {
      for (int i = 0; i < kSections; ++i) {
        EnterGuestInterruptLock();
        if (counter < last_writer_value) {
          ordered = false;
        }
        last_writer_value = ++counter;
        LeaveGuestInterruptLock();
      }
    });
  }
  for (std::thread& guest : guests) {
    guest.join();
  }
  stop_host = true;
  host.join();
  fault.join();
  CHECK(host_increments > 0 && fault_increments > 0);
  CHECK(counter == uint64_t(kThreads) * kSections + host_increments + fault_increments);
  CHECK(ordered);
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
}

// Host code that holds the region runs a guest section, as the GPU interrupt dispatch does: no self-deadlock, mfmsr
// reports interrupts disabled only inside the section, and the region stays the host's until it leaves.
void TestSectionInsideHostRegion() {
  std::printf("a section run by a thread already holding the region\n");
  auto host = rex::thread::global_critical_region::AcquireDirect();
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
  EnterGuestInterruptLock();
  CHECK(GuestInterruptMsr() == 0);
  LeaveGuestInterruptLock();
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
  bool other_acquired = true;
  std::thread other([&] { other_acquired = rex::thread::global_critical_region{}.TryAcquire().owns_lock(); });
  other.join();
  CHECK(!other_acquired);
  host.unlock();
}

// A write-watch fault takes the region polled: it waits out a host hold far longer than the polling, keeps sections
// and host code out while it holds it, and nests in a section of the faulting thread without leaving it.
void TestPollingAcquire() {
  std::printf("the polled acquire of write-watch faults excludes sections and host holds, and nests in a section\n");
  auto host = rex::thread::global_critical_region::AcquireDirect();
  std::atomic<bool> acquired{false};
  std::thread fault([&] {
    auto lock = AcquireGlobalCriticalRegionPolling();
    acquired = lock.owns_lock();
  });
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!acquired.load());
  host.unlock();
  CHECK(WaitFor(acquired));
  fault.join();

  auto lock = AcquireGlobalCriticalRegionPolling();
  CHECK(lock.owns_lock());
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
  std::atomic<bool> entered{false};
  std::thread guest([&] {
    EnterGuestInterruptLock();
    entered = true;
    LeaveGuestInterruptLock();
  });
  bool host_acquired = true;
  std::thread other_host([&] { host_acquired = rex::thread::global_critical_region{}.TryAcquire().owns_lock(); });
  other_host.join();
  CHECK(!host_acquired);
  std::this_thread::sleep_for(kBlockedFor);
  CHECK(!entered.load());
  lock.unlock();
  CHECK(WaitFor(entered));
  guest.join();

  EnterGuestInterruptLock();
  {
    auto nested = AcquireGlobalCriticalRegionPolling();
    CHECK(nested.owns_lock());
    CHECK(GuestInterruptMsr() == 0);
  }
  CHECK(GuestInterruptMsr() == 0);
  bool other_acquired = true;
  std::thread other([&] { other_acquired = rex::thread::global_critical_region{}.TryAcquire().owns_lock(); });
  other.join();
  CHECK(!other_acquired);
  LeaveGuestInterruptLock();
  CHECK(GuestInterruptMsr() == kInterruptsEnabled);
}

// A thread that ran sections and exited leaves the region free and no interrupt state behind for a new thread.
void TestThreadExit() {
  std::printf("an exited thread leaves no interrupt state or hold behind\n");
  std::thread first([] {
    EnterGuestInterruptLock();
    EnterGuestInterruptLock();
    LeaveGuestInterruptLock();
    LeaveGuestInterruptLock();
  });
  first.join();
  uint64_t second_msr = 0;
  bool second_entered = false;
  std::thread second([&] {
    second_msr = GuestInterruptMsr();
    second_entered = rex::thread::global_critical_region{}.TryAcquire().owns_lock();
  });
  second.join();
  CHECK(second_msr == kInterruptsEnabled);
  CHECK(second_entered);
}

}  // namespace

int main() {
  TestNestedSections();
  TestOtherThreadsSection();
  TestHostCriticalRegion();
  TestLongHostHold();
  TestContendedSectionsExclude();
  TestSectionInsideHostRegion();
  TestPollingAcquire();
  TestThreadExit();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
