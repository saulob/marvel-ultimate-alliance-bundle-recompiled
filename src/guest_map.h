#pragma once

// The guest addresses and structure offsets one executable's hooks bind to. Each executable carries one recompiled
// module, so the map of that module is the only one compiled in.
#if defined(MUA_RUNTIME_MUA1_GOLD)
#include "guest_map_mua1_gold.h"
#elif defined(MUA_RUNTIME_MUA1)
#include "guest_map_mua1.h"
#else
#error "No recompiled module is selected: define MUA_RUNTIME_MUA1 or MUA_RUNTIME_MUA1_GOLD"
#endif

// The recompiled original of a hooked guest function.
#define MUA_GUEST_ORIGINAL_(name) __imp__##name
#define MUA_GUEST_ORIGINAL(name) MUA_GUEST_ORIGINAL_(name)
