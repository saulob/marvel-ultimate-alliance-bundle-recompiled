#pragma once

#include <filesystem>

namespace mua {

// The log file this run is writing: the configured `log_file`, or the target of the logger's file
// sink. Empty when file logging is not active. Crash reporting caches this path at startup so a
// crash always appends to the current run's log, never to an older one.
std::filesystem::path ActiveLogFile();

}  // namespace mua
