// AMD FSR 3.1: patches/rexglue-sdk/0165 adds it to the scene upscaler beside DLSS with AMD's signed
// amd_fidelityfx_dx12.dll and its unmodified ffx_api headers (FidelityFX SDK v1.1.4), and feeds it the scene upscaler's
// own color, depth, camera motion vectors and jitter in AMD's conventions; the app sets its mode only for the FSR 3.1
// choice, MUA1 and its Gold Edition offer it, the DLL comes only from AMD's pinned commit and ships with its license,
// and no frame generation is involved.
// Run: mua_fsr3_tests

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>

#include "fsr3_support.h"
#include "game_definition.h"
#include "sha256.h"
#include "update_bundle.h"
#include "upscaling.h"

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
  std::erase(content, '\r');
  return content;
}

bool Has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

const std::string kRoot = std::string(MUA_SRC_DIR) + "/../";
constexpr char kPatch[] = "patches/rexglue-sdk/0165-d3d12-fsr3-scene-upscaler.windows.patch";

// The part of a patch that changes one file, from its "diff --git" line to the next.
std::string FileDiff(const std::string& patch, const std::string& path) {
  const size_t begin = patch.find("diff --git a/" + path + " b/" + path + "\n");
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = patch.find("\ndiff --git ", begin + 1);
  return patch.substr(begin, end == std::string::npos ? std::string::npos : end + 1 - begin);
}

// The content of a file a patch adds.
std::string NewFile(const std::string& patch, const std::string& path) {
  const std::string diff = FileDiff(patch, path);
  const size_t hunk = diff.find("\n@@ ");
  if (!Has(diff, "\nnew file mode ") || hunk == std::string::npos) {
    return "";
  }
  std::string content;
  std::istringstream lines(diff.substr(diff.find('\n', hunk + 1) + 1));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] == '+') {
      content += line.substr(1) + "\n";
    }
  }
  return content;
}

// Lines a patch adds to a file.
std::string Added(const std::string& diff) {
  std::string added;
  std::istringstream lines(diff);
  for (std::string line; std::getline(lines, line);) {
    if (line.size() > 1 && line[0] == '+' && line.rfind("+++", 0) != 0) {
      added += line.substr(1) + "\n";
    }
  }
  return added;
}

std::string Sha256Hex(const std::string& data) {
  mua::Sha256 hash;
  hash.Update(data.data(), data.size());
  std::string hex;
  for (const uint8_t byte : hash.Finish()) {
    constexpr char kDigits[] = "0123456789abcdef";
    hex += kDigits[byte >> 4];
    hex += kDigits[byte & 15];
  }
  return hex;
}

void TestAmdHeaders(const std::string& patch) {
  std::printf("the patch carries FidelityFX SDK v1.1.4's ffx_api headers and license unmodified\n");
  constexpr std::pair<const char*, const char*> kFiles[] = {
      {"thirdparty/fidelityfx-api/include/ffx_api/ffx_api.h",
       "408b4203545e8299ed188fbb1d9ddfe0932b73ea6e8b14fe10edcf1f45771561"},
      {"thirdparty/fidelityfx-api/include/ffx_api/ffx_api_types.h",
       "dee2782813d22047c068e0fb27d1347324f8ce79d280a4672848b3b294cb12fe"},
      {"thirdparty/fidelityfx-api/include/ffx_api/ffx_upscale.h",
       "1155bcdcea9c10e6a95507bc9422ca062a853ac84250f0b09a637a66836b3ac1"},
      {"thirdparty/fidelityfx-api/include/ffx_api/dx12/ffx_api_dx12.h",
       "215804fba316acb81d2c463b6bbce36c53cc63465c15b8c9858554c8111fae24"},
      {"thirdparty/fidelityfx-api/LICENSE.txt", "82cf74fc23885107c7f69249ac3f1dac55898ff68320eb2c753984cae6b2d78a"},
  };
  for (const auto& [path, sha256] : kFiles) {
    const std::string content = NewFile(patch, path);
    CHECK(!content.empty());
    CHECK(Sha256Hex(content) == sha256);
  }
  // Only the upscaler's headers: no frame generation, no Vulkan, no SDK sources or samples.
  std::istringstream lines(patch);
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind("diff --git a/thirdparty/", 0) == 0) {
      CHECK(Has(line, "thirdparty/fidelityfx-api/"));
      CHECK(!Has(line, "framegeneration") && !Has(line, "/vk/") && !Has(line, ".cpp"));
    }
  }
}

void TestBackend(const std::string& patch) {
  std::printf("the GPU plugin loads AMD's DLL lazily and dispatches FSR 3.1 with AMD's conventions\n");
  const std::string runtime = NewFile(patch, "src/graphics/d3d12/fsr3_upscaler.cpp");
  CHECK(!runtime.empty());
  CHECK(Has(runtime, "LoadLibraryExW(dll.c_str()"));
  CHECK(Has(runtime, "\"ffxCreateContext\"") && Has(runtime, "\"ffxDestroyContext\"") && Has(runtime, "\"ffxQuery\"") &&
        Has(runtime, "\"ffxDispatch\""));
  CHECK(Has(runtime, "versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;"));
  CHECK(Has(runtime, "backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;"));
  // Depth inversion is the only creation flag: LDR color without auto exposure, render-size motion vectors without
  // jitter, a fixed render size.
  CHECK(Has(runtime, "upscale.flags = desc.depth_inverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0u;"));
  for (const char* flag : {"FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE", "FFX_UPSCALE_ENABLE_AUTO_EXPOSURE",
                           "FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION",
                           "FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS", "FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION",
                           "FFX_UPSCALE_ENABLE_DEPTH_INFINITE"}) {
    CHECK(!Has(runtime, flag));
  }
  CHECK(Has(runtime, "desc.color = ffxApiGetResourceDX12(frame.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);"));
  CHECK(Has(runtime, "desc.depth = ffxApiGetResourceDX12(frame.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);"));
  CHECK(Has(runtime, "desc.motionVectors = ffxApiGetResourceDX12(frame.motion, FFX_API_RESOURCE_STATE_COMPUTE_READ);"));
  CHECK(Has(runtime, "desc.output = ffxApiGetResourceDX12(frame.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);"));
  CHECK(Has(runtime, "desc.jitterOffset = {frame.jitter_x, frame.jitter_y};"));
  CHECK(Has(runtime, "desc.motionVectorScale = {1.0f, 1.0f};"));
  CHECK(Has(runtime, "desc.preExposure = 1.0f;"));
  CHECK(Has(runtime, "desc.enableSharpening = frame.sharpness > 0.0f;"));
  CHECK(!Has(runtime, "desc.reactive =") && !Has(runtime, "desc.transparencyAndComposition =") &&
        !Has(runtime, "desc.exposure ="));
  // SDR: no dispatch flags (no sRGB or PQ input, which the 3.1.4 provider ignores anyway), no HDR, no auto exposure.
  CHECK(!Has(runtime, "desc.flags") && !Has(runtime, "NON_LINEAR"));
  // The context is made on the command list's thread when asked or missing, and failures stop FSR 3.1.
  CHECK(Has(runtime, "if ((args.create || !context_) && !CreateContext(args.create_desc)) {"));
  CHECK(Has(runtime, "failed_.store(true, std::memory_order_relaxed);"));

  const std::string upscaler = Added(FileDiff(patch, "src/graphics/d3d12/scene_upscaler.cpp"));
  CHECK(Has(upscaler, "REXCVAR_DEFINE_STRING(d3d12_fsr3_mode, \"off\", \"GPU/D3D12\","));
  CHECK(Has(upscaler, ".allowed({\"off\", \"native\", \"quality\", \"balanced\", \"performance\", \"ultra_performance\"})"));
  CHECK(Has(upscaler, "REXCVAR_DEFINE_STRING(d3d12_fsr3_library, \"\", \"GPU/D3D12\","));
  CHECK(Has(upscaler, "REXCVAR_DEFINE_INT32(d3d12_fsr3_sharpness, 80, \"GPU/D3D12\","));
  // DLSS keeps precedence, and a failure turns FSR 3.1 off for the session instead of switching upscaler.
  CHECK(Has(upscaler, "backend_ = quality ? Backend::kDlss : Backend::kFsr3;"));
  CHECK(Has(upscaler, "DisableForSession(\"AMD FSR 3.1 is unavailable\");"));
  CHECK(Has(upscaler, "{0}: off for this session, {1} - the scene renders at full resolution without {0}"));
  CHECK(Has(upscaler, "DisableForSession(std::string(Tag()) + \" stopped working\");"));
  // AMD's render size and phase count, from the DLL with the same formulas as fallbacks; one shared jitter sequence.
  CHECK(Has(upscaler, "fsr3_->QueryRenderSize(output_width_, output_height_, fsr3_quality_);"));
  CHECK(Has(upscaler, ".value_or(scene::Fsr3JitterPhaseCount(render_width_, output_width_));"));
  CHECK(Has(upscaler, "frame.jitter_x = jitter[0];") && Has(upscaler, "frame.jitter_y = jitter[1];"));
  CHECK(Has(upscaler, "frame.camera_far = std::isfinite(camera.far_depth) ? float(camera.far_depth) : FLT_MAX;"));
  // Recreated only when the depth convention changes, after the GPU is done with the old context.
  CHECK(Has(upscaler, "const bool recreate = fsr3_recreate_ || fsr3_depth_inverted_ != depth_inverted_;"));
  CHECK(Has(upscaler, "if (recreate && fsr3_context_requested_) {\n"
                      "    // The previous context may still be in use by submitted command lists.\n"
                      "    command_processor_.AwaitAllQueueOperationsCompletion();"));
  CHECK(Has(upscaler, "ExternalCall(Fsr3Upscaler::RecordOnCommandList, fsr3_.get(), &args,"));
  CHECK(Has(upscaler, "    fsr3_->DestroyContext();\n"));
  // DLSS's evaluation is the same code, moved.
  CHECK(Has(upscaler, "args.feature_desc.flags = REX_DLSS_FEATURE_MV_LOW_RES | (depth_inverted_ ? "
                      "REX_DLSS_FEATURE_DEPTH_INVERTED : 0u);"));
  CHECK(Has(upscaler, "evaluate.mv_scale_x = 1.0f;"));

  // FSR 3.1 writes alpha 1; its write-back always takes the scene's own alpha, at the unjittered point, and DLSS's
  // write-back is unchanged.
  CHECK(Has(upscaler, "EncodeStaging(backend_ == Backend::kFsr3 ? Encode::kOutputSceneAlpha : Encode::kOutput, "
                      "nullptr);"));
  CHECK(!Has(patch, "d3d12_fsr3_scene_alpha"));
  CHECK(Has(upscaler, "{color_source, scene_alpha ? color_ : depth_},"));
  CHECK(Has(upscaler, "scene_alpha ? kAlphaAsFirstComponent : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);"));
  CHECK(Has(upscaler, "D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3, D3D12_SHADER_COMPONENT_MAPPING_FROM_"
                      "MEMORY_COMPONENT_1,"));
  CHECK(Has(upscaler, "  float jitter[2];\n"));
  const std::string encode = Added(FileDiff(patch, "src/graphics/shaders/scene_encode.cs.hlsl"));
  CHECK(Has(encode, "  float2 jitter;\n"));
  CHECK(Has(encode, "float2 source_position = (float2(position) + 0.5f) * source_scale + jitter;"));
  CHECK(Has(encode, "packed = PackColor(float4(color[position].rgb, alpha));"));
  // The bytecode is the shader's: the constants' layout matches EncodeConstants.
  const std::string bytecode = Added(FileDiff(patch, "src/graphics/shaders/bytecode/d3d12_5_1/scene_encode_cs.h"));
  CHECK(Has(bytecode, "//   float2 jitter;                     // Offset:   68 Size:     8"));

  const std::string cmake = Added(FileDiff(patch, "src/graphics/CMakeLists.txt"));
  CHECK(Has(cmake, "d3d12/fsr3_upscaler.cpp"));
  CHECK(Has(cmake, "${REXGLUE_ROOT}/thirdparty/fidelityfx-api/include"));
}

void TestNoFrameGeneration(const std::string& patch) {
  std::printf("no frame generation: no swap chain, no interpolation, no frame generation descriptors\n");
  for (const char* path : {"src/graphics/d3d12/fsr3_upscaler.cpp", "src/graphics/d3d12/scene_upscaler.cpp",
                           "include/rex/graphics/d3d12/fsr3_upscaler.h", "include/rex/graphics/d3d12/scene_upscaler.h"}) {
    const std::string added = Added(FileDiff(patch, path));
    CHECK(!added.empty());
    for (const char* word : {"FrameGeneration", "FRAMEGENERATION", "framegeneration", "SwapChain", "Interpolat",
                             "OpticalFlow", "DEBUG_SAULO"}) {
      CHECK(!Has(added, word));
    }
  }
  CHECK(!Has(patch, "ffx_framegeneration.h\n"));
}

void TestApp() {
  std::printf("the app sets FSR 3.1 only for its choice, from AMD's DLL beside the program\n");
  const std::string settings = ReadFile(kRoot + "src/settings.cpp");
  CHECK(Has(settings, "void ApplyFsr3() {\n  const bool selected = ActiveUpscaler() == Upscaler::kFsr3;"));
  CHECK(Has(settings, "  if (!selected) {\n    rex::cvar::SetFlagByName(\"d3d12_fsr3_mode\", \"off\");"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_fsr3_library\", Utf8(library));"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_fsr3_mode\", Fsr3ModeFor(quality));"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(\"d3d12_fsr3_sharpness\", std::to_string(sharpness));"));
  CHECK(Has(settings, "REXCVAR_DEFINE_INT32(mua_fsr3_sharpness, mua::kDefaultFsr3Sharpness,"));
  CHECK(Has(settings, "\"mua_fsr3_sharpness\"};"));
  const std::string app = ReadFile(kRoot + "src/marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "mua::ApplyDlss(routed.cache_root);\n      mua::ApplyFsr3();"));
  const std::string launcher = ReadFile(kRoot + "src/launcher.cpp");
  CHECK(Has(launcher, "fsr3_support_ = Fsr3SupportFor("));
  CHECK(Has(launcher, "ui.Info(\"AMD FSR 3.1\", fsr3_unavailable.c_str());"));
  CHECK(Has(launcher, "\"mua_fsr3_sharpness\", kDefaultFsr3Sharpness, 0, 100);"));
  CHECK(Has(launcher, "Works on NVIDIA, AMD and Intel graphics"));
  CHECK(!Has(launcher, "Frame Generation") && !Has(launcher, "frame generation"));
}

void TestGames() {
  std::printf("all three games offer FSR 3.1\n");
  for (const mua::GameId id : {mua::GameId::kMua1, mua::GameId::kMua1Gold, mua::GameId::kMua2}) {
    CHECK((mua::Game(id).upscalers & mua::UpscalerBit(mua::Upscaler::kFsr3)) != 0);
    CHECK(mua::ParseUpscaler(mua::kUpscalerFsr3, mua::Game(id).upscalers) ==
          (mua::kUpscalerAvailable ? mua::Upscaler::kFsr3 : mua::Upscaler::kOff));
  }
}

void TestSource(const std::string& patch) {
  std::printf("the DLL comes only from AMD's pinned commit and ships with AMD's license\n");
  const std::string script = ReadFile(kRoot + "tools/setup-fidelityfx.ps1");
  CHECK(Has(script, "$version = '1.1.4'"));
  CHECK(Has(script, "$commit = 'c6efa6bf7f2027b3ec94f28578bb5965eabb9e55'"));
  CHECK(Has(script, "\"https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$commit/$relative\""));
  CHECK(Has(script, "'PrebuiltSignedDLL/amd_fidelityfx_dx12.dll' = "
                    "'12A5081257EC95B0B53AD51B4A87FB3C03F97FE0BBB59F9496968F8D50EF93A6'"));
  CHECK(Has(script, "'LICENSE.txt' = '82CF74FC23885107C7F69249AC3F1DAC55898FF68320EB2C753984CAE6B2D78A'"));
  CHECK(!Has(script, "latest") && !Has(script, "amd_fidelityfx_vk"));
  const std::string build = ReadFile(kRoot + "tools/build.ps1");
  CHECK(Has(build, "$Fsr3Ready = (& (Join-Path $PSScriptRoot 'setup-fidelityfx.ps1')) -eq $true"));
  CHECK(Has(build, "$Fsr3Files = if ($Fsr3Ready) { @('amd_fidelityfx_dx12.dll') } else { @() }"));
  CHECK(Has(ReadFile(kRoot + ".gitignore"), "\n/tools/amd-fidelityfx/\n"));
  // The headers 0165 adds are cleaned before the patches are applied again, like everything else they add.
  const std::string patched_roots = "'src', 'include', 'tests', 'cmake', 'thirdparty/fidelityfx-api'";
  CHECK(Has(ReadFile(kRoot + "tools/rexruntime-patch.ps1"), "$RuntimePatchedRoots = @(" + patched_roots + ")"));
  const std::string runtime_build = ReadFile(kRoot + "tools/build-patched-rexruntime.ps1");
  CHECK(Has(runtime_build, "git -C $src clean -fdq -- @RuntimePatchedRoots"));
  CHECK(Has(runtime_build, "ls-files --others --exclude-standard -- @RuntimePatchedRoots"));
  CHECK(!Has(runtime_build, "-- src include tests cmake"));
  const std::string linux_build = ReadFile(kRoot + "tools/build-patched-rexruntime.sh");
  CHECK(Has(linux_build, "PATCHED_ROOTS=(src include tests cmake thirdparty/fidelityfx-api)"));
  CHECK(!Has(linux_build, "-- src include tests cmake"));
  CHECK(Has(patch, "diff --git a/thirdparty/fidelityfx-api/"));
  std::istringstream added_files(patch);
  for (std::string line; std::getline(added_files, line);) {
    if (line.rfind("+++ b/", 0) == 0) {
      const std::string path = line.substr(6);
      CHECK(path.rfind("src/", 0) == 0 || path.rfind("include/", 0) == 0 || path.rfind("thirdparty/fidelityfx-api/", 0) == 0);
    }
  }
  const std::string cmake = ReadFile(kRoot + "CMakeLists.txt");
  CHECK(Has(cmake, "tools/amd-fidelityfx/1.1.4/PrebuiltSignedDLL/amd_fidelityfx_dx12.dll"));
  CHECK(Has(cmake, "copy_if_different \"${MUA_FIDELITYFX_DLL}\" \"${CMAKE_BINARY_DIR}\""));

  const std::string package = ReadFile(kRoot + "tools/package-release.ps1");
  CHECK(Has(package, "'amd_fidelityfx_dx12.dll'"));
  CHECK(std::find(mua::kBundleFiles.begin(), mua::kBundleFiles.end(), std::string_view(mua::kFidelityFxLibraryFile)) !=
        mua::kBundleFiles.end());
  const std::string licenses = ReadFile(kRoot + "packaging/licenses/THIRD-PARTY-LICENSES.txt");
  CHECK(Has(licenses, "AMD FidelityFX SDK (FSR 3.1)"));
  CHECK(Has(licenses, "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK (tag v1.1.4, commit c6efa6b)"));
  CHECK(Has(licenses, "Copyright (C) 2024 Advanced Micro Devices, Inc."));
}

}  // namespace

int main() {
  const std::string patch = ReadFile(kRoot + kPatch);
  CHECK(!patch.empty());
  TestAmdHeaders(patch);
  TestBackend(patch);
  TestNoFrameGeneration(patch);
  TestApp();
  TestGames();
  TestSource(patch);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
