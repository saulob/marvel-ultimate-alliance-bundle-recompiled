// Player names: the one validator and its edit gate, the four shared launcher keys and their section, the SDK values
// they produce, that the display name never reaches the profile identity (XUID, sign-in, save and achievement
// folders), and the guest hooks of MUA1, Gold Edition and MUA 2 at their verified addresses.
// Run: mua_player_names_tests

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game_data.h"
#include "game_definition.h"
#include "player_names.h"

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

using rex::system::xam::AcceptUserDisplayNameEdit;
using rex::system::xam::NormalizeUserDisplayName;
using rex::system::xam::UserDisplayNameResult;

const fs::path kSources = MUA_SRC_DIR;
const fs::path kRoot = kSources.parent_path();

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string result = text.str();
  std::erase(result, '\r');
  return result;
}

std::string ReadSource(const char* name) {
  return ReadFile(kSources / name);
}

std::string ReadSdkPatch() {
  return ReadFile(kRoot / "patches" / "rexglue-sdk" / "0147-local-user-display-names.patch");
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

std::string Normalized(const std::string& raw) {
  std::string out = "<rejected>";
  NormalizeUserDisplayName(raw, &out);
  return out;
}

// What the name field holds after typing, pasting or deleting: the proposed text only when it is allowed.
std::string Edit(const std::string& previous, const std::string& proposed, UserDisplayNameResult* result = nullptr) {
  return AcceptUserDisplayNameEdit(previous, proposed, result);
}

const std::string kAccent = "\xC3\xA9";

std::string Repeat(const std::string& unit, int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    out += unit;
  }
  return out;
}

struct GameHooks {
  const char* key;
  const char* map;
  const char* player_number_name;
  const char* list_function;  // empty: the game has no Players list
  const char* list_site;
  const char* list_port_writer;
  const char* stats_function;  // empty: the game has no formatted Statistics title
  const char* stats_site;
};

const GameHooks kGames[] = {
    {"mua1", "guest_map_mua1.h", "821EA3C8", "823FA3E8", "823FA52C", "8233B000", "", ""},
    {"mua1-gold", "guest_map_mua1_gold.h", "821FB9F8", "82410C18", "82410D68", "8234F300", "", ""},
    {"mua2", "guest_map_mua2.h", "8260BA78", "", "", "", "82919468", "82919550"},
};

void TestValidator() {
  std::printf("validator: 15 characters and 15 bytes in the 16-byte XAM buffer\n");
  CHECK(rex::system::xam::kUserNameBufferSize == 16);
  CHECK(rex::system::xam::kUserDisplayNameMaxBytes == 15);
  CHECK(rex::system::xam::kUserDisplayNameMaxCharacters == 15);
  CHECK(Normalized("Saulo") == "Saulo");
  CHECK(Normalized("Jo\xC3\xA3o") == "Jo\xC3\xA3o");
  CHECK(Normalized("  Saulo  ") == "Saulo");
  CHECK(Normalized("") == "<rejected>");
  CHECK(Normalized("    ") == "<rejected>");
  CHECK(Normalized(std::string(15, 'a')) == std::string(15, 'a'));
  CHECK(Normalized(std::string(16, 'a')) == "<rejected>");
  CHECK(Normalized(" " + std::string(15, 'a') + " ") == std::string(15, 'a'));
  CHECK(Normalized(Repeat(kAccent, 7) + "a") == Repeat(kAccent, 7) + "a");
  CHECK(Normalized(Repeat(kAccent, 8)) == "<rejected>");
  CHECK(Normalized("a\xC3") == "<rejected>");
  CHECK(Normalized("\xFF") == "<rejected>");
  CHECK(Normalized("\xC0\x80") == "<rejected>");
  CHECK(Normalized("\xED\xA0\x80") == "<rejected>");
  CHECK(Normalized("\xF4\x90\x80\x80") == "<rejected>");
  CHECK(Normalized(std::string("a\0b", 3)) == "<rejected>");
  CHECK(Normalized("a\tb") == "<rejected>");
  CHECK(Normalized("a\nb") == "<rejected>");
  CHECK(Normalized("a\x7F") == "<rejected>");
  CHECK(Normalized("a\xC2\x85") == "<rejected>");
  CHECK(rex::system::xam::Utf8PrefixLength("ab\xC3\xA9", 3) == 2);
  CHECK(rex::system::xam::Utf8PrefixLength("ab\xC3\xA9", 4) == 4);
}

void TestMessages() {
  std::printf("friendly messages\n");
  CHECK(mua::CheckPlayerName(std::string(15, 'a')) == mua::PlayerNameResult::Ok);
  CHECK(mua::CheckPlayerName(std::string(16, 'a')) == mua::PlayerNameResult::TooManyCharacters);
  CHECK(std::string(mua::PlayerNameMessage(mua::PlayerNameResult::TooManyCharacters)) == "Maximum 15 characters.");
  CHECK(mua::CheckPlayerName(Repeat(kAccent, 8)) == mua::PlayerNameResult::TooManyBytes);
  CHECK(std::string(mua::PlayerNameMessage(mua::PlayerNameResult::TooManyBytes)) ==
        "Name is too long for the game (15-byte limit).");
  CHECK(mua::CheckPlayerName("a\tb") == mua::PlayerNameResult::BadCharacter);
  CHECK(mua::CheckPlayerName("\xFF") == mua::PlayerNameResult::BadCharacter);
  CHECK(mua::CheckPlayerName("   ") == mua::PlayerNameResult::Empty);
  CHECK(std::string(mua::PlayerNameMessage(mua::PlayerNameResult::Empty)) == "Enter a name.");
  CHECK(std::string(mua::PlayerNameMessage(mua::PlayerNameResult::BadCharacter)) ==
        "That name has a character that cannot be used.");
  CHECK(std::string(mua::PlayerNameMessage(mua::PlayerNameResult::Ok)).empty());
  const std::string hint = mua::kPlayerNameHint;
  CHECK(hint == "Up to 15 characters.");
  CHECK(!Has(hint, "byte") && !Has(hint, "accent"));
}

void TestEditGate() {
  std::printf("the field never holds an oversized or invalid value\n");
  const std::string fifteen = "123456789012345";
  UserDisplayNameResult result;
  CHECK(Edit("12345678901234", fifteen, &result) == fifteen && result == UserDisplayNameResult::Ok);
  CHECK(Edit(fifteen, fifteen + "6", &result) == fifteen && result == UserDisplayNameResult::TooManyCharacters);
  CHECK(Edit(fifteen, "1234567890X12345", &result) == fifteen);
  const std::string accents = Repeat(kAccent, 7) + "a";
  CHECK(Edit(accents, accents, &result) == accents);
  CHECK(Edit(accents, accents + "a", &result) == accents && result == UserDisplayNameResult::TooManyBytes);
  CHECK(Edit(accents, accents + kAccent, &result) == accents && result == UserDisplayNameResult::TooManyBytes);
  CHECK(Edit("abc", "abc" + std::string(40, 'x'), &result) == "abc");
  CHECK(Edit("abc", "abc" + Repeat(kAccent, 20), &result) == "abc");
  CHECK(Edit("abcdef", "abXYZef") == "abXYZef");
  CHECK(Edit(fifteen, fifteen.substr(0, 14)) == fifteen.substr(0, 14));
  CHECK(Edit(accents, accents.substr(0, accents.size() - 1)) == accents.substr(0, accents.size() - 1));
  CHECK(Edit("abc", "") == "");
  CHECK(Edit("abc", "ab\tc", &result) == "abc" && result == UserDisplayNameResult::BadCharacter);
  CHECK(Edit("abc", "ab\xFF", &result) == "abc" && result == UserDisplayNameResult::BadCharacter);
  CHECK(Edit("abc", "ab\xC3", &result) == "abc");
}

void TestKeysAndSdkValues() {
  std::printf("four shared keys, four SDK cvars, player N is local user N - 1\n");
  CHECK(mua::kPlayerNames.size() == 4);
  std::set<std::string> keys;
  std::set<std::string> sdk_keys;
  for (int i = 0; i < 4; ++i) {
    const mua::PlayerNameSetting& player = mua::kPlayerNames[i];
    CHECK(std::string(player.key) == "mua_player_" + std::to_string(i + 1) + "_name");
    CHECK(std::string(player.sdk_key) == "local_player_" + std::to_string(i + 1) + "_name");
    CHECK(std::string(player.label) == "Player " + std::to_string(i + 1));
    CHECK(std::string(player.default_name) == "Player " + std::to_string(i + 1));
    keys.insert(player.key);
    sdk_keys.insert(player.sdk_key);
  }
  CHECK(keys.size() == 4 && sdk_keys.size() == 4);

  const auto& p1 = mua::kPlayerNames[0];
  const auto& p2 = mua::kPlayerNames[1];
  const auto& p3 = mua::kPlayerNames[2];
  const auto& p4 = mua::kPlayerNames[3];
  CHECK(p1.send_default && !p2.send_default && !p3.send_default && !p4.send_default);
  CHECK(mua::SdkPlayerName(p1, "Saulo") == "Saulo");
  CHECK(mua::SdkPlayerName(p2, "Jo\xC3\xA3o") == "Jo\xC3\xA3o");
  CHECK(mua::SdkPlayerName(p3, "Peter") == "Peter");
  CHECK(mua::SdkPlayerName(p4, "Logan") == "Logan");
  CHECK(mua::SdkPlayerName(p1, "  Saulo ") == "Saulo");
  CHECK(mua::SdkPlayerName(p1, "Player 1") == "Player 1");
  CHECK(mua::SdkPlayerName(p1, "") == "Player 1");
  CHECK(mua::SdkPlayerName(p1, "\xFF") == "Player 1");
  CHECK(mua::SdkPlayerName(p1, std::string(16, 'a')) == "Player 1");
  CHECK(mua::SdkPlayerName(p1, Repeat(kAccent, 8)) == "Player 1");
  CHECK(mua::SdkPlayerName(p2, "Player 2").empty());
  CHECK(mua::SdkPlayerName(p3, "Player 3").empty());
  CHECK(mua::SdkPlayerName(p4, "Player 4").empty());
  CHECK(mua::SdkPlayerName(p2, "").empty());
  CHECK(mua::SdkPlayerName(p2, std::string(16, 'a')).empty());
  CHECK(mua::SdkPlayerName(p2, "a\tb").empty());
  CHECK(mua::SdkPlayerName(p1, std::string(15, 'a')) == std::string(15, 'a'));

  // Each player's value depends on that player's own launcher value only.
  const std::string launcher[4] = {"Saulo", "Jo\xC3\xA3o", "Peter", "Logan"};
  std::string sdk[4];
  for (int i = 0; i < 4; ++i) {
    sdk[i] = mua::SdkPlayerName(mua::kPlayerNames[i], launcher[i]);
  }
  CHECK(sdk[0] == "Saulo" && sdk[1] == "Jo\xC3\xA3o" && sdk[2] == "Peter" && sdk[3] == "Logan");
  CHECK(mua::SdkPlayerName(p2, "Tony") == "Tony");
  CHECK(mua::SdkPlayerName(p1, launcher[0]) == "Saulo" && mua::SdkPlayerName(p3, launcher[2]) == "Peter");
}

void TestSettingsAreShared() {
  std::printf("the names are shared settings in settings.toml, applied before the guest starts\n");
  const std::string settings = ReadSource("settings.cpp");
  for (int i = 1; i <= 4; ++i) {
    const std::string n = std::to_string(i);
    CHECK(Has(settings, "REXCVAR_DEFINE_STRING(mua_player_" + n + "_name, \"Player " + n + "\""));
  }
  CHECK(Has(settings, "for (const PlayerNameSetting& player : kPlayerNames) {\n    keys.emplace_back(player.key);"));
  const size_t scoped = settings.find("constexpr std::string_view kGameScopedKeys[]");
  CHECK(scoped != std::string::npos);
  const std::string scoped_list = settings.substr(scoped, settings.find(';', scoped) - scoped);
  CHECK(!Has(scoped_list, "player"));
  CHECK(Has(settings, "!IsValidPlayerName(name->second)"));
  CHECK(Has(settings, "name->second = player.default_name;"));
  CHECK(Has(settings, "rex::cvar::SetFlagByName(player.sdk_key, SdkPlayerName(player, rex::cvar::GetFlagByName(player.key)));"));
  CHECK(Has(settings, "  ApplyGameSettings();\n  ApplyPlayerNames();\n  ApplyShaderWait();\n  ApplyMultisampling();\n}"));
  CHECK(!Has(settings, "hcu_") && !Has(settings, "gw2_"));

  // No game carries a copy of its own: no per-game keys anywhere in the sources or the shipped config.
  const std::regex per_game(R"((mua1|gold|mua2|mua1_gold)_player_\d_name)");
  for (const auto& entry : fs::directory_iterator(kSources)) {
    CHECK(!std::regex_search(ReadFile(entry.path()), per_game));
  }
  CHECK(!std::regex_search(ReadFile(kRoot / "config" / "marvel_ultimate_alliance.toml"), per_game));
  CHECK(!Has(ReadFile(kRoot / "config" / "marvel_ultimate_alliance.toml"), "player_1_name"));

  // A game's own program loads settings.toml and applies the names before its guest starts.
  const std::string app = ReadSource("marvel_ultimate_alliance_app.h");
  const size_t load = app.find("mua::LoadPlayerSettings(mua::PlayerSettingsPath(user_data_root()));");
  const size_t game_branch = app.find("if (!IsLauncher()) {\n      if (mua::TakeLauncherHandoff()");
  const size_t apply = app.find("mua::ApplyPlayerNames();", game_branch);
  const size_t routed = app.find("const rex::PathConfig routed = RoutedPaths(defaults, selected);");
  CHECK(load != std::string::npos && game_branch != std::string::npos);
  CHECK(apply != std::string::npos && load < apply && game_branch < apply && apply < routed);
  CHECK(Count(app, "mua::ApplyPlayerNames();") == 1);
}

void TestLauncherSection() {
  std::printf("one Player names section on the Gameplay page, the reference row\n");
  const std::string launcher = ReadSource("launcher.cpp");
  CHECK(Count(launcher, "ui.Section(\"Player names\"") == 1);
  const size_t gameplay = launcher.find("void PageGameplay(trg::Ui& ui)");
  const size_t names = launcher.find("ui.Section(\"Player names\"");
  const size_t next_page = launcher.find("const MonitorInfo* SelectedMonitor()");
  CHECK(gameplay < names && names < next_page);
  CHECK(Has(launcher, "ui.Help(kPlayerNamesHelp);"));
  CHECK(Has(launcher, "for (const PlayerNameSetting& player : kPlayerNames) {\n        PlayerNameRow(ui, player);"));
  CHECK(Has(launcher, "ImGuiInputTextFlags_CallbackEdit"));
  CHECK(Has(launcher, "rex::system::xam::AcceptUserDisplayNameEdit("));
  CHECK(Has(launcher, "ImGui::InputTextWithHint(\"##text\", player.default_name, text, sizeof text,"));
  CHECK(Has(launcher, "ui.Row(player.label, kPlayerNameHint);"));
  CHECK(Has(launcher, "ui.Help(PlayerNameMessage(shown));"));
  CHECK(std::string(mua::kPlayerNamesHelp).find("all three games") != std::string::npos);
  CHECK(std::string(mua::kPlayerNamesHelp).find("Save data is not affected.") != std::string::npos);
  CHECK(!Has(launcher, "hcu_") && !Has(launcher, "gw2_"));
}

void TestIdentityUnchanged() {
  std::printf("renaming never reaches the XUID, the sign-in state or the content directory\n");
  const std::string patch = ReadSdkPatch();
  CHECK(!patch.empty());
  CHECK(Has(patch, "+  const std::string& profile_id() const { return name_; }"));
  CHECK(Has(patch, "-  auto user_name = rex::to_path(kernel_state_->user_profile()->name());"));
  CHECK(Has(patch, "+  auto user_name = rex::to_path(kernel_state_->user_profile()->profile_id());"));
  CHECK(!Has(patch, "xuid_ ="));
  CHECK(!Has(patch, "+  name_ ="));
  CHECK(Has(patch, "+  *name = name_;"));
  CHECK(Has(patch, "user_profile->display_name(user_index, &name)"));
  CHECK(!Has(patch, "+  info->xuid") && !Has(patch, "-  info->xuid"));
  CHECK(!Has(patch, "+  info->signin_state") && !Has(patch, "-  info->signin_state"));
  CHECK(!Has(patch, "XamUserGetSigninState") && !Has(patch, "XamUserGetXUID"));
  CHECK(Has(patch, "local_player_1_name") && Has(patch, "local_player_2_name") &&
        Has(patch, "local_player_3_name") && Has(patch, "local_player_4_name"));
  CHECK(!Has(patch, "mua_") && !Has(patch, "hcu_") && !Has(patch, "gw2_"));

  // The per-game save and achievement folders come from the game alone; no name takes part.
  const fs::path root = "user_data";
  CHECK(mua::GameUserDataRoot(mua::Game(mua::GameId::kMua1), root) == root / "mua1");
  CHECK(mua::GameUserDataRoot(mua::Game(mua::GameId::kMua1Gold), root) == root / "mua1_gold");
  CHECK(mua::GameUserDataRoot(mua::Game(mua::GameId::kMua2), root) == root / "mua2");
  CHECK(mua::AchievementStoreFile(mua::Game(mua::GameId::kMua1), root) == root / "mua1" / "achievements" / "415607DA.toml");
  CHECK(mua::AchievementStoreFile(mua::Game(mua::GameId::kMua1Gold), root) ==
        root / "mua1_gold" / "achievements" / "415607DA.toml");
  CHECK(mua::AchievementStoreFile(mua::Game(mua::GameId::kMua2), root) == root / "mua2" / "achievements" / "4156082F.toml");
  for (const char* file : {"game_definition.cpp", "game_data.cpp", "user_data.cpp", "game_profile.cpp"}) {
    const std::string source = ReadSource(file);
    CHECK(!Has(source, "player_names.h") && !Has(source, "_player_") && !Has(source, "local_player"));
  }
  const std::string hooks = ReadSource("player_name_hooks.cpp");
  CHECK(!Has(hooks, "xuid") && !Has(hooks, "Xuid") && !Has(hooks, "XUID") && !Has(hooks, "Signin"));
  CHECK(!Has(hooks, "user_data") && !Has(hooks, "profile_id"));
}

void TestSharedValidator() {
  std::printf("the project validator is the SDK patch's header, byte for byte\n");
  const std::string patch = ReadSdkPatch();
  const std::string start = "+++ b/include/rex/system/xam/user_display_name.h\n";
  size_t at = patch.find(start);
  CHECK(at != std::string::npos);
  at = patch.find('\n', patch.find("@@", at)) + 1;
  const size_t end = patch.find("\ndiff --git", at);
  std::string added;
  std::istringstream lines(patch.substr(at, end - at));
  for (std::string line; std::getline(lines, line);) {
    if (!line.empty() && line[0] == '+') {
      added += line.substr(1) + "\n";
    }
  }
  CHECK(!added.empty() && added == ReadSource("user_display_name.h"));
}

void TestGuestNameBytes() {
  std::printf("the guest name buffers hold at most 15 bytes and a NUL, and only a custom name\n");
  char buffer[16];
  std::memset(buffer, 0x7F, sizeof buffer);
  CHECK(!mua::CachedPlayerNameBytes("", buffer));
  CHECK(buffer[0] == 0x7F);
  CHECK(mua::CachedPlayerNameBytes("Guest2", buffer));
  CHECK(std::string(buffer) == "Guest2");
  CHECK(buffer[6] == '\0' && buffer[15] == '\0');
  const std::string fifteen(15, 'a');
  CHECK(mua::CachedPlayerNameBytes(fifteen, buffer));
  CHECK(std::string(buffer, 15) == fifteen && buffer[15] == '\0');
  CHECK(mua::CachedPlayerNameBytes(std::string(40, 'b'), buffer));
  CHECK(std::strlen(buffer) == 15);
  const std::string accents = Repeat(kAccent, 7) + "a";
  CHECK(mua::CachedPlayerNameBytes(accents, buffer));
  CHECK(std::string(buffer) == accents);
  CHECK(mua::CachedPlayerNameBytes(Repeat(kAccent, 8), buffer));
  CHECK(std::string(buffer) == Repeat(kAccent, 7));

  // A name kept at its default leaves the game's own (localized) text.
  for (int i = 0; i < 4; ++i) {
    const mua::PlayerNameSetting& player = mua::kPlayerNames[i];
    std::memset(buffer, 0x7F, sizeof buffer);
    CHECK(!mua::CustomPlayerNameBytes(player, mua::SdkPlayerName(player, player.default_name), buffer));
    CHECK(!mua::CustomPlayerNameBytes(player, mua::SdkPlayerName(player, ""), buffer));
    CHECK(buffer[0] == 0x7F);
  }
  CHECK(mua::CustomPlayerNameBytes(mua::kPlayerNames[0], "Saulo", buffer) && std::string(buffer) == "Saulo");
  CHECK(mua::CustomPlayerNameBytes(mua::kPlayerNames[3], "Logan", buffer) && std::string(buffer) == "Logan");
  CHECK(!mua::CustomPlayerNameBytes(mua::kPlayerNames[1], "Player 2", buffer));
  CHECK(mua::CustomPlayerNameBytes(mua::kPlayerNames[1], "Player 1", buffer));

  CHECK(rex::system::xam::kUserNameBufferSize <= mua::kPlayerListRowTextSize);
  CHECK(mua::kPlayerListPortTextOffset + 10 <= mua::kPlayerListRowTextOffset);
}

void TestPlayerListRow() {
  std::printf("a Players list row belongs to the user of its controller's port digit\n");
  CHECK(mua::PlayerListRowUser("1") == 0);
  CHECK(mua::PlayerListRowUser("2") == 1);
  CHECK(mua::PlayerListRowUser("3") == 2);
  CHECK(mua::PlayerListRowUser("4") == 3);
  CHECK(mua::PlayerListRowUser("") == -1);
  CHECK(mua::PlayerListRowUser("0") == -1);
  CHECK(mua::PlayerListRowUser("5") == -1);
  CHECK(mua::PlayerListRowUser("12") == -1);
  CHECK(mua::PlayerListRowUser("1x") == -1);
}

void TestHooks() {
  std::printf("the hooks only replace the game's own fallback text\n");
  const std::string hooks = ReadSource("player_name_hooks.cpp");
  CHECK(Has(hooks, "REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_PLAYER_NUMBER_NAME));"));
  CHECK(Has(hooks, "REX_HOOK_RAW(MUA_GUEST_PLAYER_NUMBER_NAME) {"));
  CHECK(Has(hooks, "MUA_GUEST_ORIGINAL(MUA_GUEST_PLAYER_NUMBER_NAME)(ctx, base);"));
  CHECK(Has(hooks, "user < mua::kPlayerNames.size() && names.custom[user]"));
  CHECK(Has(hooks, "mua::CustomPlayerNameBytes(player, rex::cvar::GetFlagByName(player.sdk_key), bytes[i])"));
  CHECK(Has(hooks, "SystemHeapAlloc(sizeof(bytes))"));
  CHECK(Has(hooks, "#if !defined(MUA_RUNTIME_MUA2)\n"));
  CHECK(Has(hooks, "void mua_player_list_name(PPCRegister& stack) {"));
  CHECK(Has(hooks, "mua::PlayerListRowUser(port)"));
  CHECK(Has(hooks, "std::memcpy(base + stack.u32 + mua::kPlayerListRowTextOffset, base + name, kNameSize);"));
  CHECK(Has(hooks, "#else\n"));
  CHECK(Has(hooks, "void mua_player_stats_name(PPCRegister& frame, PPCRegister& user) {"));
  CHECK(Has(hooks, "std::memcpy(base + mua::LoadBE32(base, title + mua::kStatsTitleTextOffset), base + name, kNameSize);"));
  CHECK(Has(hooks, "mua::StoreBE32(base, title, uint32_t(strnlen("));
  CHECK(hooks.find("#if !defined(MUA_RUNTIME_MUA2)\n") < hooks.find("void mua_player_list_name(") &&
        hooks.find("void mua_player_list_name(") < hooks.find("#else\n") &&
        hooks.find("#else\n") < hooks.find("void mua_player_stats_name("));
  CHECK(mua::kStatsTitleOffset == 80 && mua::kStatsTitleTextOffset == 8 && mua::kStatsTitleCapacity == 64);
  CHECK(!Has(hooks, "SetFlagByName"));
  CHECK(Has(ReadFile(kRoot / "CMakeLists.txt"), "    src/player_name_hooks.cpp\n"));

  size_t hooked = 0;
  for (const auto& entry : fs::directory_iterator(kSources)) {
    if (entry.path().extension() == ".cpp") {
      hooked += Count(ReadFile(entry.path()), "REX_HOOK_RAW(MUA_GUEST_PLAYER_NUMBER_NAME)");
      if (entry.path().filename() != "player_name_hooks.cpp") {
        CHECK(!Has(ReadFile(entry.path()), "mua_player_list_name"));
        CHECK(!Has(ReadFile(entry.path()), "mua_player_stats_name"));
      }
    }
  }
  CHECK(hooked == 1);
}

// Every [[midasm_hook]] address of a game's overrides.toml, with its name.
std::map<std::string, std::vector<std::string>> MidasmHooks(const std::string& game) {
  std::map<std::string, std::vector<std::string>> hooks;
  const std::string text = ReadFile(kRoot / "config" / game / "overrides.toml");
  const std::regex hook(R"re(\[\[midasm_hook\]\]\naddress = 0x([0-9A-F]{8})\nname = "(\w+)")re");
  for (std::sregex_iterator it(text.begin(), text.end(), hook), end; it != end; ++it) {
    hooks[(*it)[1].str()].push_back((*it)[2].str());
  }
  return hooks;
}

void TestGuestAddresses() {
  std::printf("each game binds its own verified addresses, none shared with another hook\n");
  std::set<std::string> numbers;
  std::set<std::string> sites;
  for (const GameHooks& game : kGames) {
    const std::string map = ReadSource(game.map);
    CHECK(Has(map, std::string("#define MUA_GUEST_PLAYER_NUMBER_NAME sub_") + game.player_number_name + "\n"));
    const std::regex mapped(R"(#define MUA_GUEST_[A-Z_]+ sub_([0-9A-F]{8}))");
    size_t same = 0;
    for (std::sregex_iterator it(map.begin(), map.end(), mapped), end; it != end; ++it) {
      same += (*it)[1].str() == game.player_number_name;
      CHECK(!*game.list_function || (*it)[1].str() != game.list_function);
    }
    CHECK(same == 1);
    numbers.insert(game.player_number_name);

    const auto midasm = MidasmHooks(game.key);
    size_t list_hooks = 0;
    size_t stats_hooks = 0;
    for (const auto& [address, names] : midasm) {
      CHECK(names.size() == 1);
      for (const std::string& name : names) {
        list_hooks += name == "mua_player_list_name";
        stats_hooks += name == "mua_player_stats_name";
      }
    }
    if (*game.stats_site) {
      CHECK(stats_hooks == 1);
      const std::string overrides = ReadFile(kRoot / "config" / game.key / "overrides.toml");
      CHECK(Has(overrides, std::string("address = 0x") + game.stats_site +
                               "\nname = \"mua_player_stats_name\"\nregisters = [\"r31\", \"r28\"]\n"
                               "after_instruction = false\n"));
    } else {
      CHECK(stats_hooks == 0);
    }
    if (*game.list_site) {
      CHECK(list_hooks == 1);
      CHECK(midasm.contains(game.list_site) && midasm.at(game.list_site).front() == "mua_player_list_name");
      const std::string overrides = ReadFile(kRoot / "config" / game.key / "overrides.toml");
      CHECK(Has(overrides, std::string("address = 0x") + game.list_site +
                               "\nname = \"mua_player_list_name\"\nregisters = [\"r1\"]\nafter_instruction = false\n"));
      sites.insert(game.list_site);
    } else {
      CHECK(list_hooks == 0);
    }
  }
  CHECK(numbers.size() == 3);
  CHECK(sites.size() == 2);
}

std::string GeneratedBody(const fs::path& module, const std::string& function) {
  for (const auto& entry : fs::directory_iterator(module)) {
    if (entry.path().extension() != ".cpp") {
      continue;
    }
    const std::string text = ReadFile(entry.path());
    const size_t start = text.find("DEFINE_REX_FUNC(" + function + ") {");
    if (start == std::string::npos) {
      continue;
    }
    const size_t end = text.find("\n}\n", start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
  }
  return {};
}

// The instructions of a generated function by guest address.
std::map<uint32_t, std::string> Instructions(const std::string& body, uint32_t start) {
  std::map<uint32_t, std::string> instructions;
  std::istringstream lines(body);
  uint32_t address = start;
  for (std::string line; std::getline(lines, line);) {
    if (line.starts_with("\t// ")) {
      instructions[address] = line.substr(4);
      address += 4;
    }
  }
  return instructions;
}

size_t CallsIn(const fs::path& module, const std::string& function) {
  size_t calls = 0;
  for (const auto& entry : fs::directory_iterator(module)) {
    if (entry.path().extension() == ".cpp") {
      calls += Count(ReadFile(entry.path()), "\t" + function + "(ctx, base);");
    }
  }
  return calls;
}

void TestGeneratedCode() {
#ifdef MUA_GENERATED_ROOT
  std::printf("the hooked code is what the hooks expect in each generated module\n");
  for (const GameHooks& game : kGames) {
    const fs::path module = fs::path(MUA_GENERATED_ROOT) / game.key;
    const std::string registry = ReadFile(module / "marvel_ultimate_alliance_register.cpp");
    if (registry.empty()) {
      std::printf("  %s: module not generated, skipped\n", game.key);
      continue;
    }

    // The localized "Player 1".."Player 4" of a user that is not signed in, string IDs 1230-1233, and nothing else.
    const std::string number = std::string("sub_") + game.player_number_name;
    CHECK(Has(registry, "SetFunction(0x" + std::string(game.player_number_name) + ", " + number + ")"));
    const std::string body = GeneratedBody(module, number);
    CHECK(!body.empty());
    for (const char* id : {",1230\n", ",1231\n", ",1232\n", ",1233\n"}) {
      CHECK(Has(body, id));
    }
    CHECK(Has(body, "// lwz r") && Has(body, ",8(r") && Has(body, "// bctrl"));
    CHECK(!std::regex_search(body, std::regex(R"(REX_STORE_U\d+\((?!ctx\.r1\.u32 \+ |ea, ))")));
    CHECK(CallsIn(module, number) == 1);

    // The Statistics title: the primary port (vt+164 of the player state, its field +64) is formatted with
    // "Player %d Statistics" into the 64-byte string object at r31 + 80, whose text pointer (+8) Lua then reads.
    if (*game.stats_function) {
      const uint32_t start = uint32_t(std::stoul(game.stats_function, nullptr, 16));
      const uint32_t site = uint32_t(std::stoul(game.stats_site, nullptr, 16));
      const std::string stats_body = GeneratedBody(module, std::string("sub_") + game.stats_function);
      const auto code = Instructions(stats_body, start);
      CHECK(code.contains(site - 72) && code.contains(site + 12));
      if (code.contains(site - 72) && code.contains(site + 12)) {
        CHECK(code.at(site - 64) == "lwz r10,164(r11)");
        CHECK(code.at(site - 48) == "mr r28,r3");
        CHECK(code.at(site - 44) == "li r5,64");
        CHECK(code.at(site - 40) == "addi r3,r31,80");
        CHECK(code.at(site - 32) == "addi r4,r1,80");
        CHECK(code.at(site - 24) == "lis r8,-32244");
        CHECK(code.at(site - 20) == "addi r6,r28,1");
        CHECK(code.at(site - 16) == "addi r5,r8,22184");
        CHECK(code.at(site - 8) == "addi r3,r31,80");
        CHECK(code.at(site - 4) == "bl 0x822cbba0");
        CHECK(code.at(site) == "lis r7,-31952");
        CHECK(code.at(site + 12) == "lwz r28,88(r31)");
      }
      CHECK(Has(stats_body, "ctx.lr = 0x" + std::string(game.stats_site) + ";"));
      std::string branch_target = std::string("0x") + game.stats_site + "\n";
      for (char& c : branch_target) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
      }
      CHECK(!Has(stats_body, branch_target));
      CHECK(Count(stats_body, "mua_player_stats_name(ctx.r31, ctx.r28);") <= 1);
      CHECK(CallsIn(module, std::string("sub_") + game.stats_function) == 0);
      const std::string string_object = GeneratedBody(module, "sub_822CB560");
      CHECK(Has(string_object, "// stw r5,4(r3)\n") && Has(string_object, "// stw r4,8(r3)\n") &&
            Has(string_object, "// stw r11,0(r3)\n"));
      CHECK(Has(GeneratedBody(module, "sub_8244D958"), "// lwz r3,64(r3)\n"));
    }

    if (!*game.list_function) {
      bool list_hook = false;
      for (const auto& entry : fs::directory_iterator(module)) {
        list_hook = list_hook || (entry.path().extension() == ".cpp" && Has(ReadFile(entry.path()), "mua_player_list_name("));
      }
      CHECK(!list_hook);
      continue;
    }

    // The Players list fill: "Player %s:" (623) formatted with the port text at r1 + 80 into r1 + 208, expanded into
    // the 100-byte row text at r1 + 96, its last character cut, then handed to the list (vt+140) right at the site.
    const uint32_t start = uint32_t(std::stoul(game.list_function, nullptr, 16));
    const uint32_t site = uint32_t(std::stoul(game.list_site, nullptr, 16));
    const std::string list_body = GeneratedBody(module, std::string("sub_") + game.list_function);
    CHECK(!list_body.empty());
    const auto code = Instructions(list_body, start);
    CHECK(code.contains(site));
    if (!code.contains(site) || !code.contains(site - 4) || !code.contains(site + 12)) {
      continue;
    }
    CHECK(code.at(site) == "lwz r11,0(r31)");
    CHECK(code.at(site + 4) == "lwz r11,140(r11)");
    CHECK(code.at(site + 8) == "mtctr r11");
    CHECK(code.at(site + 12) == "bctrl ");
    CHECK(std::regex_match(code.at(site - 4), std::regex(R"(stbx r\d+,r\d+,r11)")));
    CHECK(Has(list_body, "// li r4,623\n"));
    CHECK(Has(list_body, "// addi r5,r1,80\n"));
    CHECK(Has(list_body, "// addi r3,r1,80\n") && Has(list_body, "// li r5,10\n"));
    CHECK(Has(list_body, "// addi r3,r1,96\n") && Has(list_body, "// addi r4,r1,96\n"));
    CHECK(std::regex_search(list_body, std::regex(R"(// stb r\d+,196\(r1\)\n)")));
    CHECK(Has(list_body, "// lwz r11,148(r11)\n") && Has(list_body, "// lwz r11,88(r11)\n"));
    CHECK(Count(list_body, "mua_player_list_name(ctx.r1);") <= 1);
    CHECK(Count(list_body, "// li r4,623\n") == 1);

    // The pad manager's vt+148 writes the port as one ASCII digit, '1' + port, and a NUL.
    const std::string port = GeneratedBody(module, std::string("sub_") + game.list_port_writer);
    CHECK(Has(port, "// addi r11,r3,49\n"));
    CHECK(Has(port, "// stb r11,0(r31)\n") && Has(port, "// stb r30,1(r31)\n"));
  }
#endif
}

}  // namespace

int main() {
  TestValidator();
  TestMessages();
  TestEditGate();
  TestKeysAndSdkValues();
  TestSettingsAreShared();
  TestLauncherSection();
  TestIdentityUnchanged();
  TestSharedValidator();
  TestGuestNameBytes();
  TestPlayerListRow();
  TestHooks();
  TestGuestAddresses();
  TestGeneratedCode();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
