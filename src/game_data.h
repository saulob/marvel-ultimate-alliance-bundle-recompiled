#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mua {

struct GameDefinition;

// Where ReXGlue keeps the game's unlocked achievements: under the game's own folder, so games sharing a title ID
// stay apart.
std::filesystem::path AchievementStoreFile(const GameDefinition& game, const std::filesystem::path& user_data_root);

// Moves data from the earlier layouts into each game's folder (GameUserDataRoot): Gold Edition's title folder and
// achievements from the user data root,
// MUA 1's folder and every game's settings file from games/. A target that already exists is never overwritten.
// Returns a line for each move made or refused.
std::vector<std::string> MigrateLegacyGameData(const std::filesystem::path& user_data_root);

}  // namespace mua
