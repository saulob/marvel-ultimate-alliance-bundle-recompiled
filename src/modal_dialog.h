#pragma once

#include <bitset>

#include <imgui.h>

#include <rex/ui/imgui_dialog.h>

#include "imgui_gamepad.h"
#include "overlay_rules.h"

namespace rex::ui {
class Window;
}  // namespace rex::ui

namespace mua {

// Base of the in-game overlays that take over input. At most one is open at a time. While it is, and until what was
// pressed in it is released, the game reads neither the keyboard nor the controller, so closing with B or Escape never
// also presses it in the game.
class ModalDialog : public rex::ui::ImGuiDialog {
 public:
  static bool AnyOpen();

  // Escape closes the open dialog before the ImGui drawer or the game see it.
  static void AttachEscapeKey(rex::ui::Window* window);
  static void DetachEscapeKey();

  void RequestClose() { close_.Request(); }

 protected:
  explicit ModalDialog(rex::ui::ImGuiDrawer* drawer);
  ~ModalDialog() override;

  virtual void DrawContent(ImGuiIO& io) = 0;

  void OnDraw(ImGuiIO& io) final;
  void OnClose() override;

 private:
  void TrackPressedKeys();

  ImGuiGamepad gamepad_;
  std::bitset<ImGuiKey_NamedKey_COUNT> pressed_;
  ImGuiConfigFlags saved_nav_flags_ = 0;
  ImGuiBackendFlags saved_has_gamepad_ = 0;
  bool started_ = false;
  CloseGate close_;
};

}  // namespace mua
