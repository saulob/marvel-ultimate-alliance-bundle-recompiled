#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "game_library.h"

namespace fake {

namespace fs = std::filesystem;

struct Scratch {
  fs::path root;
  fs::path games;

  explicit Scratch(const char* name) : root(fs::temp_directory_path() / name), games(root / "game") {
    fs::remove_all(root);
    fs::create_directories(games);
  }
  ~Scratch() { fs::remove_all(root); }
};

inline void WriteText(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

inline std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// "<game key>|<build>": a folder whose default.xex names the game passes the check.
inline std::string Xex(const mua::GameDefinition& game, const std::string& build = "installed") {
  return std::string(game.key) + "|" + build;
}

inline void Install(const fs::path& dir, const mua::GameDefinition& game, const std::string& build = "installed") {
  WriteText(dir / "default.xex", Xex(game, build));
}

inline mua::GameCheck Check(const mua::GameDefinition& game, const fs::path& dir) {
  mua::GameCheck check;
  std::error_code ec;
  const fs::path xex = dir / "default.xex";
  if (!fs::is_regular_file(xex, ec)) {
    return check;
  }
  const std::string text = ReadText(xex);
  const bool ours = text.substr(0, text.find('|')) == game.key;
  check.state = ours && text.find("|bad") == std::string::npos ? mua::GameFileState::kValid
                                                              : mua::GameFileState::kDifferentVersion;
  if (check.state == mua::GameFileState::kValid) {
    check.profile = &mua::ProfileOf(game);
  }
  return check;
}

inline mua::DiscIdentity DiscOf(const mua::GameDefinition& game) {
  const mua::GameProfile& profile = mua::ProfileOf(game);
  return {profile.title_id, profile.media_id};
}

}  // namespace fake
