#include "audio_pcm_firewall.h"

#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

// Codegen midasm hook before the XAudioSubmitRenderDriverFrame call in sub_824AF110 (config/overrides.toml).
void mua_audio_pcm_firewall(PPCRegister& samples) {
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint8_t* frame = rex::memory::GuestPtr<uint8_t*>(base, samples.u32);
  mua::PcmFirewallCounts counts;
  mua::SanitizeBigEndianPcm(frame, mua::kPcmFirewallSamples, counts);
}
