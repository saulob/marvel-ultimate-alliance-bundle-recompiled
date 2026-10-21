#include "launcher_achievements.h"

#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

namespace mua {

struct LauncherAchievements::Runtime {
  rex::Runtime runtime;
  std::unique_ptr<rex::ui::AchievementIconCache> icons;

  Runtime(const std::filesystem::path& game_dir, const std::filesystem::path& user_dir)
      : runtime(game_dir, user_dir) {}
  // The icons are textures of the immediate drawer and read the runtime's XDBF: they go first.
  ~Runtime() { icons.reset(); }
};

LauncherAchievements::LauncherAchievements(const std::filesystem::path& game_dir,
                                           const std::filesystem::path& user_dir,
                                           rex::ui::ImmediateDrawer* immediate_drawer) {
  std::error_code ec;
  auto runtime = std::make_unique<Runtime>(std::filesystem::absolute(game_dir, ec),
                                           std::filesystem::absolute(user_dir, ec));
  rex::RuntimeConfig config;
  config.kernel_init = rex::kernel::InitializeKernel;
  config.tool_mode = true;
  if (XFAILED(runtime->runtime.Setup(std::move(config)))) {
    REXLOG_WARN("Launcher achievements: runtime setup failed");
    return;
  }
  if (XFAILED(runtime->runtime.LoadXexImage("game:\\default.xex"))) {
    REXLOG_WARN("Launcher achievements: default.xex could not be loaded");
    return;
  }
  rex::system::KernelState& kernel = *runtime->runtime.kernel_state();
  entries_ = ListAchievements(kernel.achievements());
  runtime->icons = std::make_unique<rex::ui::AchievementIconCache>(immediate_drawer, &runtime->runtime);
  runtime_ = std::move(runtime);
  ok_ = !entries_.empty();
}

LauncherAchievements::~LauncherAchievements() = default;

ImTextureID LauncherAchievements::Icon(const rex::system::AchievementInfo& info) {
  if (!runtime_ || !runtime_->icons) return {};
  return reinterpret_cast<ImTextureID>(runtime_->icons->GetIcon(info));
}

}  // namespace mua
