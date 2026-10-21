#pragma once

#include <filesystem>
#include <string_view>

namespace mua {

// The next free <name>_NNN.log in `folder`, which is created. Empty when it cannot be created.
std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name);

// The folder holding the logs users send: the active log's folder, or <port folder>/logs with the
// AppImage fallback to <user data>/logs.
std::filesystem::path LogsFolder(const std::filesystem::path& port_folder,
                                 const std::filesystem::path& user_data_root,
                                 const std::filesystem::path& active_log, bool appimage);

}  // namespace mua
