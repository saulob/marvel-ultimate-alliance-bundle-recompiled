// XMA packet continuity in the patched ReXGlue source: synthetic interleaved streams built from real 2 KB packets are
// walked with the SDK's stream helpers across both input buffers, and the decoder source is checked for the waits,
// resets and loop handling around them.
// Run: mua_xma_packet_continuity_tests

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <rex/audio/xma/helpers.h>

namespace fs = std::filesystem;
namespace xma = rex::audio::xma;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr uint32_t kBytesPerPacket = 2048;
constexpr uint32_t kBitsPerPacket = kBytesPerPacket * 8;
constexpr uint32_t kHeaderBits = 32;
constexpr uint32_t kPayloadBits = kBitsPerPacket - kHeaderBits;
constexpr uint32_t kPacketsPerBuffer = 64;
constexpr uint32_t kFills = 3;
constexpr uint32_t kNoFrame = 0x7FFF;

uint32_t GetBit(const uint8_t* bytes, uint32_t position) {
  return (bytes[position >> 3] >> (7 - (position & 7))) & 1;
}

void SetBit(uint8_t* bytes, uint32_t position, uint32_t bit) {
  const uint8_t mask = uint8_t(0x80 >> (position & 7));
  bytes[position >> 3] = bit ? uint8_t(bytes[position >> 3] | mask) : uint8_t(bytes[position >> 3] & ~mask);
}

void WriteHeader(uint8_t* packet, uint32_t frame_count, uint32_t first_frame_bit, uint32_t skip) {
  const uint32_t value = first_frame_bit == kNoFrame ? kNoFrame : first_frame_bit - kHeaderBits;
  packet[0] = uint8_t(frame_count << 2 | ((value >> 13) & 3));
  packet[1] = uint8_t(value >> 5);
  packet[2] = uint8_t((value & 0x1F) << 3);
  packet[3] = uint8_t(skip);
}

struct Frame {
  uint64_t start = 0;  // bit in the stream's concatenated payloads
  uint32_t size = 0;
};

// Three guest fills of one input buffer each (fill 0 and 2 go to buffer 0, fill 1 to buffer 1), shared by streams
// whose packets interleave in the order `pattern` repeats.
struct Interleaved {
  std::vector<std::vector<uint8_t>> fills;
  std::vector<std::vector<uint32_t>> slots;  // per stream, its global packet indices
  std::vector<std::vector<Frame>> frames;

  uint32_t PacketBit(uint32_t stream, uint64_t bit, uint32_t* slot) const {
    *slot = slots[stream][size_t(bit / kPayloadBits)];
    return kHeaderBits + uint32_t(bit % kPayloadBits);
  }
  uint8_t* Packet(uint32_t slot) { return fills[slot / kPacketsPerBuffer].data() + (slot % kPacketsPerBuffer) * kBytesPerPacket; }
  const uint8_t* Packet(uint32_t slot) const {
    return fills[slot / kPacketsPerBuffer].data() + (slot % kPacketsPerBuffer) * kBytesPerPacket;
  }
  uint32_t TruthBit(uint32_t stream, uint64_t bit) const {
    uint32_t slot;
    const uint32_t position = PacketBit(stream, bit, &slot);
    return GetBit(Packet(slot), position);
  }
};

// split_stream/split_packet: one frame of that stream ends 7 bits before the end of that packet, so the next frame's
// header is split across packets.
Interleaved Build(const std::vector<uint32_t>& pattern, uint32_t streams, uint32_t seed, int split_stream = -1,
                  uint32_t split_packet = 0) {
  Interleaved result;
  result.fills.assign(kFills, std::vector<uint8_t>(kPacketsPerBuffer * kBytesPerPacket, 0));
  result.slots.resize(streams);
  result.frames.resize(streams);
  for (uint32_t slot = 0; slot < kFills * kPacketsPerBuffer; ++slot) {
    result.slots[pattern[slot % pattern.size()]].push_back(slot);
  }
  uint32_t random = seed;
  const auto next_random = [&random] {
    random = random * 1664525u + 1013904223u;
    return random >> 8;
  };
  for (uint32_t stream = 0; stream < streams; ++stream) {
    const uint64_t total = uint64_t(result.slots[stream].size()) * kPayloadBits;
    uint64_t position = 0;
    while (true) {
      uint32_t size = 900 + next_random() % 600;
      const uint32_t packet_index = uint32_t(position / kPayloadBits);
      const uint32_t left_in_packet = kPayloadBits - uint32_t(position % kPayloadBits);
      if (int(stream) == split_stream && packet_index == split_packet && left_in_packet > 607 &&
          left_in_packet < 2600) {
        size = left_in_packet - 7;
      }
      if (position + size > total) {
        break;
      }
      result.frames[stream].push_back({position, size});
      position += size;
    }
    std::vector<Frame>& frames = result.frames[stream];
    for (size_t index = 0; index < frames.size(); ++index) {
      const Frame& frame = frames[index];
      std::vector<uint32_t> bits(frame.size);
      for (uint32_t i = 0; i < 15; ++i) {
        bits[i] = (frame.size >> (14 - i)) & 1;
      }
      for (uint32_t i = 15; i + 1 < frame.size; ++i) {
        bits[i] = next_random() & 1;
      }
      bits[frame.size - 1] = index + 1 < frames.size();
      for (uint32_t i = 0; i < frame.size; ++i) {
        uint32_t slot;
        const uint32_t packet_bit = result.PacketBit(stream, frame.start + i, &slot);
        SetBit(result.Packet(slot), packet_bit, bits[i]);
      }
    }
    for (size_t packet = 0; packet < result.slots[stream].size(); ++packet) {
      uint32_t count = 0;
      uint32_t first = kNoFrame;
      for (const Frame& frame : frames) {
        if (frame.start / kPayloadBits == packet) {
          if (!count) {
            first = kHeaderBits + uint32_t(frame.start % kPayloadBits);
          }
          ++count;
        }
      }
      const uint32_t slot = result.slots[stream][packet];
      const uint32_t skip =
          packet + 1 < result.slots[stream].size() ? result.slots[stream][packet + 1] - slot - 1 : 0;
      WriteHeader(result.Packet(slot), count, first, skip);
    }
  }
  return result;
}

struct WalkResult {
  size_t frames_matched = 0;
  bool mismatch = false;
  uint32_t cross_buffer_frames = 0;
  uint32_t split_headers = 0;
};

// Plays one stream the way XmaContext::Decode moves through the input buffers. legacy follows the stream as the
// decoder did before: a frame crossing into the other buffer takes that buffer's first packet, and so does the stream.
WalkResult Walk(Interleaved& stream_set, uint32_t stream, bool legacy) {
  WalkResult result;
  uint32_t buffer_fill[2] = {0, 1};
  bool valid[2] = {true, true};
  uint32_t next_fill = 2;
  uint8_t current = 0;
  const uint32_t counts[2] = {kPacketsPerBuffer, kPacketsPerBuffer};
  const auto packet_at = [&](uint8_t buffer, uint32_t packet) {
    return stream_set.fills[buffer_fill[buffer]].data() + packet * kBytesPerPacket;
  };
  const auto finish_buffer = [&] {
    valid[current] = false;
    if (next_fill < kFills) {
      buffer_fill[current] = next_fill++;
      valid[current] = true;
    }
    current ^= 1;
  };

  const uint32_t first_slot = stream_set.slots[stream][0];
  uint32_t read_offset = (first_slot % kPacketsPerBuffer) * kBitsPerPacket + kHeaderBits;
  const std::vector<Frame>& truth = stream_set.frames[stream];
  for (size_t guard = 0; result.frames_matched < truth.size() && guard < truth.size() * 2; ++guard) {
    const uint32_t packet_index = read_offset / kBitsPerPacket;
    const uint8_t* packet = packet_at(current, packet_index);
    uint32_t relative = read_offset % kBitsPerPacket;
    relative = std::max(relative, xma::GetPacketFrameOffset(packet));
    const uint32_t next_index = packet_index + xma::GetPacketSkipCount(packet) + 1;

    const uint8_t* next_packet = nullptr;
    bool other_buffer = false;
    if (legacy) {
      other_buffer = next_index >= counts[current];
      next_packet = other_buffer ? packet_at(current ^ 1, 0) : packet_at(current, next_index);
    } else {
      xma::StreamPacket location;
      if (xma::LocateStreamPacket(current, next_index, counts, &location) && valid[location.buffer]) {
        next_packet = packet_at(location.buffer, location.packet);
        other_buffer = location.buffer != current;
      }
    }
    const auto bit = [&](uint32_t offset) {
      const uint32_t position = relative + offset;
      return position < kBitsPerPacket ? GetBit(packet, position)
                                       : GetBit(next_packet, kHeaderBits + position - kBitsPerPacket);
    };
    if (kBitsPerPacket - relative < 15) {
      ++result.split_headers;
    }
    if (!next_packet && kBitsPerPacket - relative < 15) {
      result.mismatch = true;
      return result;
    }
    uint32_t size = 0;
    for (uint32_t i = 0; i < 15; ++i) {
      size = size << 1 | bit(i);
    }
    const Frame& expected = truth[result.frames_matched];
    bool same = size == expected.size && (next_packet || relative + size <= kBitsPerPacket);
    for (uint32_t i = 0; same && i < size; ++i) {
      same = bit(i) == stream_set.TruthBit(stream, expected.start + i);
    }
    if (!same) {
      result.mismatch = true;
      return result;
    }
    ++result.frames_matched;
    const uint32_t end = relative + size;
    if (end > kBitsPerPacket && other_buffer) {
      ++result.cross_buffer_frames;
    }
    if (end < kBitsPerPacket && bit(size - 1)) {
      read_offset = packet_index * kBitsPerPacket + end;
      continue;
    }
    if (legacy) {
      if (next_index >= counts[current]) {
        finish_buffer();
        read_offset = xma::GetPacketFrameOffset(packet_at(current, 0));
      } else {
        read_offset = next_index * kBitsPerPacket + xma::GetPacketFrameOffset(packet_at(current, next_index));
      }
      continue;
    }
    const xma::StreamAdvance advance = xma::AdvanceStream(
        current, next_index, counts, valid,
        [&](uint8_t buffer, uint32_t index) { return xma::GetPacketFrameOffset(packet_at(buffer, index)); });
    for (uint32_t i = 0; i < advance.buffers_finished; ++i) {
      finish_buffer();
    }
    if (!advance.found) {
      break;
    }
    read_offset = advance.read_offset;
  }
  return result;
}

void TestLocateStreamPacket() {
  std::printf("a packet index past the current buffer names a packet of the other buffer\n");
  const uint32_t counts[2] = {64, 64};
  xma::StreamPacket location;
  CHECK(xma::LocateStreamPacket(0, 62, counts, &location) && location.buffer == 0 && location.packet == 62);
  CHECK(xma::LocateStreamPacket(0, 64, counts, &location) && location.buffer == 1 && location.packet == 0);
  CHECK(xma::LocateStreamPacket(0, 65, counts, &location) && location.buffer == 1 && location.packet == 1);
  CHECK(xma::LocateStreamPacket(1, 66, counts, &location) && location.buffer == 0 && location.packet == 2);
  CHECK(!xma::LocateStreamPacket(0, 128, counts, &location));
  const uint32_t uneven[2] = {64, 2};
  CHECK(!xma::LocateStreamPacket(0, 66, uneven, &location));
  CHECK(xma::LocateStreamPacket(1, 3, uneven, &location) && location.buffer == 0 && location.packet == 1);
}

void TestAdvanceStream() {
  std::printf("the stream resumes at the first packet where a frame begins, in either buffer\n");
  const uint32_t counts[2] = {64, 64};
  const bool both[2] = {true, true};
  const auto offsets = [](uint8_t buffer, uint32_t packet) -> uint32_t {
    if (buffer == 0 && packet >= 10 && packet < 12) {
      return kNoFrame + kHeaderBits;
    }
    if (buffer == 0 && packet == 12) {
      return kBitsPerPacket - 4;  // a frame that begins in the last 32 bits
    }
    return 98;
  };
  xma::StreamAdvance advance = xma::AdvanceStream(0, 10, counts, both, offsets);
  CHECK(advance.found && advance.buffers_finished == 0 && advance.read_offset == 12 * kBitsPerPacket + kBitsPerPacket - 4);
  advance = xma::AdvanceStream(0, 65, counts, both, offsets);
  CHECK(advance.found && advance.buffers_finished == 1 && advance.read_offset == 1 * kBitsPerPacket + 98);
  advance = xma::AdvanceStream(1, 64, counts, both, offsets);
  CHECK(advance.found && advance.buffers_finished == 1 && advance.read_offset == 98);

  std::printf("a skip into a buffer the guest has not refilled is kept for when it is\n");
  const bool first_only[2] = {true, false};
  advance = xma::AdvanceStream(0, 66, counts, first_only, offsets);
  CHECK(!advance.found && advance.buffers_finished == 1 && advance.pending_packet == 2);
  advance = xma::AdvanceStream(0, 64, counts, first_only, offsets);
  CHECK(!advance.found && advance.buffers_finished == 1 && advance.pending_packet == 0);
  const bool second_only[2] = {false, true};
  advance = xma::AdvanceStream(1, advance.pending_packet + 2, counts, second_only, offsets);
  CHECK(advance.found && advance.read_offset == 2 * kBitsPerPacket + 98);

  std::printf("no frame anywhere in either buffer finishes both\n");
  const auto none = [](uint8_t, uint32_t) { return kNoFrame + kHeaderBits; };
  advance = xma::AdvanceStream(0, 3, counts, both, none);
  CHECK(!advance.found && advance.buffers_finished == 2);
}

void TestInterleavedStreams() {
  std::printf("four round-robin streams (skip 3) cross from buffer 0 to 1 and back to 0 frame-exact\n");
  {
    Interleaved set = Build({0, 1, 2, 3}, 4, 0x1234, 2, 30);
    uint32_t cross_buffer = 0;
    uint32_t split = 0;
    bool legacy_broke = false;
    for (uint32_t stream = 0; stream < 4; ++stream) {
      const WalkResult walk = Walk(set, stream, false);
      CHECK(!walk.mismatch);
      CHECK(walk.frames_matched == set.frames[stream].size());
      cross_buffer += walk.cross_buffer_frames;
      split += walk.split_headers;
      Interleaved legacy_set = Build({0, 1, 2, 3}, 4, 0x1234, 2, 30);
      legacy_broke = legacy_broke || Walk(legacy_set, stream, true).mismatch;
    }
    CHECK(cross_buffer >= 2);
    CHECK(split >= 1);
    std::printf("  frames crossing buffers: %u, split headers: %u\n", cross_buffer, split);
    std::printf("the old continuation (first packet of the other buffer) decodes another stream's bits\n");
    CHECK(legacy_broke);
  }
  std::printf("uneven interleave (skips 0, 2 and 3) and a single stream (skip 0) stay frame-exact\n");
  for (const uint32_t seed : {7u, 99u, 2024u}) {
    Interleaved set = Build({0, 0, 1, 2}, 3, seed);
    for (uint32_t stream = 0; stream < 3; ++stream) {
      const WalkResult walk = Walk(set, stream, false);
      CHECK(!walk.mismatch && walk.frames_matched == set.frames[stream].size());
    }
    Interleaved single = Build({0}, 1, seed);
    const WalkResult walk = Walk(single, 0, false);
    CHECK(!walk.mismatch && walk.frames_matched == single.frames[0].size());
    Interleaved single_legacy = Build({0}, 1, seed);
    CHECK(!Walk(single_legacy, 0, true).mismatch);
  }
}

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string result = text.str();
  std::erase(result, '\r');
  return result;
}

bool Has(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::string Body(const std::string& text, const std::string& signature) {
  const size_t start = text.find(signature);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = text.find("\n}\n", start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestDecoderSource(const fs::path& source) {
  std::printf("the decoder follows the skip into the other buffer and keeps its state across the switch\n");
  const std::string context = Read(source / "src/audio/xma_context.cpp");
  CHECK(!context.empty());
  const std::string next_packet = Body(context, "const uint8_t* XmaContext::GetNextPacket(");
  CHECK(Has(next_packet, "xma::LocateStreamPacket("));
  CHECK(Has(next_packet, "next.packet * kBytesPerPacket"));
  CHECK(!Has(context, "return memory()->TranslatePhysical(next_buffer_address);"));
  CHECK(!Has(context, "GetNextPacketReadOffset"));
  const std::string advance = Body(context, "void XmaContext::AdvanceToPacket(");
  CHECK(Has(advance, "xma::AdvanceStream("));
  CHECK(Has(advance, "pending_packet_ = next.pending_packet;"));
  for (const std::string& body : {advance, Body(context, "void XmaContext::SwapInputBuffer(")}) {
    CHECK(!Has(body, "ResetDecoderState"));
    CHECK(!Has(body, "avcodec_"));
    CHECK(!Has(body, "carry_valid_"));
  }
  CHECK(Has(Body(context, "void XmaContext::ResetDecoderState("), "pending_packet_ = 0;"));

  std::printf("a continuation in a buffer the guest has not refilled waits instead of erroring or dropping\n");
  const std::string decode = Body(context, "void XmaContext::Decode(");
  CHECK(!decode.empty());
  const size_t wait = decode.find("IsContinuationPending(data, next_packet_index, current_input_packet_count)");
  CHECK(wait != std::string::npos);
  CHECK(decode.find("IsContinuationPending", wait + 1) != std::string::npos);
  const size_t tail_wait = decode.rfind("IsContinuationPending");
  CHECK(tail_wait < decode.find("data->error_status = 4;\n        return;"));
  CHECK(Has(decode, "AdvanceToPacket(data, packet_index + 1);"));
  CHECK(Has(decode, "AdvanceToPacket(data, next_packet_index);"));
  CHECK(Has(decode, "AdvanceToPacket(data, pending_packet);"));

  std::printf("a loop end still jumps to the loop start before any packet advance\n");
  const size_t loop = decode.find("if (is_loop_end_frame) {\n    UpdateLoopStatus(data);\n    return;\n  }");
  CHECK(loop != std::string::npos);
  CHECK(loop < decode.find("AdvanceToPacket(data, next_packet_index);"));

  std::printf("a frame whose header is split across packets is counted in its packet\n");
  const std::string info = Body(context, "kPacketInfo XmaContext::GetPacketInfo(");
  const size_t short_tail = info.find("if (stream.BitsRemaining() < kBitsPerFrameHeader) {");
  CHECK(short_tail != std::string::npos);
  CHECK(info.find("packet_info.frame_count_++;", short_tail) < info.find("const uint64_t frame_size", short_tail));

  std::printf("the decoder is reopened only for a new format or a cleared or released context\n");
  CHECK(Has(Body(context, "int XmaContext::PrepareDecoder("), "av_context_->sample_rate != sample_rate"));
  size_t resets = 0;
  for (size_t at = context.find("ResetDecoderState();"); at != std::string::npos;
       at = context.find("ResetDecoderState();", at + 1)) {
    ++resets;
  }
  CHECK(resets == 2);
}

void TestPatches(const fs::path& patches) {
  std::printf("0137 is a generic patch and 0138 is the temporary audio history\n");
  CHECK(fs::exists(patches / "0137-xma-stream-packet-continuity.patch"));
  const std::string history = Read(patches / "0138-debug-saulo-audio-history.patch");
  CHECK(Has(history, "DEBUG_SAULO"));
  CHECK(Has(history, "DEBUG_SAULO AUDIO SNAPSHOT BEGIN"));
  CHECK(Has(history, "DEBUG_SAULO AUDIO SNAPSHOT END"));
  CHECK(Has(history, "DEBUG_SAULO AUDIO SNAPSHOT POST"));
  CHECK(Has(history, "kCapacity = uint64_t(1) << 16;"));
  const size_t record_start = history.find("+void Record(Event event, uint32_t context, uint32_t a, uint32_t b");
  const std::string record = record_start == std::string::npos
                                 ? std::string()
                                 : history.substr(record_start, history.find("\n+}\n", record_start) - record_start);
  CHECK(!record.empty());
  CHECK(!Has(record, "mutex"));
  CHECK(!Has(record, "lock"));
  CHECK(Has(record, "fetch_add(1, std::memory_order_relaxed)"));
}

void TestF9(const fs::path& src) {
  std::printf("F9 asks for the snapshot without taking the key from the game\n");
  const std::string host = Read(src / "window_host.cpp");
  const size_t f9 = host.find("rex::ui::VirtualKey::kF9");
  CHECK(f9 != std::string::npos);
  const std::string branch = host.substr(f9, host.find("}", f9) - f9);
  CHECK(Has(branch, "rex::audio::history::Dump(\"F9\")"));
  CHECK(Has(branch, "!e.prev_state()"));
  CHECK(!Has(branch, "set_handled"));
}

}  // namespace

int main() {
  TestLocateStreamPacket();
  TestAdvanceStream();
  TestInterleavedStreams();
  const fs::path source = MUA_SDK_SOURCE_DIR;
  if (fs::exists(source / "src/audio/xma_context.cpp")) {
    TestDecoderSource(source);
  } else {
    std::printf("skipped the decoder source checks: %s is not staged\n", source.string().c_str());
  }
  const fs::path src = MUA_SRC_DIR;
  TestPatches(src.parent_path() / "patches/rexglue-sdk");
  TestF9(src);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
