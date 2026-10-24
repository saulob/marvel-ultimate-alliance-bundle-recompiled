// Main menu on MUA 2: the Xbox LIVE tab keeps its slot, focus, animation and navigation and becomes Quit Game. Its label
// and subtitle are rewritten in the inflated Lua and scene data, activating it ends the title through
// XamLoaderTerminateTitle, and nothing outside MUA 2 (MUA1, Gold, Skip Intro, the shutdown patch) is touched.
// Run: mua_main_menu_mua2_tests

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "main_menu_mua2.h"

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

namespace menu = mua::main_menu_mua2;

const std::filesystem::path kSrcDir = MUA_SRC_DIR;
const std::filesystem::path kRoot = kSrcDir / "..";

std::string ReadSource(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

uint32_t CountOf(const std::vector<uint8_t>& memory, std::string_view text) {
  uint32_t count = 0;
  for (auto at = std::search(memory.begin(), memory.end(), text.begin(), text.end()); at != memory.end();
       at = std::search(at + 1, memory.end(), text.begin(), text.end())) {
    ++count;
  }
  return count;
}

// Guest memory laid out the way the game's compiled Lua and scene files store strings.
class Blob {
 public:
  explicit Blob(size_t size = 0x4000) : bytes_(size, 0xAB) {}

  uint32_t LuaString(uint32_t at, std::string_view text) {
    bytes_[at++] = 4;
    PutU32(at, uint32_t(text.size() + 1));
    at += 4;
    std::memcpy(&bytes_[at], text.data(), text.size());
    at += uint32_t(text.size());
    bytes_[at++] = 0;
    return at;
  }

  uint32_t SceneString(uint32_t at, std::string_view text) {
    PutU32(at, uint32_t(text.size()));
    at += 4;
    std::memcpy(&bytes_[at], text.data(), text.size());
    return at + uint32_t(text.size());
  }

  void PutU32(uint32_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      bytes_[at + i] = uint8_t(value >> (24 - 8 * i));
    }
  }

  std::vector<uint8_t>& bytes() { return bytes_; }
  uint8_t* base() { return bytes_.data(); }

 private:
  std::vector<uint8_t> bytes_;
};

constexpr uint32_t kStart = 0x100;

// The state names of MainMenu\Init.lua in the order the tab strip shows them.
uint32_t BuildStateNames(Blob& blob, uint32_t at) {
  for (const char* name : {"Review", "Credits", "Xbox LIVE", "Play Game", "Options"}) {
    at = blob.LuaString(at, name);
    at = blob.LuaString(at, "useMainTabSet");
  }
  return blob.LuaString(at, "Join Xbox LIVE Party Session");
}

uint32_t BuildSubtitles(Blob& blob, uint32_t at) {
  for (const char* text : {"Begin New Game or Load Saved Game", "Play Online",
                           "Review Unlocked Movies, Audio Logs, Concept Art, and Dossiers",
                           "View Game Controls or Adjust Game, Audio, and Video Settings", "Online Lobby"}) {
    at = blob.SceneString(at, "duty-regular");
    at = blob.SceneString(at, text);
  }
  return at;
}

void TestLabelBecomesQuitGame() {
  std::printf("tab label\n");
  Blob blob;
  const uint32_t end = BuildStateNames(blob, kStart);
  const std::vector<uint8_t> before = blob.bytes();
  CHECK(menu::PatchMenuText(blob.base(), kStart, end) == 1);
  CHECK(CountOf(blob.bytes(), "Quit Game") == 1);
  CHECK(CountOf(blob.bytes(), std::string_view("\x0a" "Quit Game\x00", 11)) == 1);
  CHECK(CountOf(blob.bytes(), "Xbox LIVE") == 1);
  CHECK(CountOf(blob.bytes(), "Join Xbox LIVE Party Session") == 1);
  for (const char* kept : {"Review", "Credits", "Play Game", "Options"}) {
    CHECK(CountOf(blob.bytes(), kept) == CountOf(before, kept));
  }
  CHECK(blob.bytes().size() == before.size());
  int changed = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    changed += before[i] != blob.bytes()[i];
  }
  CHECK(changed == 8);
}

void TestStateOrderIsKept() {
  std::printf("tab order\n");
  Blob blob;
  const uint32_t end = BuildStateNames(blob, kStart);
  menu::PatchMenuText(blob.base(), kStart, end);
  std::vector<std::string> names;
  for (uint32_t at = kStart; at < end;) {
    const uint32_t length = (uint32_t(blob.bytes()[at + 1]) << 24) | (uint32_t(blob.bytes()[at + 2]) << 16) |
                            (uint32_t(blob.bytes()[at + 3]) << 8) | blob.bytes()[at + 4];
    const std::string text(reinterpret_cast<const char*>(&blob.bytes()[at + 5]), length - 1);
    if (text != "useMainTabSet") {
      names.push_back(text);
    }
    at += 5 + length;
  }
  CHECK((names == std::vector<std::string>{"Review", "Credits", "Quit Game", "Play Game", "Options",
                                           "Join Xbox LIVE Party Session"}));
}

void TestSubtitleLeavesNoOnlineWording() {
  std::printf("subtitle\n");
  Blob blob;
  const uint32_t end = BuildSubtitles(blob, kStart);
  const std::vector<uint8_t> before = blob.bytes();
  CHECK(menu::PatchMenuText(blob.base(), kStart, end) == 1);
  CHECK(CountOf(blob.bytes(), "Play Online") == 0);
  CHECK(CountOf(blob.bytes(), std::string_view("\x00\x00\x00\x0b" "Exit Game  ", 15)) == 1);
  for (const char* kept : {"Begin New Game or Load Saved Game", "Review Unlocked Movies, Audio Logs, Concept Art, and Dossiers",
                           "View Game Controls or Adjust Game, Audio, and Video Settings", "Online Lobby"}) {
    CHECK(CountOf(blob.bytes(), kept) == 1);
  }
  int changed = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    changed += before[i] != blob.bytes()[i];
  }
  CHECK(changed == 10);
  CHECK(menu::kSubtitlePatch.find.size() == menu::kSubtitlePatch.replace.size());
  CHECK(menu::kLabelPatch.find.size() == menu::kLabelPatch.replace.size());
}

void TestStringsSplitAcrossInflateCalls() {
  std::printf("strings split between inflate calls\n");
  Blob blob;
  const uint32_t end = BuildStateNames(blob, kStart);
  const uint32_t at = uint32_t(std::distance(blob.bytes().begin(), std::search(blob.bytes().begin(), blob.bytes().end(),
                                              menu::kLabelPatch.find.begin(), menu::kLabelPatch.find.end())));
  CHECK(menu::PatchMenuText(blob.base(), kStart, at + 6) == 0);
  CHECK(menu::PatchMenuText(blob.base(), at + 6, end) == 1);
  CHECK(CountOf(blob.bytes(), "Quit Game") == 1);
}

void TestOtherDataIsLeftAlone() {
  std::printf("unrelated data\n");
  Blob blob;
  BuildStateNames(blob, kStart);
  const std::vector<uint8_t> before = blob.bytes();
  CHECK(menu::PatchMenuText(blob.base(), kStart, kStart) == 0);
  CHECK(menu::PatchMenuText(blob.base(), kStart + 10, kStart) == 0);
  CHECK(menu::PatchMenuText(blob.base(), 0, menu::kMaxScannedBytes + 1) == 0);
  CHECK(blob.bytes() == before);

  Blob other;
  other.LuaString(kStart, "Xbox LIVE party");
  other.SceneString(0x200, "Play Online Lobby Game");
  other.PutU32(0x300, 0x0c);
  const std::vector<uint8_t> snapshot = other.bytes();
  menu::PatchMenuText(other.base(), 0, 0x400);
  CHECK(CountOf(other.bytes(), "Quit Game") == 0);
  CHECK(CountOf(other.bytes(), "Exit Game") == 0);
  CHECK(other.bytes() == snapshot);
}

void TestEachPatchKeepsItsLength() {
  std::printf("patch bytes\n");
  for (const menu::TextPatch& patch : menu::kPatches) {
    CHECK(patch.find.size() == patch.replace.size());
    CHECK(patch.find.size() == 15);
    CHECK(patch.find.substr(0, 4) == patch.replace.substr(0, 4));
  }
  CHECK(menu::kLabelPatch.replace.find("Xbox") == std::string_view::npos);
  CHECK(menu::kLabelPatch.replace.find("LIVE") == std::string_view::npos);
  CHECK(menu::kSubtitlePatch.replace.find("Online") == std::string_view::npos);
  CHECK(menu::kSubtitlePatch.replace.find("Xbox") == std::string_view::npos);
}

void TestGuestMemoryIsBigEndian() {
  std::printf("guest pointers\n");
  Blob blob;
  blob.PutU32(0x40, 0x00001234);
  CHECK(menu::LoadU32(blob.base(), 0x40) == 0x1234);
  CHECK(menu::kStreamNextOutOffset == 12);
}

void TestQuitUsesTheTitleExitPath() {
  std::printf("quit game uses the title exit path\n");
  const std::string source = ReadSource(kSrcDir / "main_menu_mua2.cpp");
  const size_t signin = source.find("REX_HOOK_RAW(MUA_GUEST_MENU_SIGNIN)");
  CHECK(signin != std::string::npos);
  const std::string hook = signin == std::string::npos ? std::string() : source.substr(signin);
  CHECK(Has(source, "REX_EXTERN(__imp__XamLoaderTerminateTitle);"));
  CHECK(Has(hook, "__imp__XamLoaderTerminateTitle(ctx, base);"));
  CHECK(!Has(hook, "MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_SIGNIN)"));
  CHECK(Has(hook, "ctx.r3.u64 = 0;"));
  CHECK(!Has(source, "REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_SIGNIN))"));
  for (const char* direct : {"ExitProcess", "TerminateProcess", "_Exit", "std::exit", "quick_exit", "abort(", "FlushLogging",
                             "DiscardSessionLogBeforeHardExit", "relaunch", "Relaunch"}) {
    CHECK(!Has(source, direct));
  }
  const std::string header = ReadSource(kSrcDir / "main_menu_mua2.h");
  for (const char* direct : {"ExitProcess", "TerminateProcess", "_Exit", "std::exit"}) {
    CHECK(!Has(header, direct));
  }
  CHECK(!Has(source, "DEBUG_SAULO"));
  CHECK(!Has(header, "DEBUG_SAULO"));
}

void TestOnlyMua2CarriesTheHooks() {
  std::printf("mua2 only\n");
  const std::string source = ReadSource(kSrcDir / "main_menu_mua2.cpp");
  CHECK(Has(source, "#if defined(MUA_RUNTIME_MUA2)"));
  CHECK(Has(source, "REX_HOOK_RAW(MUA_GUEST_TEXT_INFLATE)"));
  CHECK(Has(source, "MUA_GUEST_ORIGINAL(MUA_GUEST_TEXT_INFLATE)(ctx, base);"));

  const std::string mua2 = ReadSource(kSrcDir / "guest_map_mua2.h");
  CHECK(Has(mua2, "#define MUA_GUEST_TEXT_INFLATE sub_8239C3C8"));
  CHECK(Has(mua2, "#define MUA_GUEST_MENU_SIGNIN sub_828FCA38"));
  for (const char* map : {"guest_map_mua1.h", "guest_map_mua1_gold.h"}) {
    const std::string text = ReadSource(kSrcDir / map);
    CHECK(!Has(text, "MUA_GUEST_TEXT_INFLATE"));
    CHECK(!Has(text, "MUA_GUEST_MENU_SIGNIN"));
  }

  const std::string cmake = ReadSource(kRoot / "CMakeLists.txt");
  CHECK(Has(cmake, "src/main_menu_mua2.cpp"));
  CHECK(Has(cmake, "src/main_menu.cpp"));
}

void TestMua1AndGoldAreUntouched() {
  std::printf("mua1 and gold menu code\n");
  const std::string source = ReadSource(kSrcDir / "main_menu.cpp");
  CHECK(Has(source, "#if !defined(MUA_RUNTIME_MUA2)"));
  CHECK(Has(source, "REX_HOOK_RAW(MUA_GUEST_MENU_ITEM_CREATE)"));
  CHECK(Has(source, "REX_HOOK_RAW(MUA_GUEST_ITEM_USE)"));
  CHECK(Has(source, "__imp__XamLoaderTerminateTitle(ctx, base);"));
  CHECK(!Has(source, "TEXT_INFLATE"));
  CHECK(!Has(source, "MENU_SIGNIN"));
  const std::string items = ReadSource(kSrcDir / "main_menu_items.h");
  CHECK(Has(items, "kExitText = \"EXIT GAME\""));
  CHECK(!Has(items, "Quit Game"));
  CHECK(!Has(items, "Xbox LIVE"));
}

void TestSkipIntroAndShutdownAreUntouched() {
  std::printf("skip intro and shutdown\n");
  for (const char* file : {"skip_intro.cpp", "skip_intro.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    CHECK(!text.empty());
    CHECK(!Has(text, "main_menu_mua2"));
    CHECK(!Has(text, "TEXT_INFLATE"));
    CHECK(!Has(text, "MENU_SIGNIN"));
  }
  const std::string overrides = ReadSource(kRoot / "config" / "mua2" / "overrides.toml");
  CHECK(!overrides.empty());
  CHECK(!Has(overrides, "main_menu"));
  CHECK(!Has(overrides, "8239C3C8"));
  CHECK(!Has(overrides, "828FCA38"));

  const std::string patch = ReadSource(kRoot / "patches" / "rexglue-sdk" / "0129-self-suspend-guest-wait.patch");
  CHECK(!patch.empty());
  CHECK(!Has(patch, "main_menu"));
  CHECK(!Has(patch, "Quit Game"));
  const std::string app = ReadSource(kSrcDir / "marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  CHECK(!Has(app, "main_menu_mua2"));
}

#ifdef MUA_GENERATED_ROOT

std::string FunctionBody(const std::filesystem::path& directory, const std::string& name) {
  const std::string marker = "DEFINE_REX_FUNC(" + name + ") {";
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    const std::string file = entry.path().filename().string();
    if (!file.starts_with("marvel_ultimate_alliance_recomp.") || entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadSource(entry.path());
    const size_t start = text.find(marker);
    if (start != std::string::npos) {
      const size_t stop = text.find("\n}\n", start);
      return text.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
    }
  }
  return {};
}

void TestGeneratedStructure() {
  std::printf("map entries match the generated mua2 module\n");
  const std::filesystem::path directory = std::filesystem::path(MUA_GENERATED_ROOT) / "mua2";
  if (!std::filesystem::exists(directory / "marvel_ultimate_alliance_register.cpp")) {
    std::printf("  mua2: module not generated, skipped\n");
    return;
  }

  const std::string inflate = FunctionBody(directory, "sub_8239C3C8");
  CHECK(!inflate.empty());
  CHECK(Has(inflate, "// lwz r29,28(r3)"));
  CHECK(Has(inflate, "// lwz r11,12(r3)"));
  CHECK(Has(inflate, "// lwz r11,16(r15)"));
  CHECK(Has(inflate, "// lwz r9,12(r15)"));

  const std::string signin = FunctionBody(directory, "sub_828FCA38");
  CHECK(!signin.empty());
  CHECK(Has(signin, "bl 0x829ac818"));
  CHECK(Has(signin, "bl 0x82a94cd8"));

  // The Lua binding table pairs the function with the name string at 0x820C5D10 ("processSignin").
  const std::string bindings = FunctionBody(directory, "sub_8291C0B8");
  CHECK(!bindings.empty());
  const std::regex pair(R"(// addi r4,r\d+,-13768[\s\S]{0,400}?// addi r5,r\d+,23824)");
  CHECK(std::regex_search(bindings, pair));
}

#endif

}  // namespace

int main() {
  TestLabelBecomesQuitGame();
  TestStateOrderIsKept();
  TestSubtitleLeavesNoOnlineWording();
  TestStringsSplitAcrossInflateCalls();
  TestOtherDataIsLeftAlone();
  TestEachPatchKeepsItsLength();
  TestGuestMemoryIsBigEndian();
  TestQuitUsesTheTitleExitPath();
  TestOnlyMua2CarriesTheHooks();
  TestMua1AndGoldAreUntouched();
  TestSkipIntroAndShutdownAreUntouched();
#ifdef MUA_GENERATED_ROOT
  TestGeneratedStructure();
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
