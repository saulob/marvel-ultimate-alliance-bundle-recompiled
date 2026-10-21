#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <rex/system/achievement_store.h>

#include "achievement_summary.h"

namespace mua {

struct AchievementEntry {
  rex::system::AchievementInfo info;
  bool unlocked = false;
};

// The store's achievements in the game's own order with their unlock state.
template <typename Manager>
std::vector<AchievementEntry> ListAchievements(const Manager& manager) {
  std::vector<AchievementEntry> entries;
  for (auto& info : manager.ListAchievements()) {
    const bool unlocked = manager.IsUnlocked(info.id);
    entries.push_back({std::move(info), unlocked});
  }
  std::stable_sort(entries.begin(), entries.end(), [](const AchievementEntry& a, const AchievementEntry& b) {
    return a.info.id < b.info.id;
  });
  return entries;
}

inline AchievementSummary Summarize(const std::vector<AchievementEntry>& entries) {
  std::vector<uint32_t> gamerscores;
  std::vector<bool> unlocked;
  for (const AchievementEntry& entry : entries) {
    gamerscores.push_back(entry.info.gamerscore);
    unlocked.push_back(entry.unlocked);
  }
  return Summarize(gamerscores, unlocked);
}

// The earned description once unlocked, the locked one before, falling back when the title has no locked text.
inline const std::string& EntryDescription(const AchievementEntry& entry) {
  return entry.unlocked || entry.info.unachieved_description.empty() ? entry.info.description
                                                                      : entry.info.unachieved_description;
}

}  // namespace mua
