#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mua {

struct ApplicationIdentity {
  std::string product;
  std::string version;
  std::string build_config;
  std::string architecture;
  std::string sdk;
  std::string backend;
};

struct SystemIdentity {
  std::string os;
  std::string cpu;
  int logical_processors = 0;
};

struct GraphicsAdapterIdentity {
  std::string selected;
  std::string resolved;
  int adapter = -1;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  bool resolved_known = false;
};

struct DisplayIdentity {
  int monitor = 0;
  int width = 0;
  int height = 0;
  int refresh_hz = 0;
  int scale_percent = 100;
  bool fullscreen = false;
  int configured_width = 0;
  int configured_height = 0;
  int output_width = 0;
  int output_height = 0;
  std::string aspect;
};

struct GameIdentity {
  uint32_t title_id = 0;
  std::string executable;
  uint64_t size = 0;
  std::string sha256;
  std::string fingerprint;
  std::string status;
};

struct LauncherDisplayIdentity {
  std::string mode;
  std::string window_size;
  std::string monitor;
  std::string aspect_ratio;
  std::string vsync;
  std::string render_resolution_in_title;
};

struct LauncherGraphicsIdentity {
  std::string graphics_card;
  std::string render_resolution;
  std::string anti_aliasing;
  std::string multisampling;
  std::string texture_filtering;
};

struct GameplayIdentity {
  std::string present;
};

struct GraphicsIdentity {
  int render_width = 0;
  int render_height = 0;
  int guest_width = 0;
  int guest_height = 0;
  std::string msaa;
  std::string anti_aliasing;
  std::string texture_filtering;
  std::string allow_invalid_fetch_constants;
  std::string render_target_path;
};

struct InputIdentity {
  std::string keyboard_mode;
  std::string controller;
};

struct StartupDiagnostics {
  LauncherDisplayIdentity launcher_display;
  LauncherGraphicsIdentity launcher_graphics;
  SystemIdentity system;
  GraphicsAdapterIdentity gpu;
  DisplayIdentity display;
  GameIdentity game;
  GameplayIdentity gameplay;
  GraphicsIdentity graphics;
  InputIdentity input;
};

std::string FormatApplicationVersion(const ApplicationIdentity& identity);
std::string FormatBuildIdentity(const ApplicationIdentity& identity);
std::vector<std::string> FormatStartupDiagnostics(const StartupDiagnostics& diagnostics);

// File name without any folder part, so a diagnostic never carries an absolute path.
std::string FileNameOnly(std::string_view path);

std::string PresentationText(bool vsync_off);
std::string FormatCleanExit(std::string_view uptime);

}  // namespace mua
