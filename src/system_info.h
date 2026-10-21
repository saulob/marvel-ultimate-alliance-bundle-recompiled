#pragma once

#include <string>

namespace mua {

// Non-identifying OS description, for the startup diagnostics.
std::string OsDescription();

// CPU model and logical processor count.
std::string CpuDescription();
int LogicalProcessorCount();

const char* BuildConfigurationName();

// Starts the uptime clock. Call once early in the process.
void InitializeProcessUptime();

// HH:MM:SS since InitializeProcessUptime.
std::string ProcessUptimeText();

}  // namespace mua
