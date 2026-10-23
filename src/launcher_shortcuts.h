#pragma once

namespace mua {

struct SaveShortcutState {
  bool ctrl_down = false;
  bool s_pressed = false;
  bool text_input = false;
  bool capturing_key = false;
  bool popup_open = false;
  bool starting = false;
};

inline bool SaveShortcutFires(const SaveShortcutState& state) {
  return state.ctrl_down && state.s_pressed && !state.text_input && !state.capturing_key && !state.popup_open &&
         !state.starting;
}

}  // namespace mua
