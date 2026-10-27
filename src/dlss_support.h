#pragma once

#include <filesystem>
#include <string>

namespace mua {

// Built beside the program when the NVIDIA DLSS SDK was present; the only code built against it.
inline constexpr char kDlssBridgeFile[] = "mua_dlss_bridge.dll";

struct DlssSupport {
  bool available = false;
  std::string reason;  // why not, for the launcher
};

// Windows: whether NVIDIA DLSS Super Resolution can run on the graphics card ReXGlue selects for `configured_adapter`
// (-1 automatic): an NVIDIA card that NVIDIA's own check, through the DLSS bridge beside the program, accepts. NGX may
// write its files to data_path.
DlssSupport CheckDlssSupport(int configured_adapter, const std::filesystem::path& data_path);

}  // namespace mua
