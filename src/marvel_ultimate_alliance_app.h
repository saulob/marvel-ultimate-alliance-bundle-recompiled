#pragma once

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/platform/env.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/flags.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/version.h>

#include "trg/game_helpers.h"

#include "achievements_overlay.h"
#include "active_log.h"
#include "aspect_ratio.h"
#include "diagnostics.h"
#include "disc_identity.h"
#include "frame_counter.h"
#include "frame_limiter.h"
#include "game_data.h"
#include "game_files.h"
#include "game_library.h"
#include "graphics_adapters.h"
#include "guest_functions.h"
#include "hang_watchdog.h"
#include "hd_text.h"
#include "hd_text_vfs.h"
#include "input_summary.h"
#include "launcher.h"
#include "log_filter.h"
#include "log_path.h"
#include "modal_dialog.h"
#include "platform.h"
#include "recomp_image.h"
#include "session_log.h"
#include "settings.h"
#include "shader_storage_repair.h"
#include "system_info.h"
#include "user_data.h"
#include "window_host.h"

class MarvelUltimateAllianceApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    mua::SetApplicationId();
    const mua::RecompiledImage& image = mua::GetRecompiledImage();
    if (image.available) {
      mua::InstallGuestFunctionTable(image.info.func_mappings);
    }
    return std::unique_ptr<MarvelUltimateAllianceApp>(
        new MarvelUltimateAllianceApp(ctx, "marvel_ultimate_alliance", image.info));
  }

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override {
    shipped_config_ = paths.config_path;
    const std::filesystem::path port_folder = mua::PortFolder();
    explicit_game_root_ = paths.game_data_root;
    if (paths.game_data_root.empty()) {
      paths.game_data_root = port_folder / "game";
    }
    ChooseUserData(paths, port_folder);
    if (const auto inherited =
            mua::TakeInheritedSessionLog({port_folder / "logs", paths.user_data_root / "logs"})) {
      rex::cvar::SetFlagFromCommandLine("log_file", mua::Utf8(*inherited));
      mua::InstallCrashReports();
    }
    // ReXGlue writes its logs beside the executable, which is read-only inside an AppImage.
    if (InsideAppImage() && rex::cvar::GetFlagByName("log_file").empty()) {
      const std::filesystem::path logs = mua::LogsFolder(port_folder, paths.user_data_root, {}, true);
      if (const std::filesystem::path file = mua::NextLogFile(logs, "marvel_ultimate_alliance"); !file.empty()) {
        rex::cvar::SetFlagByName("log_file", mua::Utf8(file));
      }
    }
  }

  bool SetupPresentation() override {
    if (rex::ReXApp::SetupPresentation()) {
      return true;
    }
    REXLOG_WARN("Graphics unavailable; opening the launcher without graphics");
    mua::RunLauncherWithoutGraphics(DescribeSelectedGame());
    return false;
  }

  void OnPostInitLogging() override {
    mua::InitializeProcessUptime();
    mua::InstallCrashReports();
    const mua::UserDataMessage message = mua::DescribeUserData(user_data_);
    if (message.warning) {
      REXLOG_WARN("{}", message.text);
    } else {
      REXLOG_INFO("{}", message.text);
    }
    mua::LoadPlayerSettings(mua::PlayerSettingsPath(user_data_root()));
    mua::ApplyDetailedLogging();
    mua::InstallSessionLogCleanup();
    mua::RemoveDiscardedSessionLogs();
    mua::RemoveOldSessionLogs();
    const mua::ApplicationIdentity identity{
        .product = "Marvel Ultimate Alliance",
        .version = MUA_PORT_VERSION,
        .build_config = mua::BuildConfigurationName(),
        .architecture = mua::ArchitectureName(),
        .sdk = REXGLUE_VERSION_STRING,
        .backend = mua::GraphicsBackendName(),
    };
    REXLOG_INFO("{}", mua::FormatApplicationVersion(identity));
    REXLOG_INFO("{}", mua::FormatBuildIdentity(identity));
    const mua::RecompiledImage& image = mua::GetRecompiledImage();
    REXLOG_INFO("Program: {}", ImageKey(image));
    if (image.game && !image.available) {
      REXLOG_WARN("Recompiled game code is not part of this build");
    }
    mua::ApplyGraphicsAdapter();
  }

  void OnPostLoadXexImage() override {
#if MUA_HAS_RECOMPILED_CODE
    mua::LogGuestHooks(mua::GetRecompiledImage().info.func_mappings);
#endif
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    mua::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnDpiScaleChanged(float scale) override {
    mua::UpdateLauncherFonts(imgui_drawer(), immediate_drawer(), scale);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    for (const std::string& line : mua::MigrateLegacyGameData(user_data_.root)) {
      REXLOG_INFO("{}", line);
    }
    game_settings_.Capture(mua::PlayerSettingsPath(user_data_root()), shipped_config_);
    games_root_ = REXCVAR_GET(mua_games_root).empty() ? mua::PortFolder() / "game"
                                                      : mua::PathFromUtf8(REXCVAR_GET(mua_games_root));
    library_.emplace(games_root_, explicit_game_root_, mua::ReadDiscIdentity);
    if (const auto own_game = mua::GetRecompiledImage().game) {
      game_settings_.SelectGame(*own_game);
    } else if (const auto explicit_game = library_->explicit_game()) {
      game_settings_.SelectGame(*explicit_game);
    } else if (!explicit_game_root_.empty()) {
      REXLOG_WARN("--game_data_root {} does not hold a supported game; using the games folder",
                  mua::Utf8(explicit_game_root_));
    } else if (!mua::FindGame(REXCVAR_GET(mua_game))) {
      REXLOG_WARN("mua_game \"{}\" is not a game; selecting {}", REXCVAR_GET(mua_game), mua::DefaultGame().key);
    }
    const mua::GameId selected = game_settings_.SelectedGame();
    REXLOG_INFO("{}", mua::SelectedGameLogText(library_->State(selected), RuntimeBlocker(mua::Game(selected))));
    if (IsLauncher()) {
      show_launcher_ = mua::ShouldShowLauncher(
          mua::BootableGame(*library_, selected, RuntimeBlocker(mua::Game(selected))) != nullptr);
      if (!show_launcher_) {
        StartRuntimeAndExit(library_->State(selected));
      }
      mua::PrepareLauncherWindow();
    } else {
      mua::ApplyUpscaler(game_settings_);
    }
    game_settings_.MarkWindowCreated();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)>) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    mua::ApplyWindowIcon();
    game_settings_.CaptureLateRegistered();
    const mua::GameState selected = library_->State(game_settings_.SelectedGame());
    if (!IsLauncher()) {
      if (mua::TakeLauncherHandoff() && window()) {
        mua::CompleteLauncherHandoff(window()->GetNativeWindowHandle());
      }
      ActivateGame(selected);
      game_settings_.ApplyGameSettings();
      mua::ApplyPlayerNames();
      mua::ApplyShaderWait();
      mua::ApplyMultisampling();
      mua::ApplyDynamicPageStreaming(mua::GetRecompiledImage().game);
      mua::LogUpscaling();
      mua::PinGuestVideoMode();
      mua::ApplyAspectRatio();
      const rex::PathConfig routed = RoutedPaths(defaults, selected);
      for (const mua::ShaderStorageRepair& repair : mua::RepairShaderStorage(routed.cache_root)) {
        REXLOG_WARN("Shader cache {}: dropped {} damaged record(s), kept {}", repair.file, repair.dropped, repair.kept);
      }
      LogPipelineStorage(routed.cache_root, mua::ProfileOf(*selected.game).title_id);
      return routed;
    }
    if (!imgui_drawer()) {
      StartRuntimeAndExit(selected);
    }
    REXLOG_INFO("Launcher shown");
    mua::LauncherCallbacks callbacks;
    callbacks.other_executable = [](const mua::GameDefinition&) { return true; };
    callbacks.start_other_executable = [this](const mua::GameState& game) {
      if (!StartRuntime(game)) {
        return false;
      }
      if (window()) {
        mua::HideNativeWindow(window()->GetNativeWindowHandle());
      }
      app_context().CallInUIThreadDeferred([] { ExitLauncher(); });
      return true;
    };
    callbacks.quit = [this] { app_context().CallInUIThreadDeferred([this] { app_context().QuitFromUIThread(); }); };
    callbacks.window_handle = [this] { return window() ? window()->GetNativeWindowHandle() : nullptr; };
    callbacks.window_fullscreen = [this] { return window() && window()->IsFullscreen(); };
    callbacks.set_window_fullscreen = [this](bool fullscreen) {
      if (window()) {
        window()->SetFullscreen(fullscreen);
      }
    };
    const std::filesystem::path port_folder = mua::PortFolder();
    const std::filesystem::path logs_dir =
        mua::LogsFolder(port_folder, defaults.user_data_root, mua::ActiveLogFile(), InsideAppImage());
    mua::LauncherPaths paths{defaults.user_data_root,
                             mua::PlayerSettingsPath(defaults.user_data_root),
                             logs_dir,
                             user_data_.outcome,
                             immediate_drawer(),
                             [](const mua::GameDefinition& game) { return RuntimeBlocker(game); }};
    mua::ShowLauncher(imgui_drawer(), std::move(paths), *library_, game_settings_, std::move(callbacks));
    return std::nullopt;
  }

  void LaunchModule() override {
    const int monitor = game_settings_.GetInt("monitor", std::atoi(rex::cvar::GetFlagByName("monitor").c_str()));
    mua::StartupWindowInfo startup_window;
    startup_window.fullscreen = window() != nullptr && window()->IsFullscreen();
    startup_window.selected_width = game_settings_.GetInt(
        "window_width", std::atoi(rex::cvar::GetFlagByName("window_width").c_str()));
    startup_window.selected_height = game_settings_.GetInt(
        "window_height", std::atoi(rex::cvar::GetFlagByName("window_height").c_str()));
    startup_window.configured_width = window() != nullptr ? int(window()->GetDesiredLogicalWidth())
                                                          : startup_window.selected_width;
    startup_window.configured_height = window() != nullptr ? int(window()->GetDesiredLogicalHeight())
                                                           : startup_window.selected_height;
    const mua::WindowSize drawable = CurrentDrawableSize(window());
    startup_window.output_width = drawable.width;
    startup_window.output_height = drawable.height;
    startup_window.controller = mua::ControllerSummary();
    mua::LogStartupDiagnostics(game_settings_, mua::MonitorForSetting(monitor), monitor, game_data_root(),
                               startup_window);
    LogAchievementStore(mua::GetRecompiledImage());
    ReXApp::LaunchModule();
  }

  void OnPostSetup() override {
#if defined(_WIN32)
    REXLOG_INFO("Scheduling: {}", trg::TuneProcessScheduling());
#endif
    // The game volume is mounted and the title has not opened its archive yet.
    if (const auto game = mua::GetRecompiledImage().game) {
      mua::hd_text::InstallHdText(runtime()->file_system(), *game,
                                  std::atoi(rex::cvar::GetFlagByName("resolution_scale").c_str()),
                                  mua::hd_text::ParseSetting(rex::cvar::GetFlagByName(mua::hd_text::kSettingKey)));
    }
    if (window()) {
      mua::CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
      mua::StartHangWatchdog(window()->GetNativeWindowHandle());
      AttachWindowHost(window(), app_context(), [this] {
        rex::ui::ImGuiDrawer* drawer = imgui_drawer();
        return drawer && drawer->GetIO().WantCaptureMouse;
      });
      if (imgui_drawer()) {
        mua::CreateFrameCounter(imgui_drawer());
        mua::QueueStartupSkipIntroNotice(REXCVAR_GET(mua_skip_intro));
        mua::CreateSkipIntroNotice(imgui_drawer());
        CreateCursorSync(imgui_drawer());
        if (auto* input = static_cast<rex::input::InputSystem*>(runtime()->input_system())) {
          // Replaces ReXApp's own callback: the modal overlay keeps the game's input until its close press is released.
          input->SetActiveCallback([this] { return GameInputActive(); });
        }
        mua::ModalDialog::AttachEscapeKey(window());
      }
    }
  }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
    mua::ModalDialog::DetachEscapeKey();
    delete achievements_;
    DetachWindowHost();
    REXLOG_INFO("{}", mua::FormatCleanExit(mua::ProcessUptimeText()));
  }

  // Window close is a hard exit: the base TerminateTitle would call StopTitleThreads and wait on guest
  // threads that are still running guest code. The OS reclaims the process.
  void OnClosing(rex::ui::UIEvent&) override {
    REXLOG_INFO("Window closing, hard-exiting process.");
    mua::DiscardSessionLogBeforeHardExit();
    rex::FlushLogging();
    std::_Exit(0);
  }

  // F7 toggles the project's own overlay; ReXApp's stock one is never created.
  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    if (achievements_) {
      achievements_->RequestClose();
    } else {
      OpenAchievements();
    }
    return nullptr;
  }

 private:
  bool GameInputActive() {
    if (mua::ModalDialog::AnyOpen()) {
      return false;
    }
    // As ReXGlue does for its own overlays: a pointer over an overlay window keeps the click from the game.
    rex::ui::ImGuiDrawer* drawer = imgui_drawer();
    return !drawer || !drawer->HasDialogs() || !drawer->GetIO().WantCaptureMouse;
  }

  void OpenAchievements() {
    if (achievements_ || mua::ModalDialog::AnyOpen() || !runtime() || !runtime()->kernel_state() || !imgui_drawer() ||
        !immediate_drawer()) {
      return;
    }
    achievements_ = new mua::AchievementsOverlay(imgui_drawer(), immediate_drawer(), runtime(), &achievements(),
                                                 [this] { achievements_ = nullptr; });
  }

  static bool IsLauncher() { return !mua::GetRecompiledImage().game; }

  static bool InsideAppImage() { return mua::PortFolder() != rex::filesystem::GetExecutableFolder(); }

  // What the launcher without graphics shows: the game this start would run.
  mua::GraphicsFallbackInfo DescribeSelectedGame() const {
    mua::GraphicsFallbackInfo info;
    if (!library_) {
      return info;
    }
    const mua::GameState& state = library_->State(game_settings_.SelectedGame());
    info.game_name = std::string(state.game->display_name);
    info.game_dir = mua::Utf8(state.data_dir);
    info.game_files_ready = mua::CanPlay(state, RuntimeBlocker(*state.game));
    info.game_files_status = info.game_files_ready ? "Ready" : "Not ready to play";
    return info;
  }

  static std::string_view ImageKey(const mua::RecompiledImage& image) {
    return image.game ? mua::Game(*image.game).key : std::string_view("launcher");
  }

  // Why this program cannot run the game: it carries the recompiled module of one game only.
  static std::string LocalRuntimeBlocker(const mua::GameDefinition& game) {
    const mua::RecompiledImage& image = mua::GetRecompiledImage();
    if (game.id != image.game) {
      return std::format("{} runs in its own program.", game.display_name);
    }
    return image.available ? std::string() : "Recompiled game code has not been generated for this build yet.";
  }

  // Why the launcher cannot start the game: the game's own program is not beside it.
  static std::string RuntimeBlocker(const mua::GameDefinition& game) {
    if (!IsLauncher()) {
      return LocalRuntimeBlocker(game);
    }
    if (game.executable.empty()) {
      return std::format("{} has no runtime yet.", game.display_name);
    }
    std::error_code ec;
    if (std::filesystem::exists(mua::PortFolder() / std::string(game.executable), ec)) {
      return {};
    }
    return std::format("The {} runtime is not part of this build.", game.display_name);
  }

  void LogAchievementStore(const mua::RecompiledImage& image) {
    if (!image.game || !runtime() || !runtime()->kernel_state()) {
      return;
    }
    auto& achievements = runtime()->kernel_state()->achievements();
    const auto listed = achievements.ListAchievements();
    const auto unlocked = std::ranges::count_if(listed, [&](const auto& info) { return achievements.IsUnlocked(info.id); });
    REXLOG_INFO("Achievements: listed={} unlocked={} store={}", listed.size(), unlocked,
                mua::Utf8(mua::AchievementStoreFile(mua::Game(*image.game), user_data_.root)));
    static std::atomic<int> logged{0};
    achievements.RegisterUnlockCallback([](const rex::system::AchievementEvent& event) {
      if (logged.fetch_add(1, std::memory_order_relaxed) < 16) {
        REXLOG_INFO("Achievement unlocked: id={} label={}", event.achievement.id, event.achievement.label);
      }
    });
  }

  // Missing or empty storage means the pipelines are built during play, a common cause of first-run stutter.
  static void LogPipelineStorage(const std::filesystem::path& cache_root, uint32_t title_id) {
    std::string text;
    for (const char* suffix : {"xsh", "rov.d3d12.xpso", "rtv.d3d12.xpso"}) {
      const std::filesystem::path file = cache_root / "shaders" / "shareable" / std::format("{:08X}.{}", title_id, suffix);
      std::error_code ec;
      const auto size = std::filesystem::file_size(file, ec);
      text += std::format("{}{} {}", text.empty() ? "" : " | ", mua::Utf8(file.filename()),
                          ec ? std::string("missing") : std::format("{} bytes", size));
    }
    REXLOG_INFO("Pipeline storage: {}", text);
  }

  static bool StartRuntime(const mua::GameState& game) {
    const std::filesystem::path executable = mua::PortFolder() / std::string(game.game->executable);
    REXLOG_INFO("Starting {}", mua::Utf8(executable));
    return mua::RelaunchExecutable(executable, L"--mua_skip_launcher=true");
  }

  [[noreturn]] static void ExitLauncher() {
    REXLOG_INFO("Started the game's own program; hard-exiting this process.");
    rex::FlushLogging();
    std::_Exit(0);
  }

  [[noreturn]] static void StartRuntimeAndExit(const mua::GameState& game) {
    if (!StartRuntime(game)) {
      REXLOG_ERROR("Could not start {}", game.game->display_name);
      rex::FlushLogging();
      std::_Exit(1);
    }
    ExitLauncher();
  }

  // A game that cannot run here gets no game folder, so the runtime never starts another game's code on its files.
  static rex::PathConfig RoutedPaths(const rex::PathConfig& defaults, const mua::GameState& game) {
    rex::PathConfig paths = defaults;
    if (mua::CanPlay(game, LocalRuntimeBlocker(*game.game))) {
      paths.game_data_root = game.data_dir;
      paths.user_data_root = mua::GameUserDataRoot(*game.game, defaults.user_data_root);
      paths.cache_root = mua::GameCacheRoot(*game.game, defaults.cache_root);
      REXLOG_INFO("Game paths: game={} data={} saves={} cache={}", game.game->key, mua::Utf8(paths.game_data_root),
                  mua::Utf8(paths.user_data_root), mua::Utf8(paths.cache_root));
    } else {
      paths.game_data_root.clear();
      REXLOG_ERROR("{} cannot be started: refusing to boot it", game.game->display_name);
    }
    return paths;
  }

  // The window and the title follow the profile the files were verified against.
  void ActivateGame(const mua::GameState& game) {
    const bool runnable = mua::CanPlay(game, LocalRuntimeBlocker(*game.game)) && game.check.profile;
    const mua::GameDefinition* verified = runnable ? mua::GameForProfile(*game.check.profile) : nullptr;
    if (!verified) {
      return;
    }
    mua::SetActiveGame(verified->id);
    if (window()) {
      window()->SetTitle(mua::GameWindowTitle(*verified));
    }
  }

  // The cache folder ReXGlue derived from the system root follows the user data root unless it was set explicitly.
  void ChooseUserData(rex::PathConfig& paths, const std::filesystem::path& port_folder) {
    user_data_ = mua::SelectUserData({port_folder, paths.user_data_root, !REXCVAR_GET(user_data_root).empty()});
    if (user_data_.root == paths.user_data_root) {
      return;
    }
    paths.user_data_root = user_data_.root;
    if (REXCVAR_GET(cache_root).empty()) {
      paths.cache_root = user_data_.root / "cache";
    }
  }

  mua::GameSettings game_settings_;
  std::optional<mua::GameLibrary> library_;
  std::filesystem::path games_root_;
  std::filesystem::path explicit_game_root_;
  std::filesystem::path shipped_config_;
  mua::UserDataSelection user_data_;
  mua::AchievementsOverlay* achievements_ = nullptr;
  bool show_launcher_ = false;
};
