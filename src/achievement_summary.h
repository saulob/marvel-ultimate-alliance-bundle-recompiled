#pragma once

#include <cstdint>
#include <vector>

namespace mua {

struct AchievementSummary {
  int unlocked = 0;
  int total = 0;
  int earned_gamerscore = 0;
  int total_gamerscore = 0;
};

// Totals over the achievements the store lists; `unlocked_flags[i]` says whether entry i is earned.
inline AchievementSummary Summarize(const std::vector<uint32_t>& gamerscores, const std::vector<bool>& unlocked_flags) {
  AchievementSummary summary;
  summary.total = static_cast<int>(gamerscores.size());
  for (size_t i = 0; i < gamerscores.size(); ++i) {
    summary.total_gamerscore += static_cast<int>(gamerscores[i]);
    if (i < unlocked_flags.size() && unlocked_flags[i]) {
      ++summary.unlocked;
      summary.earned_gamerscore += static_cast<int>(gamerscores[i]);
    }
  }
  return summary;
}

}  // namespace mua
