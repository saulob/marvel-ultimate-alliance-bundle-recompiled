#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Replaces ZIP entries without touching the file: the original bytes up to the central directory, then the
// replacements stored, a rewritten central directory and the original end record. The titles' minizip-style reader
// checks each local header against its central record (method 0 or 8, CRC, sizes).
namespace mua::zip {

inline constexpr uint32_t kLocalHeaderSignature = 0x04034b50;
inline constexpr uint32_t kCentralHeaderSignature = 0x02014b50;
inline constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054b50;
inline constexpr size_t kLocalHeaderSize = 30;
inline constexpr size_t kCentralHeaderSize = 46;
inline constexpr size_t kEndOfCentralDirectorySize = 22;
// The end record sits at most a 64 KiB comment before the end of the file; the games' archives add 4 bytes after it.
inline constexpr size_t kEndSearchSpan = kEndOfCentralDirectorySize + 0xFFFF + 64;

struct CentralEntry {
  std::string name;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc = 0;
  uint32_t compressed_size = 0;
  uint32_t size = 0;
  uint32_t local_offset = 0;
  size_t record_offset = 0;
  size_t record_size = 0;
};

struct Directory {
  uint64_t file_size = 0;
  uint32_t central_offset = 0;
  uint32_t central_size = 0;
  uint64_t end_offset = 0;
  std::vector<uint8_t> central;  // the central directory, byte for byte
  std::vector<uint8_t> end;      // the end record and everything after it up to the end of the file
  std::vector<CentralEntry> entries;
};

using ReadAt = std::function<bool(uint64_t offset, std::span<uint8_t> out)>;

std::optional<Directory> ReadDirectory(const ReadAt& read, uint64_t file_size);

// Entry names compare without case and with either slash, as the title's own lookups do.
bool SameName(std::string_view a, std::string_view b);
const CentralEntry* Find(const Directory& directory, std::string_view name);

std::optional<std::vector<uint8_t>> ReadEntry(const ReadAt& read, const CentralEntry& entry);

struct Replacement {
  std::string name;
  std::vector<uint8_t> data;
};

struct Overlay {
  // Bytes [0, prefix_size) are the original file's; the tail follows them.
  uint64_t prefix_size = 0;
  std::vector<uint8_t> tail;
  size_t replaced = 0;

  uint64_t size() const { return prefix_size + tail.size(); }
};

std::optional<Overlay> BuildOverlay(const Directory& directory, const std::vector<Replacement>& replacements);

bool ReadOverlay(const Overlay& overlay, const ReadAt& original, uint64_t offset, std::span<uint8_t> out);

}  // namespace mua::zip
