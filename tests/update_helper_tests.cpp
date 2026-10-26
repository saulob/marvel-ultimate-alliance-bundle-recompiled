// Real trg-updater flow from a temporary helper directory: imports, parent wait, install, restart quoting, child
// environment cleanup and cleanup. Run: update_helper_tests [path to trg-updater.exe]

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

class TempDir {
 public:
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("Marvel Ultimate Alliance Update Test " + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

constexpr const wchar_t* kUpdateTestVariables[] = {
    L"MUA_UPDATE_TEST", L"MUA_UPDATE_TEST_CURRENT_VERSION", L"MUA_UPDATE_TEST_RELEASE_JSON",
    L"MUA_UPDATE_TEST_ASSET_SHA256"};

class ScopedEnvironment {
 public:
  ScopedEnvironment() {
    for (const wchar_t* name : kUpdateTestVariables) {
      old_.push_back(Read(name));
      SetEnvironmentVariableW(name, L"test-value");
    }
  }
  ~ScopedEnvironment() {
    for (size_t i = 0; i < old_.size(); ++i) {
      SetEnvironmentVariableW(kUpdateTestVariables[i], old_[i] ? old_[i]->c_str() : nullptr);
    }
  }

 private:
  static std::optional<std::wstring> Read(const wchar_t* name) {
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
      return std::nullopt;
    }
    std::wstring value(size_t(length), L'\0');
    GetEnvironmentVariableW(name, value.data(), length);
    value.resize(size_t(length - 1));
    return value;
  }

  std::vector<std::optional<std::wstring>> old_;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), std::streamsize(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::wstring Quote(const std::wstring& argument) {
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
}

bool RunHelper(const std::filesystem::path& helper, const std::filesystem::path& job,
               const std::filesystem::path& working_directory, DWORD& exit_code, std::string& error) {
  std::wstring command = Quote(helper.wstring()) + L" --job " + Quote(job.wstring());
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      working_directory.c_str(), &startup, &process)) {
    error = "CreateProcessW failed (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  WaitForSingleObject(process.hProcess, 30000);
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

bool WaitForMarker(const std::filesystem::path& path) {
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (std::filesystem::exists(path)) {
      const std::string text = ReadFile(path);
      if (text.find("done=1") != std::string::npos) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

std::vector<std::string> ImportedLibraries(const std::filesystem::path& executable) {
  std::vector<std::string> names;
  const std::string image = ReadFile(executable);
  const auto* base = reinterpret_cast<const unsigned char*>(image.data());
  if (image.size() < sizeof(IMAGE_DOS_HEADER)) {
    return names;
  }
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.size()) {
    return names;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
  auto to_offset = [&](DWORD rva) -> size_t {
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
      const IMAGE_SECTION_HEADER& section = sections[i];
      if (rva >= section.VirtualAddress && rva < section.VirtualAddress + section.Misc.VirtualSize) {
        return size_t(section.PointerToRawData + (rva - section.VirtualAddress));
      }
    }
    return image.size();
  };
  for (size_t at = to_offset(imports.VirtualAddress); imports.Size && at + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= image.size();
       at += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
    const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + at);
    if (descriptor->Name == 0) {
      break;
    }
    const size_t name = to_offset(descriptor->Name);
    if (name < image.size()) {
      names.emplace_back(image.c_str() + name);
    }
  }
  return names;
}

bool IsSystemLibrary(std::string name) {
  for (char& c : name) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  static const char* kSystem[] = {"kernel32.dll", "shell32.dll", "user32.dll", "advapi32.dll", "ntdll.dll",
                                  "shlwapi.dll", "ole32.dll", "bcrypt.dll"};
  if (name.rfind("api-ms-win-", 0) == 0) {
    return true;
  }
  for (const char* system : kSystem) {
    if (name == system) {
      return true;
    }
  }
  return false;
}

bool SameFile(const std::filesystem::path& left, const std::filesystem::path& right) {
  return ReadFile(left) == ReadFile(right);
}

std::filesystem::path CurrentExecutable() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

std::string Utf8(const wchar_t* text) {
  const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) {
    return {};
  }
  std::string result(size_t(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
  result.resize(size_t(length - 1));
  return result;
}

// --restart-marker [--hold <ms>] <output> [args...]: the stand-in parent and the program the helper restarts.
int RunRestartMarker() {
  int count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (!arguments) {
    return 2;
  }
  const bool hold = count >= 5 && std::wstring_view(arguments[2]) == L"--hold";
  const int output_index = hold ? 4 : 2;
  if (count <= output_index) {
    LocalFree(arguments);
    return 2;
  }
  std::ofstream out(std::filesystem::path(arguments[output_index]), std::ios::binary | std::ios::trunc);
  if (!out) {
    LocalFree(arguments);
    return 3;
  }
  out << "argc=" << count - 1 << '\n';
  for (int i = 2; i < count; ++i) {
    out << "arg" << i - 1 << '=' << Utf8(arguments[i]) << '\n';
  }
  for (const wchar_t* name : kUpdateTestVariables) {
    out << "env_" << Utf8(name) << '=' << (GetEnvironmentVariableW(name, nullptr, 0) > 0 ? "present" : "absent") << '\n';
  }
  out << "done=1\n";
  out.close();
  if (hold) {
    Sleep(static_cast<DWORD>(std::wcstoul(arguments[3], nullptr, 10)));
  }
  LocalFree(arguments);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--restart-marker") {
    return RunRestartMarker();
  }
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  const std::filesystem::path updater =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(MUA_UPDATER_EXE);
  ScopedEnvironment environment;
  TempDir temp;
  const std::filesystem::path helper_dir = temp.path / "helper dir";
  const std::filesystem::path helper = helper_dir / "trg-updater.exe";
  const std::filesystem::path standalone = temp.path / "standalone dir";
  const std::filesystem::path install = temp.path / "install dir";
  const std::filesystem::path staging = temp.path / "staging dir";
  const std::filesystem::path backup = temp.path / "backup dir";
  const std::filesystem::path workspace = temp.path / "update workspace";
  const std::filesystem::path manifest = workspace / "manifest.txt";
  const std::filesystem::path job = workspace / "job.txt";
  const std::filesystem::path log = workspace / "updater.log";
  const std::filesystem::path marker_output = temp.path / "marker output.txt";
  const std::filesystem::path parent_output = temp.path / "parent output.txt";
  const std::filesystem::path marker = CurrentExecutable();
  const std::filesystem::path program = marker.filename();

  std::error_code ec;
  std::filesystem::create_directories(helper_dir, ec);
  std::filesystem::copy_file(updater, helper, std::filesystem::copy_options::overwrite_existing, ec);
  CHECK(!ec);
  for (const auto& entry : std::filesystem::directory_iterator(helper_dir, ec)) {
    CHECK(entry.is_regular_file());
    CHECK(entry.path().extension() == ".exe");
  }

  const std::vector<std::string> imports = ImportedLibraries(helper);
  CHECK(!imports.empty());
  for (const std::string& library : imports) {
    if (!IsSystemLibrary(library)) {
      std::printf("  helper imports %s\n", library.c_str());
    }
    CHECK(IsSystemLibrary(library));
  }

  std::filesystem::create_directories(standalone, ec);
  DWORD standalone_exit = 1;
  std::string standalone_error;
  CHECK(RunHelper(helper, standalone / "missing job.txt", standalone, standalone_exit, standalone_error));
  CHECK(standalone_exit == 2);
  const std::string standalone_log = ReadFile(standalone / "updater.log");
  CHECK(standalone_log.find("trg-updater starting") != std::string::npos);
  CHECK(standalone_log.find("job: " + (standalone / "missing job.txt").string()) != std::string::npos);
  CHECK(standalone_log.find("job read failed: Cannot read the update job.") != std::string::npos);
  CHECK(standalone_log.find("exit code: 2") != std::string::npos);

  std::wstring parent_command =
      Quote(marker.wstring()) + L" --restart-marker --hold 750 " + Quote(parent_output.wstring());
  STARTUPINFOW parent_startup{};
  parent_startup.cb = sizeof(parent_startup);
  PROCESS_INFORMATION parent{};
  CHECK(CreateProcessW(marker.c_str(), parent_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       temp.path.c_str(), &parent_startup, &parent));
  CHECK(parent.dwProcessId != 0);

  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(install / program, "DEVELOPMENT_BUILD");
  WriteFile(install / "trg-updater.exe", "OLD_HELPER");
  WriteFile(install / "user_data" / "mua1_gold" / "game_settings.toml", "game settings");
  WriteFile(install / "user_data" / "settings.toml", "player settings");
  WriteFile(install / "user_data" / "mua2" / "4156082F" / "profile" / "save.bin", "save");
  WriteFile(install / "game" / "mua1" / "default.xex", "game files");
  WriteFile(install / "logs" / "marvel_ultimate_alliance_001.log", "log");
  WriteFile(install / "notes.txt", "keep me");
  std::filesystem::copy_file(marker, staging / program, std::filesystem::copy_options::overwrite_existing, ec);
  CHECK(!ec);
  std::filesystem::copy_file(helper, staging / "trg-updater.exe", std::filesystem::copy_options::overwrite_existing,
                             ec);
  CHECK(!ec);
  WriteFile(manifest, program.string() + "\ntrg-updater.exe\n");

  std::string job_text = "format=1\n";
  job_text += "parent_pid=" + std::to_string(parent.dwProcessId) + "\n";
  job_text += "install_dir=" + install.string() + "\n";
  job_text += "staging_dir=" + staging.string() + "\n";
  job_text += "backup_dir=" + backup.string() + "\n";
  job_text += "manifest=" + manifest.string() + "\n";
  job_text += "restart=" + (install / program).string() + "\n";
  job_text += "log=" + log.string() + "\n";
  job_text += "arg=--restart-marker\n";
  job_text += "arg=" + marker_output.string() + "\n";
  job_text += "arg=--user_data_root=C:\\with space\\\n";
  job_text += "arg=--name=say \"hi\"\n";
  job_text += "arg=\n";
  job_text += "arg=--show_launcher=true\n";
  WriteFile(job, job_text);

  DWORD exit_code = 1;
  std::string error;
  CHECK(RunHelper(helper, job, workspace, exit_code, error));
  if (!error.empty()) {
    std::printf("  helper: %s\n", error.c_str());
  }
  WaitForSingleObject(parent.hProcess, 5000);
  CloseHandle(parent.hThread);
  CloseHandle(parent.hProcess);
  CHECK(exit_code == 0);
  const std::string parent_text = ReadFile(parent_output);
  CHECK(parent_text.find("done=1") != std::string::npos);
  for (const wchar_t* name : kUpdateTestVariables) {
    CHECK(parent_text.find("env_" + Utf8(name) + "=present\n") != std::string::npos);
  }
  CHECK(WaitForMarker(marker_output));
  CHECK(SameFile(install / program, marker));
  CHECK(SameFile(install / "trg-updater.exe", helper));

  const std::string marker_text = ReadFile(marker_output);
  CHECK(marker_text.find("argc=6\n") != std::string::npos);
  CHECK(marker_text.find("arg1=" + marker_output.string() + "\n") != std::string::npos);
  CHECK(marker_text.find("arg2=--user_data_root=C:\\with space\\\n") != std::string::npos);
  CHECK(marker_text.find("arg3=--name=say \"hi\"\n") != std::string::npos);
  CHECK(marker_text.find("arg4=\n") != std::string::npos);
  CHECK(marker_text.find("arg5=--show_launcher=true\n") != std::string::npos);
  CHECK(marker_text.find("env_MUA_UPDATE_TEST=absent\n") != std::string::npos);
  CHECK(marker_text.find("env_MUA_UPDATE_TEST_CURRENT_VERSION=absent\n") != std::string::npos);
  CHECK(marker_text.find("env_MUA_UPDATE_TEST_RELEASE_JSON=absent\n") != std::string::npos);
  CHECK(marker_text.find("env_MUA_UPDATE_TEST_ASSET_SHA256=absent\n") != std::string::npos);

  CHECK(ReadFile(install / "user_data" / "mua1_gold" / "game_settings.toml") == "game settings");
  CHECK(ReadFile(install / "user_data" / "settings.toml") == "player settings");
  CHECK(ReadFile(install / "user_data" / "mua2" / "4156082F" / "profile" / "save.bin") ==
        "save");
  CHECK(ReadFile(install / "game" / "mua1" / "default.xex") == "game files");
  CHECK(ReadFile(install / "logs" / "marvel_ultimate_alliance_001.log") == "log");
  CHECK(ReadFile(install / "notes.txt") == "keep me");

  const std::string helper_log = ReadFile(log);
  CHECK(helper_log.find("install root: " + install.string()) != std::string::npos);
  CHECK(helper_log.find("staging root: " + staging.string()) != std::string::npos);
  CHECK(helper_log.find("manifest: " + manifest.string()) != std::string::npos);
  CHECK(helper_log.find("restart executable: " + (install / program).string()) != std::string::npos);
  CHECK(helper_log.find("restart arguments: --restart-marker " + marker_output.string() +
                        " --user_data_root=C:\\with space\\ --name=say \"hi\"  --show_launcher=true") != std::string::npos);
  CHECK(helper_log.find("waiting for parent pid " + std::to_string(parent.dwProcessId)) != std::string::npos);
  CHECK(helper_log.find("parent exited") != std::string::npos);
  CHECK(helper_log.find("manifest entries: 2") != std::string::npos);
  CHECK(helper_log.find("backup start") != std::string::npos);
  CHECK(helper_log.find("backup complete") != std::string::npos);
  CHECK(helper_log.find("replacing 2 files") != std::string::npos);
  CHECK(helper_log.find("replacement complete") != std::string::npos);
  CHECK(helper_log.find("verification complete") != std::string::npos);
  CHECK(helper_log.find("restart start") != std::string::npos);
  CHECK(helper_log.find("restart complete") != std::string::npos);
  CHECK(helper_log.find("install result: success") != std::string::npos);
  CHECK(helper_log.find("update installed and restarted") != std::string::npos);
  CHECK(helper_log.find("exit code: 0") != std::string::npos);
  CHECK(helper_log.find("rollback") == std::string::npos);

  CHECK(!std::filesystem::exists(job));
  CHECK(!std::filesystem::exists(backup));
  CHECK(!std::filesystem::exists(staging));

  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
