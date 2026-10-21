#include "system_info.h"

#include <chrono>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#if defined(_M_X64) || defined(_M_IX86)
#include <intrin.h>
#endif
#else
#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#endif

#if defined(MUA_BUILD_CONFIG)
#define MUA_SYSTEM_BUILD_CONFIG MUA_BUILD_CONFIG
#else
#define MUA_SYSTEM_BUILD_CONFIG "unknown"
#endif

namespace mua {
namespace {

std::chrono::steady_clock::time_point g_process_start = std::chrono::steady_clock::now();

std::string Trim(std::string text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

#if defined(_WIN32)

std::string WindowsArchitecture() {
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  switch (info.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "64-bit";
    case PROCESSOR_ARCHITECTURE_INTEL:
    case PROCESSOR_ARCHITECTURE_ARM:
      return "32-bit";
    default:
      return sizeof(void*) == 8 ? "64-bit" : "32-bit";
  }
}

std::string CpuBrand() {
#if defined(_M_X64) || defined(_M_IX86)
  int registers[4]{};
  __cpuid(registers, 0x80000000);
  if (registers[0] < static_cast<int>(0x80000004)) {
    return {};
  }
  char brand[49]{};
  for (int leaf = 0; leaf < 3; ++leaf) {
    __cpuid(registers, 0x80000002 + leaf);
    for (int word = 0; word < 4; ++word) {
      const int offset = (leaf * 4 + word) * 4;
      brand[offset + 0] = char(registers[word] & 0xFF);
      brand[offset + 1] = char((registers[word] >> 8) & 0xFF);
      brand[offset + 2] = char((registers[word] >> 16) & 0xFF);
      brand[offset + 3] = char((registers[word] >> 24) & 0xFF);
    }
  }
  return Trim(brand);
#else
  return {};
#endif
}

#else

std::string LinuxDistribution() {
  std::ifstream in("/etc/os-release");
  std::string line;
  while (std::getline(in, line)) {
    constexpr const char* kKey = "PRETTY_NAME=";
    if (line.rfind(kKey, 0) != 0) {
      continue;
    }
    std::string value = line.substr(std::string(kKey).size());
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    return value;
  }
  return {};
}

std::string LinuxCpuBrand() {
  std::ifstream in("/proc/cpuinfo");
  std::string line;
  while (std::getline(in, line)) {
    constexpr const char* kKey = "model name";
    if (line.rfind(kKey, 0) != 0) {
      continue;
    }
    const size_t colon = line.find(':');
    return colon == std::string::npos ? std::string() : Trim(line.substr(colon + 1));
  }
  return {};
}

#endif

}  // namespace

std::string OsDescription() {
#if defined(_WIN32)
  struct VersionInfo {
    ULONG size;
    ULONG major;
    ULONG minor;
    ULONG build;
    ULONG platform;
    wchar_t service_pack[128];
  };
  VersionInfo version{};
  version.size = sizeof(version);
  using RtlGetVersionFn = LONG(WINAPI*)(VersionInfo*);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto rtl_get_version =
      ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
  if (rtl_get_version != nullptr) {
    rtl_get_version(&version);
  }
  std::string name = "Windows";
  if (version.major == 10 && version.build >= 22000) {
    name = "Windows 11";
  } else if (version.major == 10) {
    name = "Windows 10";
  } else if (version.major != 0) {
    name = "Windows " + std::to_string(version.major);
  }
  name += " " + WindowsArchitecture();
  if (version.build != 0) {
    name += " (build " + std::to_string(version.build) + ")";
  }
  return name;
#else
  utsname info{};
  const std::string distribution = LinuxDistribution();
  std::string name = distribution.empty() ? "Linux" : "Linux " + distribution;
  if (uname(&info) == 0 && info.release[0] != '\0') {
    name += " (kernel " + std::string(info.release) + ")";
  }
  return name;
#endif
}

std::string CpuDescription() {
#if defined(_WIN32)
  const std::string brand = CpuBrand();
  return brand.empty() ? "unknown" : brand;
#else
  const std::string brand = LinuxCpuBrand();
  return brand.empty() ? "unknown" : brand;
#endif
}

int LogicalProcessorCount() {
#if defined(_WIN32)
  const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (count != 0) {
    return int(count);
  }
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  return int(info.dwNumberOfProcessors);
#else
  const long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? int(count) : 0;
#endif
}

const char* BuildConfigurationName() {
  return MUA_SYSTEM_BUILD_CONFIG;
}

void InitializeProcessUptime() {
  g_process_start = std::chrono::steady_clock::now();
}

std::string ProcessUptimeText() {
  const auto elapsed = std::chrono::steady_clock::now() - g_process_start;
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
  const long long hours = seconds / 3600;
  const long long minutes = (seconds / 60) % 60;
  const long long remainder = seconds % 60;
  char text[32];
  std::snprintf(text, sizeof(text), "%02lld:%02lld:%02lld", hours, minutes, remainder);
  return text;
}

}  // namespace mua
