#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc.h>

#include "aspect_ratio.h"
#include "settings.h"

namespace {

mua::AspectRatio ConfiguredAspect() {
  static const mua::AspectRatio aspect = mua::ParseAspectRatio(REXCVAR_GET(mua_aspect_ratio));
  return aspect;
}

}  // namespace

// Codegen midasm hooks (overrides.toml) in the device creation, where the title sets up an HD video mode: the back
// buffer size is in r11 and r10, and the display aspect in f13.
void mua_aspect_back_buffer(PPCRegister& width, PPCRegister& height) {
  const mua::AspectRatio aspect = ConfiguredAspect();
  const int back_buffer_width = mua::BackBufferWidthFor(aspect, width.s32, height.s32);
  if (back_buffer_width != width.s32) {
    width.s64 = back_buffer_width;
    REXLOG_INFO("Guest back buffer: {}x{} ({})", back_buffer_width, height.s32, mua::InfoFor(aspect).key);
  } else if (aspect != mua::AspectRatio::k16x9) {
    REXLOG_WARN("Guest back buffer is {}x{}, not the expected {}x{}; keeping it", width.s32, height.s32,
                mua::kHdBackBufferWidth, mua::kHdBackBufferHeight);
  }
}

void mua_aspect_display_aspect(PPCRegister& game_aspect) {
  const mua::AspectRatio aspect = ConfiguredAspect();
  const double display_aspect = mua::DisplayAspectFor(aspect, game_aspect.f64);
  if (display_aspect != game_aspect.f64) {
    game_aspect.f64 = display_aspect;
    REXLOG_INFO("Guest display aspect: {:.4f} ({})", display_aspect, mua::InfoFor(aspect).key);
  }
}
