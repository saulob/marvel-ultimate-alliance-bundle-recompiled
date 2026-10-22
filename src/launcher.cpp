#include "launcher.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/version.h>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"

#include "aspect_ratio.h"
#include "dpi.h"
#include "frame_limiter.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "multisampling.h"
#include "imgui_gamepad.h"
#include "keybinds.h"
#include "launcher_achievements.h"
#include "launcher_quit.h"
#include "log_filter.h"
#include "platform.h"
#include "render_scale.h"
#include "settings.h"

namespace mua {
namespace {

// Logical pixels.
constexpr std::pair<int, int> kLauncherWindowSize = {1280, 720};
// Share of the primary display's work area the launcher may take.
constexpr double kLauncherMaxShare = 0.9;
// How long the saved message stays on screen before the game starts.
constexpr double kPlayFeedbackSeconds = 0.4;

constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

// Marvel red over the Midnight background.
const ImVec4 kAccent = ImVec4(0.93f, 0.15f, 0.17f, 1.0f);

constexpr const char* kFrameRateHelp =
    "30 FPS is the original game rate. Higher rates speed up gameplay and rendering together, not just "
    "presentation; VSync still applies.";

constexpr const char* kAuthorUrl = "https://github.com/saulob";

trg::Theme LauncherTheme() {
  return trg::Theme::Midnight().WithAccent(kAccent);
}

constexpr float kHeaderArtAspect = 1600.0f / 180.0f;

trg::HeaderPainter LauncherHeader(ImTextureID art, float art_aspect) {
  return [art, art_aspect](const trg::HeaderContext& c) {
    c.draw->AddRectFilledMultiColor(c.min, c.max, IM_COL32(14, 18, 30, 255), IM_COL32(52, 12, 16, 255),
                                    IM_COL32(28, 8, 10, 255), IM_COL32(8, 10, 18, 255));
    if (art == ImTextureID{}) {
      return;
    }
    const float height = c.max.y - c.min.y;
    const float width = height * art_aspect;
    const ImVec2 top_left(c.max.x - width, c.min.y);
    c.draw->AddImage(ImTextureRef(art), top_left, c.max);
  };
}

std::unique_ptr<rex::ui::ImmediateTexture> LoadHeaderArt(rex::ui::ImmediateDrawer* drawer) {
  if (!drawer) {
    return nullptr;
  }
  const std::filesystem::path path =
      rex::filesystem::GetExecutableFolder() / "assets" / "launcher" / "header-art.png";
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return nullptr;
  }
  const std::vector<uint8_t> encoded((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int width = 0;
  int height = 0;
  const std::vector<uint8_t> pixels = rex::ui::DecodeImageRGBA(encoded.data(), encoded.size(), width, height);
  if (pixels.empty() || width <= 0 || height <= 0) {
    REXLOG_WARN("Launcher header art could not be decoded: {}", Utf8(path.filename()));
    return nullptr;
  }
  return drawer->CreateTexture(uint32_t(width), uint32_t(height), rex::ui::ImmediateTextureFilter::kLinear, false,
                               pixels.data());
}

trg::Fonts g_fonts;
float g_font_density = 1.0f;

const char* StateName(GameFileState state) {
  switch (state) {
    case GameFileState::kMissing:
      return "missing";
    case GameFileState::kReadError:
      return "read error";
    case GameFileState::kUnrecognized:
      return "unrecognized";
    case GameFileState::kDifferentVersion:
      return "different version";
    case GameFileState::kIncomplete:
      return "incomplete";
    case GameFileState::kValid:
      return "valid";
  }
  return "unknown";
}

GameCheck CheckAndLogGameFiles(const std::filesystem::path& game_dir) {
  GameCheck check = CheckGameFiles(game_dir);
  REXLOG_INFO("Game files: {} (profile {}, default.xex {} bytes, SHA-256 {})", StateName(check.state),
              check.profile ? check.profile->id : "none", check.xex_size,
              check.xex_sha256.empty() ? std::string("not read") : check.xex_sha256);
  return check;
}

struct StatusText {
  trg::Status status;
  std::string title;
  std::string detail;
};

StatusText Describe(const GameCheck& check, const LauncherPaths& paths) {
  const std::string_view target = DefaultProfile().display_name;
  switch (check.state) {
    case GameFileState::kMissing:
      return {trg::Status::kAttention, "Game files needed",
              std::format("Extract your own {} disc into the game folder.", target)};
    case GameFileState::kReadError:
      return {trg::Status::kAttention, "Could not read the game files",
              "Check that the game folder and default.xex can be read."};
    case GameFileState::kUnrecognized:
      return {trg::Status::kAttention, "Unrecognized game files",
              "default.xex is not a supported Marvel Ultimate Alliance release."};
    case GameFileState::kDifferentVersion:
      return {trg::Status::kAttention, "Different version of the game",
              std::format("default.xex does not match the supported {} build.", target)};
    case GameFileState::kIncomplete:
      return {trg::Status::kAttention, "Game files incomplete", std::format("Missing or changed: {}", check.missing_file)};
    case GameFileState::kValid:
      break;
  }
  if (!paths.runtime_blocker.empty()) {
    return {trg::Status::kAttention, "Game files verified", paths.runtime_blocker};
  }
  return {trg::Status::kReady, "Ready to play", Utf8(paths.game_dir)};
}

std::string HexId(uint32_t id) {
  return std::format("{:08X}", id);
}

std::string Size(int width, int height) {
  return std::to_string(width) + " \xC3\x97 " + std::to_string(height);
}

std::vector<trg::Option> RenderResolutionOptions(AspectRatio aspect) {
  std::vector<trg::Option> options;
  for (int scale = kMinRenderScale; scale <= kMaxRenderScale; ++scale) {
    std::string label = scale == 1 ? "Original" : std::to_string(scale) + "x";
    label += " \xE2\x80\x94 " + std::to_string(GuestWidth(aspect) * scale) + "x" + std::to_string(kGuestHeight * scale);
    if (scale == 2) {
      label += " \xE2\x80\x94 Recommended for 1440p";
    }
    options.push_back({std::to_string(scale), label});
  }
  return options;
}

std::vector<trg::Option> AspectRatioOptions() {
  std::vector<trg::Option> options;
  for (const AspectRatioInfo& info : kAspectRatios) {
    if (IsSupportedAspectRatio(info.aspect)) {
      options.push_back({info.key, info.label});
    }
  }
  return options;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        cb_(std::move(callbacks)),
        settings_(settings),
        check_(CheckAndLogGameFiles(paths_.game_dir)),
        monitors_(ListMonitors()),
        graphics_adapters_(ListGraphicsAdapters()) {
    header_art_ = LoadHeaderArt(paths_.immediate_drawer);
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    if (WindowFullscreen()) {
      restore_when_windowed_ = true;
      RequestFullscreen(false);
    } else {
      StartInSavedState();
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (done_) {
      return;
    }
    TrackPlacement();
    if (!nav_taken_) {
      saved_nav_flags_ = io.ConfigFlags & kNavFlags;
      saved_has_gamepad_ = io.BackendFlags & ImGuiBackendFlags_HasGamepad;
      io.ConfigFlags |= kNavFlags;
      nav_taken_ = true;
    }
    gamepad_.Update(io);
    if (!starting_ && ImGui::IsKeyPressed(ImGuiKey_F11, false) && !fullscreen_gate_.Pending()) {
      ToggleLauncherFullscreen();
    }
    if (starting_ && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds && !LeavingFullscreen()) {
      StartGame();
      return;
    }
    if (starting_) {
      // TRG's hotkeys read the same key state, so release it before Frame() runs.
      io.ClearInputKeys();
      io.ClearInputMouse();
    }
    ImGui::BeginDisabled(starting_);
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (starting_) {
      DrawStartingMessage();
    }
    switch (result) {
      case trg::Result::kPlay:
        RequestPlay();
        break;
      case trg::Result::kQuit:
        if (!starting_ && mua::LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
          Finish(cb_.quit);
        }
        break;
      case trg::Result::kNone:
        break;
    }
  }

  // Every PLAY (mouse, Enter, Xbox Start) ends here. The one save happens here; StartGame never saves.
  void RequestPlay() {
    if (starting_) {
      return;
    }
    check_ = CheckAndLogGameFiles(paths_.game_dir);
    if (check_.state != GameFileState::kValid) {
      launcher_->SetStatus(Describe(check_, paths_).title + ".", 6.0);
      return;
    }
    if (!paths_.runtime_blocker.empty()) {
      launcher_->SetStatus(paths_.runtime_blocker, 6.0);
      return;
    }
    if (!launcher_->Save()) {
      return;
    }
    starting_ = true;
    play_started_ = ImGui::GetTime();
    launcher_->SetStatus("", 0.0);
  }

  void StartGame() {
    // The game builds its own runtime next; the one that read the achievements must be gone.
    achievements_.reset();
    achievements_tried_ = false;
    if (cb_.window_changed && cb_.window_changed()) {
      if (cb_.restart && cb_.restart()) {
        Finish(nullptr);
        return;
      }
      starting_ = false;
      launcher_->SetStatus("Could not start the game in a new window. Press Play to try again.", 8);
      return;
    }
    RestoreStartupWindow();
    REXLOG_INFO("Starting the game");
    Finish(cb_.play);
  }

  // Drawn outside the disabled frame so the message keeps full brightness.
  void DrawStartingMessage() {
    constexpr const char* kText = "Settings saved. Starting game...";
    ImFont* font = g_fonts.semibold ? g_fonts.semibold : ImGui::GetFont();
    const float size = g_fonts.size * 1.25f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 text_size = font->CalcTextSizeA(size, FLT_MAX, 0.0f, kText);
    const ImVec2 pos(vp->Pos.x + (vp->Size.x - text_size.x) * 0.5f,
                     vp->Pos.y + (vp->Size.y - text_size.y) * 0.5f);
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(kAccent), kText);
  }

  // Runs inside the last frame; the in-game overlays share this ImGui context.
  void OnClose() override {
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    SavePlacement(placement_.Flush());
    REXLOG_INFO("Launcher closed");
    Close();
    if (action) {
      action();
    }
  }

  void* WindowHandle() const { return cb_.window_handle ? cb_.window_handle() : nullptr; }

  bool WindowFullscreen() const { return cb_.window_fullscreen && cb_.window_fullscreen(); }

  void StartInSavedState() {
    RestorePlacement();
    if (fullscreen_pref_.wanted()) {
      RequestFullscreen(true);
    }
  }

  void ToggleLauncherFullscreen() {
    const bool fullscreen = !WindowFullscreen();
    RequestFullscreen(fullscreen);
    fullscreen_pref_.UserSet(fullscreen);
    if (!settings_.SaveNow(LauncherFullscreenValues(fullscreen))) {
      REXLOG_WARN("Could not save the launcher fullscreen state");
    }
  }

  void RequestFullscreen(bool fullscreen) {
    fullscreen_gate_.Request(fullscreen);
    if (cb_.set_window_fullscreen) {
      cb_.set_window_fullscreen(fullscreen);
    }
  }

  // The game gets the window out of the launcher's temporary fullscreen.
  bool LeavingFullscreen() {
    if (!WindowFullscreen() || leave_gave_up_) {
      return false;
    }
    if (!leave_requested_) {
      leave_requested_ = true;
      leave_started_ = ImGui::GetTime();
      RequestFullscreen(false);
    }
    leave_gave_up_ = ImGui::GetTime() - leave_started_ >= FullscreenGate::kTimeoutSeconds;
    return !leave_gave_up_;
  }

  void RestorePlacement() {
    if (const auto placed = TakePreShowResult()) {
      startup_ = placed->startup;
      placement_ = PlacementTracker(placed->applied);
      return;
    }
    void* window = WindowHandle();
    const auto state = QueryWindowState(window);
    if (!state || WindowFullscreen()) {
      return;
    }
    const WindowPlacement saved = SavedLauncherPlacement();
    const bool restorable = PlacementIsVisible(saved, WorkAreas());
    startup_ = WindowPlacement{state->rect, state->maximized};
    placement_ = PlacementTracker(restorable ? std::optional<WindowPlacement>(saved) : std::nullopt);
    if (restorable) {
      ApplyWindowPlacement(window, saved);
    }
  }

  void TrackPlacement() {
    const double now = ImGui::GetTime();
    const FullscreenGate::Step step = fullscreen_gate_.Update(WindowFullscreen(), now);
    if (restore_when_windowed_) {
      if (!WindowFullscreen() && !fullscreen_gate_.Pending()) {
        restore_when_windowed_ = false;
        StartInSavedState();
      }
      return;
    }
    if (!startup_) {
      return;
    }
    if (step.restore) {
      if (const auto current = placement_.Current()) {
        ApplyWindowPlacement(WindowHandle(), *current);
      }
    }
    if (!step.observe) {
      return;
    }
    if (const auto state = QueryWindowState(WindowHandle())) {
      placement_.Observe(*state, WindowFullscreen(), now);
      SavePlacement(placement_.TakeSettled(now));
    }
  }

  void SavePlacement(const std::optional<WindowPlacement>& placement) {
    if (placement && !settings_.SaveNow(PlacementValues(*placement))) {
      REXLOG_WARN("Could not save the launcher window placement");
    }
  }

  // The game keeps the window it was created with, not the launcher's saved placement.
  void RestoreStartupWindow() {
    if (startup_ && !WindowFullscreen()) {
      ApplyWindowPlacement(WindowHandle(), *startup_);
    }
  }

  bool GameReady() const { return check_.state == GameFileState::kValid; }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    // PLAY saves in RequestPlay, so a failed save keeps the launcher open.
    c.save_on_play = false;
    c.theme = LauncherTheme();
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
    c.fonts = g_fonts;
    c.branding.title = "MARVEL ULTIMATE ALLIANCE";
    c.branding.subtitle_segments = {
        {"PC PORT   \xC2\xB7   ", ""},
        {"v" MUA_PORT_VERSION "   \xC2\xB7   ", ""},
        {"BY SAULOB", kAuthorUrl},
    };
    const float art_aspect =
        header_art_ ? float(header_art_->width) / float(header_art_->height) : kHeaderArtAspect;
    c.branding.background = LauncherHeader(header_art_ ? reinterpret_cast<ImTextureID>(header_art_.get())
                                                       : ImTextureID{},
                                           art_aspect);
    c.pages = {
        {"Play", "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
         [this] { return !GameReady(); }},
        {"Display", "Window, monitor and how the picture fits your screen.",
         [this](trg::Ui& ui) { PageDisplay(ui); }},
        {"Graphics", "Render resolution, anti-aliasing and texture filtering.",
         [this](trg::Ui& ui) { PageGraphics(ui); }},
        {"Gameplay", "What the window shows while you play.", [](trg::Ui& ui) { PageGameplay(ui); }},
        {"Achievements", "Your achievements on this PC.", [this](trg::Ui& ui) { PageAchievements(ui); }},
        {"Controls", "Controller and keyboard.", [](trg::Ui& ui) { PageControls(ui); }},
        {"About", "About this port, and where your files and settings live.",
         [this](trg::Ui& ui) { PageAbout(ui); }},
    };
    c.can_play = [this] {
      if (!GameReady()) {
        return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
      }
      if (!paths_.runtime_blocker.empty()) {
        return trg::PlayCheck{false, paths_.runtime_blocker, 0};
      }
      return trg::PlayCheck{};
    };
    return c;
  }

  void PagePlay(trg::Ui& ui) {
    const StatusText text = Describe(check_, paths_);
    ui.StatusCard(text.status, text.title.c_str(), text.detail.c_str());

    if (paths_.game_dir.empty()) {
      ui.Help("No game folder was given: start the game with --game_data_root.");
    } else {
      const std::string target(DefaultProfile().display_name);
      const std::string detected = check_.profile ? std::string(check_.profile->display_name) : "Not detected";
      const std::string ids = check_.identity ? std::format("{} / {}", HexId(check_.identity->title_id),
                                                             HexId(check_.identity->media_id))
                                              : "Not read";
      ui.Info("Target", target.c_str());
      ui.Info("Detected", detected.c_str());
      ui.Info("Title ID / Media ID", ids.c_str());
      ui.Info("Recompiled code", paths_.runtime_blocker.empty() ? "Connected" : "Not generated");
      ui.FolderRow("Game files",
                   "No game files are included with this port. Extract your own disc into this folder, then check again.",
                   "Open game folder", Utf8(paths_.game_dir));
      if (ui.ButtonRow("Check game files", "Check the game folder again after copying files into it.",
                       "Check again")) {
        check_ = CheckAndLogGameFiles(paths_.game_dir);
        achievements_.reset();
        achievements_tried_ = false;
      }
    }
    ui.LauncherVisibilityRow("mua_launcher");
    ui.EndRows();
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode",
              "How the game starts. Borderless Fullscreen fills the monitor. F11 switches while playing.", "fullscreen",
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", "The game window's size in Windowed mode.");
    WindowSizeCombo();
    ui.Row("Monitor", "The display the game opens on.");
    MonitorCombo();
    const std::vector<trg::Option> aspects = AspectRatioOptions();
    if (aspects.size() > 1 &&
        ui.Combo("Aspect Ratio", "Sets the game's target rendering format and the Window size choices.",
                 "mua_aspect_ratio", "16:9", aspects)) {
      SelectWindowSizeForAspect();
    }
    ui.Row("VSync", "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.");
    {
      const bool tearing = settings_.GetBool(kTearingSetting, true);
      if (int i = ui.Segmented("vsync", {"Off", "On"}, tearing ? 0 : 1); i >= 0) {
        settings_.SetBool(kTearingSetting, i == 0);
      }
    }
  }

  static void PageGameplay(trg::Ui& ui) {
    std::vector<trg::Option> frame_rates;
    for (auto& [value, label] : FrameRateChoices()) {
      frame_rates.push_back({std::move(value), std::move(label)});
    }
    ui.Choice("Frame rate", kFrameRateHelp, "mua_frame_rate", "60", frame_rates, 4);
    ui.Toggle("Frame rate in title",
              "Shows the game's frame rate in the window title: Marvel Ultimate Alliance Recompiled | FPS: 60.00",
              "mua_show_fps", true, "Off", "On");
    ui.Toggle("Render resolution in title", "Shows the game's render resolution in the window title",
              "mua_show_render_resolution", false, "Off", "On");
    ui.Toggle("Frame counter", "Shows the game's frame rate in the corner. F2 toggles it while playing.",
              "mua_show_frame_counter", false, "Hidden", "Shown");
  }

  const MonitorInfo* SelectedMonitor() const {
    const int index = settings_.GetInt("monitor", 0);
    if (index >= 1 && index <= int(monitors_.size())) {
      return &monitors_[size_t(index - 1)];
    }
    for (const MonitorInfo& m : monitors_) {
      if (m.primary) {
        return &m;
      }
    }
    return monitors_.empty() ? nullptr : &monitors_.front();
  }

  AspectRatio SelectedAspectRatio() const {
    return ParseAspectRatio(settings_.Get("mua_aspect_ratio", "16:9"));
  }

  static bool WindowSizeFits(int width, int height, const MonitorInfo* monitor) {
    return !monitor || (width * monitor->scale < monitor->width && height * monitor->scale < monitor->height);
  }

  // Moves Window size to the new aspect ratio's default, unless the current size already belongs to it.
  void SelectWindowSizeForAspect() {
    const AspectRatio aspect = SelectedAspectRatio();
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect == aspect && size.width == width && size.height == height) {
        return;
      }
    }
    const AspectRatioInfo& info = InfoFor(aspect);
    const MonitorInfo* monitor = SelectedMonitor();
    const WindowSize* chosen = nullptr;
    for (const WindowSize& size : kWindowSizes) {
      if (size.aspect != aspect || !WindowSizeFits(size.width, size.height, monitor)) {
        continue;
      }
      if (size.width == info.default_width && size.height == info.default_height) {
        chosen = &size;
        break;
      }
      if (!chosen || size.width > chosen->width) {
        chosen = &size;
      }
    }
    if (chosen) {
      settings_.SetInt("window_width", chosen->width);
      settings_.SetInt("window_height", chosen->height);
    }
  }

  // window_width/height are logical pixels; display scaling enlarges them.
  void WindowSizeCombo() {
    const int width = settings_.GetInt("window_width", 0);
    const int height = settings_.GetInt("window_height", 0);
    const AspectRatio aspect = SelectedAspectRatio();
    const MonitorInfo* monitor = SelectedMonitor();
    const float scale = monitor ? monitor->scale : 1.0f;
    auto label = [scale](int w, int h) {
      if (w <= 0 || h <= 0) {
        return std::string("Default");
      }
      std::string text = Size(w, h);
      if (scale != 1.0f) {
        text += "   (" + Size(int(std::lround(w * scale)), int(std::lround(h * scale))) + " on screen)";
      }
      return text;
    };
    if (ImGui::BeginCombo("##window_size", label(width, height).c_str())) {
      for (const WindowSize& size : kWindowSizes) {
        if (size.aspect != aspect) {
          continue;
        }
        const bool current = size.width == width && size.height == height;
        if (!current && !WindowSizeFits(size.width, size.height, monitor)) {
          continue;
        }
        if (ImGui::Selectable(label(size.width, size.height).c_str(), current) && !current) {
          settings_.SetInt("window_width", size.width);
          settings_.SetInt("window_height", size.height);
        }
      }
      ImGui::EndCombo();
    }
  }

  void MonitorCombo() {
    const int current = settings_.GetInt("monitor", 0);
    auto label = [this](int index) {
      if (index <= 0) {
        return std::string("Default");
      }
      if (index > int(monitors_.size())) {
        return "Display " + std::to_string(index) + " (not connected)";
      }
      const MonitorInfo& m = monitors_[size_t(index - 1)];
      return "Display " + std::to_string(index) + (m.primary ? " (primary)" : "") + "   " + Size(m.width, m.height);
    };
    if (ImGui::BeginCombo("##monitor", label(current).c_str())) {
      for (int i = 1; i <= int(monitors_.size()); ++i) {
        if (ImGui::Selectable(label(i).c_str(), i == current) && i != current) {
          settings_.SetInt("monitor", i);
        }
      }
      ImGui::EndCombo();
    }
  }

  void PageGraphics(trg::Ui& ui) {
    if (!graphics_adapters_.empty()) {
      std::vector<trg::Option> adapters;
      for (const auto& [value, label] :
           GraphicsAdapterOptions(graphics_adapters_, settings_.Get("mua_graphics_adapter", kGraphicsAdapterAuto))) {
        adapters.push_back({value, label});
      }
      ui.Combo("Graphics card",
               "Selects which GPU renders the game. Automatic uses the recommended high-performance GPU. "
               "A GPU chosen here overrides automatic selection and restarts the game window.",
               "mua_graphics_adapter", kGraphicsAdapterAuto, adapters);
    }
    ui.Combo("Render resolution",
             "Draws the game at a multiple of its own picture, then scales it to the window. Higher is sharper and "
             "costs more GPU time. Window size is separate.",
             "resolution_scale", "1", RenderResolutionOptions(SelectedAspectRatio()));
    ui.Choice("Anti-aliasing", "Smooths jagged edges after the frame is drawn. Extreme is softer but cleaner.",
              "swap_post_effect", "none", {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA Extreme"}});
    if (MultisamplingApplies()) {
      ui.Toggle("Multisampling",
                "Use 2\xC3\x97 MSAA for best performance. Try 4\xC3\x97 only if you see graphics problems with 2\xC3\x97.",
                "mua_msaa_compatibility", false, "2\xC3\x97 MSAA", "4\xC3\x97 MSAA (Compatibility)");
    }
    ui.Choice("Texture filtering", "Anisotropic filtering keeps textures sharp at oblique angles. Game leaves it to the game.",
              "anisotropic_override", "3",
              {{"-1", "Game"},
               {"0", "Off"},
               {"2", "2\xC3\x97"},
               {"3", "4\xC3\x97"},
               {"4", "8\xC3\x97"},
               {"5", "16\xC3\x97"}},
              6);
  }

  // The choice only matters when the GPU cannot run the rasterizer-ordered views EDRAM path.
  bool MultisamplingApplies() {
    const std::string adapter = settings_.Get("mua_graphics_adapter", kGraphicsAdapterAuto);
    if (!host_render_targets_checked_ || adapter != host_render_targets_adapter_) {
      host_render_targets_checked_ = true;
      host_render_targets_adapter_ = adapter;
      host_render_targets_ = HostRenderTargetsUsed(
          rex::cvar::GetFlagByName("render_target_path_d3d12"),
          RasterizerOrderedViewsSupported(ResolveGraphicsAdapter(adapter, graphics_adapters_)));
    }
    return host_render_targets_;
  }

  void PageAchievements(trg::Ui& ui) {
    if (!GameReady()) {
      ui.Paragraph("Extract the game files to see its achievements.");
      return;
    }
    if (!achievements_ && !achievements_tried_) {
      achievements_tried_ = true;
      achievements_ = std::make_unique<LauncherAchievements>(paths_.game_dir, paths_.user_dir, paths_.immediate_drawer);
    }
    if (!achievements_ || !achievements_->ok()) {
      ui.Paragraph("The achievements could not be read from the game files.");
      return;
    }
    DrawAchievementSummary(ui, Summarize(achievements_->entries()));
    std::vector<trg::AchievementCard> cards;
    for (const AchievementEntry& entry : achievements_->entries()) {
      trg::AchievementCard card;
      card.icon = achievements_->Icon(entry.info);
      card.title = entry.info.label;
      card.description = EntryDescription(entry);
      card.points = static_cast<int>(entry.info.gamerscore);
      card.unlocked = entry.unlocked;
      cards.push_back(std::move(card));
    }
    ui.AchievementGrid(cards, 1);
  }

  static void DrawAchievementSummary(trg::Ui& ui, const AchievementSummary& summary) {
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::Text("%d / %d unlocked", summary.unlocked, summary.total);
    ImGui::SameLine();
    ImGui::TextColored(kAccent, "%dG / %dG", summary.earned_gamerscore, summary.total_gamerscore);
    ImGui::PopFont();
    const float fraction = summary.total > 0 ? float(summary.unlocked) / float(summary.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kAccent);
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 6.0f * ui.scale()), "");
    ImGui::PopStyleColor();
    ui.Spacer(8);
  }

  static void PageControls(trg::Ui& ui) {
    ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
    ui.Toggle("Keyboard", "Use the keyboard for gameplay controls together with the controller.", "mnk_mode", true,
              "Off", "On");
    if (ui.ConfirmRow("Keyboard defaults", "Puts the keyboard controls back to their standard layout.",
                      "Reset to defaults", "Put every key back to its default?", "Reset")) {
      ResetKeyboardDefaults(ui.settings());
      ui.SetStatus("Keyboard controls reset to defaults.");
    }
    if (ui.Section("Keyboard controls", true)) {
      ui.Help("Click a key, then press the new one. Esc cancels.");
      const bool enabled = ui.settings().GetBool("mnk_mode", true);
      if (!enabled) {
        ImGui::BeginDisabled();
      }
      for (const KeyboardAction& action : kKeyboardActions) {
        KeyboardRow(ui, action);
      }
      if (!enabled) {
        ImGui::EndDisabled();
      }
    }
  }

  // The key table holds the ReXGlue keybind_* settings, so a row edits the same store the rest of
  // the launcher uses. The store keeps the edit pending until Save or Play.
  static void KeyboardRow(trg::Ui& ui, const KeyboardAction& action) {
    const std::string shown = KeyboardBindingLabel(SavedKeyboardBinding(ui.settings(), action));
    const std::optional<ImGuiKey> key = ui.KeyBindRow(action.label, nullptr, shown);
    if (!key) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const std::string binding = KeyboardBindingFor(*key, io.KeyShift, io.KeyCtrl, io.KeyAlt);
    if (binding.empty()) {
      ui.SetStatus("That key cannot be used.");
      return;
    }
    ui.settings().Set(action.key, binding);
  }

  void PageAbout(trg::Ui& ui) {
    ui.Paragraph(
        "Marvel Ultimate Alliance for Xbox 360, statically recompiled to run natively on PC with the ReXGlue "
        "SDK. No game code or assets are included: the game runs from your own game files.");
    ui.Spacer(4);
    if (!paths_.game_dir.empty()) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(paths_.game_dir));
    }
    ui.FolderRow("Saves and settings", UserDataFolderHelp(paths_.user_data_outcome), "Open data folder",
                 Utf8(paths_.user_dir));
    if (ui.ButtonRow("Settings file",
                     "Your launcher choices, as plain text. Loaded after marvel_ultimate_alliance.toml.",
                     "Show settings file") &&
        launcher_->Save()) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(paths_.settings_file, ec);
      trg::OpenInFileManager(Utf8(exists ? paths_.settings_file : paths_.user_dir));
    }
    ui.ResetAllRow();
    ui.Heading("Diagnostics");
    if (ui.Toggle("Detailed logging", "Keeps logs for normal sessions. Crash diagnostics are always preserved.",
                  "detailed_logging", true)) {
      rex::cvar::SetFlagByName("detailed_logging", settings_.Get("detailed_logging", "true"));
      mua::ApplyDetailedLogging();
    }
    if (ui.ButtonRow("Logs folder", "Open the folder containing session and crash logs", "Open logs folder")) {
      OpenLogsFolder();
    }
    ui.Info("Port version", MUA_PORT_VERSION);
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "   \xC2\xB7   TRG Launcher " MUA_TRG_VERSION);
    const std::string game = std::format("Xbox 360 disc, title {}, {}", HexId(DefaultProfile().title_id),
                                         DefaultProfile().display_name);
    ui.Info("Game", game.c_str());
  }

  void OpenLogsFolder() {
    std::error_code ec;
    std::filesystem::path folder = paths_.logs_dir;
    std::filesystem::create_directories(folder, ec);
    if (!std::filesystem::is_directory(folder, ec)) {
      REXLOG_WARN("Could not create the logs folder: {}", ec.message());
      folder = folder.parent_path();
    }
    trg::OpenInFileManager(Utf8(folder));
  }

  LauncherPaths paths_;
  LauncherCallbacks cb_;
  GameSettings& settings_;
  GameCheck check_;
  std::vector<MonitorInfo> monitors_;
  std::vector<GraphicsAdapter> graphics_adapters_;
  std::string host_render_targets_adapter_;
  bool host_render_targets_checked_ = false;
  bool host_render_targets_ = true;
  std::unique_ptr<rex::ui::ImmediateTexture> header_art_;
  std::unique_ptr<trg::Launcher> launcher_;
  ImGuiGamepad gamepad_;
  bool nav_taken_ = false;
  bool starting_ = false;
  double play_started_ = 0.0;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  bool done_ = false;
  std::optional<WindowPlacement> startup_;
  PlacementTracker placement_;
  FullscreenGate fullscreen_gate_;
  LauncherFullscreenPreference fullscreen_pref_{SavedLauncherFullscreen()};
  bool restore_when_windowed_ = false;
  bool leave_requested_ = false;
  bool leave_gave_up_ = false;
  double leave_started_ = 0.0;
  std::unique_ptr<LauncherAchievements> achievements_;
  bool achievements_tried_ = false;
};

}  // namespace

void LoadLauncherFonts(ImFontAtlas* atlas, float density) {
  // Latin-1 plus General Punctuation.
  static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x2010, 0x205E, 0};
  density = ValidDpiScale(density);
  g_font_density = density;
  ImFontConfig config;
  config.RasterizerDensity = density;
  auto load = [&](const std::filesystem::path& path) -> ImFont* {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) {
      return nullptr;
    }
    return atlas->AddFontFromFileTTF(Utf8(path).c_str(), g_fonts.size, &config, kRanges);
  };
  const UiFontFiles fonts = FindUiFonts();
  g_fonts.regular = load(fonts.regular);
  g_fonts.semibold = load(fonts.semibold);
  g_fonts.bold = load(fonts.bold);
}

void UpdateLauncherFonts(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, float density) {
  if (!drawer || !immediate_drawer || !g_fonts.regular) {
    return;
  }
  density = ValidDpiScale(density);
  if (std::abs(density - g_font_density) < 0.001f) {
    return;
  }

  ImGuiIO& io = drawer->GetIO();
  for (ImFont* font : {g_fonts.regular, g_fonts.semibold, g_fonts.bold}) {
    if (!font) {
      continue;
    }
    const ImWchar fallback = font->FallbackChar;
    const ImWchar ellipsis = font->EllipsisChar;
    font->ClearOutputData();
    font->FallbackChar = fallback;
    font->EllipsisChar = ellipsis;
    font->CurrentRasterizerDensity = density;
    for (ImFontConfig* source : font->Sources) {
      source->RasterizerDensity = density;
    }
  }
  io.Fonts->TexIsBuilt = false;
  // The drawer holds the uploaded atlas until the immediate drawer is replaced.
  drawer->SetImmediateDrawer(nullptr);
  drawer->SetImmediateDrawer(immediate_drawer);
  g_font_density = density;
}

bool ShouldShowLauncher(const std::filesystem::path& game_dir) {
  const bool skip_once = REXCVAR_GET(mua_skip_launcher);
  rex::cvar::ResetToDefault("mua_skip_launcher");
  return LauncherWanted(CheckAndLogGameFiles(game_dir).state == GameFileState::kValid, skip_once, ShiftHeld(),
                        REXCVAR_GET(mua_launcher));
}

namespace {

std::pair<int, int> LauncherWindowSize() {
  const MonitorInfo primary = MonitorForSetting(1);
  if (primary.width == 0) {
    return kLauncherWindowSize;
  }
  const DpiWindowSize size =
      FitWindowToWorkArea(kLauncherWindowSize.first, kLauncherWindowSize.second, primary.work_width,
                          primary.work_height, primary.scale, kLauncherMaxShare);
  return {size.width, size.height};
}

}  // namespace

void PrepareLauncherWindow() {
  const auto [width, height] = LauncherWindowSize();
  rex::cvar::SetFlagByName("fullscreen", "false");
  rex::cvar::SetFlagByName("window_width", std::to_string(width));
  rex::cvar::SetFlagByName("window_height", std::to_string(height));
  // Monitor 0 lets SDL centre the window on the primary display, whatever the display order.
  rex::cvar::SetFlagByName("monitor", "0");
  const WindowPlacement saved = SavedLauncherPlacement();
  InstallPreShowPlacement(PlanPreShow(saved, WorkAreas()), saved);
}

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameSettings& settings,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), settings, std::move(callbacks));
}

}  // namespace mua
