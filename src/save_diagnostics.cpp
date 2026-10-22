// DEBUG_SAULO: bounded state logs for the profile and save wrappers; remove after the save flow is verified.

#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "guest_map.h"
#include "save_diagnostics.h"

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_READ));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_WRITE));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CREATE));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CLOSE));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_ENUMERATE));
REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_DEVICE_SELECTOR));

namespace {

constexpr uint32_t kErrorSuccess = 0;
constexpr uint32_t kErrorIoPending = 997;
constexpr uint32_t kErrorInsufficientBuffer = 122;

bool Budget(std::atomic<int>& used, int limit) {
  return used.fetch_add(1, std::memory_order_relaxed) < limit;
}

const uint8_t* Memory(uint8_t* base) {
  return rex::memory::GuestPtr<const uint8_t*>(base, 0);
}

std::string GuestString(const uint8_t* memory, uint32_t address) {
  if (!address) return "<null>";
  const char* text = reinterpret_cast<const char*>(memory + address);
  return std::string(text, strnlen(text, 32));
}

void LogProfileRead(const uint8_t* memory, uint32_t user, uint32_t count, uint32_t buffer, uint32_t status) {
  static std::atomic<int> used{0};
  if (!Budget(used, 12)) return;
  if (!buffer) {
    REXLOG_INFO("[DEBUG_SAULO][Profile] read size query: user={} settings={} status={}", user, count, status);
    return;
  }
  REXLOG_INFO("[DEBUG_SAULO][Profile] read: user={} settings={} status={}", user, count, status);
  if (status != kErrorSuccess) return;
  mua::ProfileSettingView setting;
  for (uint32_t i = 0; i < count; ++i) {
    if (!mua::ReadProfileSetting(memory, buffer, i, setting) || !setting.title_specific) continue;
    REXLOG_INFO("[DEBUG_SAULO][Profile] read setting {:08X}: source={} type={} size={}", setting.id, setting.source,
                setting.type, setting.size);
  }
}

}  // namespace

REX_HOOK_RAW(MUA_GUEST_PROFILE_READ) {
  const uint32_t user = ctx.r4.u32;
  const uint32_t count = ctx.r5.u32;
  const uint32_t buffer = ctx.r8.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_READ)(ctx, base);
  uint32_t status = ctx.r3.u32;
  if (status != kErrorInsufficientBuffer && status != kErrorSuccess && status != kErrorIoPending) {
    REXLOG_INFO("[DEBUG_SAULO][Profile] read failed: status={}", status);
    return;
  }
  LogProfileRead(Memory(base), user, count, buffer, status);
}

REX_HOOK_RAW(MUA_GUEST_PROFILE_WRITE) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t count = ctx.r4.u32;
  const uint32_t settings = ctx.r5.u32;
  const bool log = Budget(used, 12);
  if (log && count == 1) {
    const uint8_t* memory = Memory(base);
    REXLOG_INFO("[DEBUG_SAULO][Profile] write: user={} setting={:08X} size={}", user,
                mua::LoadBE32(memory, settings + 16), mua::LoadBE32(memory, settings + 32));
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_WRITE)(ctx, base);
  if (log) REXLOG_INFO("[DEBUG_SAULO][Profile] write status={}", ctx.r3.u32);
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_CREATE) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t root = ctx.r4.u32;
  const uint32_t data = ctx.r5.u32;
  const uint32_t flags = ctx.r6.u32;
  const uint32_t disposition = ctx.r7.u32;
  const bool log = Budget(used, 24);
  const uint8_t* memory = Memory(base);
  if (log) {
    REXLOG_INFO("[DEBUG_SAULO][Content] create: user={} root={} device={} type={} file={} flags={}", user,
                GuestString(memory, root), mua::LoadBE32(memory, data), mua::LoadBE32(memory, data + 4),
                mua::ContentFileName(memory, data), flags & 0xF);
  }
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CREATE)(ctx, base);
  if (log) {
    REXLOG_INFO("[DEBUG_SAULO][Content] create status={} disposition={}", ctx.r3.u32,
                disposition ? mua::LoadBE32(memory, disposition) : 0u);
  }
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_CLOSE) {
  static std::atomic<int> used{0};
  const uint32_t root = ctx.r3.u32;
  const bool log = Budget(used, 24);
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CLOSE)(ctx, base);
  if (log) {
    REXLOG_INFO("[DEBUG_SAULO][Content] close: root={} status={}", GuestString(Memory(base), root), ctx.r3.u32);
  }
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_ENUMERATE) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t device = ctx.r4.u32;
  const uint32_t type = ctx.r5.u32;
  const bool log = Budget(used, 12);
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_ENUMERATE)(ctx, base);
  if (log) {
    REXLOG_INFO("[DEBUG_SAULO][Content] enumerate: user={} device={} type={} status={}", user, device, type,
                ctx.r3.u32);
  }
}

REX_HOOK_RAW(MUA_GUEST_DEVICE_SELECTOR) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t type = ctx.r4.u32;
  const uint32_t device = ctx.r7.u32;
  const bool log = Budget(used, 12);
  MUA_GUEST_ORIGINAL(MUA_GUEST_DEVICE_SELECTOR)(ctx, base);
  if (log) {
    REXLOG_INFO("[DEBUG_SAULO][Content] device selector: user={} type={} status={} device={}", user, type, ctx.r3.u32,
                device ? mua::LoadBE32(Memory(base), device) : 0u);
  }
}
