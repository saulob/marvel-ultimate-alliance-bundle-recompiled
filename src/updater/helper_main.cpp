#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "updater/install_transaction.h"
#include "updater/update_job.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"

namespace {

enum ExitCode {
  kSuccess = 0,
  kBadJob = 2,
  kParentWaitFailed = 3,
  kInvalidJob = 4,
  kBackupFailed = 5,
  kInstallFailedRolledBack = 6,
  kRestartFailedRolledBack = 7,
  kInstallFailedRollbackFailed = 8,
  kRestartFailedRollbackFailed = 9,
};

ExitCode CodeFor(trg::update::InstallStatus status) {
  using trg::update::InstallStatus;
  switch (status) {
    case InstallStatus::kSuccess:
      return kSuccess;
    case InstallStatus::kInvalidJob:
      return kInvalidJob;
    case InstallStatus::kBackupFailed:
      return kBackupFailed;
    case InstallStatus::kInstallFailedRolledBack:
      return kInstallFailedRolledBack;
    case InstallStatus::kInstallFailedRollbackFailed:
      return kInstallFailedRollbackFailed;
    case InstallStatus::kRestartFailedRolledBack:
      return kRestartFailedRolledBack;
    case InstallStatus::kRestartFailedRollbackFailed:
      return kRestartFailedRollbackFailed;
  }
  return kInvalidJob;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (!arguments) {
    return kBadJob;
  }
  std::filesystem::path job_path;
  for (int i = 1; i + 1 < argument_count; ++i) {
    if (std::wstring_view(arguments[i]) == L"--job") {
      job_path = std::filesystem::path(arguments[i + 1]);
      ++i;
    }
  }
  LocalFree(arguments);
  if (job_path.empty()) {
    return kBadJob;
  }

  std::ofstream log(job_path.parent_path() / "updater.log", std::ios::binary | std::ios::app);
  auto write = [&log](std::string_view text) {
    log << text << '\n';
    log.flush();
  };

  std::string error;
  const std::optional<trg::update::UpdateJob> job = trg::update::ReadUpdateJob(job_path, error);
  if (!job) {
    write(error);
    return kBadJob;
  }
  if (!job->log_file.empty()) {
    log.close();
    log.open(job->log_file, std::ios::binary | std::ios::app);
  }
  write("update started");
  if (!trg::update::WaitForProcess(job->parent_pid, 120000, error)) {
    write(error);
    return kParentWaitFailed;
  }
  write("launcher exited");

  const trg::update::InstallOutcome outcome = trg::update::InstallFiles(*job, {}, true);
  if (outcome.status == trg::update::InstallStatus::kSuccess) {
    write("update installed and restarted");
    std::error_code ec;
    std::filesystem::remove(job_path, ec);
  } else {
    write(outcome.error);
    std::string recovery;
    if (trg::update::LaunchApplication(job->restart_executable, job->restart_arguments, job->install_dir,
                                       recovery)) {
      write("previous launcher restarted");
    } else {
      write("could not restart the previous launcher: " + recovery);
    }
  }
  return CodeFor(outcome.status);
}
