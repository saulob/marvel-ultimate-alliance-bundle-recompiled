#pragma once

#include <cstdint>

#include <rex/ppc.h>

namespace mua::d3d {

// The routines that copy dirty register, ALU constant and fetch constant shadows into the command buffer.
void UploadRegisters(PPCContext& ctx, uint8_t* base);
void UploadAluConstants(PPCContext& ctx, uint8_t* base);
void UploadFetchConstants(PPCContext& ctx, uint8_t* base);

}  // namespace mua::d3d
