#include "game_profile.h"

#include <array>

namespace mua {
namespace {

constexpr std::array kGoldCompanionFiles = {
    CompanionFile{"build.ini", 198},
    CompanionFile{"muadlc_content.zip", 44616523},
    CompanionFile{"muadlc_titleupdate.zip", 2161587},
};

constexpr std::array kProfiles = {
    GameProfile{
        .id = "mua-gold-x360",
        .display_name = "Marvel Ultimate Alliance Gold Edition",
        .title_id = 0x415607DA,
        .media_id = 0x5A20A6D4,
        .pe_name = "Marvel_Xenon_fnl.pe",
        .xex_size = 3981312,
        .xex_sha256 = "06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7",
        .companion_files = kGoldCompanionFiles,
    },
};

}  // namespace

std::span<const GameProfile> SupportedProfiles() {
  return kProfiles;
}

const GameProfile& DefaultProfile() {
  return kProfiles.front();
}

}  // namespace mua
