// DEBUG_SAULO: see world_update_profile.h.

#include "world_update_profile.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace mua::profile {

GuestFunctionIndex::GuestFunctionIndex(std::vector<HostFunction> functions) : functions_(std::move(functions)) {
  std::sort(functions_.begin(), functions_.end(), [](const HostFunction& a, const HostFunction& b) {
    return a.start != b.start ? a.start < b.start : a.guest < b.guest;
  });
  // Identical guest functions can share one host body; the lowest guest address names it.
  for (size_t first = 0, i = 1; i < functions_.size(); ++i) {
    if (functions_[i].start != functions_[first].start) {
      first = i;
    } else if (functions_[i].guest != functions_[first].guest) {
      ++folded_[functions_[first].guest];
    }
  }
  functions_.erase(std::unique(functions_.begin(), functions_.end(),
                               [](const HostFunction& a, const HostFunction& b) { return a.start == b.start; }),
                   functions_.end());
}

uint32_t GuestFunctionIndex::Find(uint64_t address, uint64_t function_start) const {
  const auto by_start = [](const HostFunction& function, uint64_t value) { return function.start < value; };
  if (function_start != 0) {
    const auto it = std::lower_bound(functions_.begin(), functions_.end(), function_start, by_start);
    return it != functions_.end() && it->start == function_start ? it->guest : 0;
  }
  auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                             [](uint64_t value, const HostFunction& function) { return value < function.start; });
  if (it == functions_.begin()) {
    return 0;
  }
  --it;
  return it->end == 0 || address < it->end ? it->guest : 0;
}

uint32_t GuestFunctionIndex::Folded(uint32_t guest) const {
  const auto it = folded_.find(guest);
  return it == folded_.end() ? 0 : it->second;
}

uint32_t GuestFunctionIndex::Below(uint64_t address) const {
  auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
                             [](uint64_t value, const HostFunction& function) { return value < function.start; });
  return it == functions_.begin() ? 0 : std::prev(it)->guest;
}

void Profile::Add(std::span<const Frame> frames) {
  ++samples_;
  if (frames.empty()) {
    ++innermost_[size_t(CodeKind::kOther)];
    return;
  }
  ++innermost_[size_t(frames[0].kind)];
  const auto first_guest = std::find_if(frames.begin(), frames.end(), [](const Frame& f) { return f.guest != 0; });
  const uint32_t self = first_guest != frames.end() ? first_guest->guest : 0;
  if (self) {
    ++guest_[self].self;
  }
  if (frames[0].guest == 0) {
    ++host_[frames[0].host];
    ++host_callers_[frames[0].host][self];
  }
  std::array<uint32_t, 256> seen;
  size_t seen_count = 0;
  for (size_t i = 0; i < frames.size(); ++i) {
    const uint32_t guest = frames[i].guest;
    if (!guest || std::find(seen.begin(), seen.begin() + seen_count, guest) != seen.begin() + seen_count) {
      continue;
    }
    if (seen_count < seen.size()) {
      seen[seen_count++] = guest;
    }
    ++guest_[guest].inclusive;
    for (size_t j = i + 1; j < frames.size(); ++j) {
      if (frames[j].guest && frames[j].guest != guest) {
        ++callers_[uint64_t(guest) << 32 | frames[j].guest];
        break;
      }
    }
  }
}

void Profile::Clear() {
  *this = Profile();
}

uint32_t Profile::TopCaller(uint32_t guest) const {
  uint32_t best = 0;
  uint64_t best_count = 0;
  for (const auto& [key, count] : callers_) {
    if (uint32_t(key >> 32) == guest && (count > best_count || (count == best_count && uint32_t(key) < best))) {
      best = uint32_t(key);
      best_count = count;
    }
  }
  return best;
}

uint32_t Profile::TopHostCaller(uint64_t host) const {
  const auto callers = host_callers_.find(host);
  if (callers == host_callers_.end()) {
    return 0;
  }
  uint32_t best = 0;
  uint64_t best_count = 0;
  for (const auto& [caller, count] : callers->second) {
    if (caller && (count > best_count || (count == best_count && caller < best))) {
      best = caller;
      best_count = count;
    }
  }
  return best;
}

std::string Naming::Guest(uint32_t guest) const {
  const uint32_t folded = index ? index->Folded(guest) : 0;
  return folded ? std::format("sub_{:08X}(+{} identical)", guest, folded) : std::format("sub_{:08X}", guest);
}

namespace {

std::string CallerSuffix(uint32_t caller, const Naming& naming) {
  return caller ? " from " + naming.Guest(caller) : std::string();
}

double Percent(uint64_t count, uint64_t total) {
  return total ? 100.0 * double(count) / double(total) : 0.0;
}

std::string InnermostCode(const Profile& profile) {
  const uint64_t total = profile.samples();
  return std::format("guest code {:.1f}%, program host code {:.1f}%, ReXGlue runtime {:.1f}%, system {:.1f}%, other {:.1f}%",
                     Percent(profile.innermost(CodeKind::kGuest), total),
                     Percent(profile.innermost(CodeKind::kProgram), total),
                     Percent(profile.innermost(CodeKind::kRuntime), total),
                     Percent(profile.innermost(CodeKind::kSystem), total),
                     Percent(profile.innermost(CodeKind::kOther), total));
}

using GuestCounts = std::pair<uint32_t, Counts>;

std::vector<GuestCounts> SortedBy(const Profile& profile, uint64_t Counts::* field) {
  std::vector<GuestCounts> sorted(profile.guest().begin(), profile.guest().end());
  std::sort(sorted.begin(), sorted.end(), [field](const GuestCounts& a, const GuestCounts& b) {
    return a.second.*field != b.second.*field ? a.second.*field > b.second.*field : a.first < b.first;
  });
  std::erase_if(sorted, [field](const GuestCounts& entry) { return entry.second.*field == 0; });
  return sorted;
}

std::string HostLine(const Profile& profile, double ms_per_sample, uint64_t total, const Naming& naming,
                     size_t top) {
  std::vector<std::pair<uint64_t, uint64_t>> hosts(profile.host().begin(), profile.host().end());
  std::sort(hosts.begin(), hosts.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  std::string line;
  for (size_t i = 0; i < std::min(top, hosts.size()); ++i) {
    line += std::format("{}{} {:.2f} ms ({:.1f}%){}", i ? "; " : "", naming.host(hosts[i].first),
                        double(hosts[i].second) * ms_per_sample, Percent(hosts[i].second, total),
                        CallerSuffix(profile.TopHostCaller(hosts[i].first), naming));
  }
  return line.empty() ? "none" : line;
}

}  // namespace

std::vector<std::string> ReportWorldUpdate(const Profile& profile, const ReportScale& scale, const Naming& naming,
                                           size_t top) {
  std::vector<std::string> lines;
  const uint64_t total = profile.samples();
  if (!total || !scale.update_samples) {
    return lines;
  }
  const double ms_per_sample = scale.update_ms / double(scale.update_samples);
  lines.push_back(std::format("world update profile: {:.2f} ms per update, {:.1f} updates/s, {} samples | innermost: {}",
                              scale.update_ms, scale.updates_per_s, total, InnermostCode(profile)));

  constexpr double kPathPercent = 90.0;
  std::string path;
  std::vector<uint32_t> on_path;
  for (const auto& [guest, counts] : SortedBy(profile, &Counts::inclusive)) {
    if (Percent(counts.inclusive, total) < kPathPercent) {
      break;
    }
    on_path.push_back(guest);
    path += std::format("{}{} {:.1f}%", path.empty() ? "" : " > ", naming.Guest(guest), Percent(counts.inclusive, total));
  }
  lines.push_back("world update path (>= 90% inclusive): " + (path.empty() ? std::string("none") : path));

  const auto self = SortedBy(profile, &Counts::self);
  for (size_t i = 0; i < std::min(top, self.size()); ++i) {
    const auto& [guest, counts] = self[i];
    lines.push_back(std::format("world update self #{}: {} {:.3f} ms ({:.1f}%), inclusive {:.3f} ms ({:.1f}%){}", i + 1,
                                naming.Guest(guest), double(counts.self) * ms_per_sample, Percent(counts.self, total),
                                double(counts.inclusive) * ms_per_sample, Percent(counts.inclusive, total),
                                CallerSuffix(profile.TopCaller(guest), naming)));
  }

  size_t rank = 0;
  for (const auto& [guest, counts] : SortedBy(profile, &Counts::inclusive)) {
    if (rank == top) {
      break;
    }
    if (std::find(on_path.begin(), on_path.end(), guest) != on_path.end()) {
      continue;
    }
    lines.push_back(std::format("world update inclusive #{}: {} {:.3f} ms ({:.1f}%), self {:.3f} ms{}", ++rank,
                                naming.Guest(guest), double(counts.inclusive) * ms_per_sample,
                                Percent(counts.inclusive, total), double(counts.self) * ms_per_sample,
                                CallerSuffix(profile.TopCaller(guest), naming)));
  }

  lines.push_back("world update innermost host code: " + HostLine(profile, ms_per_sample, total, naming, 6));
  return lines;
}

std::vector<std::string> ReportOutsideWorldUpdate(const Profile& profile, const ReportScale& scale,
                                                  const Naming& naming, size_t top) {
  std::vector<std::string> lines;
  const uint64_t total = profile.samples();
  if (!total || !scale.update_samples) {
    return lines;
  }
  const double ms_per_sample = scale.update_ms / double(scale.update_samples);
  lines.push_back(std::format("same thread outside the world update: {:.2f} ms per update, {} samples | innermost: {}",
                              double(total) * ms_per_sample, total, InnermostCode(profile)));
  std::string self_line;
  const auto self = SortedBy(profile, &Counts::self);
  for (size_t i = 0; i < std::min(top, self.size()); ++i) {
    self_line += std::format("{}{} {:.3f} ms", i ? "; " : "", naming.Guest(self[i].first),
                             double(self[i].second.self) * ms_per_sample);
  }
  lines.push_back("outside world update self: " + (self_line.empty() ? std::string("none") : self_line));
  std::string inclusive_line;
  const auto inclusive = SortedBy(profile, &Counts::inclusive);
  for (size_t i = 0; i < std::min(top, inclusive.size()); ++i) {
    inclusive_line += std::format("{}{} {:.3f} ms{}", i ? "; " : "", naming.Guest(inclusive[i].first),
                                  double(inclusive[i].second.inclusive) * ms_per_sample,
                                  CallerSuffix(profile.TopCaller(inclusive[i].first), naming));
  }
  lines.push_back("outside world update inclusive: " + (inclusive_line.empty() ? std::string("none") : inclusive_line));
  lines.push_back("outside world update innermost host code: " + HostLine(profile, ms_per_sample, total, naming, 4));
  return lines;
}

std::string TopShares(const Profile& profile, uint64_t Counts::* field, const Naming& naming, size_t top) {
  std::string line;
  const auto sorted = SortedBy(profile, field);
  for (size_t i = 0; i < std::min(top, sorted.size()); ++i) {
    line += std::format("{}{} {:.1f}%", i ? "; " : "", naming.Guest(sorted[i].first),
                        Percent(sorted[i].second.*field, profile.samples()));
  }
  return line.empty() ? "none" : line;
}

std::string ReportOtherThreads(const Profile& busy, std::span<const ThreadShare> threads, const Naming& naming,
                               size_t top) {
  uint64_t samples = 0;
  for (const ThreadShare& thread : threads) {
    samples += thread.samples;
  }
  std::vector<ThreadShare> sorted(threads.begin(), threads.end());
  std::sort(sorted.begin(), sorted.end(), [](const ThreadShare& a, const ThreadShare& b) {
    return a.busy != b.busy ? a.busy > b.busy : a.id < b.id;
  });
  std::string line = std::format("other guest threads: {} threads, {} samples, busy in {:.1f}%", threads.size(), samples,
                                 Percent(busy.samples(), samples));
  for (size_t i = 0; i < std::min(top, sorted.size()); ++i) {
    line += std::format("{}t{} {:.0f}% of {}", i ? ", " : " (", sorted[i].id, Percent(sorted[i].busy, sorted[i].samples),
                        sorted[i].samples);
  }
  return line + (sorted.empty() ? "" : ")") + " | busy self: " + TopShares(busy, &Counts::self, naming, top) +
         " | busy inclusive: " + TopShares(busy, &Counts::inclusive, naming, top);
}

bool IsGuestThreadName(std::wstring_view name) {
  // XThread::set_name appends the guest handle: "<name> (F8000028)".
  constexpr size_t kSuffix = 10;
  if (name.size() < kSuffix || name[name.size() - kSuffix] != L'(' || name.back() != L')') {
    return false;
  }
  return std::all_of(name.end() - 9, name.end() - 1, [](wchar_t c) {
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'F');
  });
}

#if defined(_WIN32)
namespace {

constexpr size_t kUnwindSlack = 1024 * 1024;
constexpr uint64_t kMaxStackSize = 256ull * 1024 * 1024;

// A chained unwind entry describes part of a function whose primary entry holds its start.
uint64_t PrimaryStart(uint64_t image_base, const RUNTIME_FUNCTION* entry) {
  constexpr uint8_t kChainInfo = 0x4;
  for (int depth = 0; depth < 32; ++depth) {
    const auto* info = reinterpret_cast<const uint8_t*>(image_base + entry->UnwindData);
    if (!((info[0] >> 3) & kChainInfo)) {
      break;
    }
    const size_t code_slots = (size_t(info[2]) + 1) & ~size_t{1};
    entry = reinterpret_cast<const RUNTIME_FUNCTION*>(info + 4 + code_slots * sizeof(uint16_t));
  }
  return image_base + entry->BeginAddress;
}

}  // namespace

StackSampler::StackSampler() {
  copy_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, kMaxCopy + kUnwindSlack, MEM_RESERVE | MEM_COMMIT,
                                             PAGE_READWRITE));
}

StackSampler::~StackSampler() {
  if (copy_) {
    VirtualFree(copy_, 0, MEM_RELEASE);
  }
}

bool StackSampler::Sample(void* thread, uint64_t& stack_high) {
  count_ = 0;
  copied_ = 0;
  stopped_ns_ = 0;
  if (!copy_) {
    return false;
  }
  CONTEXT context{};
  context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
  const auto stop = std::chrono::steady_clock::now();
  if (SuspendThread(thread) == DWORD(-1)) {
    return false;
  }
  const uint64_t rsp = GetThreadContext(thread, &context) ? context.Rsp : 0;
  if (rsp && !stack_high) {
    // The committed part of a stack is one region ending at the stack base.
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<const void*>(rsp), &info, sizeof(info)) == sizeof(info) &&
        info.State == MEM_COMMIT) {
      stack_high = reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
    }
  }
  if (rsp && rsp < stack_high && stack_high - rsp <= kMaxStackSize) {
    copied_ = size_t(std::min<uint64_t>(stack_high - rsp, kMaxCopy));
    std::memcpy(copy_, reinterpret_cast<const void*>(rsp), copied_);
  }
  ResumeThread(thread);
  stopped_ns_ = uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - stop).count());
  if (!copied_) {
    return false;
  }

  // Pointers into the copied range, in registers and saved on the stack (frame pointers), move with the copy.
  const uint64_t delta = reinterpret_cast<uint64_t>(copy_) - rsp;
  const auto relocate = [&](DWORD64& value) {
    if (value >= rsp && value - rsp < copied_) {
      value += delta;
    }
  };
  for (DWORD64* reg : {&context.Rsp, &context.Rbp, &context.Rbx, &context.Rsi, &context.Rdi, &context.R12,
                       &context.R13, &context.R14, &context.R15}) {
    relocate(*reg);
  }
  for (size_t offset = 0; offset + sizeof(uint64_t) <= copied_; offset += sizeof(uint64_t)) {
    DWORD64 word;
    std::memcpy(&word, copy_ + offset, sizeof(word));
    if (word >= rsp && word - rsp < copied_) {
      word += delta;
      std::memcpy(copy_ + offset, &word, sizeof(word));
    }
  }

  const uint64_t copy_begin = reinterpret_cast<uint64_t>(copy_);
  const uint64_t copy_end = copy_begin + copied_;
  while (count_ < kMaxFrames && context.Rip) {
    // A return address can be the first byte after a call that ends its function.
    const uint64_t lookup = count_ ? context.Rip - 1 : context.Rip;
    DWORD64 image_base = 0;
    PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(lookup, &image_base, nullptr);
    frames_[count_++] = {context.Rip, entry ? PrimaryStart(image_base, entry) : 0};
    if (context.Rsp < copy_begin || context.Rsp + sizeof(uint64_t) > copy_end) {
      break;
    }
    const DWORD64 previous = context.Rsp;
    if (entry) {
      void* handler_data = nullptr;
      DWORD64 establisher = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, entry, &context, &handler_data, &establisher,
                       nullptr);
    } else {
      // A leaf function: the return address is on top of the stack.
      std::memcpy(&context.Rip, reinterpret_cast<const void*>(context.Rsp), sizeof(context.Rip));
      context.Rsp += sizeof(uint64_t);
    }
    if (context.Rsp <= previous || context.Rsp > copy_end) {
      break;
    }
  }
  return count_ > 0;
}

HostModules::HostModules() {
  const auto add = [this](HMODULE module, CodeKind kind) {
    if (!module) {
      return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(module) + dos->e_lfanew);
    const uint64_t start = reinterpret_cast<uint64_t>(module);
    ranges_.push_back({start, start + nt->OptionalHeader.SizeOfImage, kind});
  };
  add(GetModuleHandleW(nullptr), CodeKind::kProgram);
  for (const wchar_t* runtime : {L"rexruntime.dll", L"rexruntimerd.dll", L"rexruntimed.dll"}) {
    add(GetModuleHandleW(runtime), CodeKind::kRuntime);
  }
  for (const wchar_t* system : {L"ntdll.dll", L"kernelbase.dll", L"kernel32.dll", L"win32u.dll"}) {
    add(GetModuleHandleW(system), CodeKind::kSystem);
  }
}

CodeKind HostModules::Classify(uint64_t address) const {
  for (const Range& range : ranges_) {
    if (address >= range.start && address < range.end) {
      return range.kind;
    }
  }
  return CodeKind::kOther;
}

std::string HostModules::Name(uint64_t address) const {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address), &module)) {
    return std::format("{:016X}", address);
  }
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  std::wstring_view file(path, length);
  file = file.substr(file.find_last_of(L"\\/") + 1);
  std::string name;
  for (const wchar_t c : file) {
    name += c < 0x80 ? char(c) : '?';
  }
  return std::format("{}+0x{:X}", name, address - reinterpret_cast<uint64_t>(module));
}
#endif

}  // namespace mua::profile
