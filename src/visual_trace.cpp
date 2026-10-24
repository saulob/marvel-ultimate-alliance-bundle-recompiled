// DEBUG_SAULO: temporary F9 transition recorder. Telemetry only; remove after review.

#include "visual_trace.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include <rex/logging.h>

namespace mua::visual_trace {
namespace {

constexpr double kHistorySeconds = 5.0;
constexpr double kKeepSeconds = 6.0;
constexpr size_t kMaxEvents = 256;

struct Entry {
  double time;
  uint64_t present;
  std::string text;
};

using Clock = std::chrono::steady_clock;

const Clock::time_point g_start = Clock::now();
std::atomic<uint64_t> g_presents{0};
std::mutex g_mutex;
std::deque<Entry> g_entries;
std::function<std::string()> g_state_provider;
uint32_t g_marker_id = 0;

double Now() { return std::chrono::duration<double>(Clock::now() - g_start).count(); }

void Push(const std::string& text) {
  const double now = Now();
  std::lock_guard lock(g_mutex);
  g_entries.push_back({now, g_presents.load(std::memory_order_relaxed), text});
  while (!g_entries.empty() && (now - g_entries.front().time > kKeepSeconds || g_entries.size() > kMaxEvents)) {
    g_entries.pop_front();
  }
}

}  // namespace

void Event(const char* kind, const std::string& detail) {
  Push(detail.empty() ? std::string(kind) : std::string(kind) + " " + detail);
}

void NotePresent() { g_presents.fetch_add(1, std::memory_order_relaxed); }

void SetStateProvider(std::function<std::string()> provider) {
  std::lock_guard lock(g_mutex);
  g_state_provider = std::move(provider);
}

void Marker() {
  const double now = Now();
  const uint64_t presents = g_presents.load(std::memory_order_relaxed);
  std::function<std::string()> provider;
  std::vector<Entry> history;
  uint32_t id;
  {
    std::lock_guard lock(g_mutex);
    id = ++g_marker_id;
    provider = g_state_provider;
    for (const Entry& entry : g_entries) {
      if (now - entry.time <= kHistorySeconds) {
        history.push_back(entry);
      }
    }
  }
  const std::string state = provider ? provider() : std::string("unavailable");

  REXLOG_INFO("DEBUG_SAULO visual marker: id={} now={:.3f} present={} events={}", id, now, presents, history.size());
  for (const Entry& entry : history) {
    REXLOG_INFO("DEBUG_SAULO visual history: id={} age={:.3f} t={:.3f} present={} event={}", id, now - entry.time,
                entry.time, entry.present, entry.text);
  }
  REXLOG_INFO("DEBUG_SAULO visual history: id={} age=0.000 t={:.3f} present={} event=state_now {}", id, now, presents,
              state);
  Push("marker id=" + std::to_string(id));
}

}  // namespace mua::visual_trace
