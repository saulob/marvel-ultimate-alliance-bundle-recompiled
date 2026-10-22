// The achievements each game's XDBF lists, and the imports the games reach them through.
// Run: mua_achievement_metadata_tests

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

uint32_t Be32(const std::string& data, size_t at) {
  return (uint32_t(uint8_t(data[at])) << 24) | (uint32_t(uint8_t(data[at + 1])) << 16) |
         (uint32_t(uint8_t(data[at + 2])) << 8) | uint32_t(uint8_t(data[at + 3]));
}

uint16_t Be16(const std::string& data, size_t at) {
  return uint16_t((uint8_t(data[at]) << 8) | uint8_t(data[at + 1]));
}

struct Table {
  std::vector<uint16_t> ids;
  uint32_t gamerscore = 0;
};

constexpr uint16_t kMetadataNamespace = 1;
constexpr uint32_t kAchievementTableId = 0x58414348;
constexpr size_t kXdbfHeaderSize = 24;
constexpr size_t kEntrySize = 18;
constexpr size_t kTableHeaderSize = 14;
constexpr size_t kTableEntrySize = 36;

bool ParseAchievementTable(const std::string& image, Table& table) {
  const size_t xdbf = image.find("XDBF");
  if (xdbf == std::string::npos) return false;
  const uint32_t entry_capacity = Be32(image, xdbf + 8);
  const uint32_t entry_count = Be32(image, xdbf + 12);
  const uint32_t free_capacity = Be32(image, xdbf + 16);
  const size_t data = xdbf + kXdbfHeaderSize + size_t(entry_capacity) * kEntrySize + size_t(free_capacity) * 8;
  for (uint32_t i = 0; i < entry_count; ++i) {
    const size_t entry = xdbf + kXdbfHeaderSize + size_t(i) * kEntrySize;
    if (Be16(image, entry) != kMetadataNamespace || Be32(image, entry + 6) != kAchievementTableId) continue;
    const size_t at = data + Be32(image, entry + 10);
    const uint16_t count = Be16(image, at + 12);
    for (uint16_t n = 0; n < count; ++n) {
      const size_t row = at + kTableHeaderSize + size_t(n) * kTableEntrySize;
      table.ids.push_back(Be16(image, row));
      table.gamerscore += Be16(image, row + 12);
    }
    return true;
  }
  return false;
}

void TestTables() {
  std::printf("each game's own XDBF lists its own achievements\n");
  const fs::path folder = fs::path(MUA_SRC_DIR) / ".." / "game" / "analysis";
  struct Expected {
    const char* image;
    size_t count;
    uint32_t gamerscore;
  };
  for (const Expected& expected : {Expected{"mua1.image.bin", 46, 1000}, Expected{"default.image.bin", 58, 1250},
                                   Expected{"mua2.image.bin", 50, 1000}}) {
    const std::string image = Read(folder / expected.image);
    if (image.empty()) {
      std::printf("  skipped %s: not extracted\n", expected.image);
      continue;
    }
    Table table;
    CHECK(ParseAchievementTable(image, table));
    CHECK(table.ids.size() == expected.count);
    CHECK(table.gamerscore == expected.gamerscore);
  }
}

void TestImports() {
  std::printf("no game opens the dashboard achievements, and each reports unlocks through the XGI message\n");
  const fs::path generated = fs::path(MUA_GENERATED_ROOT);
  for (const char* key : {"mua1", "mua1-gold", "mua2"}) {
    const std::string registry = Read(generated / key / "marvel_ultimate_alliance_register.cpp");
    CHECK(!registry.empty());
    CHECK(registry.find("__imp__XamShowAchievementsUI") == std::string::npos);
    CHECK(registry.find("__imp__XamUserWriteAchievements") == std::string::npos);
    CHECK(registry.find("__imp__XMsgStartIORequest,") != std::string::npos ||
          registry.find("__imp__XMsgStartIORequest)") != std::string::npos);
  }
}

}  // namespace

int main() {
  TestTables();
  TestImports();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
