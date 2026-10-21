// The logs folder follows the active log, falls back to <user data>/logs for an AppImage, and is
// created before its first use. Run: mua_log_path_tests

#include <cstdio>
#include <filesystem>
#include <fstream>

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

  std::ofstream(numbered / "marvel_ultimate_alliance_009.log") << "nine";
  CHECK(mua::NextLogFile(numbered, "marvel_ultimate_alliance") == numbered / "marvel_ultimate_alliance_010.log");
  CHECK(mua::NextLogFile(base / "missing" / "logs", "marvel_ultimate_alliance") ==
        base / "missing" / "logs" / "marvel_ultimate_alliance_001.log");

  std::filesystem::remove_all(base, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
