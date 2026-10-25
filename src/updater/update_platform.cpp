#include "updater/update_platform.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <unistd.h>

extern char** environ;
#endif

#include "updater/sha256.h"
#include "updater/update_paths.h"

namespace trg::update {

unsigned long CurrentProcessId() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return static_cast<unsigned long>(getpid());
#endif
}

#if defined(_WIN32)

std::vector<std::string> CurrentProcessArguments() {
  int count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) {
    return {};
  }
  std::vector<std::string> result;
  for (int i = 1; i < count; ++i) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
      result.emplace_back();
      continue;
    }
    std::string text(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, arguments[i], -1, text.data(), length, nullptr, nullptr);
    text.resize(size_t(length - 1));
    result.push_back(std::move(text));
  }
  LocalFree(arguments);
  return result;
}

std::filesystem::path CurrentExecutablePath() {
  std::vector<wchar_t> buffer(1024);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), length));
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool HasOriginalAppImage(std::filesystem::path&) {
  return false;
}

bool LaunchApplication(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                       const std::filesystem::path& working_directory, std::string& error) {
  auto quote = [](const std::wstring& argument) {
    if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
      return argument;
    }
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t c : argument) {
      if (c == L'\\') {
        ++backslashes;
      } else if (c == L'"') {
        quoted.append(backslashes * 2 + 1, L'\\');
        quoted += L'"';
        backslashes = 0;
      } else {
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += c;
      }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'"';
    return quoted;
  };

  std::wstring command_line = quote(executable.wstring());
  for (const std::string& argument : arguments) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), int(argument.size()), nullptr, 0);
    std::wstring wide(size_t(std::max(length, 0)), L'\0');
    if (length > 0) {
      MultiByteToWideChar(CP_UTF8, 0, argument.c_str(), int(argument.size()), wide.data(), length);
    }
    command_line += L' ';
    command_line += wide.empty() ? L"\"\"" : quote(wide);
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      working_directory.c_str(), &startup, &process)) {
    error = "Could not start the program (" + std::to_string(GetLastError()) + ").";
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

bool LaunchUpdateHelper(const std::filesystem::path& helper, const std::filesystem::path& job_file,
                        std::string& error) {
  if (!IsRegularFile(helper) || !IsRegularFile(job_file)) {
    error = "The update helper is missing from this installation.";
    return false;
  }
  return LaunchApplication(helper, {"--job", Utf8Path(job_file)}, job_file.parent_path(), error);
}

bool WaitForProcess(unsigned long pid, int timeout_ms, std::string& error) {
  HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
  if (!process) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
      return true;
    }
    error = "The launcher process could not be opened.";
    return false;
  }
  const DWORD result = WaitForSingleObject(process, DWORD(timeout_ms));
  CloseHandle(process);
  if (result == WAIT_OBJECT_0) {
    return true;
  }
  error = result == WAIT_TIMEOUT ? "The launcher did not exit in time." : "Waiting for the launcher failed.";
  return false;
}

bool ApplyAppImageUpdate(const std::filesystem::path&, const std::filesystem::path&,
                         const std::vector<std::string>&, std::string& error) {
  error = "Automatic updates are only available in the Linux AppImage build.";
  return false;
}

#else

std::vector<std::string> CurrentProcessArguments() {
  std::vector<std::string> result;
  FILE* file = std::fopen("/proc/self/cmdline", "rb");
  if (!file) {
    return result;
  }
  std::vector<char> data;
  char buffer[4096];
  for (size_t count; (count = std::fread(buffer, 1, sizeof(buffer), file)) > 0;) {
    data.insert(data.end(), buffer, buffer + count);
  }
  std::fclose(file);
  size_t start = 0;
  bool first = true;
  for (size_t i = 0; i <= data.size(); ++i) {
    if (i == data.size() || data[i] == '\0') {
      if (!first) {
        result.emplace_back(data.data() + start, i - start);
      }
      first = false;
      start = i + 1;
    }
  }
  return result;
}

std::filesystem::path CurrentExecutablePath() {
  std::vector<char> buffer(1024);
  for (;;) {
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length <= 0) {
      return {};
    }
    if (size_t(length) < buffer.size()) {
      return std::filesystem::path(std::string(buffer.data(), size_t(length)));
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool HasOriginalAppImage(std::filesystem::path& path) {
  const char* value = std::getenv("APPIMAGE");
  if (!value || !*value) {
    return false;
  }
  std::error_code ec;
  const std::filesystem::path candidate = std::filesystem::weakly_canonical(std::filesystem::path(value), ec);
  if (ec || !IsRegularFile(candidate)) {
    return false;
  }
  path = candidate;
  return true;
}

bool LaunchApplication(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
                       const std::filesystem::path&, std::string& error) {
  std::vector<std::string> storage;
  storage.reserve(arguments.size() + 1);
  storage.push_back(Utf8Path(executable));
  storage.insert(storage.end(), arguments.begin(), arguments.end());
  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (std::string& argument : storage) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int result = posix_spawn(&pid, storage.front().c_str(), nullptr, nullptr, argv.data(), environ);
  if (result != 0) {
    error = "Could not start the program (" + std::to_string(result) + ").";
    return false;
  }
  return true;
}

bool LaunchUpdateHelper(const std::filesystem::path&, const std::filesystem::path&, std::string& error) {
  error = "The update helper is only used on Windows.";
  return false;
}

bool WaitForProcess(unsigned long pid, int timeout_ms, std::string& error) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (kill(pid_t(pid), 0) != 0 && errno == ESRCH) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  error = "The launcher did not exit in time.";
  return false;
}

bool ApplyAppImageUpdate(const std::filesystem::path& downloaded, const std::filesystem::path& appimage,
                         const std::vector<std::string>& arguments, std::string& error) {
  error.clear();
  if (!IsRegularFile(downloaded) || !IsRegularFile(appimage)) {
    error = "The downloaded AppImage or the installed AppImage is missing.";
    return false;
  }
  if (downloaded == appimage) {
    error = "The update was downloaded over the installed AppImage.";
    return false;
  }

  std::error_code ec;
  std::filesystem::path temporary;
  std::filesystem::path backup;
  bool found = false;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const std::string suffix =
        ".trg-update-" + std::to_string(CurrentProcessId()) + "-" + std::to_string(attempt);
    temporary = appimage;
    temporary += suffix + ".tmp";
    backup = appimage;
    backup += suffix + ".backup";
    if (!std::filesystem::exists(temporary, ec) && !ec && !std::filesystem::exists(backup, ec) && !ec) {
      found = true;
      break;
    }
    ec.clear();
  }
  if (!found) {
    error = "Cannot find a free temporary name next to the installed AppImage.";
    return false;
  }

  std::filesystem::copy_file(downloaded, temporary, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    error = "Cannot write the updated AppImage next to the installed one.";
    return false;
  }

  struct stat status {};
  if (stat(appimage.c_str(), &status) != 0) {
    std::filesystem::remove(temporary, ec);
    error = "Cannot read the installed AppImage.";
    return false;
  }
  if (chmod(temporary.c_str(), status.st_mode | S_IXUSR | S_IXGRP | S_IXOTH) != 0) {
    std::filesystem::remove(temporary, ec);
    error = "Cannot make the updated AppImage executable.";
    return false;
  }
  if (const int descriptor = open(temporary.c_str(), O_RDONLY); descriptor >= 0) {
    fsync(descriptor);
    close(descriptor);
  }

  std::string hash_error;
  const std::string source_hash = Sha256FileHex(downloaded, hash_error);
  const std::string temporary_hash = Sha256FileHex(temporary, hash_error);
  if (source_hash.empty() || source_hash != temporary_hash) {
    std::filesystem::remove(temporary, ec);
    error = "The updated AppImage could not be verified.";
    return false;
  }

  bool linked = link(appimage.c_str(), backup.c_str()) == 0;
  if (!linked) {
    std::filesystem::copy_file(appimage, backup, std::filesystem::copy_options::overwrite_existing, ec);
    linked = !ec;
  }
  if (!linked) {
    std::filesystem::remove(temporary, ec);
    error = "Cannot back up the installed AppImage.";
    return false;
  }

  if (rename(temporary.c_str(), appimage.c_str()) != 0) {
    std::filesystem::remove(temporary, ec);
    std::filesystem::remove(backup, ec);
    error = "Cannot replace the installed AppImage.";
    return false;
  }

  const std::filesystem::path folder = appimage.parent_path();
  if (const int descriptor = open(folder.c_str(), O_RDONLY | O_DIRECTORY); descriptor >= 0) {
    fsync(descriptor);
    close(descriptor);
  }

  std::string launch_error;
  if (!LaunchApplication(appimage, arguments, folder, launch_error)) {
    if (rename(backup.c_str(), appimage.c_str()) != 0) {
      error = launch_error + " The previous AppImage could not be restored.";
    } else {
      error = launch_error;
    }
    return false;
  }
  std::filesystem::remove(backup, ec);
  return true;
}

#endif

}  // namespace trg::update
