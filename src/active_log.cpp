#include "active_log.h"

#include <spdlog/sinks/rotating_file_sink.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "log_filter.h"

namespace mua {
namespace {

std::filesystem::path FileSinkTarget(const spdlog::logger& logger) {
  for (const spdlog::sink_ptr& sink : logger.sinks()) {
    if (auto* file = dynamic_cast<spdlog::sinks::rotating_file_sink_mt*>(UnwrapLogSink(sink).get())) {
      return std::filesystem::path(file->filename());
    }
  }
  return {};
}

}  // namespace

std::filesystem::path ActiveLogFile() {
  const std::string configured = rex::cvar::GetFlagByName("log_file");
  if (!configured.empty()) {
    return std::filesystem::path(std::u8string(configured.begin(), configured.end()));
  }
  for (const rex::LogCategoryEntry& entry : rex::GetAllCategories()) {
    if (!entry.logger) {
      continue;
    }
    if (std::filesystem::path target = FileSinkTarget(*entry.logger); !target.empty()) {
      return target;
    }
  }
  if (const spdlog::logger* core = rex::GetLoggerRaw(rex::log::core()); core != nullptr) {
    return FileSinkTarget(*core);
  }
  return {};
}

}  // namespace mua
