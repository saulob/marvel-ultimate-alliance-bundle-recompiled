#include "launcher.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <format>
#include <iterator>
#include <map>
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
#include "game_definition.h"
#include "disc_identity.h"
#include "dlss_support.h"
#include "fsr3_support.h"
#include "game_files.h"
#include "game_install.h"
#include "game_library.h"
#include "game_theme.h"
#include "graphics_adapters.h"
#include "hd_text.h"
#include "multisampling.h"
#include "imgui_gamepad.h"
#include "keybinds.h"
#include "launcher_achievements.h"
#include "launcher_header_art.h"
#include "launcher_quit.h"
#include "launcher_shortcuts.h"
#include "log_filter.h"
#include "platform.h"
#include "player_names.h"
#include "port_version.h"
#include "render_scale.h"
#include "settings.h"
#include "update_config.h"
#include "updater/update_ui.h"
#include "upscaling.h"

namespace mua {
namespace {

// Logical pixels.
constexpr std::pair<int, int> kLauncherWindowSize = {1280, 720};
// Share of the primary display's work area the launcher may take.
constexpr double kLauncherMaxShare = 0.9;
// How long the saved message stays on screen before the game starts.
constexpr double kPlayFeedbackSeconds = 0.4;
// Logical pixels.
constexpr float kHeaderHeight = 92.0f;

constexpr ImGuiConfigFlags kNavFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

std::string FrameRateHelp(const FramePolicy& policy) {
  return std::to_string(policy.native) +
         " FPS is the original game rate. Higher rates speed up gameplay and rendering together, not just "
         "presentation; VSync still applies.";
}

constexpr const char* kAuthorUrl = "https://github.com/saulob";

ImVec4 Rgb(uint32_t rgb) {
  return ImVec4(float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f, float(rgb & 0xFF) / 255.0f, 1.0f);
}

trg::Theme LauncherTheme(GameId game) {
  const GamePalette palette = PaletteOf(game);
  trg::Theme theme = trg::Theme::Midnight().WithAccent(Rgb(palette.primary));
  theme.accent_hot = Rgb(palette.hover);
  theme.border = Rgb(palette.border);
  return theme;
}

ImU32 Shade(const ImVec4& color, float factor) {
  return IM_COL32(int(color.x * factor * 255.0f), int(color.y * factor * 255.0f), int(color.z * factor * 255.0f), 255);
}

constexpr float kHeaderArtAspect = 1600.0f / 180.0f;

trg::HeaderPainter LauncherHeader(ImTextureID art, float art_aspect) {
  return [art, art_aspect](const trg::HeaderContext& c) {
    c.draw->AddRectFilledMultiColor(c.min, c.max, IM_COL32(14, 18, 30, 255), Shade(c.theme.accent, 0.22f),
                                    Shade(c.theme.accent, 0.12f), IM_COL32(8, 10, 18, 255));
    if (art == ImTextureID{}) {
      return;
    }
    const float height = c.max.y - c.min.y;
    const float width = height * art_aspect;
    const ImVec2 top_left(c.max.x - width, c.min.y);
    c.draw->AddImage(ImTextureRef(art), top_left, c.max);
  };
}

std::unique_ptr<rex::ui::ImmediateTexture> LoadHeaderArt(rex::ui::ImmediateDrawer* drawer, GameId game) {
  if (!drawer) {
    return nullptr;
  }
  const std::filesystem::path path =
      rex::filesystem::GetExecutableFolder() / "assets" / "launcher" / HeaderArtFile(game);
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

void LogGameFiles(const GameState& state) {
  const GameCheck& check = state.check;
  REXLOG_INFO("Game files ({}): {} (profile {}, default.xex {} bytes, SHA-256 {})", state.game->key,
              StateName(check.state), check.profile ? check.profile->id : "none", check.xex_size,
              check.xex_sha256.empty() ? std::string("not read") : check.xex_sha256);
}

struct StatusText {
  trg::Status status;
  std::string title;
  std::string detail;
};

StatusText Describe(const GameState& state, std::string_view runtime_blocker) {
  switch (AvailabilityOf(state, runtime_blocker)) {
    case GameAvailability::kReady:
      return {trg::Status::kReady, "Ready to play", Utf8(state.data_dir)};
    case GameAvailability::kRuntimeUnavailable:
      return {trg::Status::kAttention, "Game files verified",
              state.game->runtime_supported ? std::string(runtime_blocker)
                                            : std::string("This game has no recompiled runtime in this build yet.")};
    case GameAvailability::kUnsupportedFiles:
      return {trg::Status::kAttention, "Different version of the game",
              "The installed files are not the supported version."};
    case GameAvailability::kIncomplete:
      return {trg::Status::kAttention, "Game files incomplete",
              std::format("Missing or changed: {}", state.check.missing_file)};
    case GameAvailability::kUnreadable:
      return {trg::Status::kAttention, "Could not read the game files",
              "Check that the game folder and default.xex can be read."};
    case GameAvailability::kNotInstalled:
      break;
  }
  return {trg::Status::kAttention, "Game files needed", "This port requires your own Xbox 360 disc image."};
}

std::string PlayBlockedReason(const GameState& state, std::string_view runtime_blocker) {
  if (AvailabilityOf(state, runtime_blocker) != GameAvailability::kRuntimeUnavailable) {
    return "The game files are not ready: see the Play page.";
  }
  return state.game->runtime_supported ? std::string(runtime_blocker)
                                       : std::format("{} cannot be started by this port yet.", state.game->display_name);
}

std::string HexId(uint32_t id) {
  return std::format("{:08X}", id);
}

std::string PackageText(const GameState& state) {
  return std::format("{} \xC2\xB7 Title ID {}", state.game->package_label,
                     HexId(ProfileOf(*state.game).title_id));
}

struct InstallOutcome {
  InstallResult result;
  const GameDefinition* game = nullptr;
};

void DrawFailure(trg::Ui& ui, const std::string& title, const std::string& text) {
  ui.Spacer(4);
  ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
  ImGui::TextUnformatted(title.c_str());
  ImGui::PopStyleColor();
  ui.Help(text.c_str());
  ui.Spacer(4);
}

std::string Size(int width, int height) {
  return std::to_string(width) + " \xC3\x97 " + std::to_string(height);
}

std::vector<trg::Option> RenderResolutionOptions(AspectRatio aspect) {
  std::vector<trg::Option> options;
  for (int scale = kMinRenderScale; scale <= kMaxRenderScale; ++scale) {
    options.push_back({std::to_string(scale), RenderResolutionLabel(aspect, scale)});
  }
  return options;
}

std::vector<trg::Option> UpscalerOptions(UpscalerMask offered) {
  std::vector<trg::Option> options;
  for (const UpscalerInfo& info : kUpscalers) {
    if (offered & UpscalerBit(info.upscaler)) {
      options.push_back({info.key, info.label});
    }
  }
  return options;
}

const char* UpscalingHelp(UpscalerMask offered) {
  if (offered == kFsr1Upscalers) {
    return "AMD FSR 1 scales the game's picture to the window with sharper edges than plain scaling. Quality, "
           "Balanced and Performance draw the game at a lower resolution than the Render resolution, which costs less "
           "GPU time, and FSR 1 scales it up to the window.";
  }
  return "AMD FSR 1 scales the game's picture to the window with sharper edges than plain scaling; its Quality, "
         "Balanced and Performance draw the game below the Render resolution, which costs less GPU time. AMD FSR 3.1 "
         "(any graphics card: NVIDIA, AMD and Intel) and NVIDIA DLSS (GeForce RTX) draw only the 3D scene smaller and "
         "rebuild it at the Render resolution from several frames, using motion and depth, so the HUD and menus stay "
         "sharp.";
}

// Each quality with the size the game draws at and the Render resolution the upscaler brings it to.
std::vector<trg::Option> UpscaleQualityOptions(AspectRatio aspect, int render_scale) {
  std::vector<trg::Option> options;
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    if (!OffersQuality(Upscaler::kFsr1, info.quality)) {
      continue;
    }
    const RenderSizes sizes = RenderSizesFor(aspect, render_scale, Upscaler::kFsr1, info.quality);
    std::string label = std::string(info.label) + " \xE2\x80\x94 " + SizeText(sizes.internal);
    if (info.quality != UpscaleQuality::kNative) {
      label += " (game " + SizeText(sizes.guest) + ")";
    }
    options.push_back({info.key, label});
  }
  return options;
}

// NVIDIA's runtime picks the exact scene size of each mode.
std::vector<trg::Option> DlssQualityOptions(AspectRatio aspect, int render_scale, WindowSize scene_size) {
  std::vector<trg::Option> options;
  const WindowSize canvas = SceneCanvas(aspect, scene_size);
  const int scale = render_scale < 1 ? 1 : render_scale;
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    if (!OffersQuality(Upscaler::kDlss, info.quality)) {
      continue;
    }
    const WindowSize reduced = UpscaledBackBuffer(canvas, Upscaler::kFsr1, info.quality);
    std::string label = std::string(QualityLabel(Upscaler::kDlss, info.quality)) + " \xE2\x80\x94 3D scene ";
    label += info.quality == UpscaleQuality::kNative
                 ? SizeText({canvas.width * scale, canvas.height * scale})
                 : "about " + SizeText({reduced.width * scale, reduced.height * scale});
    options.push_back({info.key, label});
  }
  return options;
}

// AMD's ratios give the exact scene size of each mode; the HUD and menus stay at the Render resolution.
std::vector<trg::Option> Fsr3QualityOptions(AspectRatio aspect, int render_scale, WindowSize scene_size) {
  std::vector<trg::Option> options;
  const WindowSize canvas = SceneCanvas(aspect, scene_size);
  const int scale = ClampRenderScale(render_scale);
  const WindowSize output = {canvas.width * scale, canvas.height * scale};
  for (const UpscaleQualityInfo& info : kUpscaleQualities) {
    const WindowSize scene = Fsr3SceneSize(output, canvas.width, info.quality);
    options.push_back({info.key, std::string(info.label) + " \xE2\x80\x94 3D scene " + SizeText(scene)});
  }
  return options;
}

std::vector<trg::Option> AspectRatioOptions() {
  std::vector<trg::Option> options;
  for (const AspectRatioInfo& info : kAspectRatios) {
    options.push_back({info.key, info.label});
  }
  return options;
}

class LauncherDialog final : public rex::ui::ImGuiDialog {
 public:
  LauncherDialog(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameLibrary library, GameSettings& settings,
                 LauncherCallbacks callbacks)
      : ImGuiDialog(drawer),
        paths_(std::move(paths)),
        cb_(std::move(callbacks)),
        library_(std::move(library)),
        settings_(settings),
        selected_(settings.SelectedGame()),
        monitors_(ListMonitors()),
        graphics_adapters_(ListGraphicsAdapters()) {
    if (SelectedAspectRatio() != AspectRatio::k16x9) {
      SelectWindowSizeForAspect();
    }
    launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
    updater_ = std::make_unique<trg::update::UpdateController>(MakeUpdateConfig(PortFolder()));
    updater_->StartCheck();
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
    PollInstall();
    PollUpdate();
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
    HandleSaveShortcut(io);
    ImGui::BeginDisabled(starting_);
    const trg::Result result = launcher_->Frame();
    ImGui::EndDisabled();
    if (result == trg::Result::kNone && !starting_) {
      HandleUpdateAction(trg::update::DrawUpdateBanner(updater_->snapshot(), launcher_->config().theme, g_fonts,
                                                       kHeaderHeight));
    }
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

  // The same Launcher::Save the Save button runs; TRG's own Ctrl+S branch is patched out.
  void HandleSaveShortcut(const ImGuiIO& io) {
    SaveShortcutState state;
    state.ctrl_down = io.KeyCtrl || ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
    state.s_pressed = ImGui::IsKeyPressed(ImGuiKey_S, false);
    state.text_input = io.WantTextInput;
    state.capturing_key = launcher_->capturing_key();
    state.popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    state.starting = starting_;
    if (SaveShortcutFires(state)) {
      launcher_->Save();
    }
  }

  // Every PLAY (mouse, Enter, Xbox Start) ends here. The one save happens here; StartGame never saves.
  void RequestPlay() {
    if (starting_ || install_.running()) {
      return;
    }
    library_.RefreshFiles(selected_);
    LogGameFiles(Selected());
    if (!GameReady()) {
      launcher_->SetStatus(PlayBlockedReason(Selected(), RuntimeBlocker()), 6.0);
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
    if (cb_.other_executable && cb_.other_executable(*Selected().game)) {
      if (cb_.start_other_executable && cb_.start_other_executable(Selected())) {
        Finish(nullptr);
        return;
      }
      starting_ = false;
      launcher_->SetStatus("Could not start the game's program. Press Play to try again.", 8);
      return;
    }
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
    REXLOG_INFO("Starting {}", Selected().game->display_name);
    Finish([play = cb_.play, game = Selected()] {
      if (play) {
        play(game);
      }
    });
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
    ImGui::GetForegroundDrawList()->AddText(font, size, pos, ImGui::GetColorU32(launcher_->config().theme.accent), kText);
  }

  // Runs inside the last frame; the in-game overlays share this ImGui context.
  void OnClose() override {
    StopUpdater();
    ImGuiIO& io = GetIO();
    gamepad_.Release(io);
    io.ConfigFlags = (io.ConfigFlags & ~kNavFlags) | saved_nav_flags_;
    io.BackendFlags = (io.BackendFlags & ~ImGuiBackendFlags_HasGamepad) | saved_has_gamepad_;
  }

 private:
  void Finish(std::function<void()> action) {
    done_ = true;
    StopUpdater();
    if (!skip_finish_save_) {
      SavePlacement(placement_.Flush());
    }
    REXLOG_INFO("Launcher closed");
    Close();
    if (action) {
      action();
    }
  }

  void StopUpdater() {
    if (!updater_) {
      return;
    }
    updater_->Cancel();
    if (updater_->Busy()) {
      // A network call must not be joined on the UI thread while the launcher leaves.
      updater_.release();
    }
  }

  void PollUpdate() {
    if (updater_) {
      updater_->Poll();
    }
  }

  // The same Save the Play button runs, so the player's choices survive the restart.
  bool SaveBeforeUpdateRestart() {
    if (install_.running() || starting_) {
      launcher_->SetStatus("Wait for the current install to finish.", 5.0);
      return false;
    }
    if (!launcher_->Save()) {
      return false;
    }
    SavePlacement(placement_.Flush());
    return true;
  }

  void HandleUpdateAction(trg::update::UpdateUiAction action) {
    switch (action) {
      case trg::update::UpdateUiAction::kDownload:
        updater_->StartDownload();
        break;
      case trg::update::UpdateUiAction::kRetry:
        if (!updater_->RetryNeedsInstall()) {
          updater_->Retry();
        } else if (SaveBeforeUpdateRestart() && updater_->Retry()) {
          skip_finish_save_ = true;
          Finish(cb_.quit);
        }
        break;
      case trg::update::UpdateUiAction::kRestart:
        if (SaveBeforeUpdateRestart() && updater_->BeginInstall()) {
          skip_finish_save_ = true;
          Finish(cb_.quit);
        }
        break;
      case trg::update::UpdateUiAction::kNone:
        break;
    }
  }

  void* WindowHandle() const { return cb_.window_handle ? cb_.window_handle() : nullptr; }

  bool WindowFullscreen() const { return cb_.window_fullscreen && cb_.window_fullscreen(); }

  void StartInSavedState() {
    RestorePlacement();
    EnsureWindowVisible(WindowHandle());
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

  const GameState& Selected() const { return library_.State(selected_); }

  std::string RuntimeBlocker() const {
    return paths_.runtime_blocker ? paths_.runtime_blocker(*Selected().game) : std::string();
  }

  bool GameReady() const { return CanPlay(Selected(), RuntimeBlocker()); }

  void SelectGame(GameId game) {
    if (game == selected_) {
      return;
    }
    if (install_.running()) {
      launcher_->SetStatus("Wait for the install to finish.", 4.0);
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    selected_ = game;
    launcher_->config().theme = LauncherTheme(game);
    launcher_->config().branding.background = MakeHeaderPainter(game);
    settings_.SelectGame(game);
    SelectWindowSizeForAspect();
    achievements_.reset();
    achievements_tried_ = false;
    library_.RefreshFiles(game);
    library_.RescanPackages();
    REXLOG_INFO("{}", SelectedGameLogText(Selected(), RuntimeBlocker()));
    LogGameFiles(Selected());
  }

  std::function<bool()> PageShown(LauncherPage page) {
    return [this, page] { return Selected().game->HasPage(page); };
  }

  trg::HeaderPainter MakeHeaderPainter(GameId game) {
    HeaderArt& art = header_art_[size_t(game)];
    if (!art.tried) {
      art.tried = true;
      art.texture = LoadHeaderArt(paths_.immediate_drawer, game);
    }
    if (!art.texture) {
      return LauncherHeader(ImTextureID{}, kHeaderArtAspect);
    }
    return LauncherHeader(reinterpret_cast<ImTextureID>(art.texture.get()),
                          float(art.texture->width) / float(art.texture->height));
  }

  trg::TabStrip MakeTabStrip() {
    trg::TabStrip strip;
    for (const GameDefinition& game : AllGames()) {
      strip.tabs.push_back({std::string(game.tab_label), std::string(game.short_label)});
    }
    strip.selected = [this] { return int(selected_); };
    strip.select = [this](int index) { SelectGame(AllGames()[size_t(index)].id); };
    return strip;
  }

  trg::LauncherConfig MakeConfig() {
    trg::LauncherConfig c;
    c.settings = &settings_;
    // PLAY saves in RequestPlay, so a failed save keeps the launcher open.
    c.save_on_play = false;
    c.theme = LauncherTheme(selected_);
    c.note = "Play applies the changes. Save keeps them without starting.";
    c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
    c.fonts = g_fonts;
    c.branding.title = "MARVEL ULTIMATE ALLIANCE";
    c.branding.subtitle_segments = {
        {"PC PORT   \xC2\xB7   ", ""},
        {"v" + PortVersion() + "   \xC2\xB7   ", ""},
        {"BY SAULOB", kAuthorUrl},
    };
    c.branding.background = MakeHeaderPainter(selected_);
    c.header_height = kHeaderHeight;
    c.tab_strip = MakeTabStrip();
    c.pages = {
        {"Play", "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
         [this] { return !GameReady(); }, PageShown(LauncherPage::kPlay)},
        {"Display", "Window, monitor and how the picture fits your screen.",
         [this](trg::Ui& ui) { PageDisplay(ui); }, {}, PageShown(LauncherPage::kDisplay)},
        {"Graphics", "Render resolution, anti-aliasing and texture filtering.",
         [this](trg::Ui& ui) { PageGraphics(ui); }, {}, PageShown(LauncherPage::kGraphics)},
        {"Gameplay", "What the window shows while you play.", [this](trg::Ui& ui) { PageGameplay(ui); }, {},
         PageShown(LauncherPage::kGameplay)},
        {"Cheats", "Optional gameplay modifiers for this game.",
         [](trg::Ui& ui) { PageCheats(ui); }, {}, PageShown(LauncherPage::kCheats)},
        {"Achievements", "Your achievements on this PC.", [this](trg::Ui& ui) { PageAchievements(ui); }, {},
         PageShown(LauncherPage::kAchievements)},
        {"Controls", "Controller and keyboard.", [](trg::Ui& ui) { PageControls(ui); }, {},
         PageShown(LauncherPage::kControls)},
        {"About", "About this port, and where your files and settings live.",
         [this](trg::Ui& ui) { PageAbout(ui); }, {}, PageShown(LauncherPage::kAbout)},
    };
    c.can_play = [this] {
      if (install_.running()) {
        return trg::PlayCheck{false, "Wait for the install to finish.", 0};
      }
      if (GameReady()) {
        return trg::PlayCheck{};
      }
      return trg::PlayCheck{false, PlayBlockedReason(Selected(), RuntimeBlocker()), 0};
    };
    return c;
  }

  void PagePlay(trg::Ui& ui) {
    const GameState& state = Selected();
    if (install_.running()) {
      ui.StatusCard(trg::Status::kBusy, "Installing game files", nullptr, std::max(0.0f, install_.fraction()));
    } else {
      const StatusText text = Describe(state, RuntimeBlocker());
      ui.StatusCard(text.status, text.title.c_str(), text.detail.c_str());
    }
    if (!install_failure_.empty()) {
      DrawFailure(ui, install_failure_, install_failure_text_);
    }
    const std::string package = PackageText(state);
    ui.Row("Game package", package.c_str());
    if (!ui.TaskProgress(install_)) {
      const bool clicked = state.check.state != GameFileState::kMissing
                               ? ImGui::Button("Reinstall from package...", ImVec2(-FLT_MIN, 0))
                               : ui.AccentButton("Install from package...");
      if (clicked) {
        BrowseAndInstall();
      }
    }
    ui.LauncherVisibilityRow("mua_launcher");
    ui.EndRows();
  }

  void BrowseAndInstall() {
    PollInstall();
    if (install_.running()) {
      return;
    }
    const GameDefinition& game = *Selected().game;
    const std::string picked = trg::BrowseForFile(std::format("Select your {} disc image", game.display_name),
                                                  {{"Xbox 360 disc image", "*.iso;*.xiso"}});
    if (picked.empty()) {
      return;
    }
    install_failure_.clear();
    install_failure_text_.clear();
    achievements_.reset();
    achievements_tried_ = false;
    outcome_ = std::make_shared<InstallOutcome>();
    outcome_->game = &game;
    const std::filesystem::path disc = PathFromUtf8(picked);
    const bool started = install_.Start(
        "Reading disc...",
        [outcome = outcome_, disc, games_root = library_.games_root()](trg::Task& task) {
          InstallHooks hooks;
          hooks.phase = [&task](const char* text) {
            task.SetLabel(text);
            task.Progress(0, 0);
          };
          hooks.cancelled = [&task] { return task.cancelled(); };
          outcome->result = InstallFromDisc(*outcome->game, disc, games_root, DiscInstallServices(task), hooks);
          return outcome->result.ok() ? std::string() : DescribeInstall(outcome->result, *outcome->game).title;
        });
    if (started) {
      REXLOG_INFO("Installing {} from {}", game.display_name, Utf8(disc.filename()));
    }
  }

  // Runs every frame so a finished install is picked up whichever page is open.
  void PollInstall() {
    std::string error;
    if (install_.Finish(&error) == trg::Task::State::kIdle) {
      return;
    }
    const std::shared_ptr<InstallOutcome> outcome = std::move(outcome_);
    if (!outcome) {
      return;
    }
    const InstallResult& result = outcome->result;
    const InstallMessage message = DescribeInstall(result, *outcome->game);
    library_.RefreshFiles(outcome->game->id);
    if (result.ok() || result.status == InstallStatus::kCancelled) {
      if (result.ok()) {
        REXLOG_INFO("Game files installed: {} files, {} bytes", result.files, result.bytes);
        LogGameFiles(library_.State(outcome->game->id));
      } else {
        REXLOG_INFO("Game install cancelled");
      }
      launcher_->SetStatus(message.title + ". " + message.text, 6.0);
      return;
    }
    REXLOG_WARN("Game install failed: {} ({})", message.title, result.detail);
    install_failure_ = message.title;
    install_failure_text_ = message.text;
    launcher_->SetStatus("Install failed: " + install_failure_, 8.0);
  }

  void PageDisplay(trg::Ui& ui) {
    ui.Toggle("Display mode",
              "How the game starts. Borderless Fullscreen fills the monitor. F11 switches while playing.", "fullscreen",
              false, "Windowed", "Borderless Fullscreen");
    ui.Row("Window size", "The game window's size in Windowed mode.");
    WindowSizeCombo();
    ui.Row("Monitor", "The display the game opens on.");
    MonitorCombo();
    if (ui.Combo("Aspect Ratio", "Sets the game's target rendering format and the Window size choices.",
                 "mua_aspect_ratio", "16:9", AspectRatioOptions())) {
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

  void PageGameplay(trg::Ui& ui) {
    ui.Toggle("Skip intro", "Start directly at the title screen", "mua_skip_intro", false, "Off", "On");
    std::vector<trg::Option> frame_rates;
    const FramePolicy& policy = Selected().game->frame_policy;
    for (auto& [value, label] : FrameRateChoices(policy)) {
      frame_rates.push_back({std::move(value), std::move(label)});
    }
    const std::string frame_rate_help = FrameRateHelp(policy);
    ui.Choice("Frame rate", frame_rate_help.c_str(), "mua_frame_rate", "60", frame_rates, 4);
    const std::string fps_help =
        "Shows the game's frame rate in the window title: " + GameWindowTitle(*Selected().game) + " | FPS: 60.00";
    ui.Toggle("Frame rate in title", fps_help.c_str(), "mua_show_fps", true, "Off", "On");
    ui.Toggle("Render resolution in title", "Shows the game's render resolution in the window title",
              "mua_show_render_resolution", false, "Off", "On");
    ui.Toggle("Frame counter", "Shows the game's frame rate in the corner. F2 toggles it while playing.",
              "mua_show_frame_counter", false, "Hidden", "Shown");
    if (ui.Section("Player names", true)) {
      ui.Help(kPlayerNamesHelp);
      for (const PlayerNameSetting& player : kPlayerNames) {
        PlayerNameRow(ui, player);
      }
    }
  }

  static void PlayerNameRow(trg::Ui& ui, const PlayerNameSetting& player) {
    static std::map<std::string, PlayerNameResult> blocked;
    struct Edit {
      std::string previous;
      PlayerNameResult blocked = PlayerNameResult::Ok;
    } edit{ui.settings().Get(player.key)};
    const auto reject_invalid = [](ImGuiInputTextCallbackData* data) {
      auto* state = static_cast<Edit*>(data->UserData);
      const std::string kept = rex::system::xam::AcceptUserDisplayNameEdit(
          state->previous, {data->Buf, size_t(data->BufTextLen)}, &state->blocked);
      if (state->blocked != PlayerNameResult::Ok) {
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, kept.c_str());
      }
      return 0;
    };
    ui.Row(player.label, kPlayerNameHint);
    char text[64];
    std::snprintf(text, sizeof text, "%s", edit.previous.c_str());
    ImGui::PushID(player.key);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##text", player.default_name, text, sizeof text, ImGuiInputTextFlags_CallbackEdit,
                                 reject_invalid, &edit)) {
      ui.settings().Set(player.key, text);
      blocked[player.key] = edit.blocked;
    }
    ImGui::PopID();
    PlayerNameResult shown = blocked[player.key];
    if (shown == PlayerNameResult::Ok) {
      shown = CheckPlayerName(ui.settings().Get(player.key));
    }
    if (shown != PlayerNameResult::Ok) {
      ui.Help(PlayerNameMessage(shown));
    }
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

  static bool WindowSizeFits(WindowSize size, const MonitorInfo* monitor) {
    return !monitor || (size.width * monitor->scale < monitor->width && size.height * monitor->scale < monitor->height);
  }

  void SelectWindowSizeForAspect() {
    const WindowSize current{settings_.GetInt("window_width", 0), settings_.GetInt("window_height", 0)};
    const MonitorInfo* monitor = SelectedMonitor();
    const WindowSize chosen = WindowSizeForAspect(SelectedAspectRatio(), current, [monitor](WindowSize size) {
      return WindowSizeFits(size, monitor);
    });
    if (chosen.width != current.width || chosen.height != current.height) {
      settings_.SetInt("window_width", chosen.width);
      settings_.SetInt("window_height", chosen.height);
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
      for (const AspectWindowSize& size : kWindowSizes) {
        const bool current = size.width == width && size.height == height;
        if (size.aspect != aspect || (!current && !WindowSizeFits({size.width, size.height}, monitor))) {
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
    if (const std::optional<int> adapter =
            ResolveGraphicsAdapter(settings_.Get("mua_graphics_adapter"), graphics_adapters_)) {
      const std::string value = std::to_string(*adapter);
      settings_.Set("mua_graphics_adapter", value);
      std::vector<trg::Option> adapters;
      for (const auto& [option_value, label] : GraphicsAdapterOptions(graphics_adapters_)) {
        adapters.push_back({option_value, label});
      }
      ui.Combo("Graphics card", "Selects which GPU renders the game and restarts the game window.",
               "mua_graphics_adapter", value.c_str(), adapters);
    }
    ui.Combo("Render resolution",
             "Draws the game at a multiple of its own picture, then scales it to the window. Higher is sharper and "
             "costs more GPU time. Window size is separate.",
             "resolution_scale", "1", RenderResolutionOptions(SelectedAspectRatio()));
    if (UpscalingApplies()) {
      UpscalerMask offered = Selected().game->upscalers;
      std::string fsr1_unavailable;
      if ((offered & UpscalerBit(Upscaler::kFsr1)) && !(RuntimeUpscalers() & UpscalerBit(Upscaler::kFsr1))) {
        offered &= ~UpscalerBit(Upscaler::kFsr1);
        fsr1_unavailable = "Unavailable: this build's graphics runtime has no AMD FSR 1.";
        if (settings_.Get("mua_upscaler") == kUpscalerFsr1) {
          settings_.Set("mua_upscaler", kUpscalerOff);
        }
      }
      std::string fsr3_unavailable;
      if ((offered & UpscalerBit(Upscaler::kFsr3)) && !Fsr3().available) {
        offered &= ~UpscalerBit(Upscaler::kFsr3);
        fsr3_unavailable = "Unavailable: " + Fsr3().reason + ".";
        if (settings_.Get("mua_upscaler") == kUpscalerFsr3) {
          settings_.Set("mua_upscaler", kUpscalerOff);
        }
      }
      std::string dlss_unavailable;
      if ((offered & UpscalerBit(Upscaler::kDlss)) && !Dlss().available) {
        offered &= ~UpscalerBit(Upscaler::kDlss);
        dlss_unavailable = "Unavailable: " + Dlss().reason + ".";
        if (settings_.Get("mua_upscaler") == kUpscalerDlss) {
          settings_.Set("mua_upscaler", kUpscalerOff);
        }
      }
      ui.Choice("Upscaling", UpscalingHelp(Selected().game->upscalers & kBackendUpscalers), "mua_upscaler",
                kUpscalerOff, UpscalerOptions(offered));
      if (!fsr1_unavailable.empty()) {
        ui.Info("AMD FSR 1", fsr1_unavailable.c_str());
      }
      if (!fsr3_unavailable.empty()) {
        ui.Info("AMD FSR 3.1", fsr3_unavailable.c_str());
      }
      if (!dlss_unavailable.empty()) {
        ui.Info("NVIDIA DLSS", dlss_unavailable.c_str());
      }
      const Upscaler upscaler = ParseUpscaler(settings_.Get("mua_upscaler", kUpscalerOff), offered);
      const UpscalerInfo& info = UpscalerInfoFor(upscaler);
      if ((info.scene_only || info.lowers_resolution) &&
          !OffersQuality(upscaler, ParseUpscaleQuality(settings_.Get("mua_upscale_quality", "native")))) {
        settings_.Set("mua_upscale_quality", QualityInfoFor(UpscaleQuality::kPerformance).key);
      }
      if (upscaler == Upscaler::kFsr3) {
        ui.Combo("Upscaling quality",
                 "How far below the Render resolution AMD FSR 3.1 draws the 3D scene before rebuilding it at the Render "
                 "resolution from motion and depth. Native keeps the full size and uses FSR 3.1 only to smooth edges. "
                 "Works on NVIDIA, AMD and Intel graphics; the HUD and menus always draw at the Render resolution.",
                 "mua_upscale_quality", "native",
                 Fsr3QualityOptions(SelectedAspectRatio(), settings_.GetInt("resolution_scale", 1),
                                    Selected().game->scene_size));
        ui.SliderInt("Sharpness", "AMD FSR 3.1's own sharpening of the rebuilt 3D scene. 0 turns it off.",
                     "mua_fsr3_sharpness", kDefaultFsr3Sharpness, 0, 100);
      } else if (info.scene_only) {
        ui.Combo("Upscaling quality",
                 "How far below the Render resolution NVIDIA DLSS draws the 3D scene before rebuilding it at the Render "
                 "resolution. DLAA keeps the full size and uses DLSS only to smooth edges. The HUD and menus always "
                 "draw at the Render resolution.",
                 "mua_upscale_quality", "native",
                 DlssQualityOptions(SelectedAspectRatio(), settings_.GetInt("resolution_scale", 1),
                                    Selected().game->scene_size));
      } else if (info.lowers_resolution) {
        const std::string help = std::string("How far below the Render resolution the game draws before ") + info.name +
                                 " scales it to the window. Lower is faster and softer, including the menus and HUD.";
        ui.Combo("Upscaling quality", help.c_str(), "mua_upscale_quality", "native",
                 UpscaleQualityOptions(SelectedAspectRatio(), settings_.GetInt("resolution_scale", 1)));
      }
    }
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
    if (HdTextApplies()) {
      ui.Toggle("HD Text",
                "Rebuilds in-game text and controller icons at higher resolution. Uses additional memory at higher "
                "render resolutions.",
                hd_text::kSettingKey, true);
    }
  }

  bool UpscalingApplies() const { return Selected().game != nullptr; }

  // NVIDIA's check runs once per graphics card choice, and only for a game that offers DLSS.
  const DlssSupport& Dlss() {
    const std::string adapter = settings_.Get("mua_graphics_adapter");
    if (!dlss_support_ || adapter != dlss_adapter_) {
      dlss_adapter_ = adapter;
      dlss_support_ = CheckDlssSupport(ResolveGraphicsAdapter(adapter, graphics_adapters_).value_or(-1),
                                       GameCacheRoot(*Selected().game, paths_.user_dir / "cache") / "ngx");
      if (dlss_support_->available && MultisamplingApplies()) {
        *dlss_support_ = {false, "it needs the rasterizer-ordered views render path, which this graphics card doesn't "
                                 "run"};
      }
      REXLOG_INFO("Launcher: NVIDIA DLSS {}",
                  dlss_support_->available ? std::string("available") : "unavailable: " + dlss_support_->reason);
    }
    return *dlss_support_;
  }

  // AMD's DLL beside the program and the rasterizer-ordered views path; checked again when the graphics card changes.
  const Fsr3Support& Fsr3() {
    const std::string adapter = settings_.Get("mua_graphics_adapter");
    if (!fsr3_support_ || adapter != fsr3_adapter_) {
      fsr3_adapter_ = adapter;
      std::error_code error;
      fsr3_support_ = Fsr3SupportFor(
          kGraphicsBackend == GraphicsBackend::kDirect3D12,
          std::filesystem::is_regular_file(rex::filesystem::GetExecutableFolder() / kFidelityFxLibraryFile, error),
          MultisamplingApplies());
      REXLOG_INFO("Launcher: AMD FSR 3.1 {}",
                  fsr3_support_->available ? std::string("available") : "unavailable: " + fsr3_support_->reason);
    }
    return *fsr3_support_;
  }

  bool HdTextApplies() const { return Selected().game && hd_text::AssetsFor(Selected().game->id); }

  // The choice only matters when the GPU cannot run the rasterizer-ordered views EDRAM path.
  bool MultisamplingApplies() {
    const std::string adapter = settings_.Get("mua_graphics_adapter");
    if (!host_render_targets_checked_ || adapter != host_render_targets_adapter_) {
      host_render_targets_checked_ = true;
      host_render_targets_adapter_ = adapter;
      host_render_targets_ = HostRenderTargetsUsed(
          rex::cvar::GetFlagByName(kBackendSettings.render_target_path),
          PixelShaderEdramSupported(ResolveGraphicsAdapter(adapter, graphics_adapters_).value_or(-1)));
    }
    return host_render_targets_;
  }

  static void PageCheats(trg::Ui& ui) {
    ui.Help("Press F10 while playing to open the Cheats menu. Changes made there apply only to the current session.");
    ui.Spacer(12);
    ui.Toggle("Infinite Health", "All four party heroes stay at their own maximum health", "mua_cheat_health", false,
              "Off", "On");
    ui.Toggle("Infinite Energy", "All four party heroes stay at their own maximum energy", "mua_cheat_energy", false,
              "Off", "On");
    ui.Choice("Coin Multiplier", "Multiply the coins the game awards", "mua_cheat_coin_multiplier", "0",
              {{"0", "Off"}, {"2", "2x"}, {"5", "5x"}, {"10", "10x"}}, 4);
  }

  void PageAchievements(trg::Ui& ui) {
    if (Selected().check.state != GameFileState::kValid) {
      ui.Paragraph("Install the game files to see its achievements.");
      return;
    }
    if (!achievements_ && !achievements_tried_) {
      achievements_tried_ = true;
      achievements_ =
          std::make_unique<LauncherAchievements>(Selected().data_dir, GameUserDataRoot(*Selected().game, paths_.user_dir),
                                                 paths_.immediate_drawer);
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
    ImGui::TextColored(ui.theme().accent, "%dG / %dG", summary.earned_gamerscore, summary.total_gamerscore);
    ImGui::PopFont();
    const float fraction = summary.total > 0 ? float(summary.unlocked) / float(summary.total) : 0.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ui.theme().accent);
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
    const GameState& state = Selected();
    std::error_code ec;
    if (std::filesystem::is_directory(state.data_dir, ec)) {
      ui.FolderRow("Game files", "Where the game data is read from.", "Open game folder", Utf8(state.data_dir));
    } else {
      ui.FolderRow("Games folder", "Where the game folders and disc images go.", "Open games folder",
                   Utf8(library_.games_root()));
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
    ui.Info("Port version", PortVersion().c_str());
    ui.Info("Libraries", "ReXGlue SDK " REXGLUE_VERSION_STRING "   \xC2\xB7   TRG Launcher " MUA_TRG_VERSION);
    const std::string game =
        std::format("Xbox 360 disc, title {}, {}", HexId(ProfileOf(*state.game).title_id), state.game->display_name);
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
  GameLibrary library_;
  GameSettings& settings_;
  GameId selected_;
  trg::Task install_;
  std::shared_ptr<InstallOutcome> outcome_;
  std::string install_failure_;
  std::string install_failure_text_;
  std::vector<MonitorInfo> monitors_;
  std::vector<GraphicsAdapter> graphics_adapters_;
  std::string host_render_targets_adapter_;
  std::string dlss_adapter_;
  std::optional<DlssSupport> dlss_support_;
  std::string fsr3_adapter_;
  std::optional<Fsr3Support> fsr3_support_;
  bool host_render_targets_checked_ = false;
  bool host_render_targets_ = true;
  struct HeaderArt {
    std::unique_ptr<rex::ui::ImmediateTexture> texture;
    bool tried = false;
  };
  std::array<HeaderArt, 3> header_art_;
  std::unique_ptr<trg::Launcher> launcher_;
  std::unique_ptr<trg::update::UpdateController> updater_;
  bool skip_finish_save_ = false;
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

ImFont* LauncherRegularFont() {
  return g_fonts.regular;
}

ImFont* LauncherSemiboldFont() {
  return g_fonts.semibold;
}

bool ShouldShowLauncher(bool game_ready) {
  const bool skip_once = REXCVAR_GET(mua_skip_launcher);
  rex::cvar::ResetToDefault("mua_skip_launcher");
  return LauncherWanted(game_ready, skip_once, ShiftHeld(), REXCVAR_GET(mua_launcher));
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

void ShowLauncher(rex::ui::ImGuiDrawer* drawer, LauncherPaths paths, GameLibrary library, GameSettings& settings,
                  LauncherCallbacks callbacks) {
  new LauncherDialog(drawer, std::move(paths), std::move(library), settings, std::move(callbacks));
}

}  // namespace mua
