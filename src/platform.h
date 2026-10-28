#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "path_utf8.h"
#include "window_placement.h"

namespace mua {

// The folder the player sees the port in: the executable's, or the AppImage's.
std::filesystem::path PortFolder();

// The folder holding this build's programs: the executable's, which inside an AppImage is its read-only mount.
std::filesystem::path ProgramFolder();

// Windows: caches the current run's log file and installs the unhandled exception filter that appends
// a CRASH REPORT block to it. A no-op on Linux.
void InstallCrashReports();

// Ends the process with exit code 0 at once. On Windows std::_Exit still runs every DLL's detach code (GPU driver,
// NGX, audio) while the closed window stays on screen.
[[noreturn]] void HardExitProcess();

// The architecture this build runs as ("x86-64", "arm64", ...), for the startup log line.
const char* ArchitectureName();

// The graphics backend the xenos GPU plugin uses here.
const char* GraphicsBackendName();

struct UiFontFiles {
  std::filesystem::path regular, semibold, bold;
};
UiFontFiles FindUiFonts();

// Gives the window the program's icon. Windows takes it from the executable's resources already; on Linux SDL
// does not, so the icon the AppImage carries for the program is loaded.
void ApplyWindowIcon();

// Names the application to the Linux desktop: SDL uses it as the Wayland app_id and the X11 WM_CLASS class. It has to
// be set before the window is created. A no-op on Windows.
void SetApplicationId();

// Linux: tells the player, in a window that needs no GPU, that graphics could not start; the SDK only logs it. A no-op
// on Windows.
void ReportGraphicsFailure();

struct GraphicsFallbackInfo {
  std::string game_name;
  std::string game_dir;
  std::string game_files_status;
  bool game_files_ready = false;
};

// Linux: the launcher's window when graphics cannot start, drawn on an SDL surface with no GPU. It shows the selected
// game and its files; Play reports the graphics failure. Returns when the player closes it. A no-op on Windows.
void RunLauncherWithoutGraphics(const GraphicsFallbackInfo& info);

// True while Shift is held. On Linux SDL only knows the keyboard once a window has focus.
bool ShiftHeld();

struct MonitorInfo {
  int left = 0, top = 0;                // physical pixels
  int width = 0, height = 0;
  int work_width = 0, work_height = 0;  // without the taskbar
  float scale = 1.0f;                   // display scaling
  bool primary = false;
  int refresh_hz = 0;                   // current mode, 0 when unavailable
};

// In the order ReXGlue's `monitor` setting numbers them: SDL's display order.
std::vector<MonitorInfo> ListMonitors();

// The display a `monitor` setting names. 0 (Default) and 1 are the primary display.
MonitorInfo MonitorForSetting(int monitor);

// Logs when the window did not open on the display the monitor setting names.
void CheckWindowDisplay(void* native_window, int monitor);

// Display work areas in screen pixels.
std::vector<Rect> WorkAreas();

// The native window's state, or nothing when the handle is not a window.
std::optional<WindowState> QueryWindowState(void* native_handle);

// Puts the window at the placement: the restored rectangle first, then maximized when asked.
void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement);

// Brings a launcher window that opened minimized back to its restored or maximized state and activates it once.
void EnsureWindowVisible(void* native_handle);

struct PreShowResult {
  WindowPlacement startup;  // where the window was created
  WindowPlacement applied;
};

// Applies the plan to the next top-level window of this thread before it is first shown. Windows only: elsewhere
// the launcher places the window once it exists.
void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved);

// Set once the window has been shown; empty when no window was placed.
std::optional<PreShowResult> TakePreShowResult();

// Starts `executable` with this run's command line plus `extra_args`, restored and allowed to take the foreground.
// Inside an AppImage the AppImage file is started again and runs `executable` from its new mount.
bool RelaunchExecutable(const std::filesystem::path& executable, std::wstring_view extra_args);

// In a program the launcher started: brings its window forward once.
void CompleteLauncherHandoff(void* native_handle);

// Hides the window at once, so nothing is left on screen while a started program prepares its own.
void HideNativeWindow(void* native_handle);

}  // namespace mua
