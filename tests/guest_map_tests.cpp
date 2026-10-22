// The two guest maps must carry the same hooks, so a hook added for one game cannot be left out of the other, and the
// present callback's argument must release the swap at once in both.
// Run: mua_guest_map_tests

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#pragma clang diagnostic ignored "-Wmacro-redefined"
namespace gold {
#include "guest_map_mua1_gold.h"
}
namespace mua1 {
#include "guest_map_mua1.h"
}

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

std::string ReadSource(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

std::set<std::string> Matches(const std::string& text, const char* pattern) {
  std::set<std::string> found;
  const std::regex re(pattern);
  for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
    found.insert((*it)[1].str());
  }
  return found;
}

const std::filesystem::path kSources = MUA_SRC_DIR;

// sub_8248A0A0 holds the swap for another vblank while the interval in the high half of its argument has not passed
// since the last flip, or while the scanline has passed the threshold in the low half.
bool Mua1CallbackHolds(uint32_t argument, uint32_t vblanks_since_flip, uint32_t scanline_percent) {
  const int64_t remaining = int64_t(argument >> 16) - int64_t(vblanks_since_flip);
  return remaining > 0 || scanline_percent > (argument & 0xFFFF);
}

void TestImmediatePresentArgument() {
  std::printf("present callback argument\n");
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x12345A07) == 0x12345007);
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x00000200) == 0);
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x00000003) == 3);

  const uint32_t original = (2u << 16) | 40u;
  CHECK(Mua1CallbackHolds(original, 1, 0));
  CHECK(Mua1CallbackHolds(original, 2, 60));
  for (const uint32_t argument : {original, 0u, 0xFFFFFFFFu}) {
    const uint32_t immediate = mua1::mua::guest::ImmediatePresentArgument(argument);
    for (uint32_t since_flip = 0; since_flip <= 8; ++since_flip) {
      for (uint32_t percent = 0; percent <= 100; ++percent) {
        CHECK(!Mua1CallbackHolds(immediate, since_flip, percent));
      }
    }
  }
}

void TestMapsCarryTheSameHooks() {
  std::printf("guest map parity\n");
  const std::string gold_map = ReadSource(kSources / "guest_map_mua1_gold.h");
  const std::string mua1_map = ReadSource(kSources / "guest_map_mua1.h");
  const char* const macros = R"(#define (MUA_GUEST_[A-Z_]+) )";
  const char* const constants = R"(inline constexpr \w+ (k\w+) =)";
  const char* const functions = R"(inline constexpr \w+ (\w+)\()";

  std::set<std::string> gold_hooks = Matches(gold_map, macros);
  const std::set<std::string> mua1_hooks = Matches(mua1_map, macros);
  CHECK(Matches(gold_map, "#define (MUA_GUEST_HAS_INDIRECT_TAIL_CALLS) 1").size() == 1);
  CHECK(Matches(mua1_map, "#define (MUA_GUEST_HAS_INDIRECT_TAIL_CALLS) 0").size() == 1);
  // The only hook one game lacks is the one its codegen output does not need.
  gold_hooks.erase("MUA_GUEST_INDIRECT_TAIL_CALL");
  CHECK(gold_hooks == mua1_hooks);

  std::set<std::string> gold_constants = Matches(gold_map, constants);
  gold_constants.erase("kIndirectTailTable");
  CHECK(gold_constants == Matches(mua1_map, constants));
  CHECK(Matches(gold_map, functions) == Matches(mua1_map, functions));
  CHECK(Matches(gold_map, "(MUA_GUEST_PRESENT_CALLBACK) sub_").size() == 1);
  CHECK(Matches(mua1_map, "(MUA_GUEST_PRESENT_CALLBACK) sub_").size() == 1);
}

void TestEveryUsedHookIsMapped() {
  std::printf("hooks used by the sources\n");
  const std::set<std::string> mapped = Matches(ReadSource(kSources / "guest_map_mua1.h"), R"(#define (MUA_GUEST_[A-Z_]+) )");
  std::set<std::string> used;
  for (const auto& entry : std::filesystem::directory_iterator(kSources)) {
    if (entry.path().extension() != ".cpp") {
      continue;
    }
    for (const std::string& name : Matches(ReadSource(entry.path()), R"((MUA_GUEST_[A-Z_]+))")) {
      used.insert(name);
    }
  }
  used.erase("MUA_GUEST_ORIGINAL");
  used.erase("MUA_GUEST_HAS_INDIRECT_TAIL_CALLS");
  used.erase("MUA_GUEST_INDIRECT_TAIL_CALL");
  CHECK(!used.empty());
  for (const std::string& name : used) {
    if (!mapped.contains(name)) {
      std::printf("  unmapped hook %s\n", name.c_str());
    }
    CHECK(mapped.contains(name));
  }
}

void TestSelection() {
  std::printf("module selection\n");
  const std::string select = ReadSource(kSources / "guest_map.h");
  CHECK(select.find("defined(MUA_RUNTIME_MUA1_GOLD)") != std::string::npos);
  CHECK(select.find("#include \"guest_map_mua1_gold.h\"") != std::string::npos);
  CHECK(select.find("defined(MUA_RUNTIME_MUA1)") != std::string::npos);
  CHECK(select.find("#include \"guest_map_mua1.h\"") != std::string::npos);
  CHECK(select.find("#error") != std::string::npos);
}

}  // namespace

int main() {
  TestImmediatePresentArgument();
  TestMapsCarryTheSameHooks();
  TestEveryUsedHookIsMapped();
  TestSelection();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
