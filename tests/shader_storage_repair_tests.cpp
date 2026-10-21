// Shader storage repair: damaged records are dropped, whole records after them are kept, other files are untouched.
// Temporary folders only.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif
#include <xxhash.h>

#include "shader_storage_repair.h"

namespace fs = std::filesystem;
using namespace mua;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

using Bytes = std::vector<std::uint8_t>;

const fs::path kRoot = fs::temp_directory_path() / "mua_shader_storage_repair_tests";

void Put32(Bytes& out, std::uint32_t v) {
  const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
  out.insert(out.end(), p, p + 4);
}

void Put64(Bytes& out, std::uint64_t v) {
  const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
  out.insert(out.end(), p, p + 8);
}

Bytes ShaderRecord(std::uint32_t seed, std::uint32_t dwords, bool damaged = false) {
  Bytes ucode;
  for (std::uint32_t i = 0; i < dwords; ++i) {
    Put32(ucode, seed * 31 + i);
  }
  Bytes record;
  Put64(record, XXH3_64bits(ucode.data(), ucode.size()) ^ (damaged ? 1 : 0));
  Put32(record, dwords);
  record.insert(record.end(), ucode.begin(), ucode.end());
  return record;
}

Bytes PipelineRecord(std::uint8_t seed, bool damaged = false) {
  Bytes description(64, seed);
  Bytes record;
  Put64(record, XXH3_64bits(description.data(), description.size()) ^ (damaged ? 1 : 0));
  record.insert(record.end(), description.begin(), description.end());
  return record;
}

Bytes Join(std::initializer_list<Bytes> parts) {
  Bytes out;
  for (const Bytes& part : parts) {
    out.insert(out.end(), part.begin(), part.end());
  }
  return out;
}

void Write(const fs::path& file, const Bytes& data) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

Bytes Read(const fs::path& file) {
  std::ifstream f(file, std::ios::binary);
  return Bytes((std::istreambuf_iterator<char>(f)), {});
}

Bytes ShaderHeader() {
  Bytes header;
  Put32(header, 0x48534558);
  Put32(header, 0x20201219);
  return header;
}

Bytes PipelineHeader() {
  Bytes header;
  Put32(header, 0x53504558);
  Put32(header, 0x54525844);
  Put32(header, 1);
  return header;
}

void TestShaderFile() {
  std::printf("shader storage keeps whole records after a damaged one\n");
  const fs::path file = kRoot / "shaders" / "shareable" / "415607DA.xsh";
  const Bytes a = ShaderRecord(1, 4), b = ShaderRecord(2, 3, true), c = ShaderRecord(3, 5);
  Bytes cut = ShaderRecord(4, 6);
  cut.resize(cut.size() - 7);
  Write(file, Join({ShaderHeader(), a, b, c, cut}));
  const std::vector<ShaderStorageRepair> repaired = RepairShaderStorage(kRoot);
  CHECK(repaired.size() == 1);
  CHECK(!repaired.empty() && repaired[0].file == "415607DA.xsh" && repaired[0].dropped == 2 && repaired[0].kept == 2);
  CHECK(Read(file) == Join({ShaderHeader(), a, c}));
}

void TestPipelineFile() {
  std::printf("pipeline storage drops a damaged and a partly written record\n");
  const fs::path file = kRoot / "shaders" / "shareable" / "415607DA.rov.d3d12.xpso";
  const Bytes a = PipelineRecord(1), b = PipelineRecord(2, true), c = PipelineRecord(3);
  Write(file, Join({PipelineHeader(), a, b, c, Bytes(10, 0xCD)}));
  const std::vector<ShaderStorageRepair> repaired = RepairShaderStorage(kRoot);
  CHECK(repaired.size() == 1);
  CHECK(!repaired.empty() && repaired[0].dropped == 2 && repaired[0].kept == 2);
  CHECK(Read(file) == Join({PipelineHeader(), a, c}));
}

void TestUntouched() {
  std::printf("whole storage files and other files are left alone\n");
  const fs::path good = kRoot / "shaders" / "local" / "415607DA.xsh";
  const fs::path other = kRoot / "shaders" / "shareable" / "notes.bin";
  const fs::path unknown = kRoot / "shaders" / "shareable" / "foreign.xsh";
  const Bytes good_data = Join({ShaderHeader(), ShaderRecord(5, 2), ShaderRecord(6, 2)});
  const Bytes other_data = Join({ShaderHeader(), ShaderRecord(7, 2, true)});
  const Bytes unknown_data(40, 0x11);
  Write(good, good_data);
  Write(other, other_data);
  Write(unknown, unknown_data);
  CHECK(RepairShaderStorage(kRoot).empty());
  CHECK(Read(good) == good_data);
  CHECK(Read(other) == other_data);
  CHECK(Read(unknown) == unknown_data);
  CHECK(RepairShaderStorage(kRoot / "missing").empty());
}

}  // namespace

int main() {
  std::error_code ec;
  fs::remove_all(kRoot, ec);
  TestShaderFile();
  TestPipelineFile();
  TestUntouched();
  fs::remove_all(kRoot, ec);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
