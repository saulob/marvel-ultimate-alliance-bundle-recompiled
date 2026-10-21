// Crash reporting must target the log of the current run, never the newest log in the folder:
// ActiveLogFile follows the logger's file sink (or a configured log_file), and the run's own
// sequential log is created by the SDK. Run: mua_active_log_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "active_log.h"

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

void Touch(const std::filesystem::path& file) {
  std::ofstream(file, std::ios::binary | std::ios::app);
}

}  // namespace

int main() {
  std::error_code ec;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path(ec) / "mua_active_log_tests";
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  Touch(dir / "mua_active_log_test_001.log");
  Touch(dir / "mua_active_log_test_002.log");

  rex::LogConfig config;
  config.app_name = "mua_active_log_test";
  config.log_dir = dir.string();
  rex::InitLogging(config);

  const std::filesystem::path active = dir / "mua_active_log_test_003.log";
  std::printf("active log is the run's own file, not the newest existing one\n");
  const std::filesystem::path detected = mua::ActiveLogFile();
  std::printf("  active log: %s\n", detected.string().c_str());
  CHECK(detected == active);
  CHECK(detected != dir / "mua_active_log_test_002.log");
  CHECK(std::filesystem::exists(active));

  std::printf("a configured log_file wins over the sequential naming\n");
  const std::filesystem::path configured = dir / "configured.log";
  rex::cvar::SetFlagByName("log_file", configured.string());
  CHECK(mua::ActiveLogFile() == configured);

  rex::cvar::SetFlagByName("log_file", "");
  CHECK(mua::ActiveLogFile() == active);

  std::filesystem::remove_all(dir, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
