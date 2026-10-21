#include "log_path.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>

namespace mua {
namespace {

bool IsNumber(std::string_view text) {
  return !text.empty() && text.size() <= 9 &&
         std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::filesystem::path NextLogFile(const std::filesystem::path& folder, std::string_view name) {
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  if (!std::filesystem::is_directory(folder, ec)) {
    return {};
  }
  const std::string prefix = std::string(name) + "_";
  int last = 0;
  for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
    const std::string file = entry.path().filename().string();
    if (!file.starts_with(prefix) || !file.ends_with(".log")) {
      continue;
    }
    const std::string_view number =
        std::string_view(file).substr(prefix.size(), file.size() - prefix.size() - sizeof(".log") + 1);
    if (IsNumber(number)) {
      last = std::max(last, std::atoi(std::string(number).c_str()));
    }
  }
  char number[16];
  std::snprintf(number, sizeof(number), "%03d.log", last + 1);
  return folder / (prefix + number);
}

std::filesystem::path LogsFolder(const std::filesystem::path& port_folder,
                                 const std::filesystem::path& user_data_root,
                                 const std::filesystem::path& active_log, bool appimage) {
  if (!active_log.empty()) {
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(active_log, ec);
    return (ec ? active_log : absolute).parent_path();
  }
  const std::filesystem::path portable = port_folder / "logs";
  if (!appimage) {
    return portable;
  }
  std::error_code ec;
  std::filesystem::create_directories(portable, ec);
  if (std::filesystem::is_directory(portable, ec)) {
    return portable;
  }
  return user_data_root / "logs";
}

}  // namespace mua
