// Installing a game from a disc image: disc routing, per-game target folders, staging, verification, the swap and
// the previous install surviving every failure. A fake disc reader and extractor stand in for the Xbox 360 disc
// reader; scratch folders only.
// Run: mua_game_install_tests

#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fake_games.h"
#include "game_install.h"

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

const mua::GameDefinition& Mua1() {
  return mua::Game(mua::GameId::kMua1);
}
const mua::GameDefinition& Gold() {
  return mua::Game(mua::GameId::kMua1Gold);
}
const mua::GameDefinition& Mua2() {
  return mua::Game(mua::GameId::kMua2);
}

// Discs by file name; the extractor writes a default.xex that names `payload_game` and a second file.
struct FakeDisc {
  std::map<std::string, mua::DiscIdentity> discs;
  const mua::GameDefinition* payload_game = nullptr;
  std::string build = "new";
  std::string failure;
  int extractions = 0;

  mua::InstallServices Services() {
    mua::InstallServices services;
    services.read_identity = [this](const fs::path& image) -> std::optional<mua::DiscIdentity> {
      const auto found = discs.find(image.filename().string());
      if (found == discs.end()) {
        return std::nullopt;
      }
      return found->second;
    };
    services.extract = [this](const fs::path&, const fs::path& destination) {
      ++extractions;
      fake::Install(destination, *payload_game, build);
      fake::WriteText(destination / "z" / "assets.bin", "assets");
      return failure;
    };
    services.check = fake::Check;
    return services;
  }
};

FakeDisc AllDiscs() {
  FakeDisc disc;
  disc.discs = {{"mua1.iso", fake::DiscOf(Mua1())}, {"gold.iso", fake::DiscOf(Gold())}, {"mua2.iso", fake::DiscOf(Mua2())}};
  return disc;
}

bool NoInstallLeftovers(const fake::Scratch& scratch) {
  for (const mua::GameDefinition& game : mua::AllGames()) {
    if (fs::exists(scratch.games / (std::string(game.key) + ".staging")) ||
        fs::exists(scratch.games / (std::string(game.key) + ".backup"))) {
      return false;
    }
  }
  return true;
}

void TestWrongDisc() {
  std::printf("a disc for another game is rejected before anything is written\n");
  const fake::Scratch scratch("mua_install_wrong");
  FakeDisc disc = AllDiscs();
  disc.payload_game = &Gold();

  mua::InstallResult result = mua::InstallFromDisc(Mua1(), "gold.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongGame);
  CHECK(result.disc_game == &Gold());
  CHECK(mua::DescribeInstall(result, Mua1()).title == "Wrong game disc");
  CHECK(mua::DescribeInstall(result, Mua1()).text ==
        "This disc is Marvel Ultimate Alliance Gold Edition, but Marvel Ultimate Alliance is selected.");

  result = mua::InstallFromDisc(Gold(), "mua1.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongGame && result.disc_game == &Mua1());
  result = mua::InstallFromDisc(Mua2(), "gold.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongGame && result.disc_game == &Gold());
  result = mua::InstallFromDisc(Mua2(), "mua1.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongGame && result.disc_game == &Mua1());

  CHECK(disc.extractions == 0);
  CHECK(fs::is_empty(scratch.games));
}

void TestUnknownAndUnreadableDiscs() {
  std::printf("files that are not a supported disc are rejected\n");
  const fake::Scratch scratch("mua_install_unknown");
  FakeDisc disc = AllDiscs();
  disc.discs["other.iso"] = {0x415607DA, 0x11111111};
  disc.payload_game = &Gold();

  CHECK(mua::InstallFromDisc(Gold(), "other.iso", scratch.games, disc.Services()).status ==
        mua::InstallStatus::kUnknownDisc);
  CHECK(mua::InstallFromDisc(Gold(), "missing.iso", scratch.games, disc.Services()).status ==
        mua::InstallStatus::kNotDisc);
  CHECK(mua::InstallFromDisc(Gold(), "gold.iso", {}, disc.Services()).status == mua::InstallStatus::kNoGameFolder);
  CHECK(disc.extractions == 0);
  CHECK(fs::is_empty(scratch.games));
}

void TestEachGameInstallsToItsOwnFolder() {
  std::printf("each game installs into its own folder and leaves the others and the legacy folder alone\n");
  const fake::Scratch scratch("mua_install_targets");
  fake::Install(scratch.games / "extracted", Gold(), "legacy");
  FakeDisc disc = AllDiscs();

  std::vector<std::string> phases;
  mua::InstallHooks hooks;
  hooks.phase = [&](const char* text) { phases.push_back(text); };

  disc.payload_game = &Gold();
  mua::InstallResult result = mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services(), hooks);
  CHECK(result.ok());
  CHECK(result.files == 2 && result.bytes > 0);
  CHECK(fake::ReadText(scratch.games / "mua1-gold" / "default.xex") == fake::Xex(Gold(), "new"));
  CHECK(fs::exists(scratch.games / "mua1-gold" / "z" / "assets.bin"));
  CHECK(fake::ReadText(scratch.games / "extracted" / "default.xex") == fake::Xex(Gold(), "legacy"));
  CHECK(!fs::exists(scratch.games / "mua1") && !fs::exists(scratch.games / "mua2"));
  CHECK(phases == (std::vector<std::string>{"Reading disc...", "Extracting game files...", "Verifying installation...",
                                            "Finishing..."}));

  disc.payload_game = &Mua1();
  CHECK(mua::InstallFromDisc(Mua1(), "mua1.iso", scratch.games, disc.Services()).ok());
  CHECK(fake::ReadText(scratch.games / "mua1" / "default.xex") == fake::Xex(Mua1(), "new"));
  CHECK(!fs::exists(scratch.games / "mua2"));

  disc.payload_game = &Mua2();
  CHECK(mua::InstallFromDisc(Mua2(), "mua2.iso", scratch.games, disc.Services()).ok());
  CHECK(fake::ReadText(scratch.games / "mua2" / "default.xex") == fake::Xex(Mua2(), "new"));
  CHECK(fake::ReadText(scratch.games / "mua1-gold" / "default.xex") == fake::Xex(Gold(), "new"));
  CHECK(NoInstallLeftovers(scratch));
  CHECK(mua::InstallFolder(scratch.games, Gold()) == scratch.games / "mua1-gold");
}

void TestReinstallReplacesAndCleansUp() {
  std::printf("a reinstall replaces the game and removes the backup\n");
  const fake::Scratch scratch("mua_install_reinstall");
  fake::Install(scratch.games / "mua1-gold", Gold(), "old");
  fake::WriteText(scratch.games / "mua1-gold" / "z" / "old-only.bin", "old");
  FakeDisc disc = AllDiscs();
  disc.payload_game = &Gold();

  CHECK(mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services()).ok());
  CHECK(fake::ReadText(scratch.games / "mua1-gold" / "default.xex") == fake::Xex(Gold(), "new"));
  CHECK(!fs::exists(scratch.games / "mua1-gold" / "z" / "old-only.bin"));
  CHECK(NoInstallLeftovers(scratch));
}

void TestFailedReinstallKeepsPreviousInstall() {
  std::printf("a failed, rejected or cancelled reinstall keeps the previous install\n");
  const fake::Scratch scratch("mua_install_failure");
  const fs::path live = scratch.games / "mua1-gold";
  fake::Install(live, Gold(), "old");
  FakeDisc disc = AllDiscs();
  const auto kept = [&] {
    return fake::ReadText(live / "default.xex") == fake::Xex(Gold(), "old") && NoInstallLeftovers(scratch);
  };

  disc.payload_game = &Gold();
  disc.build = "bad";
  mua::InstallResult result = mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongVersion);
  CHECK(kept());

  disc.payload_game = &Mua1();
  disc.build = "new";
  result = mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongVersion);
  CHECK(kept());

  disc.payload_game = &Gold();
  disc.failure = "The disc image ended early.";
  result = mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kExtractFailed && result.detail == "The disc image ended early.");
  CHECK(kept());

  disc.failure.clear();
  mua::InstallHooks cancelled;
  cancelled.cancelled = [] { return true; };
  result = mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services(), cancelled);
  CHECK(result.status == mua::InstallStatus::kCancelled);
  CHECK(kept());

  CHECK(mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services()).ok());
  CHECK(fake::ReadText(live / "default.xex") == fake::Xex(Gold(), "new"));
}

void TestIncompleteExtractionIsNotActivated() {
  std::printf("an extraction without the game's default.xex is not activated\n");
  const fake::Scratch scratch("mua_install_incomplete");
  FakeDisc disc = AllDiscs();
  mua::InstallServices services = disc.Services();
  services.extract = [](const fs::path&, const fs::path&) { return std::string(); };

  const mua::InstallResult result = mua::InstallFromDisc(Mua2(), "mua2.iso", scratch.games, services);
  CHECK(result.status == mua::InstallStatus::kIncomplete);
  CHECK(!fs::exists(scratch.games / "mua2"));
  CHECK(NoInstallLeftovers(scratch));
}

void TestInterruptedSwapIsRecovered() {
  std::printf("a backup left by an interrupted swap is put back when the next install fails\n");
  const fake::Scratch scratch("mua_install_recover");
  fake::Install(scratch.games / "mua1.backup", Mua1(), "old");
  FakeDisc disc = AllDiscs();
  disc.payload_game = &Mua1();
  disc.build = "bad";

  const mua::InstallResult result = mua::InstallFromDisc(Mua1(), "mua1.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kWrongVersion);
  CHECK(fake::ReadText(scratch.games / "mua1" / "default.xex") == fake::Xex(Mua1(), "old"));
  CHECK(NoInstallLeftovers(scratch));
}

void TestForeignFolder() {
  std::printf("a folder of other files is not replaced, an empty one is\n");
  const fake::Scratch scratch("mua_install_foreign");
  fake::WriteText(scratch.games / "mua2" / "notes.txt", "mine");
  FakeDisc disc = AllDiscs();
  disc.payload_game = &Mua2();

  const mua::InstallResult result = mua::InstallFromDisc(Mua2(), "mua2.iso", scratch.games, disc.Services());
  CHECK(result.status == mua::InstallStatus::kForeignFolder);
  CHECK(disc.extractions == 0);
  CHECK(fake::ReadText(scratch.games / "mua2" / "notes.txt") == "mine");
  CHECK(!fs::exists(scratch.games / "mua2" / "default.xex"));

  fs::remove_all(scratch.games / "mua2");
  fs::create_directories(scratch.games / "mua2");
  CHECK(mua::InstallFromDisc(Mua2(), "mua2.iso", scratch.games, disc.Services()).ok());
  CHECK(fs::exists(scratch.games / "mua2" / "default.xex"));
}

void TestInstalledGameIsReadBack() {
  std::printf("the library reads an installed game back as that game\n");
  const fake::Scratch scratch("mua_install_readback");
  FakeDisc disc = AllDiscs();
  disc.payload_game = &Gold();
  CHECK(mua::InstallFromDisc(Gold(), "gold.iso", scratch.games, disc.Services()).ok());

  const mua::GameLibrary library(scratch.games, {}, disc.Services().read_identity, fake::Check);
  CHECK(library.State(mua::GameId::kMua1Gold).data_dir == scratch.games / "mua1-gold");
  CHECK(mua::CanPlay(library.State(mua::GameId::kMua1Gold), ""));
  CHECK(!mua::CanPlay(library.State(mua::GameId::kMua1), ""));
  CHECK(!mua::CanPlay(library.State(mua::GameId::kMua2), ""));
}

}  // namespace

int main() {
  TestWrongDisc();
  TestUnknownAndUnreadableDiscs();
  TestEachGameInstallsToItsOwnFolder();
  TestReinstallReplacesAndCleansUp();
  TestFailedReinstallKeepsPreviousInstall();
  TestIncompleteExtractionIsNotActivated();
  TestInterruptedSwapIsRecovered();
  TestForeignFolder();
  TestInstalledGameIsReadBack();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
