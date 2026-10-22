#include "disc_identity.h"

#include "trg/platform.h"
#include "trg/xbox360.h"

#include "path_utf8.h"

namespace mua {

std::optional<DiscIdentity> ReadDiscIdentity(const std::filesystem::path& image) {
  const trg::Xbox360DiscIdentity identity = trg::ReadXbox360DiscIdentity(Utf8(image));
  if (identity.title_id == 0) {
    return std::nullopt;
  }
  return DiscIdentity{identity.title_id, identity.media_id};
}

InstallServices DiscInstallServices(trg::Task& task) {
  InstallServices services;
  services.read_identity = ReadDiscIdentity;
  services.extract = [&task](const std::filesystem::path& disc, const std::filesystem::path& destination) {
    return trg::ExtractXbox360Disc(task, Utf8(disc), Utf8(destination));
  };
  return services;
}

}  // namespace mua
