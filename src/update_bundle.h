#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "updater/version.h"

namespace mua {

inline constexpr char kUpdateReleasesUrl[] =
    "https://api.github.com/repos/saulob/marvel-ultimate-alliance-recompiled/releases/latest";
inline constexpr char kUpdatePackageName[] = "MarvelUltimateAlliance";
inline constexpr char kBundleLauncher[] = "marvel_ultimate_alliance.exe";
inline constexpr char kUpdateHelper[] = "trg-updater.exe";

// Every program and library of the bundle: a release that lacks one is refused, so a package never replaces
// the launcher without the game programs it starts.
inline constexpr std::array<const char*, 12> kBundleFiles = {
    "marvel_ultimate_alliance.exe",
    "marvel_ultimate_alliance_mua1.exe",
    "marvel_ultimate_alliance_mua1_gold.exe",
    "marvel_ultimate_alliance_mua2.exe",
    "trg-updater.exe",
    "rexruntime.dll",
    "rexgpu-xenos.dll",
    "marvel_ultimate_alliance.toml",
    "assets/launcher/header-art-mua1.png",
    "assets/launcher/header-art-mua1-gold.png",
    "assets/launcher/header-art-mua2.png",
    "licenses/THIRD-PARTY-LICENSES.txt",
};

inline std::string UpdateAssetStem(const trg::update::SemanticVersion& version) {
  return std::string(kUpdatePackageName) + "-v" + version.CoreString() + "-windows-x64";
}

inline std::string UpdateAssetName(const trg::update::SemanticVersion& version) {
  return UpdateAssetStem(version) + ".zip";
}

inline std::string UpdateChecksumAssetName(const trg::update::SemanticVersion& version) {
  return UpdateAssetStem(version) + ".sha256";
}

// The bundle launcher restarts with its window, not the game's program the update was started from.
inline std::vector<std::string> UpdateRestartArguments(std::vector<std::string> arguments) {
  arguments.erase(std::remove_if(arguments.begin(), arguments.end(),
                                 [](const std::string& argument) {
                                   return std::string_view(argument).starts_with("--mua_skip_launcher") ||
                                          std::string_view(argument).starts_with("--mua_launcher");
                                 }),
                  arguments.end());
  arguments.emplace_back("--mua_launcher=true");
  return arguments;
}

}  // namespace mua
