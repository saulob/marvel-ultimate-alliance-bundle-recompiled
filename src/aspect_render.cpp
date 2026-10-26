#include <atomic>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc.h>

#include "aspect_ratio.h"
#include "settings.h"
#include "upscaling.h"

namespace {

mua::AspectRatio ConfiguredAspect() {
  static const mua::AspectRatio aspect = mua::ParseAspectRatio(REXCVAR_GET(mua_aspect_ratio));
  return aspect;
}

mua::WindowSize UpscaledBackBufferFor(mua::WindowSize canvas) {
  return mua::UpscaledBackBuffer(canvas, mua::ActiveUpscaler(),
                                 mua::ParseUpscaleQuality(REXCVAR_GET(mua_upscale_quality)));
}

std::atomic<bool> g_back_buffer_upscaled{false};

}  // namespace

// Codegen midasm hooks (overrides.toml) in the device creation, where the title sets up an HD video mode: the back
// buffer size is in r11 and r10, and the display aspect in f13.
void mua_aspect_back_buffer(PPCRegister& width, PPCRegister& height) {
  const mua::AspectRatio aspect = ConfiguredAspect();
  if (width.s32 != mua::kHdBackBufferWidth || height.s32 != mua::kHdBackBufferHeight) {
    if (aspect != mua::AspectRatio::k16x9 || mua::ActiveUpscaler() != mua::Upscaler::kOff) {
      REXLOG_WARN("Guest back buffer is {}x{}, not the expected {}x{}; keeping it", width.s32, height.s32,
                  mua::kHdBackBufferWidth, mua::kHdBackBufferHeight);
    }
    return;
  }
  const mua::WindowSize canvas{mua::BackBufferWidthFor(aspect, width.s32, height.s32), height.s32};
  const mua::WindowSize back_buffer = UpscaledBackBufferFor(canvas);
  if (back_buffer.width != width.s32 || back_buffer.height != height.s32) {
    width.s64 = back_buffer.width;
    height.s64 = back_buffer.height;
    g_back_buffer_upscaled = back_buffer.width != canvas.width || back_buffer.height != canvas.height;
    REXLOG_INFO("Guest back buffer: {}x{} ({}, canvas {}x{})", back_buffer.width, back_buffer.height,
                mua::InfoFor(aspect).key, canvas.width, canvas.height);
  }
}

// Codegen midasm hook (overrides.toml) in the engine's D3D presentation parameters, where a fullscreen back buffer
// that matches no supported display mode falls back to the video mode (MUA1 sub_824FE4C8, Gold sub_825186D0,
// MUA2 sub_82271DF8 — same mode-list scan and native fallback in all three, just renamed registers in MUA2).
void mua_upscale_device_back_buffer(PPCRegister& width, PPCRegister& height) {
  if (!g_back_buffer_upscaled) {
    return;
  }
  const mua::WindowSize canvas = mua::VideoModeSize(ConfiguredAspect());
  if (width.s32 != canvas.width || height.s32 != canvas.height) {
    REXLOG_WARN("Device back buffer falls back to {}x{}, not the expected {}x{}; keeping it", width.s32, height.s32,
                canvas.width, canvas.height);
    return;
  }
  const mua::WindowSize back_buffer = UpscaledBackBufferFor(canvas);
  width.s64 = back_buffer.width;
  height.s64 = back_buffer.height;
  REXLOG_INFO("Guest back buffer: device {}x{} instead of display mode {}x{}", back_buffer.width, back_buffer.height,
              canvas.width, canvas.height);
}

void mua_aspect_display_aspect(PPCRegister& game_aspect) {
  const mua::AspectRatio aspect = ConfiguredAspect();
  const double display_aspect = mua::DisplayAspectFor(aspect, game_aspect.f64);
  if (display_aspect != game_aspect.f64) {
    game_aspect.f64 = display_aspect;
    REXLOG_INFO("Guest display aspect: {:.4f} ({})", display_aspect, mua::InfoFor(aspect).key);
  }
}
