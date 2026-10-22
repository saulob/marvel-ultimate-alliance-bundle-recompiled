// D3D12 asynchronous submission (patches/rexglue-sdk/0080-d3d12-async-submission.windows.patch): the
// recorded commands of a submission are replayed, executed and fenced by a dedicated thread instead
// of the GPU command processor thread. The state machine, the recorded-stream ownership (SwapStream),
// the command allocator lifetime it drives and the two call-site predicates are mirrored here; the
// D3D12 calls themselves, which need a device, are inspected in the patch text.
// Run: mua_d3d12_async_submission_tests

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

size_t Find(const std::string& text, const char* needle) {
  return text.find(needle);
}

int Count(const std::string& text, const char* needle) {
  int count = 0;
  const size_t length = std::char_traits<char>::length(needle);
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + length)) {
    ++count;
  }
  return count;
}

// Lines the patch deletes, ignoring the --- headers.
int CountRemovedLines(const std::string& patch) {
  int count = 0;
  for (size_t at = 0; at < patch.size(); ++at) {
    if (patch[at] != '\n' || at + 1 >= patch.size() || patch[at + 1] != '-') {
      continue;
    }
    if (at + 2 < patch.size() && patch[at + 2] == '-') {
      continue;
    }
    ++count;
  }
  return count;
}

using Clock = std::chrono::steady_clock;

// Opened by the test when a job may finish, so an in-flight job can be observed.
class JobGate {
 public:
  void Open() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    cv_.notify_all();
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
  }

  bool open() {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
  }

  void Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return open_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool open_ = true;
};

struct AllocatorPool {
  std::vector<bool> in_use;
  std::vector<uint64_t> last_use;

  uint32_t Acquire() {
    for (size_t i = 0; i < in_use.size(); ++i) {
      if (!in_use[i]) {
        in_use[i] = true;
        return uint32_t(i);
      }
    }
    in_use.push_back(true);
    last_use.push_back(0);
    return uint32_t(in_use.size() - 1);
  }

  void Release(uint32_t allocator, uint64_t submission) { last_use[allocator] = submission; }
};

// One recorded command stream, owned by a single list at a time. The producer's list and the lists
// the submission thread replays are distinct objects, so the mirrors below can see a stream that is
// delivered twice, delivered after reuse or lost entirely.
class RecordedList {
 public:
  RecordedList() : id_(++next_id_) { ++live_; }
  RecordedList(const RecordedList&) = delete;
  RecordedList& operator=(const RecordedList&) = delete;
  ~RecordedList() { --live_; }

  std::vector<uint64_t> commands;
  uint64_t id() const { return id_; }
  void Reset() { commands.clear(); }

  static uint64_t next_id_;
  static int live_;

 private:
  uint64_t id_;
};

uint64_t RecordedList::next_id_ = 0;
int RecordedList::live_ = 0;

// The D3D12 objects the submission thread talks to, with the invariants of the patch checked as the
// jobs run: a submission is executed and fenced exactly once in increasing order, each recorded
// command reaches the target exactly once, an allocator is reset before the replay using it, and it
// is not handed out again before the fence of its last use has been signalled.
class SubmissionTarget {
 public:
  JobGate& gate() { return gate_; }
  int errors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return errors_;
  }
  size_t execution_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return execution_log_.size();
  }
  std::vector<uint64_t> ExecutionLog() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return execution_log_;
  }
  size_t FenceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signalled_.size();
  }
  int max_executing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return max_executing_;
  }
  uint64_t ResetSubmission(uint32_t allocator) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocator < reset_submission_.size() ? reset_submission_[allocator] : 0;
  }
  uint64_t Signalled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_signalled_;
  }
  size_t delivered_commands() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return delivered_commands_;
  }
  std::set<uint64_t> Delivered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return delivered_;
  }

  void ResetAllocator(uint32_t allocator, uint64_t submission) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reset_submission_.size() <= allocator) {
      reset_submission_.resize(allocator + 1, 0);
    }
    if (reset_submission_[allocator] && reset_submission_[allocator] > last_signalled_) {
      Fail("allocator " + std::to_string(allocator) + " reset for submission " +
           std::to_string(submission) + " before its fence");
    }
    reset_submission_[allocator] = submission;
    ++reset_count_;
  }

  void Execute(uint64_t submission, uint32_t allocator, const std::vector<uint64_t>& commands) {
    if (!gate_.open()) {
      gate_.Wait();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!execution_log_.empty() && submission <= execution_log_.back()) {
      Fail("submission " + std::to_string(submission) + " executed after " +
           std::to_string(execution_log_.back()));
    }
    if (executed_.count(submission)) {
      Fail("submission " + std::to_string(submission) + " executed twice");
    }
    if (executed_allocators_.count(allocator)) {
      Fail("allocator " + std::to_string(allocator) + " used by two live submissions");
    }
    for (uint64_t command : commands) {
      if (!delivered_.insert(command).second) {
        Fail("recorded command " + std::to_string(command) + " was delivered twice");
      }
      ++delivered_commands_;
    }
    executed_.insert(submission);
    executed_allocators_.insert(allocator);
    execution_log_.push_back(submission);
    ++executing_;
    if (executing_ > max_executing_) {
      max_executing_ = executing_;
    }
  }

  void FinishExecute(uint32_t allocator) {
    std::lock_guard<std::mutex> lock(mutex_);
    --executing_;
    executed_allocators_.erase(allocator);
  }

  void Signal(uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (value <= last_signalled_) {
      Fail("fence signalled " + std::to_string(value) + " after " +
           std::to_string(last_signalled_));
    }
    last_signalled_ = value;
    signalled_.push_back(value);
  }

  // Mirrors the reclaim loop of CheckSubmissionFence: an allocator becomes writable again only once
  // the fence reaches the submission that last used it, and only after its reset.
  void Reclaim(AllocatorPool& pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < pool.in_use.size(); ++i) {
      if (!pool.in_use[i] || !pool.last_use[i] || pool.last_use[i] > last_signalled_) {
        continue;
      }
      if (i >= reset_submission_.size() || reset_submission_[i] != pool.last_use[i]) {
        Fail("allocator " + std::to_string(i) + " reclaimed before its reset");
      }
      reset_submission_[i] = 0;
      pool.in_use[i] = false;
    }
  }

 private:
  void Fail(const std::string& message) {
    ++errors_;
    std::printf("  FAIL %s\n", message.c_str());
  }

  mutable std::mutex mutex_;
  JobGate gate_;
  std::vector<uint32_t> reset_submission_;
  std::set<uint64_t> executed_;
  std::set<uint32_t> executed_allocators_;
  std::vector<uint64_t> execution_log_;
  std::vector<uint64_t> signalled_;
  std::set<uint64_t> delivered_;
  size_t delivered_commands_ = 0;
  int reset_count_ = 0;
  uint64_t last_signalled_ = 0;
  int executing_ = 0;
  int max_executing_ = 0;
  int errors_ = 0;
};

// Mirrors StartAsyncSubmission, StopAsyncSubmission, EnqueueAsyncSubmission, AwaitAsyncSubmissions
// and AsyncSubmissionThread of the patch. The recorded command stream is modelled as the list
// SwapStream moves it through.
class SubmissionStateMachine {
 public:
  explicit SubmissionStateMachine(SubmissionTarget& target) : target_(target) {}

  // A second start leaves the running worker alone instead of creating a second one.
  bool Start() {
    if (thread_.joinable()) {
      return false;
    }
    stop_ = false;
    busy_ = false;
    thread_ = std::thread([this]() { Worker(); });
    ++workers_started_;
    enabled_ = true;
    return true;
  }

  void Stop() {
    if (!thread_.joinable()) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    work_cv_.notify_one();
    thread_.join();
    enabled_ = false;
    free_lists_.clear();
  }

  // The producer records into its own list; SwapStream hands the recorded commands over on enqueue.
  void Record(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
      producer_commands_.push_back(++next_command_);
      ++produced_;
    }
  }

  void Enqueue(uint32_t allocator, uint64_t submission) {
    std::unique_ptr<RecordedList> commands;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!free_lists_.empty()) {
        commands = std::move(free_lists_.back());
        free_lists_.pop_back();
        ++reused_lists_;
      }
    }
    if (!commands) {
      commands = std::make_unique<RecordedList>();
      ++allocated_lists_;
    }
    commands->commands.swap(producer_commands_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.push_back({std::move(commands), allocator, submission});
    }
    work_cv_.notify_one();
  }

  // Mirrors the branch of EndSubmission: with the thread running the submission is handed over,
  // otherwise it is executed on the caller's thread.
  bool Submit(uint32_t allocator, uint64_t submission) {
    if (enabled_) {
      Enqueue(allocator, submission);
      return true;
    }
    target_.ResetAllocator(allocator, submission);
    target_.Execute(submission, allocator, producer_commands_);
    producer_commands_.clear();
    target_.FinishExecute(allocator);
    target_.Signal(submission);
    return false;
  }

  void AwaitIdle() {
    if (!enabled_) {
      return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    idle_cv_.wait(lock, [this]() { return queue_.empty() && !busy_; });
    ++idle_returns_;
  }

  bool enabled() const { return enabled_; }
  bool joined() const { return joined_; }
  int workers_started() const { return workers_started_; }
  size_t queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }
  size_t free_list_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_lists_.size();
  }
  size_t producer_size() const { return producer_commands_.size(); }
  size_t produced_commands() const { return produced_; }
  int allocated_lists() const { return allocated_lists_; }
  int reused_lists() const { return reused_lists_; }
  int worker_loops() const { return worker_loops_.load(); }
  int idle_returns() const { return idle_returns_; }

 private:
  struct Job {
    std::unique_ptr<RecordedList> commands;
    uint32_t allocator;
    uint64_t submission;
  };

  void Worker() {
    while (true) {
      std::unique_lock<std::mutex> lock(mutex_);
      worker_loops_.fetch_add(1);
      work_cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
      if (queue_.empty()) {
        break;
      }
      Job job = std::move(queue_.front());
      queue_.pop_front();
      busy_ = true;
      lock.unlock();

      RecordedList* commands = job.commands.get();
      target_.ResetAllocator(job.allocator, job.submission);
      target_.Execute(job.submission, job.allocator, commands->commands);
      target_.FinishExecute(job.allocator);
      target_.Signal(job.submission);
      commands->Reset();

      lock.lock();
      free_lists_.push_back(std::move(job.commands));
      busy_ = false;
      if (queue_.empty()) {
        idle_cv_.notify_all();
      }
    }
    joined_ = true;
  }

  SubmissionTarget& target_;
  mutable std::mutex mutex_;
  std::condition_variable work_cv_;
  std::condition_variable idle_cv_;
  std::deque<Job> queue_;
  std::vector<std::unique_ptr<RecordedList>> free_lists_;
  std::vector<uint64_t> producer_commands_;
  std::thread thread_;
  uint64_t next_command_ = 0;
  size_t produced_ = 0;
  bool enabled_ = false;
  bool busy_ = false;
  bool stop_ = false;
  bool joined_ = false;
  int workers_started_ = 0;
  int allocated_lists_ = 0;
  int reused_lists_ = 0;
  int idle_returns_ = 0;
  std::atomic<int> worker_loops_{0};
};

bool WaitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
  const Clock::time_point deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return predicate();
}

// Mirrors the guard at the top of IssueDraw.
bool DrawRequestsBoundary(bool async_enabled, bool submission_open, uint32_t& draws_since,
                          uint32_t draws_per_submission, bool occlusion_query_valid,
                          bool can_end_immediately) {
  if (!(async_enabled && submission_open) || !draws_per_submission) {
    return false;
  }
  ++draws_since;
  return draws_since >= draws_per_submission && !occlusion_query_valid && can_end_immediately;
}

// Mirrors the swap drain added to EndSubmission.
bool SwapDrainsSubmissions(bool is_swap, bool async_enabled) { return is_swap && async_enabled; }

// Mirrors StartAsyncSubmission falling back to synchronous submission when the thread is missing.
bool UseAsyncSubmission(bool cvar_enabled, bool thread_created) {
  return cvar_enabled && thread_created;
}

void TestFifoOrder() {
  std::printf("FIFO submission order\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  const uint32_t kCount = 500;
  for (uint32_t i = 1; i <= kCount; ++i) {
    machine.Submit(0, i);
  }
  machine.AwaitIdle();
  const std::vector<uint64_t> log = target.ExecutionLog();
  CHECK(log.size() == kCount);
  bool in_order = log.size() == kCount;
  for (size_t i = 0; i < log.size(); ++i) {
    in_order = in_order && log[i] == i + 1;
  }
  CHECK(in_order);
  CHECK(target.FenceCount() == kCount);
  machine.Stop();
  CHECK(target.errors() == 0);
}

void TestWorkerSleepsWhenIdle() {
  std::printf("worker sleeps when the queue is empty\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const int loops = machine.worker_loops();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // A worker waiting on the queue wakes only for a submission, not with a spin.
  CHECK(machine.worker_loops() - loops <= 2);
  CHECK(target.execution_count() == 0);
  machine.Stop();
}

void TestEnqueueWakesWorker() {
  std::printf("enqueue wakes the worker\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  machine.Submit(0, 1);
  CHECK(WaitUntil([&]() { return target.execution_count() == 1; }, std::chrono::milliseconds(2000)));
  machine.AwaitIdle();
  CHECK(target.errors() == 0);
  machine.Stop();
}

void TestExactlyOnce() {
  std::printf("every submission is processed exactly once\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  const uint32_t kCount = 200;
  for (uint32_t i = 1; i <= kCount; ++i) {
    machine.Record(2);
    machine.Submit(0, i);
  }
  machine.AwaitIdle();
  std::set<uint64_t> seen;
  for (uint64_t submission : target.ExecutionLog()) {
    seen.insert(submission);
  }
  CHECK(seen.size() == kCount);
  CHECK(target.execution_count() == kCount);
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(target.delivered_commands() == target.Delivered().size());
  machine.Stop();
  CHECK(target.errors() == 0);
}

void TestSwapStreamDeliversExactlyOnce() {
  std::printf("recorded commands are handed over exactly once\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  for (uint64_t submission = 1; submission <= 40; ++submission) {
    machine.Record(uint32_t(submission % 5) + 1);
    machine.Submit(0, submission);
  }
  machine.AwaitIdle();
  CHECK(machine.producer_size() == 0);
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(target.Delivered().size() == machine.produced_commands());
  // The producer may run ahead of the worker, so several lists may be in flight; every list is
  // either reused from the free list or allocated, and none is destroyed while the worker runs.
  CHECK(machine.allocated_lists() + machine.reused_lists() == 40);
  CHECK(RecordedList::live_ == machine.allocated_lists());
  CHECK(machine.free_list_size() == size_t(machine.allocated_lists()));
  machine.Stop();
  // Stopping destroys the free lists, leaving nothing behind.
  CHECK(RecordedList::live_ == 0);
  CHECK(target.errors() == 0);
}

void TestDrainWaitsForInFlightWork() {
  std::printf("drain waits for the queue and the in-flight job\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  target.gate().Close();
  for (uint32_t i = 1; i <= 5; ++i) {
    machine.Submit(0, i);
  }
  std::atomic<bool> drained{false};
  std::thread drainer([&]() {
    machine.AwaitIdle();
    drained.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!drained.load());
  target.gate().Open();
  drainer.join();
  CHECK(drained.load());
  CHECK(target.execution_count() == 5);
  CHECK(machine.queued() == 0);
  CHECK(target.errors() == 0);
  machine.Stop();
}

void TestShutdownDrainsAndStops() {
  std::printf("shutdown drains the queue and joins\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  target.gate().Close();
  const uint32_t kCount = 20;
  for (uint32_t i = 1; i <= kCount; ++i) {
    machine.Submit(0, i);
  }
  std::atomic<bool> stopped{false};
  std::thread stopper([&]() {
    machine.Stop();
    stopped.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!stopped.load());
  target.gate().Open();
  stopper.join();
  CHECK(stopped.load());
  CHECK(machine.joined());
  CHECK(target.execution_count() == kCount);
  CHECK(target.errors() == 0);
  CHECK(!machine.enabled());
}

void TestStopWithEmptyQueue() {
  std::printf("stop with an empty queue joins without work\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  machine.Stop();
  CHECK(machine.joined());
  CHECK(!machine.enabled());
  CHECK(machine.queued() == 0);
  CHECK(machine.free_list_size() == 0);
  CHECK(RecordedList::live_ == 0);
  CHECK(target.execution_count() == 0);
  CHECK(target.errors() == 0);
}

void TestStopWithPendingQueue() {
  std::printf("stop with a pending queue drains it before joining\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  target.gate().Close();
  for (uint64_t submission = 1; submission <= 8; ++submission) {
    machine.Record(3);
    machine.Submit(0, submission);
  }
  std::atomic<bool> stopped{false};
  std::thread stopper([&]() {
    machine.Stop();
    stopped.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  CHECK(!stopped.load());
  target.gate().Open();
  stopper.join();
  CHECK(stopped.load());
  CHECK(machine.joined());
  CHECK(machine.queued() == 0);
  CHECK(target.execution_count() == 8);
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(target.errors() == 0);
}

void TestRepeatedShutdown() {
  std::printf("shutdown repeated is safe\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  machine.Record(1);
  machine.Submit(0, 1);
  machine.AwaitIdle();
  for (int i = 0; i < 5; ++i) {
    machine.Stop();
  }
  CHECK(machine.joined());
  CHECK(!machine.enabled());
  CHECK(machine.queued() == 0);
  CHECK(machine.free_list_size() == 0);
  CHECK(target.execution_count() == 1);
  CHECK(target.errors() == 0);
}

void TestStartIsIdempotent() {
  std::printf("a second start does not create a second worker\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  CHECK(!machine.Start());
  machine.Record(2);
  machine.Submit(0, 1);
  machine.AwaitIdle();
  CHECK(machine.workers_started() == 1);
  // Two workers would replay the same command list concurrently.
  CHECK(target.max_executing() <= 1);
  CHECK(target.execution_count() == 1);
  machine.Stop();
  CHECK(machine.joined());
  CHECK(target.errors() == 0);
}

void TestRollbackToSynchronousPath() {
  std::printf("cvar off and thread creation failure use the synchronous path\n");
  CHECK(!UseAsyncSubmission(false, true));
  CHECK(!UseAsyncSubmission(true, false));
  CHECK(UseAsyncSubmission(true, true));

  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  machine.AwaitIdle();
  machine.Record(4);
  CHECK(!machine.Submit(0, 1));
  CHECK(machine.queued() == 0);
  CHECK(machine.producer_size() == 0);
  CHECK(target.execution_count() == 1);
  CHECK(target.delivered_commands() == 4);
  CHECK(RecordedList::live_ == 0);
  machine.Stop();
}

void TestNoSubmissionAfterShutdown() {
  std::printf("no submission is accepted once shutdown began\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  machine.Stop();
  CHECK(!machine.enabled());
  CHECK(!machine.Submit(0, 1));
  CHECK(machine.queued() == 0);
  CHECK(target.execution_count() == 1);
  machine.Stop();
  CHECK(target.errors() == 0);
}

void TestRestartIsClean() {
  std::printf("start and stop repeat cleanly\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  for (int round = 0; round < 3; ++round) {
    CHECK(machine.Start());
    machine.Record(uint32_t(round) + 1);
    CHECK(machine.Submit(0, uint64_t(round * 10 + 1)));
    machine.AwaitIdle();
    machine.Stop();
    CHECK(machine.joined());
    CHECK(!machine.enabled());
    CHECK(machine.queued() == 0);
    CHECK(machine.free_list_size() == 0);
    CHECK(RecordedList::live_ == 0);
  }
  CHECK(machine.workers_started() == 3);
  CHECK(target.execution_count() == 3);
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(target.errors() == 0);
}

void TestStressStartStop() {
  std::printf("start and stop 1000 times leave no stale state\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  const int kCycles = 1000;
  for (int cycle = 1; cycle <= kCycles; ++cycle) {
    CHECK(machine.Start());
    machine.Record(1);
    CHECK(machine.Submit(0, uint64_t(cycle)));
    machine.AwaitIdle();
    machine.Stop();
    CHECK(machine.joined());
    CHECK(!machine.enabled());
    CHECK(machine.queued() == 0);
    CHECK(machine.free_list_size() == 0);
  }
  CHECK(machine.workers_started() == kCycles);
  CHECK(target.execution_count() == size_t(kCycles));
  CHECK(target.FenceCount() == size_t(kCycles));
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(RecordedList::live_ == 0);
  CHECK(target.errors() == 0);
  CHECK(target.Signalled() == uint64_t(kCycles));
}

void TestStressQueueAndDrain() {
  std::printf("repeated queue and drain with an in-flight job\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  uint64_t submission = 0;
  for (int round = 0; round < 100; ++round) {
    target.gate().Close();
    for (int i = 0; i < 5; ++i) {
      machine.Record(2);
      machine.Submit(uint32_t(i), ++submission);
    }
    CHECK(machine.queued() <= 5);
    target.gate().Open();
    machine.AwaitIdle();
    CHECK(machine.queued() == 0);
    CHECK(machine.enabled());
  }
  machine.Stop();
  CHECK(machine.joined());
  CHECK(target.execution_count() == 500);
  CHECK(target.delivered_commands() == machine.produced_commands());
  CHECK(target.errors() == 0);
}

void TestDrawBoundaryThreshold() {
  std::printf("draw count boundary\n");
  const uint32_t kThreshold = 2000;
  uint32_t draws_since = 0;
  for (uint32_t draw = 1; draw <= kThreshold; ++draw) {
    const bool boundary = DrawRequestsBoundary(true, true, draws_since, kThreshold, false, true);
    CHECK(boundary == (draw == kThreshold));
  }
  CHECK(draws_since == kThreshold);
  draws_since = 0;
  CHECK(!DrawRequestsBoundary(true, true, draws_since, kThreshold, false, true));
  // Zero keeps the boundaries that already existed and does not count draws.
  draws_since = 0;
  for (uint32_t draw = 0; draw < kThreshold * 2; ++draw) {
    CHECK(!DrawRequestsBoundary(true, true, draws_since, 0, false, true));
  }
  CHECK(draws_since == 0);
  // An active occlusion query, a closed submission and unfinished pipeline creation prevent the
  // split.
  draws_since = kThreshold - 1;
  CHECK(!DrawRequestsBoundary(true, true, draws_since, kThreshold, true, true));
  CHECK(!DrawRequestsBoundary(true, true, draws_since, kThreshold, false, false));
  CHECK(!DrawRequestsBoundary(true, false, draws_since, kThreshold, false, true));
  CHECK(!DrawRequestsBoundary(false, true, draws_since, kThreshold, false, true));
}

void TestSwapDrainSemantics() {
  std::printf("swap drain semantics\n");
  CHECK(SwapDrainsSubmissions(true, true));
  CHECK(!SwapDrainsSubmissions(false, true));
  CHECK(!SwapDrainsSubmissions(true, false));
}

void TestAllocatorLifetime() {
  std::printf("allocator lifetime across the queue\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  AllocatorPool pool;
  CHECK(machine.Start());
  target.gate().Close();
  const uint64_t kCount = 32;
  for (uint64_t submission = 1; submission <= kCount; ++submission) {
    const uint32_t allocator = pool.Acquire();
    CHECK(machine.Submit(allocator, submission));
    pool.Release(allocator, submission);
    target.Reclaim(pool);
  }
  // Nothing has been fenced while the first job is held, so no allocator is writable yet.
  CHECK(pool.in_use.size() == kCount);
  target.gate().Open();
  machine.AwaitIdle();
  target.Reclaim(pool);
  size_t free_allocators = 0;
  for (bool in_use : pool.in_use) {
    if (!in_use) {
      ++free_allocators;
    }
  }
  CHECK(free_allocators == kCount);
  // The next round reuses the pool instead of growing it.
  for (uint64_t submission = kCount + 1; submission <= kCount * 2; ++submission) {
    const uint32_t allocator = pool.Acquire();
    CHECK(machine.Submit(allocator, submission));
    pool.Release(allocator, submission);
    target.Reclaim(pool);
  }
  CHECK(pool.in_use.size() == kCount);
  machine.AwaitIdle();
  CHECK(target.execution_count() == kCount * 2);
  machine.Stop();
  CHECK(target.errors() == 0);
}

void TestCommandListReuse() {
  std::printf("recorded command lists are reused after the submission\n");
  SubmissionTarget target;
  SubmissionStateMachine machine(target);
  CHECK(machine.Start());
  for (uint64_t submission = 1; submission <= 50; ++submission) {
    machine.Submit(0, submission);
    machine.AwaitIdle();
  }
  // One list was allocated first; the rest came back from the free list.
  CHECK(machine.allocated_lists() == 1);
  CHECK(machine.reused_lists() == 49);
  CHECK(machine.free_list_size() == 1);
  machine.Stop();
  CHECK(target.errors() == 0);
}

void TestPatchWiring() {
  std::printf("patch wiring\n");
  const std::string patch =
      ReadFile(std::string(MUA_SRC_DIR) +
               "/../patches/rexglue-sdk/0080-d3d12-async-submission.windows.patch");
  CHECK(!patch.empty());

  // The cvars and their defaults.
  CHECK(Has(patch, "REXCVAR_DEFINE_BOOL(d3d12_async_submission, true, \"GPU/D3D12\","));
  CHECK(Has(patch, "REXCVAR_DEFINE_UINT32(d3d12_async_submission_draws, 2000, \"GPU/D3D12\","));
  CHECK(Has(patch, "kRequiresRestart"));

  // The state machine itself.
  CHECK(Has(patch, "struct AsyncSubmission {"));
  CHECK(Has(patch, "#include <condition_variable>"));
  CHECK(Has(patch, "#include <mutex>"));
  CHECK(Has(patch, "std::mutex async_submission_mutex_;"));
  CHECK(Has(patch, "std::condition_variable async_submission_work_cv_;"));
  CHECK(Has(patch, "std::condition_variable async_submission_idle_cv_;"));
  CHECK(Has(patch, "std::deque<AsyncSubmission> async_submission_queue_;"));
  CHECK(
      Has(patch, "std::vector<std::unique_ptr<DeferredCommandList>> async_submission_free_lists_;"));
  CHECK(Has(patch, "bool async_submission_busy_ = false;"));
  CHECK(Has(patch, "bool async_submission_stop_ = false;"));
  CHECK(Has(patch, "uint32_t draws_since_submission_ = 0;"));
  CHECK(Has(patch, "void SwapStream(DeferredCommandList& other) { command_stream_.swap("
                   "other.command_stream_); }"));
  CHECK(Has(patch, "+  async_submission_work_cv_.notify_one();"));
  CHECK(Has(patch, "+  rex::thread::Wait(async_submission_thread_.get(), false);"));

  // The worker is joined before the object's members are destroyed, and a second start cannot create
  // a second worker.
  const size_t destructor = Find(patch, "D3D12CommandProcessor::~D3D12CommandProcessor() {");
  const size_t destructor_stop = patch.find("+  StopAsyncSubmission();", destructor);
  CHECK(destructor != std::string::npos && destructor_stop != std::string::npos);
  CHECK(destructor < destructor_stop);
  CHECK(Has(patch, "+  if (async_submission_thread_) {"));
  CHECK(Has(patch, "+    return;\n+  }\n+  async_submission_stop_ = false;"));

  // Started on setup and stopped on shutdown, after everything queue-side has been awaited.
  CHECK(Has(patch, "if (REXCVAR_GET(d3d12_async_submission)) {"));
  CHECK(Has(patch, "+    StartAsyncSubmission();"));
  const size_t shutdown = Find(patch, "void D3D12CommandProcessor::ShutdownContext() {");
  const size_t await_all = Find(patch, "  AwaitAllQueueOperationsCompletion();");
  const size_t stop = patch.find("+  StopAsyncSubmission();", shutdown);
  const size_t invalidate = Find(patch, "  InvalidateAllVertexBufferResidency();");
  CHECK(shutdown != std::string::npos && await_all != std::string::npos);
  CHECK(stop != std::string::npos && invalidate != std::string::npos);
  CHECK(shutdown < await_all && await_all < stop && stop < invalidate);

  // The worker is the only user of the command list while it runs, and its order is reset, replay,
  // close, execute, fence, recycle.
  const size_t worker = Find(patch, "+void D3D12CommandProcessor::AsyncSubmissionThread() {");
  const size_t reset_allocator = Find(patch, "+    job.command_allocator->Reset();");
  const size_t reset_list = Find(patch, "+    command_list_->Reset(job.command_allocator, nullptr);");
  const size_t execute = Find(patch, "+    job.commands->Execute(command_list_, command_list_1_);");
  const size_t close = Find(patch, "+    command_list_->Close();");
  const size_t execute_lists =
      Find(patch, "+    direct_queue->ExecuteCommandLists(1, execute_command_lists);");
  const size_t signal = Find(patch, "+    direct_queue->Signal(submission_fence_, job.submission);");
  const size_t recycle = Find(patch, "+    job.commands->Reset();");
  CHECK(worker != std::string::npos && reset_allocator != std::string::npos);
  CHECK(reset_list != std::string::npos && execute != std::string::npos);
  CHECK(close != std::string::npos && execute_lists != std::string::npos);
  CHECK(signal != std::string::npos && recycle != std::string::npos);
  CHECK(worker < reset_allocator && reset_allocator < reset_list && reset_list < execute);
  CHECK(execute < close && close < execute_lists && execute_lists < signal && signal < recycle);

  // The fence value only increases on the recording thread; the worker signals it after execution.
  CHECK(Has(patch, "      // The submission thread signals this value once the list has been "
                   "executed."));
  CHECK(Has(patch, "+      ++submission_current_;"));

  // The synchronous path is kept for the cvar being off and for a failed thread creation.
  CHECK(Has(patch, "thread_ = rex::thread::Thread::Create({}, [this]() { AsyncSubmissionThread(); "
                   "});"));
  CHECK(Has(patch, "submitting synchronously"));
  const size_t branch = Find(patch, "+    if (async_submission_enabled_) {");
  const size_t enqueue = Find(patch, "+      EnqueueAsyncSubmission(command_allocator, submission_current_);");
  const size_t sync_reset = Find(patch, "+      command_allocator->Reset();");
  CHECK(branch != std::string::npos && enqueue != std::string::npos && sync_reset != std::string::npos);
  CHECK(branch < enqueue && enqueue < sync_reset);

  // The swap drains everything the frame queued before the presenter submits its own work.
  const size_t end_submission = Find(patch, "bool D3D12CommandProcessor::EndSubmission(bool is_swap) {");
  const size_t drain = Find(patch, "+  if (is_swap && async_submission_enabled_) {");
  const size_t drain_call = Find(patch, "+    AwaitAsyncSubmissions();");
  CHECK(end_submission != std::string::npos && drain != std::string::npos);
  CHECK(drain_call != std::string::npos);
  CHECK(end_submission < drain && drain < drain_call && drain_call < worker);

  // The draw boundary, with the occlusion query and the pipeline creation exclusions.
  CHECK(Has(patch, "  if (async_submission_enabled_ && submission_open_) {"));
  CHECK(Has(patch, "    if (draws_per_submission && ++draws_since_submission_ >= draws_per_submission &&"));
  CHECK(Has(patch, "        !active_occlusion_query_.valid && CanEndSubmissionImmediately()) {"));
  CHECK(Has(patch, "+    draws_since_submission_ = 0;"));

  // Windows only, and nothing removed besides the seven submission lines moved into the else
  // branch and the defaulted destructor that now joins the worker.
  CHECK(!Has(patch, "vulkan"));
  CHECK(!Has(patch, "VK_"));
  CHECK(CountRemovedLines(patch) == 8);

  // No CPU affinity, no telemetry and no progress logs of this optimization.
  CHECK(!Has(patch, "set_affinity_mask"));
  CHECK(!Has(patch, "GetHostThreadAffinityMask"));
  CHECK(!Has(patch, "DEBUG_SAULO"));
  CHECK(Count(patch, "REXGPU_") == 1);
}

}  // namespace

int main() {
  TestFifoOrder();
  TestWorkerSleepsWhenIdle();
  TestEnqueueWakesWorker();
  TestExactlyOnce();
  TestSwapStreamDeliversExactlyOnce();
  TestDrainWaitsForInFlightWork();
  TestShutdownDrainsAndStops();
  TestStopWithEmptyQueue();
  TestStopWithPendingQueue();
  TestRepeatedShutdown();
  TestStartIsIdempotent();
  TestRollbackToSynchronousPath();
  TestNoSubmissionAfterShutdown();
  TestRestartIsClean();
  TestStressStartStop();
  TestStressQueueAndDrain();
  TestDrawBoundaryThreshold();
  TestSwapDrainSemantics();
  TestAllocatorLifetime();
  TestCommandListReuse();
  TestPatchWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
