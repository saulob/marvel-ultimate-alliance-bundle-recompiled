#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mua::cheats {

enum class Cheat : uint8_t { Health, Energy };
inline constexpr size_t kCheatCount = 2;

enum class CoinMultiplier : uint8_t { Off = 0, Two = 2, Five = 5, Ten = 10 };

// The launcher's saved value of a cheat, or the in-game menu's override of it for this session.
// Defined in cheats.cpp.
bool Enabled(Cheat cheat);
void ToggleForSession(Cheat cheat);
CoinMultiplier Multiplier();
void CycleMultiplierForSession();

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

 private:
  static constexpr uint8_t kFollowsSaved = 0;
  static constexpr uint8_t kOff = 1;
  static constexpr uint8_t kOn = 2;
  static constexpr int16_t kFollowsSavedMultiplier = -1;

  static size_t Index(Cheat cheat) { return static_cast<size_t>(cheat); }

  std::array<std::atomic<uint8_t>, kCheatCount> states_{};
  std::atomic<int16_t> multiplier_{kFollowsSavedMultiplier};
};


// Guest addresses below the image and at the top of the 4 GB space cannot hold an actor.
inline bool ValidGuestActor(uint32_t address) { return address >= 0x10000 && address < 0xFFFFF000; }

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
