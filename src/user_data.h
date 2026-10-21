#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace mua {

enum class UserDataOutcome {
  kPortable,             // <port folder>/user_data
  kMigrated,             // the system folder was moved into <port folder>/user_data
  kMigratedLegacyKept,   // migrated, but the system folder could not be removed
  kBothExist,            // portable data selected; the system folder holds data too and is left alone
  kMigrationFailed,      // the system folder is used for this run and migration is retried at the next start
  kPortableUnavailable,  // <port folder>/user_data cannot be written: the system folder is used for this run
  kExplicit,             // --user_data_root
};

struct UserDataRequest {
  std::filesystem::path port_folder;
  // The root ReXGlue chose before any change: the system folder, or what --user_data_root names.
  std::filesystem::path system;
  bool explicit_root = false;
};

struct UserDataSelection {
  UserDataOutcome outcome = UserDataOutcome::kPortable;
  std::filesystem::path root;
  std::filesystem::path portable;
  std::filesystem::path system;
  std::string detail;  // why a failed step failed
};

// The file system calls a migration makes, replaceable to test failures.
struct UserDataFileOps {
  std::function<void(const std::filesystem::path&, const std::filesystem::path&, std::error_code&)> rename =
      [](const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
        std::filesystem::rename(from, to, ec);
      };
  std::function<void(const std::filesystem::path&, const std::filesystem::path&, std::error_code&)> copy_file =
      [](const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, ec);
      };
  std::function<void(const std::filesystem::path&, std::error_code&)> remove_all =
      [](const std::filesystem::path& path, std::error_code& ec) { std::filesystem::remove_all(path, ec); };
};

// Picks the user data root. Data in the system folder moves into <port folder>/user_data before anything reads or
// writes it; a failed move leaves the system folder untouched and in use.
UserDataSelection SelectUserData(const UserDataRequest& request, const UserDataFileOps& ops = {});

struct UserDataMessage {
  bool warning = false;
  std::string text;
};

UserDataMessage DescribeUserData(const UserDataSelection& selection);

// The About page's explanation of where saves and settings are.
const char* UserDataFolderHelp(UserDataOutcome outcome);

}  // namespace mua
