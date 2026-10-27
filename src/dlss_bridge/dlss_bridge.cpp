// NVIDIA DLSS Super Resolution through NGX, behind the C interface of rex_dlss_bridge.h.

#include "rex_dlss_bridge.h"

#include <windows.h>

#include <d3d12.h>
#include <dxgi.h>

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#ifndef MUA_DLSS_ENGINE_VERSION
#define MUA_DLSS_ENGINE_VERSION "0"
#endif

struct RexDlssFeature {
  NVSDK_NGX_Handle* handle = nullptr;
  RexDlssFeatureDesc desc{};
};

namespace {

// NGX identifies a title without an NVIDIA application ID by a GUID of its own.
constexpr char kProjectId[] = "ac790753-fa69-481d-a735-a5bd9b99bb26";

struct BridgeState {
  ID3D12Device* device = nullptr;
  NVSDK_NGX_Parameter* parameters = nullptr;
  RexDlssLogFunction log = nullptr;
  void* log_user = nullptr;
};

BridgeState g_state;
// NGX isn't thread-safe, and features are created and evaluated on whichever thread records the command list.
std::mutex g_mutex;
thread_local std::string g_last_error;

void Log(RexDlssLogLevel level, const char* message) {
  if (g_state.log) {
    g_state.log(g_state.log_user, level, message);
  }
}

void NgxLog(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature) {
  std::string line = message ? message : "";
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  Log(REX_DLSS_LOG_INFO, ("NGX: " + line).c_str());
}

std::string Narrow(const wchar_t* text) {
  if (!text || !*text) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  std::string narrow(size > 0 ? size_t(size) : 0, '\0');
  if (size > 0) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), size, nullptr, nullptr);
    narrow.pop_back();
  }
  return narrow;
}

std::string ResultText(NVSDK_NGX_Result result) {
  char code[16];
  std::snprintf(code, sizeof(code), "0x%08X", unsigned(result));
  const std::string name = Narrow(GetNGXResultAsString(result));
  return name.empty() ? code : name + " (" + code + ")";
}

RexDlssStatus Fail(RexDlssStatus status, const std::string& message) {
  g_last_error = message;
  return status;
}

RexDlssStatus FailNgx(RexDlssStatus status, const char* call, NVSDK_NGX_Result result) {
  return Fail(status, std::string(call) + " failed: " + ResultText(result));
}

std::wstring BridgeDirectory() {
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&BridgeDirectory), &module)) {
    return {};
  }
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(module, path.data(), DWORD(path.size()));
    if (!length) {
      return {};
    }
    if (length < path.size()) {
      path.resize(length);
      break;
    }
    path.resize(path.size() * 2);
  }
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

void ReadRuntimeVersion(const std::wstring& directory, char (&version)[32]) {
  version[0] = '\0';
  const std::wstring dll = directory + L"\\nvngx_dlss.dll";
  DWORD handle = 0;
  const DWORD size = GetFileVersionInfoSizeW(dll.c_str(), &handle);
  if (!size) {
    return;
  }
  std::string data(size, '\0');
  VS_FIXEDFILEINFO* info = nullptr;
  UINT info_size = 0;
  if (!GetFileVersionInfoW(dll.c_str(), 0, size, data.data()) ||
      !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &info_size) || !info) {
    return;
  }
  std::snprintf(version, sizeof(version), "%u.%u.%u.%u", unsigned(HIWORD(info->dwFileVersionMS)),
                unsigned(LOWORD(info->dwFileVersionMS)), unsigned(HIWORD(info->dwFileVersionLS)),
                unsigned(LOWORD(info->dwFileVersionLS)));
}

NVSDK_NGX_PerfQuality_Value PerfQuality(RexDlssQuality quality) {
  switch (quality) {
    case REX_DLSS_QUALITY_DLAA:
      return NVSDK_NGX_PerfQuality_Value_DLAA;
    case REX_DLSS_QUALITY_QUALITY:
      return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case REX_DLSS_QUALITY_BALANCED:
      return NVSDK_NGX_PerfQuality_Value_Balanced;
    case REX_DLSS_QUALITY_PERFORMANCE:
      return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    default:
      return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
  }
}

constexpr std::array<std::pair<RexDlssQuality, const char*>, 5> kPresetHints = {{
    {REX_DLSS_QUALITY_DLAA, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA},
    {REX_DLSS_QUALITY_QUALITY, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality},
    {REX_DLSS_QUALITY_BALANCED, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced},
    {REX_DLSS_QUALITY_PERFORMANCE, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance},
    {REX_DLSS_QUALITY_ULTRA_PERFORMANCE, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance},
}};

int FeatureFlags(uint32_t flags) {
  int ngx = NVSDK_NGX_DLSS_Feature_Flags_None;
  if (flags & REX_DLSS_FEATURE_HDR) ngx |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
  if (flags & REX_DLSS_FEATURE_MV_LOW_RES) ngx |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
  if (flags & REX_DLSS_FEATURE_MV_JITTERED) ngx |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
  if (flags & REX_DLSS_FEATURE_DEPTH_INVERTED) ngx |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
  if (flags & REX_DLSS_FEATURE_AUTO_EXPOSURE) ngx |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
  return ngx;
}

void Shutdown() {
  if (!g_state.device) {
    return;
  }
  if (g_state.parameters) {
    NVSDK_NGX_D3D12_DestroyParameters(g_state.parameters);
  }
  NVSDK_NGX_D3D12_Shutdown1(g_state.device);
  g_state.device = nullptr;
  g_state.parameters = nullptr;
}

RexDlssStatus QueryAdapter(IDXGIAdapter* adapter, const wchar_t* data_path, RexDlssAdapterRequirements* requirements) {
  if (!adapter || !data_path || !requirements) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "query_adapter: missing adapter, data path or result");
  }
  *requirements = {};
  const std::wstring directory = BridgeDirectory();
  const wchar_t* paths[] = {directory.c_str()};
  NVSDK_NGX_FeatureCommonInfo common = {};
  common.PathListInfo.Path = paths;
  common.PathListInfo.Length = directory.empty() ? 0 : 1;
  NVSDK_NGX_FeatureDiscoveryInfo discovery = {};
  discovery.SDKVersion = NVSDK_NGX_Version_API;
  discovery.FeatureID = NVSDK_NGX_Feature_SuperSampling;
  discovery.Identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
  discovery.Identifier.v.ProjectDesc.ProjectId = kProjectId;
  discovery.Identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
  discovery.Identifier.v.ProjectDesc.EngineVersion = MUA_DLSS_ENGINE_VERSION;
  discovery.ApplicationDataPath = data_path;
  discovery.FeatureInfo = &common;
  NVSDK_NGX_FeatureRequirement requirement = {};
  const NVSDK_NGX_Result result = NVSDK_NGX_D3D12_GetFeatureRequirements(adapter, &discovery, &requirement);
  if (NVSDK_NGX_FAILED(result)) {
    return FailNgx(REX_DLSS_UNAVAILABLE, "NVSDK_NGX_D3D12_GetFeatureRequirements", result);
  }
  requirements->unsupported_reasons = uint32_t(requirement.FeatureSupported);
  requirements->supported = requirement.FeatureSupported == NVSDK_NGX_FeatureSupportResult_Supported;
  requirements->min_hw_architecture = requirement.MinHWArchitecture;
  std::snprintf(requirements->min_os_version, sizeof(requirements->min_os_version), "%s", requirement.MinOSVersion);
  return REX_DLSS_OK;
}

RexDlssStatus Init(const RexDlssInitDesc* desc, RexDlssCapabilities* capabilities) {
  if (!desc || desc->struct_size != sizeof(RexDlssInitDesc) || !desc->device || !desc->data_path || !capabilities) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "init: missing device, data path or result, or a different interface");
  }
  if (g_state.device) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "init: already initialized");
  }
  *capabilities = {};
  g_state.log = desc->log;
  g_state.log_user = desc->log_user;

  const std::wstring directory = BridgeDirectory();
  ReadRuntimeVersion(directory, capabilities->runtime_version);
  const wchar_t* paths[] = {directory.c_str()};
  NVSDK_NGX_FeatureCommonInfo common = {};
  common.PathListInfo.Path = paths;
  common.PathListInfo.Length = directory.empty() ? 0 : 1;
  if (desc->log && desc->ngx_log_level > 0) {
    common.LoggingInfo.LoggingCallback = NgxLog;
    common.LoggingInfo.MinimumLoggingLevel =
        desc->ngx_log_level > 1 ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_ON;
  }
  const NVSDK_NGX_Result init_result = NVSDK_NGX_D3D12_Init_with_ProjectID(
      kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, desc->engine_version ? desc->engine_version : MUA_DLSS_ENGINE_VERSION,
      desc->data_path, desc->device, &common, NVSDK_NGX_Version_API);
  if (NVSDK_NGX_FAILED(init_result)) {
    return FailNgx(REX_DLSS_INIT_FAILED, "NVSDK_NGX_D3D12_Init_with_ProjectID", init_result);
  }
  g_state.device = desc->device;

  const NVSDK_NGX_Result parameters_result = NVSDK_NGX_D3D12_GetCapabilityParameters(&g_state.parameters);
  if (NVSDK_NGX_FAILED(parameters_result) || !g_state.parameters) {
    g_state.parameters = nullptr;
    Shutdown();
    return FailNgx(REX_DLSS_INIT_FAILED, "NVSDK_NGX_D3D12_GetCapabilityParameters", parameters_result);
  }
  int available = 0;
  int needs_driver_update = 0;
  unsigned int min_driver_major = 0;
  unsigned int min_driver_minor = 0;
  int feature_init_result = 0;
  NVSDK_NGX_Parameter_GetI(g_state.parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
  NVSDK_NGX_Parameter_GetI(g_state.parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver,
                           &needs_driver_update);
  NVSDK_NGX_Parameter_GetUI(g_state.parameters, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor,
                            &min_driver_major);
  NVSDK_NGX_Parameter_GetUI(g_state.parameters, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor,
                            &min_driver_minor);
  NVSDK_NGX_Parameter_GetI(g_state.parameters, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult,
                           &feature_init_result);
  capabilities->available = available != 0;
  capabilities->needs_driver_update = needs_driver_update != 0;
  capabilities->min_driver_major = min_driver_major;
  capabilities->min_driver_minor = min_driver_minor;
  capabilities->feature_init_result = feature_init_result;
  if (capabilities->needs_driver_update) {
    Shutdown();
    char message[128];
    std::snprintf(message, sizeof(message), "DLSS needs NVIDIA driver %u.%u or newer", min_driver_major,
                  min_driver_minor);
    return Fail(REX_DLSS_DRIVER_UPDATE_REQUIRED, message);
  }
  if (!capabilities->available) {
    Shutdown();
    return Fail(REX_DLSS_UNAVAILABLE, "DLSS Super Resolution is not available: " +
                                          ResultText(NVSDK_NGX_Result(feature_init_result)));
  }
  return REX_DLSS_OK;
}

RexDlssStatus QueryOptimalSettings(uint32_t output_width, uint32_t output_height, RexDlssQuality quality,
                                   RexDlssOptimalSettings* settings) {
  if (!settings || !output_width || !output_height || quality >= REX_DLSS_QUALITY_COUNT) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "query_optimal_settings: missing size, quality or result");
  }
  *settings = {};
  if (!g_state.parameters) {
    return Fail(REX_DLSS_NOT_INITIALIZED, "query_optimal_settings: not initialized");
  }
  unsigned int width = 0, height = 0, max_width = 0, max_height = 0, min_width = 0, min_height = 0;
  float sharpness = 0.0f;
  const NVSDK_NGX_Result result =
      NGX_DLSS_GET_OPTIMAL_SETTINGS(g_state.parameters, output_width, output_height, PerfQuality(quality), &width,
                                    &height, &max_width, &max_height, &min_width, &min_height, &sharpness);
  if (NVSDK_NGX_FAILED(result)) {
    return FailNgx(REX_DLSS_NGX_ERROR, "NGX_DLSS_GET_OPTIMAL_SETTINGS", result);
  }
  if (!width || !height) {
    return Fail(REX_DLSS_MODE_UNAVAILABLE, "query_optimal_settings: DLSS offers no render size for this mode");
  }
  *settings = {width, height, min_width, min_height, max_width, max_height};
  return REX_DLSS_OK;
}

RexDlssStatus CreateFeature(ID3D12GraphicsCommandList* command_list, const RexDlssFeatureDesc* desc,
                            RexDlssFeature** feature) {
  if (!command_list || !desc || desc->struct_size != sizeof(RexDlssFeatureDesc) || !feature ||
      desc->quality >= REX_DLSS_QUALITY_COUNT || !desc->render_width || !desc->render_height ||
      desc->output_width < 64 || desc->output_height < 32) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "create_feature: missing command list or result, or bad sizes");
  }
  *feature = nullptr;
  if (!g_state.parameters) {
    return Fail(REX_DLSS_NOT_INITIALIZED, "create_feature: not initialized");
  }
  // The parameter map outlives each feature, so every preset hint is set again.
  for (const auto& [quality, hint] : kPresetHints) {
    NVSDK_NGX_Parameter_SetUI(g_state.parameters, hint, quality == desc->quality ? desc->preset : 0);
  }
  NVSDK_NGX_DLSS_Create_Params create = {};
  create.Feature.InWidth = desc->render_width;
  create.Feature.InHeight = desc->render_height;
  create.Feature.InTargetWidth = desc->output_width;
  create.Feature.InTargetHeight = desc->output_height;
  create.Feature.InPerfQualityValue = PerfQuality(desc->quality);
  create.InFeatureCreateFlags = FeatureFlags(desc->flags);
  NVSDK_NGX_Handle* handle = nullptr;
  const NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(command_list, 1, 1, &handle, g_state.parameters, &create);
  if (NVSDK_NGX_FAILED(result) || !handle) {
    return FailNgx(REX_DLSS_NGX_ERROR, "NGX_D3D12_CREATE_DLSS_EXT", result);
  }
  *feature = new RexDlssFeature{handle, *desc};
  return REX_DLSS_OK;
}

RexDlssStatus Evaluate(ID3D12GraphicsCommandList* command_list, RexDlssFeature* feature,
                       const RexDlssEvaluateDesc* desc) {
  if (!command_list || !feature || !desc || desc->struct_size != sizeof(RexDlssEvaluateDesc) || !desc->color ||
      !desc->depth || !desc->motion_vectors || !desc->output || !desc->render_width || !desc->render_height ||
      desc->mv_scale_x == 0.0f || desc->mv_scale_y == 0.0f) {
    return Fail(REX_DLSS_INVALID_ARGUMENT,
                "evaluate: color, depth, motion vectors, output, a render size and motion vector scales are needed");
  }
  if (!g_state.parameters) {
    return Fail(REX_DLSS_NOT_INITIALIZED, "evaluate: not initialized");
  }
  if (desc->render_width > feature->desc.render_width || desc->render_height > feature->desc.render_height) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "evaluate: the render size is larger than the feature was created for");
  }
  if (!desc->exposure && !(feature->desc.flags & REX_DLSS_FEATURE_AUTO_EXPOSURE) &&
      (feature->desc.flags & REX_DLSS_FEATURE_HDR)) {
    return Fail(REX_DLSS_INVALID_ARGUMENT, "evaluate: HDR input needs an exposure texture or auto exposure");
  }
  NVSDK_NGX_D3D12_DLSS_Eval_Params eval = {};
  eval.Feature.pInColor = desc->color;
  eval.Feature.pInOutput = desc->output;
  eval.pInDepth = desc->depth;
  eval.pInMotionVectors = desc->motion_vectors;
  eval.pInExposureTexture = desc->exposure;
  eval.InJitterOffsetX = desc->jitter_x;
  eval.InJitterOffsetY = desc->jitter_y;
  eval.InMVScaleX = desc->mv_scale_x;
  eval.InMVScaleY = desc->mv_scale_y;
  eval.InPreExposure = desc->pre_exposure;
  eval.InReset = desc->reset ? 1 : 0;
  eval.InRenderSubrectDimensions.Width = desc->render_width;
  eval.InRenderSubrectDimensions.Height = desc->render_height;
  eval.InColorSubrectBase = {desc->input_x, desc->input_y};
  eval.InDepthSubrectBase = {desc->input_x, desc->input_y};
  eval.InMVSubrectBase = {desc->input_x, desc->input_y};
  const NVSDK_NGX_Result result =
      NGX_D3D12_EVALUATE_DLSS_EXT(command_list, feature->handle, g_state.parameters, &eval);
  if (NVSDK_NGX_FAILED(result)) {
    return FailNgx(REX_DLSS_NGX_ERROR, "NGX_D3D12_EVALUATE_DLSS_EXT", result);
  }
  return REX_DLSS_OK;
}

void ReleaseFeature(RexDlssFeature* feature) {
  if (!feature) {
    return;
  }
  if (feature->handle && g_state.device) {
    const NVSDK_NGX_Result result = NVSDK_NGX_D3D12_ReleaseFeature(feature->handle);
    if (NVSDK_NGX_FAILED(result)) {
      FailNgx(REX_DLSS_NGX_ERROR, "NVSDK_NGX_D3D12_ReleaseFeature", result);
      Log(REX_DLSS_LOG_WARNING, g_last_error.c_str());
    }
  }
  delete feature;
}

const char* LastError() { return g_last_error.c_str(); }

template <auto Function>
struct Locked;
template <typename Result, typename... Args, Result (*Function)(Args...)>
struct Locked<Function> {
  static Result Call(Args... args) {
    std::lock_guard lock(g_mutex);
    return Function(args...);
  }
};

constexpr RexDlssBridgeApi kApi = {
    .abi_version = REX_DLSS_BRIDGE_ABI_VERSION,
    .query_adapter = Locked<QueryAdapter>::Call,
    .init = Locked<Init>::Call,
    .query_optimal_settings = Locked<QueryOptimalSettings>::Call,
    .create_feature = Locked<CreateFeature>::Call,
    .evaluate = Locked<Evaluate>::Call,
    .release_feature = Locked<ReleaseFeature>::Call,
    .shutdown = Locked<Shutdown>::Call,
    .last_error = LastError,
};

}  // namespace

extern "C" __declspec(dllexport) const RexDlssBridgeApi* RexDlssBridgeGetApi(uint32_t abi_version) {
  return abi_version == REX_DLSS_BRIDGE_ABI_VERSION ? &kApi : nullptr;
}
