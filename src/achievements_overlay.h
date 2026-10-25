#pragma once

#include <functional>

#include <rex/system/achievement_manager.h>
#include <rex/ui/overlay/achievement_icon_cache.h>

#include "achievement_list.h"
#include "modal_dialog.h"

namespace rex {
class Runtime;
}  // namespace rex

namespace rex::ui {
class ImmediateDrawer;
}  // namespace rex::ui

namespace mua {

// The achievements list, sized and centred for the viewport and themed for the running game, with a B Back footer.
// It deletes itself when closed and calls `closed` as it goes.
class AchievementsOverlay final : public ModalDialog {
 public:
  AchievementsOverlay(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate_drawer, rex::Runtime* runtime,
                      rex::system::AchievementManager* achievements, std::function<void()> closed);
  ~AchievementsOverlay() override;

 protected:
  void DrawContent(ImGuiIO& io) override;

 private:
  rex::system::AchievementManager* achievements_;
  rex::ui::AchievementIconCache icon_cache_;
  std::function<void()> closed_;
  bool focused_ = false;
};

}  // namespace mua
