#include "zip_overlay.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "updater/third_party/miniz/miniz.h"

namespace mua::zip {
namespace {

uint16_t Get16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}

uint32_t Get32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void Put16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

void Put32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

void Append16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(uint8_t(v));
  out.push_back(uint8_t(v >> 8));
}

void Append32(std::vector<uint8_t>& out, uint32_t v) {
  Append16(out, uint16_t(v));
  Append16(out, uint16_t(v >> 16));
}

char FoldName(char c) {
  if (c == '\\') {
    return '/';
  }
  return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

uint32_t Crc32(std::span<const uint8_t> data) {
  return uint32_t(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
}

std::optional<std::vector<CentralEntry>> ParseCentral(const std::vector<uint8_t>& central, uint16_t expected) {
  std::vector<CentralEntry> entries;
  size_t p = 0;
  while (p + kCentralHeaderSize <= central.size()) {
    const uint8_t* r = central.data() + p;
    if (Get32(r) != kCentralHeaderSignature) {
      return std::nullopt;
    }
    const size_t name_size = Get16(r + 28);
    const size_t record_size = kCentralHeaderSize + name_size + Get16(r + 30) + Get16(r + 32);
    if (p + record_size > central.size()) {
      return std::nullopt;
    }
    CentralEntry entry;
    entry.name.assign(reinterpret_cast<const char*>(r + kCentralHeaderSize), name_size);
    entry.flags = Get16(r + 8);
    entry.method = Get16(r + 10);
    entry.crc = Get32(r + 16);
    entry.compressed_size = Get32(r + 20);
    entry.size = Get32(r + 24);
    entry.local_offset = Get32(r + 42);
    entry.record_offset = p;
    entry.record_size = record_size;
    entries.push_back(std::move(entry));
    p += record_size;
  }
  if (p != central.size() || entries.size() != expected) {
    return std::nullopt;
  }
  return entries;
}

}  // namespace

std::optional<Directory> ReadDirectory(const ReadAt& read, uint64_t file_size) {
  if (file_size < kEndOfCentralDirectorySize) {
    return std::nullopt;
  }
  const uint64_t searched = std::min<uint64_t>(file_size, kEndSearchSpan);
  std::vector<uint8_t> back(static_cast<size_t>(searched));
  const uint64_t back_offset = file_size - searched;
  if (!read(back_offset, back)) {
    return std::nullopt;
  }
  // The last signature whose record fits the file and whose central directory ends right at it; a bare signature
  // after the record (the games' archives end with one) fails that test.
  for (size_t i = back.size() - kEndOfCentralDirectorySize + 1; i-- > 0;) {
    const uint8_t* e = back.data() + i;
    if (Get32(e) != kEndOfCentralDirectorySignature) {
      continue;
    }
    const uint64_t end_offset = back_offset + i;
    const uint16_t disk = Get16(e + 4);
    const uint16_t central_disk = Get16(e + 6);
    const uint16_t disk_entries = Get16(e + 8);
    const uint16_t total_entries = Get16(e + 10);
    const uint32_t central_size = Get32(e + 12);
    const uint32_t central_offset = Get32(e + 16);
    const uint16_t comment = Get16(e + 20);
    if (disk != 0 || central_disk != 0 || disk_entries != total_entries ||
        end_offset + kEndOfCentralDirectorySize + comment > file_size ||
        uint64_t(central_offset) + central_size != end_offset) {
      continue;
    }
    Directory directory;
    directory.file_size = file_size;
    directory.central_offset = central_offset;
    directory.central_size = central_size;
    directory.end_offset = end_offset;
    directory.end.assign(back.begin() + i, back.end());
    directory.central.resize(central_size);
    if (!read(central_offset, directory.central)) {
      return std::nullopt;
    }
    auto entries = ParseCentral(directory.central, total_entries);
    if (!entries) {
      return std::nullopt;
    }
    directory.entries = std::move(*entries);
    return directory;
  }
  return std::nullopt;
}

bool SameName(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return FoldName(x) == FoldName(y); });
}

const CentralEntry* Find(const Directory& directory, std::string_view name) {
  for (const CentralEntry& entry : directory.entries) {
    if (SameName(entry.name, name)) {
      return &entry;
    }
  }
  return nullptr;
}

std::optional<std::vector<uint8_t>> ReadEntry(const ReadAt& read, const CentralEntry& entry) {
  if ((entry.flags & 1) != 0 || (entry.method != 0 && entry.method != 8)) {
    return std::nullopt;
  }
  uint8_t header[kLocalHeaderSize];
  if (!read(entry.local_offset, header) || Get32(header) != kLocalHeaderSignature) {
    return std::nullopt;
  }
  const uint64_t data_offset = uint64_t(entry.local_offset) + kLocalHeaderSize + Get16(header + 26) + Get16(header + 28);
  std::vector<uint8_t> packed(entry.compressed_size);
  if (!read(data_offset, packed)) {
    return std::nullopt;
  }
  std::vector<uint8_t> data;
  if (entry.method == 0) {
    if (entry.compressed_size != entry.size) {
      return std::nullopt;
    }
    data = std::move(packed);
  } else {
    data.resize(entry.size);
    const size_t written = tinfl_decompress_mem_to_mem(data.data(), data.size(), packed.data(), packed.size(),
                                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (written != entry.size) {
      return std::nullopt;
    }
  }
  if (Crc32(data) != entry.crc) {
    return std::nullopt;
  }
  return data;
}

std::optional<Overlay> BuildOverlay(const Directory& directory, const std::vector<Replacement>& replacements) {
  if (directory.end.size() < kEndOfCentralDirectorySize) {
    return std::nullopt;
  }
  struct Placed {
    size_t entry;
    uint32_t local_offset;
    uint32_t crc;
    uint32_t size;
  };
  std::vector<Placed> placed;
  Overlay overlay;
  overlay.prefix_size = directory.central_offset;
  constexpr uint64_t kLimit = std::numeric_limits<uint32_t>::max();
  for (const Replacement& replacement : replacements) {
    const auto it = std::find_if(directory.entries.begin(), directory.entries.end(),
                                 [&](const CentralEntry& entry) { return SameName(entry.name, replacement.name); });
    if (it == directory.entries.end()) {
      return std::nullopt;
    }
    const size_t index = size_t(it - directory.entries.begin());
    if (std::any_of(placed.begin(), placed.end(), [&](const Placed& p) { return p.entry == index; })) {
      return std::nullopt;
    }
    const uint64_t local_offset = overlay.prefix_size + overlay.tail.size();
    if (local_offset > kLimit || replacement.data.size() > kLimit) {
      return std::nullopt;
    }
    const uint8_t* record = directory.central.data() + it->record_offset;
    const uint32_t crc = Crc32(replacement.data);
    const uint32_t size = uint32_t(replacement.data.size());
    // Local header: the central record's version and timestamps, stored data, no data descriptor and no extra field.
    Append32(overlay.tail, kLocalHeaderSignature);
    Append16(overlay.tail, Get16(record + 6));
    Append16(overlay.tail, 0);
    Append16(overlay.tail, 0);
    Append16(overlay.tail, Get16(record + 12));
    Append16(overlay.tail, Get16(record + 14));
    Append32(overlay.tail, crc);
    Append32(overlay.tail, size);
    Append32(overlay.tail, size);
    Append16(overlay.tail, uint16_t(it->name.size()));
    Append16(overlay.tail, 0);
    overlay.tail.insert(overlay.tail.end(), it->name.begin(), it->name.end());
    overlay.tail.insert(overlay.tail.end(), replacement.data.begin(), replacement.data.end());
    placed.push_back({index, uint32_t(local_offset), crc, size});
  }

  const uint64_t central_offset = overlay.prefix_size + overlay.tail.size();
  if (central_offset + directory.central.size() + directory.end.size() > kLimit) {
    return std::nullopt;
  }
  const size_t central_start = overlay.tail.size();
  overlay.tail.insert(overlay.tail.end(), directory.central.begin(), directory.central.end());
  for (const Placed& p : placed) {
    uint8_t* record = overlay.tail.data() + central_start + directory.entries[p.entry].record_offset;
    Put16(record + 8, 0);
    Put16(record + 10, 0);
    Put32(record + 16, p.crc);
    Put32(record + 20, p.size);
    Put32(record + 24, p.size);
    Put32(record + 42, p.local_offset);
  }
  const size_t end_start = overlay.tail.size();
  overlay.tail.insert(overlay.tail.end(), directory.end.begin(), directory.end.end());
  Put32(overlay.tail.data() + end_start + 12, uint32_t(directory.central.size()));
  Put32(overlay.tail.data() + end_start + 16, uint32_t(central_offset));
  overlay.replaced = placed.size();
  return overlay;
}

bool ReadOverlay(const Overlay& overlay, const ReadAt& original, uint64_t offset, std::span<uint8_t> out) {
  if (offset > overlay.size() || out.size() > overlay.size() - offset) {
    return false;
  }
  if (offset < overlay.prefix_size) {
    const size_t head = size_t(std::min<uint64_t>(out.size(), overlay.prefix_size - offset));
    if (!original(offset, out.first(head))) {
      return false;
    }
    out = out.subspan(head);
    offset += head;
  }
  if (!out.empty()) {
    std::memcpy(out.data(), overlay.tail.data() + (offset - overlay.prefix_size), out.size());
  }
  return true;
}

}  // namespace mua::zip
