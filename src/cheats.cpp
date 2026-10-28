#include "cheats.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>

#include "settings.h"

#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)

#if defined(MUA_RUNTIME_MUA1)
#define MUA_CHEAT_SET_HEALTH sub_82146990
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_82146990
#define MUA_CHEAT_GUEST_SET_HEALTH 0x82146990u
#define MUA_CHEAT_SET_ENERGY sub_82138070
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_82138070
#define MUA_CHEAT_GUEST_SET_ENERGY 0x82138070u
#define MUA_CHEAT_RECALCULATE sub_82138748
#define MUA_CHEAT_RECALCULATE_ORIGINAL __imp__sub_82138748
#define MUA_CHEAT_GUEST_RECALCULATE 0x82138748u
#define MUA_CHEAT_PARTY_TICK sub_8219F118
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_8219F118
#define MUA_CHEAT_GUEST_PARTY_TICK 0x8219F118u
#define MUA_CHEAT_SET_MONEY sub_821BF660
#define MUA_CHEAT_SET_MONEY_ORIGINAL __imp__sub_821BF660
#define MUA_CHEAT_GUEST_SET_MONEY 0x821BF660u
#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004AA4u
#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C68u
#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007F04u
#define MUA_CHEAT_ACTOR_STATS 848u
#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x8410u
#else
#define MUA_CHEAT_SET_HEALTH sub_82157100
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_82157100
#define MUA_CHEAT_GUEST_SET_HEALTH 0x82157100u
#define MUA_CHEAT_SET_ENERGY sub_82148690
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_82148690
#define MUA_CHEAT_GUEST_SET_ENERGY 0x82148690u
#define MUA_CHEAT_RECALCULATE sub_82148C78
#define MUA_CHEAT_RECALCULATE_ORIGINAL __imp__sub_82148C78
#define MUA_CHEAT_GUEST_RECALCULATE 0x82148C78u
#define MUA_CHEAT_PARTY_TICK sub_821AFD70
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_821AFD70
#define MUA_CHEAT_GUEST_PARTY_TICK 0x821AFD70u
#define MUA_CHEAT_SET_MONEY sub_821D0528
#define MUA_CHEAT_SET_MONEY_ORIGINAL __imp__sub_821D0528
#define MUA_CHEAT_GUEST_SET_MONEY 0x821D0528u
#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004ADCu
#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C60u
#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007EFCu
#define MUA_CHEAT_ACTOR_STATS 856u
#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x88D0u
#endif

#define DEBUG_SAULO(...) REXLOG_INFO("DEBUG_SAULO " __VA_ARGS__)

REX_EXTERN(MUA_CHEAT_SET_HEALTH);
REX_EXTERN(MUA_CHEAT_SET_HEALTH_ORIGINAL);
REX_EXTERN(MUA_CHEAT_SET_ENERGY);
REX_EXTERN(MUA_CHEAT_SET_ENERGY_ORIGINAL);
REX_EXTERN(MUA_CHEAT_RECALCULATE);
REX_EXTERN(MUA_CHEAT_RECALCULATE_ORIGINAL);
REX_EXTERN(MUA_CHEAT_PARTY_TICK);
REX_EXTERN(MUA_CHEAT_PARTY_TICK_ORIGINAL);
REX_EXTERN(MUA_CHEAT_SET_MONEY);
REX_EXTERN(MUA_CHEAT_SET_MONEY_ORIGINAL);

namespace {

namespace cheats = mua::cheats;

constexpr bool kGoldRuntime =
#if defined(MUA_RUNTIME_MUA1_GOLD)
    true;
#else
    false;
#endif

constexpr uint32_t kHealth = 0x264;
constexpr uint32_t kHealthMax = 0x26C;
constexpr uint32_t kEnergy = 0x270;
constexpr uint32_t kEnergyMax = 0x288;
constexpr uint32_t kActorStats = MUA_CHEAT_ACTOR_STATS;
constexpr uint32_t kStatsHero = 288;

constexpr uint32_t kPartyActorVtable = MUA_CHEAT_PARTY_ACTOR_VTABLE;
constexpr uint32_t kPartyStatsVtable = MUA_CHEAT_PARTY_STATS_VTABLE;
constexpr uint32_t kHeroStatsVtable = MUA_CHEAT_HERO_STATS_VTABLE;

constexpr uint32_t kMoneyArrayOffset = MUA_CHEAT_MONEY_ARRAY_OFFSET;
constexpr uint32_t kPlayerSlots = 4;

// The game kills/switches party members with a large negative SetHealth value rather than ordinary damage.
constexpr float kLifecycleHealthSentinel = -100000.0f;

constexpr size_t kMaxHeroes = 8;
constexpr size_t kMaxRejectedLogs = 12;

std::array<std::atomic<uint32_t>, kMaxHeroes> g_heroes{};
std::atomic<bool> g_health_refill_pending{false};
std::atomic<bool> g_energy_refill_pending{false};

struct Counters {
  uint64_t accepted_heroes = 0;
  uint64_t rejected_nonheroes = 0;
  uint64_t health_blocks = 0;
  uint64_t health_lifecycle_passes = 0;
  uint64_t health_refills = 0;
  uint64_t energy_blocks = 0;
  uint64_t energy_refills = 0;
  uint64_t stale_actor_drops = 0;
  uint64_t rejected_logs = 0;
};

Counters g_counters{};
uint32_t g_frame = 0;
uint32_t g_last_summary_frame = 0;
bool g_init_logged = false;

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return __builtin_bswap32(value);
}

float LoadFloat(const uint8_t* base, uint32_t address) {
  const uint32_t bits = Load32(base, address);
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void StoreFloat(uint8_t* base, uint32_t address, float value) {
  uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = __builtin_bswap32(bits);
  std::memcpy(base + address, &bits, sizeof(bits));
}

bool ValidRange(uint32_t address, uint32_t size) {
  return cheats::ValidGuestActor(address) && uint64_t(address) + size <= 0xFFFFF000ull;
}

bool SafeLoad32(const uint8_t* base, uint32_t address, uint32_t& value) {
  if (!ValidRange(address, sizeof(uint32_t))) {
    return false;
  }
  value = Load32(base, address);
  return true;
}

bool SafeLoadFloat(const uint8_t* base, uint32_t address, float& value) {
  if (!ValidRange(address, sizeof(uint32_t))) {
    return false;
  }
  value = LoadFloat(base, address);
  return true;
}

bool OffsetAddress(uint32_t address, uint32_t offset, uint32_t& result) {
  const uint64_t sum = uint64_t(address) + offset;
  if (sum > 0xFFFFFFFFull) {
    return false;
  }
  result = static_cast<uint32_t>(sum);
  return ValidRange(result, sizeof(uint32_t));
}

struct HeroInfo {
  bool hero = false;
  uint32_t actor_vtable = 0;
  uint32_t stats = 0;
  uint32_t stats_vtable = 0;
  uint32_t hero_stats = 0;
  uint32_t hero_stats_vtable = 0;
};

HeroInfo InspectHero(const uint8_t* base, uint32_t actor) {
  HeroInfo info;
  if (!ValidRange(actor, kEnergyMax + 4)) {
    return info;
  }
  info.actor_vtable = Load32(base, actor);
  uint32_t stats_address = 0;
  if (!OffsetAddress(actor, kActorStats, stats_address) ||
      !SafeLoad32(base, stats_address, info.stats) || !ValidRange(info.stats, sizeof(uint32_t))) {
    return info;
  }
  info.stats_vtable = Load32(base, info.stats);
  uint32_t hero_stats_address = 0;
  if (!OffsetAddress(info.stats, kStatsHero, hero_stats_address) ||
      !SafeLoad32(base, hero_stats_address, info.hero_stats) ||
      !ValidRange(info.hero_stats, sizeof(uint32_t))) {
    return info;
  }
  info.hero_stats_vtable = Load32(base, info.hero_stats);
  info.hero =
      info.actor_vtable == kPartyActorVtable && info.stats_vtable == kPartyStatsVtable &&
      info.hero_stats_vtable == kHeroStatsVtable;
  return info;
}

bool IsPartyHero(const uint8_t* base, uint32_t actor) { return InspectHero(base, actor).hero; }

void LogHeroAccepted(const uint8_t* base, uint32_t actor) {
  float health = 0.0f;
  float health_max = 0.0f;
  float energy = 0.0f;
  float energy_max = 0.0f;
  SafeLoadFloat(base, actor + kHealth, health);
  SafeLoadFloat(base, actor + kHealthMax, health_max);
  SafeLoadFloat(base, actor + kEnergy, energy);
  SafeLoadFloat(base, actor + kEnergyMax, energy_max);
  DEBUG_SAULO("HERO ACCEPTED actor=0x{:08X} hero_stats_marker=0x{:08X} health={:.3f}/{:.3f} "
              "energy={:.3f}/{:.3f}",
      actor, kHeroStatsVtable, health, health_max, energy, energy_max);
}

void LogHeroRejected(const uint8_t* base, uint32_t actor, const char* source) {
  ++g_counters.rejected_nonheroes;
  if (g_counters.rejected_logs >= kMaxRejectedLogs) {
    return;
  }
  ++g_counters.rejected_logs;
  const HeroInfo info = InspectHero(base, actor);
  DEBUG_SAULO("HERO REJECTED source={} actor=0x{:08X} actor_vtable=0x{:08X} stats=0x{:08X} "
              "stats_vtable=0x{:08X} hero_stats=0x{:08X} hero_stats_vtable=0x{:08X}",
      source, actor, info.actor_vtable, info.stats, info.stats_vtable, info.hero_stats,
      info.hero_stats_vtable);
}

bool AddHero(uint32_t actor) {
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == actor) {
      return false;
    }
  }
  for (std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == 0) {
      hero.store(actor, std::memory_order_relaxed);
      ++g_counters.accepted_heroes;
      return true;
    }
  }
  return false;
}

void RememberHero(const uint8_t* base, uint32_t actor) {
  for (std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t existing = hero.load(std::memory_order_relaxed);
    if (existing != 0 && !IsPartyHero(base, existing)) {
      hero.store(0, std::memory_order_relaxed);
      ++g_counters.stale_actor_drops;
    }
  }
  if (AddHero(actor)) {
    LogHeroAccepted(base, actor);
  }
}

void RefillPending(uint8_t* base) {
  const bool health = g_health_refill_pending.exchange(false, std::memory_order_relaxed);
  const bool energy = g_energy_refill_pending.exchange(false, std::memory_order_relaxed);
  if (!health && !energy) {
    return;
  }
  for (std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t actor = hero.load(std::memory_order_relaxed);
    if (actor == 0) {
      continue;
    }
    if (!IsPartyHero(base, actor)) {
      hero.store(0, std::memory_order_relaxed);
      ++g_counters.stale_actor_drops;
      continue;
    }
    if (health) {
      float current = 0.0f;
      float max = 0.0f;
      if (SafeLoadFloat(base, actor + kHealth, current) && SafeLoadFloat(base, actor + kHealthMax, max) &&
          std::isfinite(current) && std::isfinite(max) && max > 0.0f && current > 0.0f && current < max) {
        StoreFloat(base, actor + kHealth, max);
        ++g_counters.health_refills;
        DEBUG_SAULO("HEALTH REFILL actor=0x{:08X} old={:.3f} max={:.3f}", actor, current, max);
      }
    }
    if (energy) {
      float health = 0.0f;
      float current = 0.0f;
      float max = 0.0f;
      if (SafeLoadFloat(base, actor + kHealth, health) && health > 0.0f &&
          SafeLoadFloat(base, actor + kEnergy, current) && SafeLoadFloat(base, actor + kEnergyMax, max) &&
          std::isfinite(current) && std::isfinite(max) && max > 0.0f && current >= 0.0f && current < max) {
        StoreFloat(base, actor + kEnergy, max);
        ++g_counters.energy_refills;
        DEBUG_SAULO("ENERGY REFILL actor=0x{:08X} old={:.3f} max={:.3f}", actor, current, max);
      }
    }
  }
}

bool SavedValue(cheats::Cheat cheat) {
  switch (cheat) {
    case cheats::Cheat::Health:
      return REXCVAR_GET(mua_cheat_health);
    case cheats::Cheat::Energy:
      return REXCVAR_GET(mua_cheat_energy);
  }
  return false;
}

cheats::CoinMultiplier SavedMultiplier() {
  return cheats::ParseCoinMultiplier(REXCVAR_GET(mua_cheat_coin_multiplier));
}

cheats::SessionCheats g_session;

const char* OverrideName(int value) {
  return value < 0 ? "follows-saved" : (value == 0 ? "off" : "on");
}

const char* SavedName(bool value) { return value ? "on" : "off"; }

void LogFinalSummary();

void LogSummary() {
  DEBUG_SAULO("CHEAT SUMMARY frames={} accepted_heroes={} rejected_nonheroes={} health_blocks={} "
              "health_lifecycle_passes={} health_refills={} energy_blocks={} energy_refills={} "
              "stale_actor_drops={}",
      g_frame, g_counters.accepted_heroes, g_counters.rejected_nonheroes, g_counters.health_blocks,
      g_counters.health_lifecycle_passes, g_counters.health_refills, g_counters.energy_blocks,
      g_counters.energy_refills, g_counters.stale_actor_drops);
}

void LogInit();

void TickFrame() {
  ++g_frame;
  if (!g_init_logged) {
    LogInit();
  }
  if (g_frame - g_last_summary_frame >= 600) {
    g_last_summary_frame = g_frame;
    LogSummary();
  }
}

void LogInit() {
  if (g_init_logged) {
    return;
  }
  g_init_logged = true;
  const mua::GameDefinition* game = mua::ActiveGame();
  const std::string_view game_key = game ? game->key : std::string_view("none");
  const bool health_saved = SavedValue(cheats::Cheat::Health);
  const bool energy_saved = SavedValue(cheats::Cheat::Energy);
  DEBUG_SAULO("CHEATS INIT runtime={} compile_gold={} health_cvar={} energy_cvar={} "
              "health_override={} energy_override={} health_effective={} energy_effective={} "
              "party_actor_vtable=0x{:08X} stats_vtable=0x{:08X} hero_stats_vtable=0x{:08X} "
              "actor_stats=0x{:03X} money_array=0x{:04X}",
      game_key, int(kGoldRuntime), SavedName(health_saved), SavedName(energy_saved),
      OverrideName(g_session.Override(cheats::Cheat::Health)),
      OverrideName(g_session.Override(cheats::Cheat::Energy)),
      g_session.Effective(cheats::Cheat::Health, health_saved),
      g_session.Effective(cheats::Cheat::Energy, energy_saved), kPartyActorVtable, kPartyStatsVtable,
      kHeroStatsVtable, kActorStats, kMoneyArrayOffset);
  DEBUG_SAULO("CHEATS HOOKS health_set=0x{:08X} host=0x{:016X} energy_set=0x{:08X} host=0x{:016X} "
              "recalculate=0x{:08X} host=0x{:016X} party_tick=0x{:08X} host=0x{:016X} "
              "set_money=0x{:08X} host=0x{:016X}",
      MUA_CHEAT_GUEST_SET_HEALTH, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_HEALTH),
      MUA_CHEAT_GUEST_SET_ENERGY, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_ENERGY),
      MUA_CHEAT_GUEST_RECALCULATE, reinterpret_cast<uintptr_t>(&MUA_CHEAT_RECALCULATE),
      MUA_CHEAT_GUEST_PARTY_TICK, reinterpret_cast<uintptr_t>(&MUA_CHEAT_PARTY_TICK),
      MUA_CHEAT_GUEST_SET_MONEY, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_MONEY));
  if (g_session.Effective(cheats::Cheat::Health, health_saved)) {
    g_health_refill_pending.store(true, std::memory_order_relaxed);
  }
  if (g_session.Effective(cheats::Cheat::Energy, energy_saved)) {
    g_energy_refill_pending.store(true, std::memory_order_relaxed);
  }
  std::atexit(&LogFinalSummary);
}

void LogFinalSummary() {
  DEBUG_SAULO("FINAL CHEAT SUMMARY frames={} accepted_heroes={} rejected_nonheroes={} health_blocks={} "
              "health_lifecycle_passes={} health_refills={} energy_blocks={} energy_refills={} "
              "stale_actor_drops={} health_cvar={} health_override={} health_effective={} "
              "energy_cvar={} energy_override={} energy_effective={}",
      g_frame, g_counters.accepted_heroes, g_counters.rejected_nonheroes, g_counters.health_blocks,
      g_counters.health_lifecycle_passes, g_counters.health_refills, g_counters.energy_blocks,
      g_counters.energy_refills, g_counters.stale_actor_drops, SavedName(SavedValue(cheats::Cheat::Health)),
      OverrideName(g_session.Override(cheats::Cheat::Health)),
      g_session.Effective(cheats::Cheat::Health, SavedValue(cheats::Cheat::Health)),
      SavedName(SavedValue(cheats::Cheat::Energy)), OverrideName(g_session.Override(cheats::Cheat::Energy)),
      g_session.Effective(cheats::Cheat::Energy, SavedValue(cheats::Cheat::Energy)));
  for (size_t i = 0; i < g_heroes.size(); ++i) {
    const uint32_t actor = g_heroes[i].load(std::memory_order_relaxed);
    if (actor == 0) {
      continue;
    }
    DEBUG_SAULO("FINAL HERO index={} actor=0x{:08X}", i, actor);
  }
}

}  // namespace

namespace mua::cheats {

bool Enabled(Cheat cheat) { return g_session.Effective(cheat, SavedValue(cheat)); }

void ToggleForSession(Cheat cheat) {
  const bool saved = SavedValue(cheat);
  const int old_override = g_session.Override(cheat);
  const bool old_effective = g_session.Effective(cheat, saved);
  g_session.Toggle(cheat, saved);
  const bool new_effective = g_session.Effective(cheat, saved);
  if (cheat == Cheat::Health) {
    g_health_refill_pending.store(new_effective && !old_effective, std::memory_order_relaxed);
  } else {
    g_energy_refill_pending.store(new_effective && !old_effective, std::memory_order_relaxed);
  }
  DEBUG_SAULO("CHEAT STATE cheat={} launcher_cvar={} override_before={} override_after={} "
              "effective_before={} effective_after={}",
      cheat == Cheat::Health ? "InfiniteHealth" : "InfiniteEnergy", SavedName(saved),
      OverrideName(old_override), OverrideName(g_session.Override(cheat)), old_effective, new_effective);
}

CoinMultiplier Multiplier() { return g_session.EffectiveMultiplier(SavedMultiplier()); }

void CycleMultiplierForSession() { g_session.CycleMultiplier(SavedMultiplier()); }

}  // namespace mua::cheats

// CActorHero::SetHealth(float). Writes the current value at +0x264 and clamps to +0x26C.
REX_HOOK_RAW(MUA_CHEAT_SET_HEALTH) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const HeroInfo info = InspectHero(base, actor);
  if (info.hero) {
    RememberHero(base, actor);
  } else {
    LogHeroRejected(base, actor, "SetHealth");
  }
  if (cheats::Enabled(cheats::Cheat::Health) && info.hero) {
    float previous = 0.0f;
    float max = 0.0f;
    if (SafeLoadFloat(base, actor + kHealth, previous) && SafeLoadFloat(base, actor + kHealthMax, max) &&
        std::isfinite(previous) && std::isfinite(max) && max > 0.0f &&
        (previous > 0.0f || requested > 0.0f)) {
      if (requested <= kLifecycleHealthSentinel) {
        ++g_counters.health_lifecycle_passes;
        DEBUG_SAULO("HEALTH LIFECYCLE PASS actor=0x{:08X} previous={:.3f} requested={:.3f} caller=0x{:08X}",
            actor, previous, requested, ctx.lr);
      } else if (requested < max) {
        ctx.f1.f64 = max;
        ++g_counters.health_blocks;
        DEBUG_SAULO("HEALTH BLOCKED actor=0x{:08X} previous={:.3f} requested={:.3f} applied={:.3f} "
                    "max={:.3f} caller=0x{:08X}",
            actor, previous, requested, max, max, ctx.lr);
      }
    }
  }
  MUA_CHEAT_SET_HEALTH_ORIGINAL(ctx, base);
}

// CActorHero::SetEnergy(float). Writes the current value at +0x270 and clamps to +0x288.
REX_HOOK_RAW(MUA_CHEAT_SET_ENERGY) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const HeroInfo info = InspectHero(base, actor);
  if (info.hero) {
    RememberHero(base, actor);
  } else {
    LogHeroRejected(base, actor, "SetEnergy");
  }
  if (cheats::Enabled(cheats::Cheat::Energy) && info.hero) {
    float health = 0.0f;
    float previous = 0.0f;
    float max = 0.0f;
    if (SafeLoadFloat(base, actor + kHealth, health) && health > 0.0f &&
        SafeLoadFloat(base, actor + kEnergy, previous) && SafeLoadFloat(base, actor + kEnergyMax, max) &&
        std::isfinite(previous) && std::isfinite(max) && max > 0.0f && previous > 0.0f &&
        requested >= 0.0f && requested < max) {
      ctx.f1.f64 = max;
      ++g_counters.energy_blocks;
      DEBUG_SAULO("ENERGY BLOCKED actor=0x{:08X} previous={:.3f} requested={:.3f} applied={:.3f} "
                  "max={:.3f} caller=0x{:08X}",
          actor, previous, requested, max, max, ctx.lr);
    }
  }
  MUA_CHEAT_SET_ENERGY_ORIGINAL(ctx, base);
}

// Recalculates max health/energy from the character stats; also registers party heroes at spawn.
REX_HOOK_RAW(MUA_CHEAT_RECALCULATE) {
  if (IsPartyHero(base, ctx.r3.u32)) {
    RememberHero(base, ctx.r3.u32);
  }
  MUA_CHEAT_RECALCULATE_ORIGINAL(ctx, base);
}

// The party tick called from the world update. Refill once when a cheat is switched on.
REX_HOOK_RAW(MUA_CHEAT_PARTY_TICK) {
  TickFrame();
  MUA_CHEAT_PARTY_TICK_ORIGINAL(ctx, base);
  RefillPending(base);
}

// CInventorySystem::SetMoney(player, value). The built-in 100,000 coin cheat reads the per-player balance and adds
// 100000 through this setter, so every award arrives here as a positive balance delta.
REX_HOOK_RAW(MUA_CHEAT_SET_MONEY) {
  const cheats::CoinMultiplier multiplier = cheats::Multiplier();
  const uint32_t player = ctx.r4.u32;
  if (static_cast<uint32_t>(multiplier) > 1 && ctx.r3.u32 != 0 && player < kPlayerSlots) {
    const uint32_t address = ctx.r3.u32 + kMoneyArrayOffset + player * 4;
    const int32_t current = static_cast<int32_t>(Load32(base, address));
    const int32_t requested = static_cast<int32_t>(ctx.r5.u32);
    if (requested > current && requested > 0) {
      const uint32_t scaled = cheats::ScaleAward(static_cast<uint32_t>(requested - current), multiplier);
      const int64_t total = int64_t(current) + scaled;
      ctx.r5.u32 = total > std::numeric_limits<int32_t>::max()
                       ? uint32_t(std::numeric_limits<int32_t>::max())
                       : uint32_t(total);
    }
  }
  MUA_CHEAT_SET_MONEY_ORIGINAL(ctx, base);
}

#else

namespace mua::cheats {

bool Enabled(Cheat) { return false; }

void ToggleForSession(Cheat) {}

CoinMultiplier Multiplier() { return CoinMultiplier::Off; }

void CycleMultiplierForSession() {}

}  // namespace mua::cheats

#endif  // MUA_RUNTIME_MUA1 || MUA_RUNTIME_MUA1_GOLD
