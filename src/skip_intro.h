#pragma once

#include <cstdint>
#include <string_view>

namespace mua {

// MUA1/Gold intro gate state on its "this": one-shot bits for the legal notice, the ESRB warning and the
// "runscript menus/intro_normal" step.
inline constexpr uint32_t kLegalShownFlagOffset = 40;
inline constexpr uint8_t kLegalShownFlagBit = 0x10;
inline constexpr uint32_t kEsrbShownFlagOffset = 48;
inline constexpr uint8_t kEsrbShownFlagBit = 0x40;
inline constexpr uint32_t kIntroScriptFlagOffset = 40;
inline constexpr uint8_t kIntroScriptFlagBit = 0x40;

// intro_normal ends with openmenu("start").
inline constexpr std::string_view kStartMenuName = "start";

inline constexpr std::string_view kIntroScriptCommand = "runscript menus/intro_normal";
inline constexpr std::string_view kStartMenuCommand = "openmenu start";
inline constexpr uint32_t kBootGateStampOffset = 36;
inline constexpr uint32_t kBootGateSecondsAddress = 0x820009A8;

inline constexpr int32_t kNoPad = -1;
inline constexpr int32_t kMaxPads = 4;
inline constexpr uint32_t kOnlineProfileFlag = 32;

inline bool ShouldSkipBootIntro(bool skip_setting) { return skip_setting; }

inline bool IsLegalMenu(std::string_view menu) { return menu == "legal_360" || menu == "legal_360_uk"; }

inline bool IsIntroScriptCommand(std::string_view command) { return command == kIntroScriptCommand; }

// The boot gate runs once its stamp is kBootGateSecondsAddress's value in the past.
inline float ElapsedBootStamp(float stamp, float gate_seconds) { return stamp - gate_seconds; }

// Startup only: skips movies from the gate call that issues intro_normal until the start menu opens, and never
// restarts afterwards.
class BootIntroSkip {
 public:
  bool skipping_movies() const { return phase_ == Phase::kSkippingMovies; }
  bool before_intro() const { return phase_ == Phase::kIdle; }

  void OnIntroScriptReplaced(bool skip_setting) {
    if (phase_ == Phase::kIdle && ShouldSkipBootIntro(skip_setting)) {
      phase_ = Phase::kSkippingMovies;
    }
  }

  void OnIntroGate(bool skip_setting, uint8_t script_flags_before, uint8_t script_flags_after) {
    const bool issued =
        (script_flags_before & kIntroScriptFlagBit) == 0 && (script_flags_after & kIntroScriptFlagBit) != 0;
    if (phase_ == Phase::kIdle && ShouldSkipBootIntro(skip_setting) && issued) {
      phase_ = Phase::kSkippingMovies;
    }
  }

  void OnOpenMenu(std::string_view menu) {
    if (phase_ == Phase::kSkippingMovies && menu == kStartMenuName) {
      phase_ = Phase::kFinished;
    }
  }

 private:
  enum class Phase { kIdle, kSkippingMovies, kFinished };
  Phase phase_ = Phase::kIdle;
};

}  // namespace mua
