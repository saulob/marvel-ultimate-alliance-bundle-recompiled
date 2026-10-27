// Game definitions, per-game state, disc classification, legacy and preferred game folders, which games can be played
// and which game a start without the launcher boots. Scratch folders only.
// Run: mua_game_library_tests

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fake_games.h"
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

void PutU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    bytes[offset + i] = uint8_t(value >> (24 - 8 * i));
  }
}

std::vector<uint8_t> MakeXex(uint32_t title_id, uint32_t media_id, const std::string& pe_name) {
  std::vector<uint8_t> bytes(0x200);
  std::copy_n("XEX2", 4, bytes.begin());
  PutU32(bytes, 0x14, 2);
  PutU32(bytes, 0x18, 0x00040006);
  PutU32(bytes, 0x1C, 0x100);
  PutU32(bytes, 0x20, 0x000183FF);
  PutU32(bytes, 0x24, 0x140);
  PutU32(bytes, 0x100, media_id);
  PutU32(bytes, 0x104, 3);
  PutU32(bytes, 0x108, 3);
  PutU32(bytes, 0x10C, title_id);
  PutU32(bytes, 0x110, 0x00000101);
  PutU32(bytes, 0x140, uint32_t(pe_name.size() + 5));
  std::copy(pe_name.begin(), pe_name.end(), bytes.begin() + 0x144);
  return bytes;
}

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

mua::DiscReader FakeDiscs(std::map<std::string, mua::DiscIdentity> discs, int* reads = nullptr) {
  return [discs = std::move(discs), reads](const fs::path& image) -> std::optional<mua::DiscIdentity> {
    if (reads) {
      ++*reads;
    }
    const auto found = discs.find(image.filename().string());
    if (found == discs.end()) {
      return std::nullopt;
    }
    return found->second;
  };
}

mua::GameLibrary FakeLibrary(const fake::Scratch& scratch) {
  return mua::GameLibrary(scratch.games, {}, FakeDiscs({}), fake::Check);
}

const mua::GameDefinition& Mua1() {
  return mua::Game(mua::GameId::kMua1);
}
const mua::GameDefinition& Gold() {
  return mua::Game(mua::GameId::kMua1Gold);
}
const mua::GameDefinition& Mua2() {
  return mua::Game(mua::GameId::kMua2);
}

void TestDefinitions() {
  std::printf("game definitions\n");
  const auto games = mua::AllGames();
  CHECK(games.size() == 3);
  std::set<std::string> keys;
  for (size_t i = 0; i < games.size(); ++i) {
    const mua::GameDefinition& game = games[i];
    CHECK(size_t(game.id) == i);
    CHECK(keys.insert(std::string(game.key)).second);
    CHECK(mua::ProfileById(game.profile_id) != nullptr);
    CHECK(mua::GameForProfile(mua::ProfileOf(game)) == &game);
    CHECK(mua::FindGame(game.key) == &game);
    CHECK(!game.tab_label.empty() && !game.short_label.empty());
    CHECK(game.HasPage(mua::LauncherPage::kPlay) && game.HasPage(mua::LauncherPage::kAbout));
  }
  CHECK(mua::FindGame("unknown") == nullptr);
  CHECK(mua::DefaultGame().id == mua::GameId::kMua1Gold);

  CHECK(mua::GameWindowTitle(Mua1()) == "Marvel Ultimate Alliance Recompiled");
  CHECK(mua::GameWindowTitle(Gold()) == "Marvel Ultimate Alliance Gold Edition Recompiled");
  CHECK(mua::GameWindowTitle(Mua2()) == "Marvel Ultimate Alliance 2 Recompiled");

  CHECK(Mua1().runtime_supported && Gold().runtime_supported && Mua2().runtime_supported);
  for (const mua::GameDefinition* game : {&Mua1(), &Gold(), &Mua2()}) {
    CHECK(game->HasPage(mua::LauncherPage::kDisplay) && game->HasPage(mua::LauncherPage::kGraphics) &&
          game->HasPage(mua::LauncherPage::kGameplay) && game->HasPage(mua::LauncherPage::kAchievements) &&
          game->HasPage(mua::LauncherPage::kControls));
  }
  CHECK(Gold().executable == "marvel_ultimate_alliance_mua1_gold.exe" &&
        Mua1().executable == "marvel_ultimate_alliance_mua1.exe" &&
        Mua2().executable == "marvel_ultimate_alliance_mua2.exe");
  CHECK(Mua2().executable != Mua1().executable && Mua2().executable != Gold().executable);
  CHECK(mua::ProfileOf(Mua2()).id == "mua2-x360" && mua::ProfileOf(Mua2()).title_id == 0x4156082F);
}

void TestFramePolicies() {
  std::printf("each game's frame rate policy\n");
  CHECK(Mua1().frame_policy.native == 30 && Gold().frame_policy.native == 30);
  CHECK(!Mua1().frame_policy.host_limiter && !Gold().frame_policy.host_limiter);
  CHECK(Mua2().frame_policy.native == 30 && Mua2().frame_policy.host_limiter);
  CHECK(mua::FrameRateChoices(Mua2().frame_policy)[0] ==
        std::make_pair(std::string("30"), std::string("30 - Original")));
  CHECK(mua::FrameRateChoices(Mua1().frame_policy)[0] ==
        std::make_pair(std::string("30"), std::string("30 - Original")));
}

void TestUpscalers() {
  std::printf("DLSS belongs to MUA 1; Gold Edition and MUA 2 keep Off and FSR 1; NIS and CAS are gone\n");
  CHECK(Mua1().upscalers == mua::kAllUpscalers);
  CHECK(Gold().upscalers == mua::kFsr1Upscalers);
  CHECK(Mua2().upscalers == mua::kFsr1Upscalers);
  for (const mua::GameDefinition* game : {&Gold(), &Mua2()}) {
    CHECK(mua::ParseUpscaler(mua::kUpscalerDlss, game->upscalers) == mua::Upscaler::kOff);
    CHECK(mua::ParseUpscaler(mua::kUpscalerFsr1, game->upscalers) ==
          (mua::kUpscalerAvailable ? mua::Upscaler::kFsr1 : mua::Upscaler::kOff));
  }
  CHECK(mua::ParseUpscaler(mua::kUpscalerDlss, Mua1().upscalers) ==
        (mua::kUpscalerAvailable ? mua::Upscaler::kDlss : mua::Upscaler::kOff));
  for (const mua::GameDefinition* game : {&Mua1(), &Gold(), &Mua2()}) {
    CHECK(mua::ParseUpscaler("nis", game->upscalers) == mua::Upscaler::kOff);
    CHECK(mua::ParseUpscaler("cas", game->upscalers) == mua::Upscaler::kOff);
  }
}

void TestSaveRoots() {
  std::printf("MUA 1 and Gold Edition share a title ID but not a save root\n");
  const fs::path root = "user_data";
  CHECK(mua::ProfileOf(Mua1()).title_id == mua::ProfileOf(Gold()).title_id);
  CHECK(mua::GameUserDataRoot(Gold(), root) == root / "mua1_gold");
  CHECK(mua::GameUserDataRoot(Mua1(), root) == root / "mua1");
  CHECK(mua::GameUserDataRoot(Mua2(), root) == root / "mua2");
  CHECK(mua::GameUserDataRoot(Mua1(), root) != mua::GameUserDataRoot(Gold(), root));
}

void TestProfiles() {
  std::printf("profiles\n");
  const mua::GameProfile& gold = mua::ProfileOf(Gold());
  CHECK(gold.id == "mua-gold-x360");
  CHECK(gold.title_id == 0x415607DA && gold.media_id == 0x5A20A6D4);
  CHECK(gold.xex_sha256 == "06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7");

  std::set<std::pair<uint32_t, uint32_t>> identities;
  for (const mua::GameProfile& profile : mua::KnownProfiles()) {
    CHECK(identities.emplace(profile.title_id, profile.media_id).second);
    CHECK(profile.xex_sha256.size() == 64);
    CHECK(profile.install_bytes > 0);
  }
  CHECK(mua::ProfileOf(Mua1()).title_id == gold.title_id && mua::ProfileOf(Mua1()).media_id != gold.media_id);
}

void TestActiveGame() {
  std::printf("no game is active until one is started\n");
  CHECK(mua::ActiveGame() == nullptr);
  mua::SetActiveGame(mua::GameId::kMua1Gold);
  CHECK(mua::ActiveGame() == &Gold());
}

void TestDiscClassification() {
  std::printf("a disc is classified by title ID and media ID\n");
  CHECK(mua::GameForDisc({0x415607DA, 0x26848425}) == &Mua1());
  CHECK(mua::GameForDisc({0x415607DA, 0x5A20A6D4}) == &Gold());
  CHECK(mua::GameForDisc({0x4156082F, 0x39B3E2C9}) == &Mua2());
  CHECK(mua::GameForDisc({0x415607DA, 0x11111111}) == nullptr);
  CHECK(mua::GameForDisc({0x4156082F, 0x5A20A6D4}) == nullptr);
  CHECK(mua::GameForDisc({0, 0}) == nullptr);
}

void TestEmptyLibrary() {
  std::printf("an empty games folder installs nothing\n");
  const fake::Scratch scratch("mua_library_empty");
  const mua::GameLibrary library = FakeLibrary(scratch);
  for (const mua::GameDefinition& game : mua::AllGames()) {
    const mua::GameState& state = library.State(game.id);
    CHECK(state.game == &game);
    CHECK(state.check.state == mua::GameFileState::kMissing);
    CHECK(state.data_dir == scratch.games / std::string(game.key));
    CHECK(state.source_package.empty());
    CHECK(mua::AvailabilityOf(state, "") == mua::GameAvailability::kNotInstalled);
    CHECK(!mua::CanPlay(state, ""));
  }
  CHECK(!library.explicit_game());
}

void TestSourcePackages() {
  std::printf("source packages are matched by title ID and media ID, not by name\n");
  const fake::Scratch scratch("mua_library_sources");
  fake::WriteText(scratch.games / "first.iso", "1");
  fake::WriteText(scratch.games / "second.ISO", "1");
  fake::WriteText(scratch.games / "third.iso", "1");
  fake::WriteText(scratch.games / "unknown.iso", "1");
  fake::WriteText(scratch.games / "notes.txt", "1");
  int reads = 0;
  const mua::GameLibrary library(
      scratch.games, {},
      FakeDiscs({{"first.iso", {0x415607DA, 0x26848425}},
                 {"second.ISO", {0x415607DA, 0x5A20A6D4}},
                 {"third.iso", {0x4156082F, 0x39B3E2C9}},
                 {"unknown.iso", {0x415607DA, 0x11111111}},
                 {"notes.txt", {0x415607DA, 0x5A20A6D4}}},
                &reads),
      fake::Check);
  CHECK(reads == 4);
  CHECK(library.State(mua::GameId::kMua1).source_package.filename() == "first.iso");
  CHECK(library.State(mua::GameId::kMua1Gold).source_package.filename() == "second.ISO");
  CHECK(library.State(mua::GameId::kMua2).source_package.filename() == "third.iso");
  for (const mua::GameDefinition& game : mua::AllGames()) {
    CHECK(mua::AvailabilityOf(library.State(game.id), "") == mua::GameAvailability::kNotInstalled);
  }
}

void TestForeignFilesAreNotAnotherGame() {
  std::printf("another game's files in a game's folder are an unsupported version\n");
  const fake::Scratch scratch("mua_library_foreign");
  fake::Install(scratch.games / "mua1", Gold());
  fake::WriteText(scratch.games / "mua2" / "default.xex", "garbage");
  const mua::GameLibrary library = FakeLibrary(scratch);
  const mua::GameState& mua1 = library.State(mua::GameId::kMua1);
  CHECK(mua1.check.state == mua::GameFileState::kDifferentVersion);
  CHECK(mua1.check.profile == nullptr);
  CHECK(mua1.data_dir == scratch.games / "mua1");
  CHECK(mua::AvailabilityOf(mua1, "") == mua::GameAvailability::kUnsupportedFiles);
  CHECK(mua::AvailabilityOf(library.State(mua::GameId::kMua2), "") == mua::GameAvailability::kUnsupportedFiles);
  CHECK(mua::AvailabilityOf(library.State(mua::GameId::kMua1Gold), "") == mua::GameAvailability::kNotInstalled);
}

void TestGoldLegacyFolder() {
  std::printf("Gold in the earlier extracted folder is accepted, and the new Gold folder is preferred\n");
  const fake::Scratch scratch("mua_library_legacy");
  fake::Install(scratch.games / "extracted", Gold(), "legacy");
  mua::GameLibrary library = FakeLibrary(scratch);
  const mua::GameState& gold = library.State(mua::GameId::kMua1Gold);
  CHECK(gold.data_dir == scratch.games / "extracted");
  CHECK(gold.check.state == mua::GameFileState::kValid);
  CHECK(mua::CanPlay(gold, ""));
  CHECK(library.State(mua::GameId::kMua1).data_dir == scratch.games / "mua1");
  CHECK(library.State(mua::GameId::kMua2).data_dir == scratch.games / "mua2");

  fake::Install(scratch.games / "mua1-gold", Gold(), "new");
  library.RefreshFiles(mua::GameId::kMua1Gold);
  CHECK(library.State(mua::GameId::kMua1Gold).data_dir == scratch.games / "mua1-gold");
  CHECK(fake::ReadText(scratch.games / "extracted" / "default.xex") == fake::Xex(Gold(), "legacy"));

  fake::WriteText(scratch.games / "mua1-gold" / "default.xex", "garbage");
  library.RefreshFiles(mua::GameId::kMua1Gold);
  CHECK(library.State(mua::GameId::kMua1Gold).data_dir == scratch.games / "extracted");
  CHECK(mua::CanPlay(library.State(mua::GameId::kMua1Gold), ""));

  fs::remove_all(scratch.games / "extracted");
  library.RefreshFiles(mua::GameId::kMua1Gold);
  CHECK(library.State(mua::GameId::kMua1Gold).data_dir == scratch.games / "mua1-gold");
  CHECK(!mua::CanPlay(library.State(mua::GameId::kMua1Gold), ""));
}

void TestInstalledGamesCanPlay() {
  std::printf("every installed game is playable by its own program\n");
  const fake::Scratch scratch("mua_library_play");
  fake::Install(scratch.games / "mua1", Mua1());
  fake::Install(scratch.games / "mua1-gold", Gold());
  fake::Install(scratch.games / "mua2", Mua2());
  const mua::GameLibrary library = FakeLibrary(scratch);
  const mua::GameState& mua1 = library.State(mua::GameId::kMua1);
  const mua::GameState& mua2 = library.State(mua::GameId::kMua2);
  CHECK(mua1.check.state == mua::GameFileState::kValid && mua2.check.state == mua::GameFileState::kValid);
  CHECK(mua::AvailabilityOf(mua1, "") == mua::GameAvailability::kReady);
  CHECK(mua::AvailabilityOf(mua2, "") == mua::GameAvailability::kReady);
  CHECK(mua::CanPlay(mua1, "") && mua::CanPlay(mua2, ""));
  CHECK(!mua::CanPlay(mua1, "its program is missing") && !mua::CanPlay(mua2, "its program is missing"));
  CHECK(mua::CanPlay(library.State(mua::GameId::kMua1Gold), ""));
  CHECK(!mua::CanPlay(library.State(mua::GameId::kMua1Gold), "recompiled code is missing"));
}

void TestBootRouting() {
  std::printf("a start without the launcher boots the selected game or nothing\n");
  const fake::Scratch scratch("mua_library_boot");
  fake::Install(scratch.games / "extracted", Gold());
  fake::Install(scratch.games / "mua1", Mua1());
  fake::Install(scratch.games / "mua2", Mua2());
  const mua::GameLibrary library = FakeLibrary(scratch);

  const mua::GameState* gold = mua::BootableGame(library, mua::GameId::kMua1Gold, "");
  CHECK(gold != nullptr && gold->game == &Gold() && gold->data_dir == scratch.games / "extracted");
  CHECK(gold->check.profile == &mua::ProfileOf(Gold()));
  const mua::GameState* mua1 = mua::BootableGame(library, mua::GameId::kMua1, "");
  CHECK(mua1 != nullptr && mua1->game == &Mua1() && mua1->data_dir == scratch.games / "mua1");
  CHECK(mua::BootableGame(library, mua::GameId::kMua1, "runs in its own program") == nullptr);
  const mua::GameState* mua2 = mua::BootableGame(library, mua::GameId::kMua2, "");
  CHECK(mua2 != nullptr && mua2->game == &Mua2() && mua2->data_dir == scratch.games / "mua2");
  CHECK(mua2->check.profile == &mua::ProfileOf(Mua2()));
  CHECK(mua::BootableGame(library, mua::GameId::kMua2, "runs in its own program") == nullptr);
  CHECK(mua::BootableGame(library, mua::GameId::kMua1Gold, "recompiled code is missing") == nullptr);

  CHECK(!mua::LauncherWanted(mua2 != nullptr, false, false, false));
  CHECK(!mua::LauncherWanted(gold != nullptr, false, false, false));
  CHECK(mua::LauncherWanted(gold != nullptr, false, true, false));
  CHECK(!mua::LauncherWanted(gold != nullptr, true, false, true));
  CHECK(mua::LauncherWanted(gold != nullptr, false, false, true));
}

void TestAvailabilityText() {
  std::printf("availability follows the files and the runtime\n");
  mua::GameState gold;
  gold.game = &Gold();
  gold.check.state = mua::GameFileState::kValid;
  gold.check.profile = &mua::ProfileOf(Gold());
  CHECK(mua::AvailabilityOf(gold, "") == mua::GameAvailability::kReady);
  CHECK(mua::AvailabilityOf(gold, "not generated") == mua::GameAvailability::kRuntimeUnavailable);

  mua::GameState incomplete = gold;
  incomplete.check.state = mua::GameFileState::kIncomplete;
  CHECK(mua::AvailabilityOf(incomplete, "") == mua::GameAvailability::kIncomplete);
  incomplete.check.state = mua::GameFileState::kReadError;
  CHECK(mua::AvailabilityOf(incomplete, "") == mua::GameAvailability::kUnreadable);

  CHECK(mua::SelectedGameLogText(gold, "") ==
        "Selected game: Marvel Ultimate Alliance Gold Edition | profile=mua-gold-x360 | installed=yes | source=no | "
        "supported=yes");
  mua::GameState mua1 = gold;
  mua1.game = &Mua1();
  mua1.check.profile = &mua::ProfileOf(Mua1());
  CHECK(mua::SelectedGameLogText(mua1, "") ==
        "Selected game: Marvel Ultimate Alliance | profile=mua-x360 | installed=yes | source=no | supported=yes");
  mua::GameState missing;
  missing.game = &Mua2();
  missing.source_package = "game/mua2.iso";
  CHECK(mua::SelectedGameLogText(missing, "") ==
        "Selected game: Marvel Ultimate Alliance 2 | profile=none | installed=no | source=yes | supported=no");
  mua::GameState mua2 = gold;
  mua2.game = &Mua2();
  mua2.check.profile = &mua::ProfileOf(Mua2());
  CHECK(mua::SelectedGameLogText(mua2, "") ==
        "Selected game: Marvel Ultimate Alliance 2 | profile=mua2-x360 | installed=yes | source=no | supported=yes");
}

void TestExplicitRoot() {
  std::printf("a game folder named on the command line belongs to the game its files identify\n");
  const fake::Scratch scratch("mua_library_explicit");
  const fs::path elsewhere = scratch.root / "elsewhere";
  Write(elsewhere / "default.xex", MakeXex(0x415607DA, 0x5A20A6D4, "Marvel_Xenon_fnl.pe"));
  const mua::GameLibrary library(scratch.games, elsewhere, FakeDiscs({}));
  CHECK(library.explicit_game() == mua::GameId::kMua1Gold);
  CHECK(library.State(mua::GameId::kMua1Gold).data_dir == elsewhere);
  CHECK(library.State(mua::GameId::kMua1).data_dir == scratch.games / "mua1");
  CHECK(library.State(mua::GameId::kMua2).data_dir == scratch.games / "mua2");

  const fs::path stranger = scratch.root / "stranger";
  Write(stranger / "default.xex", std::vector<uint8_t>(0x200, 0x55));
  CHECK(!mua::GameLibrary(scratch.games, stranger, FakeDiscs({})).explicit_game());
}

}  // namespace

int main() {
  TestDefinitions();
  TestFramePolicies();
  TestUpscalers();
  TestProfiles();
  TestSaveRoots();
  TestActiveGame();
  TestDiscClassification();
  TestAvailabilityText();
  TestEmptyLibrary();
  TestSourcePackages();
  TestForeignFilesAreNotAnotherGame();
  TestGoldLegacyFolder();
  TestInstalledGamesCanPlay();
  TestBootRouting();
  TestExplicitRoot();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
