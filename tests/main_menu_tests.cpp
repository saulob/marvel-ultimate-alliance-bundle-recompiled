// Main menu on MUA1 and Gold: the Xbox Live item is replaced while the menu parses its XML, so the menu keeps exactly
// Play / Options / Review / Exit Game in the game's own four slots, navigation wraps over those four, each action
// stays with its item, Exit Game ends the title through XamLoaderTerminateTitle, and nothing outside MUA1/Gold (MUA2,
// Skip Intro) is touched.
// Run: mua_main_menu_tests

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

#include "main_menu_items.h"

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

namespace menu = mua::main_menu;

const std::filesystem::path kSrcDir = MUA_SRC_DIR;

std::string ReadSource(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

using Attributes = std::vector<std::pair<std::string, std::string>>;

// Guest memory holding the main menu's XML as the game's node layout lays it out: a node is name, next sibling,
// first child and attribute count followed by (name, value) pointer pairs; equal strings share one pointer.
class GuestMenu {
 public:
  static constexpr uint32_t kMenuObject = 0x40;
  static constexpr uint32_t kVtable = 0x8203F634;
  static constexpr uint32_t kLiveEntry = 0x44;
  static constexpr uint32_t kExitStrings = 0x80;

  GuestMenu() : memory_(0x20000, 0) {
    Store(kMenuObject, kVtable);
    empty_ = Intern("");
    Store(kLiveEntry, Intern("option02_text"));
    menu::WriteExitStrings(base(), kExitStrings);
    next_ = 0x8000;
  }

  uint8_t* base() { return memory_.data(); }
  const uint8_t* base() const { return memory_.data(); }
  menu::GuestAddresses guest() const { return {kVtable, kLiveEntry, empty_, kExitStrings}; }
  uint32_t first() const { return nodes_.front(); }
  const std::vector<uint32_t>& nodes() const { return nodes_; }
  void Store(uint32_t address, uint32_t value) { menu::StoreU32(base(), address, value); }

  uint32_t Intern(const std::string& text) {
    const auto found = strings_.find(text);
    if (found != strings_.end()) {
      return found->second;
    }
    const uint32_t address = string_next_;
    std::memcpy(base() + address, text.c_str(), text.size() + 1);
    string_next_ += uint32_t(text.size()) + 1;
    strings_[text] = address;
    return address;
  }

  uint32_t AddNode(const std::string& name, const Attributes& attributes) {
    const uint32_t node = next_;
    next_ += menu::kNodeAttributesOffset + uint32_t(attributes.size()) * menu::kAttributeSize;
    Store(node, Intern(name));
    Store(node + menu::kNodeNextOffset, 0);
    Store(node + 8, 0xFFFFFFFF);
    Store(node + menu::kNodeAttributeCountOffset, uint32_t(attributes.size()));
    uint32_t slot = node + menu::kNodeAttributesOffset;
    for (const auto& [key, value] : attributes) {
      Store(slot, Intern(key));
      Store(slot + 4, Intern(value));
      slot += menu::kAttributeSize;
    }
    if (!nodes_.empty()) {
      Store(nodes_.back() + menu::kNodeNextOffset, node);
    }
    nodes_.push_back(node);
    return node;
  }

  uint32_t Find(const std::string& name) const { return menu::FindItem(base(), first(), name); }
  std::string Value(const std::string& node_name, const std::string& attribute) const {
    return std::string(menu::AttributeValue(base(), Find(node_name), attribute));
  }

 private:
  std::vector<uint8_t> memory_;
  std::map<std::string, uint32_t> strings_;
  std::vector<uint32_t> nodes_;
  uint32_t string_next_ = 0x100;
  uint32_t next_ = 0;
  uint32_t empty_ = 0;
};

Attributes TextItem(const std::string& name, const std::string& text, const std::string& command,
                    const std::string& up, const std::string& down, const std::string& mode, int slot) {
  return {{"animtext_scene", "AT_option" + std::to_string(slot)},
          {"down", down},
          {"left", " "},
          {"mode", mode},
          {"name", name},
          {"right", "debug_tab_label"},
          {"style", "BIG_MAINMENU"},
          {"text", text},
          {"textalignx", "TEXT_ALIGN_LEFT"},
          {"textaligny", "TEXT_ALIGN_CENTER_Y"},
          {"up", up},
          {"usecmd", command}};
}

Attributes FocusModel(const std::string& name, const std::string& mode) {
  return {{"animate", "true"}, {"mode", mode}, {"model", "ui/models/m_invis"}, {"name", name},
          {"type", "MENU_ITEM_MODEL"}};
}

// The item nodes of ui/menus/main.xmlb in file order (identical in MUA1 and Gold).
GuestMenu BuildMainMenuData() {
  GuestMenu data;
  data.AddNode("item", {{"name", "help"}, {"style", "MED_WHITE"}});
  data.AddNode("item", {{"name", "left_frame"}, {"neverfocus", "true"}, {"type", "MENU_ITEM_MODEL"}});
  data.AddNode("item", FocusModel("option01_focus", "main play diff"));
  data.AddNode("item", FocusModel("option02_focus", "main play diff"));
  data.AddNode("item", FocusModel("option03_focus", "main diff"));
  data.AddNode("item", FocusModel("option04_focus", "main "));
  data.AddNode("item", TextItem("option01_text", "PLAY", "menusetmode play", "debug_tab_label", "option02_text",
                                "default main", 1));
  data.AddNode("item", TextItem("option02_text", "PLAY ONLINE", " ", "option01_text", "option03_text",
                                "default main", 2));
  data.AddNode("item", TextItem("option03_text", "OPTIONS", "options_main", "option02_text", "option04_text",
                                "default main", 3));
  data.AddNode("item", TextItem("option04_text", "REVIEW", "set reviewmode -1;openmenu review", "option03_text",
                                "debug_tab_label", "default main", 4));
  data.AddNode("item", TextItem("option05_text", "BEGIN STORY", "menusetmode diff", "option07_text",
                                "option06_text", "play", 1));
  data.AddNode("item", TextItem("option06_text", "LOAD GAME", "runscript saveloadProcess(3)", "option05_text",
                                "option07_text", "play", 2));
  data.AddNode("item", TextItem("option10_text", "Easy", "newgame 0", "option12_text", "option11_text", "diff", 1));
  Attributes debug = TextItem("debug_tab_label", "debug", "openmenu debug", "option04_text", "option01_text",
                              "default main", 1);
  debug.emplace_back("debug", "true");
  data.AddNode("item", debug);
  return data;
}

struct Item {
  std::string name;
  std::string text;
  std::string command;
  std::string mode;
  bool hidden = false;
};

// What the game creates while it walks the item nodes: each node reaches the create function once, in order.
std::vector<Item> ParseItems(GuestMenu& data) {
  std::vector<Item> created;
  const menu::GuestAddresses guest = data.guest();
  for (uint32_t node = data.first(); node != 0; node = menu::LoadU32(data.base(), node + menu::kNodeNextOffset)) {
    menu::ReplaceLiveItem(data.base(), node, guest);
    Item item;
    item.name = std::string(menu::AttributeValue(data.base(), node, "name"));
    item.text = std::string(menu::AttributeValue(data.base(), node, "text"));
    item.command = std::string(menu::AttributeValue(data.base(), node, "usecmd"));
    item.mode = std::string(menu::AttributeValue(data.base(), node, "mode"));
    item.hidden = menu::AttributeSlot(data.base(), node, "debug") != 0;
    created.push_back(item);
  }
  return created;
}

std::vector<Item> MainTextItems(const std::vector<Item>& items) {
  std::vector<Item> main;
  for (const Item& item : items) {
    const bool main_mode = item.mode.find("main") != std::string::npos && !item.text.empty() && !item.hidden;
    if (main_mode && item.name.ends_with("_text")) {
      main.push_back(item);
    }
  }
  return main;
}

// The game's neighbor walk (sub_823DCBB0): follow the item's link; a hidden target hands over to its
// own link in the same direction; a missing target ends the walk.
std::string Neighbor(GuestMenu& data, const std::string& name, const std::string& direction) {
  std::string current = name;
  for (int steps = 0; steps < 32; ++steps) {
    const std::string target = data.Value(current, direction);
    const uint32_t node = data.Find(target);
    if (node == 0) {
      return {};
    }
    if (menu::AttributeSlot(data.base(), node, "debug") == 0) {
      return target;
    }
    current = target;
  }
  return {};
}

std::string Text(GuestMenu& data, const std::string& name) { return data.Value(name, "text"); }

void TestOriginalMenuHasFourEntries() {
  std::printf("original main menu data\n");
  GuestMenu data = BuildMainMenuData();
  const menu::GuestAddresses guest = data.guest();
  CHECK(menu::GuestString(data.base(), menu::ExitText(guest)) == "EXIT GAME");
  std::vector<std::string> texts;
  for (const char* name : {"option01_text", "option02_text", "option03_text", "option04_text"}) {
    texts.push_back(Text(data, name));
  }
  CHECK((texts == std::vector<std::string>{"PLAY", "PLAY ONLINE", "OPTIONS", "REVIEW"}));
}

void TestEntriesAfterReplacement() {
  std::printf("xbox live replaced, four entries remain\n");
  GuestMenu data = BuildMainMenuData();
  const std::vector<Item> items = ParseItems(data);
  const std::vector<Item> main = MainTextItems(items);

  CHECK(main.size() == 4);
  if (main.size() == 4) {
    CHECK(main[0].name == "option01_text" && main[0].text == "PLAY" && main[0].command == "menusetmode play");
    CHECK(main[1].name == "option02_text" && main[1].text == "OPTIONS" && main[1].command == "options_main");
    CHECK(main[2].name == "option03_text" && main[2].text == "REVIEW" &&
          main[2].command == "set reviewmode -1;openmenu review");
    CHECK(main[3].name == "option04_text" && main[3].text == "EXIT GAME" && main[3].command == "mua_exit_game");
  }
  for (const Item& item : items) {
    CHECK(item.text != "PLAY ONLINE");
    CHECK(item.command.find("live") == std::string::npos);
  }
}

void TestFourNativeSlots() {
  std::printf("native slots and focus models\n");
  GuestMenu data = BuildMainMenuData();
  const std::vector<Item> items = ParseItems(data);
  std::vector<std::string> names;
  for (const Item& item : items) {
    names.push_back(item.name);
  }
  const auto count = [&](const char* name) { return std::count(names.begin(), names.end(), std::string(name)); };
  for (const char* name : {"option01_focus", "option02_focus", "option03_focus", "option04_focus", "option01_text",
                           "option02_text", "option03_text", "option04_text", "option05_text"}) {
    CHECK(count(name) == 1);
  }
  CHECK(MainTextItems(items).size() == 4);
  for (int slot = 1; slot <= 4; ++slot) {
    const std::string name = "option0" + std::to_string(slot) + "_text";
    CHECK(data.Value(name, "animtext_scene") == "AT_option" + std::to_string(slot));
    CHECK(data.Value(name, "style") == "BIG_MAINMENU");
    CHECK(data.Value(name, "mode") == "default main");
  }
  CHECK(data.Value("option04_focus", "mode") == "main ");
  CHECK(data.Value("option04_focus", "model") == "ui/models/m_invis");
  for (const Item& item : items) {
    if (item.name.ends_with("_text") && item.mode.find("main") != std::string::npos) {
      CHECK(!item.text.empty());
    }
  }
}

void TestNoXboxLiveAction() {
  std::printf("no xbox live action\n");
  GuestMenu data = BuildMainMenuData();
  const std::vector<Item> items = ParseItems(data);
  const menu::GuestAddresses guest = data.guest();
  for (const Item& item : items) {
    CHECK(item.text.find("ONLINE") == std::string::npos);
    CHECK(item.text.find("LIVE") == std::string::npos);
  }
  CHECK(menu::GuestString(data.base(), menu::LoadU32(data.base(), guest.live_name_entry)).empty());
  for (const Item& item : MainTextItems(items)) {
    CHECK(!item.command.empty() && !menu::IsBlank(item.command));
  }
}

void TestNavigationWrapsOverFourEntries() {
  std::printf("navigation over the four entries\n");
  GuestMenu data = BuildMainMenuData();
  ParseItems(data);

  CHECK(Text(data, Neighbor(data, "option01_text", "down")) == "OPTIONS");
  CHECK(Text(data, Neighbor(data, "option02_text", "down")) == "REVIEW");
  CHECK(Text(data, Neighbor(data, "option03_text", "down")) == "EXIT GAME");
  CHECK(Text(data, Neighbor(data, "option04_text", "down")) == "PLAY");
  CHECK(Neighbor(data, "option04_text", "down") == "option01_text");

  CHECK(Text(data, Neighbor(data, "option04_text", "up")) == "REVIEW");
  CHECK(Text(data, Neighbor(data, "option03_text", "up")) == "OPTIONS");
  CHECK(Text(data, Neighbor(data, "option02_text", "up")) == "PLAY");
  CHECK(Text(data, Neighbor(data, "option01_text", "up")) == "EXIT GAME");
  CHECK(Neighbor(data, "option01_text", "up") == "option04_text");

  const std::vector<std::string> slots = {"option01_text", "option02_text", "option03_text", "option04_text"};
  for (const std::string& start : slots) {
    std::string name = start;
    for (int i = 0; i < 4; ++i) {
      name = Neighbor(data, name, "down");
      CHECK(std::count(slots.begin(), slots.end(), name) == 1);
    }
    CHECK(name == start);
    for (int i = 0; i < 4; ++i) {
      name = Neighbor(data, name, "up");
      CHECK(std::count(slots.begin(), slots.end(), name) == 1);
    }
    CHECK(name == start);
  }
}

void TestRebuildsKeepTheSameMenu() {
  std::printf("rebuilding an already rewritten menu\n");
  GuestMenu data = BuildMainMenuData();
  const std::vector<Item> first = ParseItems(data);
  const std::vector<uint8_t> after_first(data.base(), data.base() + 0x20000);
  const std::vector<Item> second = ParseItems(data);
  CHECK(std::memcmp(after_first.data(), data.base(), after_first.size()) == 0);
  CHECK(first.size() == second.size());
  const std::vector<Item> main = MainTextItems(second);
  CHECK(main.size() == 4);
  CHECK(Text(data, Neighbor(data, "option01_text", "down")) == "OPTIONS");
  CHECK(Neighbor(data, "option04_text", "down") == "option01_text");
}

void TestOtherModesAreUntouched() {
  std::printf("play and difficulty entries keep their data\n");
  GuestMenu before = BuildMainMenuData();
  GuestMenu after = BuildMainMenuData();
  ParseItems(after);
  for (const char* name : {"option01_text", "option05_text", "option06_text", "option10_text", "help", "left_frame",
                           "option01_focus", "option02_focus", "option03_focus", "option04_focus",
                           "debug_tab_label"}) {
    for (const char* attribute : {"name", "text", "usecmd", "up", "down", "mode", "animtext_scene"}) {
      CHECK(before.Value(name, attribute) == after.Value(name, attribute));
    }
  }
  CHECK(after.Value("option01_text", "up") == "debug_tab_label");
  CHECK(after.Value("option02_text", "up") == "option01_text");
  CHECK(after.Value("option03_text", "up") == "option02_text");
  CHECK(after.Value("option03_text", "down") == "option04_text");
  CHECK(after.Value("option04_text", "up") == "option03_text");
  CHECK(after.Value("option04_text", "down") == "debug_tab_label");
  CHECK(after.Value("debug_tab_label", "up") == "option04_text");
  CHECK(after.Value("debug_tab_label", "down") == "option01_text");
}

void TestLiveHandlersHaveNothingToMatch() {
  std::printf("xbox live handlers detached\n");
  GuestMenu data = BuildMainMenuData();
  const menu::GuestAddresses guest = data.guest();
  CHECK(menu::GuestString(data.base(), menu::LoadU32(data.base(), guest.live_name_entry)) == "option02_text");
  ParseItems(data);
  const std::string_view entry = menu::GuestString(data.base(), menu::LoadU32(data.base(), guest.live_name_entry));
  CHECK(entry.empty());
  for (const char* item : {"option01_text", "option02_text", "option03_text", "option04_text"}) {
    CHECK(data.Value(item, "name") != std::string(entry));
  }
}

void TestUnexpectedDataIsLeftAlone() {
  std::printf("unexpected data leaves the menu as the game built it\n");
  {
    GuestMenu data;
    data.AddNode("item", {{"name", "help"}});
    data.AddNode("item", TextItem("option02_text", "PLAY ONLINE", " ", "option01_text", "option03_text", "main", 2));
    data.AddNode("item", TextItem("option03_text", "OPTIONS", "options_main", "option02_text", "option04_text", "main", 3));
    const std::vector<uint8_t> before(data.base(), data.base() + 0x20000);
    const std::vector<Item> items = ParseItems(data);
    CHECK(std::memcmp(before.data(), data.base(), before.size()) == 0);
    CHECK(items.size() == 3);
  }
  {
    GuestMenu data = BuildMainMenuData();
    const uint32_t command = menu::AttributeSlot(data.base(), data.nodes()[7], "usecmd");
    data.Store(command, data.Intern("openmenu online"));
    const std::vector<uint8_t> before(data.base(), data.base() + 0x20000);
    const std::vector<Item> items = ParseItems(data);
    CHECK(std::memcmp(before.data(), data.base(), before.size()) == 0);
    CHECK(MainTextItems(items).size() == 4);
  }
  {
    GuestMenu data = BuildMainMenuData();
    data.Store(GuestMenu::kLiveEntry, data.Intern("option09_text"));
    const std::vector<uint8_t> before(data.base(), data.base() + 0x20000);
    const std::vector<Item> items = ParseItems(data);
    CHECK(std::memcmp(before.data(), data.base(), before.size()) == 0);
    CHECK(MainTextItems(items).size() == 4);
  }
  {
    GuestMenu data = BuildMainMenuData();
    menu::GuestAddresses guest = data.guest();
    guest.exit_strings = 0;
    for (uint32_t node : data.nodes()) {
      menu::ReplaceLiveItem(data.base(), node, guest);
    }
    CHECK(data.Value("option02_text", "text") == "PLAY ONLINE");
    CHECK(data.Value("option04_text", "text") == "REVIEW");
  }
}

void TestExitCommandIdentity() {
  std::printf("exit game command\n");
  GuestMenu data = BuildMainMenuData();
  const menu::GuestAddresses guest = data.guest();
  CHECK(menu::IsExitCommand(data.base(), menu::ExitCommand(guest)));
  CHECK(!menu::IsExitCommand(data.base(), 0));
  CHECK(!menu::IsExitCommand(data.base(), menu::ExitText(guest)));
  for (const char* command : {"menusetmode play", "options_main", "set reviewmode -1;openmenu review", " ", "",
                              "mainmenuexit 1", "quit"}) {
    CHECK(!menu::IsExitCommand(data.base(), data.Intern(command)));
  }
  ParseItems(data);
  CHECK(menu::IsExitCommand(data.base(), menu::LoadU32(data.base(), menu::AttributeSlot(
                                             data.base(), data.Find("option04_text"), "usecmd"))));
  for (const char* item : {"option01_text", "option02_text", "option03_text"}) {
    const uint32_t slot = menu::AttributeSlot(data.base(), data.Find(item), "usecmd");
    CHECK(!menu::IsExitCommand(data.base(), menu::LoadU32(data.base(), slot)));
  }
}

void TestExitUsesTheTitleExitPath() {
  std::printf("exit game uses the title exit path\n");
  const std::string source = ReadSource(kSrcDir / "main_menu.cpp");
  const size_t use = source.find("REX_HOOK_RAW(MUA_GUEST_ITEM_USE)");
  CHECK(use != std::string::npos);
  const std::string hook = use == std::string::npos ? std::string() : source.substr(use);
  CHECK(Has(source, "REX_EXTERN(__imp__XamLoaderTerminateTitle);"));
  CHECK(Has(hook, "IsExitItem(ctx, base, ctx.r3.u32)"));
  CHECK(Has(hook, "__imp__XamLoaderTerminateTitle(ctx, base);"));
  CHECK(Has(hook, "MUA_GUEST_ORIGINAL(MUA_GUEST_ITEM_USE)(ctx, base);"));
  CHECK(Has(source, "MUA_GUEST_STRING_DATA(ctx, base);"));
  for (const char* direct : {"ExitProcess", "TerminateProcess", "_Exit", "std::exit", "quick_exit", "abort(", "FlushLogging"}) {
    CHECK(!Has(source, direct));
  }
  const std::string header = ReadSource(kSrcDir / "main_menu_items.h");
  for (const char* direct : {"ExitProcess", "TerminateProcess", "_Exit", "std::exit"}) {
    CHECK(!Has(header, direct));
  }
}

void TestShutdownStaysWithTheHost() {
  std::printf("host shutdown is unchanged\n");
  const std::string app = ReadSource(kSrcDir / "marvel_ultimate_alliance_app.h");
  CHECK(Has(app, "void OnShutdown() override"));
  CHECK(Has(app, "mua::FormatCleanExit(mua::ProcessUptimeText())"));
  CHECK(Has(app, "void OnClosing(rex::ui::UIEvent&) override"));
  CHECK(Has(app, "mua::DiscardSessionLogBeforeHardExit();"));
  CHECK(Has(app, "Window closing, hard-exiting process."));
  CHECK(!Has(app, "main_menu"));
  CHECK(!Has(app, "XamLoader"));
}

void TestOnlyTheMainMenuIsRewritten() {
  std::printf("other menus are not touched\n");
  GuestMenu data = BuildMainMenuData();
  CHECK(menu::IsMenu(data.base(), GuestMenu::kMenuObject, GuestMenu::kVtable));
  CHECK(!menu::IsMenu(data.base(), GuestMenu::kMenuObject, GuestMenu::kVtable + 4));
  CHECK(!menu::IsMenu(data.base(), 0, GuestMenu::kVtable));
}

void TestHookSource() {
  std::printf("hook source\n");
  const std::string source = ReadSource(kSrcDir / "main_menu.cpp");
  CHECK(Has(source, "#if !defined(MUA_RUNTIME_MUA2)"));
  CHECK(Has(source, "REX_HOOK_RAW(MUA_GUEST_MENU_ITEM_CREATE)"));
  CHECK(Has(source, "MUA_GUEST_ORIGINAL(MUA_GUEST_MENU_ITEM_CREATE)(ctx, base);"));
  CHECK(Has(source, "mua::guest::kMainMenuVtable"));
  CHECK(Has(source, "mua::main_menu::ReplaceLiveItem(base, node, Guest(base));"));
  CHECK(!Has(source, "ShouldSkipItem"));
  CHECK(!Has(source, "ctx.r3.u64 = 0;"));
  CHECK(!Has(source, "DEBUG_SAULO"));
  CHECK(!Has(source, "visual_trace"));

  const std::string header = ReadSource(kSrcDir / "main_menu_items.h");
  CHECK(!Has(header, "DEBUG_SAULO"));

  const std::string cmake = ReadSource(kSrcDir / ".." / "CMakeLists.txt");
  CHECK(Has(cmake, "src/main_menu.cpp"));
}

void TestGuestMaps() {
  std::printf("mua1 and gold guest maps\n");
  const std::string mua1 = ReadSource(kSrcDir / "guest_map_mua1.h");
  CHECK(Has(mua1, "#define MUA_GUEST_MENU_ITEM_CREATE sub_823CF578"));
  CHECK(Has(mua1, "#define MUA_GUEST_ITEM_USE sub_823DC040"));
  CHECK(Has(mua1, "#define MUA_GUEST_STRING_DATA sub_82390570"));
  CHECK(Has(mua1, "kMainMenuVtable = 0x8203F634;"));
  CHECK(Has(mua1, "kMainMenuLiveNameEntry = 0x8288D5F8;"));
  CHECK(Has(mua1, "kEmptyString = 0x820034A0;"));

  const std::string gold = ReadSource(kSrcDir / "guest_map_mua1_gold.h");
  CHECK(Has(gold, "#define MUA_GUEST_MENU_ITEM_CREATE sub_823E5290"));
  CHECK(Has(gold, "#define MUA_GUEST_ITEM_USE sub_823F1F78"));
  CHECK(Has(gold, "#define MUA_GUEST_STRING_DATA sub_823A5948"));
  CHECK(Has(gold, "kMainMenuVtable = 0x8203E4CC;"));
  CHECK(Has(gold, "kMainMenuLiveNameEntry = 0x828BE2E0;"));
  CHECK(Has(gold, "kEmptyString = 0x820034D8;"));

  const std::string mua2 = ReadSource(kSrcDir / "guest_map_mua2.h");
  for (const char* name : {"MUA_GUEST_MENU_ITEM_CREATE", "MUA_GUEST_ITEM_USE", "MUA_GUEST_STRING_DATA", "kMainMenuVtable",
                           "kMainMenuLiveNameEntry", "kEmptyString"}) {
    CHECK(!Has(mua2, name));
  }
}

void TestSkipIntroIsNotInvolved() {
  std::printf("skip intro and mua2 are not involved\n");
  for (const char* file : {"skip_intro.cpp", "skip_intro.h", "guest_map_mua2.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    CHECK(!text.empty());
    CHECK(!Has(text, "main_menu"));
    CHECK(!Has(text, "MENU_ITEM_CREATE"));
  }
  const std::string mua2_overrides = ReadSource(kSrcDir / ".." / "config" / "mua2" / "overrides.toml");
  CHECK(!mua2_overrides.empty());
  CHECK(!Has(mua2_overrides, "main_menu"));
}

#ifdef MUA_GENERATED_ROOT

struct GeneratedGame {
  const char* key;
  const char* directory;
  const char* ctor;
  const char* vtable_displacement;
  const char* create;
  const char* activation;
  const char* handler;
  const char* name_displacement;
  const char* item_use;
  const char* string_data;
};

const GeneratedGame kGames[] = {
    {"mua1", "mua1", "sub_823D5768", "-2508", "sub_823CF578", "sub_823F4278", "sub_823F4620", "-10760", "sub_823DC040", "sub_82390570"},
    {"gold", "mua1-gold", "sub_823EB538", "-6964", "sub_823E5290", "sub_8240A910", "sub_8240ACD8", "-7456", "sub_823F1F78", "sub_823A5948"},
};

std::string FunctionBody(const std::string& file_text, const std::string& name) {
  const std::string marker = "DEFINE_REX_FUNC(" + name + ") {";
  const size_t start = file_text.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const size_t stop = file_text.find("\n}\n", start);
  return file_text.substr(start, stop == std::string::npos ? std::string::npos : stop - start);
}

// Finds each function in the module's generated sources by scanning the files once.
std::map<std::string, std::string> GeneratedBodies(const std::filesystem::path& directory,
                                                   const std::vector<std::string>& names) {
  std::map<std::string, std::string> bodies;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    const std::string file = entry.path().filename().string();
    if (!file.starts_with("marvel_ultimate_alliance_recomp.") || entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadSource(entry.path());
    for (const std::string& name : names) {
      if (!bodies.contains(name)) {
        std::string body = FunctionBody(text, name);
        if (!body.empty()) {
          bodies[name] = std::move(body);
        }
      }
    }
  }
  return bodies;
}

void TestGeneratedStructure() {
  std::printf("map entries match the generated modules\n");
  const std::filesystem::path root = MUA_GENERATED_ROOT;
  for (const GeneratedGame& game : kGames) {
    const std::filesystem::path directory = root / game.directory;
    if (!std::filesystem::exists(directory / "marvel_ultimate_alliance_register.cpp")) {
      std::printf("  %s: module not generated, skipped\n", game.key);
      continue;
    }
    const auto bodies =
        GeneratedBodies(directory, {game.ctor, game.create, game.activation, game.handler, game.item_use, game.string_data});
    for (const char* name : {game.ctor, game.create, game.activation, game.handler, game.item_use, game.string_data}) {
      CHECK(bodies.contains(name));
    }
    if (bodies.size() != 6) {
      continue;
    }

    const std::string& ctor = bodies.at(game.ctor);
    CHECK(Has(ctor, "lis r11,-32252"));
    CHECK(Has(ctor, (std::string("addi r11,r11,") + game.vtable_displacement).c_str()));
    CHECK(0x82040000u + uint32_t(std::stoi(game.vtable_displacement)) ==
          (std::string(game.key) == "mua1" ? 0x8203F634u : 0x8203E4CCu));

    const std::string& create = bodies.at(game.create);
    CHECK(Has(create, "lwz r11,164(r11)"));
    CHECK(Has(create, "lwz r11,72(r11)"));
    CHECK(Has(create, "lwz r11,168(r11)"));

    const std::regex name_load(std::string(R"(// lwz r\d+,)") + game.name_displacement + R"(\(r\d+\))");
    CHECK(std::regex_search(bodies.at(game.activation), name_load));
    CHECK(std::regex_search(bodies.at(game.handler), name_load));
    CHECK(Has(bodies.at(game.activation), "li r4,1178"));

    const std::string& item_use = bodies.at(game.item_use);
    CHECK(std::regex_search(item_use, std::regex(R"(// addi r\d+,r3,36)")));
    CHECK(Has(item_use, "lwz r11,28("));
    CHECK(Has(item_use, "li r3,3"));
    CHECK(Has(bodies.at(game.string_data), "addi r3,r11,4104"));
  }
}

#endif

}  // namespace

int main() {
  TestOriginalMenuHasFourEntries();
  TestEntriesAfterReplacement();
  TestFourNativeSlots();
  TestNoXboxLiveAction();
  TestNavigationWrapsOverFourEntries();
  TestRebuildsKeepTheSameMenu();
  TestOtherModesAreUntouched();
  TestLiveHandlersHaveNothingToMatch();
  TestUnexpectedDataIsLeftAlone();
  TestExitCommandIdentity();
  TestExitUsesTheTitleExitPath();
  TestShutdownStaysWithTheHost();
  TestOnlyTheMainMenuIsRewritten();
  TestHookSource();
  TestGuestMaps();
  TestSkipIntroIsNotInvolved();
#ifdef MUA_GENERATED_ROOT
  TestGeneratedStructure();
#endif
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
