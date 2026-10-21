#include "platform.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

#include <windows.h>
#include <shellscalingapi.h>

#include "trg/platform.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "active_log.h"
#include "crash_report.h"
#include "dpi.h"
#include "path_utf8.h"
#include "session_log.h"

// The NVIDIA Optimus and AMD switchable graphics drivers run programs exporting these on the
// discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace mua {
namespace {

BOOL CALLBACK AddMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }
  MonitorInfo found;
  found.left = info.rcMonitor.left;
  found.top = info.rcMonitor.top;
  found.width = info.rcMonitor.right - info.rcMonitor.left;
  found.height = info.rcMonitor.bottom - info.rcMonitor.top;
  found.work_width = info.rcWork.right - info.rcWork.left;
  found.work_height = info.rcWork.bottom - info.rcWork.top;
  found.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
    found.refresh_hz = int(mode.dmDisplayFrequency);
  }
  UINT dpi_x = 0;
  UINT dpi_y = 0;
  if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
    found.scale = DpiScaleFromDpi(dpi_x);
  }
  reinterpret_cast<std::vector<MonitorInfo>*>(user)->push_back(found);
  return TRUE;
}

BOOL CALLBACK AddWorkArea(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info)) {
    reinterpret_cast<std::vector<Rect>*>(user)->push_back(
        {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom});
  }
  return TRUE;
}

PreShowPlan g_plan;
WindowPlacement g_saved;
HHOOK g_hook = nullptr;
std::optional<PreShowResult> g_result;

LRESULT CALLBACK PreShowProc(int code, WPARAM wparam, LPARAM lparam) {
  const auto* message = reinterpret_cast<const CWPSTRUCT*>(lparam);
  if (code == HC_ACTION && message->message == WM_SHOWWINDOW && message->wParam && message->lParam == 0 &&
      !GetParent(message->hwnd) && !g_result) {
    HHOOK hook = g_hook;
    g_hook = nullptr;
    const HWND window = message->hwnd;
    RECT before;
    GetWindowRect(window, &before);
    g_result = PreShowResult{{{before.left, before.top, before.right, before.bottom}, false}, g_saved};
    if (g_plan.normal) {
      SetWindowPos(window, nullptr, g_plan.normal->left, g_plan.normal->top, g_plan.normal->width(),
                   g_plan.normal->height(), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (g_plan.maximized) {
      ShowWindow(window, SW_MAXIMIZE);
    }
    UnhookWindowsHookEx(hook);
    return 0;
  }
  return CallNextHookEx(g_hook, code, wparam, lparam);
}

}  // namespace

std::filesystem::path PortFolder() {
  return rex::filesystem::GetExecutableFolder();
}

const char* ArchitectureName() {
#if defined(_M_ARM64)
  return "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
  return "x86-64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}

const char* GraphicsBackendName() {
  return "Direct3D 12";
}

void InstallCrashReports() {
  const std::filesystem::path file = ActiveLogFile();
  if (file.empty()) {
    REXLOG_WARN("Windows crash reporting disabled: the current log file could not be resolved");
    return;
  }
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
  InstallCrashReporting(Utf8(ec ? file : absolute).c_str());
  REXLOG_INFO("Windows crash reporting enabled for current log.");
}

UiFontFiles FindUiFonts() {
  wchar_t windows[MAX_PATH];
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  const std::filesystem::path fonts =
      std::filesystem::path(length ? std::wstring(windows, length) : std::wstring(L"C:\\Windows")) / L"Fonts";
  return {fonts / "segoeui.ttf", fonts / "seguisb.ttf", fonts / "segoeuib.ttf"};
}

bool ShiftHeld() {
  return trg::ShiftHeld();
}

// SDL builds its display list the same way: the primary display, then EnumDisplayMonitors order.
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

MonitorInfo MonitorForSetting(int monitor) {
  const std::vector<MonitorInfo> monitors = ListMonitors();
  if (monitors.empty()) {
    return {};
  }
  if (monitor >= 1 && monitor <= int(monitors.size())) {
    return monitors[size_t(monitor - 1)];
  }
  const auto primary = std::find_if(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return primary != monitors.end() ? *primary : monitors.front();
}

std::vector<Rect> WorkAreas() {
  std::vector<Rect> areas;
  EnumDisplayMonitors(nullptr, nullptr, AddWorkArea, reinterpret_cast<LPARAM>(&areas));
  return areas;
}

std::optional<WindowState> QueryWindowState(void* native_handle) {
  const HWND window = static_cast<HWND>(native_handle);
  RECT rect;
  if (!window || !IsWindow(window) || !GetWindowRect(window, &rect)) {
    return std::nullopt;
  }
  WindowState state;
  state.rect = {rect.left, rect.top, rect.right, rect.bottom};
  state.minimized = IsIconic(window) != FALSE;
  state.maximized = IsZoomed(window) != FALSE;
  if (state.minimized) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    state.maximized = GetWindowPlacement(window, &placement) && (placement.flags & WPF_RESTORETOMAXIMIZED);
  }
  return state;
}

void ApplyWindowPlacement(void* native_handle, const WindowPlacement& placement) {
  const HWND window = static_cast<HWND>(native_handle);
  if (!window || !IsWindow(window)) {
    return;
  }
  ShowWindow(window, SW_RESTORE);
  SetWindowPos(window, nullptr, placement.normal.left, placement.normal.top, placement.normal.width(),
               placement.normal.height(), SWP_NOZORDER | SWP_NOACTIVATE);
  if (placement.maximized) {
    ShowWindow(window, SW_MAXIMIZE);
  }
}

void InstallPreShowPlacement(const PreShowPlan& plan, const WindowPlacement& saved) {
  if (g_hook || !plan.Needed()) {
    return;
  }
  g_plan = plan;
  g_saved = saved;
  g_result.reset();
  g_hook = SetWindowsHookExW(WH_CALLWNDPROC, PreShowProc, nullptr, GetCurrentThreadId());
}

std::optional<PreShowResult> TakePreShowResult() {
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
  std::optional<PreShowResult> result = g_result;
  g_result.reset();
  return result;
}

void CheckWindowDisplay(void* native_window, int monitor) {
  const HWND hwnd = static_cast<HWND>(native_window);
  const MonitorInfo expected = MonitorForSetting(monitor);
  if (!hwnd || expected.width == 0) {
    return;
  }
  MONITORINFO actual{sizeof(actual)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &actual)) {
    return;
  }
  if (actual.rcMonitor.left != expected.left || actual.rcMonitor.top != expected.top) {
    REXLOG_WARN("Window opened on the display at ({}, {}), but monitor setting {} names ({}, {})",
                actual.rcMonitor.left, actual.rcMonitor.top, monitor, expected.left, expected.top);
  }
}

bool RelaunchSelf(std::wstring_view extra_args) {
  BeginSessionLogHandoff();
  std::wstring command_line = GetCommandLineW();
  command_line += L' ';
  command_line += extra_args;
  wchar_t exe[MAX_PATH];
  if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    CancelSessionLogHandoff();
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                      &process)) {
    REXLOG_ERROR("Relaunch failed ({})", GetLastError());
    CancelSessionLogHandoff();
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

}  // namespace mua
