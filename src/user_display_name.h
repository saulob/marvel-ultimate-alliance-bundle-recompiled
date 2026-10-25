#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rex {
namespace system {
namespace xam {

// XamUserGetName, XamUserGetGamerTag and X_USER_SIGNIN_INFO all hold 16 bytes including the NUL.
inline constexpr size_t kUserNameBufferSize = 16;
inline constexpr size_t kUserDisplayNameMaxBytes = kUserNameBufferSize - 1;
inline constexpr uint32_t kLocalUserCount = 4;

// Length of the UTF-8 sequence at the start of text, or 0 when it is malformed, overlong,
// a surrogate or beyond U+10FFFF.
inline size_t Utf8SequenceLength(std::string_view text, uint32_t* code_point) {
  if (text.empty()) {
    return 0;
  }
  const auto byte = [&](size_t i) { return static_cast<uint8_t>(text[i]); };
  size_t length;
  uint32_t value;
  uint32_t minimum;
  if (byte(0) < 0x80) {
    *code_point = byte(0);
    return 1;
  } else if ((byte(0) & 0xE0) == 0xC0) {
    length = 2, value = byte(0) & 0x1F, minimum = 0x80;
  } else if ((byte(0) & 0xF0) == 0xE0) {
    length = 3, value = byte(0) & 0x0F, minimum = 0x800;
  } else if ((byte(0) & 0xF8) == 0xF0) {
    length = 4, value = byte(0) & 0x07, minimum = 0x10000;
  } else {
    return 0;
  }
  if (text.size() < length) {
    return 0;
  }
  for (size_t i = 1; i < length; ++i) {
    if ((byte(i) & 0xC0) != 0x80) {
      return 0;
    }
    value = (value << 6) | (byte(i) & 0x3F);
  }
  if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
    return 0;
  }
  *code_point = value;
  return length;
}

// The longest prefix of text that fits in max_bytes without splitting a UTF-8 sequence.
inline size_t Utf8PrefixLength(std::string_view text, size_t max_bytes) {
  size_t length = 0;
  while (length < text.size()) {
    uint32_t code_point;
    size_t step = Utf8SequenceLength(text.substr(length), &code_point);
    if (!step || length + step > max_bytes) {
      break;
    }
    length += step;
  }
  return length;
}

inline constexpr size_t kUserDisplayNameMaxCharacters = 15;

enum class UserDisplayNameResult { Ok, Empty, BadCharacter, TooManyCharacters, TooManyBytes };

// Checks already-trimmed text: valid UTF-8 without control characters, at most 15 characters and 15 bytes.
inline UserDisplayNameResult MeasureUserDisplayName(std::string_view text) {
  if (text.empty()) {
    return UserDisplayNameResult::Empty;
  }
  size_t characters = 0;
  for (size_t i = 0; i < text.size(); ++characters) {
    uint32_t code_point;
    size_t step = Utf8SequenceLength(text.substr(i), &code_point);
    if (!step || code_point < 0x20 || (code_point >= 0x7F && code_point <= 0x9F)) {
      return UserDisplayNameResult::BadCharacter;
    }
    i += step;
  }
  if (characters > kUserDisplayNameMaxCharacters) {
    return UserDisplayNameResult::TooManyCharacters;
  }
  return text.size() > kUserDisplayNameMaxBytes ? UserDisplayNameResult::TooManyBytes
                                                : UserDisplayNameResult::Ok;
}

inline std::string_view TrimUserDisplayName(std::string_view raw) {
  while (!raw.empty() && raw.front() == ' ') {
    raw.remove_prefix(1);
  }
  while (!raw.empty() && raw.back() == ' ') {
    raw.remove_suffix(1);
  }
  return raw;
}

// Trims spaces and accepts a name that passes MeasureUserDisplayName. Anything else is rejected, never
// truncated; out is only written on success.
inline bool NormalizeUserDisplayName(std::string_view raw, std::string* out) {
  raw = TrimUserDisplayName(raw);
  if (MeasureUserDisplayName(raw) != UserDisplayNameResult::Ok) {
    return false;
  }
  if (out) {
    out->assign(raw);
  }
  return true;
}

// The text after an edit of a name field: the proposed text when it can still become a valid name, otherwise
// the previous text. An empty proposal is allowed so the field can be cleared. Padding spaces are allowed
// while typing and counted, so the stored text never exceeds the limits.
inline std::string AcceptUserDisplayNameEdit(std::string_view previous, std::string_view proposed,
                                             UserDisplayNameResult* result = nullptr) {
  const UserDisplayNameResult measured =
      proposed.empty() ? UserDisplayNameResult::Ok : MeasureUserDisplayName(proposed);
  if (result) {
    *result = measured;
  }
  return std::string(measured == UserDisplayNameResult::Ok ? proposed : previous);
}

}  // namespace xam
}  // namespace system
}  // namespace rex
