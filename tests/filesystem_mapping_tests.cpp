// Host file mapping as Runtime::SetupVfs builds it: game_data_root is the console's game: and D: volume, update: exists
// only with an update root. Scratch folders, and the real game folder when a path is given.
// Run: mua_filesystem_mapping_tests [game folder]

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/vfs.h>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

constexpr char kGameVolume[] = "\\Device\\Harddisk0\\Partition1";
constexpr char kUpdateVolume[] = "\\Device\\Harddisk0\\PartitionUpdate";

void Touch(const fs::path& file) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << "x";
}

std::unique_ptr<rex::filesystem::VirtualFileSystem> Mount(const fs::path& game_root, const fs::path& update_root) {
  auto vfs = std::make_unique<rex::filesystem::VirtualFileSystem>();
  auto game = std::make_unique<rex::filesystem::HostPathDevice>(kGameVolume, fs::absolute(game_root), true);
  CHECK(game->Initialize());
  CHECK(vfs->RegisterDevice(std::move(game)));
  vfs->RegisterSymbolicLink("game:", kGameVolume);
  vfs->RegisterSymbolicLink("d:", kGameVolume);
  if (!update_root.empty()) {
    auto update = std::make_unique<rex::filesystem::HostPathDevice>(kUpdateVolume, fs::absolute(update_root), true);
    CHECK(update->Initialize());
    CHECK(vfs->RegisterDevice(std::move(update)));
    vfs->RegisterSymbolicLink("update:", kUpdateVolume);
  }
  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"), std::string("\\Cache1")};
  auto null_device = std::make_unique<rex::filesystem::NullDevice>("\\Device\\Harddisk0", null_paths);
  if (null_device->Initialize()) {
    vfs->RegisterDevice(std::move(null_device));
  }
  return vfs;
}

void TestScratchTree() {
  std::printf("D: and game: are the game data root\n");
  const fs::path root = fs::temp_directory_path() / "mua_filesystem_mapping_tests";
  fs::remove_all(root);
  Touch(root / "game" / "default.xex");
  Touch(root / "game" / "z" / "assetsfb.zip");
  Touch(root / "game" / "sounds" / "eng" / "x" / "_" / "x_common.zsm");
  Touch(root / "update" / "sounds" / "eng" / "x" / "_" / "x_voice.zsm");

  auto vfs = Mount(root / "game", {});
  for (const char* path : {"d:\\default.xex", "D:\\default.xex", "game:\\default.xex", "d:\\z\\assetsfb.zip",
                           "D:\\Z\\AssetsFB.zip", "d:\\sounds\\eng\\x\\_\\x_common.zsm"}) {
    CHECK(vfs->ResolvePath(path) != nullptr);
  }
  CHECK(vfs->ResolvePath("d:\\z\\assetsfb.wad") == nullptr);
  CHECK(vfs->ResolvePath("d:\\drivers.ini") == nullptr);
  CHECK(vfs->ResolvePath("d:\\sounds\\eng\\x\\_\\x_voice.zsm") == nullptr);

  std::printf("update: exists only with an update root\n");
  CHECK(vfs->ResolvePath("update:\\sounds\\eng\\x\\_\\x_voice.zsm") == nullptr);
  auto with_update = Mount(root / "game", root / "update");
  CHECK(with_update->ResolvePath("update:\\sounds\\eng\\x\\_\\x_voice.zsm") != nullptr);
  CHECK(with_update->ResolvePath("d:\\sounds\\eng\\x\\_\\x_voice.zsm") == nullptr);
  fs::remove_all(root);
}

void TestGameFolder(const fs::path& game_dir) {
  std::printf("D: resolves the extracted game folder\n");
  auto vfs = Mount(game_dir, {});
  CHECK(vfs->ResolvePath("d:\\default.xex") != nullptr);
  CHECK(vfs->ResolvePath("d:\\z\\assetsfb.zip") != nullptr);
  CHECK(vfs->ResolvePath("d:\\muadlc_content.zip") != nullptr);
  CHECK(vfs->ResolvePath("d:\\muadlc_titleupdate.zip") != nullptr);
  CHECK(vfs->ResolvePath("d:\\alchemy.ini") != nullptr);
  CHECK(vfs->ResolvePath("d:\\sounds\\eng\\x\\_\\x_common.zsm") != nullptr);
  CHECK(vfs->ResolvePath("d:\\sounds\\eng\\x\\_\\x_voice.zss") != nullptr);
  // Probed by the game, not on the disc.
  CHECK(vfs->ResolvePath("d:\\z\\assetsfb.wad") == nullptr);
  CHECK(vfs->ResolvePath("d:\\drivers.ini") == nullptr);
  CHECK(vfs->ResolvePath("d:\\sounds\\eng\\x\\_\\x_voice.zsm") == nullptr);
  CHECK(vfs->ResolvePath("update:\\sounds\\eng\\x\\_\\x_voice.zsm") == nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  TestScratchTree();
  if (argc > 1) {
    TestGameFolder(argv[1]);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
