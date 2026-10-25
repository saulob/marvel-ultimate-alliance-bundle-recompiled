// The Skip intro startup notice state, the counter's per-game accent and the removal of ReXGlue's F3/F4 binds.
// Headless: no window, no ImGui, no clock.
// Run: mua_skip_intro_notice_tests

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "frame_counter.h"
#include "game_theme.h"
#include "skip_intro_notice.h"

using namespace mua;

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

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

void TestNoticeLifecycle() {
  std::printf("startup notice lifecycle\n");
  CHECK(kSkipIntroStartupNoticeSeconds == 5.0);
  CHECK(std::strcmp(kSkipIntroNoticeText, "Skip intro: On") == 0);

  SkipIntroNotice off;
  off.QueueStartup(false, kSkipIntroStartupNoticeSeconds);
  CHECK(!off.Visible(0.0));
  CHECK(!off.Visible(1.0));

  SkipIntroNotice on;
  on.QueueStartup(true, kSkipIntroStartupNoticeSeconds);
  CHECK(on.Visible(100.0));
  CHECK(on.Visible(104.9));
  CHECK(!on.Visible(105.0));
  CHECK(!on.Visible(200.0));
  CHECK(std::strcmp(on.text(), "Skip intro: On") == 0);

  SkipIntroNotice once;
  once.QueueStartup(false, kSkipIntroStartupNoticeSeconds);
  once.QueueStartup(true, kSkipIntroStartupNoticeSeconds);
  CHECK(!once.Visible(1.0));
}

void TestNoticeWaitsForCounterOnly() {
  std::printf("the notice does not depend on the counter setting\n");
  CHECK(SkipIntroNoticeReady(false, false));
  CHECK(!SkipIntroNoticeReady(true, false));
  CHECK(SkipIntroNoticeReady(true, true));

  SkipIntroNotice hidden_counter;
  hidden_counter.QueueStartup(true, kSkipIntroStartupNoticeSeconds);
  CHECK(hidden_counter.Visible(10.0, SkipIntroNoticeReady(false, false)));

  SkipIntroNotice waiting;
  waiting.QueueStartup(true, kSkipIntroStartupNoticeSeconds);
  CHECK(!waiting.Visible(10.0, SkipIntroNoticeReady(true, false)));
  CHECK(waiting.Visible(20.0, SkipIntroNoticeReady(true, true)));
  CHECK(waiting.Visible(24.0));
  CHECK(!waiting.Visible(25.0));
}

void TestNoticeApplicability() {
  std::printf("the notice is queued for a running game with Skip intro on\n");
  for (const GameDefinition& game : AllGames()) {
    CHECK(SkipIntroNoticeApplies(&game, true) == game.runtime_supported);
    CHECK(!SkipIntroNoticeApplies(&game, false));
  }
  CHECK(!SkipIntroNoticeApplies(nullptr, true));
}

void TestAccentColors() {
  std::printf("the counter follows the game accent\n");
  CHECK(FrameCounterAccent(&Game(GameId::kMua1)) == PaletteOf(GameId::kMua1).primary);
  CHECK(FrameCounterAccent(&Game(GameId::kMua1Gold)) == PaletteOf(GameId::kMua1Gold).primary);
  CHECK(FrameCounterAccent(&Game(GameId::kMua2)) == PaletteOf(GameId::kMua2).primary);
  CHECK(FrameCounterAccent(nullptr) == PaletteOf(GameId::kMua1).primary);
  CHECK(FrameCounterAccent(&Game(GameId::kMua1)) != FrameCounterAccent(&Game(GameId::kMua1Gold)));
  CHECK(FrameCounterAccent(&Game(GameId::kMua1)) != FrameCounterAccent(&Game(GameId::kMua2)));

  const std::string counter = ReadFile(kSrcDir / "frame_counter.cpp");
  CHECK(Has(counter, "FrameCounterAccent(ActiveGame())"));
  CHECK(!Has(counter, "IM_COL32(92, 224, 112"));
}

void TestWiring() {
  std::printf("the app queues the notice before creating its overlay\n");
  const std::string app = ReadFile(kSrcDir / "marvel_ultimate_alliance_app.h");
  const size_t queue = app.find("mua::QueueStartupSkipIntroNotice(REXCVAR_GET(mua_skip_intro))");
  const size_t create = app.find("mua::CreateSkipIntroNotice(imgui_drawer())");
  CHECK(queue != std::string::npos && create != std::string::npos && queue < create);
}

void TestReXGlueBindsRemoved() {
  std::printf("the patched SDK registers neither F3 nor F4\n");
  const std::string patch =
      ReadFile(kSrcDir / ".." / "patches" / "rexglue-sdk" / "0146-disable-debug-and-settings-overlay-binds.patch");
  CHECK(Has(patch, "-  rex::ui::RegisterBind(\"bind_debug_overlay\", \"F3\""));
  CHECK(Has(patch, "-  rex::ui::RegisterBind(\"bind_settings\", \"F4\""));
  CHECK(!Has(patch, "+  rex::ui::RegisterBind"));
  CHECK(!Has(patch, "bind_console\", \"Backtick\", \"Toggle console overlay\", [this] {\n-"));
  CHECK(!Has(patch, "-  rex::ui::RegisterBind(\"bind_achievements\""));

  const std::filesystem::path root = kSrcDir / "..";
  for (const auto& entry : std::filesystem::directory_iterator(root / "patches" / "rexglue-sdk")) {
    if (entry.path().extension() == ".patch" && entry.path().filename().string() != "0146-disable-debug-and-settings-overlay-binds.patch") {
      const std::string other = ReadFile(entry.path());
      CHECK(!Has(other, "+  rex::ui::RegisterBind(\"bind_debug_overlay\""));
      CHECK(!Has(other, "+  rex::ui::RegisterBind(\"bind_settings\""));
    }
  }

  // The game executable compiles ReXApp from the SDK folder, so that copy must be the patched one.
  const std::string installer = ReadFile(root / "tools" / "rexruntime-patch.ps1");
  CHECK(Has(installer, "'share/rexglue/rex_app.cpp' = 'src/ui/rex_app.cpp'"));
  CHECK(Has(installer, "does not match the patched source"));
  CHECK(Has(ReadFile(root / "tools" / "build-patched-rexruntime.ps1"), "Copy-FileIfDifferent (Join-Path $src $RuntimeSourceFiles[$_])"));

  const std::filesystem::path installed = root / "tools" / "rexglue" / "win-amd64";
  const std::string stamp = ReadFile(installed / "rexruntime-patch.txt");
  if (Has(stamp, "  share/rexglue/rex_app.cpp")) {
    const std::string source = ReadFile(installed / "share" / "rexglue" / "rex_app.cpp");
    CHECK(!Has(source, "RegisterBind(\"bind_debug_overlay\""));
    CHECK(!Has(source, "RegisterBind(\"bind_settings\""));
    CHECK(Has(source, "RegisterBind(\"bind_achievements\", \"F7\""));
  }
}

}  // namespace

int main() {
  TestNoticeLifecycle();
  TestNoticeWaitsForCounterOnly();
  TestNoticeApplicability();
  TestAccentColors();
  TestWiring();
  TestReXGlueBindsRemoved();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
