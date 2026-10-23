// The guest maps of the three games must carry the same hooks, so a hook added for one game cannot be left out of
// another, the present callback's argument must release the swap at once in each, and every mapped function must be a
// function of the module generated for that game.
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
namespace mua2 {
#include "guest_map_mua2.h"
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

struct MapFile {
  const char* key;
  const char* file;
  const char* runtime_define;
  const char* generated_dir;
};

const MapFile kMaps[] = {
    {"mua1", "guest_map_mua1.h", "MUA_RUNTIME_MUA1", "mua1"},
    {"mua1-gold", "guest_map_mua1_gold.h", "MUA_RUNTIME_MUA1_GOLD", "mua1-gold"},
    {"mua2", "guest_map_mua2.h", "MUA_RUNTIME_MUA2", "mua2"},
};

// The hooks a game's code can lack because its codegen output or its audio engine does not need them.
const std::set<std::string> kOptionalHooks = {"MUA_GUEST_INDIRECT_TAIL_CALL", "MUA_GUEST_AUDIO_BARRIER",
                                              "MUA_GUEST_RESET_EVENT", "MUA_GUEST_MEMMOVE"};
const std::set<std::string> kOptionalConstants = {"kIndirectTailTable"};

// sub_8248A0A0 holds the swap for another vblank while the interval in the high half of its argument has not passed
// since the last flip, or while the scanline has passed the threshold in the low half.
bool Mua1CallbackHolds(uint32_t argument, uint32_t vblanks_since_flip, uint32_t scanline_percent) {
  const int64_t remaining = int64_t(argument >> 16) - int64_t(vblanks_since_flip);
  return remaining > 0 || scanline_percent > (argument & 0xFFFF);
}

// The Gold Edition and MUA 2 callbacks take the interval from bits 8-11 and the threshold from the low byte; the
// threshold only counts while an interval is set.
bool BitsCallbackHolds(uint32_t argument, uint32_t vblanks_since_flip, uint32_t scanline_percent) {
  const uint32_t interval = (argument >> 8) & 0xF;
  return int64_t(interval) - int64_t(vblanks_since_flip) > 0 || (interval != 0 && scanline_percent > (argument & 0xFF));
}

void TestImmediatePresentArgument() {
  std::printf("present callback argument\n");
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x12345A07) == 0x12345007);
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x00000200) == 0);
  CHECK(mua2::mua::guest::ImmediatePresentArgument(0x00000264) == 0x00000064);
  CHECK(mua2::mua::guest::ImmediatePresentArgument(0x00000064) == 0x00000064);
  CHECK(gold::mua::guest::ImmediatePresentArgument(0x00000003) == 3);
  CHECK(mua2::mua::guest::ImmediatePresentArgument(0x12345A07) == 0x12345007);
  CHECK(mua2::mua::guest::ImmediatePresentArgument(0x00000200) == 0);
  CHECK(mua2::mua::guest::ImmediatePresentArgument(0x00000003) == 3);

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

  const uint32_t two_vblanks = (2u << 8) | 40u;
  CHECK(BitsCallbackHolds(two_vblanks, 1, 0));
  CHECK(BitsCallbackHolds(two_vblanks, 2, 60));
  for (const uint32_t argument : {two_vblanks, 0x0000010Au, 0x12345A07u, 0u, 0xFFFFFFFFu}) {
    const uint32_t gold_immediate = gold::mua::guest::ImmediatePresentArgument(argument);
    const uint32_t mua2_immediate = mua2::mua::guest::ImmediatePresentArgument(argument);
    CHECK(gold_immediate == mua2_immediate);
    for (uint32_t since_flip = 0; since_flip <= 8; ++since_flip) {
      for (uint32_t percent = 0; percent <= 100; ++percent) {
        CHECK(!BitsCallbackHolds(gold_immediate, since_flip, percent));
        CHECK(!BitsCallbackHolds(mua2_immediate, since_flip, percent));
      }
    }
  }
}

void TestMapsCarryTheSameHooks() {
  std::printf("guest map parity\n");
  const char* const macros = R"(#define (MUA_GUEST_[A-Z_]+) )";
  const char* const constants = R"(inline constexpr \w+ (k\w+) =)";
  const char* const functions = R"(inline constexpr \w+ (\w+)\()";
  const char* const features = R"(#define (MUA_GUEST_HAS_[A-Z_]+) [01])";

  const std::string reference_map = ReadSource(kSources / kMaps[0].file);
  std::set<std::string> reference_hooks;
  for (const std::string& hook : Matches(reference_map, macros)) {
    if (!kOptionalHooks.contains(hook)) {
      reference_hooks.insert(hook);
    }
  }
  const std::set<std::string> reference_constants = Matches(reference_map, constants);
  for (const MapFile& map : kMaps) {
    const std::string text = ReadSource(kSources / map.file);
    std::set<std::string> hooks = Matches(text, macros);
    std::set<std::string> kept;
    for (const std::string& hook : hooks) {
      if (!kOptionalHooks.contains(hook)) {
        kept.insert(hook);
      }
    }
    CHECK(kept == reference_hooks);
    CHECK(Matches(text, features) == Matches(reference_map, features));
    std::set<std::string> map_constants = Matches(text, constants);
    for (const std::string& name : kOptionalConstants) {
      map_constants.erase(name);
    }
    CHECK(map_constants == reference_constants);
    CHECK(Matches(text, functions) == Matches(reference_map, functions));
    CHECK(Matches(text, "(MUA_GUEST_PRESENT_CALLBACK) sub_").size() == 1);
  }

  const std::string gold_map = ReadSource(kSources / "guest_map_mua1_gold.h");
  const std::string mua1_map = ReadSource(kSources / "guest_map_mua1.h");
  const std::string mua2_map = ReadSource(kSources / "guest_map_mua2.h");
  CHECK(Matches(gold_map, "#define (MUA_GUEST_HAS_INDIRECT_TAIL_CALLS) 1").size() == 1);
  CHECK(Matches(mua1_map, "#define (MUA_GUEST_HAS_INDIRECT_TAIL_CALLS) 0").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_INDIRECT_TAIL_CALLS) 0").size() == 1);
  for (const std::string& text : {gold_map, mua1_map}) {
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_AUDIO_BARRIER) 1").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_RESET_EVENT) 1").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_AUDIO_BARRIER) sub_").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_RESET_EVENT) sub_").size() == 1);
  }
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_AUDIO_BARRIER) 0").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_RESET_EVENT) 0").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_AUDIO_BARRIER) ").empty());
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_RESET_EVENT) ").empty());
  CHECK(Matches(mua1_map, "#define (MUA_GUEST_HAS_MEMMOVE) 1").size() == 1);
  CHECK(Matches(mua1_map, "#define (MUA_GUEST_MEMMOVE) sub_8282A150").size() == 1);
  CHECK(Matches(gold_map, "#define (MUA_GUEST_HAS_MEMMOVE) 1").size() == 1);
  CHECK(Matches(gold_map, "#define (MUA_GUEST_MEMMOVE) sub_82859500").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_MEMMOVE) 1").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_MEMMOVE) sub_82A65700").size() == 1);
}

void TestEveryUsedHookIsMapped() {
  std::printf("hooks used by the sources\n");
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
  CHECK(!used.empty());
  for (const MapFile& map : kMaps) {
    const std::set<std::string> mapped = Matches(ReadSource(kSources / map.file), R"(#define (MUA_GUEST_[A-Z_]+) )");
    for (const std::string& name : used) {
      if (kOptionalHooks.contains(name) || name.starts_with("MUA_GUEST_HAS_")) {
        continue;
      }
      if (!mapped.contains(name)) {
        std::printf("  %s: unmapped hook %s\n", map.key, name.c_str());
      }
      CHECK(mapped.contains(name));
    }
  }
}

void TestOptionalHooksFollowTheirFlags() {
  std::printf("optional hooks follow their feature flags\n");
  const std::string ring = ReadSource(kSources / "sound_stream_ring.cpp");
  const std::string barrier = ReadSource(kSources / "xaudio_worker_barrier.cpp");
  const std::string tail = ReadSource(kSources / "indirect_tail_calls.cpp");
  const std::string crt = ReadSource(kSources / "crt_native.cpp");
  const std::string report = ReadSource(kSources / "guest_hook_report.cpp");
  CHECK(ring.find("#if MUA_GUEST_HAS_RESET_EVENT") != std::string::npos);
  CHECK(barrier.find("#if MUA_GUEST_HAS_AUDIO_BARRIER") != std::string::npos);
  CHECK(tail.find("#if MUA_GUEST_HAS_INDIRECT_TAIL_CALLS") != std::string::npos);
  CHECK(crt.find("#if MUA_GUEST_HAS_MEMMOVE") != std::string::npos);
  CHECK(report.find("#if MUA_GUEST_HAS_MEMMOVE") != std::string::npos);
}

void TestSelection() {
  std::printf("module selection\n");
  const std::string select = ReadSource(kSources / "guest_map.h");
  CHECK(select.find("defined(MUA_RUNTIME_MUA1_GOLD)") != std::string::npos);
  CHECK(select.find("#include \"guest_map_mua1_gold.h\"") != std::string::npos);
  CHECK(select.find("defined(MUA_RUNTIME_MUA1)") != std::string::npos);
  CHECK(select.find("#include \"guest_map_mua1.h\"") != std::string::npos);
  CHECK(select.find("defined(MUA_RUNTIME_MUA2)") != std::string::npos);
  CHECK(select.find("#include \"guest_map_mua2.h\"") != std::string::npos);
  CHECK(select.find("#error") != std::string::npos);
}

// Whether each address is the return address of a call in the module: the site a hook tells its caller apart by.
bool ReturnAddressesExist(const std::filesystem::path& module, std::set<std::string> addresses) {
  for (const auto& entry : std::filesystem::directory_iterator(module)) {
    const std::string name = entry.path().filename().string();
    if (name.find("_recomp.") == std::string::npos || entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadSource(entry.path());
    for (auto it = addresses.begin(); it != addresses.end();) {
      it = text.find("ctx.lr = 0x" + *it + ";") != std::string::npos ? addresses.erase(it) : std::next(it);
    }
    if (addresses.empty()) {
      return true;
    }
  }
  return addresses.empty();
}

// Every mapped function is a function of the game's own module, and the codegen hooks its config names are
// emitted there; skipped where the module has not been generated.
void TestMappedFunctionsAreGenerated() {
#ifdef MUA_GENERATED_ROOT
  std::printf("mapped functions exist in the generated modules\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  for (const MapFile& map : kMaps) {
    const std::string registry = ReadSource(root / map.generated_dir / "marvel_ultimate_alliance_register.cpp");
    if (registry.empty()) {
      std::printf("  %s: module not generated, skipped\n", map.key);
      continue;
    }
    const std::string text = ReadSource(kSources / map.file);
    const std::regex hook(R"(#define MUA_GUEST_[A-Z_]+ sub_([0-9A-F]{8}))");
    int functions = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), hook), end; it != end; ++it) {
      const std::string address = (*it)[1].str();
      ++functions;
      CHECK(registry.find("SetFunction(0x" + address + ", sub_" + address + ")") != std::string::npos);
    }
    CHECK(functions >= 15);
    std::set<std::string> sites;
    const std::regex site(R"(inline constexpr uint32_t k\w+Site = 0x([0-9A-F]{8});)");
    for (std::sregex_iterator it(text.begin(), text.end(), site), end; it != end; ++it) {
      sites.insert((*it)[1].str());
    }
    CHECK(sites.size() == 3);
    CHECK(ReturnAddressesExist(root / map.generated_dir, sites));
  }
#endif
}

// The compare-then-tail-call-memcpy-or-copy-backward shape every game's memmove shares, confirmed by generated code
// rather than assumed from its address, with the memcpy it tail-calls matching that game's own MUA_GUEST_MEMCPY.
void TestMemmoveMatchesMemcpy() {
#ifdef MUA_GENERATED_ROOT
  std::printf("memmove tail-calls that game's own memcpy\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  for (const MapFile& map : kMaps) {
    const std::string registry = ReadSource(root / map.generated_dir / "marvel_ultimate_alliance_register.cpp");
    if (registry.empty()) {
      continue;
    }
    const std::string header = ReadSource(kSources / map.file);
    const std::set<std::string> memmove = Matches(header, "#define MUA_GUEST_MEMMOVE sub_([0-9A-F]{8})");
    const std::set<std::string> memcpy = Matches(header, "#define MUA_GUEST_MEMCPY sub_([0-9A-F]{8})");
    CHECK(memmove.size() == 1 && memcpy.size() == 1);
    if (memmove.size() != 1 || memcpy.size() != 1) {
      continue;
    }
    const std::string memmove_fn = "sub_" + *memmove.begin();
    const std::string memcpy_fn = "sub_" + *memcpy.begin();
    std::string body;
    for (const auto& entry : std::filesystem::directory_iterator(root / map.generated_dir)) {
      if (entry.path().extension() != ".cpp") {
        continue;
      }
      const std::string text = ReadSource(entry.path());
      const size_t start = text.find("DEFINE_REX_FUNC(" + memmove_fn + ")");
      if (start == std::string::npos) {
        continue;
      }
      const size_t end = text.find("DEFINE_REX_FUNC(", start + 1);
      body = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
      break;
    }
    CHECK(!body.empty());
    CHECK(body.find("ctx.cr0.compare<int32_t>(ctx.r3.s32, ctx.r4.s32, ctx.xer);") != std::string::npos);
    CHECK(body.find("if (ctx.cr0.eq) return;") != std::string::npos);
    CHECK(body.find(memcpy_fn + "(ctx, base);") != std::string::npos);
    CHECK(body.find("REX_STORE_U8(ctx.r3.u32 + -1, ctx.r0.u8);") != std::string::npos);
  }
#endif
}

}  // namespace

int main() {
  TestImmediatePresentArgument();
  TestMapsCarryTheSameHooks();
  TestEveryUsedHookIsMapped();
  TestOptionalHooksFollowTheirFlags();
  TestSelection();
  TestMappedFunctionsAreGenerated();
  TestMemmoveMatchesMemcpy();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
