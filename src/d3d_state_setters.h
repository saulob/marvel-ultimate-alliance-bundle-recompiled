#pragma once

// The D3D device fields and state setters of the module's own D3D build.
#if defined(MUA_RUNTIME_MUA1_GOLD)
#include "d3d_state_setters_mua1_gold.h"
#elif defined(MUA_RUNTIME_MUA1)
#include "d3d_state_setters_mua1.h"
#elif defined(MUA_RUNTIME_MUA2)
#include "d3d_state_setters_mua2.h"
#else
#error "No D3D state setter table for this module"
#endif
