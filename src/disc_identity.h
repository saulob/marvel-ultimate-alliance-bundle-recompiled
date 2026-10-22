#pragma once

#include <filesystem>
#include <optional>

#include "game_install.h"
#include "game_library.h"

namespace trg {
class Task;
}  // namespace trg

namespace mua {

// The title ID and media ID of an Xbox 360 disc image, read from the start of its default.xex.
std::optional<DiscIdentity> ReadDiscIdentity(const std::filesystem::path& image);

// The install services backed by the disc reader and extractor of TRG Launcher; `task` reports the progress.
InstallServices DiscInstallServices(trg::Task& task);

}  // namespace mua
