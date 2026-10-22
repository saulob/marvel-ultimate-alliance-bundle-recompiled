#include "game_profile.h"

#include <algorithm>
#include <array>

namespace mua {
namespace {

constexpr std::array kGoldCompanionFiles = {
    CompanionFile{"build.ini", 198},
    CompanionFile{"muadlc_content.zip", 44616523},
    CompanionFile{"muadlc_titleupdate.zip", 2161587},
};

constexpr std::array kMua1CompanionFiles = {
    CompanionFile{"build.ini", 198},
    CompanionFile{"alchemy.ini", 502},
};

constexpr std::array kMua2CompanionFiles = {
    CompanionFile{"build.xml", 220},
    CompanionFile{"version.xml", 143},
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
        .install_bytes = 5949759828,
    },
    GameProfile{
        .id = "mua-x360",
        .display_name = "Marvel Ultimate Alliance",
        .title_id = 0x415607DA,
        .media_id = 0x26848425,
        .pe_name = "Marvel_Xenon_fnl.pe",
        .xex_size = 3850240,
        .xex_sha256 = "44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A",
        .companion_files = kMua1CompanionFiles,
        .install_bytes = 5862081806,
    },
    GameProfile{
        .id = "mua2-x360",
        .display_name = "Marvel Ultimate Alliance 2",
        .title_id = 0x4156082F,
        .media_id = 0x39B3E2C9,
        .pe_name = "Alliance-xenon-fin.pe",
        .xex_size = 12664832,
        .xex_sha256 = "2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440",
        .companion_files = kMua2CompanionFiles,
        .install_bytes = 7079485383,
    },
};

}  // namespace

std::span<const GameProfile> KnownProfiles() {
  return kProfiles;
}

const GameProfile* ProfileById(std::string_view id) {
  const auto match = std::ranges::find_if(kProfiles, [&](const GameProfile& profile) { return profile.id == id; });
  return match == kProfiles.end() ? nullptr : &*match;
}

}  // namespace mua
