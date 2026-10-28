#pragma once

#include <charconv>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct IDXGIAdapter1;

namespace mua {

// A graphics card the runtime can use: a DXGI adapter that can create a Direct3D 12 device, or a Vulkan device.
// `index` is the one ReXGlue's d3d12_adapter or vulkan_device takes.
struct GraphicsAdapter {
  int index;
  std::string name;
};

// The hardware graphics cards: on Windows in the order ReXGlue counts them for d3d12_adapter, on Linux in the order its
// Vulkan provider prefers them.
std::vector<GraphicsAdapter> ListGraphicsAdapters();

struct GraphicsAdapterDetails {
  int index = -1;
  std::string name;
  uint32_t vendor_id = 0;
  uint32_t device_id = 0;
  bool known = false;
};

// The graphics card the runtime will use for `configured_index` (-1 automatic; -2 WARP on Windows). Mirrors the
// runtime's enumeration, so the effective GPU is the one actually picked.
GraphicsAdapterDetails EffectiveGraphicsAdapter(int configured_index);

// Windows: calls `use` with that adapter and returns true; false when there is none, and elsewhere.
bool WithEffectiveGraphicsAdapter(int configured_index,
                                  const std::function<void(IDXGIAdapter1*, const GraphicsAdapterDetails&)>& use);

// Whether the graphics card ReXGlue selects for `configured_index` (-1 automatic, -2 WARP on Windows) can emulate the
// EDRAM in pixel shaders: rasterizer-ordered views for render_target_path_d3d12 = "rov", fragment shader interlock for
// render_target_path_vulkan = "fsi".
bool PixelShaderEdramSupported(int configured_index);

// The adapter index a mua_graphics_adapter value names; empty for anything that is not an index, such as the retired
// "auto".
inline std::optional<int> ParseGraphicsAdapter(std::string_view setting) {
  int index = 0;
  const char* end = setting.data() + setting.size();
  const auto [stop, error] = std::from_chars(setting.data(), end, index);
  if (setting.empty() || error != std::errc() || stop != end || index < 0) {
    return std::nullopt;
  }
  return index;
}

// The adapter the setting names if it is present, else the first adapter. Empty only when there are no adapters.
inline std::optional<int> ResolveGraphicsAdapter(std::string_view setting, const std::vector<GraphicsAdapter>& adapters) {
  if (adapters.empty()) {
    return std::nullopt;
  }
  const std::optional<int> index = ParseGraphicsAdapter(setting);
  for (const GraphicsAdapter& adapter : adapters) {
    if (index && adapter.index == *index) {
      return adapter.index;
    }
  }
  return adapters.front().index;
}

// The Graphics card choices as {value, label}, one per adapter.
inline std::vector<std::pair<std::string, std::string>> GraphicsAdapterOptions(
    const std::vector<GraphicsAdapter>& adapters) {
  std::vector<std::pair<std::string, std::string>> options;
  for (const GraphicsAdapter& adapter : adapters) {
    int same_name = 0;
    for (const GraphicsAdapter& other : adapters) {
      same_name += other.name == adapter.name;
    }
    options.emplace_back(std::to_string(adapter.index),
                         same_name > 1 ? adapter.name + " (" + std::to_string(adapter.index) + ")" : adapter.name);
  }
  return options;
}

}  // namespace mua
