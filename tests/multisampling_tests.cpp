// Multisampling: host samples for each guest sample count and launcher choice, the Multisampling row,
// its settings key and where native_2x_msaa is set from it, and what the startup log says about each renderer
// path. The render target caches run only with a GPU, so their choice is mirrored by HostMsaaSamples.
// Run: mua_multisampling_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "multisampling.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TestHostSamples() {
  std::printf("host samples per launcher choice\n");
  for (bool supported : {false, true}) {
    for (bool compatibility : {false, true}) {
      bool native = mua::Native2xMsaaFor(compatibility);
      CHECK(mua::HostMsaaSamples(1, native, supported) == 1);
      CHECK(mua::HostMsaaSamples(4, native, supported) == 4);
    }
  }
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), true) == 2);
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), true) == 4);
  CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), false) == 4);
  // No choice draws fewer host samples than the game asks for, and native is never more.
  for (bool supported : {false, true}) {
    CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), supported) >= 2);
    CHECK(mua::HostMsaaSamples(2, mua::Native2xMsaaFor(false), supported) <=
          mua::HostMsaaSamples(2, mua::Native2xMsaaFor(true), supported));
  }
}

void TestPathDecidesTheChoice() {
  std::printf("the choice only applies to host render targets\n");
  CHECK(!mua::HostRenderTargetsUsed("rov", true));
  CHECK(mua::HostRenderTargetsUsed("rov", false));
  CHECK(mua::HostRenderTargetsUsed("rtv", true));
  CHECK(mua::HostRenderTargetsUsed("", true));
  CHECK(!mua::HostRenderTargetsUsed("fsi", true));
  CHECK(mua::HostRenderTargetsUsed("fsi", false));
  CHECK(mua::HostRenderTargetsUsed("fbo", true));
}

void TestRovIsFixed() {
  std::printf("under ROV the choice is not applied and the logs say so\n");
  for (bool compatibility : {false, true}) {
    CHECK(!mua::Native2xMsaaToApply(false, compatibility).has_value());
    CHECK(mua::MsaaLauncherText(false, compatibility, "ROV") == "ROV (fixed)");
    CHECK(mua::MsaaRuntimeText(false, compatibility, "ROV") == "2x as 4x (ROV, fixed)");
    CHECK(mua::MsaaLauncherText(false, compatibility, "FSI") == "FSI (fixed)");
    CHECK(mua::MsaaRuntimeText(false, compatibility, "FSI") == "2x as 4x (FSI, fixed)");
  }
  CHECK(mua::Native2xMsaaToApply(true, false) == std::optional<bool>(true));
  CHECK(mua::Native2xMsaaToApply(true, true) == std::optional<bool>(false));
}

void TestHostRenderTargetsLogTheChoice() {
  std::printf("with host render targets the logs follow the choice\n");
  for (const char* edram : {"ROV", "FSI"}) {
    CHECK(mua::MsaaLauncherText(true, false, edram) == "2x MSAA");
    CHECK(mua::MsaaLauncherText(true, true, edram) == "4x MSAA (Compatibility)");
    CHECK(mua::MsaaRuntimeText(true, false, edram) == "2x native (host render targets)");
    CHECK(mua::MsaaRuntimeText(true, true, edram) == "2x as 4x (host render targets, compatibility)");
    CHECK(mua::MsaaRuntimeText(true, false, edram) != mua::MsaaRuntimeText(true, true, edram));
    CHECK(mua::MsaaRuntimeText(true, true, edram) != mua::MsaaRuntimeText(false, true, edram));
  }
}

void TestSetting() {
  std::printf("setting, launcher row and startup\n");
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_msaa_compatibility, false,"));
  CHECK(Has(settings, "    \"mua_msaa_compatibility\",\n") ||
        Has(settings, "    \"mua_msaa_compatibility\",\r\n"));
  CHECK(!Has(settings, "    \"native_2x_msaa\","));
  CHECK(Has(settings, "  ApplyMultisampling();\n}") || Has(settings, "  ApplyMultisampling();\r\n}"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"native_2x_msaa\""));
  CHECK(Has(settings, "Native2xMsaaToApply(HostRenderTargetsActive(), REXCVAR_GET(mua_msaa_compatibility))"));
  CHECK(Has(settings, "MsaaRuntimeText(host_render_targets, msaa_compatibility, kBackendSettings.pixel_shader_edram)"));
  CHECK(Has(settings, "MsaaLauncherText(host_render_targets, msaa_compatibility, kBackendSettings.pixel_shader_edram)"));
  CHECK(!Has(settings, "setting unused"));
  CHECK(!Has(settings, "settings.GetBool(\"mua_msaa_compatibility\", false) ? \"4x"));

  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Has(launcher, "\"mua_msaa_compatibility\", false, \"2\\xC3\\x97 MSAA\", \"4\\xC3\\x97 MSAA (Compatibility)\""));
  CHECK(!Has(launcher, "\"native_2x_msaa\""));
  const std::size_t gate = launcher.find("if (MultisamplingApplies()) {");
  const std::size_t row = launcher.find("ui.Toggle(\"Multisampling\"");
  CHECK(gate != std::string::npos && row != std::string::npos && gate < row);
  CHECK(Has(launcher, "rex::cvar::GetFlagByName(kBackendSettings.render_target_path)"));
  CHECK(Has(settings, "rex::cvar::GetFlagByName(kBackendSettings.render_target_path)"));
  CHECK(!Has(launcher, "\"render_target_path_d3d12\"") && !Has(settings, "\"render_target_path_d3d12\""));

  // Old settings.toml values stay valid, and the hidden row never asks for a new process.
  const std::size_t window_keys = settings.find("const std::vector<std::string> kWindowKeys");
  const std::size_t window_keys_end = settings.find("};", window_keys);
  CHECK(window_keys != std::string::npos && window_keys_end != std::string::npos);
  CHECK(settings.substr(window_keys, window_keys_end - window_keys).find("mua_msaa_compatibility") == std::string::npos);

  const std::string shipped = ReadSource("../config/marvel_ultimate_alliance.toml");
  CHECK(Has(shipped, "render_target_path_d3d12 = \"rov\""));
  CHECK(Has(shipped, "render_target_path_vulkan = \"fsi\""));
  CHECK(!Has(shipped, "native_2x_msaa"));

  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const std::size_t game_branch = app.find("if (!IsLauncher()) {");
  const std::size_t shader_wait = app.find("mua::ApplyShaderWait();", game_branch);
  const std::size_t multisampling = app.find("mua::ApplyMultisampling();", game_branch);
  CHECK(game_branch != std::string::npos && shader_wait != std::string::npos && shader_wait < multisampling);
}

}  // namespace

int main() {
  TestHostSamples();
  TestPathDecidesTheChoice();
  TestRovIsFixed();
  TestHostRenderTargetsLogTheChoice();
  TestSetting();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
