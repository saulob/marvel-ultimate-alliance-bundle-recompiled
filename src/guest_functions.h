#pragma once

struct PPCFuncMapping;

namespace mua {

// Registers the generated guest-to-host function table, so a native crash address can name the recompiled function.
void InstallGuestFunctionTable(const PPCFuncMapping* mappings);

// DEBUG_SAULO: logs which guest hooks the function table runs. Only the programs that carry a module define it.
void LogGuestHooks(const PPCFuncMapping* mappings);

}  // namespace mua
