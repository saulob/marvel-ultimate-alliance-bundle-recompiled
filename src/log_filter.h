#pragma once

#include <string_view>

#include <spdlog/spdlog.h>

namespace mua {

// Minimal mode keeps warnings, errors and the first-line startup diagnostics; it drops the ordinary
// runtime chatter. Pure, so the classification can be tested without a logger.
bool ShouldLogInMinimalMode(spdlog::level::level_enum level, std::string_view category,
                            std::string_view message);

// The `detailed_logging` setting in effect now: true unless it is explicitly off. A missing or
// malformed value means Detailed logging.
bool DetailedLoggingEnabled();

// Adds the Minimal filter to the file sink, or removes it, to match the setting. Call it again
// after the launcher applies the player's choice; the runtime is not started while it changes.
void ApplyDetailedLogging();

bool MinimalLogFilterInstalled();

// Drops the Minimal filter and the reference it holds to the file sink, so the sink's file can be
// closed by ShutdownLogging. Only used on the way out.
void ReleaseMinimalLogFilter();

// The sink a logger actually writes to, looking through the log filters.
spdlog::sink_ptr UnwrapLogSink(const spdlog::sink_ptr& sink);

}  // namespace mua
