#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mua {

// Tells AppRun (packaging/linux/AppRun) which of the bundle's programs an AppImage start runs; unset runs the launcher.
inline constexpr char kAppImageProgramVariable[] = "MUA_APPIMAGE_PROGRAM";

struct RelaunchCommand {
  std::string executable;
  std::vector<std::string> argv;
  // The value of kAppImageProgramVariable for the new process; empty outside an AppImage.
  std::string appimage_program;
};

// Starts `program` with this process's command line `args` plus `extra_args`. Inside an AppImage the AppImage file is
// started again and AppRun runs `program` from it: this process runs from a mount that closes when it exits.
inline std::optional<RelaunchCommand> MakeRelaunchCommand(std::vector<std::string> args, const char* appimage,
                                                          std::string_view program,
                                                          const std::vector<std::string>& extra_args) {
  if (program.empty() || args.empty()) {
    return std::nullopt;
  }
  RelaunchCommand command;
  if (appimage && *appimage) {
    command.executable = appimage;
    const size_t slash = program.find_last_of('/');
    command.appimage_program = std::string(slash == std::string_view::npos ? program : program.substr(slash + 1));
  } else {
    command.executable = std::string(program);
  }
  args.front() = command.executable;
  args.insert(args.end(), extra_args.begin(), extra_args.end());
  command.argv = std::move(args);
  return command;
}

}  // namespace mua
