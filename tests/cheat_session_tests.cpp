// The launcher's cheat settings, the F10 menu's session overrides and the coin award scaling.
// Run: mua_cheat_session_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

#include "cheats.h"

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

std::string ReadSource(const char* name) {
  std::ifstream in(std::string(MUA_SRC_DIR) + "/" + name, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

size_t Count(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

void TestDefaults() {
  using mua::cheats::Cheat;
  using mua::cheats::CoinMultiplier;
  using mua::cheats::SessionCheats;
  const SessionCheats session;
  CHECK(!session.Effective(Cheat::Health, false));
  CHECK(!session.Effective(Cheat::Energy, false));
  CHECK(session.Effective(Cheat::Health, true));
  CHECK(session.Effective(Cheat::Energy, true));
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Off) == CoinMultiplier::Off);
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Five) == CoinMultiplier::Five);
}

void TestSessionOverrides() {
  using mua::cheats::Cheat;
  using mua::cheats::SessionCheats;
  SessionCheats session;
  CHECK(session.Override(Cheat::Health) == -1);
  session.Toggle(Cheat::Health, false);
  CHECK(session.Effective(Cheat::Health, false));
  CHECK(session.Override(Cheat::Health) == 1);
  session.Toggle(Cheat::Health, false);
  CHECK(!session.Effective(Cheat::Health, false));
  CHECK(session.Override(Cheat::Health) == -1);

  // Toggling back to the saved value drops the override: a later saved value is followed again.
  CHECK(session.Effective(Cheat::Health, true));

  session.Set(Cheat::Energy, true, false);
  CHECK(!session.Effective(Cheat::Energy, true));
  CHECK(!session.Effective(Cheat::Energy, false));
  session.Set(Cheat::Energy, false, true);
  CHECK(session.Effective(Cheat::Energy, false));
}

void TestMultiplier() {
  using mua::cheats::CoinMultiplier;
  using mua::cheats::NextCoinMultiplier;
  using mua::cheats::ParseCoinMultiplier;
  using mua::cheats::SessionCheats;

  CHECK(NextCoinMultiplier(CoinMultiplier::Off) == CoinMultiplier::Two);
  CHECK(NextCoinMultiplier(CoinMultiplier::Two) == CoinMultiplier::Five);
  CHECK(NextCoinMultiplier(CoinMultiplier::Five) == CoinMultiplier::Ten);
  CHECK(NextCoinMultiplier(CoinMultiplier::Ten) == CoinMultiplier::Off);

  CHECK(ParseCoinMultiplier(0) == CoinMultiplier::Off);
  CHECK(ParseCoinMultiplier(2) == CoinMultiplier::Two);
  CHECK(ParseCoinMultiplier(5) == CoinMultiplier::Five);
  CHECK(ParseCoinMultiplier(10) == CoinMultiplier::Ten);
  CHECK(ParseCoinMultiplier(3) == CoinMultiplier::Off);
  CHECK(ParseCoinMultiplier(-1) == CoinMultiplier::Off);

  SessionCheats session;
  session.CycleMultiplier(CoinMultiplier::Off);
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Off) == CoinMultiplier::Two);
  session.CycleMultiplier(CoinMultiplier::Off);
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Off) == CoinMultiplier::Five);
  session.CycleMultiplier(CoinMultiplier::Off);
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Off) == CoinMultiplier::Ten);
  session.CycleMultiplier(CoinMultiplier::Off);
  CHECK(session.EffectiveMultiplier(CoinMultiplier::Off) == CoinMultiplier::Off);

  // Cycling back to the launcher's value clears the override.
  SessionCheats follow;
  follow.CycleMultiplier(CoinMultiplier::Five);
  CHECK(follow.EffectiveMultiplier(CoinMultiplier::Five) == CoinMultiplier::Ten);
  follow.CycleMultiplier(CoinMultiplier::Five);
  CHECK(follow.EffectiveMultiplier(CoinMultiplier::Five) == CoinMultiplier::Off);
  follow.CycleMultiplier(CoinMultiplier::Five);
  CHECK(follow.EffectiveMultiplier(CoinMultiplier::Five) == CoinMultiplier::Two);
  follow.CycleMultiplier(CoinMultiplier::Five);
  CHECK(follow.EffectiveMultiplier(CoinMultiplier::Five) == CoinMultiplier::Five);
  CHECK(follow.EffectiveMultiplier(CoinMultiplier::Ten) == CoinMultiplier::Ten);
}

void TestAwardScaling() {
  using mua::cheats::CoinMultiplier;
  using mua::cheats::ScaleAward;
  CHECK(ScaleAward(10, CoinMultiplier::Off) == 10);
  CHECK(ScaleAward(10, CoinMultiplier::Two) == 20);
  CHECK(ScaleAward(10, CoinMultiplier::Five) == 50);
  CHECK(ScaleAward(10, CoinMultiplier::Ten) == 100);
  CHECK(ScaleAward(0, CoinMultiplier::Ten) == 0);
  CHECK(ScaleAward(1, CoinMultiplier::Ten) == 10);
  CHECK(ScaleAward(std::numeric_limits<uint32_t>::max(), CoinMultiplier::Ten) ==
        std::numeric_limits<uint32_t>::max());
}

void TestGuestAddresses() {
  using mua::cheats::ValidGuestActor;
  CHECK(!ValidGuestActor(0));
  CHECK(!ValidGuestActor(0x1000));
  CHECK(ValidGuestActor(0x10000));
  CHECK(ValidGuestActor(0x82000000));
  CHECK(!ValidGuestActor(0xFFFFF000));
  CHECK(!ValidGuestActor(0xFFFFFFFF));
}

void TestLauncherPage() {
  const std::string definition = ReadSource("game_definition.h");
  CHECK(Has(definition, "kPlay, kDisplay, kGraphics, kGameplay, kCheats, kAchievements, kControls, kAbout"));

  const std::string games = ReadSource("game_definition.cpp");
  CHECK(Has(games, "kCheatPages = kPlayablePages | PageBit(LauncherPage::kCheats)"));
  CHECK(Count(games, ".pages = kCheatPages") == 3);
  CHECK(Count(games, ".pages = kPlayablePages") == 0);

  const std::string launcher = ReadSource("launcher.cpp");
  const size_t gameplay = launcher.find("{\"Gameplay\"");
  const size_t cheats = launcher.find("{\"Cheats\"");
  const size_t achievements = launcher.find("{\"Achievements\"");
  CHECK(gameplay != std::string::npos);
  CHECK(cheats != std::string::npos);
  CHECK(achievements != std::string::npos);
  CHECK(gameplay < cheats);
  CHECK(cheats < achievements);
  CHECK(Has(launcher, "void PageCheats(trg::Ui& ui)"));
  CHECK(Has(launcher, "\"mua_cheat_health\""));
  CHECK(Has(launcher, "\"mua_cheat_energy\""));
  CHECK(Has(launcher, "\"mua_cheat_coin_multiplier\""));
  CHECK(Has(launcher, "{{\"0\", \"Off\"}, {\"2\", \"2x\"}, {\"5\", \"5x\"}, {\"10\", \"10x\"}}"));
  CHECK(Has(launcher, "game != GameId::kMua2"));
  CHECK(Has(launcher, "\"mua_turbo_attack\""));
  CHECK(Has(launcher, "{{\"0\", \"Off\"}, {\"1\", \"On\"}}"));
  CHECK(Has(launcher, "ui.Heading(\"COMBAT\")"));
  CHECK(!Has(launcher, "Movement Speed"));
  CHECK(!Has(ReadSource("cheat_menu.cpp"), "Movement Speed"));
}

void TestSettingsFlow() {
  const std::string settings = ReadSource("settings.cpp");
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_cheat_health, false"));
  CHECK(Has(settings, "REXCVAR_DEFINE_BOOL(mua_cheat_energy, false"));
  CHECK(Has(settings, "REXCVAR_DEFINE_INT32(mua_cheat_coin_multiplier, 0"));
  CHECK(Count(settings, "\"mua_cheat_health\"") == 2);
  CHECK(Count(settings, "\"mua_cheat_energy\"") == 2);
  CHECK(Count(settings, "\"mua_cheat_coin_multiplier\"") == 2);
  CHECK(Has(settings, "REXCVAR_DEFINE_INT32(mua_turbo_attack, 0"));
  CHECK(Count(settings, "\"mua_turbo_attack\"") == 2);

  const std::string header = ReadSource("settings.h");
  CHECK(Has(header, "REXCVAR_DECLARE(bool, mua_cheat_health)"));
  CHECK(Has(header, "REXCVAR_DECLARE(bool, mua_cheat_energy)"));
  CHECK(Has(header, "REXCVAR_DECLARE(int32_t, mua_cheat_coin_multiplier)"));
  CHECK(Has(header, "REXCVAR_DECLARE(int32_t, mua_turbo_attack)"));

  // Game-scoped keys are stored per executable, so MUA1 and Gold Edition keep their own cheat settings.
  const size_t scoped = settings.find("kGameScopedKeys[]");
  const size_t scoped_end = settings.find("};", scoped);
  CHECK(scoped != std::string::npos);
  CHECK(scoped_end != std::string::npos);
  const std::string scoped_block = settings.substr(scoped, scoped_end - scoped);
  CHECK(Has(scoped_block, "\"mua_cheat_health\""));
  CHECK(Has(scoped_block, "\"mua_cheat_energy\""));
  CHECK(Has(scoped_block, "\"mua_cheat_coin_multiplier\""));
}

void TestRuntimeSources() {
  const std::string menu = ReadSource("cheat_menu.cpp");
  CHECK(!Has(menu, "CreateContext"));
  CHECK(!Has(menu, "SDL_CreateWindow"));
  CHECK(!Has(menu, "REXCVAR_DEFINE"));
  CHECK(!Has(menu, "settings"));
  CHECK(Has(menu, "VirtualKey::kF10"));
  CHECK(Has(menu, "e.set_handled(true)"));
  CHECK(Has(menu, "ModalDialog::AnyOpen"));
  CHECK(Has(menu, "cheats::ToggleForSession"));
  CHECK(Has(menu, "cheats::CycleMultiplierForSession"));
  CHECK(Has(menu, "#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)"));
  CHECK(Has(menu, "{RowKind::CoinMultiplier, \"ITEMS\", \"Coin Multiplier\"}"));
  CHECK(Has(menu, "{RowKind::TurboAttack, \"COMBAT\", \"Turbo Attack\"}"));
  CHECK(Count(menu, "{RowKind::TurboAttack") == 1);
  CHECK(Has(menu, "cheats::ToggleTurboForSession"));
  // The menu borrows the frame counter's per-game accent instead of keeping its own palette.
  CHECK(Has(menu, "FrameCounterAccent(ActiveGame())"));
  CHECK(Has(menu, "ImGuiCol_Separator, accent"));
  CHECK(!Has(menu, "kAccent"));
  CHECK(!Has(menu, "kRowFocus"));

  const std::string cheats_source = ReadSource("cheats.cpp");
  CHECK(!Has(cheats_source, "REXCVAR_DEFINE"));
  CHECK(!Has(cheats_source, "REXCVAR_SET"));
  CHECK(!Has(cheats_source, "SaveConfig"));
  CHECK(!Has(cheats_source, "kHeroActorVtable"));
  CHECK(!Has(cheats_source, "kApplyHealthEnergy"));
  CHECK(!Has(cheats_source, "sub_821B19B0"));
  CHECK(!Has(cheats_source, "sub_821A8778"));
  CHECK(!Has(cheats_source, "sub_821BAE40"));
  CHECK(Has(cheats_source, "#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)"));
  CHECK(Has(cheats_source, "kLifecycleHealthSentinel"));
  CHECK(Has(cheats_source, "ScaleAward"));
  CHECK(Has(cheats_source, "kMoneyArrayOffset"));
  CHECK(Has(cheats_source, "REXCVAR_GET(mua_cheat_health)"));
  CHECK(Has(cheats_source, "MUA_RUNTIME_MUA1_GOLD"));
  CHECK(Has(cheats_source, "DEBUG_SAULO"));
  CHECK(Has(cheats_source, "CHEATS INIT"));
  CHECK(Has(cheats_source, "CHEATS HOOKS"));
  CHECK(Has(cheats_source, "CHEAT STATE"));
  CHECK(Has(cheats_source, "HERO ACCEPTED"));
  CHECK(Has(cheats_source, "HERO REJECTED"));
  CHECK(Has(cheats_source, "HEALTH BLOCKED"));
  CHECK(Has(cheats_source, "HEALTH LIFECYCLE PASS"));
  CHECK(Has(cheats_source, "HEALTH REFILL"));
  CHECK(Has(cheats_source, "ENERGY BLOCKED"));
  CHECK(Has(cheats_source, "ENERGY REFILL"));
  CHECK(Has(cheats_source, "CHEAT SUMMARY"));
  CHECK(Has(cheats_source, "FINAL CHEAT SUMMARY"));
}

// Each executable is compiled with one guest map, so its hooks and structure offsets must be selected at compile
// time. A copy of one game's addresses in the other game's block would silently write the wrong fields.
void TestGameIsolation() {
  const std::string source = ReadSource("cheats.cpp");

  CHECK(Count(source, "#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)") == 3);
  CHECK(Has(source, "#elif defined(MUA_RUNTIME_MUA1)") == false);

  struct Pair {
    const char* mua1;
    const char* gold;
  };
  const Pair defaults[] = {
      {"#define MUA_CHEAT_SET_HEALTH sub_82146990", "#define MUA_CHEAT_SET_HEALTH sub_82157100"},
      {"#define MUA_CHEAT_SET_ENERGY sub_82138070", "#define MUA_CHEAT_SET_ENERGY sub_82148690"},
      {"#define MUA_CHEAT_RECALCULATE sub_82138748", "#define MUA_CHEAT_RECALCULATE sub_82148C78"},
      {"#define MUA_CHEAT_PARTY_TICK sub_8219F118", "#define MUA_CHEAT_PARTY_TICK sub_821AFD70"},
      {"#define MUA_CHEAT_SET_MONEY sub_821BF660", "#define MUA_CHEAT_SET_MONEY sub_821D0528"},
      {"#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004AA4u",
       "#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004ADCu"},
      {"#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C68u",
       "#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C60u"},
      {"#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007F04u",
       "#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007EFCu"},
      {"#define MUA_CHEAT_ACTOR_STATS 848u", "#define MUA_CHEAT_ACTOR_STATS 856u"},
      {"#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x8410u", "#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x88D0u"},
  };
  for (const Pair& pair : defaults) {
    CHECK(Count(source, pair.mua1) == 1);
    CHECK(Count(source, pair.gold) == 1);
  }

  // MUA2 keeps its own setters, its own offsets and no coin hook at all.
  for (const char* define : {"#define MUA_CHEAT_SET_HEALTH sub_8260BFD0",
                             "#define MUA_CHEAT_SET_ENERGY sub_8260C000",
                             "#define MUA_CHEAT_PARTY_TICK sub_826006E8",
                             "#define MUA_CHEAT_HERO_RESOLVE sub_825B1880",
                             "#define MUA_CHEAT_HEALTH 1316u",
                             "#define MUA_CHEAT_HEALTH_MAX 1324u",
                             "#define MUA_CHEAT_ENERGY 1332u",
                             "#define MUA_CHEAT_ENERGY_MAX 1340u"}) {
    CHECK(Count(source, define) == 1);
  }
  // The raw party table the first attempt compared against must be gone.
  CHECK(!Has(source, "0x82D33840"));
  CHECK(!Has(source, "PruneParty"));
  CHECK(!Has(source, "IsPartyHandle"));
  CHECK(Has(source, "t_in_party_tick"));
  CHECK(Has(source, "CommitPartySnapshot"));
  CHECK(Has(source, "PARTY SNAPSHOT"));
  CHECK(Has(source, "HERO REMOVED"));
  CHECK(Count(source, "#define MUA_CHEAT_PARTY_RESOLVER_LR 0x82600774u") == 1);
  CHECK(Has(source, "IsPartyResolverCaller(caller, MUA_CHEAT_PARTY_RESOLVER_LR)"));
  CHECK(Has(source, "resolver_nested_ignored"));
  CHECK(Has(source, "party_resolver_direct_hits"));
  // Turbo Attack is MUA1-only.
  CHECK(Has(source, "#define MUA_CHEAT_GET_PAD_STATE sub_82467DD0"));
  CHECK(Has(source, "#define MUA_CHEAT_GET_PAD_STATE sub_82480338"));
  CHECK(Has(source, "#define MUA_CHEAT_GET_PAD_STATE sub_829AD238"));
  CHECK(Count(source, "#define MUA_CHEAT_GUEST_GET_PAD_STATE ") == 3);
  CHECK(Count(source, "#define MUA_CHEAT_USES_TURBO_ATTACK 1") == 3);
  CHECK(Count(source, "#define MUA_CHEAT_USES_TURBO_ATTACK 0") == 0);
  CHECK(Has(source, "#define MUA_CHEAT_GET_PAD_STATE sub_82467DD0"));
  CHECK(Count(source, "#define MUA_CHEAT_USES_COIN_MULTIPLIER 1") == 2);
  CHECK(Count(source, "#define MUA_CHEAT_USES_COIN_MULTIPLIER 0") == 1);
  CHECK(Count(source, "#define MUA_CHEAT_USES_STATS_CHAIN 1") == 2);
  CHECK(Count(source, "#define MUA_CHEAT_USES_STATS_CHAIN 0") == 1);

  // Both executables must define the same macro set, and the shared body must select one of the two.
  for (const char* name : {"MUA_CHEAT_RECALCULATE ", "MUA_CHEAT_SET_MONEY "}) {
    const std::string define = std::string("#define ") + name;
    CHECK(Count(source, define.c_str()) == 2);
  }
  for (const char* name : {"MUA_CHEAT_SET_HEALTH ", "MUA_CHEAT_SET_ENERGY ", "MUA_CHEAT_PARTY_TICK "}) {
    const std::string define = std::string("#define ") + name;
    CHECK(Count(source, define.c_str()) == 3);
  }
  CHECK(Count(source, "#define MUA_CHEAT_HERO_RESOLVE ") == 1);
  CHECK(Count(source, "#elif defined(MUA_RUNTIME_MUA1_GOLD)") == 1);
  CHECK(Count(source, "#elif defined(MUA_RUNTIME_MUA2)") == 1);
  CHECK(Has(source, "#endif  // MUA_RUNTIME_MUA1 || MUA_RUNTIME_MUA1_GOLD || MUA_RUNTIME_MUA2"));
}

void TestPartyResolverCaller() {
  using mua::cheats::IsPartyResolverCaller;
  constexpr uint32_t kPartyCallsite = 0x82600774;

  // The party loop's own resolver call is the only one that describes a party slot.
  CHECK(IsPartyResolverCaller(kPartyCallsite, kPartyCallsite));
  // Nested resolver calls made by functions the party tick calls are unrelated lookups.
  CHECK(!IsPartyResolverCaller(kPartyCallsite + 4, kPartyCallsite));
  CHECK(!IsPartyResolverCaller(kPartyCallsite - 4, kPartyCallsite));
  CHECK(!IsPartyResolverCaller(0x82600A00, kPartyCallsite));
  CHECK(!IsPartyResolverCaller(0, kPartyCallsite));
}

void TestPartySnapshot() {
  using mua::cheats::PartySnapshot;
  const uint32_t a = 0x44856BF0;
  const uint32_t b = 0x44856C40;
  const uint32_t c = 0x44856C90;
  const uint32_t d = 0x44856CE0;

  PartySnapshot snapshot;
  CHECK(snapshot.Count() == 0);
  CHECK(!snapshot.Contains(a));

  // A stable party is reported once and then left alone, so it never looks stale.
  CHECK(snapshot.Update(&a, 1));
  CHECK(snapshot.Count() == 1);
  CHECK(snapshot.Contains(a));
  for (int tick = 0; tick < 5; ++tick) {
    CHECK(!snapshot.Update(&a, 1));
    CHECK(snapshot.Count() == 1);
  }

  // Four heroes coexist, and repeated resolver results collapse to one entry.
  const uint32_t four[] = {a, b, c, d};
  CHECK(snapshot.Update(four, 4));
  CHECK(snapshot.Count() == 4);
  CHECK(snapshot.Contains(a) && snapshot.Contains(b) && snapshot.Contains(c) && snapshot.Contains(d));
  const uint32_t duplicates[] = {a, a, b, c, d, a};
  CHECK(!snapshot.Update(duplicates, 6));
  CHECK(snapshot.Count() == 4);

  // An actor missing from the next authoritative snapshot is dropped.
  const uint32_t remaining[] = {b, c, d};
  CHECK(snapshot.Update(remaining, 3));
  CHECK(!snapshot.Contains(a));
  CHECK(snapshot.Contains(b) && snapshot.Contains(c) && snapshot.Contains(d));

  // Null entries are never treated as actors.
  PartySnapshot empty;
  const uint32_t zero[] = {0, 0};
  CHECK(!empty.Update(zero, 2));
  CHECK(empty.Count() == 0);
  CHECK(!empty.Contains(0));

  // A fresh snapshot with null entries plus one actor keeps only the actor.
  const uint32_t mixed[] = {0, c, 0};
  CHECK(empty.Update(mixed, 3));
  CHECK(empty.Count() == 1);
  CHECK(empty.At(0) == c);
}

void TestTurboSession() {
  using mua::cheats::ParseTurboEnabled;
  using mua::cheats::SessionCheats;

  // Old local settings may hold 2 or 4 from the Off/2x/4x choice; anything non-zero is On.
  CHECK(!ParseTurboEnabled(0));
  CHECK(ParseTurboEnabled(1));
  CHECK(ParseTurboEnabled(2));
  CHECK(ParseTurboEnabled(4));

  SessionCheats session;
  CHECK(!session.EffectiveTurbo(false));
  CHECK(session.EffectiveTurbo(true));
  CHECK(session.TurboOverride() == -1);
  session.ToggleTurbo(false);
  CHECK(session.EffectiveTurbo(false));
  CHECK(session.TurboOverride() == 1);
  session.ToggleTurbo(false);
  CHECK(!session.EffectiveTurbo(false));
  CHECK(session.TurboOverride() == -1);
  CHECK(session.EffectiveTurbo(true));

  // Overriding back to the launcher's value drops the override.
  SessionCheats follow;
  follow.SetTurbo(true, false);
  CHECK(!follow.EffectiveTurbo(true));
  follow.ToggleTurbo(true);
  CHECK(follow.EffectiveTurbo(true));
  CHECK(!follow.EffectiveTurbo(false));
}

// Runs the pad through the turbo state at 1 ms steps and counts fresh press edges.
int TurboPressEdges(bool enabled, double seconds, bool gameplay_active = true) {
  mua::cheats::TurboAttackState state;
  constexpr uint16_t held = mua::cheats::kTurboAttackButton;
  bool previous = false;
  int edges = 0;
  for (int step = 0; step <= static_cast<int>(seconds * 1000.0); ++step) {
    const uint16_t out = state.Apply(held, enabled, gameplay_active, step / 1000.0);
    const bool pressed = (out & mua::cheats::kTurboAttackButton) != 0;
    if (pressed && !previous) {
      ++edges;
    }
    previous = pressed;
  }
  return edges;
}

void TestTurboAttack() {
  using mua::cheats::TurboAttackState;
  constexpr uint16_t attack = mua::cheats::kTurboAttackButton;
  constexpr uint16_t jump = 0x2000;

  // Off passes every bit through untouched.
  TurboAttackState off;
  CHECK(off.Apply(attack | jump, false, true, 0.0) == (attack | jump));
  CHECK(off.Apply(jump, false, true, 0.1) == jump);
  CHECK(!off.holding());

  // The first physical press is delivered on the very first poll, not delayed.
  TurboAttackState state;
  CHECK(state.Apply(attack, true, true, 0.0) == attack);
  CHECK(state.holding());
  CHECK(state.pulses() == 1);

  // The synthetic release keeps the other buttons and clears only the attack bit.
  CHECK((state.Apply(attack | jump, true, true, 0.03) & attack) == 0);
  CHECK((state.Apply(attack | jump, true, true, 0.03) & jump) == jump);

  // On keeps the former 4x cadence: a fresh press every 50 ms.
  const int edges = TurboPressEdges(true, 1.0);
  CHECK(edges >= 19 && edges <= 21);

  // Releasing the button stops immediately and returns the pad to its physical state.
  TurboAttackState release;
  release.Apply(attack, true, true, 0.0);
  release.Apply(attack, true, true, 0.03);
  CHECK(release.Apply(jump, true, true, 0.04) == jump);
  CHECK(!release.holding());

  // Turning the cheat off while held returns the physical state.
  TurboAttackState toggle;
  toggle.Apply(attack, true, true, 0.0);
  CHECK((toggle.Apply(attack, true, true, 0.03) & attack) == 0);
  CHECK(toggle.Apply(attack, false, true, 0.04) == attack);
  CHECK(!toggle.holding());

  // Blocked input (F10 open, menus, cutscenes) never synthesizes and clears the sequence.
  TurboAttackState blocked;
  blocked.Apply(attack, true, true, 0.0);
  CHECK(blocked.holding());
  CHECK(blocked.Apply(attack, true, false, 0.03) == attack);
  CHECK(!blocked.holding());
  CHECK(TurboPressEdges(true, 0.3, false) == 1);
}

}  // namespace

int main() {
  TestDefaults();
  TestSessionOverrides();
  TestMultiplier();
  TestAwardScaling();
  TestGuestAddresses();
  TestLauncherPage();
  TestSettingsFlow();
  TestRuntimeSources();
  TestGameIsolation();
  TestPartyResolverCaller();
  TestPartySnapshot();
  TestTurboSession();
  TestTurboAttack();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
