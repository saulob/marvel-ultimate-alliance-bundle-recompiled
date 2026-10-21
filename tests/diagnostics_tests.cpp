// Startup diagnostic formatting: port identity, selected versus resolved GPU, launcher selections versus
// effective window state, game identity without paths, timing wording and privacy. Run: mua_diagnostics_tests

#include <cstdio>
#include <string>
#include <vector>

#include "diagnostics.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string Join(const std::vector<std::string>& lines) {
  std::string text;
  for (const std::string& line : lines) {
    text += line;
    text += '\n';
  }
  return text;
}

mua::StartupDiagnostics Sample() {
  mua::StartupDiagnostics diagnostics;
  diagnostics.launcher_display.mode = "Borderless Fullscreen";
  diagnostics.launcher_display.window_size = "1280x720";
  diagnostics.launcher_display.monitor = "Display 1";
  diagnostics.launcher_display.aspect_ratio = "16:9 Original";
  diagnostics.launcher_display.vsync = "Off";
  diagnostics.launcher_display.render_resolution_in_title = "Off";
  diagnostics.launcher_graphics.graphics_card = "Automatic";
  diagnostics.launcher_graphics.render_resolution = "2x (2560x1440)";
  diagnostics.launcher_graphics.anti_aliasing = "Off";
  diagnostics.launcher_graphics.multisampling = "2x MSAA";
  diagnostics.launcher_graphics.texture_filtering = "4x";
  diagnostics.system.os = "Windows 11 64-bit (build 22631)";
  diagnostics.system.cpu = "Test CPU";
  diagnostics.system.logical_processors = 16;
  diagnostics.gpu.selected = "Automatic";
  diagnostics.gpu.resolved = "NVIDIA GeForce RTX 4070";
  diagnostics.gpu.adapter = 0;
  diagnostics.gpu.vendor_id = 0x10DE;
  diagnostics.gpu.device_id = 0x2786;
  diagnostics.gpu.resolved_known = true;
  diagnostics.display.monitor = 1;
  diagnostics.display.width = 2560;
  diagnostics.display.height = 1440;
  diagnostics.display.refresh_hz = 144;
  diagnostics.display.scale_percent = 225;
  diagnostics.display.fullscreen = true;
  diagnostics.display.configured_width = 1280;
  diagnostics.display.configured_height = 720;
  diagnostics.display.output_width = 2560;
  diagnostics.display.output_height = 1440;
  diagnostics.display.aspect = "16:9";
  diagnostics.game.title_id = 0x415607DA;
  diagnostics.game.executable = "default.xex";
  diagnostics.game.size = 3981312;
  diagnostics.game.sha256 = "ABCDEF";
  diagnostics.game.fingerprint = "profile:mua-gold-x360";
  diagnostics.game.status = "verified";
  diagnostics.gameplay.present = "guest 60Hz | host VBlank (VSync On)";
  diagnostics.graphics.render_width = 1280;
  diagnostics.graphics.render_height = 720;
  diagnostics.graphics.guest_width = 1280;
  diagnostics.graphics.guest_height = 720;
  diagnostics.graphics.msaa = "2x native";
  diagnostics.graphics.anti_aliasing = "Off";
  diagnostics.graphics.texture_filtering = "4x";
  diagnostics.graphics.allow_invalid_fetch_constants = "true";
  diagnostics.graphics.render_target_path = "rov";
  diagnostics.input.keyboard_mode = "On";
  diagnostics.input.controller = "Xbox One S Controller";
  return diagnostics;
}

void TestIdentity() {
  std::printf("port and build identity\n");
  const mua::ApplicationIdentity identity{
      .product = "Marvel Ultimate Alliance",
      .version = "0.1.0",
      .build_config = "RelWithDebInfo",
      .architecture = "x86-64",
      .sdk = "0.10.0",
      .backend = "Direct3D 12",
  };
  CHECK(mua::FormatApplicationVersion(identity) == "Marvel Ultimate Alliance port 0.1.0");
  const std::string build = mua::FormatBuildIdentity(identity);
  CHECK(Has(build, "Build: RelWithDebInfo x86-64"));
  CHECK(Has(build, "ReXGlue SDK 0.10.0"));
  CHECK(Has(build, "Direct3D 12"));
}

void TestStartupBlock() {
  std::printf("startup snapshot carries launcher and runtime fields\n");
  const std::string text = Join(mua::FormatStartupDiagnostics(Sample()));
  CHECK(Has(text, "Runtime configuration:"));
  CHECK(Has(text, "Launcher Display: Display mode=Borderless Fullscreen | Window size=1280x720 | "
                  "Monitor=Display 1 | Aspect Ratio=16:9 Original | VSync=Off | Render resolution in title=Off"));
  CHECK(Has(text, "Launcher Graphics: Graphics card=Automatic | Render resolution=2x (2560x1440) | "
                  "Anti-aliasing=Off | Multisampling=2x MSAA | Texture filtering=4x"));
  CHECK(Has(text, "Runtime OS: Windows 11 64-bit (build 22631)"));
  CHECK(Has(text, "Runtime CPU: Test CPU | logical processors=16"));
  CHECK(Has(text, "Runtime GPU: selected=Automatic | resolved=NVIDIA GeForce RTX 4070 | adapter=0 | "
                  "vendor/device=10DE/2786"));
  CHECK(Has(text, "Runtime display: 2560x1440@144Hz | scale=225% | monitor=1 | fullscreen | window=1280x720 | "
                  "output=2560x1440 | aspect=16:9"));
  CHECK(Has(text, "Runtime game: title_id=415607DA | default.xex | size=3981312 | "
                  "fingerprint=profile:mua-gold-x360 | status=verified"));
  CHECK(Has(text, "Runtime game sha256: ABCDEF"));
  CHECK(Has(text, "Runtime timing: present=guest 60Hz | host VBlank (VSync On)"));
  CHECK(Has(text, "Runtime renderer: render=1280x720 | guest=1280x720 | msaa=2x native | anti-aliasing=Off | "
                  "texture filtering=4x | allow_invalid_fetch_constants=true | target=rov"));
  CHECK(Has(text, "Runtime input: keyboard_mode=On | controller=Xbox One S Controller"));
}

void TestSelectedWindowResolution() {
  std::printf("selected window size differs from the effective window\n");
  mua::StartupDiagnostics diagnostics = Sample();
  diagnostics.launcher_display.mode = "Windowed";
  diagnostics.launcher_display.window_size = "1600x900";
  diagnostics.launcher_display.aspect_ratio = "16:9 Original";
  diagnostics.display.fullscreen = false;
  diagnostics.display.configured_width = 1600;
  diagnostics.display.configured_height = 900;
  diagnostics.display.output_width = 1600;
  diagnostics.display.output_height = 900;
  diagnostics.display.aspect = "16:9";
  const std::string text = Join(mua::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "Launcher Display: Display mode=Windowed | Window size=1600x900 | "
                  "Monitor=Display 1 | Aspect Ratio=16:9 Original | VSync=Off"));
  CHECK(Has(text, "Runtime display: 2560x1440@144Hz | scale=225% | monitor=1 | windowed | "
                  "window=1600x900 | output=1600x900 | aspect=16:9"));
}

void TestSelectedAndResolvedDiffer() {
  std::printf("selected and resolved GPU stay distinguishable\n");
  mua::StartupDiagnostics diagnostics = Sample();
  diagnostics.launcher_graphics.graphics_card = "Intel UHD 730";
  diagnostics.gpu.selected = "Intel UHD 730";
  diagnostics.gpu.resolved = "NVIDIA GeForce RTX 4070";
  const std::string text = Join(mua::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "Launcher Graphics: Graphics card=Intel UHD 730"));
  CHECK(Has(text, "Runtime GPU: selected=Intel UHD 730"));
  CHECK(Has(text, "resolved=NVIDIA GeForce RTX 4070"));

  diagnostics.gpu.resolved_known = false;
  CHECK(Has(Join(mua::FormatStartupDiagnostics(diagnostics)), "resolved=unknown"));

  mua::StartupDiagnostics automatic = Sample();
  automatic.launcher_graphics.graphics_card = "Automatic";
  automatic.gpu.selected = "Automatic";
  automatic.gpu.resolved = "NVIDIA GeForce RTX 4070";
  const std::string automatic_text = Join(mua::FormatStartupDiagnostics(automatic));
  CHECK(Has(automatic_text, "Launcher Graphics: Graphics card=Automatic"));
  CHECK(Has(automatic_text, "Runtime GPU: selected=Automatic | resolved=NVIDIA GeForce RTX 4070"));
}

void TestPresentationWording() {
  std::printf("guest presentation and host vsync are named separately\n");
  CHECK(mua::PresentationText(false) == "guest 60Hz | host VBlank (VSync On)");
  CHECK(mua::PresentationText(true) == "guest 60Hz | host Immediate (tearing allowed)");
}

void TestCleanExit() {
  std::printf("the clean-exit line carries the uptime\n");
  CHECK(mua::FormatCleanExit("01:02:03") == "Execution complete: clean exit, uptime 01:02:03");
}

void TestPrivacy() {
  std::printf("absolute paths are reduced to file names\n");
  mua::StartupDiagnostics diagnostics = Sample();
  diagnostics.game.executable = "C:\\Users\\Saulo\\Documents\\Games\\default.xex";
  const std::string text = Join(mua::FormatStartupDiagnostics(diagnostics));
  CHECK(Has(text, "default.xex"));
  CHECK(!Has(text, "C:\\Users"));
  CHECK(!Has(text, "Saulo"));
  CHECK(!Has(text, "Documents"));

  CHECK(mua::FileNameOnly("C:\\Users\\Saulo\\Documents\\Games\\default.xex") == "default.xex");
  CHECK(mua::FileNameOnly("/home/saulo/games/default.xex") == "default.xex");
  CHECK(mua::FileNameOnly("default.xex") == "default.xex");
}

}  // namespace

int main() {
  TestIdentity();
  TestStartupBlock();
  TestSelectedWindowResolution();
  TestSelectedAndResolvedDiffer();
  TestPresentationWording();
  TestCleanExit();
  TestPrivacy();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
