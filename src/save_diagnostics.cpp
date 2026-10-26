// Bounded logs of the profile and save calls: every failure, the content (save) operations, and once the title
// settings the profile holds, so a save or profile report shows what the game asked for and what it got.

#include <atomic>
#include <cstdint>
#include <format>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>

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

bool Succeeded(uint32_t status) {
  return status == kErrorSuccess || status == kErrorIoPending;
}

const uint8_t* Memory(uint8_t* base) {
  return rex::memory::GuestPtr<const uint8_t*>(base, 0);
}

std::string GuestString(const uint8_t* memory, uint32_t address) {
  if (!address) return "<null>";
  const char* text = reinterpret_cast<const char*>(memory + address);
  return std::string(text, strnlen(text, 32));
}

// The title settings of the first profile read that returns any, in one line.
void LogTitleSettings(const uint8_t* memory, uint32_t user, uint32_t count, uint32_t buffer) {
  static std::atomic<bool> logged{false};
  if (logged.load(std::memory_order_relaxed)) return;
  std::string text;
  mua::ProfileSettingView setting;
  for (uint32_t i = 0; i < count; ++i) {
    if (!mua::ReadProfileSetting(memory, buffer, i, setting) || !setting.title_specific) continue;
    text += (text.empty() ? "" : ", ") + std::format("{:08X}", setting.id) +
            (setting.source ? std::format(" {} bytes", setting.size) : std::string(" unset"));
  }
  if (!text.empty() && !logged.exchange(true)) {
    REXLOG_INFO("Profile title settings: user={} {}", user, text);
  }
}

int64_t EnumeratorItems(const uint8_t* memory, uint32_t handle_out, uint32_t status) {
  if (status != kErrorSuccess || !handle_out) return -1;
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return -1;
  auto enumerator = kernel->object_table()->LookupObject<rex::system::XEnumerator>(mua::LoadBE32(memory, handle_out));
  return enumerator ? int64_t(static_cast<rex::system::XStaticUntypedEnumerator&>(*enumerator).item_count()) : -1;
}

}  // namespace

REX_HOOK_RAW(MUA_GUEST_PROFILE_READ) {
  static std::atomic<int> failures{0};
  const uint32_t user = ctx.r4.u32;
  const uint32_t count = ctx.r5.u32;
  const uint32_t buffer = ctx.r8.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_READ)(ctx, base);
  const uint32_t status = ctx.r3.u32;
  if (status == kErrorSuccess && buffer) {
    LogTitleSettings(Memory(base), user, count, buffer);
  } else if (!Succeeded(status) && status != kErrorInsufficientBuffer && Budget(failures, 12)) {
    REXLOG_WARN("Profile settings read failed: user={} settings={} status={}", user, count, status);
  }
}

REX_HOOK_RAW(MUA_GUEST_PROFILE_WRITE) {
  static std::atomic<int> failures{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t count = ctx.r4.u32;
  const uint32_t settings = ctx.r5.u32;
  const uint32_t first_setting = count && settings ? mua::LoadBE32(Memory(base), settings + 16) : 0u;
  MUA_GUEST_ORIGINAL(MUA_GUEST_PROFILE_WRITE)(ctx, base);
  if (!Succeeded(ctx.r3.u32) && Budget(failures, 12)) {
    REXLOG_WARN("Profile settings write failed: user={} settings={} first={:08X} status={}", user, count,
                first_setting, ctx.r3.u32);
  }
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_CREATE) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t root = ctx.r4.u32;
  const uint32_t data = ctx.r5.u32;
  const uint32_t flags = ctx.r6.u32;
  const uint32_t disposition = ctx.r7.u32;
  const uint8_t* memory = Memory(base);
  const std::string root_name = GuestString(memory, root);
  const uint32_t device = data ? mua::LoadBE32(memory, data) : 0u;
  const uint32_t type = data ? mua::LoadBE32(memory, data + 4) : 0u;
  const std::string file = data ? mua::ContentFileName(memory, data) : std::string();
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CREATE)(ctx, base);
  const uint32_t status = ctx.r3.u32;
  if (!Budget(used, 24) && Succeeded(status)) {
    return;
  }
  const std::string text = std::format("user={} root={} device={} type={} file={} flags={} status={} disposition={}",
                                       user, root_name, device, type, file, flags & 0xF, status,
                                       disposition ? mua::LoadBE32(memory, disposition) : 0u);
  if (Succeeded(status)) {
    REXLOG_INFO("Content create: {}", text);
  } else {
    REXLOG_WARN("Content create failed: {}", text);
  }
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_CLOSE) {
  static std::atomic<int> used{0};
  const uint32_t root = ctx.r3.u32;
  const std::string root_name = GuestString(Memory(base), root);
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_CLOSE)(ctx, base);
  if (!Succeeded(ctx.r3.u32)) {
    REXLOG_WARN("Content close failed: root={} status={}", root_name, ctx.r3.u32);
  } else if (Budget(used, 24)) {
    REXLOG_INFO("Content close: root={} status={}", root_name, ctx.r3.u32);
  }
}

REX_HOOK_RAW(MUA_GUEST_CONTENT_ENUMERATE) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t device = ctx.r4.u32;
  const uint32_t type = ctx.r5.u32;
  const uint32_t handle_out = ctx.r9.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_CONTENT_ENUMERATE)(ctx, base);
  if (Budget(used, 12)) {
    REXLOG_INFO("Content enumerate: user={} device={} type={} status={} items={}", user, device, type, ctx.r3.u32,
                EnumeratorItems(Memory(base), handle_out, ctx.r3.u32));
  }
}

REX_HOOK_RAW(MUA_GUEST_DEVICE_SELECTOR) {
  static std::atomic<int> used{0};
  const uint32_t user = ctx.r3.u32;
  const uint32_t type = ctx.r4.u32;
  const uint32_t device = ctx.r7.u32;
  MUA_GUEST_ORIGINAL(MUA_GUEST_DEVICE_SELECTOR)(ctx, base);
  if (Budget(used, 12)) {
    REXLOG_INFO("Content device selector: user={} type={} status={} device={}", user, type, ctx.r3.u32,
                device ? mua::LoadBE32(Memory(base), device) : 0u);
  }
}
