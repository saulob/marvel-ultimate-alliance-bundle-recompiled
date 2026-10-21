#pragma once

// Remappable keyboard controls for the launcher's Controls page. The launcher edits the ReXGlue
// keybind_* settings through the same trg::SettingsStore every other launcher setting uses, so the
// keyboard needs no settings system of its own.
//
// A binding is one ReXGlue name, optionally with Shift/Ctrl/Alt prefixes ("Shift+Up"), exactly the
// form ReXGlue's keyboard driver parses. Stored defaults are the pinned ReXGlue ones and keep their
// alternatives ("Semicolon,Space"); a capture replaces the whole binding with one key.

#include <array>
#include <string>
#include <string_view>

#include <imgui.h>

#include "trg/settings.h"

namespace mua {

// One keyboard control the launcher offers: the ReXGlue keybind_* setting, the label shown on the
// Controls page, and the pinned ReXGlue default (one key, or alternatives separated by commas).
struct KeyboardAction {
  const char* key;
  const char* label;
  const char* default_binding;
};

// The controls the launcher offers, in page order. Guide is left out: it is empty by default and
// the pad ignores it. The defaults mirror ReXGlue 0.10.0
// (src/input/mnk/mnk_input_driver.cpp); ReXGlue uses this table's key names
// (src/ui/keybinds.cpp). Reset all settings and Keyboard defaults restore exactly these.
inline constexpr std::array<KeyboardAction, 24> kKeyboardActions = {{
    {"keybind_a", "A", "Semicolon,Space"},
    {"keybind_b", "B", "Quote,Backspace"},
    {"keybind_x", "X", "L"},
    {"keybind_y", "Y", "P"},
    {"keybind_left_shoulder", "Left bumper", "1"},
    {"keybind_right_shoulder", "Right bumper", "3"},
    {"keybind_left_trigger", "Left trigger", "Q,I"},
    {"keybind_right_trigger", "Right trigger", "E,O"},
    {"keybind_lstick_up", "Left stick up", "W"},
    {"keybind_lstick_down", "Left stick down", "S"},
    {"keybind_lstick_left", "Left stick left", "A"},
    {"keybind_lstick_right", "Left stick right", "D"},
    {"keybind_lstick_press", "Left stick press", "F"},
    {"keybind_rstick_up", "Right stick up", "Up"},
    {"keybind_rstick_down", "Right stick down", "Down"},
    {"keybind_rstick_left", "Right stick left", "Left"},
    {"keybind_rstick_right", "Right stick right", "Right"},
    {"keybind_rstick_press", "Right stick press", "K"},
    {"keybind_dpad_up", "D-pad up", "Shift+Up"},
    {"keybind_dpad_down", "D-pad down", "Shift+Down"},
    {"keybind_dpad_left", "D-pad left", "Shift+Left"},
    {"keybind_dpad_right", "D-pad right", "Shift+Right"},
    {"keybind_back", "View / Back", "Z,Tab"},
    {"keybind_start", "Menu / Start", "X,Return"},
}};

namespace keybind_detail {

inline std::string_view Trim(std::string_view text) {
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  return text;
}

}  // namespace keybind_detail

// The ReXGlue key name for an ImGui key, or nullptr when ReXGlue has no name for it: a mouse or
// gamepad key, a bare modifier (one can never fire), or a key its keybind parser does not accept.
inline const char* KeyboardKeyName(ImGuiKey key) {
  switch (key) {
    case ImGuiKey_Tab:
      return "Tab";
    case ImGuiKey_LeftArrow:
      return "Left";
    case ImGuiKey_RightArrow:
      return "Right";
    case ImGuiKey_UpArrow:
      return "Up";
    case ImGuiKey_DownArrow:
      return "Down";
    case ImGuiKey_PageUp:
      return "PageUp";
    case ImGuiKey_PageDown:
      return "PageDown";
    case ImGuiKey_Home:
      return "Home";
    case ImGuiKey_End:
      return "End";
    case ImGuiKey_Insert:
      return "Insert";
    case ImGuiKey_Delete:
      return "Delete";
    case ImGuiKey_Backspace:
      return "Backspace";
    case ImGuiKey_Space:
      return "Space";
    case ImGuiKey_Enter:
      return "Return";
    case ImGuiKey_Escape:
      return "Escape";
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt:
      // Modifiers prefix a binding ("Shift+Up"); on their own they can never fire, so ReXGlue
      // never names them as a binding.
      return nullptr;
    case ImGuiKey_Apostrophe:
      return "Quote";
    case ImGuiKey_Comma:
      return "Comma";
    case ImGuiKey_Minus:
      return "Minus";
    case ImGuiKey_Period:
      return "Period";
    case ImGuiKey_Slash:
      return "Slash";
    case ImGuiKey_Semicolon:
      return "Semicolon";
    case ImGuiKey_Equal:
      return "Plus";
    case ImGuiKey_LeftBracket:
      return "LBracket";
    case ImGuiKey_Backslash:
      return "Backslash";
    case ImGuiKey_RightBracket:
      return "RBracket";
    case ImGuiKey_GraveAccent:
      return "Backtick";
    case ImGuiKey_CapsLock:
      return "CapsLock";
    case ImGuiKey_ScrollLock:
      return "ScrollLock";
    case ImGuiKey_NumLock:
      return "NumLock";
    case ImGuiKey_PrintScreen:
      return "PrintScreen";
    case ImGuiKey_Pause:
      return "Pause";
    case ImGuiKey_KeypadDivide:
      return "NumpadSlash";
    case ImGuiKey_KeypadMultiply:
      return "NumpadStar";
    case ImGuiKey_KeypadSubtract:
      return "NumpadMinus";
    case ImGuiKey_KeypadAdd:
      return "NumpadPlus";
    case ImGuiKey_KeypadEnter:
      return "NumpadEnter";
    default:
      break;
  }
  if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
    static constexpr const char* kDigits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    return kDigits[key - ImGuiKey_0];
  }
  if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
    static constexpr const char* kLetters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                               "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    return kLetters[key - ImGuiKey_A];
  }
  if (key >= ImGuiKey_F1 && key <= ImGuiKey_F24) {
    static constexpr const char* kFunction[] = {"F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",
                                                "F9",  "F10", "F11", "F12", "F13", "F14", "F15", "F16",
                                                "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
    return kFunction[key - ImGuiKey_F1];
  }
  if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
    static constexpr const char* kNumpad[] = {"Numpad0", "Numpad1", "Numpad2", "Numpad3", "Numpad4",
                                              "Numpad5", "Numpad6", "Numpad7", "Numpad8", "Numpad9"};
    return kNumpad[key - ImGuiKey_Keypad0];
  }
  return nullptr;
}

// Whether the port handles this key itself or keeps it for the TekRant ports' shared hotkeys. Such a key may not be a
// gameplay binding, so a press does not reach the game and the port action at once. Live listeners: F2 frame counter,
// F3 debug overlay, F4 settings overlay, F7 achievements, F11 fullscreen, Backtick console. Reserved without a
// listener: F10 (no cheats in this port yet), F12 and Escape.
inline bool KeyboardBindingIsReserved(ImGuiKey key) {
  switch (key) {
    case ImGuiKey_Escape:
    case ImGuiKey_F2:
    case ImGuiKey_F3:
    case ImGuiKey_F4:
    case ImGuiKey_F7:
    case ImGuiKey_F10:
    case ImGuiKey_F11:
    case ImGuiKey_F12:
    case ImGuiKey_GraveAccent:
      return true;
    default:
      return false;
  }
}

// The binding to store for a captured key, e.g. "Space" or "Shift+Up". Empty when the key cannot be
// bound: a mouse or gamepad key, a key ReXGlue does not name, a bare modifier, or a reserved key.
inline std::string KeyboardBindingFor(ImGuiKey key, bool shift, bool ctrl, bool alt) {
  const char* name = KeyboardKeyName(key);
  if (!name || KeyboardBindingIsReserved(key)) {
    return {};
  }
  std::string binding;
  if (shift) {
    binding += "Shift+";
  }
  if (ctrl) {
    binding += "Ctrl+";
  }
  if (alt) {
    binding += "Alt+";
  }
  binding += name;
  return binding;
}

// A readable form of a stored binding: "Semicolon,Space" -> "Semicolon or Space".
inline std::string KeyboardBindingLabel(std::string_view binding) {
  if (binding.empty()) {
    return "None";
  }
  std::string label;
  std::size_t start = 0;
  for (;;) {
    const std::string_view rest = binding.substr(start);
    const std::size_t comma = rest.find(',');
    const std::string_view part = keybind_detail::Trim(rest.substr(0, comma));
    if (!label.empty()) {
      label += " or ";
    }
    label += part;
    if (comma == std::string_view::npos) {
      break;
    }
    start += comma + 1;
  }
  return label;
}

namespace keybind_detail {

// Whether 'name' is a key ReXGlue names and the port does not reserve.
inline bool NameIsBindable(std::string_view name) {
  for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_GamepadStart; ++k) {
    const ImGuiKey key = static_cast<ImGuiKey>(k);
    const char* known = KeyboardKeyName(key);
    if (known && name == known) {
      return !KeyboardBindingIsReserved(key);
    }
  }
  return false;
}

// Whether one comma-separated part is a named, unreserved key with known modifier prefixes. A part
// that is only a modifier, or carries a prefix ReXGlue does not recognise, is refused.
inline bool BindingPartIsValid(std::string_view part) {
  for (;;) {
    const std::size_t plus = part.find('+');
    if (plus == std::string_view::npos || plus == 0) {
      break;
    }
    const std::string_view head = part.substr(0, plus);
    if (head != "Shift" && head != "Ctrl" && head != "Control" && head != "Alt") {
      break;
    }
    part.remove_prefix(plus + 1);
  }
  return NameIsBindable(part);
}

}  // namespace keybind_detail

// Whether every part of a stored binding is a key ReXGlue names and none is reserved. An empty or
// partly unusable binding is invalid.
inline bool KeyboardBindingIsValid(std::string_view binding) {
  bool any = false;
  std::size_t start = 0;
  for (;;) {
    const std::string_view rest = binding.substr(start);
    const std::size_t comma = rest.find(',');
    const std::string_view part = keybind_detail::Trim(rest.substr(0, comma));
    if (part.empty() || !keybind_detail::BindingPartIsValid(part)) {
      return false;
    }
    any = true;
    if (comma == std::string_view::npos) {
      break;
    }
    start += comma + 1;
  }
  return any;
}

// What the launcher shows and edits for an action: the stored value, or the default when the stored
// value is missing or not something ReXGlue can use. Edits stay pending in the store until Save or Play.
inline std::string SavedKeyboardBinding(const trg::SettingsStore& settings, const KeyboardAction& action) {
  const std::string saved = settings.Get(action.key);
  return KeyboardBindingIsValid(saved) ? saved : std::string(action.default_binding);
}

// Puts every keyboard control back to its pinned ReXGlue default, and nothing else.
inline void ResetKeyboardDefaults(trg::SettingsStore& settings) {
  for (const KeyboardAction& action : kKeyboardActions) {
    settings.Set(action.key, action.default_binding);
  }
}

}  // namespace mua
