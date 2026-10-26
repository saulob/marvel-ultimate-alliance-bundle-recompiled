#include "update_config.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include <rex/logging.h>

#include "port_version.h"
#include "update_bundle.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"

namespace mua {
namespace {

std::optional<std::string> Environment(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || !value) {
    return std::nullopt;
  }
  std::string result(value);
  std::free(value);
  if (result.empty()) {
    return std::nullopt;
  }
  return result;
#else
  const char* value = std::getenv(name);
  if (!value || !*value) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

bool TestMode() {
  const std::optional<std::string> value = Environment("MUA_UPDATE_TEST");
  return value && *value == "1";
}

std::optional<std::string> TestVariable(const char* name) {
  if (!TestMode()) {
    return std::nullopt;
  }
  return Environment(name);
}

}  // namespace

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir) {
  trg::update::UpdateConfig config;
  config.current_version = PortVersion();
  config.releases_url = kUpdateReleasesUrl;
  config.user_agent = "MarvelUltimateAlliance-Updater/" + PortVersion();
  config.install_dir = install_dir;
  config.restart_arguments = UpdateRestartArguments(trg::update::CurrentProcessArguments());

#if defined(_WIN32)
  config.package_kind = trg::update::UpdatePackageKind::kZip;
  config.asset_name = UpdateAssetName;
  config.checksum_asset_name = UpdateChecksumAssetName;
  config.restart_executable = install_dir / kBundleLauncher;
  config.required_staged_files.assign(kBundleFiles.begin(), kBundleFiles.end());
  config.install_supported = std::filesystem::exists(install_dir / kUpdateHelper);
  if (!config.install_supported) {
    config.install_unsupported_reason =
        "The update helper is missing from this installation. Reinstall this release to enable automatic updates.";
  }
#else
  config.package_kind = trg::update::UpdatePackageKind::kAppImage;
  config.asset_name = UpdateAppImageName;
  config.checksum_asset_name = UpdateAppImageChecksumName;
  std::filesystem::path appimage;
  if (trg::update::HasOriginalAppImage(appimage)) {
    config.appimage_path = appimage;
    config.restart_executable = appimage;
    config.install_supported = true;
  } else {
    config.install_unsupported_reason = "Automatic installation is available in the AppImage build.";
  }
#endif

  config.log = [](std::string_view text) { REXLOG_INFO("Updater: {}", text); };

  if (const std::optional<std::string> version = TestVariable("MUA_UPDATE_TEST_CURRENT_VERSION"); version) {
    config.current_version = *version;
    REXLOG_WARN("Updater test mode: reporting version {}", config.current_version);
  }
  if (const std::optional<std::string> json = TestVariable("MUA_UPDATE_TEST_RELEASE_JSON"); json) {
    config.release_json_override = trg::update::PathFromUtf8(*json);
    config.allow_loopback_http = true;
  }
  if (const std::optional<std::string> hash = TestVariable("MUA_UPDATE_TEST_ASSET_SHA256"); hash) {
    config.sha256_override = *hash;
  }
  return config;
}

}  // namespace mua
