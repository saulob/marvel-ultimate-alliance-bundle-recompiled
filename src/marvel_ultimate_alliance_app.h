#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

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

#include "active_log.h"
#include "aspect_ratio.h"
#include "diagnostics.h"
#include "frame_counter.h"
#include "frame_limiter.h"
#include "game_files.h"
#include "graphics_adapters.h"
#include "guest_functions.h"
#include "hang_watchdog.h"
#include "input_summary.h"
#include "launcher.h"
#include "log_filter.h"
#include "log_path.h"
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
    if (paths.game_data_root.empty()) {
      paths.game_data_root = port_folder / "game";
    }
    ChooseUserData(paths, port_folder);
    if (const auto inherited =
            mua::TakeInheritedSessionLog({port_folder / "logs", paths.user_data_root / "logs"})) {
      rex::cvar::SetFlagFromCommandLine("log_file", mua::Utf8(*inherited));
      mua::InstallCrashReports();
    }
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
    if (!mua::GetRecompiledImage().available) {
      REXLOG_WARN("Recompiled game code is not part of this build");
    }
    mua::ApplyGraphicsAdapter();
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    mua::LoadLauncherFonts(atlas, window() ? float(window()->GetDpi()) / float(window()->GetMediumDpi()) : 1.0f);
  }

  void OnDpiScaleChanged(float scale) override {
    mua::UpdateLauncherFonts(imgui_drawer(), immediate_drawer(), scale);
  }

  void OnPreSetup(rex::RuntimeConfig&) override {
    // DEBUG_SAULO: the GPU plugin's frame log (patch 0049) for five frames, 3 s after the first swap.
    if (!rex::platform::env::get("REX_DEV_FRAME_LOG")) {
      rex::platform::env::set("REX_DEV_FRAME_LOG", "3,5");
    }
    game_settings_.Capture(mua::PlayerSettingsPath(user_data_root()), shipped_config_);
    show_launcher_ = mua::ShouldShowLauncher(game_data_root());
    if (show_launcher_) {
      mua::PrepareLauncherWindow();
    }
    game_settings_.MarkWindowCreated();
  }

  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    if (window()) {
      window()->SetTitle(kWindowTitle);
    }
    for (const mua::ShaderStorageRepair& repair : mua::RepairShaderStorage(defaults.cache_root)) {
      REXLOG_WARN("Shader cache {}: dropped {} damaged record(s), kept {}", repair.file, repair.dropped, repair.kept);
    }
    game_settings_.CaptureLateRegistered();
    if (!show_launcher_) {
      mua::ApplyShaderWait();
      mua::ApplyMultisampling();
      mua::PinGuestVideoMode();
      return defaults;
    }
    if (!imgui_drawer()) {
      game_settings_.Apply();
      mua::PinGuestVideoMode();
      return defaults;
    }
    REXLOG_INFO("Launcher shown");
    mua::LauncherCallbacks callbacks;
    callbacks.play = [this, resume, defaults] {
      app_context().CallInUIThreadDeferred([this, resume, defaults] {
        game_settings_.Apply();
        mua::ApplyDetailedLogging();
        mua::PinGuestVideoMode();
        REXLOG_INFO("DEBUG_SAULO Play: fullscreen setting {}, window fullscreen {}",
                    rex::cvar::GetFlagByName("fullscreen"), window() && window()->IsFullscreen());
        resume(defaults);
      });
    };
    callbacks.window_changed = [this] { return game_settings_.WindowChanged(); };
    callbacks.restart = [this] {
      if (!mua::RelaunchSelf(L"--mua_skip_launcher=true")) {
        return false;
      }
      app_context().CallInUIThreadDeferred([] {
        REXLOG_INFO("Restarted in a new window; hard-exiting this process.");
        rex::FlushLogging();
        std::_Exit(0);
      });
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
        mua::LogsFolder(port_folder, defaults.user_data_root, mua::ActiveLogFile(), false);
    mua::LauncherPaths paths{defaults.game_data_root,
                             defaults.user_data_root,
                             mua::PlayerSettingsPath(defaults.user_data_root),
                             logs_dir,
                             user_data_.outcome,
                             immediate_drawer(),
                             {}};
    if (!mua::GetRecompiledImage().available) {
      paths.runtime_blocker = "Recompiled game code has not been generated for this build yet.";
    }
    mua::ShowLauncher(imgui_drawer(), std::move(paths), game_settings_, std::move(callbacks));
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
    ReXApp::LaunchModule();
  }

  void OnPostSetup() override {
    REXLOG_INFO("Scheduling: {}", trg::TuneProcessScheduling());
    if (window()) {
      mua::CheckWindowDisplay(window()->GetNativeWindowHandle(), REXCVAR_GET(monitor));
      mua::StartHangWatchdog(window()->GetNativeWindowHandle(),
                             mua::LogsFolder(mua::PortFolder(), user_data_root(), mua::ActiveLogFile(), false));
      AttachWindowHost(window(), app_context(), [this] {
        rex::ui::ImGuiDrawer* drawer = imgui_drawer();
        return drawer && drawer->GetIO().WantCaptureMouse;
      });
      if (imgui_drawer()) {
        mua::CreateFrameCounter(imgui_drawer());
        CreateCursorSync(imgui_drawer());
      }
    }
  }

  void OnShutdown() override {
    // ReXApp destroys the window before the runtime; the input drivers must detach while it still exists.
    if (auto* input = runtime() ? runtime()->input_system() : nullptr) {
      input->Shutdown();
    }
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

 private:
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
  std::filesystem::path shipped_config_;
  mua::UserDataSelection user_data_;
  bool show_launcher_ = false;
};
