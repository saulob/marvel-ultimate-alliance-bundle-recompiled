#pragma once

#include <span>
#include <string_view>

namespace mua {

enum class GraphicsBackend { kDirect3D12, kVulkan };

// The runtime settings behind a launcher choice that both backends offer under their own names.
struct BackendSettings {
  std::string_view name;
  // VSync Off sets each of these to true and VSync On to false; the launcher saves the first.
  std::span<const char* const> tearing;
  // The graphics card index.
  const char* graphics_card;
  const char* render_target_path;
  // The pixel shader EDRAM path, in logs.
  std::string_view pixel_shader_edram;
};

inline constexpr const char* kDirect3D12Tearing[] = {"d3d12_allow_variable_refresh_rate_and_tearing"};
// FIFO relaxed tears whenever a frame is late, so VSync On disallows it along with immediate.
inline constexpr const char* kVulkanTearing[] = {"vulkan_allow_present_mode_immediate",
                                                 "vulkan_allow_present_mode_fifo_relaxed"};

inline constexpr BackendSettings kDirect3D12Settings{"Direct3D 12", kDirect3D12Tearing, "d3d12_adapter",
                                                     "render_target_path_d3d12", "ROV"};
inline constexpr BackendSettings kVulkanSettings{"Vulkan", kVulkanTearing, "vulkan_device",
                                                 "render_target_path_vulkan", "FSI"};

constexpr const BackendSettings& SettingsFor(GraphicsBackend backend) {
  return backend == GraphicsBackend::kDirect3D12 ? kDirect3D12Settings : kVulkanSettings;
}

#if defined(_WIN32)
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::kDirect3D12;
#else
inline constexpr GraphicsBackend kGraphicsBackend = GraphicsBackend::kVulkan;
#endif

inline constexpr const BackendSettings& kBackendSettings = SettingsFor(kGraphicsBackend);

}  // namespace mua
