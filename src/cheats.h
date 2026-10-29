#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mua::cheats {

enum class Cheat : uint8_t { Health, Energy };
inline constexpr size_t kCheatCount = 2;

enum class CoinMultiplier : uint8_t { Off = 0, Two = 2, Five = 5, Ten = 10 };

// XInput's A button, MUA1's basic attack.
inline constexpr uint16_t kTurboAttackButton = 0x1000;

// Half of the turbo press interval: a fresh press every 50 ms while the button stays held.
inline constexpr double kTurboPulseSeconds = 0.025;

// The launcher's saved value of a cheat, or the in-game menu's override of it for this session.
// Defined in cheats.cpp.
bool Enabled(Cheat cheat);
void ToggleForSession(Cheat cheat);
CoinMultiplier Multiplier();
void CycleMultiplierForSession();
bool Turbo();
void ToggleTurboForSession();

inline CoinMultiplier NextCoinMultiplier(CoinMultiplier current) {
  switch (current) {
    case CoinMultiplier::Off:
      return CoinMultiplier::Two;
    case CoinMultiplier::Two:
      return CoinMultiplier::Five;
    case CoinMultiplier::Five:
      return CoinMultiplier::Ten;
    case CoinMultiplier::Ten:
      return CoinMultiplier::Off;
  }
  return CoinMultiplier::Off;
}

inline CoinMultiplier ParseCoinMultiplier(int value) {
  switch (value) {
    case 2:
      return CoinMultiplier::Two;
    case 5:
      return CoinMultiplier::Five;
    case 10:
      return CoinMultiplier::Ten;
    default:
      return CoinMultiplier::Off;
  }
}

// Old local settings may still hold 2 or 4 from the earlier Off/2x/4x choice; any non-zero value means On.
inline bool ParseTurboEnabled(int value) { return value != 0; }

// Turns one held attack button into fresh press edges. The guest still decides whether each press becomes an attack;
// this only rewrites the button bit it sees.
class TurboAttackState {
 public:
  // Returns the buttons the guest should see for this poll. `now` is a monotonic host time in seconds.
  uint16_t Apply(uint16_t buttons, bool enabled, bool gameplay_active, double now) {
    const bool physical = (buttons & kTurboAttackButton) != 0;
    if (!enabled || !gameplay_active || !physical) {
      holding_ = false;
      return buttons;
    }
    if (!holding_) {
      holding_ = true;
      engaged_ = true;
      pulses_ = 1;
      next_edge_ = now + kTurboPulseSeconds;
      return buttons;
    }
    for (int step = 0; step < 4 && now >= next_edge_; ++step) {
      engaged_ = !engaged_;
      next_edge_ += kTurboPulseSeconds;
      if (engaged_) {
        ++pulses_;
      }
    }
    return engaged_ ? buttons : static_cast<uint16_t>(buttons & ~kTurboAttackButton);
  }

  bool holding() const { return holding_; }

  uint32_t pulses() const { return pulses_; }

 private:
  bool holding_ = false;
  bool engaged_ = false;
  double next_edge_ = 0.0;
  uint32_t pulses_ = 0;
};

// What the in-game menu changed. It only records differences from the saved value, so the saved value is
// never altered and a cheat toggled back follows the saved value again.
class SessionCheats {
 public:
  bool Effective(Cheat cheat, bool saved) const {
    const uint8_t state = states_[Index(cheat)].load(std::memory_order_relaxed);
    return state == kFollowsSaved ? saved : state == kOn;
  }

  void Set(Cheat cheat, bool saved, bool enabled) {
    states_[Index(cheat)].store(enabled == saved ? kFollowsSaved : (enabled ? kOn : kOff),
                                std::memory_order_relaxed);
  }

  void Toggle(Cheat cheat, bool saved) { Set(cheat, saved, !Effective(cheat, saved)); }

  // -1 follows the saved value, 0 overridden off, 1 overridden on.
  int Override(Cheat cheat) const {
    const uint8_t state = states_[Index(cheat)].load(std::memory_order_relaxed);
    return state == kFollowsSaved ? -1 : (state == kOff ? 0 : 1);
  }

  CoinMultiplier EffectiveMultiplier(CoinMultiplier saved) const {
    const int16_t value = multiplier_.load(std::memory_order_relaxed);
    return value < 0 ? saved : static_cast<CoinMultiplier>(value);
  }

  void SetMultiplier(CoinMultiplier saved, CoinMultiplier multiplier) {
    multiplier_.store(multiplier == saved ? kFollowsSavedMultiplier : static_cast<int16_t>(multiplier),
                      std::memory_order_relaxed);
  }

  void CycleMultiplier(CoinMultiplier saved) { SetMultiplier(saved, NextCoinMultiplier(EffectiveMultiplier(saved))); }

  bool EffectiveTurbo(bool saved) const {
    const uint8_t state = turbo_.load(std::memory_order_relaxed);
    return state == kFollowsSaved ? saved : state == kOn;
  }

  void SetTurbo(bool saved, bool enabled) {
    turbo_.store(enabled == saved ? kFollowsSaved : (enabled ? kOn : kOff), std::memory_order_relaxed);
  }

  void ToggleTurbo(bool saved) { SetTurbo(saved, !EffectiveTurbo(saved)); }

  int TurboOverride() const {
    const uint8_t state = turbo_.load(std::memory_order_relaxed);
    return state == kFollowsSaved ? -1 : (state == kOff ? 0 : 1);
  }

 private:
  static constexpr uint8_t kFollowsSaved = 0;
  static constexpr uint8_t kOff = 1;
  static constexpr uint8_t kOn = 2;
  static constexpr int16_t kFollowsSavedMultiplier = -1;

  static size_t Index(Cheat cheat) { return static_cast<size_t>(cheat); }

  std::array<std::atomic<uint8_t>, kCheatCount> states_{};
  std::atomic<int16_t> multiplier_{kFollowsSavedMultiplier};
  std::atomic<uint8_t> turbo_{kFollowsSaved};
};


// Guest addresses below the image and at the top of the 4 GB space cannot hold an actor.
inline bool ValidGuestActor(uint32_t address) { return address >= 0x10000 && address < 0xFFFFF000; }

// The party table is filled only by the resolver call the party loop itself makes; the same resolver is also used by
// nested lookups, which describe unrelated objects.
inline bool IsPartyResolverCaller(uint32_t caller, uint32_t party_callsite) {
  return caller == party_callsite;
}

// The party the game reported during its last party tick. Anchoring the set to that one call means an actor stays
// registered for as long as the game keeps handing it out, instead of being re-derived from raw memory each frame.
class PartySnapshot {
 public:
  static constexpr size_t kCapacity = 8;

  // Replaces the set with the unique non-zero actors. Returns whether the set actually changed.
  bool Update(const uint32_t* actors, size_t count) {
    std::array<uint32_t, kCapacity> next{};
    size_t next_count = 0;
    for (size_t i = 0; i < count && next_count < kCapacity; ++i) {
      const uint32_t actor = actors[i];
      if (actor == 0 || ContainsIn(next, next_count, actor)) {
        continue;
      }
      next[next_count++] = actor;
    }
    if (next_count == count_ && std::equal(next.begin(), next.begin() + next_count, actors_.begin())) {
      return false;
    }
    actors_ = next;
    count_ = next_count;
    return true;
  }

  size_t Count() const { return count_; }

  uint32_t At(size_t index) const { return index < count_ ? actors_[index] : 0; }

  bool Contains(uint32_t actor) const { return actor != 0 && ContainsIn(actors_, count_, actor); }

 private:
  static bool ContainsIn(const std::array<uint32_t, kCapacity>& actors, size_t count, uint32_t actor) {
    for (size_t i = 0; i < count; ++i) {
      if (actors[i] == actor) {
        return true;
      }
    }
    return false;
  }

  std::array<uint32_t, kCapacity> actors_{};
  size_t count_ = 0;
};

// A positive award of `award` coins multiplied by the session setting. The existing balance is never passed here.
inline uint32_t ScaleAward(uint32_t award, CoinMultiplier multiplier) {
  const uint32_t factor = static_cast<uint32_t>(multiplier);
  if (factor <= 1 || award == 0) {
    return award;
  }
  const uint64_t scaled = uint64_t(award) * factor;
  return scaled > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                       : static_cast<uint32_t>(scaled);
}

}  // namespace mua::cheats
