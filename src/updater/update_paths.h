#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace trg::update {

std::string Utf8Path(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(std::string_view text);

bool NormalizeRelativePath(std::string_view input, std::string& normalized, std::string* reason = nullptr);
bool IsSafeRelativePath(std::string_view path, std::string* reason = nullptr);
bool IsProtectedRelativePath(std::string_view normalized_path);
bool PathIsWithin(const std::filesystem::path& path, const std::filesystem::path& root);
bool IsRegularFile(const std::filesystem::path& path);
// Whether appimage is the AppImage the running program was started from: a regular Marvel Ultimate Alliance AppImage
// outside its own mount, with the running program inside `appdir`.
bool IsRunningAppImage(const std::filesystem::path& appimage, const std::filesystem::path& appdir,
                       const std::filesystem::path& executable);
bool HasReparseAncestor(const std::filesystem::path& path, const std::filesystem::path& root);

}  // namespace trg::update
