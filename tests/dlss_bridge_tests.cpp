// NVIDIA DLSS: patches/rexglue-sdk/0160 carries the same interface as src/dlss_bridge/rex_dlss_bridge.h and loads the
// bridge only when 0161's scene upscaler first draws a scene with DLSS selected, 0161 evaluates DLSS on the command
// list from the scene's own color, depth and motion, 0162 draws the targets drawn only before the scene at the scene's
// whole scale and stretches their resolves back, 0163 takes the scene's depth once between depth clears, 0164 draws
// the pre-scene targets only the scene samples at 1x in Performance, the app sets the DLSS mode only for the DLSS
// choice, MUA1 and its Gold Edition offer it, the SDK comes only from NVIDIA's pinned commit, releases carry no DLSS
// file yet, and no patch carries diagnostics. With the NVIDIA DLSS SDK, it also loads the built mua_dlss_bridge.dll and
// calls its interface without a GPU.
// Run: mua_dlss_bridge_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "dlss_bridge/rex_dlss_bridge.h"
#include "dlss_support.h"
#include "game_definition.h"
#include "upscaling.h"

#if defined(_WIN32) && defined(MUA_DLSS_BRIDGE_DLL)
#include <windows.h>

#include <thread>
#endif

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string content = text.str();
  std::string lf;
  lf.reserve(content.size());
  for (char c : content) {
    if (c != '\r') {
      lf += c;
    }
  }
  return lf;
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

const std::string kRoot = std::string(MUA_SRC_DIR) + "/../";
const std::string kPatches = kRoot + "patches/rexglue-sdk/";
constexpr char kBridgePatch[] = "0160-d3d12-dlss-bridge.windows.patch";
constexpr char kScenePatch[] = "0161-d3d12-dlss-scene-upscaler.windows.patch";
constexpr char kPreScenePatch[] = "0162-d3d12-dlss-pre-scene-target-scale.windows.patch";
constexpr char kDepthPatch[] = "0163-d3d12-dlss-depth-capture.windows.patch";
constexpr char kNativePatch[] = "0164-d3d12-dlss-native-pre-scene-targets.windows.patch";

// The lines a patch adds, without their context.
std::string AddedLines(const std::string& patch) {
  std::istringstream lines(patch);
  std::string added;
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind("+", 0) == 0 && line.rfind("+++", 0) != 0) {
      added += line + "\n";
    }
  }
  return added;
}

// The content of a file a patch adds.
std::string AddedFile(const std::string& patch, const std::string& path) {
  const size_t start = patch.find("+++ b/" + path + "\n");
  if (start == std::string::npos) {
    return {};
  }
  const size_t hunk = patch.find("\n@@", start);
  const size_t body = patch.find('\n', hunk + 1) + 1;
  const size_t end = patch.find("\ndiff --git", body);
  std::istringstream lines(patch.substr(body, end == std::string::npos ? std::string::npos : end + 1 - body));
  std::string content;
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] == '+') {
      content += line.substr(1) + "\n";
    }
  }
  return content;
}

void TestInterfaceCopy() {
  const std::string patch = ReadFile(kPatches + kBridgePatch);
  const std::string header = ReadFile(kRoot + "src/dlss_bridge/rex_dlss_bridge.h");
  CHECK(!header.empty());
  CHECK(AddedFile(patch, "include/rex/graphics/d3d12/rex_dlss_bridge.h") == header);
}

void TestBridgeLoader() {
  const std::string patch = ReadFile(kPatches + kBridgePatch);
  CHECK(Has(patch, "+        d3d12/dlss_bridge.cpp\n"));
  CHECK(Has(patch, "GpuVendorID::kNvidia"));
  CHECK(Has(patch, "+REXCVAR_DEFINE_INT32(d3d12_dlss_ngx_log_level, 0,"));
  // Nothing at setup: the old startup probe is gone.
  CHECK(!Has(patch, "ProbeDlss") && !Has(patch, "SetupContext"));
  // The plugin never includes NVIDIA's headers.
  CHECK(!Has(patch, "nvsdk_ngx"));
}

void TestSceneUpscaler() {
  const std::string patch = ReadFile(kPatches + kScenePatch);
  CHECK(!patch.empty());
  CHECK(Has(patch, "+        d3d12/scene_upscaler.cpp\n"));
  CHECK(Has(patch, "+REXCVAR_DEFINE_STRING(d3d12_dlss_mode, \"off\","));
  CHECK(Has(patch, "+REXCVAR_DEFINE_STRING(d3d12_dlss_bridge, \"\","));
  CHECK(Has(patch, "+REXCVAR_DEFINE_STRING(d3d12_dlss_data_path, \"\","));
  CHECK(Has(patch, "+    .allowed({\"off\", \"dlaa\", \"quality\", \"balanced\", \"performance\", \"ultra_performance\"})"));
  // Off returns before anything is loaded, and NGX starts with the first scene draw.
  CHECK(Has(patch, "+  if (!quality) {\n+    return true;\n+  }"));
  CHECK(Count(patch, "bridge_.Initialize(") == 1);
  CHECK(Has(patch, "+bool SceneUpscaler::EnsureRuntime() {"));
  CHECK(Has(patch, "+  if (!EnsureRuntime()) {\n+    return adjustment;\n+  }"));
  // The command processor's hooks.
  CHECK(Has(patch, "+  const SceneUpscaler::DrawAdjustment scene_adjustment = scene_upscaler_->BeginDraw("));
  CHECK(Has(patch, "+      scene_upscaler_->BeforeResolve(*register_file_, resolve_info);"));
  CHECK(Has(patch, "+  scene_upscaler_->AfterResolve();"));
  CHECK(Has(patch, "+  scene_upscaler_->BeforeSwap();"));
  // Each resolve's texture gets the scene stretched right away; the DLSS output goes only to those no later one covers.
  CHECK(Has(patch, "+  EncodeStaging(resolve.depth ? Encode::kStretchedDepth : Encode::kStretchedColor, &resolve);"));
  CHECK(Has(patch, "+    const std::vector<bool> covered = scene::CoveredByLater(rects);"));
  CHECK(Has(patch, "+  info.copy_dest_coordinate_info.copy_sample_select = draw_util::SanitizeCopySampleSelect("));
  // Color and depth are each taken at the first resolve after they were drawn, before a resolve clears them.
  CHECK(Has(patch, "+  const uint32_t planes = uint32_t(!depth && color_fresh_) | uint32_t(depth_fresh_) << 1;"));
  CHECK(Has(patch, "+  depth_fresh_ &= !resolve.info.rb_copy_control.depth_clear_enable;"));
  CHECK(Has(patch, "+++ b/include/rex/graphics/d3d12/scene_layout.h\n"));
  // DLSS is created and evaluated on the command list, from the scene's own inputs.
  CHECK(Has(patch, "+  void ExternalCall(ExternalCallFunction function, void* context, const void* data, size_t data_size) {"));
  CHECK(Has(patch, "ExternalCall(EvaluateOnCommandList, &feature_, &args,"));
  CHECK(Has(patch, "feature.api->create_feature(command_list, &args.feature_desc, &feature.handle)"));
  CHECK(Has(patch, "feature.api->evaluate(command_list, feature.handle, &args.evaluate)"));
  CHECK(Has(patch, "+    evaluate.color = color_;\n+    evaluate.depth = depth_;\n+    evaluate.motion_vectors = motion_;"));
  CHECK(Has(patch, "REX_DLSS_FEATURE_MV_LOW_RES"));
  CHECK(Has(patch, "query_optimal_settings(output_width_, output_height_, quality_, &optimal_)"));
  // Every failure falls back to drawing the scene at full resolution.
  CHECK(Has(patch, "the scene renders at full resolution without DLSS"));
  CHECK(Has(patch, "+  bool CopyResolveToMemory(const draw_util::ResolveInfo& resolve_info, D3D12SharedMemory& shared_memory,"));
  for (const char* shader : {"scene_capture", "scene_motion", "scene_encode"}) {
    CHECK(Has(patch, ("+++ b/src/graphics/shaders/" + std::string(shader) + ".cs.hlsl\n").c_str()));
    CHECK(Has(patch, ("+++ b/src/graphics/shaders/bytecode/d3d12_5_1/" + std::string(shader) + "_cs.h\n").c_str()));
  }
  CHECK(!Has(patch, "nvsdk_ngx") && !Has(patch, "DEBUG_SAULO"));
}

void TestPreSceneTargets() {
  const std::string patch = ReadFile(kPatches + kPreScenePatch);
  CHECK(!patch.empty());
  // The smallest whole scale with the scene's detail, decided once DLSS knows the scene's size.
  CHECK(Has(patch, "+      pre_scene_scale ? scene::PreSceneScale(render_width_, guest_width_, resolution_scale_x_) : resolution_scale_x_;"));
  CHECK(Has(patch, "+REXCVAR_DEFINE_BOOL(d3d12_dlss_pre_scene_scale, true, \"GPU/D3D12\","));
  CHECK(Has(patch, "+  REXGPU_INFO(\"DLSS: scale domains | output, HUD and menus {}x{} ({}) | scene {}x{} ({}) | targets drawn"));
  // Draws and resolves of a reduced surface agree for the whole frame.
  CHECK(Count(patch, "+  if (pre_scene_frame_ && pre_scene_targets_.IsReduced(key)) {") == 2);
  CHECK(Has(patch, "+    adjustment.scale_x = float(pre_scene_scale_x_) / float(resolution_scale_x_);"));
  CHECK(Has(patch, "+    return StretchPreSceneResolve(resolve_info);"));
  CHECK(Has(patch, "+  if (pre_scene_targets_.EndFrame(scene_drawn_, scene_key_) && runtime_ready_) {"));
  // The resolve copies the stretched rectangle; the clear still goes to the EDRAM.
  CHECK(Has(patch, "+      copy_source = scene_upscaler_->BeforeResolve(*register_file_, resolve_info);"));
  CHECK(Has(patch, "+                                             written_address, written_length, copy_source);"));
  CHECK(Has(patch, "+    if (!CopyResolveToMemory(resolve_info, shared_memory, texture_cache, copy_source, written_address_out,"));
  CHECK(Has(patch, "+  const uint32_t staging_tiles = pre_scene_reduced ? xenos::kEdramTileCount : staging_pitch_tiles_ * staging_rows;"));
  CHECK(Has(patch, "+++ b/src/graphics/shaders/scene_edram_stretch.cs.hlsl\n"));
  CHECK(Has(patch, "+++ b/src/graphics/shaders/bytecode/d3d12_5_1/scene_edram_stretch_cs.h\n"));
  CHECK(Has(patch, "+  uint2 source_pixel = pixel * draw_scale / output_scale;"));
  // Textures sampled after the scene keep their surface at the full scale.
  CHECK(Has(patch, "+        pre_scene_targets_.NoteSampledAfterScene(resolve.key);"));
  // No jitter, no depth bias change, nothing temporary.
  const std::string added = AddedLines(patch);
  CHECK(!Has(added, "jitter") && !Has(added, "poly_offset") && !Has(patch, "DEBUG_SAULO"));
}

void TestDepthCapture() {
  const std::string patch = ReadFile(kPatches + kDepthPatch);
  CHECK(!patch.empty());
  // Depth once between depth clears, after draws that wrote it; color keeps its rule.
  CHECK(Has(patch, "+  planes_.NoteSceneDraw(depth_control.z_enable && depth_control.z_write_enable);"));
  CHECK(Has(patch, "+  const uint32_t planes = planes_.Take(depth);"));
  CHECK(Has(patch, "+    const bool depth = !depth_taken_ && (depth_written_ || !depth_written_in_frame_);"));
  CHECK(Has(patch, "-  color_fresh_ = depth_fresh_ = true;"));
  CHECK(Has(patch, "+class ScenePlanes {"));
  const std::string added = AddedLines(patch);
  // Nothing else: no motion, scaling or temporary code.
  CHECK(!Has(added, "motion") && !Has(added, "pre_scene") && !Has(added, "jitter") && !Has(patch, "DEBUG_SAULO"));
}

void TestNativePreScene() {
  const std::string patch = ReadFile(kPatches + kNativePatch);
  CHECK(!patch.empty());
  // Performance modes only, with a smaller scale to come down from, and a switch.
  CHECK(Has(patch, "(quality_ == REX_DLSS_QUALITY_PERFORMANCE || quality_ == REX_DLSS_QUALITY_ULTRA_PERFORMANCE) &&"));
  CHECK(Has(patch, "+REXCVAR_DEFINE_BOOL(d3d12_dlss_pre_scene_native, true, \"GPU/D3D12\","));
  // Draws and resolves use the same per-surface scale.
  CHECK(Count(patch, "PreSceneScaleX(key)") == 2);
  CHECK(Has(patch, "+  constants.draw_scale[0] = PreSceneScaleX(key);"));
  // The game's own depth bias at 1x, nowhere else.
  CHECK(Count(patch, "+      adjustment.depth_bias_resolution_scale = 1.0f;") == 1);
  CHECK(Has(patch, "(depth_bias_resolution_scale > 0.0f ? depth_bias_resolution_scale"));
  // Proof the scene samples them, checked only while it matters.
  CHECK(Has(patch, "+  if (pre_scene_targets_.AwaitsSceneSampling() && !other_resolves_.empty()) {"));
  const std::string added = AddedLines(patch);
  CHECK(!Has(added, "jitter_x =") && !Has(patch, "DEBUG_SAULO"));
}

void TestApp() {
  const std::string settings = ReadFile(kRoot + "src/settings.cpp");
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_dlss_bridge\", Utf8(bridge));"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_dlss_data_path\", Utf8(cache_root / \"ngx\"));"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_dlss_mode\", mode);"));
  CHECK(Has(settings, "  if (!selected) {\n    rex::cvar::SetFlagByName(\"d3d12_dlss_mode\", \"off\");"));
  CHECK(!Has(settings, "mua_dlss_probe") && !Has(settings, "ApplyDlssProbe"));
  const std::string app = ReadFile(kRoot + "src/marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "const rex::PathConfig routed = RoutedPaths(defaults, selected);\n      "
                 "mua::ApplyDlss(routed.cache_root);"));
  const std::string launcher = ReadFile(kRoot + "src/launcher.cpp");
  CHECK(Has(launcher, "dlss_support_ = CheckDlssSupport("));
  CHECK(Has(launcher, "ui.Info(\"NVIDIA DLSS\", dlss_unavailable.c_str());"));

  const std::string cmake = ReadFile(kRoot + "CMakeLists.txt");
  CHECK(std::string(mua::kDlssBridgeFile) == "mua_dlss_bridge.dll");
  CHECK(Has(cmake, "add_library(mua_dlss_bridge SHARED src/dlss_bridge/dlss_bridge.cpp)"));
  CHECK(Has(cmake, "nvsdk_ngx_s$<$<CONFIG:Debug>:_dbg>.lib"));
  CHECK(Has(cmake, "MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>\""));
  CHECK(Has(cmake, "    src/dlss_support.cpp\n"));
}

void TestGames() {
  CHECK((mua::Game(mua::GameId::kMua1).upscalers & mua::UpscalerBit(mua::Upscaler::kDlss)) != 0);
  CHECK((mua::Game(mua::GameId::kMua1Gold).upscalers & mua::UpscalerBit(mua::Upscaler::kDlss)) != 0);
  CHECK((mua::Game(mua::GameId::kMua2).upscalers & mua::UpscalerBit(mua::Upscaler::kDlss)) != 0);
  CHECK(mua::ParseUpscaler(mua::kUpscalerDlss, mua::Game(mua::GameId::kMua2).upscalers) ==
        (mua::kUpscalerAvailable ? mua::Upscaler::kDlss : mua::Upscaler::kOff));
}

void TestSdkSource() {
  const std::string script = ReadFile(kRoot + "tools/setup-dlss-sdk.ps1");
  CHECK(Has(script, "$commit = '9981c6350dc58c0ce21f024fd65e31e851c31689'"));
  CHECK(Has(script, "\"https://raw.githubusercontent.com/NVIDIA/DLSS/$commit/$relative\""));
  CHECK(Has(script, "'lib/Windows_x86_64/rel/nvngx_dlss.dll' = '"));
  // The watermarked development DLL must never ship.
  CHECK(!Has(script, "Windows_x86_64/dev/"));
  const std::regex entry("\\n    '([^']+)' = '([0-9A-F]+)'");
  int entries = 0;
  for (auto it = std::sregex_iterator(script.begin(), script.end(), entry); it != std::sregex_iterator(); ++it) {
    ++entries;
    CHECK((*it)[2].length() == 64);
  }
  CHECK(entries == 10);
  const std::string build = ReadFile(kRoot + "tools/build.ps1");
  CHECK(Has(build, "$DlssReady = (& (Join-Path $PSScriptRoot 'setup-dlss-sdk.ps1')) -eq $true"));
  CHECK(Has(ReadFile(kRoot + ".gitignore"), "\n/tools/nvidia-dlss/\n"));
}

void TestNotReleased() {
  const std::string package = ReadFile(kRoot + "tools/package-release.ps1");
  CHECK(!package.empty());
  CHECK(!Has(package, "nvngx_dlss") && !Has(package, "mua_dlss_bridge"));
}

void TestNoDiagnostics() {
  for (const auto& entry : std::filesystem::directory_iterator(kPatches)) {
    const std::string name = entry.path().filename().string();
    CHECK(name.find("debug-saulo") == std::string::npos);
    if (entry.path().extension() != ".patch") {
      continue;
    }
    const std::string patch = ReadFile(entry.path().string());
    CHECK(!Has(patch, "DebugNoteEvent") && !Has(patch, "debug_readback_") && !Has(patch, "scene_debug_depth_probe") &&
          !Has(patch, "DEBUG_SAULO"));
  }
}

#if defined(_WIN32) && defined(MUA_DLSS_BRIDGE_DLL)
void TestBuiltBridge() {
  const std::filesystem::path dll = std::filesystem::path(MUA_DLSS_BRIDGE_DLL).make_preferred();
  HMODULE module = LoadLibraryW(dll.c_str());
  CHECK(module != nullptr);
  if (!module) {
    return;
  }
  const auto get_api = reinterpret_cast<RexDlssBridgeGetApiFunction>(
      reinterpret_cast<void*>(GetProcAddress(module, REX_DLSS_BRIDGE_ENTRY_POINT)));
  CHECK(get_api != nullptr);
  if (get_api) {
    CHECK(get_api(REX_DLSS_BRIDGE_ABI_VERSION + 1) == nullptr);
    const RexDlssBridgeApi* api = get_api(REX_DLSS_BRIDGE_ABI_VERSION);
    CHECK(api && api->abi_version == REX_DLSS_BRIDGE_ABI_VERSION);
    if (api) {
      RexDlssCapabilities capabilities = {};
      CHECK(api->init(nullptr, &capabilities) == REX_DLSS_INVALID_ARGUMENT);
      CHECK(std::string(api->last_error()).find("init") != std::string::npos);
      // Each thread keeps its own last error: features are evaluated on the thread recording the command list.
      std::string other_thread_error;
      std::thread([&] {
        RexDlssFeature* feature = nullptr;
        api->create_feature(nullptr, nullptr, &feature);
        other_thread_error = api->last_error();
      }).join();
      CHECK(other_thread_error.find("create_feature") != std::string::npos);
      CHECK(std::string(api->last_error()).find("init") != std::string::npos);
      RexDlssInitDesc wrong_size = {};
      wrong_size.struct_size = sizeof(wrong_size) + 4;
      CHECK(api->init(&wrong_size, &capabilities) == REX_DLSS_INVALID_ARGUMENT);
      RexDlssOptimalSettings settings = {};
      CHECK(api->query_optimal_settings(2560, 1440, REX_DLSS_QUALITY_QUALITY, &settings) ==
            REX_DLSS_NOT_INITIALIZED);
      CHECK(api->query_optimal_settings(2560, 1440, REX_DLSS_QUALITY_COUNT, &settings) ==
            REX_DLSS_INVALID_ARGUMENT);
      RexDlssFeature* feature = nullptr;
      CHECK(api->create_feature(nullptr, nullptr, &feature) == REX_DLSS_INVALID_ARGUMENT && !feature);
      CHECK(api->evaluate(nullptr, nullptr, nullptr) == REX_DLSS_INVALID_ARGUMENT);
      CHECK(api->query_adapter(nullptr, L".", nullptr) == REX_DLSS_INVALID_ARGUMENT);
      api->release_feature(nullptr);
      api->shutdown();
    }
  }
  CHECK(std::filesystem::is_regular_file(dll.parent_path() / "nvngx_dlss.dll"));
  FreeLibrary(module);
}
#endif

}  // namespace

int main() {
  TestInterfaceCopy();
  TestBridgeLoader();
  TestSceneUpscaler();
  TestPreSceneTargets();
  TestDepthCapture();
  TestNativePreScene();
  TestApp();
  TestGames();
  TestSdkSource();
  TestNotReleased();
  TestNoDiagnostics();
#if defined(_WIN32) && defined(MUA_DLSS_BRIDGE_DLL)
  TestBuiltBridge();
#else
  std::printf("  (mua_dlss_bridge.dll not built: interface calls skipped)\n");
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
