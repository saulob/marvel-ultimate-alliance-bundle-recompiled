#include "cheats.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

#include <rex/hook.h>
#include <rex/logging.h>

#include "guest_map.h"
#include "settings.h"

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
#define MUA_CHEAT_HEALTH 0x264u
#define MUA_CHEAT_HEALTH_MAX 0x26Cu
#define MUA_CHEAT_ENERGY 0x270u
#define MUA_CHEAT_ENERGY_MAX 0x288u
#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004AA4u
#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C68u
#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007F04u
#define MUA_CHEAT_ACTOR_STATS 848u
#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x8410u
#define MUA_CHEAT_GET_PAD_STATE sub_82467DD0
#define MUA_CHEAT_GET_PAD_STATE_ORIGINAL __imp__sub_82467DD0
#define MUA_CHEAT_GUEST_GET_PAD_STATE 0x82467DD0u
#define MUA_CHEAT_USES_STATS_CHAIN 1
#define MUA_CHEAT_USES_COIN_MULTIPLIER 1
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#elif defined(MUA_RUNTIME_MUA1_GOLD)
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
#define MUA_CHEAT_HEALTH 0x264u
#define MUA_CHEAT_HEALTH_MAX 0x26Cu
#define MUA_CHEAT_ENERGY 0x270u
#define MUA_CHEAT_ENERGY_MAX 0x288u
#define MUA_CHEAT_PARTY_ACTOR_VTABLE 0x82004ADCu
#define MUA_CHEAT_PARTY_STATS_VTABLE 0x82006C60u
#define MUA_CHEAT_HERO_STATS_VTABLE 0x82007EFCu
#define MUA_CHEAT_ACTOR_STATS 856u
#define MUA_CHEAT_MONEY_ARRAY_OFFSET 0x88D0u
#define MUA_CHEAT_GET_PAD_STATE sub_82480338
#define MUA_CHEAT_GET_PAD_STATE_ORIGINAL __imp__sub_82480338
#define MUA_CHEAT_GUEST_GET_PAD_STATE 0x82480338u
#define MUA_CHEAT_USES_STATS_CHAIN 1
#define MUA_CHEAT_USES_COIN_MULTIPLIER 1
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#elif defined(MUA_RUNTIME_MUA2)
// The vitals live on the character itself: vtable slots 166 and 170 are the setters the game uses for damage and
// power use, and the party keeps its four characters in a fixed handle table the HUD resolves every frame.
#define MUA_CHEAT_SET_HEALTH sub_8260BFD0
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_8260BFD0
#define MUA_CHEAT_GUEST_SET_HEALTH 0x8260BFD0u
#define MUA_CHEAT_SET_ENERGY sub_8260C000
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_8260C000
#define MUA_CHEAT_GUEST_SET_ENERGY 0x8260C000u
#define MUA_CHEAT_PARTY_TICK sub_826006E8
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_826006E8
#define MUA_CHEAT_GUEST_PARTY_TICK 0x826006E8u
#define MUA_CHEAT_HERO_RESOLVE sub_825B1880
#define MUA_CHEAT_HERO_RESOLVE_ORIGINAL __imp__sub_825B1880
#define MUA_CHEAT_GUEST_HERO_RESOLVE 0x825B1880u
// Return address of the resolver call inside the four-slot party loop of sub_826006E8 (bl at 0x82600770).
#define MUA_CHEAT_PARTY_RESOLVER_LR 0x82600774u
#define MUA_CHEAT_GET_PAD_STATE sub_829AD238
#define MUA_CHEAT_GET_PAD_STATE_ORIGINAL __imp__sub_829AD238
#define MUA_CHEAT_GUEST_GET_PAD_STATE 0x829AD238u
#define MUA_CHEAT_HEALTH 1316u
#define MUA_CHEAT_HEALTH_MAX 1324u
#define MUA_CHEAT_ENERGY 1332u
#define MUA_CHEAT_ENERGY_MAX 1340u
#define MUA_CHEAT_USES_STATS_CHAIN 0
#define MUA_CHEAT_USES_COIN_MULTIPLIER 0
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#endif

#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD) || defined(MUA_RUNTIME_MUA2)

#define DEBUG_SAULO(...) REXLOG_INFO("DEBUG_SAULO " __VA_ARGS__)

REX_EXTERN(MUA_CHEAT_SET_HEALTH);
REX_EXTERN(MUA_CHEAT_SET_HEALTH_ORIGINAL);
REX_EXTERN(MUA_CHEAT_SET_ENERGY);
REX_EXTERN(MUA_CHEAT_SET_ENERGY_ORIGINAL);
REX_EXTERN(MUA_CHEAT_PARTY_TICK);
REX_EXTERN(MUA_CHEAT_PARTY_TICK_ORIGINAL);
#if MUA_CHEAT_USES_STATS_CHAIN
REX_EXTERN(MUA_CHEAT_RECALCULATE);
REX_EXTERN(MUA_CHEAT_RECALCULATE_ORIGINAL);
#endif
#if MUA_CHEAT_USES_COIN_MULTIPLIER
REX_EXTERN(MUA_CHEAT_SET_MONEY);
REX_EXTERN(MUA_CHEAT_SET_MONEY_ORIGINAL);
#endif
#if !MUA_CHEAT_USES_STATS_CHAIN
REX_EXTERN(MUA_CHEAT_HERO_RESOLVE);
REX_EXTERN(MUA_CHEAT_HERO_RESOLVE_ORIGINAL);
#endif
#if MUA_CHEAT_USES_TURBO_ATTACK
REX_EXTERN(MUA_CHEAT_GET_PAD_STATE);
REX_EXTERN(MUA_CHEAT_GET_PAD_STATE_ORIGINAL);
#endif
#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)
REX_EXTERN(MUA_GUEST_MENU_MANAGER);
REX_EXTERN(MUA_GUEST_MOVIE_PLAYING);
#endif

namespace {

namespace cheats = mua::cheats;

constexpr bool kGoldRuntime =
#if defined(MUA_RUNTIME_MUA1_GOLD)
    true;
#else
    false;
#endif

constexpr uint32_t kHealth = MUA_CHEAT_HEALTH;
constexpr uint32_t kHealthMax = MUA_CHEAT_HEALTH_MAX;
constexpr uint32_t kEnergy = MUA_CHEAT_ENERGY;
constexpr uint32_t kEnergyMax = MUA_CHEAT_ENERGY_MAX;
#if MUA_CHEAT_USES_STATS_CHAIN
constexpr uint32_t kActorStats = MUA_CHEAT_ACTOR_STATS;
constexpr uint32_t kStatsHero = 288;

constexpr uint32_t kPartyActorVtable = MUA_CHEAT_PARTY_ACTOR_VTABLE;
constexpr uint32_t kPartyStatsVtable = MUA_CHEAT_PARTY_STATS_VTABLE;
constexpr uint32_t kHeroStatsVtable = MUA_CHEAT_HERO_STATS_VTABLE;
#else
constexpr uint32_t kVitalsFirstSlot = 160;
constexpr uint32_t kVitalsLastSlot = 176;
#endif

#if MUA_CHEAT_USES_COIN_MULTIPLIER
constexpr uint32_t kMoneyArrayOffset = MUA_CHEAT_MONEY_ARRAY_OFFSET;
constexpr uint32_t kPlayerSlots = 4;
#endif

// The game kills/switches party members with a large negative SetHealth value rather than ordinary damage.
constexpr float kLifecycleHealthSentinel = -100000.0f;

constexpr size_t kMaxHeroes = 8;
constexpr size_t kMaxRejectedLogs = 12;

std::array<std::atomic<uint32_t>, kMaxHeroes> g_heroes{};
#if !MUA_CHEAT_USES_STATS_CHAIN
cheats::PartySnapshot g_party;
thread_local std::array<uint32_t, cheats::PartySnapshot::kCapacity> t_seen{};
thread_local size_t t_seen_count = 0;
thread_local bool t_in_party_tick = false;
#endif
std::atomic<bool> g_health_refill_pending{false};
std::atomic<bool> g_energy_refill_pending{false};
#if MUA_CHEAT_USES_TURBO_ATTACK
cheats::TurboAttackState g_turbo;
uint64_t g_turbo_starts = 0;
#endif

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
  uint64_t party_tick_hits = 0;
  uint64_t resolver_hits_inside_party_tick = 0;
  uint64_t party_resolver_direct_hits = 0;
  uint64_t resolver_nested_ignored = 0;
  uint64_t party_snapshot_changes = 0;
  uint64_t max_party_snapshot_size = 0;
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

uint16_t Load16(const uint8_t* base, uint32_t address) {
  uint16_t value;
  std::memcpy(&value, base + address, sizeof(value));
  return static_cast<uint16_t>((value >> 8) | (value << 8));
}

void Store16(uint8_t* base, uint32_t address, uint16_t value) {
  const uint16_t swapped = static_cast<uint16_t>((value >> 8) | (value << 8));
  std::memcpy(base + address, &swapped, sizeof(swapped));
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
  uint32_t vitals_marker = 0;
};

#if !MUA_CHEAT_USES_STATS_CHAIN
bool FindHero(uint32_t actor, uint32_t* handle);

// MUA2 subclasses shift the vitals virtuals by a slot or two, so the setter is matched inside the block that holds
// them rather than at one fixed index.
bool HasCharacterVitals(const uint8_t* base, uint32_t actor, uint32_t* marker) {
  uint32_t vtable = 0;
  if (!SafeLoad32(base, actor, vtable)) {
    return false;
  }
  for (uint32_t slot = kVitalsFirstSlot; slot <= kVitalsLastSlot; ++slot) {
    uint32_t entry_address = 0;
    uint32_t entry = 0;
    if (OffsetAddress(vtable, slot * 4, entry_address) && SafeLoad32(base, entry_address, entry) &&
        entry == MUA_CHEAT_GUEST_SET_ENERGY) {
      if (marker != nullptr) {
        *marker = entry;
      }
      return true;
    }
  }
  return false;
}

bool HasReadableVitals(const uint8_t* base, uint32_t actor) {
  float health_max = 0.0f;
  float energy_max = 0.0f;
  return SafeLoadFloat(base, actor + kHealthMax, health_max) &&
         SafeLoadFloat(base, actor + kEnergyMax, energy_max) && std::isfinite(health_max) &&
         std::isfinite(energy_max) && health_max > 0.0f && energy_max >= 0.0f;
}
#endif

HeroInfo InspectHero(const uint8_t* base, uint32_t actor) {
  HeroInfo info;
  if (!ValidRange(actor, kEnergyMax + 4)) {
    return info;
  }
  info.actor_vtable = Load32(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
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
#else
  info.hero = FindHero(actor, nullptr) && HasCharacterVitals(base, actor, &info.vitals_marker) &&
              HasReadableVitals(base, actor);
#endif
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
#if MUA_CHEAT_USES_STATS_CHAIN
  DEBUG_SAULO("HERO ACCEPTED actor=0x{:08X} hero_stats_marker=0x{:08X} health={:.3f}/{:.3f} "
              "energy={:.3f}/{:.3f}",
      actor, kHeroStatsVtable, health, health_max, energy, energy_max);
#else
  DEBUG_SAULO("HERO ACCEPTED actor=0x{:08X} vtable=0x{:08X} health={:.3f}/{:.3f} energy={:.3f}/{:.3f}", actor,
      Load32(base, actor), health, health_max, energy, energy_max);
#endif
}

void LogHeroRejected(const uint8_t* base, uint32_t actor, const char* source) {
  ++g_counters.rejected_nonheroes;
  if (g_counters.rejected_logs >= kMaxRejectedLogs) {
    return;
  }
  ++g_counters.rejected_logs;
  const HeroInfo info = InspectHero(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
  DEBUG_SAULO("HERO REJECTED source={} actor=0x{:08X} actor_vtable=0x{:08X} stats=0x{:08X} "
              "stats_vtable=0x{:08X} hero_stats=0x{:08X} hero_stats_vtable=0x{:08X}",
      source, actor, info.actor_vtable, info.stats, info.stats_vtable, info.hero_stats,
      info.hero_stats_vtable);
#else
  DEBUG_SAULO("HERO REJECTED source={} actor=0x{:08X} actor_vtable=0x{:08X} vitals_marker=0x{:08X}", source,
      actor, info.actor_vtable, info.vitals_marker);
#endif
}

#if MUA_CHEAT_USES_STATS_CHAIN
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

void ForgetHero(size_t index) {
  g_heroes[index].store(0, std::memory_order_relaxed);
  ++g_counters.stale_actor_drops;
}

void RememberHero(const uint8_t* base, uint32_t actor) {
  for (size_t i = 0; i < g_heroes.size(); ++i) {
    const uint32_t existing = g_heroes[i].load(std::memory_order_relaxed);
    if (existing != 0 && !IsPartyHero(base, existing)) {
      ForgetHero(i);
    }
  }
  if (AddHero(actor)) {
    LogHeroAccepted(base, actor);
  }
}
#else
bool FindHero(uint32_t actor, uint32_t* handle) {
  (void)handle;
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == actor) {
      return true;
    }
  }
  return false;
}

void LogPartySnapshot(const uint8_t* base) {
  std::array<char, 256> actors{};
  size_t used = 0;
  for (size_t i = 0; i < g_party.Count() && used + 12 < actors.size(); ++i) {
    const int written = std::snprintf(actors.data() + used, actors.size() - used, " 0x%08X",
        g_party.At(i));
    if (written <= 0) {
      break;
    }
    used += static_cast<size_t>(written);
  }
  actors[used] = '\0';
  DEBUG_SAULO("PARTY SNAPSHOT count={} actors={}", g_party.Count(), actors.data());
  for (size_t i = 0; i < g_party.Count(); ++i) {
    float health = 0.0f;
    float health_max = 0.0f;
    float energy = 0.0f;
    float energy_max = 0.0f;
    const uint32_t actor = g_party.At(i);
    SafeLoadFloat(base, actor + kHealth, health);
    SafeLoadFloat(base, actor + kHealthMax, health_max);
    SafeLoadFloat(base, actor + kEnergy, energy);
    SafeLoadFloat(base, actor + kEnergyMax, energy_max);
    DEBUG_SAULO("PARTY MEMBER actor=0x{:08X} health={:.3f}/{:.3f} energy={:.3f}/{:.3f}", actor, health,
        health_max, energy, energy_max);
  }
}

// Replaces the registry with the actors the game itself resolved during this party tick.
void CommitPartySnapshot(const uint8_t* base) {
  uint32_t candidates[cheats::PartySnapshot::kCapacity]{};
  size_t candidate_count = 0;
  for (size_t i = 0; i < t_seen_count && candidate_count < std::size(candidates); ++i) {
    const uint32_t actor = t_seen[i];
    if (ValidRange(actor, kEnergyMax + 4) && HasCharacterVitals(base, actor, nullptr) &&
        HasReadableVitals(base, actor)) {
      candidates[candidate_count++] = actor;
    }
  }

  cheats::PartySnapshot previous;
  uint32_t previous_actors[cheats::PartySnapshot::kCapacity]{};
  size_t previous_count = 0;
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t actor = hero.load(std::memory_order_relaxed);
    if (actor != 0 && previous_count < std::size(previous_actors)) {
      previous_actors[previous_count++] = actor;
    }
  }
  previous.Update(previous_actors, previous_count);

  if (!g_party.Update(candidates, candidate_count)) {
    return;
  }

  ++g_counters.party_snapshot_changes;
  g_counters.max_party_snapshot_size = std::max<uint64_t>(g_counters.max_party_snapshot_size, g_party.Count());

  for (size_t i = 0; i < g_heroes.size(); ++i) {
    g_heroes[i].store(i < g_party.Count() ? g_party.At(i) : 0, std::memory_order_relaxed);
  }
  for (size_t i = 0; i < g_party.Count(); ++i) {
    const uint32_t actor = g_party.At(i);
    if (!previous.Contains(actor)) {
      ++g_counters.accepted_heroes;
      LogHeroAccepted(base, actor);
    }
  }
  for (size_t i = 0; i < previous.Count(); ++i) {
    const uint32_t actor = previous.At(i);
    if (!g_party.Contains(actor)) {
      ++g_counters.stale_actor_drops;
      DEBUG_SAULO("HERO REMOVED actor=0x{:08X}", actor);
    }
  }
  LogPartySnapshot(base);
}
#endif

#if MUA_CHEAT_USES_STATS_CHAIN
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
#endif

// MUA2 tops every party hero up each frame: a hero that was damaged before it entered the snapshot still gets its
// missing points back, and the cheat starts working on the first tick after it is enabled.
void RefillParty(uint8_t* base) {
#if !MUA_CHEAT_USES_STATS_CHAIN
  const bool health = cheats::Enabled(cheats::Cheat::Health);
  const bool energy = cheats::Enabled(cheats::Cheat::Energy);
  if (!health && !energy) {
    return;
  }
  for (std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t actor = hero.load(std::memory_order_relaxed);
    if (actor == 0) {
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
      float current_health = 0.0f;
      float current = 0.0f;
      float max = 0.0f;
      if (SafeLoadFloat(base, actor + kHealth, current_health) && current_health > 0.0f &&
          SafeLoadFloat(base, actor + kEnergy, current) && SafeLoadFloat(base, actor + kEnergyMax, max) &&
          std::isfinite(current) && std::isfinite(max) && max > 0.0f && current >= 0.0f && current < max) {
        StoreFloat(base, actor + kEnergy, max);
        ++g_counters.energy_refills;
        DEBUG_SAULO("ENERGY REFILL actor=0x{:08X} old={:.3f} max={:.3f}", actor, current, max);
      }
    }
  }
#else
  (void)base;
#endif
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

bool SavedTurbo() { return REXCVAR_GET(mua_turbo_attack) != 0; }

cheats::SessionCheats g_session;

const char* OverrideName(int value) {
  return value < 0 ? "follows-saved" : (value == 0 ? "off" : "on");
}

const char* SavedName(bool value) { return value ? "on" : "off"; }

void LogFinalSummary();

void LogSummary() {
#if MUA_CHEAT_USES_STATS_CHAIN
  DEBUG_SAULO("CHEAT SUMMARY frames={} accepted_heroes={} rejected_nonheroes={} health_blocks={} "
              "health_lifecycle_passes={} health_refills={} energy_blocks={} energy_refills={} "
              "stale_actor_drops={}",
      g_frame, g_counters.accepted_heroes, g_counters.rejected_nonheroes, g_counters.health_blocks,
      g_counters.health_lifecycle_passes, g_counters.health_refills, g_counters.energy_blocks,
      g_counters.energy_refills, g_counters.stale_actor_drops);
#else
  DEBUG_SAULO("CHEAT SUMMARY frames={} party_tick_hits={} resolver_hits_inside_party_tick={} "
              "party_resolver_direct_hits={} resolver_nested_ignored={} party_snapshot_changes={} "
              "max_party_snapshot_size={} party_size={} accepted_heroes={} stale_actor_drops={} "
              "rejected_nonheroes={} health_blocks={} health_lifecycle_passes={} health_refills={} "
              "energy_blocks={} energy_refills={}",
      g_frame, g_counters.party_tick_hits, g_counters.resolver_hits_inside_party_tick,
      g_counters.party_resolver_direct_hits, g_counters.resolver_nested_ignored,
      g_counters.party_snapshot_changes, g_counters.max_party_snapshot_size, g_party.Count(),
      g_counters.accepted_heroes, g_counters.stale_actor_drops, g_counters.rejected_nonheroes,
      g_counters.health_blocks, g_counters.health_lifecycle_passes, g_counters.health_refills,
      g_counters.energy_blocks, g_counters.energy_refills);
#endif
#if MUA_CHEAT_USES_TURBO_ATTACK
  DEBUG_SAULO("TURBO ATTACK SUMMARY starts={} pulses={} mode={}", g_turbo_starts, g_turbo.pulses(),
      cheats::Turbo() ? "on" : "off");
#endif
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
#if MUA_CHEAT_USES_STATS_CHAIN
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
#else
  DEBUG_SAULO("CHEATS INIT runtime={} compile_gold={} health_cvar={} energy_cvar={} "
              "health_override={} energy_override={} health_effective={} energy_effective={} "
              "party_snapshot=resolver-in-party-tick party_resolver_lr=0x{:08X} vitals_slots={}..{} "
              "health=0x{:X}/0x{:X} energy=0x{:X}/0x{:X}",
      game_key, int(kGoldRuntime), SavedName(health_saved), SavedName(energy_saved),
      OverrideName(g_session.Override(cheats::Cheat::Health)),
      OverrideName(g_session.Override(cheats::Cheat::Energy)),
      g_session.Effective(cheats::Cheat::Health, health_saved),
      g_session.Effective(cheats::Cheat::Energy, energy_saved), MUA_CHEAT_PARTY_RESOLVER_LR,
      kVitalsFirstSlot, kVitalsLastSlot, kHealth, kHealthMax, kEnergy, kEnergyMax);
#endif
  DEBUG_SAULO("CHEATS HOOKS health_set=0x{:08X} host=0x{:016X} energy_set=0x{:08X} host=0x{:016X} "
              "recalculate=0x{:08X} host=0x{:016X} party_tick=0x{:08X} host=0x{:016X} "
              "set_money=0x{:08X} host=0x{:016X}",
      MUA_CHEAT_GUEST_SET_HEALTH, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_HEALTH),
      MUA_CHEAT_GUEST_SET_ENERGY, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_ENERGY),
#if MUA_CHEAT_USES_STATS_CHAIN
      MUA_CHEAT_GUEST_RECALCULATE,
#else
      MUA_CHEAT_GUEST_HERO_RESOLVE,
#endif
      reinterpret_cast<uintptr_t>(
#if MUA_CHEAT_USES_STATS_CHAIN
          &MUA_CHEAT_RECALCULATE
#else
          &MUA_CHEAT_HERO_RESOLVE
#endif
          ),
      MUA_CHEAT_GUEST_PARTY_TICK, reinterpret_cast<uintptr_t>(&MUA_CHEAT_PARTY_TICK),
#if MUA_CHEAT_USES_COIN_MULTIPLIER
      MUA_CHEAT_GUEST_SET_MONEY, reinterpret_cast<uintptr_t>(&MUA_CHEAT_SET_MONEY)
#else
      0u, uintptr_t{0}
#endif
  );
#if MUA_CHEAT_USES_TURBO_ATTACK
  DEBUG_SAULO("TURBO ATTACK INIT runtime={} pad_wrapper=0x{:08X} launcher_cvar={} effective={} "
              "attack_button=0x{:04X} half_ms={}",
      game_key, MUA_CHEAT_GUEST_GET_PAD_STATE, SavedTurbo() ? "on" : "off",
      cheats::Turbo() ? "on" : "off", unsigned(cheats::kTurboAttackButton),
      int(cheats::kTurboPulseSeconds * 1000.0));
#endif
  if (g_session.Effective(cheats::Cheat::Health, health_saved)) {
    g_health_refill_pending.store(true, std::memory_order_relaxed);
  }
  if (g_session.Effective(cheats::Cheat::Energy, energy_saved)) {
    g_energy_refill_pending.store(true, std::memory_order_relaxed);
  }
  std::atexit(&LogFinalSummary);
}

void LogFinalSummary() {
#if MUA_CHEAT_USES_STATS_CHAIN
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
#else
  DEBUG_SAULO("FINAL CHEAT SUMMARY frames={} party_tick_hits={} resolver_hits_inside_party_tick={} "
              "party_resolver_direct_hits={} resolver_nested_ignored={} party_snapshot_changes={} "
              "max_party_snapshot_size={} party_size={} accepted_heroes={} stale_actor_drops={} "
              "rejected_nonheroes={} health_blocks={} health_lifecycle_passes={} health_refills={} "
              "energy_blocks={} energy_refills={} health_cvar={} health_override={} health_effective={} "
              "energy_cvar={} energy_override={} energy_effective={}",
      g_frame, g_counters.party_tick_hits, g_counters.resolver_hits_inside_party_tick,
      g_counters.party_resolver_direct_hits, g_counters.resolver_nested_ignored,
      g_counters.party_snapshot_changes, g_counters.max_party_snapshot_size, g_party.Count(),
      g_counters.accepted_heroes, g_counters.stale_actor_drops, g_counters.rejected_nonheroes,
      g_counters.health_blocks, g_counters.health_lifecycle_passes, g_counters.health_refills,
      g_counters.energy_blocks, g_counters.energy_refills, SavedName(SavedValue(cheats::Cheat::Health)),
      OverrideName(g_session.Override(cheats::Cheat::Health)),
      g_session.Effective(cheats::Cheat::Health, SavedValue(cheats::Cheat::Health)),
      SavedName(SavedValue(cheats::Cheat::Energy)), OverrideName(g_session.Override(cheats::Cheat::Energy)),
      g_session.Effective(cheats::Cheat::Energy, SavedValue(cheats::Cheat::Energy)));
#endif
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

bool Turbo() {
#if MUA_CHEAT_USES_TURBO_ATTACK
  return g_session.EffectiveTurbo(SavedTurbo());
#else
  return false;
#endif
}

void ToggleTurboForSession() {
#if MUA_CHEAT_USES_TURBO_ATTACK
  const bool saved = SavedTurbo();
  const int old_override = g_session.TurboOverride();
  const bool old_effective = g_session.EffectiveTurbo(saved);
  g_session.ToggleTurbo(saved);
  DEBUG_SAULO("TURBO ATTACK STATE launcher_cvar={} override_before={} override_after={} "
              "effective_before={} effective_after={} half_ms={}",
      saved ? "on" : "off", OverrideName(old_override), OverrideName(g_session.TurboOverride()),
      old_effective ? "on" : "off", g_session.EffectiveTurbo(saved) ? "on" : "off",
      int(cheats::kTurboPulseSeconds * 1000.0));
#else
  (void)0;
#endif
}

}  // namespace mua::cheats

// CActorHero::SetHealth(float). Writes the current value below the maximum and clamps to it.
REX_HOOK_RAW(MUA_CHEAT_SET_HEALTH) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const HeroInfo info = InspectHero(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
  if (info.hero) {
    RememberHero(base, actor);
  } else {
    LogHeroRejected(base, actor, "SetHealth");
  }
#else
  if (!info.hero) {
    LogHeroRejected(base, actor, "SetHealth");
  }
#endif
  if (cheats::Enabled(cheats::Cheat::Health) && info.hero) {
    float previous = 0.0f;
    float max = 0.0f;
    if (SafeLoadFloat(base, actor + kHealth, previous) && SafeLoadFloat(base, actor + kHealthMax, max) &&
        std::isfinite(previous) && std::isfinite(max) && max > 0.0f &&
        (previous > 0.0f || requested > 0.0f)) {
#if MUA_CHEAT_USES_STATS_CHAIN
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
#else
      if (requested < 0.0f) {
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
#endif
    }
  }
  MUA_CHEAT_SET_HEALTH_ORIGINAL(ctx, base);
}

// CActorHero::SetEnergy(float). Writes the current value below the maximum and clamps to it.
REX_HOOK_RAW(MUA_CHEAT_SET_ENERGY) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const HeroInfo info = InspectHero(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
  if (info.hero) {
    RememberHero(base, actor);
  } else {
    LogHeroRejected(base, actor, "SetEnergy");
  }
#else
  if (!info.hero) {
    LogHeroRejected(base, actor, "SetEnergy");
  }
#endif
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

#if MUA_CHEAT_USES_STATS_CHAIN
// Recalculates max health/energy from the character stats; also registers party heroes at spawn.
REX_HOOK_RAW(MUA_CHEAT_RECALCULATE) {
  if (IsPartyHero(base, ctx.r3.u32)) {
    RememberHero(base, ctx.r3.u32);
  }
  MUA_CHEAT_RECALCULATE_ORIGINAL(ctx, base);
}
#endif

// The party tick called from the world update. Refill once when a cheat is switched on.
REX_HOOK_RAW(MUA_CHEAT_PARTY_TICK) {
  TickFrame();
#if MUA_CHEAT_USES_STATS_CHAIN
  MUA_CHEAT_PARTY_TICK_ORIGINAL(ctx, base);
  RefillPending(base);
#else
  ++g_counters.party_tick_hits;
  t_seen_count = 0;
  t_in_party_tick = true;
  MUA_CHEAT_PARTY_TICK_ORIGINAL(ctx, base);
  t_in_party_tick = false;
  CommitPartySnapshot(base);
  RefillParty(base);
#endif
}

#if MUA_CHEAT_USES_COIN_MULTIPLIER
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
#endif

#if !MUA_CHEAT_USES_STATS_CHAIN
// Resolves a party handle to its character. Only the call the party loop itself makes describes the party; the same
// resolver is used by nested lookups for unrelated objects.
REX_HOOK_RAW(MUA_CHEAT_HERO_RESOLVE) {
  const bool in_party_tick = t_in_party_tick;
  const uint32_t caller = static_cast<uint32_t>(ctx.lr);
  MUA_CHEAT_HERO_RESOLVE_ORIGINAL(ctx, base);
  if (!in_party_tick) {
    return;
  }
  ++g_counters.resolver_hits_inside_party_tick;
  if (!cheats::IsPartyResolverCaller(caller, MUA_CHEAT_PARTY_RESOLVER_LR)) {
    ++g_counters.resolver_nested_ignored;
    return;
  }
  ++g_counters.party_resolver_direct_hits;
  const uint32_t actor = ctx.r3.u32;
  if (!ValidRange(actor, kEnergyMax + 4) || !HasCharacterVitals(base, actor, nullptr) ||
      !HasReadableVitals(base, actor)) {
    return;
  }
  for (size_t i = 0; i < t_seen_count; ++i) {
    if (t_seen[i] == actor) {
      return;
    }
  }
  if (t_seen_count < t_seen.size()) {
    t_seen[t_seen_count++] = actor;
  }
}
#endif

#if MUA_CHEAT_USES_TURBO_ATTACK
double NowSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD)
// MUA1 and Gold play every menu and cutscene as a Scaleform movie, so no movie means gameplay.
bool GameplayActive(PPCContext& parent, uint8_t* base) {
  rex::CallFrame frame(parent);
  frame.ctx.r3.u64 = 0;
  MUA_GUEST_MENU_MANAGER(frame, base);
  const uint32_t manager = frame.ctx.r3.u32;
  if (manager == 0) {
    return false;
  }
  frame.ctx.r3.u64 = manager;
  MUA_GUEST_MOVIE_PLAYING(frame, base);
  return frame.ctx.r3.u8 == 0;
}
#else
// MUA2 has no movie flag; the party HUD pass only fills the snapshot once the game is running, so an empty party
// means the front end or a loading screen.
bool GameplayActive(PPCContext& parent, uint8_t* base) {
  (void)parent;
  (void)base;
  return g_party.Count() != 0;
}
#endif

// The one guest call that reads XamInputGetState. Turbo rewrites the A bit it just delivered, so the game keeps
// deciding whether each press becomes an attack.
REX_HOOK_RAW(MUA_CHEAT_GET_PAD_STATE) {
  const uint32_t state = ctx.r4.u32;
  MUA_CHEAT_GET_PAD_STATE_ORIGINAL(ctx, base);
  if (ctx.r3.u32 != 0 || !ValidRange(state, 8)) {
    return;
  }
  const bool turbo = cheats::Turbo();
  const uint16_t buttons = Load16(base, state + 4);
  const bool physical = (buttons & cheats::kTurboAttackButton) != 0;
  const bool gameplay = physical && turbo ? GameplayActive(ctx, base) : true;
  const bool was_holding = g_turbo.holding();
  const uint32_t pulses_before = g_turbo.pulses();
  const uint16_t applied = g_turbo.Apply(buttons, turbo, gameplay, NowSeconds());
  if (!was_holding && g_turbo.holding()) {
    ++g_turbo_starts;
    DEBUG_SAULO("TURBO ATTACK START mode=on period_ms={} guest_pressed=1",
        int(cheats::kTurboPulseSeconds * 2000.0));
  } else if (was_holding && !g_turbo.holding()) {
    DEBUG_SAULO("TURBO ATTACK STOP mode=off pulses={} guest_pressed={}", g_turbo.pulses(),
        (applied & cheats::kTurboAttackButton) != 0);
  } else if (g_turbo.pulses() != pulses_before &&
             (g_turbo.pulses() <= 3 || g_turbo.pulses() % 50 == 0)) {
    DEBUG_SAULO("TURBO ATTACK PULSE mode=on count={}", g_turbo.pulses());
  }
  if (applied != buttons) {
    Store16(base, state + 4, applied);
  }
}
#endif

#else

namespace mua::cheats {

bool Enabled(Cheat) { return false; }

void ToggleForSession(Cheat) {}

CoinMultiplier Multiplier() { return CoinMultiplier::Off; }

void CycleMultiplierForSession() {}

bool Turbo() { return false; }

void ToggleTurboForSession() {}

}  // namespace mua::cheats

#endif  // MUA_RUNTIME_MUA1 || MUA_RUNTIME_MUA1_GOLD || MUA_RUNTIME_MUA2
