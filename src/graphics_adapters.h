#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mua {

// A DXGI adapter that can create a Direct3D 12 device. `index` is the one ReXGlue's d3d12_adapter takes.
struct GraphicsAdapter {
  int index;
  std::string name;
};

// Windows: the hardware adapters in the order ReXGlue counts them for d3d12_adapter. Empty elsewhere.
std::vector<GraphicsAdapter> ListGraphicsAdapters();

struct GraphicsAdapterDetails {
  int index = -1;
  std::string name;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  bool known = false;
};

// Windows: the adapter ReXGlue's d3d12_adapter selection will use for `configured_index` (-1 automatic, -2 WARP).
// Mirrors the runtime's enumeration, so Automatic reports the GPU that is actually picked.
GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index);

// Windows: whether the adapter ReXGlue selects for `configured_index` (-1 automatic, -2 WARP) supports rasterizer-ordered
// views, which the EDRAM in pixel shaders path (render_target_path_d3d12 = "rov") needs. False elsewhere.
bool RasterizerOrderedViewsSupported(int configured_index);

inline constexpr char kGraphicsAdapterAuto[] = "auto";

// The adapter index a mua_graphics_adapter value names; empty for Automatic and for anything that is not an index.
inline std::optional<int> ParseGraphicsAdapter(std::string_view setting) {
  int index = 0;
  const char* end = setting.data() + setting.size();
  const auto [stop, error] = std::from_chars(setting.data(), end, index);
  if (setting.empty() || error != std::errc() || stop != end || index < 0) {
    return std::nullopt;
  }
  return index;
}

// The value for d3d12_adapter: -1 (automatic) unless the setting names an adapter that is present.
inline int ResolveGraphicsAdapter(std::string_view setting, const std::vector<GraphicsAdapter>& adapters) {
  const std::optional<int> index = ParseGraphicsAdapter(setting);
  for (const GraphicsAdapter& adapter : adapters) {
    if (index && adapter.index == *index) {
      return adapter.index;
    }
  }
  return -1;
}

// The Graphics card choices as {value, label}: Automatic, then each adapter, then the saved one if it is not present.
inline std::vector<std::pair<std::string, std::string>> GraphicsAdapterOptions(
    const std::vector<GraphicsAdapter>& adapters, std::string_view current) {
  std::vector<std::pair<std::string, std::string>> options;
  options.emplace_back(kGraphicsAdapterAuto, "Automatic (Recommended)");
  for (const GraphicsAdapter& adapter : adapters) {
    int same_name = 0;
    for (const GraphicsAdapter& other : adapters) {
      same_name += other.name == adapter.name;
    }
    options.emplace_back(std::to_string(adapter.index),
                         same_name > 1 ? adapter.name + " (" + std::to_string(adapter.index) + ")" : adapter.name);
  }
  if (const std::optional<int> index = ParseGraphicsAdapter(current);
      index && ResolveGraphicsAdapter(current, adapters) < 0) {
    options.emplace_back(std::string(current), "Graphics card " + std::to_string(*index) + " (not found)");
  }
  return options;
}

}  // namespace mua
