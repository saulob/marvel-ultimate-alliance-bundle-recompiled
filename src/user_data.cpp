#include "user_data.h"

#include <cstdint>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace mua {
namespace {

constexpr char kPortableFolder[] = "user_data";
constexpr char kStagingSuffix[] = ".migrating";

std::string Reason(const char* step, const std::error_code& ec) {
  return ec ? std::string(step) + ": " + ec.message() : std::string(step);
}

// True for anything but folders; also when the folder cannot be read, so that it is never mistaken for empty.
bool HoldsFiles(const fs::path& folder) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code entry_ec;
    if (it->symlink_status(entry_ec).type() != fs::file_type::directory || entry_ec) {
      return true;
    }
  }
  return bool(ec);
}

bool CanWrite(const fs::path& folder) {
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec || !fs::is_directory(folder, ec)) {
    return false;
  }
  const fs::path probe = folder / ".write_test";
  {
    std::ofstream file(probe, std::ios::binary | std::ios::trunc);
    file << 'x';
    file.flush();
    if (!file) {
      return false;
    }
  }
  fs::remove(probe, ec);
  return true;
}

struct TreeEntry {
  fs::file_type type = fs::file_type::none;
  std::uintmax_t size = 0;
  bool operator==(const TreeEntry&) const = default;
};
using TreeListing = std::map<fs::path, TreeEntry>;

bool ListTree(const fs::path& root, TreeListing& listing, std::error_code& ec) {
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::file_status status = it->symlink_status(ec);
    if (ec) {
      return false;
    }
    TreeEntry entry{status.type(), 0};
    if (fs::is_regular_file(status)) {
      entry.size = it->file_size(ec);
      if (ec) {
        return false;
      }
    }
    listing[it->path().lexically_relative(root)] = entry;
  }
  return !ec;
}

bool CopyTree(const fs::path& from, const fs::path& to, const UserDataFileOps& ops, std::error_code& ec) {
  fs::create_directory(to, ec);
  if (ec) {
    return false;
  }
  for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path target = to / it->path().filename();
    const fs::file_status status = it->symlink_status(ec);
    if (!ec) {
      if (fs::is_directory(status)) {
        CopyTree(it->path(), target, ops, ec);
      } else if (fs::is_symlink(status)) {
        fs::copy_symlink(it->path(), target, ec);
      } else if (fs::is_regular_file(status)) {
        ops.copy_file(it->path(), target, ec);
      } else {
        ec = std::make_error_code(std::errc::not_supported);
      }
    }
    // increment() clears the code, so a failed entry has to stop the loop here.
    if (ec) {
      return false;
    }
  }
  return !ec;
}

std::string CopyVerified(const fs::path& from, const fs::path& to, const UserDataFileOps& ops) {
  std::error_code ec;
  if (!CopyTree(from, to, ops, ec)) {
    return Reason("cannot copy the data", ec);
  }
  TreeListing original;
  TreeListing copy;
  if (!ListTree(from, original, ec) || !ListTree(to, copy, ec)) {
    return Reason("cannot verify the copy", ec);
  }
  return original == copy ? std::string() : "the copy does not match the original";
}

// Empty when `portable` holds the complete data. Only a direct move touches the system folder.
std::string MoveIntoPlace(const fs::path& system, const fs::path& portable, const UserDataFileOps& ops) {
  std::error_code ec;
  const fs::file_status existing = fs::symlink_status(portable, ec);
  if (fs::exists(existing)) {
    if (!fs::is_directory(existing)) {
      return "user_data exists and is not a folder";
    }
    ops.remove_all(portable, ec);
    if (ec) {
      return Reason("cannot clear the empty user_data folder", ec);
    }
  }
  ops.rename(system, portable, ec);
  if (!ec) {
    return {};
  }

  fs::path staging = portable;
  staging += kStagingSuffix;
  ops.remove_all(staging, ec);
  if (ec) {
    return Reason("cannot clear an earlier migration attempt", ec);
  }
  std::string failure = CopyVerified(system, staging, ops);
  if (failure.empty()) {
    ops.rename(staging, portable, ec);
    if (!ec) {
      return {};
    }
    failure = Reason("cannot activate the copied data", ec);
  }
  ops.remove_all(staging, ec);
  return failure;
}

}  // namespace

UserDataSelection SelectUserData(const UserDataRequest& request, const UserDataFileOps& ops) {
  UserDataSelection selection;
  selection.portable = request.port_folder / kPortableFolder;
  selection.system = request.system;
  selection.root = request.system;
  if (request.explicit_root) {
    selection.outcome = UserDataOutcome::kExplicit;
    return selection;
  }

  std::error_code ec;
  const bool system_has_data = fs::is_directory(request.system, ec) && HoldsFiles(request.system);
  const bool portable_has_data = fs::is_directory(selection.portable, ec) && HoldsFiles(selection.portable);

  if (system_has_data && !portable_has_data) {
    selection.detail = MoveIntoPlace(request.system, selection.portable, ops);
    if (!selection.detail.empty()) {
      selection.outcome = UserDataOutcome::kMigrationFailed;
      return selection;
    }
    selection.root = selection.portable;
    selection.outcome = UserDataOutcome::kMigrated;
    if (fs::exists(request.system, ec)) {
      ops.remove_all(request.system, ec);
      if (ec || fs::exists(request.system, ec)) {
        selection.outcome = UserDataOutcome::kMigratedLegacyKept;
        selection.detail = Reason("cannot remove the old folder", ec);
      }
    }
    return selection;
  }

  if (!CanWrite(selection.portable)) {
    selection.outcome = UserDataOutcome::kPortableUnavailable;
    return selection;
  }
  selection.root = selection.portable;
  selection.outcome = system_has_data ? UserDataOutcome::kBothExist : UserDataOutcome::kPortable;
  return selection;
}

UserDataMessage DescribeUserData(const UserDataSelection& selection) {
  switch (selection.outcome) {
    case UserDataOutcome::kPortable:
      return {false, "User data: portable path selected"};
    case UserDataOutcome::kMigrated:
      return {false, "User data: migrated legacy data into user_data"};
    case UserDataOutcome::kMigratedLegacyKept:
      return {true, "User data: migrated legacy data into user_data; the old folder was not removed (" +
                        selection.detail + ")"};
    case UserDataOutcome::kBothExist:
      return {true, "User data: portable and legacy data both exist; portable data selected"};
    case UserDataOutcome::kMigrationFailed:
      return {true, "User data: migrating legacy data into user_data failed (" + selection.detail +
                        "); using the legacy folder for this run"};
    case UserDataOutcome::kPortableUnavailable:
      return {true, "User data: portable path unavailable; using the legacy folder for this run"};
    case UserDataOutcome::kExplicit:
      return {false, "User data: --user_data_root selected"};
  }
  return {};
}

const char* UserDataFolderHelp(UserDataOutcome outcome) {
  switch (outcome) {
    case UserDataOutcome::kMigrationFailed:
      return "Saves, achievements and settings are stored in the previous system folder, because "
             "moving them into the portable user_data folder failed. It is tried again at the next start";
    case UserDataOutcome::kPortableUnavailable:
      return "Saves, achievements and settings are stored in the system user data folder, because "
             "the portable user_data folder is not writable";
    case UserDataOutcome::kExplicit:
      return "Saves, achievements and settings are stored in the folder given by --user_data_root";
    default:
      return "Saves, achievements and settings are stored in the portable user_data folder";
  }
}

}  // namespace mua
