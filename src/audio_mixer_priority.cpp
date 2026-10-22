// sub_824AF110: the stock XAudio render-driver callback that hands a mixed frame to
// XAudioSubmitRenderDriverFrame (it already carries the audio_pcm_firewall midasm hook right before that call,
// src/audio_pcm_firewall.cpp). It runs on its own thread, separate from the mix-worker rendezvous in
// sub_824A6BA8 (xaudio_worker_barrier.cpp) and from ReXGlue's own AudioSystem/XmaDecoder workers that 0115's
// RaiseAudioThreadPriority() already covers, so this thread needs the same request on its own.

#include <rex/audio/thread_priority.h>
#include <rex/hook.h>
#include <rex/logging.h>

REX_EXTERN(__imp__sub_824AF110);

// The callback runs repeatedly on one dedicated thread; the request only needs to happen once.
REX_HOOK_RAW(sub_824AF110) {
  thread_local const bool raised = [] {
    const bool ok = rex::audio::RaiseAudioThreadPriority();
    REXLOG_INFO("Audio mixer callback thread priority: {}", ok ? "raised" : "unavailable");
    return ok;
  }();
  (void)raised;
  __imp__sub_824AF110(ctx, base);
}
