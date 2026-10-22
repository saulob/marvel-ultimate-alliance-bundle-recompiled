// Each game's data folder, the move of the earlier layouts into it, and which program each game starts.
// Run: mua_game_data_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "game_data.h"
#include "game_definition.h"
#include "launcher.h"

namespace fs = std::filesystem;

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

const mua::GameDefinition& Mua1() { return mua::Game(mua::GameId::kMua1); }
const mua::GameDefinition& Gold() { return mua::Game(mua::GameId::kMua1Gold); }
const mua::GameDefinition& Mua2() { return mua::Game(mua::GameId::kMua2); }

struct Scratch {
  fs::path root;
  Scratch() {
    root = fs::temp_directory_path() / "mua_game_data_tests";
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Scratch() { fs::remove_all(root); }
};

void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string ReadSource(const char* name) {
  return Read(fs::path(MUA_SRC_DIR) / name);
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

void TestFolders() {
  std::printf("each game has its own data folder\n");
  const fs::path root = "user_data";
  CHECK(mua::GameUserDataRoot(Mua1(), root) == root / "mua1");
  CHECK(mua::GameUserDataRoot(Gold(), root) == root / "mua1_gold");
  CHECK(mua::GameUserDataRoot(Mua2(), root) == root / "mua2");
}

void TestGoldTitleFolderMoves() {
  std::printf("Gold Edition's saves move out of the root, once\n");
  Scratch scratch;
  Write(scratch.root / "415607DA" / "profile" / "save.bin", "gold save");
  Write(scratch.root / "settings.toml", "shared");
  const auto log = mua::MigrateLegacyGameData(scratch.root);
  CHECK(!log.empty());
  CHECK(Read(scratch.root / "mua1_gold" / "415607DA" / "profile" / "save.bin") == "gold save");
  CHECK(!fs::exists(scratch.root / "415607DA"));
  CHECK(Read(scratch.root / "settings.toml") == "shared");
  CHECK(!fs::exists(scratch.root / "mua1") && !fs::exists(scratch.root / "mua2"));
  CHECK(mua::MigrateLegacyGameData(scratch.root).empty());
}

void TestMua1FolderAndSettingsMove() {
  std::printf("the development folders and settings files move into the game folders\n");
  Scratch scratch;
  Write(scratch.root / "games" / "mua1" / "415607DA" / "save.bin", "mua1 save");
  Write(scratch.root / "games" / "mua1.toml", "mua_frame_rate = 120\n");
  Write(scratch.root / "games" / "mua1-gold.toml", "mua_frame_rate = 90\n");
  mua::MigrateLegacyGameData(scratch.root);
  CHECK(Read(scratch.root / "mua1" / "415607DA" / "save.bin") == "mua1 save");
  CHECK(Read(scratch.root / "mua1" / "game_settings.toml") == "mua_frame_rate = 120\n");
  CHECK(Read(scratch.root / "mua1_gold" / "game_settings.toml") == "mua_frame_rate = 90\n");
  CHECK(!fs::exists(scratch.root / "games" / "mua1") && !fs::exists(scratch.root / "games" / "mua1.toml"));
  CHECK(!fs::exists(scratch.root / "mua1_gold" / "415607DA"));
}

void TestNothingIsOverwritten() {
  std::printf("a target that exists is kept, and the earlier data stays\n");
  Scratch scratch;
  Write(scratch.root / "415607DA" / "old.bin", "old gold");
  Write(scratch.root / "mua1_gold" / "415607DA" / "new.bin", "new gold");
  Write(scratch.root / "games" / "mua1" / "415607DA" / "old.bin", "old mua1");
  Write(scratch.root / "mua1" / "other.bin", "current mua1");
  const auto log = mua::MigrateLegacyGameData(scratch.root);
  CHECK(log.size() == 2);
  CHECK(Read(scratch.root / "415607DA" / "old.bin") == "old gold");
  CHECK(Read(scratch.root / "mua1_gold" / "415607DA" / "new.bin") == "new gold");
  CHECK(!fs::exists(scratch.root / "mua1_gold" / "415607DA" / "old.bin"));
  CHECK(Read(scratch.root / "games" / "mua1" / "415607DA" / "old.bin") == "old mua1");
  CHECK(Read(scratch.root / "mua1" / "other.bin") == "current mua1");
}

void TestNothingIsCreatedWithoutData() {
  std::printf("no folder is created for a game without data\n");
  Scratch scratch;
  CHECK(mua::MigrateLegacyGameData(scratch.root).empty());
  CHECK(fs::is_empty(scratch.root));
}

void TestDispatch() {
  std::printf("each game starts its own program, and only that one\n");
  CHECK(Mua1().executable == "marvel_ultimate_alliance_mua1.exe");
  CHECK(Gold().executable == "marvel_ultimate_alliance_mua1_gold.exe");
  CHECK(Mua2().executable == "marvel_ultimate_alliance_mua2.exe" && Mua2().runtime_supported);
  CHECK(Mua1().executable != Gold().executable);
  CHECK(Mua2().executable != Mua1().executable && Mua2().executable != Gold().executable);
  CHECK(mua::FindGame("mua2") == &Mua2() && Mua2().data_folder == "mua2");
  for (const mua::GameDefinition& game : mua::AllGames()) {
    CHECK(game.executable != "marvel_ultimate_alliance.exe" && !game.executable.empty() && game.runtime_supported);
  }

  CHECK(mua::LauncherWanted(false, false, false, false));
  CHECK(mua::LauncherWanted(false, true, false, false));
  CHECK(!mua::LauncherWanted(true, false, false, false));
  CHECK(mua::LauncherWanted(true, false, true, false));
  CHECK(mua::LauncherWanted(true, false, false, true));
  CHECK(!mua::LauncherWanted(true, true, true, true));
}

void TestWiring() {
  std::printf("the launcher program carries no game, and the programs are wired that way\n");
  const std::string cmake = ReadSource("../CMakeLists.txt");
  CHECK(Has(cmake, "mua_add_game_executable(marvel_ultimate_alliance \"\")"));
  CHECK(Has(cmake, "mua_add_game_executable(marvel_ultimate_alliance_mua1 mua1)"));
  CHECK(Has(cmake, "mua_add_game_executable(marvel_ultimate_alliance_mua1_gold mua1-gold)"));
  CHECK(Has(cmake, "mua_add_game_executable(marvel_ultimate_alliance_mua2 mua2)"));
  const std::string modules = ReadSource("../cmake/rexglue.cmake");
  CHECK(Has(modules, "mua_declare_module(mua2 marvel_ultimate_alliance_mua2_codegen)"));
  CHECK(Has(ReadSource("../mua2.manifest.toml"), "out_directory_path = \"generated/mua2\""));
  CHECK(Has(cmake, "if(NOT \"${module_key}\" STREQUAL \"\")\n        target_sources(${target} PRIVATE ${MUA_RUNTIME_SOURCES})") ||
        Has(cmake, "if(NOT \"${module_key}\" STREQUAL \"\")\r\n        target_sources(${target} PRIVATE ${MUA_RUNTIME_SOURCES})"));
  const std::string rexglue = ReadSource("../cmake/rexglue.cmake");
  CHECK(Has(rexglue, "MUA_RUNTIME_KEY=\"${module_key}\""));

  const size_t runtime_list = cmake.find("set(MUA_RUNTIME_SOURCES");
  const size_t host_list = cmake.find("set(MUA_HOST_SOURCES");
  CHECK(runtime_list != std::string::npos && host_list != std::string::npos && host_list < runtime_list);
  const std::string host_sources = cmake.substr(host_list, runtime_list - host_list);
  for (const char* hook_file : {"frame_limiter.cpp", "frame_pacing.cpp", "gpu_fence_wait.cpp", "save_diagnostics.cpp",
                                "swap_hooks.cpp", "audio_mixer_priority.cpp", "xaudio_worker_barrier.cpp",
                                "sound_stream_ring.cpp", "crt_native.cpp", "indirect_tail_calls.cpp",
                                "guest_hook_report.cpp"}) {
    CHECK(!Has(host_sources, hook_file));
  }

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "static bool IsLauncher() { return !mua::GetRecompiledImage().game; }"));
  CHECK(Has(app, "game_settings_.SelectGame(*own_game);"));
  CHECK(Has(app, "StartRuntimeAndExit(library_->State(selected));"));
  CHECK(Has(app, "std::string(game.game->executable)"));
  CHECK(!Has(app, "RelaunchSelf"));
  CHECK(!Has(app, "REX_DEV_FRAME_LOG"));

  CHECK(Has(app, "paths.game_data_root = game.data_dir;"));
  CHECK(Has(app, "paths.user_data_root = mua::GameUserDataRoot(*game.game, defaults.user_data_root);"));
  CHECK(Has(app, "mua::SetActiveGame(verified->id);"));
  CHECK(Has(app, "window()->SetTitle(mua::GameWindowTitle(*verified));"));
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "std::make_unique<LauncherAchievements>(Selected().data_dir, GameUserDataRoot(*Selected().game,"));

  const std::string image = ReadSource("recomp_image.h");
  CHECK(Has(image, "std::optional<GameId> game;"));
}

}  // namespace

int main() {
  TestFolders();
  TestGoldTitleFolderMoves();
  TestMua1FolderAndSettingsMove();
  TestNothingIsOverwritten();
  TestNothingIsCreatedWithoutData();
  TestDispatch();
  TestWiring();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
