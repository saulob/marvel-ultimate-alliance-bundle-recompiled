#include "input_summary.h"

#include <SDL3/SDL_gamepad.h>

namespace mua {

std::string ControllerSummary() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (ids == nullptr) {
    return {};
  }
  std::string summary;
  for (int i = 0; i < count; ++i) {
    const char* name = SDL_GetGamepadNameForID(ids[i]);
    if (name == nullptr || name[0] == '\0') {
      continue;
    }
    if (!summary.empty()) {
      summary += ", ";
    }
    summary += name;
  }
  SDL_free(ids);
  return summary;
}

}  // namespace mua
