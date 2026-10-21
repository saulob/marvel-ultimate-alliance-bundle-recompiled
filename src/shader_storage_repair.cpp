#include "shader_storage_repair.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

namespace mua {
namespace {

std::vector<std::uint8_t> ReadAll(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

std::uint64_t Load64(const std::uint8_t* p) {
  std::uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}

std::uint32_t Load32(const std::uint8_t* p) {
  std::uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}

// Storage files written by the runtime (host byte order):
//   .xsh:  'XESH', version, then per shader {u64 hash, u32 dword count | type << 31, ucode}
//   .xpso: 'XEPS', 'DXRT'/'DXRO', version, then 72-byte {u64 hash, description} records
constexpr std::uint32_t kShaderMagic = 0x48534558;
constexpr std::uint32_t kPipelineMagic = 0x53504558;
constexpr std::size_t kPipelineRecord = 72;

struct Records {
  std::size_t header = 0;
  std::vector<std::pair<std::size_t, std::size_t>> spans;  // offset, size
  std::size_t damaged = 0;
};

bool Parse(const std::vector<std::uint8_t>& d, Records& out) {
  if (d.size() < 8) {
    return false;
  }
  const std::uint32_t magic = Load32(d.data());
  if (magic == kShaderMagic) {
    out.header = 8;
    for (std::size_t at = 8; at + 12 <= d.size();) {
      const std::size_t size = 12 + std::size_t(Load32(&d[at + 8]) & 0x7FFFFFFF) * 4;
      if (at + size > d.size()) {
        ++out.damaged;
        break;
      }
      if (XXH3_64bits(&d[at + 12], size - 12) == Load64(&d[at])) {
        out.spans.push_back({at, size});
      } else {
        ++out.damaged;
      }
      at += size;
    }
    return true;
  }
  if (magic == kPipelineMagic && d.size() >= 12) {
    out.header = 12;
    for (std::size_t at = 12; at + kPipelineRecord <= d.size(); at += kPipelineRecord) {
      if (XXH3_64bits(&d[at + 8], kPipelineRecord - 8) == Load64(&d[at])) {
        out.spans.push_back({at, kPipelineRecord});
      } else {
        ++out.damaged;
      }
    }
    if ((d.size() - 12) % kPipelineRecord) {
      ++out.damaged;
    }
    return true;
  }
  return false;
}

bool WriteRecords(const std::filesystem::path& path, const std::vector<std::uint8_t>& d, const Records& r) {
  std::vector<std::uint8_t> out(d.begin(), d.begin() + std::ptrdiff_t(r.header));
  for (const auto& [at, size] : r.spans) {
    out.insert(out.end(), d.begin() + std::ptrdiff_t(at), d.begin() + std::ptrdiff_t(at + size));
  }
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
  return bool(f);
}

}  // namespace

std::vector<ShaderStorageRepair> RepairShaderStorage(const std::filesystem::path& cache_dir) {
  namespace fs = std::filesystem;
  std::vector<ShaderStorageRepair> repaired;
  std::error_code ec;
  for (const char* sub : {"shareable", "local"}) {
    const fs::path dir = cache_dir / "shaders" / sub;
    if (!fs::is_directory(dir, ec)) {
      continue;
    }
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
      const std::string ext = entry.path().extension().string();
      if (ext != ".xsh" && ext != ".xpso") {
        continue;
      }
      const std::vector<std::uint8_t> data = ReadAll(entry.path());
      Records r;
      if (!Parse(data, r) || !r.damaged) {
        continue;
      }
      if (WriteRecords(entry.path(), data, r)) {
        repaired.push_back({entry.path().filename().string(), r.damaged, r.spans.size()});
      }
    }
  }
  return repaired;
}

}  // namespace mua
