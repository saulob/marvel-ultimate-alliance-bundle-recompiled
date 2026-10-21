#pragma once

#include <functional>

#include <rex/input/device.h>
#include <rex/input/input.h>

namespace rex::input {

#if MUA_INPUT_ACTIVITY_CALLBACK
// A passive hook added by patches/rexglue-sdk/0047-input-activity-callback.patch.
// Called on the input polling thread when a device's state changes.
void SetInputActivityCallback(
    std::function<void(DeviceId device, const X_INPUT_GAMEPAD& gamepad)> callback);
#else
inline void SetInputActivityCallback(std::function<void(DeviceId, const X_INPUT_GAMEPAD&)>) {}
#endif

}  // namespace rex::input
