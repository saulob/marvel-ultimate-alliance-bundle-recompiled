// Title shutdown on MUA1 and Gold: the worker that self-suspends (entry 0x8260E580 / Gold sub_82628D60) is covered by the
// self-suspend wait patch, the stop safeguards are untouched, and Exit Game still ends the title through
// XamLoaderTerminateTitle.
// Run: mua_title_shutdown_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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

const std::filesystem::path kSrcDir = MUA_SRC_DIR;
const std::filesystem::path kPatches = kSrcDir / ".." / "patches" / "rexglue-sdk";

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::string FunctionBody(const std::filesystem::path& module, const std::string& name) {
  const std::string marker = "DEFINE_REX_FUNC(" + name + ") {";
  for (const auto& entry : std::filesystem::directory_iterator(module)) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    const std::string text = ReadFile(entry.path());
    const size_t start = text.find(marker);
    if (start != std::string::npos) {
      return text.substr(start, text.find("\n}\n", start) - start);
    }
  }
  return {};
}

void TestSelfSuspendPatch() {
  std::printf("self-suspend wait patch\n");
  const std::string patch = ReadFile(kPatches / "0129-self-suspend-guest-wait.patch");
  CHECK(!patch.empty());
  CHECK(Has(patch, "diff --git a/src/system/xthread.cpp b/src/system/xthread.cpp"));
  CHECK(Has(patch, "+  if (IsInThread(this)) {"));
  CHECK(Has(patch, "+    GuestWait wait;"));
  CHECK(Has(patch, "+    return thread_->Suspend(&unused_host_suspend_count)"));
  CHECK(!Has(patch, "diff --git a/src/system/runtime.cpp"));
  CHECK(!Has(patch, "diff --git a/src/system/kernel_state.cpp"));
  for (const char* forbidden : {"StopTitleThreads", "kStopTimeout", "_Exit", "TerminateThread", "ExitProcess",
                                "TerminateProcess", "sleep_for", "DEBUG_SAULO"}) {
    CHECK(!Has(patch, forbidden));
  }
}

void TestStopSafeguardsUntouched() {
  std::printf("stop safeguards untouched\n");
  const std::string stop = ReadFile(kPatches / "0033-stop-title-threads.patch");
  CHECK(Has(stop, "constexpr auto kStopTimeout = std::chrono::seconds(2);"));
  CHECK(Has(stop, "XThread::StopTitleThreads: guest threads are still running; exiting"));
  CHECK(Has(stop, "std::_Exit(EXIT_FAILURE);"));
  for (const auto& entry : std::filesystem::directory_iterator(kPatches)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with("0129-")) {
      CHECK(name == "0129-self-suspend-guest-wait.patch");
    }
  }
  CHECK(!std::filesystem::exists(kPatches / "0129-debug-saulo-title-thread-report.patch"));
  for (const auto& entry : std::filesystem::directory_iterator(kPatches)) {
    if (entry.path().extension() == ".patch") {
      CHECK(!Has(ReadFile(entry.path()), "still running: tid="));
    }
  }
}

void TestExitGameStillTerminatesTheTitle() {
  std::printf("exit game unchanged\n");
  const std::string menu = ReadFile(kSrcDir / "main_menu.cpp");
  CHECK(Has(menu, "__imp__XamLoaderTerminateTitle(ctx, base);"));
  CHECK(Has(menu, "REX_HOOK_RAW(MUA_GUEST_ITEM_USE)"));
  for (const char* forbidden : {"_Exit", "ExitProcess", "TerminateProcess", "std::exit", "TerminateThread"}) {
    CHECK(!Has(menu, forbidden));
  }
}

#ifdef MUA_GENERATED_ROOT

struct Worker {
  const char* directory;
  const char* worker;
  const char* suspend_wrapper;
};

void TestWorkerMapping() {
  std::printf("self-suspending worker in the generated modules\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  const Worker workers[] = {{"mua1", "sub_8260E580", "sub_82467AC0"}, {"mua1-gold", "sub_82628D60", "sub_82480028"}};
  for (const Worker& game : workers) {
    const std::filesystem::path module = root / game.directory;
    if (!std::filesystem::exists(module / "marvel_ultimate_alliance_register.cpp")) {
      std::printf("  %s: module not generated, skipped\n", game.directory);
      continue;
    }
    const std::string worker = FunctionBody(module, game.worker);
    const std::string wrapper = FunctionBody(module, game.suspend_wrapper);
    CHECK(!worker.empty() && !wrapper.empty());
    CHECK(Has(worker, (std::string("\t") + game.suspend_wrapper + "(ctx, base);").c_str()));
    CHECK(Has(worker, "lwz r3,0(r31)"));
    CHECK(Has(worker, "REX_CALL_INDIRECT_FUNC"));
    CHECK(Has(wrapper, "__imp__NtSuspendThread(ctx, base);"));
    CHECK(Has(wrapper, "lwz r3,80(r1)"));
  }
  CHECK(Has(ReadFile(root / "mua1" / "marvel_ultimate_alliance_init.cpp"), "{ 0x8260E580, sub_8260E580 }"));
  CHECK(Has(ReadFile(root / "mua1-gold" / "marvel_ultimate_alliance_init.cpp"), "{ 0x82628D60, sub_82628D60 }"));
}

#endif

void TestSkipIntroAndMua2Untouched() {
  std::printf("skip intro and mua2 untouched\n");
  for (const char* file : {"skip_intro.cpp", "skip_intro.h", "guest_map_mua2.h"}) {
    const std::string text = ReadFile(kSrcDir / file);
    CHECK(!text.empty());
    CHECK(!Has(text, "NtSuspendThread"));
    CHECK(!Has(text, "GuestWait"));
  }
}

}  // namespace

int main() {
  TestSelfSuspendPatch();
  TestStopSafeguardsUntouched();
  TestExitGameStillTerminatesTheTitle();
#ifdef MUA_GENERATED_ROOT
  TestWorkerMapping();
#endif
  TestSkipIntroAndMua2Untouched();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
