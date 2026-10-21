#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <imgui.h>

#include "achievement_list.h"

namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace mua {

// The achievements as ReXGlue's store holds them, read for the launcher. The store needs the game's XDBF, so a
// runtime without graphics, audio or input loads default.xex just long enough to read it; it must be destroyed
// before the game's own runtime is built.
class LauncherAchievements {
 public:
  LauncherAchievements(const std::filesystem::path& game_dir, const std::filesystem::path& user_dir,
                       rex::ui::ImmediateDrawer* immediate_drawer);
  ~LauncherAchievements();

  bool ok() const { return ok_; }
  const std::vector<AchievementEntry>& entries() const { return entries_; }
  ImTextureID Icon(const rex::system::AchievementInfo& info);

 private:
  struct Runtime;

  std::unique_ptr<Runtime> runtime_;
  std::vector<AchievementEntry> entries_;
  bool ok_ = false;
};

}  // namespace mua
