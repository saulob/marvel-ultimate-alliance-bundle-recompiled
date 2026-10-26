#pragma once

// DEBUG_SAULO: a sampling profile of the guest thread that runs the world update, by recompiled guest function.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mua::profile {

// The host code of one recompiled function; end is 0 when no unwind entry covers it (a leaf).
struct HostFunction {
  uint64_t start = 0;
  uint64_t end = 0;
  uint32_t guest = 0;
};

class GuestFunctionIndex {
 public:
  GuestFunctionIndex() = default;
  explicit GuestFunctionIndex(std::vector<HostFunction> functions);

  // function_start is the start of the unwind entry covering address, or 0 when none does. 0 for host code.
  uint32_t Find(uint64_t address, uint64_t function_start) const;
  // The guest function whose host code starts closest below address, or 0.
  uint32_t Below(uint64_t address) const;
  // How many other guest functions share guest's host code (identical code folded by the linker).
  uint32_t Folded(uint32_t guest) const;
  size_t size() const { return functions_.size(); }

 private:
  std::vector<HostFunction> functions_;
  std::unordered_map<uint32_t, uint32_t> folded_;
};

enum class CodeKind : uint8_t { kGuest, kProgram, kRuntime, kSystem, kOther, kCount };

struct Frame {
  uint32_t guest = 0;  // 0 for host code
  uint64_t host = 0;   // start of the host function, or the address when no unwind entry covers it
  CodeKind kind = CodeKind::kOther;
};

struct Counts {
  uint64_t self = 0;
  uint64_t inclusive = 0;
};

// Sample counts of one report window. A guest function's self count includes the host code it calls.
class Profile {
 public:
  // frames: innermost first.
  void Add(std::span<const Frame> frames);
  void Clear();

  uint64_t samples() const { return samples_; }
  uint64_t innermost(CodeKind kind) const { return innermost_[size_t(kind)]; }
  const std::unordered_map<uint32_t, Counts>& guest() const { return guest_; }
  const std::unordered_map<uint64_t, uint64_t>& host() const { return host_; }
  // The guest function that most often called guest (or, with host set, the host function), 0 when none.
  uint32_t TopCaller(uint32_t guest) const;
  uint32_t TopHostCaller(uint64_t host) const;

 private:
  uint64_t samples_ = 0;
  std::array<uint64_t, size_t(CodeKind::kCount)> innermost_{};
  std::unordered_map<uint32_t, Counts> guest_;
  std::unordered_map<uint64_t, uint64_t> callers_;  // guest << 32 | caller
  std::unordered_map<uint64_t, uint64_t> host_;
  std::unordered_map<uint64_t, std::unordered_map<uint32_t, uint64_t>> host_callers_;
};

struct ReportScale {
  double update_ms = 0.0;      // average world update
  double updates_per_s = 0.0;
  uint64_t update_samples = 0; // samples taken inside the world update
};

struct Naming {
  std::function<std::string(uint64_t host)> host;
  const GuestFunctionIndex* index = nullptr;

  std::string Guest(uint32_t guest) const;
};

// The lines logged every report window: inside the world update (profile) and on the same thread outside it.
std::vector<std::string> ReportWorldUpdate(const Profile& profile, const ReportScale& scale, const Naming& naming,
                                           size_t top);
std::vector<std::string> ReportOutsideWorldUpdate(const Profile& profile, const ReportScale& scale,
                                                  const Naming& naming, size_t top);

struct ThreadShare {
  uint32_t id = 0;
  uint64_t samples = 0;
  uint64_t busy = 0;  // innermost frame outside the system DLLs, where a waiting thread sits
};

// "sub_X 12.3%; sub_Y 4.5%": the top functions by self or inclusive samples, as shares of all samples.
std::string TopShares(const Profile& profile, uint64_t Counts::* field, const Naming& naming, size_t top);

// One line for the other guest threads: busy shares per thread and busy samples by function.
std::string ReportOtherThreads(const Profile& busy, std::span<const ThreadShare> threads, const Naming& naming,
                               size_t top);

// The host threads that run guest code carry the guest handle in their description.
bool IsGuestThreadName(std::wstring_view name);

#if defined(_WIN32)
// The loaded modules a host frame can belong to.
class HostModules {
 public:
  HostModules();
  CodeKind Classify(uint64_t address) const;
  std::string Name(uint64_t address) const;  // module+0xOFFSET

 private:
  struct Range {
    uint64_t start;
    uint64_t end;
    CodeKind kind;
  };
  std::vector<Range> ranges_;
};

struct StackFrame {
  uint64_t address = 0;         // the instruction pointer, then return addresses
  uint64_t function_start = 0;  // 0 when no unwind entry covers the address
};

// A copy of a stopped thread's stack, unwound after the thread runs again: nothing it may hold (the loader or heap
// lock) is needed while it is stopped.
class StackSampler {
 public:
  static constexpr size_t kMaxCopy = 256 * 1024;
  static constexpr size_t kMaxFrames = 128;

  StackSampler();
  ~StackSampler();
  StackSampler(const StackSampler&) = delete;
  StackSampler& operator=(const StackSampler&) = delete;

  // thread needs THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT; stack_high is the top of its stack, 0 to find it.
  bool Sample(void* thread, uint64_t& stack_high);
  std::span<const StackFrame> frames() const { return {frames_.data(), count_}; }
  uint64_t stopped_ns() const { return stopped_ns_; }
  size_t copied() const { return copied_; }

 private:
  uint8_t* copy_ = nullptr;
  std::array<StackFrame, kMaxFrames> frames_{};
  size_t count_ = 0;
  uint64_t stopped_ns_ = 0;
  size_t copied_ = 0;
};
#endif

}  // namespace mua::profile
