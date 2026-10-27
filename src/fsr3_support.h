#pragma once

#include <string>

namespace mua {

// AMD's signed FidelityFX DLL that runs FSR 3.1 (tools/setup-fidelityfx.ps1), shipped beside the programs.
inline constexpr char kFidelityFxLibraryFile[] = "amd_fidelityfx_dx12.dll";
inline constexpr char kFidelityFxSdkVersion[] = "1.1.4";

struct Fsr3Support {
  bool available = false;
  std::string reason;  // why not, for the launcher
};

// FSR 3.1 needs no particular graphics card: only AMD's DLL beside the program and the rasterizer-ordered views render
// path, which the GPU plugin's scene upscaler reads the 3D scene from.
inline Fsr3Support Fsr3SupportFor(bool direct3d12, bool library_present, bool host_render_targets) {
  if (!direct3d12) {
    return {false, "AMD FSR 3.1 runs only on Windows"};
  }
  if (!library_present) {
    return {false, std::string("this build has no ") + kFidelityFxLibraryFile + " (built without the AMD FidelityFX SDK)"};
  }
  if (host_render_targets) {
    return {false, "it needs the rasterizer-ordered views render path, which this graphics card doesn't run"};
  }
  return {true, ""};
}

}  // namespace mua
