#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace mua {

// The next free <name>_NNN.log in `folder`, which is created. Empty when it cannot be created.
std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name);

// The session a log file belongs to: NNN of <name>_NNN.log or of a rotated part <name>_NNN.K.log; 0 for any other file.
int SessionLogNumber(std::string_view file_name, std::string_view name);

// The session logs in `folder`, with their rotated parts, older than its newest `keep` sessions. The active log's
// session is never among them.
std::vector<std::filesystem::path> OldSessionLogs(const std::filesystem::path& folder, std::string_view name,
                                                  const std::filesystem::path& active, size_t keep);

// The folder holding the logs users send: the active log's folder, or <port folder>/logs with the
// AppImage fallback to <user data>/logs.
std::filesystem::path LogsFolder(const std::filesystem::path& port_folder,
                                 const std::filesystem::path& user_data_root,
                                 const std::filesystem::path& active_log, bool appimage);

}  // namespace mua
