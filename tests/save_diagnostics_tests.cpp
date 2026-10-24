// The save diagnostics hook wrappers that persist through ReXGlue's profile settings and content manager, and parse
// the profile read buffer by its XUSER_PROFILE_SETTING layout. Reads the generated code; skipped when it is not there.
// Run: mua_save_diagnostics_tests

#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "save_diagnostics.h"

namespace fs = std::filesystem;

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

std::string Function(const fs::path& directory, const std::string& name) {
  const std::string marker = "DEFINE_REX_FUNC(" + name + ")";
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.path().extension() != ".cpp" || entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    const std::string all = text.str();
    const size_t start = all.find(marker);
    if (start != std::string::npos) {
      const size_t end = all.find("\n}\n", start);
      return all.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
  }
  return {};
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void StoreBE32(std::vector<uint8_t>& memory, uint32_t address, uint32_t value) {
  for (int i = 0; i < 4; ++i) memory[address + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
}

void TestProfileBuffer() {
  std::printf("the profile read buffer is parsed by its entry layout\n");
  std::vector<uint8_t> memory(0x400, 0);
  const uint32_t buffer = 0x100;
  StoreBE32(memory, buffer, 2);
  StoreBE32(memory, buffer + 4, buffer + mua::kProfileHeaderSize);
  const uint32_t first = buffer + mua::kProfileHeaderSize;
  const uint32_t second = first + mua::kProfileSettingSize;
  StoreBE32(memory, first, 1);
  StoreBE32(memory, first + 16, 0x10040003);
  StoreBE32(memory, second, 2);
  StoreBE32(memory, second + 16, 0x63E83FFD);
  memory[second + 24] = mua::kProfileTypeBinary;
  StoreBE32(memory, second + 32, 56);

  mua::ProfileSettingView view;
  CHECK(mua::ReadProfileSetting(memory.data(), buffer, 0, view));
  CHECK(view.id == 0x10040003 && view.source == 1 && !view.title_specific);
  CHECK(mua::ReadProfileSetting(memory.data(), buffer, 1, view));
  CHECK(view.id == 0x63E83FFD && view.source == 2 && view.type == 6 && view.size == 56 && view.title_specific);
  CHECK(!mua::ReadProfileSetting(memory.data(), buffer, 2, view));
  StoreBE32(memory, buffer + 4, buffer + 12);
  CHECK(!mua::ReadProfileSetting(memory.data(), buffer, 0, view));

  std::printf("the content file name is bounded to the XCONTENT_DATA field\n");
  std::vector<uint8_t> data(512, 'A');
  CHECK(mua::ContentFileName(data.data(), 0).size() == 42);
  std::memcpy(data.data() + 264, "slot3", 6);
  CHECK(mua::ContentFileName(data.data(), 0) == "slot3");
}

void TestGeneratedStorage(const fs::path& generated) {
  std::printf("the profile wrappers pass the settings straight to the XAM imports\n");
  const std::string read = Function(generated, "sub_824CB818");
  CHECK(Has(read, "__imp__XamUserReadProfileSettings(ctx, base);"));
  const std::string write = Function(generated, "sub_824CB880");
  CHECK(Has(write, "__imp__XamUserWriteProfileSettings(ctx, base);"));

  std::printf("the profile read asks for the title-specific settings ReXGlue registers\n");
  const std::string reader = Function(generated, "sub_8236A588");
  CHECK(Has(reader, "ctx.r22.u64 = ctx.r11.u64 | 16381;"));
  CHECK(Has(reader, "ctx.r11.s64 = 1676148736;"));
  CHECK(Has(reader, "ctx.cr6.compare<uint32_t>(ctx.r3.u32, 122, "));

  std::printf("the profile writer stores binary title settings in chunks\n");
  const std::string writer = Function(generated, "sub_8236A970");
  CHECK(Has(writer, "sub_824CB880(ctx, base);"));
  CHECK(Has(writer, "ctx.r9.u64 = ctx.r9.u64 | 16381;"));

  std::printf("saves use XamContentCreateEx with create-always and open-existing\n");
  CHECK(Has(Function(generated, "sub_8247EDF0"), "__imp__XamContentCreateEx(ctx, base);"));
  CHECK(Has(Function(generated, "sub_8247EE58"), "__imp__XamContentClose(ctx, base);"));
  CHECK(Has(Function(generated, "sub_8247EE60"), "__imp__XamContentCreateEnumerator(ctx, base);"));
  CHECK(Has(Function(generated, "sub_8247F6C8"), "__imp__XamShowDeviceSelectorUI(ctx, base);"));
  const std::string save = Function(generated, "sub_82234D40");
  CHECK(Has(save, "ctx.r6.s64 = 2;"));
  CHECK(Has(save, "sub_8247EEF0(ctx, base);"));
  const std::string load = Function(generated, "sub_82234A60");
  CHECK(Has(load, "ctx.r6.s64 = 3;"));
  CHECK(Has(load, "sub_8247EEF0(ctx, base);"));
}

void TestGeneratedStorageMua1(const fs::path& generated) {
  std::printf("the MUA 1 wrappers are the same XAM imports at their own addresses\n");
  CHECK(Has(Function(generated, "sub_824B25C8"), "__imp__XamUserReadProfileSettings(ctx, base);"));
  CHECK(Has(Function(generated, "sub_824B2630"), "__imp__XamUserWriteProfileSettings(ctx, base);"));
  CHECK(Has(Function(generated, "sub_82466A18"), "__imp__XamContentCreateEx(ctx, base);"));
  CHECK(Has(Function(generated, "sub_82466A80"), "__imp__XamContentClose(ctx, base);"));
  CHECK(Has(Function(generated, "sub_82466A88"), "__imp__XamContentCreateEnumerator(ctx, base);"));
  CHECK(Has(Function(generated, "sub_82467240"), "__imp__XamShowDeviceSelectorUI(ctx, base);"));
}

void TestGeneratedStorageMua2(const fs::path& generated) {
  std::printf("the MUA 2 wrappers are the same XAM imports at their own addresses\n");
  CHECK(Has(Function(generated, "sub_82A4EF08"), "__imp__XamUserReadProfileSettings(ctx, base);"));
  CHECK(Has(Function(generated, "sub_82A4EF70"), "__imp__XamUserWriteProfileSettings(ctx, base);"));
  CHECK(Has(Function(generated, "sub_829AC488"), "__imp__XamContentCreateEx(ctx, base);"));
  CHECK(Has(Function(generated, "sub_829AC518"), "__imp__XamContentClose(ctx, base);"));
  CHECK(Has(Function(generated, "sub_829AC528"), "__imp__XamContentCreateEnumerator(ctx, base);"));
  CHECK(Has(Function(generated, "sub_829ACA80"), "__imp__XamShowDeviceSelectorUI(ctx, base);"));

  std::printf("the MUA 2 profile read asks for the same title-specific settings\n");
  const std::string reader = Function(generated, "sub_8293EF70");
  CHECK(Has(reader, "ctx.r22.u64 = ctx.r9.u64 | 16381;") || Has(reader, "| 16381;"));
  CHECK(Has(reader, "ctx.r9.s64 = 1676148736;") || Has(reader, "1676148736"));
  CHECK(Has(reader, "sub_82A4EF08(ctx, base);"));
}

}  // namespace

int Run() {
  TestProfileBuffer();
  const fs::path generated = MUA_GENERATED_DIR;
  if (!fs::exists(generated / "marvel_ultimate_alliance_recomp.33.cpp")) {
    std::printf("skipped generated checks: generated code is not there (%s)\n", generated.string().c_str());
  } else {
    TestGeneratedStorage(generated);
  }
  const fs::path mua1 = MUA_GENERATED_DIR_MUA1;
  if (fs::exists(mua1 / "marvel_ultimate_alliance_recomp.33.cpp")) {
    TestGeneratedStorageMua1(mua1);
  }
  const fs::path mua2 = MUA_GENERATED_DIR_MUA2;
  if (fs::exists(mua2 / "marvel_ultimate_alliance_recomp.33.cpp")) {
    TestGeneratedStorageMua2(mua2);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

int main() {
  try {
    return Run();
  } catch (const std::exception& error) {
    std::printf("  FAIL exception: %s\n", error.what());
    return 1;
  }
}
