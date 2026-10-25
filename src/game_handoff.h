#pragma once

namespace mua {

inline constexpr char kLauncherHandoffKey[] = "mua_launcher_handoff";
inline constexpr wchar_t kHandoffArgument[] = L"--mua_launcher_handoff=true";

// SW_SHOWNORMAL
inline constexpr int kHandoffShowCommand = 1;

// SW_SHOWMINIMIZED, SW_MINIMIZE, SW_SHOWMINNOACTIVE, SW_FORCEMINIMIZE.
inline bool ShowCommandMinimizes(int show_command) {
  return show_command == 2 || show_command == 6 || show_command == 7 || show_command == 11;
}

}  // namespace mua
