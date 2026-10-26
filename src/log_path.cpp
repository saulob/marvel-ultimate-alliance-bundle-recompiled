#include "log_path.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <set>
#include <string>
#include <system_error>
#include <utility>

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
  int last = 0;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    last = std::max(last, SessionLogNumber(it->path().filename().string(), name));
  }
  char number[16];
  std::snprintf(number, sizeof(number), "_%03d.log", last + 1);
  return folder / (std::string(name) + number);
}

int SessionLogNumber(std::string_view file_name, std::string_view name) {
  constexpr std::string_view kSuffix = ".log";
  if (file_name.size() <= name.size() + 1 + kSuffix.size() || !file_name.starts_with(name) ||
      file_name[name.size()] != '_' || !file_name.ends_with(kSuffix)) {
    return 0;
  }
  std::string_view number = file_name.substr(name.size() + 1, file_name.size() - name.size() - 1 - kSuffix.size());
  if (const size_t dot = number.find('.'); dot != std::string_view::npos) {
    if (!IsNumber(number.substr(dot + 1))) {
      return 0;
    }
    number = number.substr(0, dot);
  }
  int session = 0;
  if (!IsNumber(number) || std::from_chars(number.data(), number.data() + number.size(), session).ec != std::errc()) {
    return 0;
  }
  return session;
}

std::vector<std::filesystem::path> OldSessionLogs(const std::filesystem::path& folder, std::string_view name,
                                                  const std::filesystem::path& active, size_t keep) {
  const int active_session = SessionLogNumber(active.filename().string(), name);
  std::vector<std::pair<int, std::filesystem::path>> logs;
  std::set<int> sessions;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code type_ec;
    if (!it->is_regular_file(type_ec)) {
      continue;
    }
    if (const int session = SessionLogNumber(it->path().filename().string(), name); session != 0) {
      logs.emplace_back(session, it->path());
      sessions.insert(session);
    }
  }
  std::set<int> kept;
  for (auto it = sessions.rbegin(); it != sessions.rend() && kept.size() < keep; ++it) {
    kept.insert(*it);
  }
  std::vector<std::filesystem::path> old;
  for (const auto& [session, path] : logs) {
    if (session != active_session && !kept.contains(session)) {
      old.push_back(path);
    }
  }
  return old;
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
