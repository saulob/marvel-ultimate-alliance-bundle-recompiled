// The logs folder follows the active log, falls back to <user data>/logs for an AppImage, and is
// created before its first use; only the newest session logs are kept. Run: mua_log_path_tests

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "log_path.h"

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

}  // namespace

int main() {
  std::error_code ec;
  const std::filesystem::path base = std::filesystem::temp_directory_path(ec) / "mua_log_path_tests";
  std::filesystem::remove_all(base, ec);
  std::filesystem::create_directories(base, ec);

  std::printf("the active log's folder wins\n");
  const std::filesystem::path active = base / "active" / "marvel_ultimate_alliance_001.log";
  std::filesystem::create_directories(active.parent_path(), ec);
  CHECK(mua::LogsFolder(base / "port", base / "user", active, true) == active.parent_path());
  CHECK(!std::filesystem::exists(base / "port" / "logs", ec));

  std::printf("without an active log the port folder holds the logs\n");
  CHECK(mua::LogsFolder(base / "port2", base / "user2", {}, false) == base / "port2" / "logs");

  std::printf("an AppImage's first launch creates the logs folder\n");
  const std::filesystem::path created = mua::LogsFolder(base / "port3", base / "user3", {}, true);
  CHECK(created == base / "port3" / "logs");
  CHECK(std::filesystem::is_directory(created, ec));

  std::printf("an AppImage falls back to user data when the port folder cannot hold logs\n");
  std::filesystem::create_directories(base / "port4", ec);
  std::ofstream(base / "port4" / "logs") << "not a directory";
  CHECK(mua::LogsFolder(base / "port4", base / "user4", {}, true) == base / "user4" / "logs");

  std::printf("sequential session logs start at 001 and skip unrelated names\n");
  const std::filesystem::path numbered = base / "numbered";
  std::filesystem::create_directories(numbered, ec);
  CHECK(mua::NextLogFile(numbered, "marvel_ultimate_alliance") == numbered / "marvel_ultimate_alliance_001.log");
  std::ofstream(numbered / "marvel_ultimate_alliance_001.log") << "one";
  std::ofstream(numbered / "marvel_ultimate_alliance_007.log") << "seven";
  std::ofstream(numbered / "marvel_ultimate_alliance_note.log") << "not numbered";
  std::ofstream(numbered / "other_012.log") << "not this port";
  CHECK(mua::NextLogFile(numbered, "marvel_ultimate_alliance") == numbered / "marvel_ultimate_alliance_008.log");
  std::ofstream(numbered / "marvel_ultimate_alliance_009.2.log") << "a rotated part of nine";
  CHECK(mua::NextLogFile(numbered, "marvel_ultimate_alliance") == numbered / "marvel_ultimate_alliance_010.log");
  CHECK(mua::NextLogFile(base / "missing" / "logs", "marvel_ultimate_alliance") ==
        base / "missing" / "logs" / "marvel_ultimate_alliance_001.log");

  std::printf("session log names: numbered sessions and their rotated parts\n");
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_007.log", "marvel_ultimate_alliance") == 7);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_1234.log", "marvel_ultimate_alliance") == 1234);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_007.1.log", "marvel_ultimate_alliance") == 7);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_note.log", "marvel_ultimate_alliance") == 0);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_007.log.discard", "marvel_ultimate_alliance") == 0);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_007.x.log", "marvel_ultimate_alliance") == 0);
  CHECK(mua::SessionLogNumber("marvel_ultimate_alliance_.log", "marvel_ultimate_alliance") == 0);
  CHECK(mua::SessionLogNumber("other_012.log", "marvel_ultimate_alliance") == 0);
  CHECK(mua::SessionLogNumber("updater.log", "marvel_ultimate_alliance") == 0);

  std::printf("only the newest sessions are kept, with their rotated parts, and never the active one\n");
  const std::filesystem::path logs = base / "retention";
  std::filesystem::create_directories(logs, ec);
  for (const char* name : {"marvel_ultimate_alliance_001.log", "marvel_ultimate_alliance_002.log",
                           "marvel_ultimate_alliance_002.1.log", "marvel_ultimate_alliance_003.log",
                           "marvel_ultimate_alliance_010.log", "marvel_ultimate_alliance_011.log",
                           "marvel_ultimate_alliance_note.log", "updater.log", "other_001.log"}) {
    std::ofstream(logs / name) << name;
  }
  const auto old = [&](const std::filesystem::path& active, size_t keep) {
    std::vector<std::string> names;
    for (const std::filesystem::path& file : mua::OldSessionLogs(logs, "marvel_ultimate_alliance", active, keep)) {
      names.push_back(file.filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
  };
  CHECK(old(logs / "marvel_ultimate_alliance_011.log", 2) ==
        (std::vector<std::string>{"marvel_ultimate_alliance_001.log", "marvel_ultimate_alliance_002.1.log",
                                  "marvel_ultimate_alliance_002.log", "marvel_ultimate_alliance_003.log"}));
  // An older active log (an explicit log_file) stays, beside the newest sessions.
  CHECK(old(logs / "marvel_ultimate_alliance_002.log", 2) ==
        (std::vector<std::string>{"marvel_ultimate_alliance_001.log", "marvel_ultimate_alliance_003.log"}));
  CHECK(old(logs / "marvel_ultimate_alliance_011.log", 30).empty());
  CHECK(mua::OldSessionLogs(base / "missing", "marvel_ultimate_alliance", {}, 2).empty());

  std::filesystem::remove_all(base, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
