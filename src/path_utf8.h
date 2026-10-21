#pragma once

#include <filesystem>
#include <string>

namespace mua {

// TRG Launcher takes UTF-8 paths.
inline std::string Utf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

inline std::filesystem::path PathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

}  // namespace mua
