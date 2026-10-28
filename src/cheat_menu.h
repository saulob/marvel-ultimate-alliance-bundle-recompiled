#pragma once

#include <atomic>

#include <rex/ui/window_listener.h>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui

namespace mua {

class CheatMenuDialog;

// The in-game cheat menu, opened and closed with F10. It changes the cheats for this session only; no setting is
// written from here.
class CheatMenu final : public rex::ui::WindowInputListener {
 public:
  void Attach(rex::ui::Window* window, rex::ui::ImGuiDrawer* drawer);
  void Detach();

  // Whether the game may read the keyboard and controller. It may not while an in-game overlay is open or the
  // keys and buttons that were pressed in it are still held.
  bool GameInputActive() const;

 private:
  friend class CheatMenuDialog;

  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void DialogClosed();

  rex::ui::Window* window_ = nullptr;
  std::atomic<rex::ui::ImGuiDrawer*> drawer_{nullptr};
  CheatMenuDialog* dialog_ = nullptr;
};

}  // namespace mua
