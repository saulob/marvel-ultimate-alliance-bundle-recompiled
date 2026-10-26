// The guest maps of the three games must carry the same hooks, so a hook added for one game cannot be left out of
// another, the present callback's argument must release the swap at once in each, and every mapped function must be a
// function of the module generated for that game.
// Run: mua_guest_map_tests

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

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
namespace gold_d3d {
#include "d3d_state_setters_mua1_gold.h"
}
namespace mua1_d3d {
#include "d3d_state_setters_mua1.h"
}
namespace mua2_d3d {
#include "d3d_state_setters_mua2.h"
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

// Hooks a game's code can lack: because its codegen output, its audio engine or its Alchemy build does not need them,
// or (the skip intro functions) because MUA 2 skips its intro through a midasm_hook instead.
const std::set<std::string> kOptionalHooks = {
    "MUA_GUEST_INDIRECT_TAIL_CALL", "MUA_GUEST_AUDIO_BARRIER", "MUA_GUEST_RESET_EVENT", "MUA_GUEST_MEMMOVE",
    "MUA_GUEST_INTRO_GATE", "MUA_GUEST_MOVIE_MENU_UPDATE", "MUA_GUEST_OPEN_MENU", "MUA_GUEST_MENU_MANAGER",
    "MUA_GUEST_MOVIE_PLAYING", "MUA_GUEST_STOP_MOVIE", "MUA_GUEST_SET_ACTIVE_PAD", "MUA_GUEST_USER_MANAGER",
    "MUA_GUEST_USER_SET_CONTROLLER", "MUA_GUEST_USER_REFRESH", "MUA_GUEST_USER_GET_SLOT", "MUA_GUEST_USER_SET_SLOT",
    "MUA_GUEST_INPUT_MANAGER", "MUA_GUEST_INPUT_SET_NAME", "MUA_GUEST_INPUT_SET_FLAGS", "MUA_GUEST_INPUT_CLEAR_FLAGS",
    "MUA_GUEST_PROFILE_MANAGER", "MUA_GUEST_PROFILE_REFRESH_PAD", "MUA_GUEST_PROFILE_RESET_ALL",
    "MUA_GUEST_PROFILE_QUERY_ALL", "MUA_GUEST_PROFILE_SIGNED_IN", "MUA_GUEST_PROFILE_ONLINE",
    "MUA_GUEST_PROFILE_NAME", "MUA_GUEST_SIGNIN_PUMP", "MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A",
    "MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B", "MUA_GUEST_CONSOLE", "MUA_GUEST_CONSOLE_QUEUE", "MUA_GUEST_PAD_MANAGER",
    "MUA_GUEST_PAD_CONNECTED", "MUA_GUEST_SCRIPT_DISPATCH", "MUA_GUEST_MENU_ITEM_CREATE",
    "MUA_GUEST_ITEM_USE", "MUA_GUEST_STRING_DATA", "MUA_GUEST_TEXT_INFLATE", "MUA_GUEST_MENU_SIGNIN",
    "MUA_GUEST_ASSIGN_ADOPTED_REF", "MUA_GUEST_AND_MASKS", "MUA_GUEST_INDEX_ALLOWED",
    "MUA_GUEST_IS_OF_TYPE", "MUA_GUEST_MATRIX_MULTIPLY", "MUA_GUEST_MATRIX_COPY", "MUA_GUEST_RELEASE_STRING",
    "MUA_GUEST_ASSIGN_STRING", "MUA_GUEST_RELEASE_RESOURCE", "MUA_GUEST_APPEND_REF", "MUA_GUEST_FREE_STRING",
    "MUA_GUEST_STRING_PREPARE", "MUA_GUEST_STRING_INTERN", "MUA_GUEST_DESTROY_RESOURCE", "MUA_GUEST_RESIZE_ARRAY",
    "MUA_GUEST_ADD_REF_IF_ANY", "MUA_GUEST_SWAP_REF", "MUA_GUEST_HANDLE_MANAGER", "MUA_GUEST_RELEASE_HANDLE"};
const std::set<std::string> kOptionalConstants = {
    "kIndirectTailTable",         "kInputUserSlotOffset",     "kMainMenuExitCommand",
    "kMainMenuVtable",            "kMainMenuLiveNameEntry",   "kEmptyString",
    "kIsOfTypeMetaSite",          "kAssignStringPrepareSite", "kAssignStringInternSite",
    "kAssignStringFreeSite",      "kReleaseResourceParentSite", "kReleaseResourceDestroySite",
    "kAppendRefResizeSite",       "kImageSize",               "kCodeBase",
    "kCodeSize",                  "kObjectHandleFlag",        "kObjectHandleManager",
    "kReleaseHandleManagerSite",  "kReleaseHandleSite"};

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
  std::set<std::string> reference_constants = Matches(reference_map, constants);
  for (const std::string& name : kOptionalConstants) {
    reference_constants.erase(name);
  }
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
  CHECK(Matches(mua1_map, "#define (MUA_GUEST_SET_VERTEX_DECLARATION) sub_824766D8").size() == 1);
  CHECK(Matches(gold_map, "#define (MUA_GUEST_SET_VERTEX_DECLARATION) sub_8248F378").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_SET_VERTEX_DECLARATION) sub_829C1DB8").size() == 1);
  for (const std::string& text : {gold_map, mua1_map, mua2_map}) {
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_D3D_STATE_FILTER) 1").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_ALCHEMY_CORE) 1").size() == 1);
  }
  for (const std::string& text : {gold_map, mua1_map}) {
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_ALCHEMY_HELPERS) 1").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_HAS_OBJECT_HANDLES) 0").size() == 1);
    CHECK(Matches(text, "inline constexpr uint32_t (kRefCountMask) = 0x3FFFFF;").size() == 1);
    CHECK(Matches(text, "inline constexpr bool (kAssignRefReturnsSlot) = false;").size() == 1);
    CHECK(Matches(text, "#define (MUA_GUEST_(ADD_REF_IF_ANY|SWAP_REF|HANDLE_MANAGER|RELEASE_HANDLE)) ").empty());
  }
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_ALCHEMY_HELPERS) 0").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_HAS_OBJECT_HANDLES) 1").size() == 1);
  CHECK(Matches(mua2_map, "inline constexpr uint32_t (kRefCountMask) = 0x7FFFF;").size() == 1);
  CHECK(Matches(mua2_map, "inline constexpr bool (kAssignRefReturnsSlot) = true;").size() == 1);
  CHECK(Matches(mua2_map, "#define (MUA_GUEST_(ASSIGN_ADOPTED_REF|AND_MASKS|INDEX_ALLOWED|IS_OF_TYPE|MATRIX_MULTIPLY|"
                          "MATRIX_COPY|\\w+_STRING|\\w+_RESOURCE|APPEND_REF|RESIZE_ARRAY|STRING_\\w+)) ")
            .empty());

  // Gold Edition carries every hook, constant and feature MUA1 has; only its indirect tail call table is its own.
  std::set<std::string> gold_hooks = Matches(gold_map, macros);
  gold_hooks.erase("MUA_GUEST_INDIRECT_TAIL_CALL");
  CHECK(gold_hooks == Matches(mua1_map, macros));
  std::set<std::string> gold_constants = Matches(gold_map, constants);
  gold_constants.erase("kIndirectTailTable");
  CHECK(gold_constants == Matches(mua1_map, constants));
  const char* const feature_values = R"(#define (MUA_GUEST_HAS_(?!INDIRECT_TAIL_CALLS)[A-Z_]+ [01]))";
  CHECK(Matches(gold_map, feature_values) == Matches(mua1_map, feature_values));
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
  const std::string declaration = ReadSource(kSources / "d3d_state_filter.cpp");
  CHECK(ring.find("#if MUA_GUEST_HAS_RESET_EVENT") != std::string::npos);
  CHECK(barrier.find("#if MUA_GUEST_HAS_AUDIO_BARRIER") != std::string::npos);
  CHECK(tail.find("#if MUA_GUEST_HAS_INDIRECT_TAIL_CALLS") != std::string::npos);
  CHECK(crt.find("#if MUA_GUEST_HAS_MEMMOVE") != std::string::npos);
  CHECK(report.find("#if MUA_GUEST_HAS_MEMMOVE") != std::string::npos);
  CHECK(declaration.find("#if MUA_GUEST_HAS_D3D_STATE_FILTER") != std::string::npos);
  const std::string watchdog = ReadSource(kSources / "hang_watchdog.cpp");
  CHECK(declaration.find("mua_skip_redundant_shader_constants, false") != std::string::npos);
  for (const std::string* source : {&declaration, &watchdog, &report}) {
    CHECK(source->find("mua_debug_") == std::string::npos);
    CHECK(source->find("DEBUG_SAULO") == std::string::npos);
  }
  CHECK(!std::filesystem::exists(kSources / "gpu_producer_counters.cpp"));
  // Every game, MUA1 included, runs the native uploads through the same hooks.
  CHECK(declaration.find("MUA_RUNTIME_") == std::string::npos);
  CHECK(declaration.find("REX_HOOK_RAW(MUA_GUEST_UPLOAD_REGISTERS)") != std::string::npos);
  const std::string alchemy = ReadSource(kSources / "alchemy_native.cpp");
  for (const char* flag : {"#if MUA_GUEST_HAS_ALCHEMY_CORE", "#if MUA_GUEST_HAS_ALCHEMY_HELPERS",
                           "#if MUA_GUEST_HAS_OBJECT_HANDLES"}) {
    CHECK(alchemy.find(flag) != std::string::npos);
    CHECK(report.find(flag) != std::string::npos);
  }
  // MUA1's own helpers and MUA 2's handle-aware reference helpers sit inside the shared core.
  const size_t core = alchemy.find("#if MUA_GUEST_HAS_ALCHEMY_CORE");
  CHECK(core < alchemy.find("REX_HOOK_RAW(MUA_GUEST_ADD_REF)") &&
        alchemy.find("#if MUA_GUEST_HAS_OBJECT_HANDLES\n\n// r3 object (null allowed).") <
            alchemy.find("REX_HOOK_RAW(MUA_GUEST_SWAP_REF)") &&
        alchemy.find("#if MUA_GUEST_HAS_ALCHEMY_HELPERS") < alchemy.find("REX_HOOK_RAW(MUA_GUEST_ASSIGN_ADOPTED_REF)") &&
        alchemy.rfind("#endif  // MUA_GUEST_HAS_ALCHEMY_CORE") > alchemy.find("REX_HOOK_RAW(MUA_GUEST_APPEND_REF)"));
  const std::filesystem::path root = std::filesystem::path(MUA_SRC_DIR).parent_path();
  for (const char* manifest : {"mua1.manifest.toml", "mua1-gold.manifest.toml", "mua2.manifest.toml"}) {
    const std::string text = ReadSource(root / manifest);
    CHECK(text.find("_as_local") == std::string::npos && text.find("skip_") == std::string::npos);
  }
  for (const char* off : {"mua_native_alchemy_matrix_and_type, false", "mua_lockfree_string_and_resource_refs, false"}) {
    CHECK(alchemy.find(off) != std::string::npos);
  }
  CHECK(report.find("#if MUA_GUEST_HAS_D3D_STATE_FILTER") != std::string::npos);
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
    CHECK(sites.size() >= 2);
    CHECK(ReturnAddressesExist(root / map.generated_dir, sites));
  }
#endif
}

std::string GeneratedBody(const std::filesystem::path& module, const std::string& function) {
  for (const auto& entry : std::filesystem::directory_iterator(module)) {
    if (entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadSource(entry.path());
    const size_t start = text.find("DEFINE_REX_FUNC(" + function + ")");
    if (start == std::string::npos) {
      continue;
    }
    const size_t end = text.find("DEFINE_REX_FUNC(", start + 1);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }
  return {};
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
    const std::string body = GeneratedBody(root / map.generated_dir, memmove_fn);
    CHECK(!body.empty());
    CHECK(body.find("ctx.cr0.compare<int32_t>(ctx.r3.s32, ctx.r4.s32, ") != std::string::npos);
    CHECK(body.find("if (ctx.cr0.eq) return;") != std::string::npos);
    CHECK(body.find(memcpy_fn + "(ctx, base);") != std::string::npos);
    CHECK(body.find("REX_STORE_U8(ctx.r3.u32 + -1, ctx.r0.u8);") != std::string::npos);
  }
#endif
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

/// The device fields a D3D build's setter table header gives.
struct D3DFields {
  uint32_t shader_constants;
  uint32_t fetch_constants;
  uint32_t vertex_fetch_constants;
  uint32_t vs_alu_constants;
  uint32_t ps_alu_constants;
  uint32_t current_fence;
  uint32_t command_cursor;
  uint32_t command_end;
  uint32_t command_reserve_limit;
  uint32_t texture_dirty;
  uint32_t flags;
  uint32_t vertex_declaration;
  uint32_t committed_strides;
  uint32_t stream_sources;
  uint32_t stream_strides;
  uint32_t textures;
  uint32_t pixel_shader;
  uint32_t vertex_shader;
  std::array<uint32_t, 4> sampler_bytes;
  uint32_t literal_entries;
  uint32_t literal_bytes;
};

#define MUA_D3D_FIELDS(ns)                                                                                       \
  D3DFields {                                                                                                    \
    ns::mua::d3d::kShaderConstants, ns::mua::d3d::kFetchConstants, ns::mua::d3d::kVertexFetchConstants,          \
        ns::mua::d3d::kVsAluConstants, ns::mua::d3d::kPsAluConstants, ns::mua::d3d::kCurrentFence,               \
        ns::mua::d3d::kCommandCursor, ns::mua::d3d::kCommandEnd, ns::mua::d3d::kCommandReserveLimit,             \
        ns::mua::d3d::kTextureDirty, ns::mua::d3d::kFlags, ns::mua::d3d::kVertexDeclaration,                     \
        ns::mua::d3d::kCommittedStrides, ns::mua::d3d::kStreamSources, ns::mua::d3d::kStreamStrides,             \
        ns::mua::d3d::kTextures, ns::mua::d3d::kPixelShader, ns::mua::d3d::kVertexShader,                        \
        ns::mua::d3d::kSamplerBytes, ns::mua::d3d::kLiteralEntries, ns::mua::d3d::kLiteralBytes                  \
  }

// One game's D3D build, with the instructions whose register allocation or bit layout differs between the builds:
// how the declaration setter requests the shader commit, the texture setter's dirty mask merge and the shader
// constant setters' (vertex, pixel) dirty mask merge and store.
struct D3DBuild {
  const char* key;
  const char* map;
  const char* table;
  D3DFields fields;
  const char* binding;
  const char* commit_request;
  const char* texture_dirty_merge;
  std::array<std::pair<const char*, const char*>, 2> constant_dirty;
};

const D3DBuild kD3DBuilds[] = {
    {"mua1",
     "guest_map_mua1.h",
     "d3d_state_setters_mua1.h",
     MUA_D3D_FIELDS(mua1_d3d),
     "sub_824874D8",
     "ctx.r12.u64 = __builtin_rotateleft64(ctx.r12.u64, 51)",
     "// or r11,r11,r6",
     {{{"// or r10,r10,r9", "// std r10,0(r11)"}, {"// or r10,r10,r9", "// std r10,8(r11)"}}}},
    {"mua1-gold",
     "guest_map_mua1_gold.h",
     "d3d_state_setters_mua1_gold.h",
     MUA_D3D_FIELDS(gold_d3d),
     "sub_824A0C30",
     "ctx.r11.u64 = ctx.r11.u64 | 524288;",
     "// or r11,r6,r11",
     {{{"// or r11,r11,r7", "// std r11,0(r3)"}, {"// or r11,r7,r11", "// std r11,8(r3)"}}}},
    {"mua2",
     "guest_map_mua2.h",
     "d3d_state_setters_mua2.h",
     MUA_D3D_FIELDS(mua2_d3d),
     "sub_829D9C98",
     "ctx.r11.u64 = ctx.r11.u64 | 524288;",
     "// or r9,r8,r6",
     {{{"// or r11,r11,r7", "// std r11,0(r3)"}, {"// or r11,r7,r11", "// std r11,8(r3)"}}}},
};

std::string N(uint32_t value) {
  return std::to_string(value);
}

// The function a guest map binds a hook to, as "sub_XXXXXXXX".
std::string Hooked(const std::string& map, const std::string& hook) {
  const std::set<std::string> found = Matches(map, ("#define " + hook + " (sub_[0-9A-F]{8})").c_str());
  return found.size() == 1 ? *found.begin() : std::string();
}

// The branch target form of a function in the generated code's instruction comments.
std::string BranchTarget(const std::string& function) {
  std::string target = "0x" + function.substr(4);
  for (char& c : target) {
    c = char(std::tolower(static_cast<unsigned char>(c)));
  }
  return target;
}

// Whether the body has the guest instruction, where "r?" stands for any general purpose register: the builds
// allocate registers differently around the same operation.
bool HasInstruction(const std::string& body, const std::string& instruction) {
  std::string pattern = "\t// ";
  for (size_t i = 0; i < instruction.size(); ++i) {
    if (instruction.compare(i, 2, "r?") == 0) {
      pattern += R"(r\d+)";
      ++i;
      continue;
    }
    if (std::string_view(".^$|()[]{}*+?\\").find(instruction[i]) != std::string_view::npos) {
      pattern += '\\';
    }
    pattern += instruction[i];
  }
  return std::regex_search(body, std::regex(pattern + "\n"));
}

// Vector stores, with or without the VMX128 encoding a later compiler chose.
size_t VectorStores(const std::string& body) {
  return Count(body, "// stvx ") + Count(body, "// stvx128 ");
}

// The D3D state filters drop a repeated set only while each setter still does what they compare against: merge its
// value into one field and mark it dirty, or (shaders and stream sources) refresh the replaced object's fence, store
// it and apply what the filter checks is already applied. Each build's device fields are the ones its own setters use.
void CheckD3DBuild(const D3DBuild& build) {
  const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / build.key;
  const std::string registry = ReadSource(module / "marvel_ultimate_alliance_register.cpp");
  if (registry.empty()) {
    std::printf("  %s: module not generated, skipped\n", build.key);
    return;
  }
  const std::string map = ReadSource(kSources / build.map);
  const auto hooked = [&](const char* hook) { return GeneratedBody(module, Hooked(map, hook)); };
  const D3DFields& fields = build.fields;
  const std::regex indexed_store(R"(REX_STORE_U\d+\(ctx\.r(\d+)\.u32 \+ ctx\.r(\d+)\.u32, )");

  const std::string declaration = hooked("MUA_GUEST_SET_VERTEX_DECLARATION");
  CHECK(Count(declaration, "REX_STORE_") == 2);
  CHECK(Has(declaration, "REX_STORE_U32(ctx.r3.u32 + " + N(fields.vertex_declaration) + ", ctx.r4.u32);"));
  CHECK(Has(declaration, build.commit_request));
  CHECK(!Has(declaration, "(ctx, base);"));
  const std::string binding = GeneratedBody(module, build.binding);
  CHECK(HasInstruction(binding, "lwz r11,48(r?)"));
  CHECK(HasInstruction(binding, "stw r11,48(r?)"));
  CHECK(HasInstruction(binding, "addi r29,r?," + N(fields.stream_strides)));
  CHECK(HasInstruction(binding, "std r11," + N(fields.committed_strides) + "(r?)"));
  CHECK(HasInstruction(binding, "lwz r?," + N(fields.current_fence) + "(r?)"));

  // Every listed render state setter is a leaf whose only stores are its listed fields, the dirty masks and its stack.
  const std::string tables = ReadSource(kSources / build.table);
  const std::regex leaf(R"(X\(([0-9A-F]{8})((?:, MUA_FIELD\(\d+, \d\))+)\))");
  const std::regex field(R"(MUA_FIELD\((\d+), (\d)\))");
  const std::regex store(R"(REX_STORE_U(8|16|32|64)\(ctx\.r(\d+)\.u32 \+ (-?\d+), )");
  const std::regex assigns_r3(R"(ctx\.r3\.(u64|s64|u32) =)");
  size_t leaves = 0;
  for (std::sregex_iterator it(tables.begin(), tables.end(), leaf), end; it != end; ++it) {
    ++leaves;
    std::set<std::pair<int, int>> allowed;
    const std::string fields_text = (*it)[2].str();
    for (std::sregex_iterator f(fields_text.begin(), fields_text.end(), field); f != end; ++f) {
      allowed.insert({std::stoi((*f)[1].str()), std::stoi((*f)[2].str())});
    }
    const std::string address = (*it)[1].str();
    CHECK(Has(registry, "SetFunction(0x" + address + ", sub_" + address + ")"));
    const std::string body = GeneratedBody(module, "sub_" + address);
    CHECK(!body.empty());
    CHECK(!Has(body, "\tsub_") && !Has(body, "REX_CALL_INDIRECT") && !std::regex_search(body, assigns_r3));
    CHECK(!std::regex_search(body, indexed_store));
    size_t dirty_stores = 0;
    for (std::sregex_iterator s(body.begin(), body.end(), store); s != end; ++s) {
      const int size = std::stoi((*s)[1].str()) / 8;
      const int reg = std::stoi((*s)[2].str());
      const int offset = std::stoi((*s)[3].str());
      const bool dirty = reg == 3 && size == 8 && (offset == 16 || offset == 24 || offset == 32);
      dirty_stores += dirty;
      CHECK(reg == 1 || dirty || (reg == 3 && allowed.contains({offset, size})));
    }
    CHECK(Count(body, "REX_STORE_") == Count(body, "REX_STORE_U8(ctx.r") + Count(body, "REX_STORE_U16(ctx.r") +
                                            Count(body, "REX_STORE_U32(ctx.r") + Count(body, "REX_STORE_U64(ctx.r"));
    CHECK(dirty_stores > 0);
  }
  CHECK(leaves == 76);

  // Every listed sampler state setter only stores into the slot's fetch constant (r3 + 24 * slot, with or without
  // the fetch constant offset folded in), the slot's bytes (r3 + slot), the texture dirty mask and its stack. A build
  // that folds the offset into the slot index (addi r11,r4,48; mulli r11,r11,24) may store the first word indexed.
  std::set<int> slot_offsets;
  for (int word = 0; word < 24; word += 4) {
    slot_offsets.insert(word);
    slot_offsets.insert(int(fields.fetch_constants) + word);
  }
  slot_offsets.insert(fields.sampler_bytes.begin(), fields.sampler_bytes.end());
  const int texture_dirty = int(fields.texture_dirty);
  const std::string fetch_slot = N(fields.fetch_constants / 24);
  const std::regex sampler(R"(X\(([0-9A-F]{8})\))");
  size_t samplers = 0;
  for (std::sregex_iterator it(tables.begin(), tables.end(), sampler), end; it != end; ++it) {
    ++samplers;
    const std::string address = (*it)[1].str();
    CHECK(Has(registry, "SetFunction(0x" + address + ", sub_" + address + ")"));
    const std::string body = GeneratedBody(module, "sub_" + address);
    CHECK(!body.empty());
    CHECK(!Has(body, "\tsub_") && !Has(body, "REX_CALL_INDIRECT") && !std::regex_search(body, assigns_r3));
    std::smatch fold;
    const bool folded = std::regex_search(
        body, fold, std::regex(R"(// addi r(\d+),r4,)" + fetch_slot + R"(\n[\s\S]*?// mulli r\1,r\1,24\n)"));
    CHECK(std::regex_search(body, std::regex(R"(// mulli r\d+,r4,24\n)")) || folded);
    CHECK(Has(body, "REX_STORE_U64(ctx.r3.u32 + " + N(fields.texture_dirty) + ", "));
    for (std::sregex_iterator s(body.begin(), body.end(), indexed_store); s != end; ++s) {
      CHECK(folded && (*s)[1].str() == fold[1].str() && (*s)[2].str() == "3");
    }
    for (std::sregex_iterator s(body.begin(), body.end(), store); s != end; ++s) {
      const int reg = std::stoi((*s)[2].str());
      const int offset = std::stoi((*s)[3].str());
      CHECK(reg == 1 || (reg == 3 && offset == texture_dirty) || (reg != 3 && slot_offsets.contains(offset)));
      if (offset >= int(fields.sampler_bytes[0])) {
        CHECK(std::regex_search(body, std::regex("// add r" + (*s)[2].str() + R"(,r3,r4\n)")));
      }
    }
  }
  CHECK(samplers == 20);

  const std::string texture = hooked("MUA_GUEST_SET_TEXTURE");
  CHECK(Has(texture, "// addi r11,r4," + N(fields.textures / 4) + "\n"));
  CHECK((Has(texture, "// mulli r11,r4,24\n") && Has(texture, "// addi r11,r11," + N(fields.fetch_constants) + "\n")) ||
        (Has(texture, "// addi r11,r4," + fetch_slot + "\n") && Has(texture, "// mulli r11,r11,24\n")));
  CHECK(Has(texture, build.texture_dirty_merge));
  CHECK(HasInstruction(texture, "std r?," + N(fields.texture_dirty) + "(r31)"));
  CHECK(HasInstruction(texture, "lbz r?," + N(fields.sampler_bytes[1]) + "(r?)"));
  CHECK(HasInstruction(texture, "lbz r?," + N(fields.sampler_bytes[2]) + "(r?)"));
  CHECK(Has(texture, "// stwx r5,r29,r31"));
  CHECK(Has(texture, "// lwz r11," + N(fields.current_fence) + "(r31)"));
  CHECK(Has(texture, "// stw r11,8(r28)"));

  const std::array<std::pair<const char*, uint32_t>, 2> constants = {
      {{"MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS", fields.vs_alu_constants},
       {"MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS", fields.ps_alu_constants}}};
  for (size_t i = 0; i < constants.size(); ++i) {
    const std::string body = hooked(constants[i].first);
    CHECK(Has(body, "// addi r10,r4," + N(constants[i].second / 16) + "\n"));
    CHECK(Has(body, "// rlwinm r10,r10,4,0,27\n"));
    CHECK(Has(body, build.constant_dirty[i].first));
    CHECK(Has(body, build.constant_dirty[i].second));
    CHECK(VectorStores(body) == 5);
    CHECK(!Has(body, "\tsub_"));
  }

  const std::string write_packets = BranchTarget(Hooked(map, "MUA_GUEST_WRITE_PACKETS"));
  const std::string reserve = BranchTarget(Hooked(map, "MUA_GUEST_RESERVE_COMMANDS"));
  const std::string cursor = N(fields.command_cursor);
  const std::string command_end = N(fields.command_end);
  const std::string packets = hooked("MUA_GUEST_WRITE_PACKETS");
  CHECK(Has(packets, "// rlwinm r10,r10,16,0,15"));
  CHECK(HasInstruction(packets, "lwzu r?,4(r27)"));
  CHECK(Has(packets, "// bl " + reserve + "\n"));
  CHECK(Has(packets, "// lwz r11," + command_end + "(r29)") && Has(packets, "// stw r4," + cursor + "(r29)"));
  const std::string reserved = hooked("MUA_GUEST_RESERVE_COMMANDS");
  CHECK(Has(reserved, "// lwz r3," + cursor + "(r31)\n") && Has(reserved, "// blr \n"));
  const std::string registers = hooked("MUA_GUEST_UPLOAD_REGISTERS");
  CHECK(Has(registers, "// addi r29,r6,-4"));
  CHECK(Has(registers, "// add r10,r27,r4"));
  CHECK(Has(registers, "// li r8,1"));
  CHECK(Has(registers, "// lwzu r10,4(r29)"));
  CHECK(Has(registers, "// bl " + write_packets + "\n"));
  CHECK(HasInstruction(registers, "lwz r4," + cursor + "(r?)") && Has(registers, "// lwz r9," + command_end + "(r26)") &&
        Has(registers, "// stw r4," + cursor + "(r26)"));
  const std::string alu = hooked("MUA_GUEST_UPLOAD_ALU_CONSTANTS");
  CHECK(HasInstruction(alu, "lis r?,-32768"));
  CHECK(Has(alu, "// clrlwi r11,r4,28"));
  CHECK(Has(alu, "// addi r11,r29,3"));
  CHECK(Has(alu, "// li r8,16"));
  CHECK(VectorStores(alu) == 4);
  CHECK(Has(alu, "// bl " + write_packets + "\n"));
  CHECK(HasInstruction(alu, "lwz r4," + cursor + "(r?)") && Has(alu, "// lwz r9," + command_end + "(r26)") &&
        Has(alu, "// stw r4," + cursor + "(r26)"));
  const std::string fetch = hooked("MUA_GUEST_UPLOAD_FETCH_CONSTANTS");
  CHECK(HasInstruction(fetch, "li r?,18432"));
  CHECK(HasInstruction(fetch, "addi r?,r?," + N(fields.fetch_constants)));
  CHECK(HasInstruction(fetch, "mulli r?,r?,24"));
  CHECK(Has(fetch, "// addi r11,r29,5"));
  CHECK(Has(fetch, "// clrlwi r11,r4,29"));
  CHECK(HasInstruction(fetch, "ori r?,r?,20480"));
  CHECK(Has(fetch, "// li r11,37"));
  CHECK(Has(fetch, "// rldicr r11,r11,44,19"));
  CHECK(Has(fetch, "// li r8,6"));
  CHECK(Has(fetch, "// bl " + write_packets + "\n") && Has(fetch, "// bl " + reserve + "\n"));
  CHECK(HasInstruction(fetch, "lwz r4," + cursor + "(r?)") && HasInstruction(fetch, "lwz r8," + command_end + "(r?)") &&
        HasInstruction(fetch, "lwz r?," + N(fields.command_reserve_limit) + "(r?)") &&
        HasInstruction(fetch, "stw r?," + cursor + "(r?)"));

  struct Shader {
    const char* hook;
    uint32_t slot;
    const char* header;
  };
  const Shader shaders[] = {
      {"MUA_GUEST_SET_PIXEL_SHADER", fields.pixel_shader, "ctx.r11.s64 = ctx.r29.s64 + 40;"},
      {"MUA_GUEST_SET_VERTEX_SHADER", fields.vertex_shader, "ctx.r11.s64 = ctx.r29.s64 + 872;"},
  };
  for (const Shader& shader : shaders) {
    const std::string body = hooked(shader.hook);
    CHECK(Has(body, "REX_STORE_U32(ctx.r30.u32 + " + N(shader.slot) + ", ctx.r29.u32);"));
    CHECK(Has(body, shader.header));
    CHECK(Has(body, "ctx.r31.s64 = ctx.r11.s64 + " + N(fields.literal_entries) + ";"));
    CHECK(HasInstruction(body, "lwz r?," + N(fields.literal_bytes) + "(r11)"));
    CHECK(HasInstruction(body, "lwz r11," + N(fields.current_fence) + "(r?)"));
    CHECK(Has(body, "REX_STORE_U32(ctx.r31.u32 + 8, ctx.r11.u32);"));
    CHECK(Has(body, "ctx.r28.s64 = ctx.r30.s64 + " + N(fields.shader_constants) + ";"));
    CHECK(Has(body, "ctx.r10.s64 = ctx.r10.s64 + 65536;"));
    CHECK(Has(body, "ctx.r10.s64 = ctx.r10.s64 + -2;"));
    CHECK(Has(body, "ctx.r9.u64 = ctx.r9.u64 | ctx.r7.u64;"));
    CHECK(Count(body, "\tsub_") == 2);
  }
  const std::string vertex_shader = hooked("MUA_GUEST_SET_VERTEX_SHADER");
  CHECK(Has(vertex_shader, "ctx.r11.u64 = REX_LOAD_U8(ctx.r30.u32 + " + N(fields.flags) + ");"));
  CHECK(Has(vertex_shader, "ctx.r11.u64 = ctx.r11.u32 & 0x7F;"));
  CHECK(Has(vertex_shader, "REX_STORE_U8(ctx.r30.u32 + " + N(fields.flags) + ", ctx.r11.u8);"));

  const std::string stream = hooked("MUA_GUEST_SET_STREAM_SOURCE");
  CHECK(HasInstruction(stream, "stw r?," + N(fields.vertex_fetch_constants + 4) + "(r?)"));
  CHECK(HasInstruction(stream, "subfic r?,r?,17"));
  CHECK(HasInstruction(stream, "addi r?,r?," + N(fields.vertex_fetch_constants / 8)));
  CHECK(HasInstruction(stream, "addi r?,r?,512"));
  CHECK(HasInstruction(stream, "clrlwi r?,r?,3"));
  CHECK(Has(stream, "ctx.r11.s64 = ctx.r29.s64 + " + N(fields.stream_sources / 4) + ";"));
  CHECK(Has(stream, "ctx.r11.u64 = REX_LOAD_U32(ctx.r31.u32 + " + N(fields.current_fence) + ");"));
  CHECK(Has(stream, "REX_STORE_U32(ctx.r28.u32 + 8, ctx.r11.u32);"));
  CHECK(HasInstruction(stream, "stb r?," + N(fields.stream_strides) + "(r?)"));
  CHECK(HasInstruction(stream, "lbz r?," + N(fields.committed_strides) + "(r?)"));
  CHECK(HasInstruction(stream, "std r11," + N(fields.texture_dirty) + "(r?)"));
}

void TestD3DStateSettersMatchFilters() {
#ifdef MUA_GENERATED_ROOT
  std::printf("D3D state setters match their filters\n");
  for (const D3DBuild& build : kD3DBuilds) {
    CheckD3DBuild(build);
  }
#endif
}

// The native Alchemy helpers copy these exact guest instruction sequences (alchemy_native.cpp).
void TestAlchemyHelpersMatchNative() {
#ifdef MUA_GENERATED_ROOT
  std::printf("Alchemy helpers match their native copies\n");
  const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / "mua1";
  if (ReadSource(module / "marvel_ultimate_alliance_register.cpp").empty()) {
    return;
  }
  const auto instructions = [&](const char* function) {
    std::string text;
    std::istringstream lines(GeneratedBody(module, function));
    for (std::string line; std::getline(lines, line);) {
      if (line.starts_with("\t// ")) {
        text += line.substr(4) + ";";
      }
    }
    return text;
  };
  CHECK(instructions("sub_82525F58") ==
        std::string("addi r11,r3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1;stwcx. r10,0,r11;mtmsrd r9,1;bne "
                    "0x82525f5c;blr ;"));
  CHECK(instructions("sub_82549140") ==
        std::string("addi r11,r3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,-1;stwcx. r10,0,r11;mtmsrd r9,1;bne"
                    " 0x82549144;mr r11,r10;clrlwi r11,r11,10;cmpwi cr6,r11,0;bnelr cr6;b 0x825465c0;"));
  CHECK(instructions("sub_82550570") ==
        std::string("mflr r12;stw r12,-8(r1);std r30,-24(r1);std r31,-16(r1);stwu r1,-112(r1);mr r30,r4;mr r31,r3;cmplwi "
                    "cr6,r30,0;beq cr6,0x825505b4;addi r11,r30,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1;stw"
                    "cx. r10,0,r11;mtmsrd r9,1;bne 0x82550598;lwz r3,0(r31);cmplwi cr6,r3,0;beq cr6,0x825505f4;addi r11,r"
                    "3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,-1;stwcx. r10,0,r11;mtmsrd r9,1;bne 0x825505c"
                    "4;mr r11,r10;clrlwi r11,r11,10;cmpwi cr6,r11,0;bne cr6,0x825505f4;bl 0x825465c0;stw r30,0(r31);addi "
                    "r1,r1,112;lwz r12,-8(r1);mtlr r12;ld r30,-24(r1);ld r31,-16(r1);blr ;"));
  CHECK(instructions("sub_82550610") ==
        std::string("mflr r12;stw r12,-8(r1);std r30,-24(r1);std r31,-16(r1);stwu r1,-112(r1);mr r31,r3;mr r30,r4;lwz r3,"
                    "0(r31);cmplwi cr6,r3,0;beq cr6,0x8255066c;addi r11,r3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r"
                    "10,r10,-1;stwcx. r10,0,r11;mtmsrd r9,1;bne 0x8255063c;mr r11,r10;clrlwi r11,r11,10;cmpwi cr6,r11,0;b"
                    "ne cr6,0x8255066c;bl 0x825465c0;stw r30,0(r31);addi r1,r1,112;lwz r12,-8(r1);mtlr r12;ld r30,-24(r1)"
                    ";ld r31,-16(r1);blr ;"));
  CHECK(instructions("sub_8238D848") ==
        std::string("addi r11,r3,4;subf r8,r3,r4;li r10,7;lwz r7,0(r4);addi r9,r11,4;lwz r5,-4(r11);addi r10,r10,-1;lwz r"
                    "6,0(r11);and r7,r5,r7;cmplwi cr6,r10,0;lwz r3,0(r9);stw r7,-4(r11);lwzx r7,r8,r11;and r7,r7,r6;stw r"
                    "7,0(r11);addi r11,r11,12;lwz r7,8(r4);addi r4,r4,12;and r7,r7,r3;stw r7,0(r9);bne cr6,0x8238d854;blr"
                    " ;"));
  CHECK(instructions("sub_824E95F8") ==
        std::string("lwz r11,12(r3);cmpw cr6,r4,r11;bge cr6,0x824e9648;cmpwi cr6,r4,0;blt cr6,0x824e9648;lwz r11,20(r3);l"
                    "i r10,0;lwz r9,8(r11);cmpwi cr6,r9,0;ble cr6,0x824e9640;lwz r11,16(r11);lhz r8,0(r11);cmpw cr6,r8,r4"
                    ";beq cr6,0x824e9648;addi r10,r10,1;addi r11,r11,2;cmpw cr6,r10,r9;blt cr6,0x824e9624;li r3,1;blr ;li"
                    " r3,0;blr ;"));
  CHECK(instructions("sub_82526B90") ==
        std::string("mflr r12;stw r12,-8(r1);std r31,-16(r1);stwu r1,-96(r1);lwz r11,0(r3);mr r31,r4;lwz r11,80(r11);mtct"
                    "r r11;bctrl ;cmplwi cr6,r3,0;beq cr6,0x82526bd0;cmplw cr6,r3,r31;beq cr6,0x82526be8;lwz r3,56(r3);cm"
                    "plwi cr6,r3,0;bne cr6,0x82526bbc;li r3,0;addi r1,r1,96;lwz r12,-8(r1);mtlr r12;ld r31,-16(r1);blr ;l"
                    "i r3,1;addi r1,r1,96;lwz r12,-8(r1);mtlr r12;ld r31,-16(r1);blr ;"));
  CHECK(instructions("sub_82599030") ==
        std::string("lfs f0,0(r4);stfs f0,0(r3);lfs f0,4(r4);stfs f0,4(r3);lfs f0,8(r4);stfs f0,8(r3);lfs f0,12(r4);stfs "
                    "f0,12(r3);lfs f0,16(r4);stfs f0,16(r3);lfs f0,20(r4);stfs f0,20(r3);lfs f0,24(r4);stfs f0,24(r3);lfs"
                    " f0,28(r4);stfs f0,28(r3);lfs f0,32(r4);stfs f0,32(r3);lfs f0,36(r4);stfs f0,36(r3);lfs f0,40(r4);st"
                    "fs f0,40(r3);lfs f0,44(r4);stfs f0,44(r3);lfs f0,48(r4);stfs f0,48(r3);lfs f0,52(r4);stfs f0,52(r3);"
                    "lfs f0,56(r4);stfs f0,56(r3);lfs f0,60(r4);stfs f0,60(r3);blr ;"));
  CHECK(instructions("sub_8235C6A8") ==
        std::string("lwz r11,0(r3);cntlzw r10,r11;rlwinm r10,r10,27,31,31;xori r10,r10,1;cmplwi cr6,r10,0;beqlr cr6;addi "
                    "r4,r11,-8;addi r9,r4,4;mfmsr r10;mtmsrd r13,1;lwarx r11,0,r9;addi r11,r11,-1;stwcx. r11,0,r9;mtmsrd "
                    "r10,1;bne 0x8235c6c8;cmplwi cr6,r11,0;bnelr cr6;lwz r3,0(r4);b 0x82550868;"));
  CHECK(instructions("sub_82374D50") ==
        std::string("mflr r12;stw r12,-8(r1);std r30,-24(r1);std r31,-16(r1);stwu r1,-112(r1);mr r31,r3;mr r30,r4;bl 0x82"
                    "35c380;mr r4,r30;bl 0x82555eb8;lwz r11,0(r31);mr r30,r3;cntlzw r10,r11;rlwinm r10,r10,27,31,31;xori "
                    "r10,r10,1;cmplwi cr6,r10,0;beq cr6,0x82374dc8;addi r4,r11,-8;addi r9,r4,4;mfmsr r10;mtmsrd r13,1;lwa"
                    "rx r11,0,r9;addi r11,r11,-1;stwcx. r11,0,r9;mtmsrd r10,1;bne 0x82374d9c;cmplwi cr6,r11,0;bne cr6,0x8"
                    "2374dc8;lwz r3,0(r4);bl 0x82550868;mr r3,r31;stw r30,0(r31);addi r1,r1,112;lwz r12,-8(r1);mtlr r12;l"
                    "d r30,-24(r1);ld r31,-16(r1);blr ;"));
  CHECK(instructions("sub_8247BB10") ==
        std::string("mflr r12;stw r12,-8(r1);std r31,-16(r1);stwu r1,-96(r1);mr r31,r3;addi r11,r31,4;mfmsr r9;mtmsrd r13"
                    ",1;lwarx r10,0,r11;addi r10,r10,-1;stwcx. r10,0,r11;mtmsrd r9,1;bne 0x8247bb28;mr r3,r10;cmplwi cr6,"
                    "r3,0;bne cr6,0x8247bb7c;lwz r11,0(r31);clrlwi r10,r11,28;cmplwi cr6,r10,4;bne cr6,0x8247bb70;rlwinm."
                    " r11,r11,0,1,1;beq 0x8247bb70;lwz r3,24(r31);bl 0x8247bb10;mr r3,r31;bl 0x8247b570;li r3,0;addi r1,r"
                    "1,96;lwz r12,-8(r1);mtlr r12;ld r31,-16(r1);blr ;"));
  CHECK(instructions("sub_8255D6C0") ==
        std::string("mflr r12;bl 0x828269ac;stwu r1,-112(r1);mr r29,r4;mr r31,r3;cmplwi cr6,r29,0;beq cr6,0x8255d6fc;addi"
                    " r11,r29,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1;stwcx. r10,0,r11;mtmsrd r9,1;bne 0x8"
                    "255d6e0;lwz r11,12(r31);lwz r30,8(r31);cmpw cr6,r30,r11;bge cr6,0x8255d728;addi r11,r30,1;rlwinm r10"
                    ",r30,2,0,29;stw r11,8(r31);lwz r11,16(r31);stwx r29,r10,r11;addi r1,r1,112;b 0x828269fc;li r5,4;addi"
                    " r4,r30,1;mr r3,r31;bl 0x825544d0;lwz r11,16(r31);rlwinm r10,r30,2,0,29;stwx r29,r10,r11;addi r1,r1,"
                    "112;b 0x828269fc;"));
  CHECK(instructions("sub_8259EAC8") ==
        std::string("mflr r12;stw r12,-8(r1);addi r12,r1,-8;bl 0x82827d04;mr r9,r3;cmplw cr6,r9,r4;beq cr6,0x8259ebf8;cmp"
                    "lw cr6,r9,r5;beq cr6,0x8259ebf8;addi r11,r5,32;subf r6,r5,r9;li r8,4;lfs f9,-32(r11);addi r10,r11,-1"
                    "6;lfs f0,0(r4);add r7,r11,r6;fmuls f0,f0,f9;lfs f9,16(r11);lfs f13,12(r4);addi r8,r8,-1;lfs f12,4(r4"
                    ");lfs f8,0(r10);cmplwi cr6,r8,0;lfs f11,0(r11);lfs f10,8(r4);fmadds f0,f13,f9,f0;fmadds f0,f12,f8,f0"
                    ";fmadds f0,f11,f10,f0;stfs f0,0(r9);lfs f13,-32(r11);lfs f0,16(r4);fmuls f0,f0,f13;lfs f12,16(r11);l"
                    "fs f13,28(r4);lfs f11,0(r11);lfs f10,24(r4);lfs f9,0(r10);lfs f8,20(r4);fmadds f0,f13,f12,f0;fmadds "
                    "f0,f11,f10,f0;fmadds f0,f9,f8,f0;stfs f0,-16(r7);lfs f13,0(r10);lfs f0,36(r4);fmuls f0,f0,f13;lfs f1"
                    "2,-32(r11);lfs f13,32(r4);lfs f10,16(r11);lfs f11,44(r4);lfs f9,0(r11);lfs f8,40(r4);fmadds f0,f13,f"
                    "12,f0;fmadds f0,f11,f10,f0;fmadds f0,f9,f8,f0;stfs f0,0(r7);lfs f13,-32(r11);lfs f0,48(r4);fmuls f0,"
                    "f0,f13;lfs f12,16(r11);lfs f13,60(r4);lfs f11,0(r11);addi r11,r11,4;lfs f10,56(r4);lfs f9,0(r10);lfs"
                    " f8,52(r4);fmadds f0,f13,f12,f0;fmadds f0,f11,f10,f0;fmadds f0,f9,f8,f0;stfs f0,48(r9);addi r9,r9,4;"
                    "bne cr6,0x8259eaf8;addi r12,r1,-8;bl 0x82827d50;lwz r12,-8(r1);mtlr r12;blr ;addi r8,r1,-144;lfs f10"
                    ",12(r4);addi r6,r1,-128;lfs f9,8(r4);subf r7,r5,r8;lfs f8,4(r4);lfs f7,0(r4);addi r10,r1,-128;lfs f6"
                    ",28(r4);addi r11,r5,32;lfs f5,24(r4);subf r6,r5,r6;lfs f4,20(r4);li r8,4;lfs f3,16(r4);lfs f2,44(r4)"
                    ";lfs f1,40(r4);lfs f31,36(r4);lfs f30,32(r4);lfs f29,60(r4);lfs f28,56(r4);lfs f27,52(r4);lfs f26,48"
                    "(r4);lfs f0,-16(r11);addi r8,r8,-1;fmuls f25,f8,f0;lfs f13,-32(r11);fmuls f24,f4,f0;lfs f12,16(r11);"
                    "fmuls f23,f31,f0;lfs f11,0(r11);fmuls f0,f27,f0;cmplwi cr6,r8,0;fmadds f25,f7,f13,f25;fmadds f24,f3,"
                    "f13,f24;fmadds f23,f30,f13,f23;fmadds f0,f26,f13,f0;fmadds f13,f10,f12,f25;fmadds f25,f6,f12,f24;fma"
                    "dds f24,f2,f12,f23;fmadds f0,f29,f12,f0;fmadds f13,f9,f11,f13;stfs f13,-16(r10);fmadds f13,f5,f11,f2"
                    "5;stfs f13,0(r10);fmadds f13,f1,f11,f24;stfsx f13,r7,r11;fmadds f0,f28,f11,f0;stfsx f0,r6,r11;addi r"
                    "10,r10,4;addi r11,r11,4;bne cr6,0x8259ec54;lfs f0,-144(r1);lfs f13,-140(r1);stfs f0,0(r9);stfs f13,4"
                    "(r9);lfs f0,-136(r1);lfs f13,-132(r1);stfs f0,8(r9);stfs f13,12(r9);lfs f0,-128(r1);lfs f13,-124(r1)"
                    ";stfs f0,16(r9);stfs f13,20(r9);lfs f0,-120(r1);lfs f13,-116(r1);stfs f0,24(r9);stfs f13,28(r9);lfs "
                    "f0,-112(r1);lfs f13,-108(r1);stfs f0,32(r9);stfs f13,36(r9);lfs f0,-104(r1);lfs f13,-100(r1);stfs f0"
                    ",40(r9);stfs f13,44(r9);lfs f0,-96(r1);lfs f13,-92(r1);stfs f0,48(r9);stfs f13,52(r9);lfs f0,-88(r1)"
                    ";lfs f13,-84(r1);stfs f0,56(r9);stfs f13,60(r9);addi r12,r1,-8;bl 0x82827d50;lwz r12,-8(r1);mtlr r12"
                    ";blr ;"));
  const std::string native = ReadSource(kSources / "alchemy_native.cpp");
  CHECK(Has(native, "kImageBase = 0x82000000;") && Has(native, "kThunkReserveSize = 0x10000;"));
#endif
}

std::string Hex(uint32_t value) {
  char text[16];
  std::snprintf(text, sizeof(text), "%X", value);
  return text;
}

uint32_t FunctionAddress(const std::string& function) {
  return uint32_t(std::stoul(function.substr(4), nullptr, 16));
}

struct AlchemyModule {
  const char* key;
  const char* map;
  uint32_t image_size;
  uint32_t code_base;
  uint32_t code_size;
  uint32_t matrix_epsilon;
};

const AlchemyModule kAlchemyModules[] = {
    {"mua1", "guest_map_mua1.h", mua1::mua::guest::kImageSize, mua1::mua::guest::kCodeBase,
     mua1::mua::guest::kCodeSize, mua1::mua::guest::kMatrixEpsilon},
    {"mua1-gold", "guest_map_mua1_gold.h", gold::mua::guest::kImageSize, gold::mua::guest::kCodeBase,
     gold::mua::guest::kCodeSize, gold::mua::guest::kMatrixEpsilon},
};

// Each game's map gives the native helpers its own module's layout, and the epsilon its matrix compare loads.
void TestAlchemyModuleConstants() {
#ifdef MUA_GENERATED_ROOT
  std::printf("Alchemy helpers use their module's layout and epsilon\n");
  for (const AlchemyModule& game : kAlchemyModules) {
    const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / game.key;
    const std::string pch = ReadSource(module / "marvel_ultimate_alliance_pch.h");
    if (pch.empty()) {
      std::printf("  %s: module not generated, skipped\n", game.key);
      continue;
    }
    CHECK(Has(pch, "#define REX_IMAGE_BASE 0x82000000ull\n"));
    CHECK(Has(pch, "#define REX_IMAGE_SIZE 0x" + Hex(game.image_size) + "ull\n"));
    CHECK(Has(pch, "#define REX_CODE_BASE 0x" + Hex(game.code_base) + "ull\n"));
    CHECK(Has(pch, "#define REX_CODE_SIZE 0x" + Hex(game.code_size) + "ull\n"));
    CHECK(Has(pch, "#define REX_THUNK_RESERVE_SIZE 0x10000ull\n"));
    const std::string compare =
        GeneratedBody(module, Hooked(ReadSource(kSources / game.map), "MUA_GUEST_MATRIX_NEARLY_EQUAL"));
    const int high = int16_t((game.matrix_epsilon + 0x8000) >> 16);
    const int low = int16_t(game.matrix_epsilon & 0xFFFF);
    CHECK(Count(compare, "// fsubs ") == 16);
    CHECK(Count(compare, "// clrlwi r11,r11,1\n") == 16);
    CHECK(Count(compare, "// fcmpu cr6,f13,f0\n") == 16);
    CHECK(Has(compare, "// lis r11," + std::to_string(high) + "\n") &&
          Has(compare, "// lfs f0," + std::to_string(low) + "(r11)\n"));
    CHECK(Has(compare, "// clrlwi r3,r11,24\n") && !Has(compare, "\tsub_"));
    CHECK(Count(compare, "(r3)\n") == 16 && Count(compare, "(r4)\n") == 16);
  }
#endif
}

std::map<uint32_t, std::string> MappedHooks(const std::string& map) {
  std::map<uint32_t, std::string> hooks;
  const std::regex hook(R"(#define (MUA_GUEST_[A-Z_]+) (sub_[0-9A-F]{8}))");
  for (std::sregex_iterator it(map.begin(), map.end(), hook), end; it != end; ++it) {
    hooks[FunctionAddress((*it)[2].str())] = (*it)[1].str();
  }
  return hooks;
}

// A function's instructions with its own branch targets as offsets from its start and its calls out as the hook the
// game's map binds there (or the runtime helper the call becomes), so two games' copies of a function compare equal.
std::string Fingerprint(const std::filesystem::path& module, const std::string& function,
                        const std::map<uint32_t, std::string>& hooks) {
  std::vector<std::string> lines;
  std::istringstream body(GeneratedBody(module, function));
  for (std::string line; std::getline(body, line);) {
    lines.push_back(line);
  }
  size_t count = 0;
  for (const std::string& line : lines) {
    count += line.starts_with("\t// ");
  }
  const uint32_t start = FunctionAddress(function);
  const uint32_t end = start + uint32_t(count) * 4;
  const std::regex target("0x([0-9a-f]{8})");
  const std::regex helper(R"((__\w+)\(ctx, base\))");
  std::string text;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (!lines[i].starts_with("\t// ")) {
      continue;
    }
    const std::string instruction = lines[i].substr(4);
    std::smatch found;
    if (!std::regex_search(instruction, found, target)) {
      text += instruction + ";";
      continue;
    }
    const uint32_t address = uint32_t(std::stoul(found[1].str(), nullptr, 16));
    std::string name = "external";
    if (address >= start && address < end) {
      name = "+" + std::to_string(address - start);
    } else if (const auto hook = hooks.find(address); hook != hooks.end()) {
      name = hook->second;
    } else {
      std::smatch call;
      for (size_t j = i + 1; j < lines.size() && !lines[j].starts_with("\t// "); ++j) {
        if (std::regex_search(lines[j], call, helper)) {
          name = call[1].str();
          break;
        }
      }
    }
    text += found.prefix().str() + name + found.suffix().str() + ";";
  }
  return text;
}

// Gold Edition's Alchemy helpers are MUA1's instruction for instruction, calling Gold Edition's own copies of the same
// functions, and the guest return addresses the native copies give their calls sit at the same offsets.
void TestGoldAlchemyHelpersMatchMua1() {
#ifdef MUA_GENERATED_ROOT
  std::printf("Gold Edition Alchemy helpers match MUA1's\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  if (ReadSource(root / "mua1" / "marvel_ultimate_alliance_register.cpp").empty() ||
      ReadSource(root / "mua1-gold" / "marvel_ultimate_alliance_register.cpp").empty()) {
    std::printf("  modules not generated, skipped\n");
    return;
  }
  const std::string mua1_map = ReadSource(kSources / "guest_map_mua1.h");
  const std::string gold_map = ReadSource(kSources / "guest_map_mua1_gold.h");
  const std::map<uint32_t, std::string> mua1_hooks = MappedHooks(mua1_map);
  const std::map<uint32_t, std::string> gold_hooks = MappedHooks(gold_map);
  for (const char* hook :
       {"MUA_GUEST_ADD_REF", "MUA_GUEST_RELEASE", "MUA_GUEST_ASSIGN_REF", "MUA_GUEST_ASSIGN_ADOPTED_REF",
        "MUA_GUEST_AND_MASKS", "MUA_GUEST_INDEX_ALLOWED", "MUA_GUEST_IS_OF_TYPE", "MUA_GUEST_MATRIX_MULTIPLY",
        "MUA_GUEST_MATRIX_COPY", "MUA_GUEST_RELEASE_STRING", "MUA_GUEST_ASSIGN_STRING", "MUA_GUEST_RELEASE_RESOURCE",
        "MUA_GUEST_APPEND_REF", "MUA_GUEST_FREE_STRING", "MUA_GUEST_STRING_INTERN", "MUA_GUEST_RESIZE_ARRAY"}) {
    const std::string mua1_print = Fingerprint(root / "mua1", Hooked(mua1_map, hook), mua1_hooks);
    const std::string gold_print = Fingerprint(root / "mua1-gold", Hooked(gold_map, hook), gold_hooks);
    if (mua1_print.empty() || mua1_print != gold_print) {
      std::printf("  %s differs\n", hook);
    }
    CHECK(!mua1_print.empty() && mua1_print == gold_print);
  }

  struct Site {
    uint32_t mua1;
    uint32_t gold;
    const char* hook;
  };
  const Site sites[] = {
      {mua1::mua::guest::kIsOfTypeMetaSite, gold::mua::guest::kIsOfTypeMetaSite, "MUA_GUEST_IS_OF_TYPE"},
      {mua1::mua::guest::kAssignStringPrepareSite, gold::mua::guest::kAssignStringPrepareSite,
       "MUA_GUEST_ASSIGN_STRING"},
      {mua1::mua::guest::kAssignStringInternSite, gold::mua::guest::kAssignStringInternSite, "MUA_GUEST_ASSIGN_STRING"},
      {mua1::mua::guest::kAssignStringFreeSite, gold::mua::guest::kAssignStringFreeSite, "MUA_GUEST_ASSIGN_STRING"},
      {mua1::mua::guest::kReleaseResourceParentSite, gold::mua::guest::kReleaseResourceParentSite,
       "MUA_GUEST_RELEASE_RESOURCE"},
      {mua1::mua::guest::kReleaseResourceDestroySite, gold::mua::guest::kReleaseResourceDestroySite,
       "MUA_GUEST_RELEASE_RESOURCE"},
      {mua1::mua::guest::kAppendRefResizeSite, gold::mua::guest::kAppendRefResizeSite, "MUA_GUEST_APPEND_REF"},
  };
  for (const Site& site : sites) {
    const uint32_t mua1_function = FunctionAddress(Hooked(mua1_map, site.hook));
    const uint32_t gold_function = FunctionAddress(Hooked(gold_map, site.hook));
    CHECK(site.mua1 > mua1_function && site.mua1 - mua1_function < 0x100);
    CHECK(site.mua1 - mua1_function == site.gold - gold_function);
  }
#endif
}

// A fingerprint's mnemonics and the hooks and runtime helpers it calls, without the other operands.
std::string Shape(const std::string& fingerprint) {
  const std::regex callee(R"((MUA_GUEST_\w+|__\w+))");
  std::string shape;
  std::istringstream instructions(fingerprint);
  for (std::string instruction; std::getline(instructions, instruction, ';');) {
    shape += instruction.substr(0, instruction.find(' '));
    std::smatch name;
    if (std::regex_search(instruction, name, callee)) {
      shape += " " + name[1].str();
    }
    shape += ";";
  }
  return shape;
}

// Every other function Gold Edition hooks is MUA1's in shape, calling the same hooks and imports; the guest wrappers of
// the save, profile and content imports and the audio and memory routines are MUA1's instruction for instruction.
// The rest were rebuilt by Gold Edition's later XDK or its own code changes: the D3D routines and the present path,
// checked per build above, and game functions whose hooks were written against both games together.
void TestGoldHooksMatchMua1() {
#ifdef MUA_GENERATED_ROOT
  std::printf("Gold Edition hooks match MUA1's functions\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  if (ReadSource(root / "mua1" / "marvel_ultimate_alliance_register.cpp").empty() ||
      ReadSource(root / "mua1-gold" / "marvel_ultimate_alliance_register.cpp").empty()) {
    std::printf("  modules not generated, skipped\n");
    return;
  }
  const std::set<std::string> rebuilt = {
      "MUA_GUEST_AUDIO_RENDER", "MUA_GUEST_ITEM_USE", "MUA_GUEST_MENU_ITEM_CREATE", "MUA_GUEST_MOVIE_MENU_UPDATE",
      "MUA_GUEST_OPEN_MENU", "MUA_GUEST_OPTIONS_INIT", "MUA_GUEST_PRESENT_CALLBACK", "MUA_GUEST_PROFILE_REFRESH_PAD",
      "MUA_GUEST_SCRIPT_DISPATCH", "MUA_GUEST_SET_PIXEL_SHADER", "MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS",
      "MUA_GUEST_SET_STREAM_SOURCE", "MUA_GUEST_SET_VERTEX_DECLARATION", "MUA_GUEST_SET_VERTEX_SHADER",
      "MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS", "MUA_GUEST_STRING_DATA", "MUA_GUEST_SWAP",
      "MUA_GUEST_UPLOAD_ALU_CONSTANTS", "MUA_GUEST_WORLD_UPDATE"};
  const std::set<std::string> identical = {
      "MUA_GUEST_AUDIO_BARRIER",   "MUA_GUEST_RESET_EVENT",       "MUA_GUEST_FENCE_BEGIN",
      "MUA_GUEST_MEMCPY",          "MUA_GUEST_MEMMOVE",           "MUA_GUEST_MEMSET",
      "MUA_GUEST_PROFILE_READ",    "MUA_GUEST_PROFILE_WRITE",     "MUA_GUEST_CONTENT_CREATE",
      "MUA_GUEST_CONTENT_CLOSE",   "MUA_GUEST_CONTENT_ENUMERATE", "MUA_GUEST_DEVICE_SELECTOR"};
  const std::string mua1_map = ReadSource(kSources / "guest_map_mua1.h");
  const std::string gold_map = ReadSource(kSources / "guest_map_mua1_gold.h");
  const std::map<uint32_t, std::string> mua1_hooks = MappedHooks(mua1_map);
  const std::map<uint32_t, std::string> gold_hooks = MappedHooks(gold_map);
  size_t compared = 0;
  for (const auto& [address, hook] : mua1_hooks) {
    if (rebuilt.contains(hook)) {
      continue;
    }
    const std::string mua1_print = Fingerprint(root / "mua1", Hooked(mua1_map, hook), mua1_hooks);
    const std::string gold_print = Fingerprint(root / "mua1-gold", Hooked(gold_map, hook), gold_hooks);
    const bool same = identical.contains(hook) ? mua1_print == gold_print : Shape(mua1_print) == Shape(gold_print);
    if (mua1_print.empty() || !same) {
      std::printf("  %s differs\n", hook.c_str());
    }
    CHECK(!mua1_print.empty() && same);
    ++compared;
  }
  CHECK(compared + rebuilt.size() == mua1_hooks.size());
#endif
}

size_t CountMatches(const std::string& text, const char* pattern) {
  const std::regex re(pattern);
  return size_t(std::distance(std::sregex_iterator(text.begin(), text.end(), re), std::sregex_iterator()));
}

// The guest instructions of a function, joined as "instruction;".
std::string Instructions(const std::filesystem::path& module, const std::string& function) {
  std::string text;
  std::istringstream lines(GeneratedBody(module, function));
  for (std::string line; std::getline(lines, line);) {
    if (line.starts_with("\t// ")) {
      text += line.substr(4) + ";";
    }
  }
  return text;
}

// MUA 2's later Alchemy build: the reference helpers its native copies replace, the count mask, handle flag, handle
// manager and handle path calls its release uses, and its matrix compare with the epsilon it loads.
void TestMua2AlchemyHelpers() {
#ifdef MUA_GENERATED_ROOT
  std::printf("MUA 2 Alchemy helpers match their native copies\n");
  const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / "mua2";
  if (ReadSource(module / "marvel_ultimate_alliance_register.cpp").empty()) {
    std::printf("  mua2: module not generated, skipped\n");
    return;
  }
  const std::string map = ReadSource(kSources / "guest_map_mua2.h");
  const auto instructions = [&](const char* hook) { return Instructions(module, Hooked(map, hook)); };
  CHECK(instructions("MUA_GUEST_ADD_REF") ==
        std::string("addi r11,r3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1;stwcx. r10,0,r11;mtmsrd r9,1;bne "
                    "0x8228f704;blr ;"));
  CHECK(instructions("MUA_GUEST_ADD_REF_IF_ANY") ==
        std::string("cmplwi cr6,r3,0;beqlr cr6;addi r11,r3,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1;stwcx. "
                    "r10,0,r11;mtmsrd r9,1;bne 0x8228f734;blr ;"));
  CHECK(instructions("MUA_GUEST_SWAP_REF") ==
        std::string("cmplwi cr6,r4,0;beq cr6,0x82291208;addi r11,r4,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10,r10,1"
                    ";stwcx. r10,0,r11;mtmsrd r9,1;bne 0x822911ec;cmplwi cr6,r3,0;beqlr cr6;b 0x8228f638;"));
  CHECK(instructions("MUA_GUEST_ASSIGN_REF") ==
        std::string("mflr r12;stw r12,-8(r1);std r30,-24(r1);std r31,-16(r1);stwu r1,-112(r1);mr r31,r3;lwz r3,0(r3);mr r"
                    "30,r4;cmplwi cr6,r4,0;beq cr6,0x82216430;addi r11,r4,4;mfmsr r9;mtmsrd r13,1;lwarx r10,0,r11;addi r10"
                    ",r10,1;stwcx. r10,0,r11;mtmsrd r9,1;bne 0x82216414;cmplwi cr6,r3,0;beq cr6,0x8221643c;bl 0x8228f638;s"
                    "tw r30,0(r31);mr r3,r31;addi r1,r1,112;lwz r12,-8(r1);mtlr r12;ld r30,-24(r1);ld r31,-16(r1);blr ;"));
  CHECK(instructions("MUA_GUEST_RELEASE") ==
        std::string("mflr r12;stw r12,-8(r1);std r31,-16(r1);stwu r1,-112(r1);lwz r10,4(r3);mr r31,r3;addi r11,r3,4;rlwinm"
                    " r10,r10,11,31,31;mfmsr r8;mtmsrd r13,1;lwarx r9,0,r11;addi r9,r9,-1;stwcx. r9,0,r11;mtmsrd r8,1;bne 0"
                    "x8228f658;addi r7,r9,1;mr r11,r9;clrlwi r11,r7,13;cmplwi cr6,r11,1;bne cr6,0x8228f6a4;mr r3,r31;bl 0x8"
                    "228f440;addi r1,r1,112;lwz r12,-8(r1);mtlr r12;ld r31,-16(r1);blr ;clrlwi r10,r10,24;cmplwi cr6,r10,0;"
                    "beq cr6,0x8228f6dc;cmplwi cr6,r11,2;bne cr6,0x8228f6dc;lis r11,-31945;lwz r11,-13324(r11);cmplwi cr6,r"
                    "11,0;beq cr6,0x8228f6dc;bl 0x82261a00;mr r4,r3;addi r3,r1,80;mr r5,r31;bl 0x82286d78;addi r1,r1,112;lw"
                    "z r12,-8(r1);mtlr r12;ld r31,-16(r1);blr ;"));
  // clrlwi r11,r7,13 keeps 19 bits; rlwinm r10,r10,11,31,31 takes bit 21; lis -31945 / lwz -13324 is the manager.
  CHECK(mua2::mua::guest::kRefCountMask == (uint32_t(1) << 19) - 1);
  CHECK(mua2::mua::guest::kObjectHandleFlag == uint32_t(1) << 21);
  CHECK(mua2::mua::guest::kObjectHandleManager == (uint32_t(-31945) << 16) - 13324u);
  CHECK(Hooked(map, "MUA_GUEST_DESTROY_OBJECT") == "sub_8228F440");
  const std::string release = GeneratedBody(module, Hooked(map, "MUA_GUEST_RELEASE"));
  CHECK(Has(release, "ctx.lr = 0x" + Hex(mua2::mua::guest::kReleaseHandleManagerSite) + ";\n\t" +
                         Hooked(map, "MUA_GUEST_HANDLE_MANAGER") + "(ctx, base);"));
  CHECK(Has(release, "ctx.lr = 0x" + Hex(mua2::mua::guest::kReleaseHandleSite) + ";\n\t" +
                         Hooked(map, "MUA_GUEST_RELEASE_HANDLE") + "(ctx, base);"));
  CHECK(Has(release, "\t" + Hooked(map, "MUA_GUEST_DESTROY_OBJECT") + "(ctx, base);"));
  const std::string manager = instructions("MUA_GUEST_HANDLE_MANAGER");
  CHECK(Has(manager, "lis r30,-31945;lwz r3,-13324(r30);cmplwi cr6,r3,0;bne cr6,0x82261af4;"));

  const std::string compare = GeneratedBody(module, Hooked(map, "MUA_GUEST_MATRIX_NEARLY_EQUAL"));
  CHECK(Count(compare, "// fsubs ") == 16);
  CHECK(CountMatches(compare, R"(// clrlwi r\d+,r\d+,1\n)") == 16);
  CHECK(CountMatches(compare, R"(// fcmpu cr6,f\d+,f0\n)") == 16);
  std::smatch epsilon;
  CHECK(std::regex_search(compare, epsilon,
                          std::regex(R"(// lis r11,(-?\d+)\n[\s\S]*?// addi r10,r11,(-?\d+)\n[\s\S]*?// lfs f0,(-?\d+)\(r10\)\n)")));
  if (!epsilon.empty()) {
    const uint32_t address = (uint32_t(std::stoi(epsilon[1].str())) << 16) + uint32_t(std::stoi(epsilon[2].str())) +
                             uint32_t(std::stoi(epsilon[3].str()));
    CHECK(address == mua2::mua::guest::kMatrixEpsilon);
  }
  CHECK(Has(compare, "// clrlwi r3,r11,24\n") && !Has(compare, "\tsub_"));
  CHECK(Count(compare, "(r3)\n") == 16 && Count(compare, "(r4)\n") == 16);
#endif
}

// The function of the module whose generated code has the needle.
std::string BodyContaining(const std::filesystem::path& module, const std::string& needle) {
  for (const auto& entry : std::filesystem::directory_iterator(module)) {
    if (entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadSource(entry.path());
    const size_t at = text.find(needle);
    if (at == std::string::npos) {
      continue;
    }
    const size_t start = text.rfind("DEFINE_REX_FUNC(", at);
    const size_t end = text.find("DEFINE_REX_FUNC(", at);
    return start == std::string::npos ? std::string()
                                      : text.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }
  return {};
}

// The ring waits gpu_fence_wait blocks: each mapped site returns from the fence poll into the D3D wait that polls the
// writeback word it blocks on (the ring read pointer at +60, the progress word at +4 of the device's fence block).
void TestGpuWaitSites() {
#ifdef MUA_GENERATED_ROOT
  std::printf("GPU wait sites poll their writeback words\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  for (const MapFile& map : kMaps) {
    if (ReadSource(root / map.generated_dir / "marvel_ultimate_alliance_register.cpp").empty()) {
      continue;
    }
    const std::string text = ReadSource(kSources / map.file);
    const std::set<std::string> fence = Matches(text, R"(kDeviceFenceOffset = (\d+);)");
    CHECK(fence.size() == 1);
    if (fence.size() != 1) {
      continue;
    }
    for (const auto& [name, word] : {std::pair{"kRingSpacePollSite", 60}, std::pair{"kRingProgressPollSite", 4}}) {
      const std::set<std::string> site = Matches(text, (std::string(name) + R"( = 0x([0-9A-F]{8});)").c_str());
      CHECK(site.size() == 1);
      if (site.size() != 1) {
        continue;
      }
      const std::string body = BodyContaining(root / map.generated_dir, "ctx.lr = 0x" + *site.begin() + ";\n\t" +
                                                                            Hooked(text, "MUA_GUEST_FENCE_POLL") +
                                                                            "(ctx, base);");
      CHECK(HasInstruction(body, "lwz r?," + *fence.begin() + "(r?)"));
      CHECK(HasInstruction(body, "lwz r11," + std::to_string(word) + "(r?)"));
      CHECK(Has(body, "\t" + Hooked(text, "MUA_GUEST_FENCE_BEGIN") + "(ctx, base);"));
    }
  }
#endif
}

// The lock-free reference count helpers rely on the game never using the recompiler's global interrupt lock for more
// than one reservation pair: anything else between mtmsrd r13 and the next mtmsrd would need that lock's exclusion.
void TestGlobalLockSectionsAreSingleAtomics() {
#ifdef MUA_GENERATED_ROOT
  std::printf("global interrupt lock sections are single atomics\n");
  const std::set<std::string> allowed = {"lwarx",  "ldarx",  "stwcx.", "stdcx.", "addi",   "add",  "subf",
                                         "or",     "and",    "andc",   "mr",     "li",     "cmpw", "cmplw",
                                         "cmpd",   "cmpwi",  "cmplwi", "bne",    "beq",    "rlwinm", "clrlwi"};
  for (const char* key : {"mua1", "mua1-gold", "mua2"}) {
    const std::filesystem::path module = std::filesystem::path(MUA_GENERATED_ROOT) / key;
    if (ReadSource(module / "marvel_ultimate_alliance_register.cpp").empty()) {
      std::printf("  %s: module not generated, skipped\n", key);
      continue;
    }
    size_t sections = 0;
    for (const auto& entry : std::filesystem::directory_iterator(module)) {
      if (entry.path().filename().string().find("_recomp.") == std::string::npos) {
        continue;
      }
      std::istringstream lines(ReadSource(entry.path()));
      bool inside = false;
      std::string first;
      std::string last;
      for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("\t// ")) {
          continue;
        }
        const std::string instruction = line.substr(4);
        const std::string op = instruction.substr(0, instruction.find(' '));
        if (op == "mtmsrd") {
          if (inside) {
            CHECK((first == "lwarx" || first == "ldarx") && (last == "stwcx." || last == "stdcx."));
          }
          inside = instruction.starts_with("mtmsrd r13,");
          sections += inside;
          first.clear();
          continue;
        }
        if (inside) {
          CHECK(allowed.contains(op));
          if (first.empty()) {
            first = op;
          }
          last = op;
        }
      }
    }
    CHECK(sections > 500);
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
  TestD3DStateSettersMatchFilters();
  TestAlchemyHelpersMatchNative();
  TestAlchemyModuleConstants();
  TestGoldAlchemyHelpersMatchMua1();
  TestGoldHooksMatchMua1();
  TestMua2AlchemyHelpers();
  TestGpuWaitSites();
  TestGlobalLockSectionsAreSingleAtomics();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
