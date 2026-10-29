#include "cheats.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include <rex/hook.h>

#include "guest_map.h"
#include "settings.h"

#if defined(MUA_RUNTIME_MUA1)
#define MUA_CHEAT_SET_HEALTH sub_82146990
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_82146990
#define MUA_CHEAT_SET_ENERGY sub_82138070
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_82138070
#define MUA_CHEAT_RECALCULATE sub_82138748
#define MUA_CHEAT_RECALCULATE_ORIGINAL __imp__sub_82138748
#define MUA_CHEAT_PARTY_TICK sub_8219F118
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_8219F118
#define MUA_CHEAT_SET_MONEY sub_821BF660
#define MUA_CHEAT_SET_MONEY_ORIGINAL __imp__sub_821BF660
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
#define MUA_CHEAT_USES_STATS_CHAIN 1
#define MUA_CHEAT_USES_COIN_MULTIPLIER 1
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#elif defined(MUA_RUNTIME_MUA1_GOLD)
#define MUA_CHEAT_SET_HEALTH sub_82157100
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_82157100
#define MUA_CHEAT_SET_ENERGY sub_82148690
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_82148690
#define MUA_CHEAT_RECALCULATE sub_82148C78
#define MUA_CHEAT_RECALCULATE_ORIGINAL __imp__sub_82148C78
#define MUA_CHEAT_PARTY_TICK sub_821AFD70
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_821AFD70
#define MUA_CHEAT_SET_MONEY sub_821D0528
#define MUA_CHEAT_SET_MONEY_ORIGINAL __imp__sub_821D0528
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
#define MUA_CHEAT_USES_STATS_CHAIN 1
#define MUA_CHEAT_USES_COIN_MULTIPLIER 1
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#elif defined(MUA_RUNTIME_MUA2)
// The vitals live on the character itself: vtable slots 166 and 170 are the setters the game uses for damage and
// power use, and the party keeps its four characters in a fixed handle table the HUD resolves every frame.
#define MUA_CHEAT_SET_HEALTH sub_8260BFD0
#define MUA_CHEAT_SET_HEALTH_ORIGINAL __imp__sub_8260BFD0
#define MUA_CHEAT_SET_ENERGY sub_8260C000
#define MUA_CHEAT_SET_ENERGY_ORIGINAL __imp__sub_8260C000
#define MUA_CHEAT_GUEST_SET_ENERGY 0x8260C000u
#define MUA_CHEAT_PARTY_TICK sub_826006E8
#define MUA_CHEAT_PARTY_TICK_ORIGINAL __imp__sub_826006E8
#define MUA_CHEAT_HERO_RESOLVE sub_825B1880
#define MUA_CHEAT_HERO_RESOLVE_ORIGINAL __imp__sub_825B1880
// Return address of the resolver call inside the four-slot party loop of sub_826006E8 (bl at 0x82600770).
#define MUA_CHEAT_PARTY_RESOLVER_LR 0x82600774u
#define MUA_CHEAT_GET_PAD_STATE sub_829AD238
#define MUA_CHEAT_GET_PAD_STATE_ORIGINAL __imp__sub_829AD238
#define MUA_CHEAT_HEALTH 1316u
#define MUA_CHEAT_HEALTH_MAX 1324u
#define MUA_CHEAT_ENERGY 1332u
#define MUA_CHEAT_ENERGY_MAX 1340u
#define MUA_CHEAT_USES_STATS_CHAIN 0
#define MUA_CHEAT_USES_COIN_MULTIPLIER 0
#define MUA_CHEAT_USES_TURBO_ATTACK 1
#endif

#if defined(MUA_RUNTIME_MUA1) || defined(MUA_RUNTIME_MUA1_GOLD) || defined(MUA_RUNTIME_MUA2)

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

#if MUA_CHEAT_USES_STATS_CHAIN
// The game kills/switches party members with a large negative SetHealth value rather than ordinary damage.
constexpr float kLifecycleHealthSentinel = -100000.0f;
#endif

constexpr size_t kMaxHeroes = 8;

std::array<std::atomic<uint32_t>, kMaxHeroes> g_heroes{};
#if !MUA_CHEAT_USES_STATS_CHAIN
cheats::PartySnapshot g_party;
thread_local std::array<uint32_t, cheats::PartySnapshot::kCapacity> t_seen{};
thread_local size_t t_seen_count = 0;
thread_local bool t_in_party_tick = false;
#endif
std::atomic<bool> g_health_refill_pending{false};
std::atomic<bool> g_energy_refill_pending{false};
std::atomic<bool> g_started{false};
#if MUA_CHEAT_USES_TURBO_ATTACK
cheats::TurboAttackState g_turbo;
#endif
cheats::SessionCheats g_session;

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

bool SavedTurbo() { return cheats::ParseTurboEnabled(REXCVAR_GET(mua_turbo_attack)); }

#if !MUA_CHEAT_USES_STATS_CHAIN
bool FindHero(uint32_t actor) {
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == actor) {
      return true;
    }
  }
  return false;
}

// MUA2 subclasses shift the vitals virtuals by a slot or two, so the setter is matched inside the block that holds
// them rather than at one fixed index.
bool HasCharacterVitals(const uint8_t* base, uint32_t actor) {
  uint32_t vtable = 0;
  if (!SafeLoad32(base, actor, vtable)) {
    return false;
  }
  for (uint32_t slot = kVitalsFirstSlot; slot <= kVitalsLastSlot; ++slot) {
    uint32_t entry_address = 0;
    uint32_t entry = 0;
    if (OffsetAddress(vtable, slot * 4, entry_address) && SafeLoad32(base, entry_address, entry) &&
        entry == MUA_CHEAT_GUEST_SET_ENERGY) {
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

bool IsPartyHero(const uint8_t* base, uint32_t actor) {
  if (!ValidRange(actor, kEnergyMax + 4)) {
    return false;
  }
#if MUA_CHEAT_USES_STATS_CHAIN
  uint32_t stats = 0;
  uint32_t hero_stats = 0;
  uint32_t address = 0;
  if (!OffsetAddress(actor, kActorStats, address) || !SafeLoad32(base, address, stats) ||
      !ValidRange(stats, sizeof(uint32_t)) || !OffsetAddress(stats, kStatsHero, address) ||
      !SafeLoad32(base, address, hero_stats) || !ValidRange(hero_stats, sizeof(uint32_t))) {
    return false;
  }
  return Load32(base, actor) == kPartyActorVtable && Load32(base, stats) == kPartyStatsVtable &&
         Load32(base, hero_stats) == kHeroStatsVtable;
#else
  return FindHero(actor) && HasCharacterVitals(base, actor) && HasReadableVitals(base, actor);
#endif
}

#if MUA_CHEAT_USES_STATS_CHAIN
void AddHero(uint32_t actor) {
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == actor) {
      return;
    }
  }
  for (std::atomic<uint32_t>& hero : g_heroes) {
    if (hero.load(std::memory_order_relaxed) == 0) {
      hero.store(actor, std::memory_order_relaxed);
      return;
    }
  }
}

void RememberHero(const uint8_t* base, uint32_t actor) {
  for (std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t existing = hero.load(std::memory_order_relaxed);
    if (existing != 0 && !IsPartyHero(base, existing)) {
      hero.store(0, std::memory_order_relaxed);
    }
  }
  AddHero(actor);
}
#else
// Replaces the registry with the actors the game itself resolved during this party tick.
void CommitPartySnapshot(const uint8_t* base) {
  uint32_t candidates[cheats::PartySnapshot::kCapacity]{};
  size_t candidate_count = 0;
  for (size_t i = 0; i < t_seen_count && candidate_count < std::size(candidates); ++i) {
    const uint32_t actor = t_seen[i];
    if (ValidRange(actor, kEnergyMax + 4) && HasCharacterVitals(base, actor) && HasReadableVitals(base, actor)) {
      candidates[candidate_count++] = actor;
    }
  }
  if (!g_party.Update(candidates, candidate_count)) {
    return;
  }
  for (size_t i = 0; i < g_heroes.size(); ++i) {
    g_heroes[i].store(i < g_party.Count() ? g_party.At(i) : 0, std::memory_order_relaxed);
  }
}
#endif

bool NeedsRefill(float current, float max, bool allow_empty) {
  return std::isfinite(current) && std::isfinite(max) && max > 0.0f && current < max &&
         (allow_empty ? current >= 0.0f : current > 0.0f);
}

void RefillHero(uint8_t* base, uint32_t actor, bool health, bool energy) {
  float current_health = 0.0f;
  float max_health = 0.0f;
  if (health && SafeLoadFloat(base, actor + kHealth, current_health) &&
      SafeLoadFloat(base, actor + kHealthMax, max_health) && NeedsRefill(current_health, max_health, false)) {
    StoreFloat(base, actor + kHealth, max_health);
  }
  float current_energy = 0.0f;
  float max_energy = 0.0f;
  if (energy && SafeLoadFloat(base, actor + kHealth, current_health) && current_health > 0.0f &&
      SafeLoadFloat(base, actor + kEnergy, current_energy) && SafeLoadFloat(base, actor + kEnergyMax, max_energy) &&
      NeedsRefill(current_energy, max_energy, true)) {
    StoreFloat(base, actor + kEnergy, max_energy);
  }
}

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
      continue;
    }
    RefillHero(base, actor, health, energy);
  }
}
#else
// MUA2 tops every party hero up each frame: a hero that was damaged before it entered the snapshot still gets its
// missing points back, and the cheat starts working on the first tick after it is enabled.
void RefillParty(uint8_t* base) {
  const bool health = cheats::Enabled(cheats::Cheat::Health);
  const bool energy = cheats::Enabled(cheats::Cheat::Energy);
  if (!health && !energy) {
    return;
  }
  for (const std::atomic<uint32_t>& hero : g_heroes) {
    const uint32_t actor = hero.load(std::memory_order_relaxed);
    if (actor != 0) {
      RefillHero(base, actor, health, energy);
    }
  }
}
#endif

// A cheat the launcher saved as on starts filled in on the first party tick.
void StartSession() {
  if (g_started.exchange(true, std::memory_order_relaxed)) {
    return;
  }
  if (cheats::Enabled(cheats::Cheat::Health)) {
    g_health_refill_pending.store(true, std::memory_order_relaxed);
  }
  if (cheats::Enabled(cheats::Cheat::Energy)) {
    g_energy_refill_pending.store(true, std::memory_order_relaxed);
  }
}

}  // namespace

namespace mua::cheats {

bool Enabled(Cheat cheat) { return g_session.Effective(cheat, SavedValue(cheat)); }

void ToggleForSession(Cheat cheat) {
  const bool saved = SavedValue(cheat);
  const bool was_enabled = g_session.Effective(cheat, saved);
  g_session.Toggle(cheat, saved);
  const bool refill = !was_enabled && g_session.Effective(cheat, saved);
  (cheat == Cheat::Health ? g_health_refill_pending : g_energy_refill_pending)
      .store(refill, std::memory_order_relaxed);
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
  g_session.ToggleTurbo(SavedTurbo());
#endif
}

}  // namespace mua::cheats

// CActorHero::SetHealth(float). Writes the current value below the maximum and clamps to it.
REX_HOOK_RAW(MUA_CHEAT_SET_HEALTH) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const bool hero = IsPartyHero(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
  if (hero) {
    RememberHero(base, actor);
  }
#endif
  if (hero && cheats::Enabled(cheats::Cheat::Health)) {
    float previous = 0.0f;
    float max = 0.0f;
    if (SafeLoadFloat(base, actor + kHealth, previous) && SafeLoadFloat(base, actor + kHealthMax, max) &&
        std::isfinite(previous) && std::isfinite(max) && max > 0.0f && (previous > 0.0f || requested > 0.0f)) {
#if MUA_CHEAT_USES_STATS_CHAIN
      const bool lifecycle = requested <= kLifecycleHealthSentinel;
#else
      const bool lifecycle = requested < 0.0f;
#endif
      if (!lifecycle && requested < max) {
        ctx.f1.f64 = max;
      }
    }
  }
  MUA_CHEAT_SET_HEALTH_ORIGINAL(ctx, base);
}

// CActorHero::SetEnergy(float). Writes the current value below the maximum and clamps to it.
REX_HOOK_RAW(MUA_CHEAT_SET_ENERGY) {
  const uint32_t actor = ctx.r3.u32;
  const float requested = static_cast<float>(ctx.f1.f64);
  const bool hero = IsPartyHero(base, actor);
#if MUA_CHEAT_USES_STATS_CHAIN
  if (hero) {
    RememberHero(base, actor);
  }
#endif
  if (hero && cheats::Enabled(cheats::Cheat::Energy)) {
    float health = 0.0f;
    float previous = 0.0f;
    float max = 0.0f;
    if (SafeLoadFloat(base, actor + kHealth, health) && health > 0.0f &&
        SafeLoadFloat(base, actor + kEnergy, previous) && SafeLoadFloat(base, actor + kEnergyMax, max) &&
        std::isfinite(previous) && std::isfinite(max) && max > 0.0f && previous > 0.0f && requested >= 0.0f &&
        requested < max) {
      ctx.f1.f64 = max;
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
  StartSession();
#if MUA_CHEAT_USES_STATS_CHAIN
  MUA_CHEAT_PARTY_TICK_ORIGINAL(ctx, base);
  RefillPending(base);
#else
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
      ctx.r5.u32 = total > std::numeric_limits<int32_t>::max() ? uint32_t(std::numeric_limits<int32_t>::max())
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
  if (!in_party_tick || !cheats::IsPartyResolverCaller(caller, MUA_CHEAT_PARTY_RESOLVER_LR)) {
    return;
  }
  const uint32_t actor = ctx.r3.u32;
  if (!ValidRange(actor, kEnergyMax + 4) || !HasCharacterVitals(base, actor) || !HasReadableVitals(base, actor)) {
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
bool GameplayActive(PPCContext&, uint8_t*) { return g_party.Count() != 0; }
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
  const uint16_t applied = g_turbo.Apply(buttons, turbo, gameplay, NowSeconds());
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
