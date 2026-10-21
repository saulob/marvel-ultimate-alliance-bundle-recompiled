// User data tests: picking the portable user_data root, moving the system folder into it, and failing safe.
// Temporary folders only; nothing under the real Documents or XDG folders is read or written.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>

#include "settings.h"
#include "user_data.h"

namespace fs = std::filesystem;
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

void Section(const char* name) {
  std::printf("%s\n", name);
}

const fs::path kRoot = fs::temp_directory_path() / "mua_user_data_tests";
const fs::path kUnicodeName = u8"perfil_ñ_日本.toml";

void WriteText(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string ReadText(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every file and folder below `root`, with the file contents.
std::map<fs::path, std::string> Snapshot(const fs::path& root) {
  std::map<fs::path, std::string> entries;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    entries[entry.path().lexically_relative(root)] = entry.is_directory() ? "<folder>" : ReadText(entry.path());
  }
  return entries;
}

struct Site {
  fs::path port;
  fs::path system;
  fs::path portable;
  fs::path staging;
  UserDataRequest request() const { return {port, system, false}; }
};

Site FreshSite(const char* name) {
  Site site;
  const fs::path dir = kRoot / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  site.port = dir / "app";
  site.system = dir / "system" / "marvel_ultimate_alliance";
  site.portable = site.port / "user_data";
  site.staging = site.port / "user_data.migrating";
  fs::create_directories(site.port);
  return site;
}

void SeedGameData(const fs::path& root, const std::string& tag) {
  WriteText(root / "0000000000000000" / "415607DA" / "Headers" / "00000002" / "dlc.header", "dlc " + tag);
  WriteText(root / "415607DA" / "live" / "stats.toml", "scores " + tag);
  WriteText(root / "achievements" / "415607DA.toml", "achievements " + tag);
  WriteText(root / "settings.toml", "fullscreen = true  # " + tag);
  WriteText(root / "cache" / "shaders" / "a.bin", std::string(300000, 'x'));
  WriteText(root / kUnicodeName, "unicode " + tag);
  fs::create_directories(root / "empty" / "nested");
}

UserDataFileOps CrossVolume(const fs::path& system) {
  UserDataFileOps ops;
  ops.rename = [system](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (from == system) {
      ec = std::make_error_code(std::errc::cross_device_link);
      return;
    }
    fs::rename(from, to, ec);
  };
  return ops;
}

void CheckNothingLeft(const Site& site) {
  CHECK(!fs::exists(site.staging));
  CHECK(!fs::exists(site.portable / ".write_test"));
}

void TestNothingExists() {
  Section("no system data and no portable data");
  const Site site = FreshSite("nothing");
  const UserDataSelection selection = SelectUserData(site.request());
  CHECK(selection.outcome == UserDataOutcome::kPortable);
  CHECK(selection.root == site.portable);
  CHECK(fs::is_directory(site.portable));
  CHECK(!fs::exists(site.system));
  CheckNothingLeft(site);

  const UserDataSelection again = SelectUserData(site.request());
  CHECK(again.outcome == UserDataOutcome::kPortable);
  CHECK(again.root == site.portable);
}

void TestMigrationByRename() {
  Section("system data moves into user_data");
  const Site site = FreshSite("rename");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  const UserDataSelection selection = SelectUserData(site.request());
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(selection.root == site.portable);
  CHECK(selection.system == site.system);
  CHECK(Snapshot(site.portable) == before);
  CHECK(ReadText(site.portable / "0000000000000000" / "415607DA" / "Headers" / "00000002" / "dlc.header") == "dlc legacy");
  CHECK(ReadText(site.portable / "415607DA" / "live" / "stats.toml") == "scores legacy");
  CHECK(ReadText(site.portable / "achievements" / "415607DA.toml") == "achievements legacy");
  CHECK(ReadText(site.portable / "settings.toml") == "fullscreen = true  # legacy");
  CHECK(ReadText(site.portable / kUnicodeName) == "unicode legacy");
  CHECK(fs::is_directory(site.portable / "empty" / "nested"));
  CHECK(fs::file_size(site.portable / "cache" / "shaders" / "a.bin") == 300000);
  CHECK(!fs::exists(site.system));
  CHECK(fs::is_directory(site.system.parent_path()));
  CheckNothingLeft(site);

  const UserDataSelection again = SelectUserData(site.request());
  CHECK(again.outcome == UserDataOutcome::kPortable);
  CHECK(Snapshot(site.portable) == before);
}

void TestMigrationByCopy() {
  Section("system data on another volume is copied, verified, then removed");
  const Site site = FreshSite("copy");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  int copies = 0;
  UserDataFileOps ops = CrossVolume(site.system);
  ops.copy_file = [&copies](const fs::path& from, const fs::path& to, std::error_code& ec) {
    ++copies;
    fs::copy_file(from, to, fs::copy_options::none, ec);
  };
  const UserDataSelection selection = SelectUserData(site.request(), ops);
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(selection.root == site.portable);
  CHECK(copies == 6);
  CHECK(Snapshot(site.portable) == before);
  CHECK(!fs::exists(site.system));
  CheckNothingLeft(site);
}

void TestStaleStagingIsReplaced() {
  Section("a leftover migration folder is cleared");
  const Site site = FreshSite("stale");
  SeedGameData(site.system, "legacy");
  WriteText(site.staging / "junk.bin", "left by a crashed run");
  const auto before = Snapshot(site.system);

  const UserDataSelection selection = SelectUserData(site.request(), CrossVolume(site.system));
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(Snapshot(site.portable) == before);
  CHECK(!fs::exists(site.system));
  CheckNothingLeft(site);
}

void TestPortableDataWins() {
  Section("portable data is used and the system folder is left alone");
  const Site site = FreshSite("both");
  SeedGameData(site.system, "legacy");
  SeedGameData(site.portable, "portable");
  const auto system_before = Snapshot(site.system);
  const auto portable_before = Snapshot(site.portable);

  for (int launch = 0; launch < 2; ++launch) {
    const UserDataSelection selection = SelectUserData(site.request());
    CHECK(selection.outcome == UserDataOutcome::kBothExist);
    CHECK(selection.root == site.portable);
    CHECK(Snapshot(site.system) == system_before);
    CHECK(Snapshot(site.portable) == portable_before);
  }
  CHECK(ReadText(site.portable / "415607DA" / "live" / "stats.toml") == "scores portable");
  CheckNothingLeft(site);
}

void TestEmptyPortableFolder() {
  Section("an empty user_data folder does not block the migration");
  const Site site = FreshSite("empty_folder");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);
  fs::create_directories(site.portable);

  const UserDataSelection selection = SelectUserData(site.request());
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(Snapshot(site.portable) == before);
  CHECK(!fs::exists(site.system));
}

void TestPortableWithOnlyFolders() {
  Section("a user_data tree without files does not block the migration");
  const Site site = FreshSite("only_folders");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);
  fs::create_directories(site.portable / "cache" / "shaders");
  fs::create_directories(site.portable / "415607DA" / "live");

  const UserDataSelection selection = SelectUserData(site.request(), CrossVolume(site.system));
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(Snapshot(site.portable) == before);
  CHECK(!fs::exists(site.system));
  CheckNothingLeft(site);
}

void CheckFailedMigration(const Site& site, const UserDataSelection& selection,
                          const std::map<fs::path, std::string>& system_before) {
  CHECK(selection.outcome == UserDataOutcome::kMigrationFailed);
  CHECK(selection.root == site.system);
  CHECK(!selection.detail.empty());
  CHECK(Snapshot(site.system) == system_before);
  CHECK(!fs::exists(site.portable));
  CHECK(!fs::exists(site.staging));
}

void TestCopyFailureKeepsSystemData() {
  Section("a copy that fails midway keeps the system folder and activates nothing");
  const Site site = FreshSite("copy_fails");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  int copies = 0;
  UserDataFileOps ops = CrossVolume(site.system);
  ops.copy_file = [&copies](const fs::path& from, const fs::path& to, std::error_code& ec) {
    if (++copies == 4) {
      ec = std::make_error_code(std::errc::no_space_on_device);
      return;
    }
    fs::copy_file(from, to, fs::copy_options::none, ec);
  };
  CheckFailedMigration(site, SelectUserData(site.request(), ops), before);

  Section("the migration is retried at the next start");
  const UserDataSelection retry = SelectUserData(site.request());
  CHECK(retry.outcome == UserDataOutcome::kMigrated);
  CHECK(retry.root == site.portable);
  CHECK(Snapshot(site.portable) == before);
  CHECK(!fs::exists(site.system));
  CheckNothingLeft(site);
}

void TestShortCopyIsRejected() {
  Section("a copy that does not match the original is not activated");
  const Site site = FreshSite("short_copy");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  UserDataFileOps ops = CrossVolume(site.system);
  ops.copy_file = [](const fs::path&, const fs::path& to, std::error_code&) { WriteText(to, "cut"); };
  CheckFailedMigration(site, SelectUserData(site.request(), ops), before);
}

void TestActivationFailureKeepsSystemData() {
  Section("a copy that cannot be activated keeps the system folder");
  const Site site = FreshSite("activation_fails");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  UserDataFileOps ops;
  ops.rename = [](const fs::path&, const fs::path&, std::error_code& ec) {
    ec = std::make_error_code(std::errc::permission_denied);
  };
  CheckFailedMigration(site, SelectUserData(site.request(), ops), before);
}

void TestPortableNameTakenByFile() {
  Section("a file named user_data is never replaced");
  const Site site = FreshSite("file_in_the_way");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);
  WriteText(site.portable, "not a folder");

  const UserDataSelection selection = SelectUserData(site.request());
  CHECK(selection.outcome == UserDataOutcome::kMigrationFailed);
  CHECK(selection.root == site.system);
  CHECK(Snapshot(site.system) == before);
  CHECK(ReadText(site.portable) == "not a folder");
}

void TestLegacyRemovalFailure() {
  Section("a system folder that cannot be removed does not undo the migration");
  const Site site = FreshSite("remove_fails");
  SeedGameData(site.system, "legacy");
  const auto before = Snapshot(site.system);

  UserDataFileOps ops = CrossVolume(site.system);
  ops.remove_all = [&site](const fs::path& path, std::error_code& ec) {
    if (path == site.system) {
      ec = std::make_error_code(std::errc::device_or_resource_busy);
      return;
    }
    fs::remove_all(path, ec);
  };
  const UserDataSelection selection = SelectUserData(site.request(), ops);
  CHECK(selection.outcome == UserDataOutcome::kMigratedLegacyKept);
  CHECK(selection.root == site.portable);
  CHECK(Snapshot(site.portable) == before);
  CHECK(Snapshot(site.system) == before);
  CHECK(!selection.detail.empty());
  CheckNothingLeft(site);
}

void TestPortableUnavailable() {
  Section("an unwritable portable folder falls back to the system path");
  const Site site = FreshSite("unwritable");
  WriteText(site.portable, "not a folder");

  const UserDataSelection selection = SelectUserData(site.request());
  CHECK(selection.outcome == UserDataOutcome::kPortableUnavailable);
  CHECK(selection.root == site.system);
  CHECK(ReadText(site.portable) == "not a folder");
  CHECK(!fs::exists(site.system));

  const Site both = FreshSite("unwritable_both");
  SeedGameData(both.system, "legacy");
  SeedGameData(both.portable, "portable");
  fs::create_directories(both.portable / ".write_test");
  const UserDataSelection blocked = SelectUserData(both.request());
  CHECK(blocked.outcome == UserDataOutcome::kPortableUnavailable);
  CHECK(blocked.root == both.system);
}

void TestExplicitRootIsAuthoritative() {
  Section("--user_data_root is never replaced");
  const Site site = FreshSite("explicit");
  const fs::path chosen = kRoot / "explicit" / "chosen";
  SeedGameData(site.system, "legacy");
  const auto system_before = Snapshot(site.system);

  UserDataRequest request = site.request();
  request.system = chosen;
  request.explicit_root = true;
  const UserDataSelection selection = SelectUserData(request);
  CHECK(selection.outcome == UserDataOutcome::kExplicit);
  CHECK(selection.root == chosen);
  CHECK(!fs::exists(chosen));
  CHECK(!fs::exists(site.portable));

  SeedGameData(chosen, "chosen");
  const auto chosen_before = Snapshot(chosen);
  request.system = site.system;
  const UserDataSelection kept = SelectUserData(request);
  CHECK(kept.outcome == UserDataOutcome::kExplicit);
  CHECK(kept.root == site.system);
  CHECK(Snapshot(site.system) == system_before);
  CHECK(Snapshot(chosen) == chosen_before);
  CHECK(!fs::exists(site.portable));
  CHECK(!fs::exists(site.staging));
}

void TestLinksAreNotFollowed() {
  Section("a link in the system folder is copied as a link and its target is left alone");
  const Site site = FreshSite("links");
  const fs::path outside = kRoot / "links" / "outside";
  WriteText(outside / "keep.bin", "outside data");
  fs::create_directories(site.system);
  std::error_code ec;
  fs::create_directory_symlink(outside, site.system / "link", ec);
  if (ec) {
    std::printf("  (symlinks unavailable: skipped)\n");
    return;
  }

  Section("a folder holding only a link counts as data");
  const UserDataSelection selection = SelectUserData(site.request(), CrossVolume(site.system));
  CHECK(selection.outcome == UserDataOutcome::kMigrated);
  CHECK(fs::is_symlink(site.portable / "link"));
  CHECK(!fs::exists(site.system));
  CHECK(ReadText(outside / "keep.bin") == "outside data");
  CheckNothingLeft(site);
}

void TestPathsFollowTheRoot() {
  Section("settings are found below whichever root is selected");
  const Site site = FreshSite("paths");
  for (const fs::path& root : {site.portable, site.system}) {
    CHECK(PlayerSettingsPath(root) == root / "settings.toml");
  }
}

void TestMessages() {
  Section("log and About page text");
  const Site site = FreshSite("messages");
  UserDataSelection selection;
  selection.root = site.portable;
  selection.portable = site.portable;
  selection.system = site.system;
  selection.detail = "disk full";

  const UserDataOutcome all[] = {UserDataOutcome::kPortable,           UserDataOutcome::kMigrated,
                                 UserDataOutcome::kMigratedLegacyKept, UserDataOutcome::kBothExist,
                                 UserDataOutcome::kMigrationFailed,    UserDataOutcome::kPortableUnavailable,
                                 UserDataOutcome::kExplicit};
  for (const UserDataOutcome outcome : all) {
    selection.outcome = outcome;
    const UserDataMessage message = DescribeUserData(selection);
    CHECK(message.text.starts_with("User data: "));
    CHECK(message.text.find('\n') == std::string::npos);
    CHECK(message.warning == (outcome != UserDataOutcome::kPortable && outcome != UserDataOutcome::kMigrated &&
                              outcome != UserDataOutcome::kExplicit));
    CHECK(*UserDataFolderHelp(outcome) != '\0');
  }

  selection.outcome = UserDataOutcome::kMigrated;
  const std::string migrated = DescribeUserData(selection).text;
  CHECK(migrated.find("user_data") != std::string::npos);
  CHECK(migrated.find(site.system.string()) == std::string::npos);
  CHECK(migrated.find(site.portable.string()) == std::string::npos);
  CHECK(std::string(UserDataFolderHelp(UserDataOutcome::kPortable)).find("portable user_data folder") !=
        std::string::npos);
  CHECK(std::string(UserDataFolderHelp(UserDataOutcome::kExplicit)).find("--user_data_root") != std::string::npos);
  CHECK(std::string(UserDataFolderHelp(UserDataOutcome::kPortableUnavailable)) !=
        UserDataFolderHelp(UserDataOutcome::kPortable));
  CHECK(std::string(UserDataFolderHelp(UserDataOutcome::kMigrationFailed)) !=
        UserDataFolderHelp(UserDataOutcome::kPortable));
}

}  // namespace

int main() {
  std::error_code ec;
  fs::remove_all(kRoot, ec);
  TestNothingExists();
  TestMigrationByRename();
  TestMigrationByCopy();
  TestStaleStagingIsReplaced();
  TestPortableDataWins();
  TestEmptyPortableFolder();
  TestPortableWithOnlyFolders();
  TestCopyFailureKeepsSystemData();
  TestShortCopyIsRejected();
  TestActivationFailureKeepsSystemData();
  TestPortableNameTakenByFile();
  TestLegacyRemovalFailure();
  TestPortableUnavailable();
  TestExplicitRootIsAuthoritative();
  TestLinksAreNotFollowed();
  TestPathsFollowTheRoot();
  TestMessages();
  fs::remove_all(kRoot, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
