// Exit Game with guest threads still alive, on the real kernel and runtime: the main guest thread terminates the
// title while a worker sleeps in its 500 ms KeDelayExecutionThread loop (the game's polling worker), one waits on a
// periodic timer that termination cancels and one runs guest code; then the runtime is torn down as ReXApp does
// after "Execution complete". No guest code may run once the main thread has finished. A last case adds a thread that
// never reaches a kernel wait: it cannot be stopped, so the runtime must end the process before tearing anything down.
// Each case runs in a child process, as it can crash.
// Run: mua_title_thread_shutdown_tests [runs]

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include <rex/image_info.h>
#include <rex/kernel/init.h>
#include <rex/logging/api.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/system/xtimer.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

using Clock = std::chrono::steady_clock;
using rex::X_STATUS;
using rex::system::object_ref;
using rex::system::XThread;
using rex::system::XTimer;

constexpr uint32_t kCodeBase = 0x82000000;
constexpr uint32_t kPollerEntry = kCodeBase + 0x100;
constexpr uint32_t kTimerEntry = kCodeBase + 0x200;
constexpr uint32_t kBusyEntry = kCodeBase + 0x300;
constexpr uint32_t kMainEntry = kCodeBase + 0x400;
constexpr uint32_t kRunawayEntry = kCodeBase + 0x500;

// Relative kernel intervals, in 100 ns units.
constexpr int64_t kPollerSleep = -500 * 10000;
constexpr int64_t kShortSleep = -1 * 10000;
constexpr auto kBusyRound = std::chrono::milliseconds(30);
// Longer than the poller's sleep: a poller left running wakes up and runs again within it.
constexpr auto kAfterTeardown = std::chrono::milliseconds(1200);
constexpr auto kChildLimit = std::chrono::seconds(20);

// kRuntimeExit: the runtime ended the process, as a guest thread could not be stopped.
enum ChildExit { kOk = 0, kRuntimeExit = EXIT_FAILURE, kSetupFailed = 2, kLateGuestCode = 3, kNotAlive = 4 };

struct Title {
  rex::Runtime* runtime = nullptr;
  object_ref<XTimer> timer;
  uint32_t scratch = 0;
  std::atomic<bool> poller_asleep{false};
  std::atomic<bool> finished{false};
  std::atomic<bool> teardown{false};
  std::atomic<int> late_rounds{0};
};
Title title;

void Delay(int64_t interval) {
  XThread::GetCurrentThread()->Delay(0, 0, uint64_t(interval));
}

// One round of guest work: guest memory is touched as the game's workers do.
void GuestRound(uint8_t* base, uint32_t slot) {
  if (title.finished.load() && title.late_rounds.fetch_add(1) == 0) {
    std::printf("    guest code ran after the title finished%s\n",
                title.teardown.load() ? ", during or after the runtime teardown" : "");
    std::fflush(stdout);
  }
  auto* word = reinterpret_cast<volatile uint32_t*>(base + title.scratch + slot * 4);
  *word = *word + 1;
}

REX_FUNC(Poller) {
  for (;;) {
    GuestRound(base, 0);
    title.poller_asleep.store(true);
    Delay(kPollerSleep);
  }
}

// The game's 10 ms timer thread: a periodic timer without a routine, which termination cancels.
REX_FUNC(TimerWaiter) {
  object_ref<XTimer> timer = title.timer;
  timer->SetTimer(-10 * 10000, 10, 0, 0, false);
  for (;;) {
    GuestRound(base, 1);
    timer->Wait(0, 0, 0, nullptr);
  }
}

REX_FUNC(Busy) {
  for (;;) {
    GuestRound(base, 2);
    const auto until = Clock::now() + kBusyRound;
    while (Clock::now() < until) {
      auto* word = reinterpret_cast<volatile uint32_t*>(base + title.scratch + 12);
      *word = *word + 1;
    }
    Delay(kShortSleep);
  }
}

REX_FUNC(Runaway) {
  for (;;) {
    GuestRound(base, 3);
  }
}

// Exit Game: XamLoaderLaunchTitle terminates the title on the main thread. The poller has just fallen asleep, so
// its sleep outlasts the termination.
REX_FUNC(Main) {
  while (!title.poller_asleep.load()) {
    Delay(kShortSleep);
  }
  Delay(-20 * 10000);
  title.runtime->kernel_state()->TerminateTitle();
}

const PPCFuncMapping kMappings[] = {{kPollerEntry, Poller}, {kTimerEntry, TimerWaiter}, {kBusyEntry, Busy},
                                   {kMainEntry, Main},     {kRunawayEntry, Runaway},  {0, nullptr}};

object_ref<XThread> Start(rex::system::KernelState* kernel, uint32_t entry, bool main) {
  object_ref<XThread> thread(new XThread(kernel, 64 * 1024, 0, entry, 0, 0, true, main));
  return thread->Create() == X_STATUS_SUCCESS ? thread : object_ref<XThread>();
}

int RunChild(bool runaway) {
  rex::InitLoggingEarly();
  const auto user_data = std::filesystem::temp_directory_path() / "mua_title_thread_shutdown_tests";
  std::filesystem::create_directories(user_data);
  auto runtime = std::make_unique<rex::Runtime>(std::filesystem::path{}, user_data);
  title.runtime = runtime.get();
  rex::RuntimeConfig config;
  config.kernel_init = rex::kernel::InitializeKernel;
  rex::PPCImageInfo image{};
  image.code_base = kCodeBase;
  image.code_size = 0x1000;
  image.image_base = kCodeBase;
  image.image_size = 0x10000;
  image.func_mappings = kMappings;
  if (runtime->Setup(image, std::move(config)) != X_STATUS_SUCCESS) {
    return kSetupFailed;
  }
  auto* kernel = runtime->kernel_state();
  title.scratch = runtime->memory()->SystemHeapAlloc(64);
  title.timer = object_ref<XTimer>(new XTimer(kernel));
  title.timer->Initialize(1);

  auto poller = Start(kernel, kPollerEntry, false);
  auto timer_waiter = Start(kernel, kTimerEntry, false);
  auto busy = Start(kernel, kBusyEntry, false);
  auto runaway_thread = runaway ? Start(kernel, kRunawayEntry, false) : object_ref<XThread>();
  auto main_thread = Start(kernel, kMainEntry, true);
  if (!poller || !timer_waiter || !busy || (runaway && !runaway_thread) || !main_thread) {
    return kSetupFailed;
  }

  // ReXApp's module thread, then ReXApp::OnDestroy resetting the runtime.
  main_thread->Wait(0, 0, 0, nullptr);
  title.finished.store(true);
  const bool poller_alive = poller->is_running();
  std::printf("    title finished; the polling worker is %s\n", poller_alive ? "still asleep" : "gone");
  std::fflush(stdout);
  main_thread.reset();
  poller.reset();
  timer_waiter.reset();
  busy.reset();
  runaway_thread.reset();
  title.timer.reset();

  title.teardown.store(true);
  const auto teardown_start = Clock::now();
  runtime.reset();
  std::printf("    runtime torn down in %.1f ms\n",
              std::chrono::duration<double, std::milli>(Clock::now() - teardown_start).count());
  std::fflush(stdout);
  std::this_thread::sleep_for(kAfterTeardown);

  const int late = title.late_rounds.load();
  std::printf("    guest rounds after the title finished: %d\n", late);
  std::fflush(stdout);
  if (late) {
    return kLateGuestCode;
  }
  return poller_alive ? kOk : kNotAlive;
}

enum class Outcome { kOk, kHang, kCrash, kLate, kFailed };

const char* Describe(Outcome outcome) {
  switch (outcome) {
    case Outcome::kOk:
      return "ok";
    case Outcome::kHang:
      return "HANG (killed)";
    case Outcome::kCrash:
      return "CRASH";
    case Outcome::kLate:
      return "LATE GUEST CODE";
    default:
      return "FAILED";
  }
}

Outcome FromExitCode(unsigned code, unsigned expected) {
  return code == expected ? Outcome::kOk : code == kLateGuestCode ? Outcome::kLate : Outcome::kFailed;
}

#if defined(_WIN32)
Outcome Spawn(const char* self, const char* mode, unsigned expected) {
  std::string command = std::string("\"") + self + "\" " + mode;
  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    return Outcome::kFailed;
  }
  CloseHandle(process.hThread);
  const DWORD limit = DWORD(std::chrono::duration_cast<std::chrono::milliseconds>(kChildLimit).count());
  Outcome outcome;
  if (WaitForSingleObject(process.hProcess, limit) == WAIT_TIMEOUT) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, INFINITE);
    outcome = Outcome::kHang;
  } else {
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    outcome = code >= 0xC0000000 ? Outcome::kCrash : FromExitCode(code, expected);
  }
  CloseHandle(process.hProcess);
  return outcome;
}
#else
Outcome Spawn(const char* self, const char* mode, unsigned expected) {
  char* argv[] = {const_cast<char*>(self), const_cast<char*>(mode), nullptr};
  pid_t pid = 0;
  if (posix_spawn(&pid, self, nullptr, nullptr, argv, environ) != 0) {
    return Outcome::kFailed;
  }
  const auto deadline = Clock::now() + kChildLimit;
  int status = 0;
  while (waitpid(pid, &status, WNOHANG) == 0) {
    if (Clock::now() >= deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      return Outcome::kHang;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (WIFSIGNALED(status)) {
    return Outcome::kCrash;
  }
  return WIFEXITED(status) ? FromExitCode(unsigned(WEXITSTATUS(status)), expected) : Outcome::kFailed;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::strcmp(argv[1], "child") == 0 || std::strcmp(argv[1], "runaway") == 0)) {
    return RunChild(std::strcmp(argv[1], "runaway") == 0);
  }
  const int runs = argc > 1 ? std::max(1, std::atoi(argv[1])) : 5;
  int failures = 0;
  for (int i = 0; i <= runs; ++i) {
    const bool runaway = i == runs;
    if (runaway) {
      std::printf("guest thread that never waits\n");
    } else {
      std::printf("run %d\n", i + 1);
    }
    std::fflush(stdout);
    const Outcome outcome = Spawn(argv[0], runaway ? "runaway" : "child", runaway ? kRuntimeExit : kOk);
    std::printf("  %s\n", Describe(outcome));
    failures += outcome != Outcome::kOk;
  }
  std::printf("%s: %d of %d shutdowns failed\n", failures ? "FAIL" : "PASS", failures, runs + 1);
  return failures ? 1 : 0;
}
