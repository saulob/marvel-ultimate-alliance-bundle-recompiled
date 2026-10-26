#pragma once

#include <string>

namespace mua {

// "major.minor.patch" from the project version, plus MUA_VERSION_SUFFIX.
const std::string& PortVersion();

}  // namespace mua
