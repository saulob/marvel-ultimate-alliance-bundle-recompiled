// Skip intro: default, persistence/per-game isolation, Gameplay row order, the MUA1/Gold startup movie skip (Off never
// skips; it covers intro_normal's movies only and ends when the start menu opens), the hooks acting only inside it,
// that the Press Start bypass and failed closemenu/dispatcher approaches are gone, the MUA2 midasm hook, and no
// generic movie or VFS suppression.
// Run: mua_skip_intro_tests

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "skip_intro.h"

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

const std::filesystem::path kSrcDir = MUA_SRC_DIR;

std::string ReadSource(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

std::string WithoutCr(const std::string& text) {
  std::string normalized;
  for (const char c : text) {
    if (c != '\r') {
      normalized += c;
    }
  }
  return normalized;
}

// The text from the first begin marker up to the next end marker, or empty when either is missing.
std::string Between(const std::string& text, const char* begin, const char* end) {
  const size_t start = text.find(begin);
  if (start == std::string::npos) {
    return {};
  }
  const size_t stop = text.find(end, start);
  if (stop == std::string::npos) {
    return {};
  }
  return text.substr(start, stop - start);
}

void TestDefault() {
  std::printf("skip intro default\n");
  const std::string header = ReadSource(kSrcDir / "settings.h");
  CHECK(Has(header, "REXCVAR_DECLARE(bool, mua_skip_intro);"));

  const std::string settings = ReadSource(kSrcDir / "settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_skip_intro, false,"));
}

void TestPersistenceAndIsolation() {
  std::printf("skip intro persistence and per-game isolation\n");
  const std::string settings = ReadSource(kSrcDir / "settings.cpp");
  const std::string keys = Between(settings, "std::vector<std::string> GameSettingKeys() {", "return keys;");
  CHECK(Has(keys, "\"mua_skip_intro\","));

  const std::string scoped = Between(settings, "kGameScopedKeys[] = {", "};");
  CHECK(Has(scoped, "\"mua_skip_intro\""));
}

void TestGameplayRow() {
  std::printf("skip intro gameplay row\n");
  const std::string launcher = ReadSource(kSrcDir / "launcher.cpp");
  const std::string page = Between(launcher, "void PageGameplay(trg::Ui& ui) {", "SelectedMonitor()");
  CHECK(!page.empty());

  const size_t skip = page.find("\"mua_skip_intro\"");
  const size_t frame_rate = page.find("\"mua_frame_rate\"");
  CHECK(skip != std::string::npos);
  CHECK(frame_rate != std::string::npos);
  CHECK(skip < frame_rate);  // first option, before frame rate
  CHECK(Has(page, "ui.Toggle(\"Skip intro\", \"Start directly at the title screen\", \"mua_skip_intro\", false, "
                  "\"Off\", \"On\");"));
}

void TestGateFlags() {
  std::printf("skip intro gate flags\n");
  CHECK(mua::ShouldSkipBootIntro(true));
  CHECK(!mua::ShouldSkipBootIntro(false));
  CHECK(mua::kLegalShownFlagOffset == 40 && mua::kLegalShownFlagBit == 0x10);
  CHECK(mua::kEsrbShownFlagOffset == 48 && mua::kEsrbShownFlagBit == 0x40);
  CHECK(mua::kIntroScriptFlagOffset == 40 && mua::kIntroScriptFlagBit == 0x40);
  CHECK(mua::kStartMenuName == "start");
}

void TestOffNeverSkips() {
  std::printf("skip intro off never skips\n");
  mua::BootIntroSkip intro;
  intro.OnIntroGate(false, 0x10, 0x50);
  CHECK(!intro.skipping_movies());
  intro.OnOpenMenu("movie");
  CHECK(!intro.skipping_movies());
}

void TestMovieSkipLifecycle() {
  std::printf("skip intro movie skip lifecycle\n");
  mua::BootIntroSkip intro;
  intro.OnIntroGate(true, 0x00, 0x10);  // legal/ESRB steps: intro_normal not issued yet
  CHECK(!intro.skipping_movies());
  intro.OnIntroGate(true, 0x30, 0x70);  // the call that issues runscript menus/intro_normal
  CHECK(intro.skipping_movies());

  // Between intro movies the game reopens "blank" and "movie"; neither ends the skip.
  intro.OnOpenMenu("blank");
  intro.OnOpenMenu("movie");
  CHECK(intro.skipping_movies());

  intro.OnOpenMenu("start");  // the Press Start screen is left to the player
  CHECK(!intro.skipping_movies());

  // Later gate calls, a return to "start" and story movies (new_game.py's cine01, epilogues) stay untouched.
  intro.OnIntroGate(true, 0x00, 0x40);
  intro.OnOpenMenu("movie");
  intro.OnOpenMenu("start");
  CHECK(!intro.skipping_movies());
}

void TestMua1GoldAddresses() {
  std::printf("skip intro mua1/gold profile addresses\n");
  const std::string mua1 = ReadSource(kSrcDir / "guest_map_mua1.h");
  CHECK(Has(mua1, "#define MUA_GUEST_INTRO_GATE sub_82111868"));
  CHECK(Has(mua1, "#define MUA_GUEST_MOVIE_MENU_UPDATE sub_823F51E8"));
  CHECK(Has(mua1, "#define MUA_GUEST_OPEN_MENU sub_8240DC10"));
  CHECK(Has(mua1, "#define MUA_GUEST_MENU_MANAGER sub_82409678"));
  CHECK(Has(mua1, "#define MUA_GUEST_MOVIE_PLAYING sub_82402150"));
  CHECK(Has(mua1, "#define MUA_GUEST_STOP_MOVIE sub_82403A98"));

  const std::string gold = ReadSource(kSrcDir / "guest_map_mua1_gold.h");
  CHECK(Has(gold, "#define MUA_GUEST_INTRO_GATE sub_82121898"));
  CHECK(Has(gold, "#define MUA_GUEST_MOVIE_MENU_UPDATE sub_8240B8B8"));
  CHECK(Has(gold, "#define MUA_GUEST_OPEN_MENU sub_82424738"));
  CHECK(Has(gold, "#define MUA_GUEST_MENU_MANAGER sub_82420268"));
  CHECK(Has(gold, "#define MUA_GUEST_MOVIE_PLAYING sub_82418C50"));
  CHECK(Has(gold, "#define MUA_GUEST_STOP_MOVIE sub_8241A600"));

  const std::string mua2 = ReadSource(kSrcDir / "guest_map_mua2.h");
  for (const char* name : {"MUA_GUEST_INTRO_GATE", "MUA_GUEST_MOVIE_MENU_UPDATE", "MUA_GUEST_OPEN_MENU",
                           "MUA_GUEST_MENU_MANAGER", "MUA_GUEST_MOVIE_PLAYING", "MUA_GUEST_STOP_MOVIE"}) {
    CHECK(!Has(mua2, name));
  }
}

// Each hook runs the original unconditionally and only adds its skip action while movies are being skipped, so
// with Skip intro off every hooked function behaves exactly like the game's own code.
void TestHooksActOnlyWhileSkippingMovies() {
  std::printf("skip intro hooks act only while skipping the startup movies\n");
  const std::string hook = WithoutCr(ReadSource(kSrcDir / "skip_intro.cpp"));

  const std::string gate = Between(hook, "REX_HOOK_RAW(MUA_GUEST_INTRO_GATE) {", "\n}\n");
  CHECK(Has(gate, "if (skip) {\n    base[self + mua::kLegalShownFlagOffset] |= mua::kLegalShownFlagBit;"));
  CHECK(Has(gate, "MUA_GUEST_ORIGINAL(MUA_GUEST_INTRO_GATE)(ctx, base);"));
  CHECK(Has(gate, "g_boot_intro.OnIntroGate(skip, script_flags_before, base[self + mua::kIntroScriptFlagOffset]);"));
  CHECK(!Has(gate, "kIntroScriptFlagOffset] |="));  // the RunScript step itself is never forced or suppressed

  const std::string update = Between(hook, "REX_HOOK_RAW(MUA_GUEST_MOVIE_MENU_UPDATE) {", "\n}\n");
  const size_t window = update.find("if (g_boot_intro.skipping_movies()) {");
  const size_t stop = update.find("MUA_GUEST_STOP_MOVIE(ctx, base);");
  const size_t original = update.find("MUA_GUEST_ORIGINAL(MUA_GUEST_MOVIE_MENU_UPDATE)(ctx, base);");
  CHECK(window != std::string::npos && stop != std::string::npos && original != std::string::npos);
  CHECK(window < stop && stop < original);
  CHECK(Has(update, "if (MoviePlaying(ctx, base, manager)) {"));
  CHECK(Has(update, "ctx.r3.u64 = menu;"));

  const std::string open = Between(hook, "REX_HOOK_RAW(MUA_GUEST_OPEN_MENU) {", "\n}\n");
  CHECK(Has(open, "if (g_boot_intro.skipping_movies()) {"));
  CHECK(Has(open, "g_boot_intro.OnOpenMenu("));
  CHECK(Has(open, "MUA_GUEST_ORIGINAL(MUA_GUEST_OPEN_MENU)(ctx, base);"));
}

struct GuestFunction {
  const char* name;
  const char* mua1;
  const char* gold;
};

const GuestFunction kBypassFunctions[] = {
    {"SET_ACTIVE_PAD", "sub_82408A20", "sub_8241F730"},
    {"USER_MANAGER", "sub_821E7070", "sub_821F8688"},
    {"USER_SET_CONTROLLER", "sub_821E8CC8", "sub_821FA2C8"},
    {"USER_REFRESH", "sub_821E6708", "sub_821F7D28"},
    {"USER_GET_SLOT", "sub_821E6E20", "sub_8263AE38"},
    {"USER_SET_SLOT", "sub_82619520", "sub_82633D00"},
    {"INPUT_MANAGER", "sub_82456B48", "sub_8246EFD0"},
    {"INPUT_SET_NAME", "sub_82454558", "sub_8246C4C8"},
    {"INPUT_SET_FLAGS", "sub_82454420", "sub_8246C390"},
    {"INPUT_CLEAR_FLAGS", "sub_82454450", "sub_8246C3C0"},
    {"PROFILE_MANAGER", "sub_82355F50", "sub_8236ABB8"},
    {"PROFILE_REFRESH_PAD", "sub_82355F60", "sub_8236ABC8"},
    {"PROFILE_RESET_ALL", "sub_823556B0", "sub_8236A258"},
    {"PROFILE_QUERY_ALL", "sub_82355708", "sub_8236A2B0"},
    {"PROFILE_SIGNED_IN", "sub_82355768", "sub_8236A310"},
    {"PROFILE_ONLINE", "sub_82355788", "sub_8236A330"},
    {"PROFILE_NAME", "sub_82355E70", "sub_8236AAD8"},
    {"SIGNIN_PUMP", "sub_82356E58", "sub_8236BBB0"},
    {"SIGNIN_PUMP_SET_FLAG_A", "sub_82356D68", "sub_8236BAC0"},
    {"SIGNIN_PUMP_SET_FLAG_B", "sub_82356D70", "sub_8236BAC8"},
    {"CONSOLE", "sub_823453A8", "sub_82359770"},
    {"CONSOLE_QUEUE", "sub_82344CB0", "sub_82359080"},
    {"PAD_MANAGER", "sub_8233C588", "sub_823508A0"},
    {"PAD_CONNECTED", "sub_8233AD90", "sub_8234F090"},
};

void TestBypassFunctionMapping() {
  std::printf("skip intro start-screen bypass functions are mapped for mua1 and gold only\n");
  const std::string mua1 = ReadSource(kSrcDir / "guest_map_mua1.h");
  const std::string gold = ReadSource(kSrcDir / "guest_map_mua1_gold.h");
  const std::string mua2 = ReadSource(kSrcDir / "guest_map_mua2.h");
  for (const GuestFunction& function : kBypassFunctions) {
    const std::string macro = std::string("#define MUA_GUEST_") + function.name + " ";
    CHECK(Has(mua1, (macro + function.mua1).c_str()));
    CHECK(Has(gold, (macro + function.gold).c_str()));
    CHECK(!Has(mua2, (std::string("MUA_GUEST_") + function.name).c_str()));
  }
  CHECK(Has(mua1, "#define MUA_GUEST_SCRIPT_DISPATCH sub_82344378"));
  CHECK(Has(gold, "#define MUA_GUEST_SCRIPT_DISPATCH sub_82358780"));
  CHECK(!Has(mua2, "MUA_GUEST_SCRIPT_DISPATCH"));
  CHECK(Has(mua1, "kInputUserSlotOffset = 1488;"));
  CHECK(Has(mua1, "kMainMenuExitCommand = 0x82015DE8;"));
  CHECK(Has(gold, "kInputUserSlotOffset = 1520;"));
  CHECK(Has(gold, "kMainMenuExitCommand = 0x82015A10;"));
  CHECK(!Has(mua2, "kMainMenuExitCommand"));
  CHECK(mua::kNoPad == -1 && mua::kMaxPads == 4 && mua::kOnlineProfileFlag == 32);
}

// The skipped start menu's required effects come from the game's own functions, in the order its activation and its
// confirm branch call them, ending in the game's console queue.
void TestBypassRunsTheOriginalFunctionsInOrder() {
  std::printf("skip intro bypass runs the original activation and confirm functions in order\n");
  const std::string hook = WithoutCr(ReadSource(kSrcDir / "skip_intro.cpp"));
  const std::string confirm = Between(hook, "bool ConfirmStartScreen(", "\n}\n");
  CHECK(!confirm.empty());

  size_t from = 0;
  for (const char* call : {"MUA_GUEST_USER_REFRESH,", "MUA_GUEST_SIGNIN_PUMP_SET_FLAG_B,",
                           "MUA_GUEST_SIGNIN_PUMP_SET_FLAG_A,", "MUA_GUEST_PROFILE_RESET_ALL,",
                           "MUA_GUEST_PROFILE_QUERY_ALL,", "MUA_GUEST_SET_ACTIVE_PAD,",
                           "MUA_GUEST_USER_SET_CONTROLLER,", "MUA_GUEST_USER_SET_SLOT,", "MUA_GUEST_USER_GET_SLOT,",
                           "mua::guest::kInputUserSlotOffset", "MUA_GUEST_PROFILE_REFRESH_PAD,",
                           "MUA_GUEST_PROFILE_SIGNED_IN,", "MUA_GUEST_PROFILE_NAME,", "MUA_GUEST_INPUT_SET_NAME,",
                           "MUA_GUEST_PROFILE_ONLINE,", "MUA_GUEST_INPUT_SET_FLAGS : MUA_GUEST_INPUT_CLEAR_FLAGS",
                           "MUA_GUEST_CONSOLE_QUEUE,"}) {
    const size_t at = confirm.find(call, from);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) {
      break;
    }
    from = at + 1;
  }
  CHECK(Has(confirm, "mua::guest::kMainMenuExitCommand"));
  CHECK(Has(confirm, "return (CallMethod(MUA_GUEST_CONSOLE_QUEUE"));

  // Direct guest-memory stores: the game's own `input manager user slot = user manager slot` store, and the boot
  // gate's stamp (see the legal-wait test).
  size_t stores = 0;
  for (size_t at = hook.find("std::memcpy(base +"); at != std::string::npos;
       at = hook.find("std::memcpy(base +", at + 1)) {
    ++stores;
  }
  CHECK(stores == 2);
  CHECK(Has(confirm, "std::memcpy(base + input + mua::guest::kInputUserSlotOffset, &swapped_slot"));
  CHECK(!Has(hook, "MUA_GUEST_CONSOLE_QUEUE(ctx"));  // only through CallMethod with the console object
}

// The start menu is skipped only when a pad is connected and the queue accepted the command; otherwise the original
// OpenMenu runs with its own arguments, and the skip window is over either way.
void TestBypassFallsBackToTheStartMenu() {
  std::printf("skip intro bypass falls back to the normal start menu\n");
  const std::string hook = WithoutCr(ReadSource(kSrcDir / "skip_intro.cpp"));
  const std::string skip = Between(hook, "bool SkipStartMenu(", "\n}\n");
  CHECK(Has(skip, "const int32_t pad = FirstConnectedPad(ctx, base);"));
  CHECK(Has(skip, "pad != mua::kNoPad && ConfirmStartScreen(ctx, base, pad)"));
  CHECK(Has(skip, "ctx.r3.u64 = r3;"));
  CHECK(Has(skip, "ctx.r4.u64 = r4;"));
  CHECK(Has(skip, "restore();"));

  const std::string open = Between(hook, "REX_HOOK_RAW(MUA_GUEST_OPEN_MENU) {", "\n}\n");
  const size_t window = open.find("if (g_boot_intro.skipping_movies()) {");
  const size_t finished = open.find("g_boot_intro.OnOpenMenu(menu);");
  const size_t bypass = open.find("const bool skipped = SkipStartMenu(ctx, base);");
  CHECK(Has(open, "if (skipped) {\n        return;"));
  const size_t original = open.find("MUA_GUEST_ORIGINAL(MUA_GUEST_OPEN_MENU)(ctx, base);");
  CHECK(window != std::string::npos && finished != std::string::npos && bypass != std::string::npos &&
        original != std::string::npos);
  CHECK(window < finished && finished < bypass && bypass < original);
}

// No presenter work, host input, timers or synthesized start-menu update/input hooks.
void TestBypassHasNoWorkarounds() {
  std::printf("skip intro bypass has no presenter, input, timer or start-menu hooks\n");
  for (const char* file : {"skip_intro.cpp", "skip_intro.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    for (const char* needle : {"START_MENU", "MENU_INPUT", "kStartConfirmAction", "SetGuestOutputHidden", "Presenter",
                               "DEBUG_SAULO", "REXLOG", "SDL", "Sleep", "sleep_for", "chrono", "steady_clock",
                               "frame_count", "SOUND", "272"}) {
      CHECK(!Has(text, needle));
    }
  }
  for (const char* file : {"guest_map_mua1.h", "guest_map_mua1_gold.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    for (const char* needle : {"START_MENU_UPDATE", "MENU_INPUT"}) {
      CHECK(!Has(text, needle));
    }
  }
  const std::filesystem::path patches = kSrcDir / ".." / "patches" / "rexglue-sdk";
  CHECK(!std::filesystem::exists(patches / "0129-presenter-hide-guest-output.patch"));
  CHECK(!std::filesystem::exists(kSrcDir / "start_state_probe.cpp"));
  CHECK(!std::filesystem::exists(kSrcDir / "start_state_probe.h"));
}

void TestBootStateMachineForLegalAndIntro() {
  std::printf("skip intro legal menu and intro script state\n");
  CHECK(mua::IsLegalMenu("legal_360"));
  CHECK(mua::IsLegalMenu("legal_360_uk"));
  CHECK(!mua::IsLegalMenu("start"));
  CHECK(!mua::IsLegalMenu("movie"));
  CHECK(!mua::IsLegalMenu("ESRB_warning"));
  CHECK(mua::IsIntroScriptCommand("runscript menus/intro_normal"));
  CHECK(!mua::IsIntroScriptCommand("runscript menus/new_game"));
  CHECK(!mua::IsIntroScriptCommand("openmenu start"));
  CHECK(mua::kStartMenuCommand == "openmenu start");
  CHECK(mua::kBootGateStampOffset == 36 && mua::kBootGateSecondsAddress == 0x820009A8);
  CHECK(mua::ElapsedBootStamp(7.5f, 5.0f) == 2.5f);

  mua::BootIntroSkip off;
  CHECK(off.before_intro());
  off.OnIntroScriptReplaced(false);
  CHECK(off.before_intro() && !off.skipping_movies());

  mua::BootIntroSkip on;
  on.OnIntroScriptReplaced(true);
  CHECK(!on.before_intro() && on.skipping_movies());
  on.OnOpenMenu("start");  // the replaced command's own "openmenu start"
  CHECK(!on.before_intro() && !on.skipping_movies());
  on.OnIntroScriptReplaced(true);  // never restarts
  CHECK(!on.skipping_movies());
}

// Skip intro on: the legal screen is not opened, its display wait is treated as elapsed, and the intro command is
// replaced by the script's final step. Every piece is gated on the setting and on the pre-intro state.
void TestLegalAndIntroFastForwardHooks() {
  std::printf("skip intro legal and intro fast-forward hooks\n");
  const std::string hook = WithoutCr(ReadSource(kSrcDir / "skip_intro.cpp"));

  const std::string open = Between(hook, "REX_HOOK_RAW(MUA_GUEST_OPEN_MENU) {", "\n}\n");
  const size_t legal = open.find("mua::IsLegalMenu(requested)");
  const size_t skipped = open.find("return;", legal);
  const size_t original = open.find("MUA_GUEST_ORIGINAL(MUA_GUEST_OPEN_MENU)(ctx, base);");
  CHECK(legal != std::string::npos && skipped != std::string::npos && original != std::string::npos);
  CHECK(legal < skipped && skipped < original);
  CHECK(Has(open, "g_boot_intro.before_intro() && mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro)) &&"));

  const std::string dispatch = Between(hook, "REX_HOOK_RAW(MUA_GUEST_SCRIPT_DISPATCH) {", "\n}\n");
  CHECK(Has(dispatch, "rex::ppc::stack_guard guard(ctx);"));
  CHECK(Has(dispatch, "g_boot_intro.before_intro() && mua::ShouldSkipBootIntro(REXCVAR_GET(mua_skip_intro)) &&"));
  CHECK(Has(dispatch, "mua::IsIntroScriptCommand(GuestString(base, ctx.r4.u32))"));
  CHECK(Has(dispatch, "g_boot_intro.OnIntroScriptReplaced(true);"));
  CHECK(Has(dispatch, "ctx.r4.u32 = rex::ppc::stack_push_string("));
  CHECK(Has(dispatch, "std::string(mua::kStartMenuCommand)"));
  CHECK(dispatch.find("ctx.r4.u32 =") < dispatch.find("MUA_GUEST_ORIGINAL(MUA_GUEST_SCRIPT_DISPATCH)(ctx, base);"));
  CHECK(!Has(dispatch, "ctx.r3"));  // the console the game passed is untouched

  const std::string gate = Between(hook, "REX_HOOK_RAW(MUA_GUEST_INTRO_GATE) {", "\n}\n");
  const size_t elapse = gate.find("ElapseLegalWait(base, self);");
  CHECK(elapse != std::string::npos);
  CHECK(gate.find("if (skip) {") < elapse);
  CHECK(gate.find("if (g_boot_intro.before_intro()) {") < elapse);
  CHECK(elapse < gate.find("MUA_GUEST_ORIGINAL(MUA_GUEST_INTRO_GATE)(ctx, base);"));

  // The only stamp write is the elapse helper's, and it shifts the game's own stamp by the game's own constant.
  const std::string elapsed = Between(hook, "void ElapseLegalWait(", "\n}\n");
  CHECK(Has(elapsed, "mua::ElapsedBootStamp(stamp, LoadGuestFloat(base, mua::kBootGateSecondsAddress))"));
  CHECK(Has(elapsed, "StoreGuestFloat(base, gate + mua::kBootGateStampOffset, elapsed);"));
  size_t float_stores = 0;
  for (size_t at = hook.find("StoreGuestFloat(base"); at != std::string::npos;
       at = hook.find("StoreGuestFloat(base", at + 1)) {
    ++float_stores;
  }
  CHECK(float_stores == 1);

  // Required boot calls stay with the game: nothing here issues resetgame, launchMap or the boot steps themselves.
  for (const char* needle : {"resetgame", "launchMap", "savedefaultoptions", "deviceSelector", "ESRB_warning"}) {
    CHECK(!Has(hook, needle));
  }
}

// Calling the closemenu handler with a raw string and re-entering the console dispatcher both crashed; neither may
// come back.
void TestFailedApproachesAreGone() {
  std::printf("skip intro failed closemenu/dispatcher approaches are gone\n");
  for (const char* file : {"guest_map_mua1.h", "guest_map_mua1_gold.h", "guest_map_mua2.h", "skip_intro.cpp",
                           "skip_intro.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    CHECK(!Has(text, "MUA_GUEST_CLOSE_MENU"));
  }
  const std::string hook = ReadSource(kSrcDir / "skip_intro.cpp");
  CHECK(!Has(hook, "closemenu"));
  CHECK(!Has(hook, "openmenu movie"));
  CHECK(!Has(hook, "runscript"));

  // The dispatcher runs once per hooked call, on the console the game passed, with at most its argument replaced.
  size_t dispatches = 0;
  for (size_t at = hook.find("MUA_GUEST_SCRIPT_DISPATCH(ctx"); at != std::string::npos;
       at = hook.find("MUA_GUEST_SCRIPT_DISPATCH(ctx", at + 1)) {
    ++dispatches;
  }
  CHECK(dispatches == 0);
  size_t originals = 0;
  for (size_t at = hook.find("MUA_GUEST_ORIGINAL(MUA_GUEST_SCRIPT_DISPATCH)(ctx, base);"); at != std::string::npos;
       at = hook.find("MUA_GUEST_ORIGINAL(MUA_GUEST_SCRIPT_DISPATCH)(ctx, base);", at + 1)) {
    ++originals;
  }
  CHECK(originals == 1);
}

void TestMua2Hook() {
  std::printf("skip intro mua2 hook\n");
  const std::string hook = ReadSource(kSrcDir / "skip_intro.cpp");
  CHECK(Has(hook, "#if defined(MUA_RUNTIME_MUA2)"));
  CHECK(Has(hook, "void mua_skip_intro_mua2(PPCRegister& already_shown) {"));
  CHECK(Has(hook, "already_shown.u32 = 0;"));

  const std::string overrides = WithoutCr(ReadSource(kSrcDir / ".." / "config" / "mua2" / "overrides.toml"));
  CHECK(Has(overrides, "address = 0x8252A72C\nname = \"mua_skip_intro_mua2\"\nregisters = [\"r31\"]"));
  CHECK(Has(overrides, "after_instruction = false"));

#ifdef MUA_GENERATED_ROOT
  std::printf("skip intro mua2 hook site is a real instruction boundary\n");
  bool found = false;
  for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MUA_GENERATED_ROOT) / "mua2")) {
    if (entry.path().filename().string().find("_recomp.") == std::string::npos ||
        entry.path().extension() != ".cpp") {
      continue;
    }
    if (Has(ReadSource(entry.path()), "ctx.lr = 0x8252A72C;")) {
      found = true;
      break;
    }
  }
  CHECK(found);
#endif
}

void TestNoGenericMovieOrVfsSuppression() {
  std::printf("skip intro does not touch movie playback or the VFS generically\n");
  for (const char* file : {"skip_intro.cpp", "skip_intro.h"}) {
    const std::string text = ReadSource(kSrcDir / file);
    CHECK(!Has(text, ".bik"));
    CHECK(!Has(text, "NtCreateFile"));
    CHECK(!Has(text, "MUA_GUEST_PRESENT_CALLBACK"));
    CHECK(!Has(text, "MUA_GUEST_WORLD_UPDATE"));
  }
}

}  // namespace

int main() {
  TestDefault();
  TestPersistenceAndIsolation();
  TestGameplayRow();
  TestGateFlags();
  TestOffNeverSkips();
  TestMovieSkipLifecycle();
  TestMua1GoldAddresses();
  TestHooksActOnlyWhileSkippingMovies();
  TestBypassFunctionMapping();
  TestBypassRunsTheOriginalFunctionsInOrder();
  TestBypassFallsBackToTheStartMenu();
  TestBypassHasNoWorkarounds();
  TestBootStateMachineForLegalAndIntro();
  TestLegalAndIntroFastForwardHooks();
  TestFailedApproachesAreGone();
  TestMua2Hook();
  TestNoGenericMovieOrVfsSuppression();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
