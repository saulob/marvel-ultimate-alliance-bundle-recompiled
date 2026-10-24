#pragma once

#include <functional>
#include <string>

namespace mua::visual_trace {

// DEBUG_SAULO: rolling history of startup and menu transitions, dumped to the log when F9 is pressed.

void Event(const char* kind, const std::string& detail = {});

// One genuine guest present.
void NotePresent();

// The state shown as the current state by every marker; read from the thread that presses F9.
void SetStateProvider(std::function<std::string()> provider);

// F9: logs the buffered history of the last five seconds and the current state.
void Marker();

}  // namespace mua::visual_trace
