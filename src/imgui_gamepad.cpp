#include "imgui_gamepad.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

#include <imgui.h>

#include <SDL3/SDL.h>

namespace mua {
namespace {

// The buttons that drive ImGui navigation. SDL names them by position (south, east, ...), so a Steam Controller's
// or a DualSense's face buttons map exactly as an Xbox pad's.
struct ButtonKey {
  ImGuiKey key;
  SDL_GamepadButton button;
};
constexpr ButtonKey kButtons[] = {
    {ImGuiKey_GamepadStart, SDL_GAMEPAD_BUTTON_START},
    {ImGuiKey_GamepadBack, SDL_GAMEPAD_BUTTON_BACK},
    {ImGuiKey_GamepadFaceDown, SDL_GAMEPAD_BUTTON_SOUTH},
    {ImGuiKey_GamepadFaceRight, SDL_GAMEPAD_BUTTON_EAST},
    {ImGuiKey_GamepadFaceLeft, SDL_GAMEPAD_BUTTON_WEST},
    {ImGuiKey_GamepadFaceUp, SDL_GAMEPAD_BUTTON_NORTH},
    {ImGuiKey_GamepadDpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {ImGuiKey_GamepadDpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {ImGuiKey_GamepadDpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {ImGuiKey_GamepadDpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
};

// The left stick dead zone the launcher has always used (XInput's). SDL's Y axis points down.
constexpr int kStickDeadZone = 7849;
struct StickKey {
  ImGuiKey key;
  SDL_GamepadAxis axis;
  int direction;
};
constexpr StickKey kSticks[] = {
    {ImGuiKey_GamepadLStickLeft, SDL_GAMEPAD_AXIS_LEFTX, -1},
    {ImGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX, 1},
    {ImGuiKey_GamepadLStickUp, SDL_GAMEPAD_AXIS_LEFTY, -1},
    {ImGuiKey_GamepadLStickDown, SDL_GAMEPAD_AXIS_LEFTY, 1},
};

bool Tracks(const std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  for (SDL_Gamepad* pad : pads) {
    if (SDL_GetGamepadID(pad) == id) {
      return true;
    }
  }
  return false;
}

void OpenPad(std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  if (Tracks(pads, id)) {
    return;
  }
  if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
    pads.push_back(pad);
  }
}

void ClosePad(std::vector<SDL_Gamepad*>& pads, SDL_JoystickID id) {
  for (auto it = pads.begin(); it != pads.end(); ++it) {
    if (SDL_GetGamepadID(*it) == id) {
      SDL_CloseGamepad(*it);
      pads.erase(it);
      return;
    }
  }
}

}  // namespace

ImGuiGamepad::~ImGuiGamepad() {
  CloseAll();
}

void ImGuiGamepad::Update(ImGuiIO& io) {
  if (!subsystem_) {
    // Started here and quit in Release; on Linux the runtime shares this SDL and counts the same starts, and on
    // Windows it has its own instance, so either way the game's own input is unaffected.
    if (!(subsystem_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD))) {
      return;
    }
    // Controllers connected before this started do not arrive as events; take the ones already there.
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
      OpenPad(pads_, ids[i]);
    }
    SDL_free(ids);
  }
  DrainEvents();
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  // The union of every connected pad, so any of them navigates. All-false when none is connected, which is what
  // releases the keys of a pad that was unplugged while a button was held.
  std::array<bool, std::size(kButtons)> down{};
  std::array<float, std::size(kSticks)> amount{};
  for (SDL_Gamepad* pad : pads_) {
    for (size_t i = 0; i < std::size(kButtons); ++i) {
      down[i] = down[i] || SDL_GetGamepadButton(pad, kButtons[i].button);
    }
    for (size_t i = 0; i < std::size(kSticks); ++i) {
      const int value = kSticks[i].direction * int(SDL_GetGamepadAxis(pad, kSticks[i].axis));
      const float reached =
          std::clamp(float(value - kStickDeadZone) / float(32767 - kStickDeadZone), 0.0f, 1.0f);
      amount[i] = std::max(amount[i], reached);
    }
  }
  for (size_t i = 0; i < std::size(kButtons); ++i) {
    io.AddKeyEvent(kButtons[i].key, down[i]);
  }
  for (size_t i = 0; i < std::size(kSticks); ++i) {
    io.AddKeyAnalogEvent(kSticks[i].key, amount[i] > 0.1f, amount[i]);
  }
}

void ImGuiGamepad::Release(ImGuiIO& io) {
  for (const ButtonKey& b : kButtons) {
    io.AddKeyEvent(b.key, false);
  }
  for (const StickKey& s : kSticks) {
    io.AddKeyAnalogEvent(s.key, false, 0.0f);
  }
  CloseAll();
}

void ImGuiGamepad::DrainEvents() {
  SDL_Event event;
  auto handle = [this](const SDL_Event& e) {
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
      OpenPad(pads_, e.gdevice.which);
    } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
      ClosePad(pads_, e.gdevice.which);
    }
  };
#if defined(_WIN32)
  // The program's own SDL, with a queue of its own (the runtime DLL has a separate instance): everything queued
  // here is ours. SDL_PollEvent pumps and updates the joysticks on the way.
  while (SDL_PollEvent(&event)) {
    handle(event);
  }
#else
  // The program and the runtime share one SDL, whose queue also holds the window's events (its paint requests, the
  // keys and the mouse): take only the controllers' and leave the rest for the window.
  SDL_PumpEvents();
  while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_FINGER_DOWN - 1) > 0) {
    handle(event);
  }
#endif
}

void ImGuiGamepad::CloseAll() {
  for (SDL_Gamepad* pad : pads_) {
    SDL_CloseGamepad(pad);
  }
  pads_.clear();
  if (subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    subsystem_ = false;
  }
}

}  // namespace mua
