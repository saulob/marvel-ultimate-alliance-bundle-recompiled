// Stress test for the ReXGlue physical heap release order (see docs/rexglue-runtime-patch.md).
// Several threads allocate and release physical memory at once; with the stock v0.10.0 order,
// some releases fail because another thread's allocation lost its page-table entry.
// Run: mua_physical_heap_stress   (exit code 0 = no failed release)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

namespace {

constexpr uint32_t kThreads = 8;
constexpr uint32_t kIterations = 20000;
constexpr uint32_t kPhysicalViews[] = {0xA0000000, 0xE0000000};

struct Result {
  uint32_t alloc_failures = 0;
  uint32_t release_failures = 0;
};

Result Hammer(rex::memory::BaseHeap* heap) {
  std::atomic<uint32_t> alloc_failures{0};
  std::atomic<uint32_t> release_failures{0};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;
  for (uint32_t t = 0; t < kThreads; ++t) {
    threads.emplace_back([&] {
      while (!go.load(std::memory_order_acquire)) {
      }
      for (uint32_t i = 0; i < kIterations; ++i) {
        uint32_t address = 0;
        if (!heap->Alloc(
                4096, 4096,
                rex::memory::kMemoryAllocationReserve | rex::memory::kMemoryAllocationCommit,
                rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite, false,
                &address)) {
          ++alloc_failures;
          continue;
        }
        if (!heap->Release(address, nullptr)) {
          ++release_failures;
        }
      }
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& thread : threads) {
    thread.join();
  }
  return {alloc_failures.load(), release_failures.load()};
}

}  // namespace

int main() {
  rex::InitLogging();
  rex::memory::Memory memory;
  if (!memory.Initialize()) {
    std::printf("Memory::Initialize failed\n");
    return 2;
  }

  bool ok = true;
  for (uint32_t base : kPhysicalViews) {
    auto* heap = const_cast<rex::memory::BaseHeap*>(memory.LookupHeap(base));
    if (!heap) {
      std::printf("no heap at %08X\n", base);
      return 2;
    }
    Result result = Hammer(heap);
    std::printf("view %08X: %u failed allocs, %u failed releases (%u threads x %u)\n", base,
                result.alloc_failures, result.release_failures, kThreads, kIterations);
    ok = ok && result.alloc_failures == 0 && result.release_failures == 0;
  }
  std::printf(ok ? "PASS\n" : "FAIL\n");
  return ok ? 0 : 1;
}
