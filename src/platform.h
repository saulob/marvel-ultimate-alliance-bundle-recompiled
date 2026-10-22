#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "path_utf8.h"
#include "window_placement.h"

namespace mua {

// The folder the player sees the port in: the executable's.
std::filesystem::path PortFolder();

// Caches the current run's log file and installs the unhandled exception filter that appends
// a CRASH REPORT block to it.
void InstallCrashReports();

// The architecture this build runs as ("x86-64", "arm64", ...), for the startup log line.
const char* ArchitectureName();

// The graphics backend the xenos GPU plugin uses here.
const char* GraphicsBackendName();

struct UiFontFiles {
  std::filesystem::path regular, semibold, bold;
};
UiFontFiles FindUiFonts();

// True while Shift is held.
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

struct PreShowResult {
  WindowPlacement startup;  // where the window was created
  WindowPlacement applied;
};

// Applies the plan to the next top-level window of this thread before it is first shown.
void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved);

// Set once the window has been shown; empty when no window was placed.
std::optional<PreShowResult> TakePreShowResult();

// Starts `executable` with this run's command line plus `extra_args`.
bool RelaunchExecutable(const std::filesystem::path& executable, std::wstring_view extra_args);

// Hides the window at once, so nothing is left on screen while a started program prepares its own.
void HideNativeWindow(void* native_handle);

}  // namespace mua
