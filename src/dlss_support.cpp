#include "dlss_support.h"

#if defined(_WIN32)

#include <windows.h>

#include <dxgi.h>

#include <cstdint>
#include <system_error>

#include <rex/filesystem.h>

#include "dlss_bridge/rex_dlss_bridge.h"
#include "graphics_adapters.h"

namespace mua {
namespace {

constexpr uint32_t kNvidiaVendorId = 0x10DE;

// The bits of RexDlssAdapterRequirements::unsupported_reasons (NVIDIA's NVSDK_NGX_Feature_Support_Result).
std::string UnsupportedReason(uint32_t reasons) {
  if (reasons & 4) {
    return "this graphics card can't run it (NVIDIA DLSS needs a GeForce RTX card)";
  }
  if (reasons & 2) {
    return "the NVIDIA driver is too old for it";
  }
  if (reasons & 8) {
    return "this version of Windows is too old for it";
  }
  return "NVIDIA reports this system unsupported";
}

}  // namespace

DlssSupport CheckDlssSupport(int configured_adapter, const std::filesystem::path& data_path) {
  DlssSupport support;
  const std::filesystem::path bridge = rex::filesystem::GetExecutableFolder() / kDlssBridgeFile;
  std::error_code error;
  if (!std::filesystem::is_regular_file(bridge, error)) {
    support.reason = "this build has no NVIDIA DLSS (it was built without the NVIDIA DLSS SDK)";
    return support;
  }
  const bool found = WithEffectiveGraphicsAdapter(
      configured_adapter, [&](IDXGIAdapter1* adapter, const GraphicsAdapterDetails& details) {
        if (details.vendor_id != kNvidiaVendorId) {
          support.reason = "it needs an NVIDIA GeForce RTX graphics card";
          return;
        }
        // Never unloaded, like the GPU plugin's: NGX may keep threads in it.
        const HMODULE module = LoadLibraryExW(bridge.c_str(), nullptr,
                                              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        const auto get_api = module ? reinterpret_cast<RexDlssBridgeGetApiFunction>(reinterpret_cast<void*>(
                                          GetProcAddress(module, REX_DLSS_BRIDGE_ENTRY_POINT)))
                                    : nullptr;
        const RexDlssBridgeApi* api = get_api ? get_api(REX_DLSS_BRIDGE_ABI_VERSION) : nullptr;
        if (!api) {
          support.reason = std::string(kDlssBridgeFile) + " can't be loaded";
          return;
        }
        std::filesystem::create_directories(data_path, error);
        RexDlssAdapterRequirements requirements = {};
        if (api->query_adapter(adapter, data_path.c_str(), &requirements) != REX_DLSS_OK) {
          support.reason = std::string("NVIDIA's check failed (") + api->last_error() + ")";
          return;
        }
        support.available = requirements.supported != 0;
        if (!support.available) {
          support.reason = UnsupportedReason(requirements.unsupported_reasons);
        }
      });
  if (!found) {
    support.reason = "no Direct3D 12 graphics card was found";
  }
  return support;
}

}  // namespace mua

#else

namespace mua {

DlssSupport CheckDlssSupport(int, const std::filesystem::path&) {
  return {false, "NVIDIA DLSS runs only on Windows"};
}

}  // namespace mua

#endif
