// The shipped runtime configuration: the values the runtime starts with, the compatibility option for invalid
// texture fetch constants, and that the built program carries the same file next to the runtime that knows these
// settings. Run: mua_runtime_config_tests

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

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

std::string ReadBytes(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

const fs::path kSource = MUA_SRC_DIR;
const fs::path kShipped = kSource / ".." / "config" / "marvel_ultimate_alliance.toml";

void TestShippedValues() {
  std::printf("shipped runtime settings\n");
  const toml::table config = toml::parse_file(kShipped.string());
  CHECK(config["gpu_plugin"].value<std::string>() == "xenos");
  CHECK(config["gpu_allow_invalid_fetch_constants"].value<bool>() == true);
  CHECK(config["render_target_path_d3d12"].value<std::string>() == "rov");
  CHECK(config["render_target_path_vulkan"].value<std::string>() == "fsi");
  CHECK(config["async_shader_wait_ms"].value<int64_t>() == 16);
  CHECK(config["mnk_mode"].value<bool>() == true);
  CHECK(config["hid_mappings_file"].value<std::string>() == "");
  CHECK(config["fullscreen"].value<bool>() == false);
  CHECK(config["monitor"].value<int64_t>() == 1);
  CHECK(config["window_width"].value<int64_t>() == 853);
  CHECK(config["window_height"].value<int64_t>() == 480);
  // The guest video mode is pinned in code, not in this file.
  CHECK(!config.contains("video_mode_width"));
  CHECK(!config.contains("video_mode_height"));
}

void TestFetchConstantOptionIsConfigOnly() {
  std::printf("the fetch constant option comes from the shipped settings, not from code\n");
  for (const char* file : {"settings.cpp", "launcher.cpp", "marvel_ultimate_alliance_app.h", "window_host.cpp"}) {
    CHECK(!Has(ReadBytes(kSource / file), "SetFlagByName(\"gpu_allow_invalid_fetch_constants\""));
  }
}

void TestManifest() {
  std::printf("the program manifest keeps per-monitor DPI and makes paths UTF-8 for the runtime's settings files\n");
  const std::string manifest = ReadBytes(kSource / ".." / "packaging" / "windows" / "marvel_ultimate_alliance.manifest");
  CHECK(Has(manifest, ">PerMonitorV2</dpiAwareness>"));
  CHECK(Has(manifest, ">UTF-8</activeCodePage>"));
}

void TestBuiltProgram() {
#ifdef MUA_STAGED_CONFIG
  std::printf("the built program carries the shipped settings and a runtime that knows them\n");
  const fs::path staged = MUA_STAGED_CONFIG;
  if (!fs::exists(staged)) {
    std::printf("  skipped: build the program first (%s)\n", staged.string().c_str());
    return;
  }
  CHECK(ReadBytes(staged) == ReadBytes(kShipped));
  const std::string gpu = ReadBytes(staged.parent_path() / MUA_GPU_PLUGIN_NAME);
  const std::string runtime = ReadBytes(staged.parent_path() / MUA_RUNTIME_NAME);
  CHECK(Has(gpu, "gpu_allow_invalid_fetch_constants"));
#ifdef _WIN32
  CHECK(Has(gpu, "render_target_path_d3d12"));
  CHECK(Has(gpu, "async_shader_wait_ms"));
  CHECK(Has(gpu, "REX_DEV_FRAME_LOG"));
#else
  CHECK(Has(gpu, "render_target_path_vulkan"));
#endif
  CHECK(Has(runtime, "hid_mappings_file"));
  CHECK(Has(runtime, "mnk_mode"));
  CHECK(Has(runtime, "gpu_plugin"));
  CHECK(Has(runtime, "window_width"));
#endif
}

}  // namespace

int main() {
  TestShippedValues();
  TestFetchConstantOptionIsConfigOnly();
  TestManifest();
  TestBuiltProgram();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
