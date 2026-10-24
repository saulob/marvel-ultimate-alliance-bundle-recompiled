// The game repeats D3D state sets on every geometry bind and material apply, and the D3D setters mark their state
// dirty every time, so the shader commit (sub_82487620) and the register uploads run again on nearly every draw. A set
// that matches the current state changes nothing else, so its dirty marking is dropped, as SetStreamSource already
// does for an unchanged stride.

#include "d3d_state_filter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "guest_map.h"

#if MUA_GUEST_HAS_D3D_STATE_FILTER

#include "d3d_state_setters_mua1.h"

REXCVAR_DEFINE_BOOL(mua_skip_redundant_vertex_declaration, true, "Marvel Ultimate Alliance",
                    "Drop a vertex declaration set that repeats the current one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_vertex_shader, true, "Marvel Ultimate Alliance",
                    "Drop a vertex shader set that repeats the current one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_pixel_shader, true, "Marvel Ultimate Alliance",
                    "Drop a pixel shader set that repeats the current one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_stream_source, true, "Marvel Ultimate Alliance",
                    "Drop a stream source set that repeats the current buffer, offset and stride")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_render_state, true, "Marvel Ultimate Alliance",
                    "Drop the dirty marking of a render state set that leaves the state as it was")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_texture_state, true, "Marvel Ultimate Alliance",
                    "Drop the dirty marking of a texture or sampler state set that leaves the fetch constant as it was")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_skip_redundant_shader_constants, false, "Marvel Ultimate Alliance",
                    "Mark only the shader constant blocks whose values a constant set actually changes")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_native_register_upload, true, "Marvel Ultimate Alliance",
                    "Copy dirty registers and shader constants into the command buffer natively")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_debug_verify_native_upload, false, "Marvel Ultimate Alliance",
                    "DEBUG_SAULO: compare the first native register uploads with the D3D routine")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(mua_debug_d3d_filter_stats, false, "Marvel Ultimate Alliance",
                    "DEBUG_SAULO: log how many D3D state sets each filter received and dropped")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_REGISTERS));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_ALU_CONSTANTS));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_FETCH_CONSTANTS));
REX_EXTERN(MUA_GUEST_WRITE_PACKETS);
REX_EXTERN(MUA_GUEST_RESERVE_COMMANDS);

namespace {

// The game's D3D device and resource layout.
constexpr uint32_t kVsConstantDirty = 0;
constexpr uint32_t kPsConstantDirty = 8;
constexpr uint32_t kStateDirty = 16;
constexpr uint32_t kStateDirty2 = 24;
constexpr uint32_t kStateDirty3 = 32;
constexpr uint32_t kCommandCursor = 40;
constexpr uint32_t kCommandEnd = 44;
constexpr uint32_t kCommandReserveLimit = 48;
constexpr uint32_t kShaderConstants = 1024;
constexpr uint32_t kFetchConstants = 1024;
constexpr uint32_t kVertexFetchConstants = 1648;
constexpr uint32_t kVsAluConstants = 1792;
constexpr uint32_t kPsAluConstants = 5888;
constexpr uint32_t kCurrentFence = 10780;
constexpr uint32_t kFlags = 10810;
constexpr uint32_t kVertexDeclaration = 11536;
constexpr uint32_t kCommittedStrides = 11552;
constexpr uint32_t kStreamSources = 12188;
constexpr uint32_t kStreamStrides = 12256;
constexpr uint32_t kTextures = 12272;
constexpr uint32_t kPixelShader = 12416;
constexpr uint32_t kVertexShader = 12420;
constexpr std::array<uint32_t, 4> kSamplerBytes = {11652, 11678, 11704, 11730};
constexpr uint32_t kSamplerCount = 26;
constexpr uint32_t kTextureCount = 32;
constexpr uint32_t kStreamCount = 16;
constexpr uint32_t kAluConstantCount = 256;
constexpr uint32_t kFetchConstantBytes = 24;
constexpr uint8_t kFlagClearedByVertexShaderSet = 0x80;
constexpr uint32_t kResourceFence = 8;
constexpr uint32_t kDeclarationBindingId = 48;
constexpr uint32_t kPixelShaderHeader = 40;
constexpr uint32_t kVertexShaderHeader = 872;
constexpr uint32_t kFetchConstantBase = 0x4800;
constexpr uint32_t kNop = 0x80000000;
constexpr uint32_t kFetchFlushHeader = 0x00025000;

const uint8_t* GuestBytes(uint8_t* base, uint32_t address) {
  return rex::memory::GuestPtr<const uint8_t*>(base, address);
}

uint8_t* GuestBytesMut(uint8_t* base, uint32_t address) {
  return rex::memory::GuestPtr(base, address);
}

template <typename T>
T Load(uint8_t* base, uint32_t address) {
  T value{};
  std::memcpy(&value, GuestBytes(base, address), sizeof(value));
  return std::byteswap(value);
}

uint32_t Load32(uint8_t* base, uint32_t address) {
  return Load<uint32_t>(base, address);
}

uint64_t Load64(uint8_t* base, uint32_t address) {
  return Load<uint64_t>(base, address);
}

uint8_t Load8(uint8_t* base, uint32_t address) {
  return *GuestBytes(base, address);
}

template <typename T>
void Store(uint8_t* base, uint32_t address, T value) {
  value = std::byteswap(value);
  std::memcpy(GuestBytesMut(base, address), &value, sizeof(value));
}

void Store32(uint8_t* base, uint32_t address, uint32_t value) {
  Store<uint32_t>(base, address, value);
}

void Store64(uint8_t* base, uint32_t address, uint64_t value) {
  Store<uint64_t>(base, address, value);
}

void CopyGuest(uint8_t* base, uint32_t dest, uint32_t source, uint32_t size) {
  std::memcpy(GuestBytesMut(base, dest), GuestBytes(base, source), size);
}

bool GuestEqual(uint8_t* base, uint32_t a, uint32_t b, uint32_t size) {
  return std::memcmp(GuestBytes(base, a), GuestBytes(base, b), size) == 0;
}

// DEBUG_SAULO: how many calls each filter received and how many it found redundant, one log line per 10 s. Off by
// default; the counters are plain loads and stores of the render thread's own values.
enum Group : size_t {
  kDeclaration,
  kVertexShaderGroup,
  kPixelShaderGroup,
  kStreamSource,
  kRenderState,
  kSamplerState,
  kTexture,
  kConstantGroup,
  kGroupCount,
};

constexpr std::array<const char*, kGroupCount> kGroupNames = {
    "decl", "vs", "ps", "stream", "render state", "sampler", "texture", "constants",
};

struct GroupCounters {
  std::atomic<uint64_t> received{0};
  std::atomic<uint64_t> skipped{0};
};

std::array<GroupCounters, kGroupCount> g_groups;
std::atomic<uint64_t> g_constant_blocks{0};
std::atomic<uint64_t> g_constant_blocks_marked{0};
std::atomic<uint64_t> g_upload_splits{0};
int64_t g_window_start_ns = 0;
int g_reports = 0;
uint32_t g_calls_since_check = 0;
constexpr int kMaxReports = 90;

bool StatsEnabled() {
  return REXCVAR_GET(mua_debug_d3d_filter_stats);
}

void Bump(std::atomic<uint64_t>& counter, uint64_t amount = 1) {
  counter.store(counter.load(std::memory_order_relaxed) + amount, std::memory_order_relaxed);
}

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void MaybeReport() {
  const int64_t now = NowNs();
  if (g_window_start_ns == 0) {
    g_window_start_ns = now;
    return;
  }
  if (now - g_window_start_ns < 10'000'000'000 || g_reports >= kMaxReports) {
    return;
  }
  const double seconds = (now - g_window_start_ns) / 1e9;
  g_window_start_ns = now;
  std::string text;
  for (size_t i = 0; i < kGroupCount; ++i) {
    const uint64_t received = g_groups[i].received.exchange(0, std::memory_order_relaxed);
    const uint64_t skipped = g_groups[i].skipped.exchange(0, std::memory_order_relaxed);
    if (!received) {
      continue;
    }
    text += fmt::format("{}{} {:.0f}/s skip {:.0f}/s pass {:.0f}/s ({:.0f}%)", text.empty() ? "" : " | ",
                        kGroupNames[i], received / seconds, skipped / seconds, (received - skipped) / seconds,
                        100.0 * double(skipped) / double(received));
  }
  const uint64_t blocks = g_constant_blocks.exchange(0, std::memory_order_relaxed);
  const uint64_t marked = g_constant_blocks_marked.exchange(0, std::memory_order_relaxed);
  if (blocks) {
    text += fmt::format(" | constant blocks {:.0f}/s marked {:.0f}%", blocks / seconds, 100.0 * double(marked) /
                                                                                            double(blocks));
  }
  const uint64_t splits = g_upload_splits.exchange(0, std::memory_order_relaxed);
  if (splits) {
    text += fmt::format(" | upload splits {:.0f}/s", splits / seconds);
  }
  ++g_reports;
  REXLOG_INFO("DEBUG_SAULO D3D filters: {}", text.empty() ? "none" : text);
}

[[clang::noinline]] void CountEnabled(Group group, bool skipped) {
  Bump(g_groups[group].received);
  if (skipped) {
    Bump(g_groups[group].skipped);
  }
  if (++g_calls_since_check % 8192 == 0) {
    MaybeReport();
  }
}

void Count(Group group, bool skipped) {
  if (StatsEnabled()) [[unlikely]] {
    CountEnabled(group, skipped);
  }
}

// A set of the bound object still records the fence it stays busy until, so a lock or free of a resource that stays
// bound waits for its latest use. Without a current fence the setter queues a fixup instead, which only it can do.
bool RefreshBoundFence(uint8_t* base, uint32_t device, uint32_t object) {
  if (!object) {
    return true;
  }
  const uint32_t fence = Load32(base, device + kCurrentFence);
  if (!fence) {
    return false;
  }
  Store32(base, object + kResourceFence, fence);
  return true;
}

// Whether setting the shader again would leave the constant shadow as it is. Its literal table holds entries the
// commit loads from the shader itself, then runs copied whole, then (mask, value) pairs merged into single dwords.
bool LiteralsApplied(uint8_t* base, uint32_t device, uint32_t header) {
  const uint32_t table_offset = Load32(base, header + 20);
  if (!table_offset) {
    return true;
  }
  const uint32_t table = header + table_offset;
  const uint32_t shadow = device + kShaderConstants;
  uint32_t cursor = table + 32;
  const uint32_t end = cursor + Load32(base, table + 24);
  while (cursor < end) {
    const uint16_t count = Load<uint16_t>(base, cursor + 2);
    cursor += 4;
    if (!count) {
      break;
    }
    cursor += 4;
  }
  while (cursor < end) {
    const uint32_t bytes = uint32_t(Load<uint16_t>(base, cursor + 2)) * 4;
    const uint32_t target = shadow + Load<uint16_t>(base, cursor);
    cursor += 4;
    if (!bytes) {
      break;
    }
    if (!GuestEqual(base, target, cursor, bytes)) {
      return false;
    }
    cursor += bytes;
  }
  while (cursor < end) {
    uint16_t count = Load<uint16_t>(base, cursor + 2);
    uint32_t target = shadow + Load<uint16_t>(base, cursor);
    cursor += 4;
    if (!count) {
      break;
    }
    if (count & 1) {
      return false;
    }
    for (; count; count -= 2, cursor += 8, target += 4) {
      const uint32_t current = Load32(base, target);
      if (((current & Load32(base, cursor)) | Load32(base, cursor + 4)) != current) {
        return false;
      }
    }
  }
  return true;
}

bool ShaderIsCurrent(uint8_t* base, uint32_t device, uint32_t slot, uint32_t shader, uint32_t header) {
  return Load32(base, device + slot) == shader && (!shader || LiteralsApplied(base, device, shader + header)) &&
         RefreshBoundFence(base, device, shader);
}

// The fetch constant the setter would build from the buffer's own one, including its host page adjustment for
// addresses at or above 0xE0000000.
bool StreamSourceIsCurrent(uint8_t* base, uint32_t device, uint32_t stream, uint32_t buffer, uint32_t offset,
                           uint32_t stride) {
  if (stream >= kStreamCount || Load32(base, device + kStreamSources + stream * 4) != buffer) {
    return false;
  }
  const uint32_t stride_dwords = stride >> 2;
  if (Load8(base, device + kStreamStrides + stream) != uint8_t(stride_dwords) ||
      (stride_dwords && Load8(base, device + kCommittedStrides + stream) != stride_dwords)) {
    return false;
  }
  if (buffer) {
    const uint32_t fetch = device + kVertexFetchConstants + (17 - stream) * 8;
    const uint32_t address = Load32(base, buffer + 24) + offset;
    const uint32_t host_address = (((address >> 20) + 512) & 0x1000) + (address & 0x1FFFFFFF);
    if (Load32(base, fetch) != host_address || Load32(base, fetch + 4) != Load32(base, buffer + 28) - offset) {
      return false;
    }
  }
  return RefreshBoundFence(base, device, buffer);
}

struct Field {
  uint32_t offset;
  uint32_t size;
};

#define MUA_FIELD(offset, size) Field{offset, size}

uint32_t LoadField(uint8_t* base, uint32_t address, uint32_t size) {
  return size == 1 ? Load8(base, address) : Load32(base, address);
}

struct DirtyWords {
  uint64_t words[3];
};

DirtyWords LoadDirty(uint8_t* base, uint32_t device) {
  return {{Load64(base, device + kStateDirty), Load64(base, device + kStateDirty2), Load64(base, device + kStateDirty3)}};
}

void RestoreDirty(uint8_t* base, uint32_t device, const DirtyWords& dirty) {
  Store64(base, device + kStateDirty, dirty.words[0]);
  Store64(base, device + kStateDirty2, dirty.words[1]);
  Store64(base, device + kStateDirty3, dirty.words[2]);
}

// The setter's only effects are its fields and the dirty masks (checked against the generated code), so when every
// field comes out as it was, restoring the masks leaves the device exactly as if it had not been called.
void RunLeafSetter(PPCContext& ctx, uint8_t* base, PPCFunc* original, std::initializer_list<Field> fields) {
  if (!REXCVAR_GET(mua_skip_redundant_render_state)) {
    original(ctx, base);
    return;
  }
  const uint32_t device = ctx.r3.u32;
  std::array<uint32_t, 8> before{};
  size_t i = 0;
  for (const Field& field : fields) {
    before[i++] = LoadField(base, device + field.offset, field.size);
  }
  const DirtyWords dirty = LoadDirty(base, device);
  original(ctx, base);
  i = 0;
  for (const Field& field : fields) {
    if (LoadField(base, device + field.offset, field.size) != before[i++]) {
      Count(kRenderState, false);
      return;
    }
  }
  RestoreDirty(base, device, dirty);
  Count(kRenderState, true);
}

struct SamplerSnapshot {
  uint8_t fetch[kFetchConstantBytes];
  uint8_t bytes[kSamplerBytes.size()];

  bool operator==(const SamplerSnapshot&) const = default;
};

SamplerSnapshot TakeSampler(uint8_t* base, uint32_t device, uint32_t slot) {
  SamplerSnapshot snapshot;
  std::memcpy(snapshot.fetch, GuestBytes(base, device + kFetchConstants + slot * kFetchConstantBytes),
              kFetchConstantBytes);
  for (size_t i = 0; i < kSamplerBytes.size(); ++i) {
    snapshot.bytes[i] = Load8(base, device + kSamplerBytes[i] + slot);
  }
  return snapshot;
}

// Each sampler state setter merges its value into the slot's fetch constant and per-slot bytes and marks the slot.
void RunSamplerSetter(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t slot = ctx.r4.u32;
  if (!REXCVAR_GET(mua_skip_redundant_texture_state) || slot >= kSamplerCount) {
    original(ctx, base);
    return;
  }
  const SamplerSnapshot before = TakeSampler(base, device, slot);
  const uint64_t dirty = Load64(base, device + kStateDirty);
  original(ctx, base);
  const bool unchanged = TakeSampler(base, device, slot) == before;
  if (unchanged) {
    Store64(base, device + kStateDirty, dirty);
  }
  Count(kSamplerState, unchanged);
}

// Native copies of the D3D upload routines: each run of set bits in the mask (most significant first) becomes one
// type-0 packet of the shadowed values, and a run that does not fit goes through the D3D packet writer, which splits
// it across command buffer segments.
uint32_t WritePackets(PPCContext& ctx, uint8_t* base, uint32_t device, uint32_t cursor, uint32_t reg,
                      uint32_t source, uint32_t count, uint32_t granularity, bool& split) {
  if (StatsEnabled()) {
    Bump(g_upload_splits);
  }
  split = true;
  ctx.r3.u64 = device;
  ctx.r4.u64 = cursor;
  ctx.r5.u64 = reg;
  ctx.r6.u64 = source - 4;
  ctx.r7.u64 = count;
  ctx.r8.u64 = granularity;
  MUA_GUEST_WRITE_PACKETS(ctx, base);
  return ctx.r3.u32;
}

uint32_t PacketHeader(uint32_t count, uint32_t reg) {
  return ((count - 1) << 16) | reg;
}

uint64_t ShiftOut(uint64_t mask, int bits) {
  return bits >= 64 ? 0 : mask << bits;
}

struct UploadCall {
  PPCFunc* original;
  uint64_t r3, r4, r5, r6;
  uint32_t start;
};

UploadCall BeginUpload(PPCContext& ctx, uint8_t* base, PPCFunc* original) {
  return {original, ctx.r3.u64, ctx.r4.u64, ctx.r5.u64, ctx.r6.u64, Load32(base, ctx.r3.u32 + kCommandCursor)};
}

// DEBUG_SAULO: for each routine's first calls, replays the D3D routine over what the native copy just wrote and
// compares cursor and bytes; the command buffer keeps the D3D routine's output. Only calls that fit without a split
// replay the same way.
enum Upload : size_t { kRegisters, kAluConstants, kFetchConstantUpload, kUploadCount };
constexpr std::array<const char*, kUploadCount> kUploadNames = {"registers", "ALU constants", "fetch constants"};
constexpr uint64_t kVerifyCalls = 20000;
constexpr uint64_t kMaxMismatchLogs = 5;
std::array<std::atomic<uint64_t>, kUploadCount> g_verified{};
std::array<std::atomic<uint64_t>, kUploadCount> g_mismatches{};

[[clang::noinline]] void VerifyUpload(PPCContext& ctx, uint8_t* base, Upload upload, const UploadCall& call) {
  if (g_verified[upload].load(std::memory_order_relaxed) >= kVerifyCalls) {
    return;
  }
  static uint8_t native[16384];
  const uint32_t device = uint32_t(call.r3);
  const uint32_t native_end = Load32(base, device + kCommandCursor);
  const uint32_t size = native_end - call.start;
  if (size > sizeof(native)) {
    return;
  }
  std::memcpy(native, GuestBytes(base, call.start + 4), size);
  Store32(base, device + kCommandCursor, call.start);
  ctx.r3.u64 = call.r3;
  ctx.r4.u64 = call.r4;
  ctx.r5.u64 = call.r5;
  ctx.r6.u64 = call.r6;
  call.original(ctx, base);
  const uint32_t end = Load32(base, device + kCommandCursor);
  const bool match = end == native_end && std::memcmp(native, GuestBytes(base, call.start + 4), size) == 0;
  if (!match && g_mismatches[upload].fetch_add(1, std::memory_order_relaxed) < kMaxMismatchLogs) {
    REXLOG_WARN("DEBUG_SAULO native upload mismatch ({}): mask {:016X} start {:08X} native end {:08X} D3D end {:08X}",
                kUploadNames[upload], call.r4, call.start, native_end, end);
  }
  if (g_verified[upload].fetch_add(1, std::memory_order_relaxed) + 1 == kVerifyCalls) {
    REXLOG_INFO("DEBUG_SAULO native upload verify ({}): {} calls, {} mismatches", kUploadNames[upload], kVerifyCalls,
                g_mismatches[upload].load(std::memory_order_relaxed));
  }
}

}  // namespace

namespace mua::d3d {

// r3 device, r4 mask, r5 first register, r6 its shadow.
void UploadRegisters(PPCContext& ctx, uint8_t* base) {
  PPCFunc* original = &MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_REGISTERS);
  if (!REXCVAR_GET(mua_native_register_upload) || !ctx.r4.u64) {
    original(ctx, base);
    return;
  }
  const UploadCall call = BeginUpload(ctx, base, original);
  const uint32_t device = ctx.r3.u32;
  uint64_t mask = ctx.r4.u64;
  uint32_t reg = ctx.r5.u32;
  uint32_t source = ctx.r6.u32;
  uint32_t cursor = call.start;
  bool split = false;
  while (mask) {
    const int skip = std::countl_zero(mask);
    mask <<= skip;
    reg += skip;
    source += skip * 4;
    const int run = std::countl_one(mask);
    if (cursor + run * 4 < Load32(base, device + kCommandEnd)) {
      Store32(base, cursor + 4, PacketHeader(run, reg));
      CopyGuest(base, cursor + 8, source, run * 4);
      cursor += 4 + run * 4;
    } else {
      cursor = WritePackets(ctx, base, device, cursor, reg, source, run, 1, split);
    }
    mask = ShiftOut(mask, run);
    reg += run;
    source += run * 4;
  }
  Store32(base, device + kCommandCursor, cursor);
  if (REXCVAR_GET(mua_debug_verify_native_upload) && !split) [[unlikely]] {
    VerifyUpload(ctx, base, kRegisters, call);
  }
}

// r3 device, r4 mask of 4-constant blocks, r5 first register, r6 its shadow. The data starts 16-byte aligned after
// type-2 padding, and the shadow is read the way lvx reads it.
void UploadAluConstants(PPCContext& ctx, uint8_t* base) {
  PPCFunc* original = &MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_ALU_CONSTANTS);
  if (!REXCVAR_GET(mua_native_register_upload) || !ctx.r4.u64) {
    original(ctx, base);
    return;
  }
  const UploadCall call = BeginUpload(ctx, base, original);
  const uint32_t device = ctx.r3.u32;
  uint64_t mask = ctx.r4.u64;
  uint32_t reg = ctx.r5.u32;
  uint32_t source = ctx.r6.u32;
  uint32_t cursor = call.start;
  bool split = false;
  while (mask) {
    const int skip = std::countl_zero(mask);
    mask <<= skip;
    reg += skip * 16;
    source += skip * 64;
    const int run = std::countl_one(mask);
    const uint32_t dwords = run * 16;
    if (cursor + (dwords + 3) * 4 < Load32(base, device + kCommandEnd)) {
      const uint32_t padding = (cursor & 15) == 8 ? 0 : (cursor & 15) == 12 ? 3 : (cursor & 15) == 0 ? 2 : 1;
      for (uint32_t i = 0; i < padding; ++i) {
        cursor += 4;
        Store32(base, cursor, kNop);
      }
      Store32(base, cursor + 4, PacketHeader(dwords, reg));
      cursor += 4;
      for (uint32_t i = 0; i < dwords / 4; ++i) {
        CopyGuest(base, (cursor + 4 + i * 16) & ~15u, (source + i * 16) & ~15u, 16);
      }
      cursor += dwords * 4;
    } else {
      cursor = WritePackets(ctx, base, device, cursor, reg, source, dwords, 16, split);
    }
    mask = ShiftOut(mask, run);
    reg += dwords;
    source += run * 64;
  }
  Store32(base, device + kCommandCursor, cursor);
  if (REXCVAR_GET(mua_debug_verify_native_upload) && !split) [[unlikely]] {
    VerifyUpload(ctx, base, kAluConstants, call);
  }
}

// r3 device, r4 mask of fetch constants. Ends with the three-register write the D3D routine closes every upload with;
// its second dword differs with the path the last run took.
void UploadFetchConstants(PPCContext& ctx, uint8_t* base) {
  PPCFunc* original = &MUA_GUEST_ORIGINAL(MUA_GUEST_UPLOAD_FETCH_CONSTANTS);
  if (!REXCVAR_GET(mua_native_register_upload) || !ctx.r4.u64) {
    original(ctx, base);
    return;
  }
  const UploadCall call = BeginUpload(ctx, base, original);
  const uint32_t device = ctx.r3.u32;
  uint64_t mask = ctx.r4.u64;
  uint32_t reg = kFetchConstantBase;
  uint32_t source = device + kFetchConstants;
  uint32_t cursor = call.start;
  bool split = false;
  bool last_fit = false;
  while (mask) {
    const int skip = std::countl_zero(mask);
    mask <<= skip;
    reg += skip * 6;
    source += skip * kFetchConstantBytes;
    const int run = std::countl_one(mask);
    const uint32_t dwords = run * 6;
    last_fit = cursor + (dwords + 5) * 4 < Load32(base, device + kCommandEnd);
    if (last_fit) {
      Store32(base, cursor + 4, kNop);
      cursor += cursor & 7;
      Store32(base, cursor + 4, PacketHeader(dwords, reg));
      cursor += 4;
      CopyGuest(base, cursor + 4, source, dwords * 4);
      cursor += dwords * 4;
    } else {
      cursor = WritePackets(ctx, base, device, cursor, reg, source, dwords, 6, split);
    }
    mask = ShiftOut(mask, run);
    reg += dwords;
    source += run * kFetchConstantBytes;
  }
  if (last_fit) {
    Store32(base, device + kCommandCursor, cursor + 16);
    for (uint32_t offset : {4u, 12u}) {
      Store32(base, cursor + offset, kFetchFlushHeader);
      Store32(base, cursor + offset + 4, 0);
    }
  } else {
    Store32(base, device + kCommandCursor, cursor);
    if (cursor > Load32(base, device + kCommandReserveLimit)) {
      split = true;
      ctx.r3.u64 = device;
      MUA_GUEST_RESERVE_COMMANDS(ctx, base);
      cursor = ctx.r3.u32;
    }
    Store32(base, cursor + 4, kFetchFlushHeader);
    Store32(base, cursor + 8, 0);
    Store32(base, cursor + 12, 0);
    Store32(base, cursor + 16, 0);
    Store32(base, device + kCommandCursor, cursor + 16);
  }
  if (REXCVAR_GET(mua_debug_verify_native_upload) && !split) [[unlikely]] {
    VerifyUpload(ctx, base, kFetchConstantUpload, call);
  }
}

}  // namespace mua::d3d

// r3 device, r4 declaration. One without a binding id has never been bound, so it may be a new object at a freed one's
// address and still needs the rebind.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_DECLARATION));
REX_HOOK_RAW(MUA_GUEST_SET_VERTEX_DECLARATION) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t declaration = ctx.r4.u32;
  const bool skip = REXCVAR_GET(mua_skip_redundant_vertex_declaration) && declaration &&
                    Load32(base, device + kVertexDeclaration) == declaration &&
                    Load32(base, declaration + kDeclarationBindingId);
  Count(kDeclaration, skip);
  if (!skip) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_DECLARATION)(ctx, base);
  }
}

// r3 device, r4 shader.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_SHADER));
REX_HOOK_RAW(MUA_GUEST_SET_VERTEX_SHADER) {
  const uint32_t device = ctx.r3.u32;
  const bool skip = REXCVAR_GET(mua_skip_redundant_vertex_shader) &&
                    !(Load8(base, device + kFlags) & kFlagClearedByVertexShaderSet) &&
                    ShaderIsCurrent(base, device, kVertexShader, ctx.r4.u32, kVertexShaderHeader);
  Count(kVertexShaderGroup, skip);
  if (!skip) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_SHADER)(ctx, base);
  }
}

// r3 device, r4 shader.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_PIXEL_SHADER));
REX_HOOK_RAW(MUA_GUEST_SET_PIXEL_SHADER) {
  const bool skip = REXCVAR_GET(mua_skip_redundant_pixel_shader) &&
                    ShaderIsCurrent(base, ctx.r3.u32, kPixelShader, ctx.r4.u32, kPixelShaderHeader);
  Count(kPixelShaderGroup, skip);
  if (!skip) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SET_PIXEL_SHADER)(ctx, base);
  }
}

// r3 device, r4 stream, r5 buffer, r6 offset, r7 stride, r8 the stream's fetch constant dirty bit.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_STREAM_SOURCE));
REX_HOOK_RAW(MUA_GUEST_SET_STREAM_SOURCE) {
  const bool skip = REXCVAR_GET(mua_skip_redundant_stream_source) &&
                    StreamSourceIsCurrent(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
  Count(kStreamSource, skip);
  if (!skip) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SET_STREAM_SOURCE)(ctx, base);
  }
}

// r3 device, r4 slot, r5 texture, r6 the slot's dirty bit. The setter always runs, for its fence bookkeeping of the
// replaced texture; only the slot's dirty bit is withdrawn when the same texture leaves the fetch constant unchanged.
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_TEXTURE));
REX_HOOK_RAW(MUA_GUEST_SET_TEXTURE) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t slot = ctx.r4.u32;
  const uint32_t texture = ctx.r5.u32;
  const uint64_t slot_bit = ctx.r6.u64;
  if (!REXCVAR_GET(mua_skip_redundant_texture_state) || slot >= kTextureCount) {
    MUA_GUEST_ORIGINAL(MUA_GUEST_SET_TEXTURE)(ctx, base);
    return;
  }
  const uint32_t fetch = device + kFetchConstants + slot * kFetchConstantBytes;
  uint8_t before[kFetchConstantBytes];
  std::memcpy(before, GuestBytes(base, fetch), sizeof(before));
  const bool same_texture = Load32(base, device + kTextures + slot * 4) == texture;
  const uint64_t dirty = Load64(base, device + kStateDirty);
  MUA_GUEST_ORIGINAL(MUA_GUEST_SET_TEXTURE)(ctx, base);
  const bool skip = same_texture && std::memcmp(before, GuestBytes(base, fetch), sizeof(before)) == 0;
  if (skip) {
    Store64(base, device + kStateDirty, Load64(base, device + kStateDirty) & ~(slot_bit & ~dirty));
  }
  Count(kTexture, skip);
}

namespace {

// r3 device, r4 first constant, r5 values, r6 count of float4s, r7 the blocks of four the range covers (bit 63 first).
// Marks only the blocks holding a constant whose value changes; an unchanged block's registers already hold the
// shadow, or a pending shader commit loads the same literal values into them.
void SetShaderConstants(PPCContext& ctx, uint8_t* base, PPCFunc* original, uint32_t shadow_offset,
                        uint32_t dirty_offset) {
  const uint32_t device = ctx.r3.u32;
  const uint32_t first = ctx.r4.u32;
  const uint32_t values = ctx.r5.u32;
  const uint32_t count = ctx.r6.u32;
  const uint64_t blocks = ctx.r7.u64;
  if (!REXCVAR_GET(mua_skip_redundant_shader_constants) || !count || first + count > kAluConstantCount) {
    original(ctx, base);
    return;
  }
  const uint32_t shadow = (device + shadow_offset + first * 16) & ~15u;
  uint64_t changed = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (!GuestEqual(base, shadow + i * 16, values + i * 16, 16)) {
      changed |= uint64_t(1) << (63 - (first + i) / 4);
    }
  }
  const uint64_t marked = blocks & changed;
  if (StatsEnabled()) {
    Bump(g_constant_blocks, std::popcount(blocks));
    Bump(g_constant_blocks_marked, std::popcount(marked));
  }
  Count(kConstantGroup, !changed);
  if (!changed) {
    return;
  }
  CopyGuest(base, shadow, values, count * 16);
  Store64(base, device + dirty_offset, Load64(base, device + dirty_offset) | marked);
}

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS));
REX_HOOK_RAW(MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS) {
  SetShaderConstants(ctx, base, &MUA_GUEST_ORIGINAL(MUA_GUEST_SET_VERTEX_SHADER_CONSTANTS), kVsAluConstants,
                     kVsConstantDirty);
}

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS));
REX_HOOK_RAW(MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS) {
  SetShaderConstants(ctx, base, &MUA_GUEST_ORIGINAL(MUA_GUEST_SET_PIXEL_SHADER_CONSTANTS), kPsAluConstants,
                     kPsConstantDirty);
}

#define MUA_LEAF_SETTER_HOOK(address, ...)                                \
  REX_EXTERN(__imp__sub_##address);                                       \
  REX_HOOK_RAW(sub_##address) {                                           \
    RunLeafSetter(ctx, base, &__imp__sub_##address, {__VA_ARGS__});       \
  }
MUA_D3D_LEAF_SETTERS(MUA_LEAF_SETTER_HOOK)

#define MUA_SAMPLER_SETTER_HOOK(address)                                  \
  REX_EXTERN(__imp__sub_##address);                                       \
  REX_HOOK_RAW(sub_##address) {                                           \
    RunSamplerSetter(ctx, base, &__imp__sub_##address);                   \
  }
MUA_D3D_SAMPLER_SETTERS(MUA_SAMPLER_SETTER_HOOK)

#endif  // MUA_GUEST_HAS_D3D_STATE_FILTER
