#ifndef REX_DLSS_BRIDGE_H
#define REX_DLSS_BRIDGE_H

// The C interface between the ReXGlue Direct3D 12 GPU plugin and a DLL that runs NVIDIA DLSS Super Resolution, so
// the plugin needs neither the NVIDIA SDK nor the DLL to run.

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IDXGIAdapter IDXGIAdapter;
typedef struct ID3D12Device ID3D12Device;
typedef struct ID3D12GraphicsCommandList ID3D12GraphicsCommandList;
typedef struct ID3D12Resource ID3D12Resource;

#define REX_DLSS_BRIDGE_ABI_VERSION 1
#define REX_DLSS_BRIDGE_ENTRY_POINT "RexDlssBridgeGetApi"

typedef enum RexDlssStatus {
  REX_DLSS_OK = 0,
  REX_DLSS_INVALID_ARGUMENT,
  REX_DLSS_NOT_INITIALIZED,
  REX_DLSS_INIT_FAILED,
  REX_DLSS_UNAVAILABLE,
  REX_DLSS_DRIVER_UPDATE_REQUIRED,
  REX_DLSS_MODE_UNAVAILABLE,
  REX_DLSS_NGX_ERROR,
} RexDlssStatus;

typedef enum RexDlssQuality {
  REX_DLSS_QUALITY_DLAA = 0,
  REX_DLSS_QUALITY_QUALITY,
  REX_DLSS_QUALITY_BALANCED,
  REX_DLSS_QUALITY_PERFORMANCE,
  REX_DLSS_QUALITY_ULTRA_PERFORMANCE,
  REX_DLSS_QUALITY_COUNT,
} RexDlssQuality;

enum {
  REX_DLSS_FEATURE_HDR = 1u << 0,
  REX_DLSS_FEATURE_MV_LOW_RES = 1u << 1,
  REX_DLSS_FEATURE_MV_JITTERED = 1u << 2,
  REX_DLSS_FEATURE_DEPTH_INVERTED = 1u << 3,
  REX_DLSS_FEATURE_AUTO_EXPOSURE = 1u << 4,
};

typedef enum RexDlssLogLevel {
  REX_DLSS_LOG_INFO = 0,
  REX_DLSS_LOG_WARNING,
  REX_DLSS_LOG_ERROR,
} RexDlssLogLevel;

// May be called from NVIDIA's threads as well as the caller's.
typedef void (*RexDlssLogFunction)(void* user, RexDlssLogLevel level, const char* message);

typedef struct RexDlssAdapterRequirements {
  int32_t supported;
  // NVSDK_NGX_Feature_Support_Result bits, 0 when supported.
  uint32_t unsupported_reasons;
  uint32_t min_hw_architecture;
  char min_os_version[64];
} RexDlssAdapterRequirements;

typedef struct RexDlssInitDesc {
  uint32_t struct_size;
  ID3D12Device* device;
  // Must be writable: NGX keeps its logs there.
  const wchar_t* data_path;
  const char* engine_version;
  RexDlssLogFunction log;
  void* log_user;
  // 0 off, 1 on, 2 verbose: how much of NGX's own log reaches the log function.
  int32_t ngx_log_level;
} RexDlssInitDesc;

typedef struct RexDlssCapabilities {
  int32_t available;
  int32_t needs_driver_update;
  uint32_t min_driver_major;
  uint32_t min_driver_minor;
  int32_t feature_init_result;
  // The nvngx_dlss.dll beside the bridge, as major.minor.build.revision; empty when it is missing.
  char runtime_version[32];
} RexDlssCapabilities;

typedef struct RexDlssOptimalSettings {
  uint32_t render_width;
  uint32_t render_height;
  uint32_t min_render_width;
  uint32_t min_render_height;
  uint32_t max_render_width;
  uint32_t max_render_height;
} RexDlssOptimalSettings;

typedef struct RexDlssFeatureDesc {
  uint32_t struct_size;
  // The largest render size the feature evaluates.
  uint32_t render_width;
  uint32_t render_height;
  uint32_t output_width;
  uint32_t output_height;
  RexDlssQuality quality;
  uint32_t flags;
  // An NVSDK_NGX_DLSS_Hint_Render_Preset value; 0 lets the DLSS DLL choose for the quality.
  uint32_t preset;
} RexDlssFeatureDesc;

// Inputs in D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, the output in D3D12_RESOURCE_STATE_UNORDERED_ACCESS; DLSS
// leaves them in those states. Motion vectors are in render pixels and point from the current frame to the previous
// one, after scaling by mv_scale; the jitter is in render pixels in the same convention.
typedef struct RexDlssEvaluateDesc {
  uint32_t struct_size;
  ID3D12Resource* color;
  ID3D12Resource* depth;
  ID3D12Resource* motion_vectors;
  ID3D12Resource* exposure;  // 1x1, or null with REX_DLSS_FEATURE_AUTO_EXPOSURE
  ID3D12Resource* output;
  uint32_t input_x;  // where color, depth and motion vectors start in their resources
  uint32_t input_y;
  uint32_t render_width;
  uint32_t render_height;
  float jitter_x;
  float jitter_y;
  float mv_scale_x;
  float mv_scale_y;
  float pre_exposure;
  int32_t reset;
} RexDlssEvaluateDesc;

typedef struct RexDlssFeature RexDlssFeature;

// NGX is not thread-safe: one thread at a time.
typedef struct RexDlssBridgeApi {
  uint32_t abi_version;
  // Needs no initialization.
  RexDlssStatus (*query_adapter)(IDXGIAdapter* adapter, const wchar_t* data_path,
                                 RexDlssAdapterRequirements* requirements);
  RexDlssStatus (*init)(const RexDlssInitDesc* desc, RexDlssCapabilities* capabilities);
  RexDlssStatus (*query_optimal_settings)(uint32_t output_width, uint32_t output_height, RexDlssQuality quality,
                                          RexDlssOptimalSettings* settings);
  // The command list must be open on a direct or compute queue.
  RexDlssStatus (*create_feature)(ID3D12GraphicsCommandList* command_list, const RexDlssFeatureDesc* desc,
                                  RexDlssFeature** feature);
  // Changes the command list's descriptor heaps, root signature and pipeline state.
  RexDlssStatus (*evaluate)(ID3D12GraphicsCommandList* command_list, RexDlssFeature* feature,
                            const RexDlssEvaluateDesc* desc);
  // Only after the GPU has finished every command list that evaluated the feature.
  void (*release_feature)(RexDlssFeature* feature);
  void (*shutdown)(void);
  // Why the calling thread's last call that failed did, for the log.
  const char* (*last_error)(void);
} RexDlssBridgeApi;

typedef const RexDlssBridgeApi* (*RexDlssBridgeGetApiFunction)(uint32_t abi_version);

#ifdef __cplusplus
}
#endif

#endif  // REX_DLSS_BRIDGE_H
