// Exit Game shutdown with the real UI loop, window and input drivers: a non-UI thread asks the idle loop to quit,
// then the teardown runs in ReXApp::OnDestroy's order. Each case runs in a child process, as it can hang or crash.
// Run: mua_shutdown_order_tests [runs]   (Linux needs a display, e.g. Xvfb, or SDL_VIDEO_DRIVER=offscreen)

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <SDL3/SDL_events.h>

#include <rex/input/input_system.h>
#include <rex/logging/api.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>

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

constexpr auto kIdleBeforeQuit = std::chrono::milliseconds(300);
// The longest SDL sleeps without a wake-up (its periodic poll) is 3 s; anything slower waited for input.
constexpr auto kQuitLimit = std::chrono::milliseconds(3500);
constexpr auto kChildLimit = std::chrono::seconds(15);

enum ChildExit { kOk = 0, kSetupFailed = 2, kQuitTooSlow = 3 };

// One Exit Game shutdown. `input_first` is MarvelUltimateAllianceApp::OnShutdown releasing the input system.
int RunChild(bool input_first, bool pad_event) {
  rex::InitLoggingEarly();
  rex::ui::SDLWindowedAppContext app_context;
  if (!app_context.Initialize()) {
    return kSetupFailed;
  }
  auto window = rex::ui::Window::Create(app_context, "mua_shutdown_order_tests", 320, 180);
  if (!window || !window->Open()) {
    return kSetupFailed;
  }
  std::unique_ptr<rex::input::InputSystem> input = rex::input::CreateDefaultInputSystem(false);
  input->Setup();
  input->AttachWindow(window.get());

  // ReXApp's module thread after "Execution complete".
  Clock::time_point quit_requested;
  std::thread module([&] {
    std::this_thread::sleep_for(kIdleBeforeQuit);
    quit_requested = Clock::now();
    app_context.CallInUIThread([&app_context] { app_context.QuitFromUIThread(); });
    if (pad_event) {
      SDL_Event event{};
      event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
      event.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
      event.gbutton.down = true;
      SDL_PushEvent(&event);
    }
  });
  app_context.RunMainMessageLoop();
  const Clock::time_point loop_exited = Clock::now();
  module.join();
  const auto quit_latency = loop_exited - quit_requested;
  std::printf("    quit latency %.1f ms\n", std::chrono::duration<double, std::milli>(quit_latency).count());
  std::fflush(stdout);

  // ReXApp::OnDestroy: OnShutdown, window_.reset(), then runtime_.reset() (Runtime::Shutdown).
  if (input_first) {
    input->Shutdown();
  }
  window.reset();
  input->Shutdown();
  input.reset();
  return quit_latency < kQuitLimit ? kOk : kQuitTooSlow;
}

enum class Outcome { kOk, kHang, kCrash, kFailed };

const char* Describe(Outcome outcome) {
  switch (outcome) {
    case Outcome::kOk:
      return "ok";
    case Outcome::kHang:
      return "HANG (killed)";
    case Outcome::kCrash:
      return "CRASH";
    default:
      return "FAILED";
  }
}

#if defined(_WIN32)
Outcome Spawn(const char* self, const char* order, const char* pad) {
  std::string command = std::string("\"") + self + "\" child " + order + " " + pad;
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
    outcome = code == kOk ? Outcome::kOk : code >= 0xC0000000 ? Outcome::kCrash : Outcome::kFailed;
  }
  CloseHandle(process.hProcess);
  return outcome;
}
#else
Outcome Spawn(const char* self, const char* order, const char* pad) {
  char* argv[] = {const_cast<char*>(self), const_cast<char*>("child"), const_cast<char*>(order),
                  const_cast<char*>(pad), nullptr};
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
  return WIFEXITED(status) && WEXITSTATUS(status) == kOk ? Outcome::kOk : Outcome::kFailed;
}
#endif

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::strcmp(argv[1], "child") == 0) {
    return RunChild(std::strcmp(argv[2], "fixed") == 0, std::strcmp(argv[3], "pad") == 0);
  }
  const int runs = argc > 1 ? std::max(1, std::atoi(argv[1])) : 3;
  int failures = 0;
  for (const char* pad : {"nopad", "pad"}) {
    for (int i = 0; i < runs; ++i) {
      std::printf("fixed order, %s, run %d\n", pad, i + 1);
      std::fflush(stdout);
      const Outcome outcome = Spawn(argv[0], "fixed", pad);
      std::printf("  %s\n", Describe(outcome));
      failures += outcome != Outcome::kOk;
    }
  }
  // Without the fix the input drivers detach from the destroyed window; reported, not counted.
  std::printf("stock order (window destroyed before the input drivers), nopad\n");
  std::fflush(stdout);
  std::printf("  %s\n", Describe(Spawn(argv[0], "stock", "nopad")));
  std::printf("%s: %d of %d fixed-order shutdowns failed\n", failures ? "FAIL" : "PASS", failures, runs * 2);
  return failures ? 1 : 0;
}
