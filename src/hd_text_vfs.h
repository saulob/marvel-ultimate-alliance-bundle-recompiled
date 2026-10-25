#pragma once

#include "game_definition.h"

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace mua::hd_text {

// Must run before the title opens its archive. False when the game keeps its own atlases.
bool InstallHdText(rex::filesystem::VirtualFileSystem* vfs, GameId game, int render_scale, bool on);

}  // namespace mua::hd_text
