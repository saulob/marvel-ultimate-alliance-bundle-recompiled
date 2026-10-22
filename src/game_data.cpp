#include "game_data.h"

#include <format>
#include <system_error>

#include "game_definition.h"
#include "path_utf8.h"

namespace mua {
namespace {

namespace fs = std::filesystem;

void Move(const fs::path& from, const fs::path& to, std::vector<std::string>& log) {
  std::error_code ec;
  if (!fs::exists(from, ec)) {
    return;
  }
  if (fs::exists(to, ec)) {
    log.push_back(std::format("Kept {}: {} already exists", Utf8(from), Utf8(to)));
    return;
  }
  fs::create_directories(to.parent_path(), ec);
  ec.clear();
  fs::rename(from, to, ec);
  log.push_back(ec ? std::format("Could not move {} to {}: {}", Utf8(from), Utf8(to), ec.message())
                   : std::format("Moved {} to {}", Utf8(from), Utf8(to)));
}

}  // namespace

std::vector<std::string> MigrateLegacyGameData(const fs::path& user_data_root) {
  std::vector<std::string> log;
  const fs::path legacy = user_data_root / "games";
  for (const GameDefinition& game : AllGames()) {
    const fs::path folder = GameUserDataRoot(game, user_data_root);
    if (game.id == GameId::kMua1Gold) {
      const std::string title_folder = std::format("{:08X}", ProfileOf(game).title_id);
      Move(user_data_root / title_folder, folder / title_folder, log);
    } else {
      Move(legacy / std::string(game.key), folder, log);
    }
    Move(legacy / (std::string(game.key) + ".toml"), folder / "game_settings.toml", log);
  }
  return log;
}

}  // namespace mua
