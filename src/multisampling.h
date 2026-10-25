#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mua {

// The launcher's Multisampling choice: native 2x MSAA, or 4x compatibility (mua_msaa_compatibility).
// Neither can turn off multisampling the game itself asks for.
inline bool Native2xMsaaFor(bool compatibility) {
  return !compatibility;
}

// Host samples per pixel ReXGlue gives a guest render target with guest_samples samples, as both the
// Direct3D 12 and the Vulkan render target caches choose them.
inline uint32_t HostMsaaSamples(uint32_t guest_samples, bool native_2x_msaa, bool host_2x_supported) {
  if (guest_samples == 2 && !(native_2x_msaa && host_2x_supported)) {
    return 4;
  }
  return guest_samples;
}

// Whether the Direct3D 12 EDRAM lives in host render targets, the only path where the Multisampling choice matters.
// With rasterizer-ordered views the EDRAM is emulated in pixel shaders at a forced sample count, and ReXGlue leaves
// native_2x_msaa unused. "rov" needs hardware support; ReXGlue falls back to host render targets without it.
inline bool HostRenderTargetsUsed(std::string_view render_target_path, bool rov_supported) {
  if (render_target_path == "rov") {
    return !rov_supported;
  }
  return true;
}

// native_2x_msaa only reaches host render targets, so it is left alone under ROV.
inline std::optional<bool> Native2xMsaaToApply(bool host_render_targets, bool compatibility) {
  if (!host_render_targets) {
    return std::nullopt;
  }
  return Native2xMsaaFor(compatibility);
}

// The Multisampling value in the launcher's startup log; under ROV the player has no choice.
inline std::string MsaaLauncherText(bool host_render_targets, bool compatibility) {
  if (!host_render_targets) {
    return "ROV (fixed)";
  }
  return compatibility ? "4x MSAA (Compatibility)" : "2x MSAA";
}

// What the renderer does with the game's 2x MSAA render targets.
inline std::string MsaaRuntimeText(bool host_render_targets, bool compatibility) {
  if (!host_render_targets) {
    return "2x as 4x (ROV, fixed)";
  }
  return compatibility ? "2x as 4x (host render targets, compatibility)" : "2x native (host render targets)";
}

}  // namespace mua
