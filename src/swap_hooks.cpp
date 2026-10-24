#include <atomic>
#include <cstdint>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "frame_limiter.h"
#include "frame_pacing.h"
#include "guest_map.h"
#include "hang_watchdog.h"
#include "visual_trace.h"
#include "window_host.h"

// The SDK's VdSwap body, exported by rexruntime.dll. The SDK has no header for it.
namespace rex::kernel::xboxkrnl {

void VdSwap_entry(mapped_void buffer_ptr, mapped_void fetch_ptr, mapped_void unk2, mapped_void unk3, mapped_void unk4,
                  mapped_u32 frontbuffer_ptr, mapped_u32 texture_format_ptr, mapped_u32 color_space_ptr,
                  mapped_u32 width, mapped_u32 height);

}  // namespace rex::kernel::xboxkrnl

namespace {

// The idle-present thread repeats the last swap through the swap routine while the game submits no frames.
thread_local bool t_repeat_present = false;

}  // namespace

REX_EXTERN(MUA_GUEST_ORIGINAL(MUA_GUEST_SWAP));

REX_HOOK_RAW(MUA_GUEST_SWAP) {
  t_repeat_present = uint32_t(ctx.lr) == mua::guest::kRepeatPresentSite;
  MUA_GUEST_ORIGINAL(MUA_GUEST_SWAP)(ctx, base);
  t_repeat_present = false;
}

// Named like the SDK's export, so REX_EXTERN would call this hook again: the original is called by its entry point.
REX_HOOK_RAW(__imp__VdSwap) {
  const bool repeat = t_repeat_present;
  if (!repeat) {
    mua::PaceGuestFrame();
  }
  rex::ppc::HostToGuestFunction<rex::kernel::xboxkrnl::VdSwap_entry>(ctx, base);
  if (!repeat) {
    static std::atomic<bool> first_present{true};
    if (first_present.exchange(false)) {
      REXLOG_INFO("DEBUG_SAULO first guest present");
    }
    mua::NoteGuestFrame();
    mua::CountPresentForDiagnostics();
    mua::visual_trace::NotePresent();
    NoteGuestPresent();
  }
}
