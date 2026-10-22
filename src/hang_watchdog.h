#pragma once

#include <filesystem>
#include <vector>

namespace mua {

struct FrameStatsSummary {
  double average_fps = 0.0;
  double low_1_percent_fps = 0.0;
  double worst_ms = 0.0;
  size_t frames_over_50ms = 0;
  size_t frames_over_100ms = 0;
};

// Summarizes a window of per-frame times (milliseconds, any order). Empty input gives a default-constructed
// summary. Exposed for direct testing; RecordFrameStats (hang_watchdog.cpp) is the only real caller.
FrameStatsSummary SummarizeFrameTimesMs(std::vector<double> times_ms);

// Counts a frame the title presents (called from the VdSwap hook), and feeds the frame pacing log below.
void NoteGuestFrame();

// Starts a thread that watches the window (native handle) and the title's frames. When the window stops answering
// messages, or the title stops producing frames, for several seconds, it writes the call stack of every thread to
// <logs_folder>/hang_<n>.txt and logs the path. Windows only; a no-op elsewhere.
void StartHangWatchdog(void* native_window, const std::filesystem::path& logs_folder);

}  // namespace mua
