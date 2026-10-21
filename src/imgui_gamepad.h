#pragma once

#include <vector>

#include <SDL3/SDL_gamepad.h>

struct ImGuiIO;

namespace mua {

// Feeds the controllers SDL recognizes to Dear ImGui, so the launcher and the in-game overlays navigate on any pad
// the game itself does: an Xbox pad, a DualSense, a Steam Controller. The runtime's input system cannot be used for
// this: it starts only with the game, and an open overlay keeps the game from reading it.
//
// This runs on the program's own SDL (on Windows a second, independent instance; elsewhere the one SDL shared with
// the runtime). It starts and quits the gamepad subsystem on its own; SDL counts the starts, so the runtime's input
// is unaffected. Every connected controller is read and their inputs combined, so any of them navigates, and pads
// added or removed while it is open are followed.
//
// Call Update() once per frame while the dialog is open and Release() when it closes. Release() lets go of every
// key, so nothing read here is left held in the game. A pad unplugged while a button is held has its key released
// on the next Update.
class ImGuiGamepad {
 public:
  ImGuiGamepad() = default;
  ~ImGuiGamepad();
  ImGuiGamepad(const ImGuiGamepad&) = delete;
  ImGuiGamepad& operator=(const ImGuiGamepad&) = delete;

  void Update(ImGuiIO& io);
  void Release(ImGuiIO& io);

 private:
  void DrainEvents();
  void CloseAll();

  std::vector<SDL_Gamepad*> pads_;
  bool subsystem_ = false;
};

}  // namespace mua
