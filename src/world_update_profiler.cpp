// DEBUG_SAULO: samples the guest thread that runs Marvel Ultimate Alliance 2's world update about 500 times a second
// and logs, every 10 s, where its time goes by recompiled function. The thread is found by its stacks, so no hook
// changes; holding it for each sample costs it about 1% of its time.

#if defined(MUA_RUNTIME_MUA2) && defined(_WIN32)

#include <algorithm>
#include <chrono>
#include <format>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// clang-format off
#include <tlhelp32.h>
// clang-format on

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/ppc/func.h>

#include "guest_map.h"
#include "recomp_image.h"
#include "world_update_profile.h"

REXCVAR_DEFINE_BOOL(mua_debug_world_update_profile, true, "Marvel Ultimate Alliance",
                    "DEBUG_SAULO: sample the world update thread and log where its time goes every 10 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(MUA_GUEST_WORLD_UPDATE);
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE));

namespace {

using namespace mua::profile;

constexpr auto kSamplePeriod = std::chrono::microseconds(1500);
constexpr auto kReportPeriod = std::chrono::seconds(10);
constexpr auto kDiscoveryPeriod = std::chrono::milliseconds(500);
constexpr uint64_t kMinReportSamples = 500;
constexpr size_t kOtherThreadEvery = 4;
constexpr auto kOtherThreadsRefresh = std::chrono::seconds(2);
constexpr size_t kTop = 12;
constexpr uint64_t kSlowUpdateSamples = 20;
constexpr int kMaxSlowUpdateReports = 40;

uint64_t HostEnd(uint64_t start) {
  DWORD64 image_base = 0;
  const PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(start, &image_base, nullptr);
  return entry && image_base + entry->BeginAddress == start ? image_base + entry->EndAddress : 0;
}

class FrameMapper {
 public:
  FrameMapper() {
    const uint64_t hook = reinterpret_cast<uint64_t>(&MUA_GUEST_WORLD_UPDATE);
    std::vector<HostFunction> functions;
    for (const PPCFuncMapping* mapping = mua::GetRecompiledImage().info.func_mappings;
         mapping && (mapping->guest || mapping->host); ++mapping) {
      if (!mapping->host) {
        continue;
      }
      const uint64_t start = reinterpret_cast<uint64_t>(mapping->host);
      functions.push_back({start, HostEnd(start), uint32_t(mapping->guest)});
      if (start == hook) {
        world_update_ = uint32_t(mapping->guest);
      }
    }
    // The table names the hook; the original body the hook times is the world update itself.
    original_ = reinterpret_cast<uint64_t>(&MUA_GUEST_ORIGINAL(MUA_GUEST_WORLD_UPDATE));
    functions.push_back({original_, HostEnd(original_), world_update_});
    index_ = GuestFunctionIndex(std::move(functions));
  }

  // Innermost first; returns the position of the world update's original body, or -1 outside it.
  int Map(std::span<const StackFrame> stack, std::vector<Frame>& frames) const {
    frames.clear();
    int root = -1;
    for (size_t i = 0; i < stack.size(); ++i) {
      const uint64_t lookup = i ? stack[i].address - 1 : stack[i].address;
      // Only the program holds guest code: a leaf in a module above it would otherwise land on the last import stub.
      const CodeKind kind = modules_.Classify(lookup);
      const uint32_t guest = kind == CodeKind::kProgram ? index_.Find(lookup, stack[i].function_start) : 0;
      frames.push_back({guest, stack[i].function_start ? stack[i].function_start : lookup,
                        guest ? CodeKind::kGuest : kind});
      if (root < 0 && stack[i].function_start == original_) {
        root = int(i);
      }
    }
    return root;
  }

  std::string NameHost(uint64_t host) const {
    std::string name = modules_.Name(host);
    if (modules_.Classify(host) == CodeKind::kProgram) {
      if (const uint32_t below = index_.Below(host)) {
        name += std::format(" after sub_{:08X}", below);
      }
    }
    return name;
  }

  const GuestFunctionIndex& index() const { return index_; }
  uint32_t world_update() const { return world_update_; }

 private:
  GuestFunctionIndex index_;
  HostModules modules_;
  uint32_t world_update_ = 0;
  uint64_t original_ = 0;
};

struct Target {
  HANDLE handle = nullptr;
  DWORD id = 0;
  uint64_t stack_high = 0;
};

std::wstring Description(HANDLE thread) {
  PWSTR text = nullptr;
  std::wstring description;
  if (SUCCEEDED(GetThreadDescription(thread, &text)) && text) {
    description = text;
    LocalFree(text);
  }
  return description;
}

void Close(std::vector<Target>& targets) {
  for (const Target& target : targets) {
    CloseHandle(target.handle);
  }
  targets.clear();
}

std::vector<Target> GuestThreads(DWORD except) {
  std::vector<Target> threads;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return threads;
  }
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId() ||
        entry.th32ThreadID == except) {
      continue;
    }
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                               FALSE, entry.th32ThreadID);
    if (thread && IsGuestThreadName(Description(thread))) {
      threads.push_back({thread, entry.th32ThreadID, 0});
    } else if (thread) {
      CloseHandle(thread);
    }
  }
  CloseHandle(snapshot);
  return threads;
}

// The guest thread whose stack is inside the world update, taken out of threads.
Target Discover(std::vector<Target>& threads, StackSampler& sampler, const FrameMapper& mapper,
                std::vector<Frame>& frames) {
  for (auto it = threads.begin(); it != threads.end(); ++it) {
    if (sampler.Sample(it->handle, it->stack_high) && mapper.Map(sampler.frames(), frames) >= 0) {
      const Target target = *it;
      threads.erase(it);
      return target;
    }
  }
  return {};
}

std::string Narrow(const std::wstring& text) {
  std::string narrow;
  for (const wchar_t c : text) {
    narrow += c < 0x80 ? char(c) : '?';
  }
  return narrow;
}

class Window {
 public:
  void Start() {
    inside_.Clear();
    outside_.Clear();
    others_.Clear();
    other_threads_.clear();
    held_ns_ = 0;
    copied_ = 0;
    start_ = std::chrono::steady_clock::now();
    update_ns_ = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdateNs);
    updates_ = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdates);
  }

  void Add(std::span<const Frame> frames, int root, const StackSampler& sampler) {
    if (root >= 0) {
      inside_.Add(frames.first(size_t(root) + 1));
    } else {
      outside_.Add(frames);
    }
    held_ns_ += sampler.stopped_ns();
    copied_ += sampler.copied();
  }

  void AddOther(DWORD thread, std::span<const Frame> frames) {
    ThreadShare& share = other_threads_[thread];
    share.id = thread;
    ++share.samples;
    if (!frames.empty() && frames[0].kind != CodeKind::kSystem) {
      ++share.busy;
      others_.Add(frames);
    }
  }

  bool Due() const { return std::chrono::steady_clock::now() - start_ >= kReportPeriod; }

  void Report(const Naming& naming) const {
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    const int64_t updates = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdates) - updates_;
    const int64_t update_ns = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdateNs) - update_ns_;
    if (inside_.samples() < kMinReportSamples || updates <= 0) {
      return;
    }
    const ReportScale scale{double(update_ns) / double(updates) / 1e6, double(updates) / seconds, inside_.samples()};
    for (const std::string& line : ReportWorldUpdate(inside_, scale, naming, kTop)) {
      REXLOG_INFO("DEBUG_SAULO MUA2 {}", line);
    }
    for (const std::string& line : ReportOutsideWorldUpdate(outside_, scale, naming, kTop / 2)) {
      REXLOG_INFO("DEBUG_SAULO MUA2 {}", line);
    }
    std::vector<ThreadShare> threads;
    for (const auto& [id, share] : other_threads_) {
      threads.push_back(share);
    }
    REXLOG_INFO("DEBUG_SAULO MUA2 {}", ReportOtherThreads(others_, threads, naming, kTop / 2));
    const uint64_t samples = inside_.samples() + outside_.samples();
    REXLOG_INFO("DEBUG_SAULO MUA2 world update sampler: {:.0f} samples/s, held the thread {:.1f} us per sample "
                "({:.2f}% of its time), {:.1f} KB of stack copied per sample",
                double(samples) / seconds, double(held_ns_) / 1e3 / double(samples),
                double(held_ns_) / 1e7 / seconds, double(copied_) / 1024.0 / double(samples));
  }

 private:
  Profile inside_;
  Profile outside_;
  Profile others_;
  std::unordered_map<DWORD, ThreadShare> other_threads_;
  uint64_t held_ns_ = 0;
  uint64_t copied_ = 0;
  std::chrono::steady_clock::time_point start_;
  int64_t update_ns_ = 0;
  int64_t updates_ = 0;
};

// The samples of one world update, logged when it ran long enough to be a hitch.
class SlowUpdate {
 public:
  void Add(std::span<const Frame> frames) {
    const auto now = std::chrono::steady_clock::now();
    if (!profile_.samples()) {
      first_ = now;
    }
    last_ = now;
    profile_.Add(frames);
  }

  void Finish(const Naming& naming) {
    if (profile_.samples() >= kSlowUpdateSamples && reports_ < kMaxSlowUpdateReports) {
      ++reports_;
      REXLOG_INFO("DEBUG_SAULO MUA2 slow world update: {} samples over {:.1f} ms | self: {} | inclusive: {}",
                  profile_.samples(), std::chrono::duration<double, std::milli>(last_ - first_).count(),
                  TopShares(profile_, &Counts::self, naming, 6), TopShares(profile_, &Counts::inclusive, naming, 8));
    }
    profile_.Clear();
  }

 private:
  Profile profile_;
  std::chrono::steady_clock::time_point first_;
  std::chrono::steady_clock::time_point last_;
  int reports_ = 0;
};

void Run() {
  SetThreadDescription(GetCurrentThread(), L"DEBUG_SAULO world update profiler");
  // A sample holds the game thread until this thread resumes it.
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  const auto wait = [timer](std::chrono::microseconds period) {
    LARGE_INTEGER due;
    due.QuadPart = -int64_t(period.count()) * 10;
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
    } else {
      Sleep(DWORD(std::max<int64_t>(1, period.count() / 1000)));
    }
  };

  std::this_thread::sleep_for(std::chrono::seconds(2));
  const FrameMapper mapper;
  StackSampler sampler;
  std::vector<Frame> frames;
  frames.reserve(StackSampler::kMaxFrames);
  Target target;
  std::vector<Target> others;
  auto others_listed = std::chrono::steady_clock::time_point();
  size_t tick = 0;
  Window window;
  SlowUpdate slow;
  int64_t updates_seen = 0;
  const Naming naming{[&mapper](uint64_t host) { return mapper.NameHost(host); }, &mapper.index()};
  bool announced = false;
  while (true) {
    if (!REXCVAR_GET(mua_debug_world_update_profile)) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      continue;
    }
    if (!target.handle) {
      wait(kDiscoveryPeriod);
      Close(others);
      others = GuestThreads(0);
      target = Discover(others, sampler, mapper, frames);
      others_listed = std::chrono::steady_clock::now();
      if (target.handle) {
        if (!announced) {
          REXLOG_INFO("DEBUG_SAULO MUA2 world update profiler: sampling thread {} \"{}\" (sub_{:08X}, {} functions)",
                      target.id, Narrow(Description(target.handle)), mapper.world_update(), mapper.index().size());
          announced = true;
        }
        window.Start();
      }
      continue;
    }
    wait(kSamplePeriod);
    if (sampler.Sample(target.handle, target.stack_high)) {
      const int root = mapper.Map(sampler.frames(), frames);
      window.Add(frames, root, sampler);
      // The count goes up as an update ends, so a sample after the change is in the next one.
      if (const int64_t updates = rex::perf::GetCounter(rex::perf::CounterId::kGuestUpdates); updates != updates_seen) {
        slow.Finish(naming);
        updates_seen = updates;
      }
      if (root >= 0) {
        slow.Add(std::span<const Frame>(frames).first(size_t(root) + 1));
      }
    } else if (WaitForSingleObject(target.handle, 0) == WAIT_OBJECT_0) {
      CloseHandle(target.handle);
      target = {};
      continue;
    }
    // Every few ticks one other guest thread, in turn: where the job workers spend the time the world update may wait.
    if (++tick % kOtherThreadEvery == 0 && !others.empty()) {
      Target& other = others[(tick / kOtherThreadEvery) % others.size()];
      if (sampler.Sample(other.handle, other.stack_high)) {
        mapper.Map(sampler.frames(), frames);
        window.AddOther(other.id, frames);
      }
    }
    if (std::chrono::steady_clock::now() - others_listed >= kOtherThreadsRefresh) {
      Close(others);
      others = GuestThreads(target.id);
      others_listed = std::chrono::steady_clock::now();
    }
    if (window.Due()) {
      window.Report(naming);
      window.Start();
    }
  }
}

[[maybe_unused]] const bool g_started = [] {
  std::thread(Run).detach();
  return true;
}();

}  // namespace

#endif
