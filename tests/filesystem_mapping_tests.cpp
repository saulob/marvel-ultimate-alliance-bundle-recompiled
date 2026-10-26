// Host file mapping as Runtime::SetupVfs builds it: game_data_root is the console's game: and D: volume, update: exists
// only with an update root. Scratch folders, and the real game folder when a path is given.
// Run: mua_filesystem_mapping_tests [game folder]

#include <algorithm>
#include <array>
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

// The roots each game tries in turn for a sound file: Gold Edition looks for resident banks on D: and then update:, and
// opens streams on muadlc: and then D:; MUA1 only uses D:.
struct SoundRoots {
  std::array<const char*, 2> banks;
  std::array<const char*, 2> streams;
};
constexpr SoundRoots kGoldRoots{{"d:", "update:"}, {"muadlc:", "d:"}};
constexpr SoundRoots kMua1Roots{{"d:", nullptr}, {"d:", nullptr}};

std::string FirstRoot(rex::filesystem::VirtualFileSystem& vfs, const std::array<const char*, 2>& roots,
                      const std::string& relative) {
  for (const char* root : roots) {
    if (root && vfs.ResolvePath(std::string(root) + "\\" + relative)) {
      return root;
    }
  }
  return {};
}

std::string SoundPath(const std::string& file) {
  return "sounds\\eng\\" + file.substr(0, 1) + "\\" + (file[1] == '_' ? "_" : file.substr(1, 1)) + "\\" + file;
}

fs::path HostSoundPath(const std::string& file) {
  std::string path = SoundPath(file);
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

// Gold Edition's own voice banks: its eight downloadable heroes and the downloadable missions' voices.
constexpr std::array kGoldOnlyStreams = {"cyclps_v.zss", "doom_v.zss",  "hulk_v.zss",  "mgneto_v.zss",
                                         "ntcrwl_v.zss", "sabrth_v.zss", "venom_v.zss", "x_vce_dlc.zss"};
constexpr std::array kGoldOnlyBanks = {"cyclps_m.zsm", "doom_m.zsm",   "hulk_m.zsm",   "mgneto_m.zsm",
                                       "ntcrwl_m.zsm", "sabrth_m.zsm", "venom_m.zsm"};

void TestGoldSoundRoots() {
  std::printf("Gold Edition sound files end on D: without update: or muadlc:\n");
  const fs::path root = fs::temp_directory_path() / "mua_filesystem_mapping_tests_gold";
  fs::remove_all(root);
  for (const char* file : {"x_common.zsm", "x_voice.zss", "x_vce_dlc.zss", "venom_m.zsm", "venom_v.zss"}) {
    Touch(root / "game" / HostSoundPath(file));
  }
  Touch(root / "update" / HostSoundPath("x_voice.zsm"));
  Touch(root / "update" / HostSoundPath("x_common.zsm"));

  auto vfs = Mount(root / "game", {});
  CHECK(vfs->ResolvePath("update:\\" + SoundPath("x_voice.zsm")) == nullptr);
  CHECK(vfs->ResolvePath("muadlc:\\" + SoundPath("x_voice.zss")) == nullptr);
  CHECK(FirstRoot(*vfs, kGoldRoots.banks, SoundPath("x_common.zsm")) == "d:");
  CHECK(FirstRoot(*vfs, kGoldRoots.banks, SoundPath("venom_m.zsm")) == "d:");
  for (const char* file : {"x_voice.zss", "x_vce_dlc.zss", "venom_v.zss"}) {
    CHECK(FirstRoot(*vfs, kGoldRoots.streams, SoundPath(file)) == "d:");
  }
  // Probed by the game, on no disc: the stream of the same name plays instead.
  for (const char* file : {"x_voice.zsm", "x_vce_dlc.zsm"}) {
    CHECK(FirstRoot(*vfs, kGoldRoots.banks, SoundPath(file)).empty());
    CHECK(FirstRoot(*vfs, kMua1Roots.banks, SoundPath(file)).empty());
  }
  CHECK(FirstRoot(*vfs, kMua1Roots.banks, SoundPath("x_common.zsm")) == "d:");
  CHECK(FirstRoot(*vfs, kMua1Roots.streams, SoundPath("x_voice.zss")) == "d:");

  std::printf("a title update package only adds what the disc lacks\n");
  auto with_update = Mount(root / "game", root / "update");
  CHECK(FirstRoot(*with_update, kGoldRoots.banks, SoundPath("x_common.zsm")) == "d:");
  CHECK(FirstRoot(*with_update, kGoldRoots.banks, SoundPath("x_voice.zsm")) == "update:");
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
  if (!fs::exists(game_dir / "muadlc_content.zip")) {
    return;
  }
  std::printf("every Gold Edition voice bank is on its disc\n");
  for (const char* file : kGoldOnlyStreams) {
    CHECK(FirstRoot(*vfs, kGoldRoots.streams, SoundPath(file)) == "d:");
  }
  for (const char* file : kGoldOnlyBanks) {
    CHECK(FirstRoot(*vfs, kGoldRoots.banks, SoundPath(file)) == "d:");
  }
  CHECK(FirstRoot(*vfs, kGoldRoots.banks, SoundPath("x_vce_dlc.zsm")).empty());
}

}  // namespace

int main(int argc, char** argv) {
  TestScratchTree();
  TestGoldSoundRoots();
  if (argc > 1) {
    TestGameFolder(argv[1]);
  }
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
