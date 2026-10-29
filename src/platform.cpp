#include "platform.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#include <shellscalingapi.h>

#include "trg/platform.h"
#else
#include <fstream>

#include <spawn.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include "relaunch_command.h"
#include "window_host.h"
#endif

#include <rex/filesystem.h>
#include <rex/logging.h>

#include "active_log.h"
#include "crash_report.h"
#include "dpi.h"
#include "game_handoff.h"
#include "path_utf8.h"
#include "port_version.h"
#include "session_log.h"

#if defined(_WIN32)
// The NVIDIA Optimus and AMD switchable graphics drivers run programs exporting these on the
// discrete GPU.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace mua {
namespace {

#if defined(_WIN32)

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

static_assert(kHandoffShowCommand == SW_SHOWNORMAL);

#else

struct FontFamily {
  const char* regular;
  const char* semibold;
  const char* bold;
};
// The first family whose regular face exists wins.
constexpr FontFamily kFontFamilies[] = {
    {"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/google-noto/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/NotoSans-Regular.ttf", "/usr/share/fonts/truetype/NotoSans-SemiBold.ttf",
     "/usr/share/fonts/truetype/NotoSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", nullptr, "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/TTF/DejaVuSans.ttf", nullptr, "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/dejavu/DejaVuSans.ttf", nullptr, "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf", nullptr,
     "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/DejaVuSans.ttf", nullptr, "/usr/share/fonts/truetype/DejaVuSans-Bold.ttf"},
    {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", nullptr,
     "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"},
    {"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", nullptr,
     "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"},
};

constexpr char kApplicationId[] = "io.github.saulob.marvel-ultimate-alliance";

// ReXGlue's native window handle is empty here, and each program has only this window.
SDL_Window* ProgramWindow() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows && count > 0 ? windows[0] : nullptr;
  SDL_free(windows);
  return window;
}

// Each program shows its own icon, installed in the AppImage under the program's name.
std::filesystem::path ProgramIconFile() {
  std::error_code ec;
  const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
  return rex::filesystem::GetExecutableFolder() / ".." / "share" / "icons" / "hicolor" / "256x256" / "apps" /
         (self.filename().string() + ".png");
}

#endif

}  // namespace

std::filesystem::path PortFolder() {
#if !defined(_WIN32)
  // An AppImage runs from a read-only mount that is gone once it exits.
  if (const char* appimage = std::getenv("APPIMAGE"); appimage && *appimage) {
    return std::filesystem::path(appimage).parent_path();
  }
#endif
  return rex::filesystem::GetExecutableFolder();
}

std::filesystem::path ProgramFolder() {
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
#if defined(_WIN32)
  return "Direct3D 12";
#else
  return "Vulkan";
#endif
}

void InstallCrashReports() {
#if defined(_WIN32)
  const std::filesystem::path file = ActiveLogFile();
  if (file.empty()) {
    REXLOG_WARN("Windows crash reporting disabled: the current log file could not be resolved");
    return;
  }
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::absolute(file, ec);
  SetCrashGameVersion(PortVersion().c_str());
  InstallCrashReporting(Utf8(ec ? file : absolute).c_str());
  REXLOG_INFO("Windows crash reporting enabled for current log.");
#endif
}

void HardExitProcess() {
#if defined(_WIN32)
  TerminateProcess(GetCurrentProcess(), 0);
#endif
  std::_Exit(0);
}

UiFontFiles FindUiFonts() {
#if defined(_WIN32)
  wchar_t windows[MAX_PATH];
  const UINT length = GetWindowsDirectoryW(windows, MAX_PATH);
  const std::filesystem::path fonts =
      std::filesystem::path(length ? std::wstring(windows, length) : std::wstring(L"C:\\Windows")) / L"Fonts";
  return {fonts / "segoeui.ttf", fonts / "seguisb.ttf", fonts / "segoeuib.ttf"};
#else
  for (const FontFamily& family : kFontFamilies) {
    std::error_code ec;
    if (std::filesystem::exists(family.regular, ec)) {
      return {family.regular, family.semibold ? family.semibold : family.bold, family.bold};
    }
  }
  return {};
#endif
}

bool ShiftHeld() {
#if defined(_WIN32)
  return trg::ShiftHeld();
#else
  SDL_PumpEvents();
  return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
#endif
}

#if defined(_WIN32)

// SDL builds its display list the same way: the primary display, then EnumDisplayMonitors order.
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&monitors));
  std::stable_partition(monitors.begin(), monitors.end(), [](const MonitorInfo& m) { return m.primary; });
  return monitors;
}

#else

// Sizes are SDL window coordinates, the units ReXGlue sizes its window in (logical size times the scale).
std::vector<MonitorInfo> ListMonitors() {
  std::vector<MonitorInfo> monitors;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  for (int i = 0; displays && i < count; ++i) {
    MonitorInfo found;
    SDL_Rect bounds{};
    if (SDL_GetDisplayBounds(displays[i], &bounds)) {
      found.left = bounds.x;
      found.top = bounds.y;
      found.width = found.work_width = bounds.w;
      found.height = found.work_height = bounds.h;
    }
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displays[i], &usable) && usable.w > 0 && usable.h > 0) {
      found.work_width = usable.w;
      found.work_height = usable.h;
    }
    if (const float scale = SDL_GetDisplayContentScale(displays[i]); scale > 0.0f) {
      found.scale = scale;
    }
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displays[i])) {
      found.refresh_hz = int(mode->refresh_rate + 0.5f);
    }
    found.primary = displays[i] == primary;
    monitors.push_back(found);
  }
  SDL_free(displays);
  return monitors;
}

#endif

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

#if defined(_WIN32)

void ApplyWindowIcon() {}

void SetApplicationId() {}

void ReportGraphicsFailure() {}

void RunLauncherWithoutGraphics(const GraphicsFallbackInfo&) {}

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

void EnsureWindowVisible(void* native_handle) {
  const std::optional<WindowState> state = QueryWindowState(native_handle);
  if (!state || !StartupNeedsRestore(*state)) {
    return;
  }
  const HWND window = static_cast<HWND>(native_handle);
  ShowWindow(window, SW_RESTORE);
  SetForegroundWindow(window);
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

bool RelaunchExecutable(const std::filesystem::path& executable, std::wstring_view extra_args) {
  BeginSessionLogHandoff();
  std::wstring command_line = GetCommandLineW();
  command_line += L' ';
  command_line += extra_args;
  command_line += L' ';
  command_line += kHandoffArgument;
  const wchar_t* exe = executable.c_str();
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = kHandoffShowCommand;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                      &process)) {
    REXLOG_ERROR("Relaunch failed ({})", GetLastError());
    CancelSessionLogHandoff();
    return false;
  }
  const bool foreground_granted = AllowSetForegroundWindow(process.dwProcessId) != FALSE;
  REXLOG_INFO("Launcher handoff: {} pid={} show=normal foreground_granted={}", Utf8(executable.filename()),
              process.dwProcessId, foreground_granted);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

void CompleteLauncherHandoff(void* native_handle) {
  const HWND window = static_cast<HWND>(native_handle);
  if (!window || !IsWindow(window)) {
    return;
  }
  const std::optional<WindowState> state = QueryWindowState(native_handle);
  const bool restored = state && StartupNeedsRestore(*state);
  if (restored) {
    ShowWindow(window, SW_RESTORE);
  }
  BringWindowToTop(window);
  const bool foreground = SetForegroundWindow(window) != FALSE;
  REXLOG_INFO("Launcher handoff: game window restored={} foreground={}", restored, foreground);
}

void HideNativeWindow(void* native_handle) {
  if (native_handle) {
    ShowWindow(static_cast<HWND>(native_handle), SW_HIDE);
  }
}

#else

void SetApplicationId() {
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, kApplicationId);
}

namespace {

struct Color {
  int r, g, b;
};

constexpr Color kPanel{0x2b, 0x2b, 0x2b};
constexpr Color kText{0xf0, 0xf0, 0xf0};

constexpr const char* kGraphicsFailureLines[] = {
    "The graphics system could not start.",
    "The game needs a working Vulkan graphics driver,",
    "such as Mesa (AMD and Intel) or the NVIDIA driver.",
    "Install or update it and start the game again.",
    "The log folder next to the AppImage has the details.",
    "Press a key or click to close.",
};
constexpr int kGraphicsFailureWidth = 520;
constexpr int kGraphicsFailureHeight = 152;
constexpr int kGraphicsFailureMargin = 16;
constexpr int kGraphicsFailureLineStep = 20;

class SoftwareText {
 public:
  SoftwareText() {
    ImFont* font = atlas_.AddFontDefault();
    baked_ = font->GetFontBaked(font->LegacySize);
    // Loading a glyph rewrites the atlas pixels, so every glyph is loaded before they are read.
    for (ImWchar c = kFirstGlyph; c <= kLastGlyph; ++c) {
      baked_->FindGlyph(c);
    }
    atlas_.GetTexDataAsAlpha8(&pixels_, &width_, &height_);
  }

  int Width(std::string_view text, int scale) const {
    float width = 0.0f;
    for (const char c : text) {
      width += baked_->FindGlyph(Printable(c))->AdvanceX;
    }
    return int(width) * scale;
  }

  int Height(int scale) const { return int(baked_->Size) * scale; }

  void Draw(SDL_Surface* surface, std::string_view text, int x, int y, int scale, Color color) const {
    float pen_x = float(x);
    for (const char c : text) {
      const ImFontGlyph* glyph = baked_->FindGlyph(Printable(c));
      DrawGlyph(surface, *glyph, int(pen_x), y, scale, color);
      pen_x += glyph->AdvanceX * float(scale);
    }
  }

 private:
  static constexpr ImWchar kFirstGlyph = 0x20;
  static constexpr ImWchar kLastGlyph = 0x7e;

  static ImWchar Printable(char c) {
    const auto code = static_cast<unsigned char>(c);
    return code >= kFirstGlyph && code <= kLastGlyph ? ImWchar(code) : ImWchar('?');
  }

  void DrawGlyph(SDL_Surface* surface, const ImFontGlyph& glyph, int x, int y, int scale, Color color) const {
    if (!glyph.Visible) {
      return;
    }
    const int source_x = int(glyph.U0 * float(width_) + 0.5f);
    const int source_y = int(glyph.V0 * float(height_) + 0.5f);
    const int width = int(glyph.X1 - glyph.X0 + 0.5f);
    const int height = int(glyph.Y1 - glyph.Y0 + 0.5f);
    const int left = x + int(glyph.X0 * float(scale) + 0.5f);
    const int top = y + int(glyph.Y0 * float(scale) + 0.5f);
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        const int alpha = pixels_[(source_y + row) * width_ + source_x + column];
        if (alpha == 0) {
          continue;
        }
        for (int dy = 0; dy < scale; ++dy) {
          for (int dx = 0; dx < scale; ++dx) {
            Blend(surface, left + column * scale + dx, top + row * scale + dy, color, alpha);
          }
        }
      }
    }
  }

  static void Blend(SDL_Surface* surface, int x, int y, Color color, int alpha) {
    Uint8 r, g, b, a;
    if (x < 0 || y < 0 || x >= surface->w || y >= surface->h ||
        !SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a)) {
      return;
    }
    SDL_WriteSurfacePixel(surface, x, y, Uint8(r + (color.r - r) * alpha / 255),
                          Uint8(g + (color.g - g) * alpha / 255), Uint8(b + (color.b - b) * alpha / 255), 0xff);
  }

  ImFontAtlas atlas_;
  ImFontBaked* baked_ = nullptr;
  unsigned char* pixels_ = nullptr;
  int width_ = 0;
  int height_ = 0;
};

void FillRect(SDL_Surface* surface, const SDL_Rect& rect, Color color) {
  SDL_FillSurfaceRect(surface, &rect, SDL_MapSurfaceRGB(surface, Uint8(color.r), Uint8(color.g), Uint8(color.b)));
}

void DrawGraphicsFailure(SDL_Window* window, const SoftwareText& text) {
  SDL_Surface* surface = SDL_GetWindowSurface(window);
  if (!surface) {
    return;
  }
  FillRect(surface, {0, 0, surface->w, surface->h}, kPanel);
  int line_y = kGraphicsFailureMargin;
  for (const char* line : kGraphicsFailureLines) {
    text.Draw(surface, line, kGraphicsFailureMargin, line_y, 1, kText);
    line_y += kGraphicsFailureLineStep;
  }
  SDL_UpdateWindowSurface(window);
}

constexpr int kFallbackWidth = 640;
constexpr int kFallbackHeight = 400;
constexpr int kFallbackMargin = 24;
constexpr int kFallbackHeaderHeight = 96;
constexpr int kFallbackIconSize = 72;
constexpr int kFallbackLargeScale = 2;
constexpr int kFallbackRowStep = 34;
constexpr int kFallbackLabelWidth = 120;
constexpr Color kFallbackBackground{0x1e, 0x1e, 0x24};
constexpr Color kFallbackHeader{0x2b, 0x2b, 0x36};
constexpr Color kAccent{0xff, 0x8a, 0x1f};
constexpr Color kAccentHover{0xff, 0xa8, 0x55};
constexpr Color kButton{0x3a, 0x3a, 0x48};
constexpr Color kButtonHover{0x4e, 0x4e, 0x60};
constexpr Color kDimText{0x9a, 0x9a, 0xa6};
constexpr Color kReadyText{0x5c, 0xc9, 0x6b};
constexpr Color kWarningText{0xf2, 0xb1, 0x3a};
constexpr Color kDarkText{0x1e, 0x1e, 0x24};

enum class FallbackButton { kNone, kPlay, kQuit };

constexpr SDL_Rect kPlayButton{kFallbackMargin, kFallbackHeight - 76, 200, 48};
constexpr SDL_Rect kQuitButton{kFallbackMargin + 216, kFallbackHeight - 76, 140, 48};

FallbackButton ButtonAt(float x, float y) {
  const SDL_Point point{int(x), int(y)};
  if (SDL_PointInRect(&point, &kPlayButton)) {
    return FallbackButton::kPlay;
  }
  return SDL_PointInRect(&point, &kQuitButton) ? FallbackButton::kQuit : FallbackButton::kNone;
}

std::string FitToWidth(const SoftwareText& text, std::string value, int max_width) {
  if (text.Width(value, 1) <= max_width) {
    return value;
  }
  while (value.size() > 1 && text.Width("..." + value, 1) > max_width) {
    value.erase(0, 1);
  }
  return "..." + value;
}

void DrawButton(SDL_Surface* surface, const SoftwareText& text, const SDL_Rect& rect, const char* label,
                bool primary, bool hovered) {
  const Color fill = primary ? (hovered ? kAccentHover : kAccent) : (hovered ? kButtonHover : kButton);
  FillRect(surface, rect, fill);
  const int width = text.Width(label, kFallbackLargeScale);
  text.Draw(surface, label, rect.x + (rect.w - width) / 2, rect.y + (rect.h - text.Height(kFallbackLargeScale)) / 2,
            kFallbackLargeScale, primary ? kDarkText : kText);
}

void DrawStatusRow(SDL_Surface* surface, const SoftwareText& text, int y, const char* label, const std::string& value,
                   Color value_color) {
  text.Draw(surface, label, kFallbackMargin, y, 1, kDimText);
  text.Draw(surface, value, kFallbackMargin + kFallbackLabelWidth, y, 1, value_color);
}

void DrawLauncherWithoutGraphics(SDL_Window* window, const SoftwareText& text, SDL_Surface* icon,
                                 const GraphicsFallbackInfo& info, FallbackButton hovered) {
  SDL_Surface* surface = SDL_GetWindowSurface(window);
  if (!surface) {
    return;
  }
  FillRect(surface, {0, 0, surface->w, surface->h}, kFallbackBackground);
  FillRect(surface, {0, 0, surface->w, kFallbackHeaderHeight}, kFallbackHeader);
  FillRect(surface, {0, kFallbackHeaderHeight, surface->w, 3}, kAccent);
  int title_x = kFallbackMargin;
  if (icon) {
    SDL_Rect target{kFallbackMargin, (kFallbackHeaderHeight - kFallbackIconSize) / 2, kFallbackIconSize,
                    kFallbackIconSize};
    SDL_BlitSurfaceScaled(icon, nullptr, surface, &target, SDL_SCALEMODE_LINEAR);
    title_x += kFallbackIconSize + kFallbackMargin;
  }
  text.Draw(surface, "MARVEL ULTIMATE ALLIANCE", title_x, 24, kFallbackLargeScale, kText);
  text.Draw(surface, "RECOMPILED", title_x, 32 + text.Height(kFallbackLargeScale), kFallbackLargeScale, kAccent);

  const int max_value_width = surface->w - 2 * kFallbackMargin - kFallbackLabelWidth;
  int row_y = kFallbackHeaderHeight + 24;
  DrawStatusRow(surface, text, row_y, "Game", FitToWidth(text, info.game_name, max_value_width), kText);
  row_y += kFallbackRowStep;
  DrawStatusRow(surface, text, row_y, "Game files", info.game_files_status,
                info.game_files_ready ? kReadyText : kWarningText);
  row_y += kFallbackRowStep;
  DrawStatusRow(surface, text, row_y, "Folder", FitToWidth(text, info.game_dir, max_value_width), kDimText);
  row_y += kFallbackRowStep;
  DrawStatusRow(surface, text, row_y, "Graphics", "Vulkan setup required", kWarningText);

  DrawButton(surface, text, kPlayButton, "PLAY", true, hovered == FallbackButton::kPlay);
  DrawButton(surface, text, kQuitButton, "QUIT", false, hovered == FallbackButton::kQuit);
  SDL_UpdateWindowSurface(window);
}

}  // namespace

// SDL's message box runs zenity first when it is installed, and zenity can block before any window appears.
void ReportGraphicsFailure() {
  const SoftwareText text;
  SDL_Window* window = SDL_CreateWindow(kWindowTitle, kGraphicsFailureWidth, kGraphicsFailureHeight, 0);
  if (!window) {
    REXLOG_ERROR("Could not create the graphics error window: {}", SDL_GetError());
    return;
  }
  if (!SDL_GetWindowSurface(window)) {
    REXLOG_ERROR("Could not get the graphics error window surface: {}", SDL_GetError());
    SDL_DestroyWindow(window);
    return;
  }
  DrawGraphicsFailure(window, text);
  bool open = true;
  while (open) {
    SDL_Event event;
    if (!SDL_WaitEvent(&event)) {
      break;
    }
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        open = false;
        break;
      case SDL_EVENT_WINDOW_EXPOSED:
        DrawGraphicsFailure(window, text);
        break;
      default:
        break;
    }
  }
  SDL_DestroyWindow(window);
}

void RunLauncherWithoutGraphics(const GraphicsFallbackInfo& info) {
  const SoftwareText text;
  SDL_Window* window = SDL_CreateWindow(kWindowTitle, kFallbackWidth, kFallbackHeight, 0);
  if (!window || !SDL_GetWindowSurface(window)) {
    REXLOG_ERROR("Could not open the launcher window without graphics: {}", SDL_GetError());
    SDL_DestroyWindow(window);
    ReportGraphicsFailure();
    return;
  }
  SDL_Surface* icon = SDL_LoadPNG(Utf8(ProgramIconFile()).c_str());
  if (icon) {
    SDL_SetWindowIcon(window, icon);
  }
  FallbackButton hovered = FallbackButton::kNone;
  DrawLauncherWithoutGraphics(window, text, icon, info, hovered);
  bool open = true;
  while (open) {
    SDL_Event event;
    if (!SDL_WaitEvent(&event)) {
      break;
    }
    FallbackButton pressed = FallbackButton::kNone;
    switch (event.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        open = false;
        break;
      case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
          pressed = FallbackButton::kPlay;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        pressed = ButtonAt(event.button.x, event.button.y);
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (const FallbackButton over = ButtonAt(event.motion.x, event.motion.y); over != hovered) {
          hovered = over;
          DrawLauncherWithoutGraphics(window, text, icon, info, hovered);
        }
        break;
      case SDL_EVENT_WINDOW_EXPOSED:
        DrawLauncherWithoutGraphics(window, text, icon, info, hovered);
        break;
      default:
        break;
    }
    if (pressed == FallbackButton::kQuit) {
      open = false;
    } else if (pressed == FallbackButton::kPlay) {
      ReportGraphicsFailure();
      DrawLauncherWithoutGraphics(window, text, icon, info, hovered);
    }
  }
  SDL_DestroySurface(icon);
  SDL_DestroyWindow(window);
}

void ApplyWindowIcon() {
  SDL_Window* window = ProgramWindow();
  if (!window) {
    return;
  }
  if (SDL_Surface* icon = SDL_LoadPNG(Utf8(ProgramIconFile()).c_str())) {
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
  }
}

std::vector<Rect> WorkAreas() {
  std::vector<Rect> areas;
  int count = 0;
  SDL_DisplayID* displays = SDL_GetDisplays(&count);
  for (int i = 0; displays && i < count; ++i) {
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(displays[i], &usable) && usable.w > 0 && usable.h > 0) {
      areas.push_back({usable.x, usable.y, usable.x + usable.w, usable.y + usable.h});
    }
  }
  SDL_free(displays);
  return areas;
}

std::optional<WindowState> QueryWindowState(void*) {
  SDL_Window* window = ProgramWindow();
  if (!window) {
    return std::nullopt;
  }
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  SDL_GetWindowPosition(window, &x, &y);
  SDL_GetWindowSize(window, &width, &height);
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
  WindowState state;
  state.rect = {x, y, x + width, y + height};
  state.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
  state.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
  return state;
}

void ApplyWindowPlacement(void*, const WindowPlacement& placement) {
  SDL_Window* window = ProgramWindow();
  if (!window) {
    return;
  }
  SDL_RestoreWindow(window);
  SDL_SyncWindow(window);
  SDL_SetWindowSize(window, placement.normal.width(), placement.normal.height());
  SDL_SetWindowPosition(window, placement.normal.left, placement.normal.top);
  if (placement.maximized) {
    SDL_MaximizeWindow(window);
  }
}

void EnsureWindowVisible(void* native_handle) {
  const std::optional<WindowState> state = QueryWindowState(native_handle);
  if (!state || !StartupNeedsRestore(*state)) {
    return;
  }
  if (SDL_Window* window = ProgramWindow()) {
    SDL_RestoreWindow(window);
    SDL_RaiseWindow(window);
  }
}

void InstallPreShowPlacement(const PreShowPlan&, const WindowPlacement&) {}

std::optional<PreShowResult> TakePreShowResult() {
  return std::nullopt;
}

// ListMonitors is SDL's own display order here, so the monitor setting always names the right display.
void CheckWindowDisplay(void*, int) {}

bool RelaunchExecutable(const std::filesystem::path& executable, std::wstring_view extra_args) {
  BeginSessionLogHandoff();
  std::vector<std::string> args;
  std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
  for (std::string arg; std::getline(cmdline, arg, '\0');) {
    args.push_back(arg);
  }
  std::vector<std::string> extra;
  for (std::wstring_view arg : {extra_args, std::wstring_view(kHandoffArgument)}) {
    extra.emplace_back(arg.begin(), arg.end());  // ASCII flags
  }
  std::optional<RelaunchCommand> command =
      MakeRelaunchCommand(std::move(args), std::getenv("APPIMAGE"), executable.string(), extra);
  if (!command) {
    REXLOG_ERROR("Relaunch failed: cannot read this process's command line");
    CancelSessionLogHandoff();
    return false;
  }
  std::vector<char*> argv;
  for (std::string& arg : command->argv) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  std::vector<std::string> environment;
  for (char** variable = environ; *variable; ++variable) {
    if (!std::string_view(*variable).starts_with(std::string(kAppImageProgramVariable) + "=")) {
      environment.emplace_back(*variable);
    }
  }
  if (!command->appimage_program.empty()) {
    environment.push_back(std::string(kAppImageProgramVariable) + "=" + command->appimage_program);
  }
  std::vector<char*> envp;
  for (std::string& variable : environment) {
    envp.push_back(variable.data());
  }
  envp.push_back(nullptr);
  pid_t pid = 0;
  if (const int error = posix_spawn(&pid, command->executable.c_str(), nullptr, nullptr, argv.data(), envp.data());
      error != 0) {
    REXLOG_ERROR("Relaunch failed ({})", error);
    CancelSessionLogHandoff();
    return false;
  }
  REXLOG_INFO("Launcher handoff: {} pid={}{}", Utf8(executable.filename()), pid,
              command->appimage_program.empty() ? "" : " (through the AppImage)");
  return true;
}

void CompleteLauncherHandoff(void*) {
  if (SDL_Window* window = ProgramWindow()) {
    SDL_RaiseWindow(window);
  }
  REXLOG_INFO("Launcher handoff: game window raised");
}

void HideNativeWindow(void*) {
  if (SDL_Window* window = ProgramWindow()) {
    SDL_HideWindow(window);
  }
}

#endif

}  // namespace mua
