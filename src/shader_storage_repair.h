#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mua {

struct ShaderStorageRepair {
  std::string file;
  std::size_t dropped = 0;
  std::size_t kept = 0;
};

// Removes damaged records from the shader (.xsh) and pipeline (.xpso) storage files in <cache>/shaders, as King Kong
// does. ReXGlue stops reading a file at the first damaged record (a write the game never finished), so one would hide
// all the rest. Call before the runtime opens the files. Returns the files that were rewritten.
std::vector<ShaderStorageRepair> RepairShaderStorage(const std::filesystem::path& cache_dir);

}  // namespace mua
