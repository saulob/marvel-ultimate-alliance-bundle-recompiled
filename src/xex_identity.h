#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace mua {

struct XexIdentity {
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint32_t base_version = 0;
  uint8_t disc_number = 0;
  uint8_t disc_count = 0;
  std::string pe_name;
};

// Reads the execution info and original PE name from the start of a XEX2 file. Nullopt when the bytes are not a
// XEX2 header carrying execution info.
std::optional<XexIdentity> ParseXexIdentity(std::span<const uint8_t> header);

}  // namespace mua
