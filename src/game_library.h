#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "game_definition.h"
#include "game_files.h"

namespace mua {

struct DiscIdentity {
  uint32_t title_id = 0;
  uint32_t media_id = 0;
};

using DiscReader = std::function<std::optional<DiscIdentity>(const std::filesystem::path&)>;
using GameChecker = std::function<GameCheck(const GameDefinition&, const std::filesystem::path&)>;

// The game whose profile has this title ID and media ID; filenames play no part.
const GameDefinition* GameForDisc(const DiscIdentity& identity);

// The one check of a game's folder: the files must match this game's own profile.
GameCheck CheckGame(const GameDefinition& game, const std::filesystem::path& dir);

struct GameState {
  const GameDefinition* game = nullptr;
  std::filesystem::path data_dir;        // where the game's files are, or the folder an install would use
  GameCheck check;
  std::filesystem::path source_package;  // a disc image under the games folder that is this game's, else empty
};

enum class GameAvailability {
  kReady,
  kRuntimeUnavailable,
  kUnsupportedFiles,
  kIncomplete,
  kUnreadable,
  kNotInstalled,
};

// `runtime_blocker` is why this build cannot start a game whose runtime is otherwise supported.
GameAvailability AvailabilityOf(const GameState& state, std::string_view runtime_blocker);

bool CanPlay(const GameState& state, std::string_view runtime_blocker);

// "Selected game: <name> | profile=<id> | installed=yes | source=no | supported=yes".
std::string SelectedGameLogText(const GameState& state, std::string_view runtime_blocker);

class GameLibrary {
 public:
  // `explicit_root` is a game folder named on the command line; it belongs to the game its files identify.
  GameLibrary(std::filesystem::path games_root, std::filesystem::path explicit_root, DiscReader disc_reader,
              GameChecker checker = {});

  const GameState& State(GameId id) const { return states_[size_t(id)]; }
  const std::filesystem::path& games_root() const { return games_root_; }
  std::optional<GameId> explicit_game() const { return explicit_game_; }

  void RefreshFiles(GameId id);
  void RescanPackages();

 private:
  GameCheck Check(const GameDefinition& game, const std::filesystem::path& dir) const;

  std::filesystem::path games_root_;
  std::filesystem::path explicit_root_;
  std::optional<GameId> explicit_game_;
  DiscReader disc_reader_;
  GameChecker checker_;
  std::array<GameState, 3> states_;
};

// The selected game when a start without the launcher can run it; never another game.
const GameState* BootableGame(const GameLibrary& library, GameId selected, std::string_view runtime_blocker);

}  // namespace mua
