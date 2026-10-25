#pragma once

#include <filesystem>

#include "updater/update_controller.h"

namespace mua {

trg::update::UpdateConfig MakeUpdateConfig(const std::filesystem::path& install_dir);

}  // namespace mua
