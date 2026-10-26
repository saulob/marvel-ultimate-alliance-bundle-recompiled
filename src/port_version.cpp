#include "port_version.h"

namespace mua {

const std::string& PortVersion() {
  static const std::string version = std::to_string(MUA_VERSION_MAJOR) + "." + std::to_string(MUA_VERSION_MINOR) +
                                     "." + std::to_string(MUA_VERSION_PATCH) + MUA_VERSION_SUFFIX;
  return version;
}

}  // namespace mua
