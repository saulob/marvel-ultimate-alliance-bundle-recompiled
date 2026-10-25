#pragma once

#include <functional>
#include <string>

#include "updater/update_job.h"

namespace trg::update {

enum class InstallStatus {
  kSuccess,
  kInvalidJob,
  kBackupFailed,
  kInstallFailedRolledBack,
  kInstallFailedRollbackFailed,
  kRestartFailedRolledBack,
  kRestartFailedRollbackFailed,
};

struct InstallOutcome {
  InstallStatus status = InstallStatus::kInvalidJob;
  std::string error;
};

struct InstallOptions {
  std::function<bool(const std::filesystem::path& from, const std::filesystem::path& to)> replace;
};

bool ValidateInstallJob(const UpdateJob& job, std::string& error);
InstallOutcome InstallFiles(const UpdateJob& job, const InstallOptions& options, bool restart);

}  // namespace trg::update
