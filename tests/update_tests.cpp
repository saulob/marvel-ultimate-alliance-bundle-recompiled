// Updater tests: semantic versions, release parsing, asset selection, SHA-256, ZIP safety, staging, manifest and
// rollback behavior, and the bundle the update replaces. Scratch folders only; nothing is downloaded or launched.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "update_bundle.h"
#include "updater/github_release.h"
#include "updater/install_transaction.h"
#include "updater/sha256.h"
#include "updater/third_party/miniz/miniz.h"
#include "updater/update_job.h"
#include "updater/update_paths.h"
#include "updater/update_zip.h"
#include "updater/version.h"

using namespace trg::update;

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

class TempDir {
 public:
  TempDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("mua-update-tests-" + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), std::streamsize(content.size()));
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, std::string_view needle) {
  return text.find(needle) != std::string::npos;
}

std::string MakeZip(const std::vector<std::pair<std::string, std::string>>& entries) {
  mz_zip_archive archive{};
  if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE) {
    return {};
  }
  for (const auto& [name, content] : entries) {
    if (mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION) ==
        MZ_FALSE) {
      mz_zip_writer_end(&archive);
      return {};
    }
  }
  void* data = nullptr;
  size_t size = 0;
  if (mz_zip_writer_finalize_heap_archive(&archive, &data, &size) == MZ_FALSE) {
    mz_zip_writer_end(&archive);
    return {};
  }
  std::string result(static_cast<const char*>(data), size);
  mz_free(data);
  mz_zip_writer_end(&archive);
  return result;
}

std::vector<std::pair<std::string, std::string>> BundleEntries(const std::string& tag) {
  std::vector<std::pair<std::string, std::string>> entries;
  for (const char* file : mua::kBundleFiles) {
    entries.emplace_back(file, tag + ":" + file);
  }
  return entries;
}

std::string ManifestText(const std::vector<std::string>& files) {
  std::string text;
  for (const std::string& file : files) {
    text += file + '\n';
  }
  return text;
}

std::vector<std::string> BundleFileList() {
  return std::vector<std::string>(mua::kBundleFiles.begin(), mua::kBundleFiles.end());
}

void WriteBundle(const std::filesystem::path& root, const std::string& tag) {
  for (const char* file : mua::kBundleFiles) {
    WriteFile(root / std::filesystem::path(file), tag + ":" + file);
  }
}

bool BundleHolds(const std::filesystem::path& root, const std::string& tag) {
  for (const char* file : mua::kBundleFiles) {
    if (ReadFile(root / std::filesystem::path(file)) != tag + ":" + file) {
      return false;
    }
  }
  return true;
}

void WriteUserAndGameFiles(const std::filesystem::path& install) {
  WriteFile(install / "user_data" / "settings.toml", "player settings");
  WriteFile(install / "user_data" / "mua1" / "saves" / "slot0.sav", "mua1 save");
  WriteFile(install / "user_data" / "mua1_gold" / "saves" / "slot0.sav", "gold save");
  WriteFile(install / "user_data" / "mua2" / "saves" / "slot0.sav", "mua2 save");
  WriteFile(install / "user_data" / "mua2" / "achievements" / "4156082F.toml", "achievements");
  WriteFile(install / "game" / "mua1-gold" / "default.xex", "gold game");
  WriteFile(install / "game" / "mua2" / "default.xex", "mua2 game");
  WriteFile(install / "game" / "mua1.staging" / "default.xex", "partial install");
  WriteFile(install / "logs" / "session.log", "log");
  WriteFile(install / "keep.txt", "not owned by the release");
}

bool UserAndGameFilesIntact(const std::filesystem::path& install) {
  return ReadFile(install / "user_data" / "settings.toml") == "player settings" &&
         ReadFile(install / "user_data" / "mua1" / "saves" / "slot0.sav") == "mua1 save" &&
         ReadFile(install / "user_data" / "mua1_gold" / "saves" / "slot0.sav") == "gold save" &&
         ReadFile(install / "user_data" / "mua2" / "saves" / "slot0.sav") == "mua2 save" &&
         ReadFile(install / "user_data" / "mua2" / "achievements" / "4156082F.toml") == "achievements" &&
         ReadFile(install / "game" / "mua1-gold" / "default.xex") == "gold game" &&
         ReadFile(install / "game" / "mua2" / "default.xex") == "mua2 game" &&
         ReadFile(install / "game" / "mua1.staging" / "default.xex") == "partial install" &&
         ReadFile(install / "logs" / "session.log") == "log" &&
         ReadFile(install / "keep.txt") == "not owned by the release";
}

bool LeftoverTemporaryFiles(const std::filesystem::path& install) {
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(install, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().filename().string().rfind(".trg-update-", 0) == 0) {
      return true;
    }
  }
  return false;
}

struct InstallFixture {
  explicit InstallFixture(const std::filesystem::path& root)
      : install(root / "install"),
        staging(root / "workspace" / "staging"),
        backup(root / "workspace" / "backup"),
        manifest(root / "workspace" / "manifest.txt") {
    job.parent_pid = 1234;
    job.install_dir = install;
    job.staging_dir = staging;
    job.backup_dir = backup;
    job.manifest_file = manifest;
    job.restart_executable = install / mua::kBundleLauncher;
    job.log_file = root / "workspace" / "updater.log";
  }

  void ResetInstall() {
    std::error_code ec;
    std::filesystem::remove_all(install, ec);
    WriteBundle(install, "old");
    std::filesystem::remove(install / "assets" / "launcher" / "header-art-mua2.png", ec);
    WriteFile(install / "assets" / "launcher" / "header-art-retired.png", "retired art");
    WriteUserAndGameFiles(install);
  }

  void Stage(const std::string& tag) {
    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    std::filesystem::remove_all(backup, ec);
    WriteBundle(staging, tag);
    WriteFile(manifest, ManifestText(BundleFileList()));
  }

  std::filesystem::path install;
  std::filesystem::path staging;
  std::filesystem::path backup;
  std::filesystem::path manifest;
  UpdateJob job;
};

void TestVersions() {
  std::printf("semantic versions\n");
  const auto dev = ParseVersion("0.1.0-dev");
  const auto v010 = ParseVersion("0.1.0");
  const auto v011 = ParseVersion("v0.1.1");
  const auto v020 = ParseVersion("0.2.0");
  const auto v0100 = ParseVersion("0.10.0");
  const auto v100 = ParseVersion("1.0.0");
  CHECK(dev && v010 && v011 && v020 && v0100 && v100);
  CHECK(*dev < *v010);
  CHECK(*v010 < *v011);
  CHECK(*v011 < *v020);
  CHECK(*v020 < *v0100);
  CHECK(*v0100 < *v100);
  CHECK(SemanticVersion::Compare(*v010, *v010) == 0);
  CHECK(SemanticVersion::Compare(*ParseVersion("v0.1.0"), *v010) == 0);
  CHECK(SemanticVersion::Compare(*v020, *v010) > 0);
  CHECK(SemanticVersion::Compare(*v010, *v020) < 0);
  CHECK(dev->CoreString() == "0.1.0");
  CHECK(dev->ToString() == "0.1.0-dev");
  CHECK(!dev->stable());
  CHECK(v010->stable());

  const auto rc = ParseVersion("0.3.2-rc");
  const auto rc1 = ParseVersion("0.3.2-rc.1");
  const auto rc10 = ParseVersion("0.3.2-rc.10");
  const auto v032 = ParseVersion("0.3.2");
  CHECK(rc && rc1 && rc10 && v032);
  CHECK(*rc < *rc1);
  CHECK(*rc1 < *rc10);
  CHECK(*rc10 < *v032);
  CHECK(SemanticVersion::Compare(*ParseVersion("0.3.2+build.5"), *v032) == 0);
  CHECK(ParseVersion("0.3.2+build.5")->build == "build.5");

  CHECK(!ParseVersion(""));
  CHECK(!ParseVersion("1.0"));
  CHECK(!ParseVersion("1.0.0.0"));
  CHECK(!ParseVersion("01.0.0"));
  CHECK(!ParseVersion("1.0.0-"));
  CHECK(!ParseVersion("v"));
  CHECK(!ParseVersion("latest"));
  CHECK(!ParseVersion("not-a-version"));
  CHECK(!ParseVersion("1.x.0"));
  CHECK(!ParseVersion("-1.0.0"));
  CHECK(ParseVersion("V0.4.0").has_value());
  CHECK(ParseVersion("0.3.2-RC1").has_value());
}

constexpr const char* kReleaseJson = R"({
    "tag_name": "v0.2.0",
    "draft": false,
    "prerelease": false,
    "html_url": "https://github.com/saulob/marvel-ultimate-alliance-recompiled/releases/tag/v0.2.0",
    "assets": [
      {"name": "MarvelUltimateAlliance-v0.2.0-windows-x64.zip",
       "browser_download_url": "https://github.com/example/windows.zip",
       "size": 123456,
       "digest": "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"},
      {"name": "MarvelUltimateAlliance-v0.2.0-windows-x64.sha256",
       "browser_download_url": "https://github.com/example/windows.sha256",
       "size": 115,
       "digest": "sha256:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB"},
      {"name": "MarvelUltimateAlliance-v0.2.0-linux-x86_64.tar.gz",
       "browser_download_url": "https://github.com/example/linux.tar.gz",
       "size": 654321,
       "digest": "sha256:CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC"},
      {"name": "MarvelUltimateAlliance-v0.1.0-windows-x64.zip",
       "browser_download_url": "https://github.com/example/older.zip",
       "size": 1000}
    ]
  })";

void TestReleases() {
  std::printf("release JSON and asset selection\n");
  std::string error;
  const std::optional<Release> release = ParseRelease(kReleaseJson, &error);
  CHECK(release.has_value());
  CHECK(error.empty());
  if (!release) {
    return;
  }
  CHECK(release->tag == "v0.2.0");
  CHECK(release->version.CoreString() == "0.2.0");
  CHECK(IsInstallableRelease(*release));
  CHECK(release->html_url.rfind("https://", 0) == 0);
  CHECK(release->assets.size() == 4);

  const std::string name = mua::UpdateAssetName(release->version);
  const std::string checksum_name = mua::UpdateChecksumAssetName(release->version);
  CHECK(name == "MarvelUltimateAlliance-v0.2.0-windows-x64.zip");
  CHECK(checksum_name == "MarvelUltimateAlliance-v0.2.0-windows-x64.sha256");
  const ReleaseAsset* windows = FindAsset(*release, name);
  const ReleaseAsset* windows_sha = FindAsset(*release, checksum_name);
  CHECK(windows != nullptr);
  CHECK(windows_sha != nullptr);
  CHECK(windows && windows->download_url == "https://github.com/example/windows.zip");
  CHECK(windows && windows->size == 123456);
  CHECK(windows && windows->sha256() == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  CHECK(windows_sha && windows_sha->download_url == "https://github.com/example/windows.sha256");
  CHECK(FindAsset(*release, "MarvelUltimateAlliance-v0.2.0-linux-x86_64.tar.gz") != nullptr);
  CHECK(FindAsset(*release, "different.zip") == nullptr);

  const auto other = ParseVersion("0.3.0");
  CHECK(other && FindAsset(*release, mua::UpdateAssetName(*other)) == nullptr);

  const std::optional<Release> draft = ParseRelease(R"({
    "tag_name":"v0.4.1","draft":true,"prerelease":false,"assets":[]
  })");
  CHECK(draft && !IsInstallableRelease(*draft));
  const std::optional<Release> prerelease = ParseRelease(R"({
    "tag_name":"v0.4.1-rc1","draft":false,"prerelease":true,"assets":[]
  })");
  CHECK(prerelease && !IsInstallableRelease(*prerelease));
  const std::optional<Release> prerelease_tag = ParseRelease(R"({
    "tag_name":"v0.4.1-rc1","draft":false,"prerelease":false,"assets":[]
  })");
  CHECK(prerelease_tag && !IsInstallableRelease(*prerelease_tag));

  CHECK(!ParseRelease(R"({"tag_name": 42})").has_value());
  CHECK(!ParseRelease(R"({"tag_name": "not-a-version"})").has_value());
  CHECK(!ParseRelease(R"({"tag_name": "latest","assets":[]})").has_value());
  CHECK(!ParseRelease("{").has_value());
  CHECK(!ParseRelease("").has_value());
  CHECK(!ParseRelease("[]").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.4.0","assets":{}})").has_value());
  CHECK(!ParseRelease(R"({"tag_name":"v0.4.0","assets":[{"name":"x"}]})").has_value());

  const std::optional<Release> no_windows = ParseRelease(R"({
    "tag_name":"v0.4.0","draft":false,"prerelease":false,
    "assets":[{"name":"MarvelUltimateAlliance-v0.4.0-linux-x86_64.tar.gz","browser_download_url":"https://x/y","size":5}]
  })");
  CHECK(no_windows && FindAsset(*no_windows, mua::UpdateAssetName(no_windows->version)) == nullptr);
}

void TestSecureUrls() {
  std::printf("update addresses\n");
  CHECK(IsSecureUpdateUrl("https://github.com/saulob/marvel-ultimate-alliance-recompiled/releases/download/a.zip"));
  CHECK(!IsSecureUpdateUrl("http://github.com/a.zip"));
  CHECK(!IsSecureUpdateUrl("http://127.0.0.1:8000/a.zip"));
  CHECK(!IsSecureUpdateUrl("ftp://github.com/a.zip"));
  CHECK(!IsSecureUpdateUrl("file:///C:/a.zip"));
  CHECK(!IsSecureUpdateUrl(""));
  CHECK(IsSecureUpdateUrl("https://github.com/a.zip", true));
  CHECK(IsSecureUpdateUrl("http://127.0.0.1:8000/a.zip", true));
  CHECK(IsSecureUpdateUrl("http://localhost/a.zip", true));
  CHECK(IsSecureUpdateUrl("http://[::1]:9000/a.zip", true));
  CHECK(!IsSecureUpdateUrl("http://127.0.0.1.example.com/a.zip", true));
  CHECK(!IsSecureUpdateUrl("http://localhost.example.com/a.zip", true));
  CHECK(!IsSecureUpdateUrl("http://127.0.0.1@example.com/a.zip", true));
  CHECK(!IsSecureUpdateUrl("http://github.com/a.zip", true));
  CHECK(!IsSecureUpdateUrl("http:///a.zip", true));
  CHECK(!IsSecureUpdateUrl("http://", true));
  CHECK(!IsSecureUpdateUrl("file:///C:/a.zip", true));
}

void TestSha256() {
  std::printf("SHA-256\n");
  CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string long_text(1000000, 'a');
  CHECK(Sha256Hex(long_text) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

  TempDir temp;
  const std::filesystem::path file = temp.path / "hash.bin";
  WriteFile(file, "abc");
  std::string error;
  const std::string actual = Sha256FileHex(file, error);
  CHECK(actual == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(error.empty());
  CHECK(actual != "0000000000000000000000000000000000000000000000000000000000000000");
  CHECK(Sha256FileHex(temp.path / "missing.bin", error).empty());
  CHECK(!error.empty());

  const std::string lower = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
  std::string upper = lower;
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return char(std::toupper(c)); });
  CHECK(IsValidSha256(lower));
  CHECK(IsValidSha256(upper));
  CHECK(NormalizeSha256(upper) == lower);
  CHECK(!IsValidSha256(lower.substr(1)));
  CHECK(!IsValidSha256(lower + "0"));
  CHECK(!IsValidSha256(""));
  CHECK(!IsValidSha256(std::string(63, 'a') + "g"));
  CHECK(ParseSha256Digest("sha256:" + lower) == lower);
  CHECK(ParseSha256Digest("sha256:" + upper) == lower);
  CHECK(ParseSha256Digest("SHA256:" + upper) == lower);
  CHECK(ParseSha256Digest("sha256:abc") == std::nullopt);
  CHECK(ParseSha256Digest("sha256:") == std::nullopt);
  CHECK(ParseSha256Digest("") == std::nullopt);
  CHECK(ParseSha256Digest(lower) == std::nullopt);
  CHECK(ParseSha256Digest("sha256:" + std::string(63, 'a') + "z") == std::nullopt);
  CHECK(ParseSha256Digest("md5:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == std::nullopt);
  CHECK(ParseSha256Digest("sha1:" + lower) == std::nullopt);

  ReleaseAsset missing;
  CHECK(!missing.sha256().has_value());
  ReleaseAsset malformed;
  malformed.digest = "sha256:12345";
  CHECK(!malformed.sha256().has_value());
  ReleaseAsset good;
  good.digest = "sha256:" + upper;
  CHECK(good.sha256() == lower);

  CHECK(ParseSha256File(upper + "  MarvelUltimateAlliance-v0.2.0-windows-x64.zip\n") == lower);
  CHECK(ParseSha256File(lower + " *MarvelUltimateAlliance-v0.2.0-windows-x64.zip\r\n") == lower);
  CHECK(ParseSha256File(lower) == lower);
  CHECK(ParseSha256File("not-a-hash  file.zip") == std::nullopt);
  CHECK(ParseSha256File("") == std::nullopt);
  CHECK(ParseSha256File("\n") == std::nullopt);
  CHECK(ParseSha256File(lower.substr(2) + "  file.zip") == std::nullopt);

  const std::string wrong_archive = MakeZip({{"a.txt", "x"}});
  WriteFile(temp.path / "archive.zip", wrong_archive);
  CHECK(Sha256FileHex(temp.path / "archive.zip", error) != lower);
  CHECK(Sha256FileHex(temp.path / "archive.zip", error) == Sha256Hex(wrong_archive));
}

void TestZipAndStaging() {
  std::printf("ZIP extraction and staging\n");
  TempDir temp;
  const std::filesystem::path archive = temp.path / "bundle.zip";
  WriteFile(archive, MakeZip(BundleEntries("new")));
  const std::filesystem::path staging = temp.path / "staging";
  std::string error;
  CHECK(ExtractZip(archive, staging, {}, error));
  CHECK(error.empty());
  CHECK(BundleHolds(staging, "new"));
  const std::vector<std::string> required = BundleFileList();
  CHECK(ValidateStaging(staging, required, error));
  CHECK(ListStagingFiles(staging, error).size() == required.size());
  std::vector<std::string> listed = ListStagingFiles(staging, error);
  std::vector<std::string> expected = required;
  std::sort(listed.begin(), listed.end());
  std::sort(expected.begin(), expected.end());
  CHECK(listed == expected);

  for (const std::string& missing : required) {
    const std::filesystem::path partial = temp.path / "partial";
    std::filesystem::remove_all(partial);
    std::vector<std::pair<std::string, std::string>> entries;
    for (const auto& entry : BundleEntries("new")) {
      if (entry.first != missing) {
        entries.push_back(entry);
      }
    }
    WriteFile(temp.path / "partial.zip", MakeZip(entries));
    CHECK(ExtractZip(temp.path / "partial.zip", partial, {}, error));
    CHECK(!ValidateStaging(partial, required, error));
    CHECK(Contains(error, missing));
  }

  WriteFile(staging / "game" / "mua1" / "default.xex", "game");
  CHECK(!ValidateStaging(staging, required, error));
  std::filesystem::remove_all(staging / "game");

  WriteFile(staging / "default.xex", "game");
  CHECK(!ValidateStaging(staging, required, error));
  std::filesystem::remove(staging / "default.xex");

  WriteFile(staging / "settings.toml", "settings");
  CHECK(!ValidateStaging(staging, required, error));
  std::filesystem::remove(staging / "settings.toml");

  WriteFile(staging / "logs" / "a.log", "log");
  CHECK(!ValidateStaging(staging, required, error));
  std::filesystem::remove_all(staging / "logs");

  WriteFile(staging / "game" / "mua1-gold.iso", "disc");
  CHECK(!ValidateStaging(staging, required, error));
  std::filesystem::remove_all(staging / "game");

  for (const char* folder : {"mua1", "mua1_gold", "mua2"}) {
    WriteFile(staging / "user_data" / folder / "saves" / "slot0.sav", "save");
    CHECK(!ValidateStaging(staging, required, error));
    std::filesystem::remove_all(staging / "user_data");
  }
  CHECK(ValidateStaging(staging, required, error));
  CHECK(!ValidateStaging(staging, {"missing.exe"}, error));
  CHECK(!ValidateStaging(temp.path / "no-such-folder", required, error));

  const std::vector<std::string> unsafe_names = {
      "../evil",          "C:/evil",         "C:\\evil",           "\\\\server\\share", "a/../../b",
      "a\\..\\b",         "a//b",            "game/default.xex",   "settings.toml",     "a/b.log",
      "default.xex",      "user_data/mua1/saves/slot0.sav",        "game/mua2/z/file.pak",
      "C:evil",           "a/b.",            "con",                "a/b:stream",
      "logs/session.log", "mua.iso",         "saves/slot0.sav",
  };
  int rejected = 0;
  for (const std::string& name : unsafe_names) {
    const std::filesystem::path unsafe_archive = temp.path / "unsafe.zip";
    const std::string bytes = MakeZip({{name, "payload"}});
    if (bytes.empty()) {
      std::printf("  zip writer rejected %s\n", name.c_str());
    }
    CHECK(!bytes.empty());
    WriteFile(unsafe_archive, bytes);
    const std::filesystem::path unsafe_staging = temp.path / "unsafe-staging";
    std::filesystem::remove_all(unsafe_staging);
    if (!ExtractZip(unsafe_archive, unsafe_staging, {}, error)) {
      ++rejected;
    }
    CHECK(!std::filesystem::exists(temp.path / "evil"));
  }
  CHECK(rejected == int(unsafe_names.size()));

  // The writer refuses a leading slash, so the name is patched into an archive of the same length.
  std::string absolute = MakeZip({{"qabsolute", "payload"}});
  for (size_t at = absolute.find("qabsolute"); at != std::string::npos; at = absolute.find("qabsolute", at)) {
    absolute[at] = '/';
  }
  WriteFile(temp.path / "absolute.zip", absolute);
  CHECK(Contains(absolute, "/absolute"));
  CHECK(!ExtractZip(temp.path / "absolute.zip", temp.path / "absolute-staging", {}, error));
  CHECK(IsSymlinkZipEntry(0120000u << 16));
  CHECK(!IsSymlinkZipEntry(0100644u << 16));
  CHECK(!IsSymlinkZipEntry(0));

  const std::filesystem::path dot_archive = temp.path / "dot.zip";
  WriteFile(dot_archive, MakeZip({{"./marvel_ultimate_alliance.exe", "launcher"}}));
  const std::filesystem::path dot_staging = temp.path / "dot-staging";
  CHECK(ExtractZip(dot_archive, dot_staging, {}, error));
  CHECK(std::filesystem::exists(dot_staging / "marvel_ultimate_alliance.exe"));

  const std::filesystem::path nested_archive = temp.path / "nested.zip";
  WriteFile(nested_archive, MakeZip({{"assets/launcher/header-art-mua1.png", "art"}, {"licenses/a/b/c.txt", "deep"}}));
  const std::filesystem::path nested_staging = temp.path / "nested-staging";
  CHECK(ExtractZip(nested_archive, nested_staging, {}, error));
  CHECK(ReadFile(nested_staging / "assets" / "launcher" / "header-art-mua1.png") == "art");
  CHECK(ReadFile(nested_staging / "licenses" / "a" / "b" / "c.txt") == "deep");
}

void TestZipLimitsAndMalformed() {
  std::printf("ZIP limits and malformed archives\n");
  TempDir temp;
  std::string error;

  WriteFile(temp.path / "dupe.zip", MakeZip({{"a.txt", "1"}, {"a.txt", "2"}}));
  CHECK(!ExtractZip(temp.path / "dupe.zip", temp.path / "dupe", {}, error));
  CHECK(Contains(error, "twice"));

#if defined(_WIN32)
  // Names that differ only in case are the same file on Windows; the AppImage has no archive to unpack.
  WriteFile(temp.path / "case.zip", MakeZip({{"rexruntime.dll", "1"}, {"RexRuntime.DLL", "2"}}));
  CHECK(!ExtractZip(temp.path / "case.zip", temp.path / "case", {}, error));
  CHECK(Contains(error, "twice"));
#endif

  WriteFile(temp.path / "dot-dupe.zip", MakeZip({{"a.txt", "1"}, {"./a.txt", "2"}}));
  CHECK(!ExtractZip(temp.path / "dot-dupe.zip", temp.path / "dot-dupe", {}, error));

  ZipLimits entries;
  entries.max_entries = 2;
  WriteFile(temp.path / "three.zip", MakeZip({{"a", "1"}, {"b", "2"}, {"c", "3"}}));
  CHECK(!ExtractZip(temp.path / "three.zip", temp.path / "three", entries, error));
  CHECK(Contains(error, "too many"));

  ZipLimits total;
  total.max_total_bytes = 10;
  WriteFile(temp.path / "big.zip", MakeZip({{"a", std::string(11, 'x')}}));
  CHECK(!ExtractZip(temp.path / "big.zip", temp.path / "big", total, error));
  CHECK(Contains(error, "too large"));
  WriteFile(temp.path / "two.zip", MakeZip({{"a", std::string(6, 'x')}, {"b", std::string(6, 'y')}}));
  CHECK(!ExtractZip(temp.path / "two.zip", temp.path / "two", total, error));

  ZipLimits archive_size;
  archive_size.max_archive_bytes = 8;
  CHECK(!ExtractZip(temp.path / "two.zip", temp.path / "two-small", archive_size, error));
  CHECK(Contains(error, "too large"));

  WriteFile(temp.path / "garbage.zip", "this is not a zip archive at all");
  CHECK(!ExtractZip(temp.path / "garbage.zip", temp.path / "garbage", {}, error));
  CHECK(Contains(error, "not a valid ZIP"));
  WriteFile(temp.path / "empty.zip", "");
  CHECK(!ExtractZip(temp.path / "empty.zip", temp.path / "empty", {}, error));
  CHECK(!ExtractZip(temp.path / "missing.zip", temp.path / "missing", {}, error));

  std::string truncated = MakeZip(BundleEntries("new"));
  truncated.resize(truncated.size() / 2);
  WriteFile(temp.path / "truncated.zip", truncated);
  CHECK(!ExtractZip(temp.path / "truncated.zip", temp.path / "truncated", {}, error));

  std::string corrupt = MakeZip({{"a.txt", std::string(2000, 'q')}});
  corrupt[40] = char(corrupt[40] ^ 0x5a);
  WriteFile(temp.path / "corrupt.zip", corrupt);
  CHECK(!ExtractZip(temp.path / "corrupt.zip", temp.path / "corrupt", {}, error));

  WriteFile(temp.path / "no-files.zip", MakeZip({}));
  if (ExtractZip(temp.path / "no-files.zip", temp.path / "no-files", {}, error)) {
    CHECK(!ValidateStaging(temp.path / "no-files", {}, error));
    CHECK(Contains(error, "empty"));
  }
}

void TestJobs() {
  std::printf("update jobs\n");
  TempDir temp;
  const std::filesystem::path install = temp.path / "install dir";
  const std::filesystem::path staging = temp.path / "staging dir";
  const std::filesystem::path backup = temp.path / "backup dir";
  const std::filesystem::path manifest = temp.path / "manifest.txt";
  std::filesystem::create_directories(install);
  std::filesystem::create_directories(staging);
  WriteFile(manifest, "marvel_ultimate_alliance.exe\n");

  UpdateJob job;
  job.parent_pid = 1234;
  job.install_dir = install;
  job.staging_dir = staging;
  job.backup_dir = backup;
  job.manifest_file = manifest;
  job.restart_executable = install / mua::kBundleLauncher;
  job.log_file = temp.path / "updater.log";
  job.restart_arguments = {"--user_data_root=with space", "--mua_launcher=true", ""};

  std::string error;
  const std::filesystem::path job_file = temp.path / "job.txt";
  CHECK(WriteUpdateJob(job_file, job, error));
  const std::optional<UpdateJob> read = ReadUpdateJob(job_file, error);
  CHECK(read.has_value());
  if (read) {
    CHECK(read->parent_pid == 1234);
    CHECK(read->install_dir == install);
    CHECK(read->staging_dir == staging);
    CHECK(read->backup_dir == backup);
    CHECK(read->manifest_file == manifest);
    CHECK(read->restart_executable == install / mua::kBundleLauncher);
    CHECK(read->restart_arguments == job.restart_arguments);
  }

  WriteFile(job_file, "format=1\nparent_pid=abc\n");
  CHECK(!ReadUpdateJob(job_file, error).has_value());
  WriteFile(job_file, "");
  CHECK(!ReadUpdateJob(job_file, error).has_value());
  CHECK(!ReadUpdateJob(temp.path / "no-job.txt", error).has_value());

  UpdateJob bad_argument = job;
  bad_argument.restart_arguments = {"--a\n--b"};
  CHECK(!WriteUpdateJob(temp.path / "bad-job.txt", bad_argument, error));

  WriteFile(staging / "marvel_ultimate_alliance.exe", "launcher");
  CHECK(ValidateInstallJob(job, error));

  UpdateJob inside = job;
  inside.staging_dir = install / "staging";
  std::filesystem::create_directories(inside.staging_dir);
  CHECK(!ValidateInstallJob(inside, error));

  UpdateJob backup_inside = job;
  backup_inside.backup_dir = install / "backup";
  CHECK(!ValidateInstallJob(backup_inside, error));

  UpdateJob manifest_inside = job;
  manifest_inside.manifest_file = install / "manifest.txt";
  WriteFile(manifest_inside.manifest_file, "marvel_ultimate_alliance.exe\n");
  CHECK(!ValidateInstallJob(manifest_inside, error));

  UpdateJob relative = job;
  relative.install_dir = "install";
  CHECK(!ValidateInstallJob(relative, error));

  UpdateJob no_pid = job;
  no_pid.parent_pid = 0;
  CHECK(!ValidateInstallJob(no_pid, error));

  UpdateJob outside = job;
  outside.restart_executable = temp.path / "marvel_ultimate_alliance.exe";
  CHECK(!ValidateInstallJob(outside, error));

  UpdateJob no_manifest = job;
  no_manifest.manifest_file = temp.path / "missing-manifest.txt";
  CHECK(!ValidateInstallJob(no_manifest, error));

  UpdateJob missing_staging = job;
  missing_staging.staging_dir = temp.path / "no-staging";
  CHECK(!ValidateInstallJob(missing_staging, error));
}

void TestBundleInstall() {
  std::printf("bundle install\n");
  TempDir temp;
  InstallFixture fixture(temp.path);
  fixture.ResetInstall();
  fixture.Stage("new");

  const InstallOutcome success = InstallFiles(fixture.job, {}, false);
  CHECK(success.status == InstallStatus::kSuccess);
  CHECK(success.error.empty());
  CHECK(BundleHolds(fixture.install, "new"));
  for (const char* program : {"marvel_ultimate_alliance.exe", "marvel_ultimate_alliance_mua1.exe",
                              "marvel_ultimate_alliance_mua1_gold.exe", "marvel_ultimate_alliance_mua2.exe",
                              "trg-updater.exe", "rexruntime.dll", "rexgpu-xenos.dll"}) {
    CHECK(ReadFile(fixture.install / program) == std::string("new:") + program);
  }
  CHECK(ReadFile(fixture.install / "assets" / "launcher" / "header-art-mua1.png") ==
        "new:assets/launcher/header-art-mua1.png");
  CHECK(ReadFile(fixture.install / "assets" / "launcher" / "header-art-mua2.png") ==
        "new:assets/launcher/header-art-mua2.png");
  CHECK(UserAndGameFilesIntact(fixture.install));
  CHECK(ReadFile(fixture.install / "assets" / "launcher" / "header-art-retired.png") == "retired art");
  CHECK(!LeftoverTemporaryFiles(fixture.install));
  CHECK(!std::filesystem::exists(fixture.backup));
  CHECK(!std::filesystem::exists(fixture.staging));
}

void TestRollbackAtEveryStep() {
  std::printf("rollback at every replacement\n");
  TempDir temp;
  InstallFixture fixture(temp.path);
  const int total = int(mua::kBundleFiles.size());
  for (int fail_at = 1; fail_at <= total; ++fail_at) {
    fixture.ResetInstall();
    fixture.Stage("new");
    int calls = 0;
    InstallOptions options;
    options.replace = [&calls, fail_at](const std::filesystem::path& from, const std::filesystem::path& to) {
      if (++calls == fail_at) {
        return false;
      }
      std::error_code ec;
      std::filesystem::rename(from, to, ec);
      return !ec;
    };
    const InstallOutcome outcome = InstallFiles(fixture.job, options, false);
    CHECK(outcome.status == InstallStatus::kInstallFailedRolledBack);
    CHECK(!outcome.error.empty());
    for (const char* file : mua::kBundleFiles) {
      const std::filesystem::path path = fixture.install / std::filesystem::path(file);
      if (std::string_view(file) == "assets/launcher/header-art-mua2.png") {
        CHECK(!std::filesystem::exists(path));
      } else {
        CHECK(ReadFile(path) == std::string("old:") + file);
      }
    }
    CHECK(ReadFile(fixture.install / "assets" / "launcher" / "header-art-retired.png") == "retired art");
    CHECK(UserAndGameFilesIntact(fixture.install));
    CHECK(!LeftoverTemporaryFiles(fixture.install));
    CHECK(!std::filesystem::exists(fixture.backup));
  }

  fixture.ResetInstall();
  fixture.Stage("new");
  int calls = 0;
  InstallOptions corrupt;
  corrupt.replace = [&calls](const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec && ++calls == 3) {
      WriteFile(to, "silently damaged");
    }
    return !ec;
  };
  const InstallOutcome damaged = InstallFiles(fixture.job, corrupt, false);
  CHECK(damaged.status == InstallStatus::kInstallFailedRolledBack);
  CHECK(Contains(damaged.error, "does not match"));
  CHECK(ReadFile(fixture.install / mua::kBundleLauncher) == "old:marvel_ultimate_alliance.exe");
  CHECK(ReadFile(fixture.install / "marvel_ultimate_alliance_mua2.exe") ==
        "old:marvel_ultimate_alliance_mua2.exe");
  CHECK(UserAndGameFilesIntact(fixture.install));
  CHECK(!LeftoverTemporaryFiles(fixture.install));

  fixture.ResetInstall();
  fixture.Stage("new");
  const InstallOutcome restart_failed = [&] {
    UpdateJob job = fixture.job;
    job.restart_executable = fixture.install / mua::kBundleLauncher;
    std::filesystem::remove(fixture.staging / mua::kBundleLauncher);
    WriteFile(fixture.staging / mua::kBundleLauncher, "not an executable");
    return InstallFiles(job, {}, true);
  }();
  CHECK(restart_failed.status == InstallStatus::kRestartFailedRolledBack);
  CHECK(ReadFile(fixture.install / mua::kBundleLauncher) == "old:marvel_ultimate_alliance.exe");
  CHECK(ReadFile(fixture.install / "marvel_ultimate_alliance_mua1.exe") == "old:marvel_ultimate_alliance_mua1.exe");
  CHECK(UserAndGameFilesIntact(fixture.install));
}

void TestManifestRules() {
  std::printf("manifest rules\n");
  TempDir temp;
  InstallFixture fixture(temp.path);
  fixture.ResetInstall();

  fixture.Stage("new");
  WriteFile(fixture.manifest, ManifestText({"marvel_ultimate_alliance_mua1.exe", "marvel_ultimate_alliance_mua2.exe"}));
  const InstallOutcome no_launcher = InstallFiles(fixture.job, {}, false);
  CHECK(no_launcher.status == InstallStatus::kInvalidJob);
  CHECK(Contains(no_launcher.error, "launcher"));
  CHECK(ReadFile(fixture.install / "marvel_ultimate_alliance_mua1.exe") == "old:marvel_ultimate_alliance_mua1.exe");

  const std::vector<std::string> forbidden = {
      "game/default.xex", "default.xex", "user_data/settings.toml", "user_data/mua1/saves/slot0.sav", "settings.toml",
      "logs/session.log", "../outside.exe", "C:/outside.exe", "game/mua2/z/file.pak", "cache/shaders/a.xsh"};
  for (const std::string& entry : forbidden) {
    fixture.Stage("new");
    WriteFile(fixture.staging / std::filesystem::path(entry).relative_path().filename(), "payload");
    WriteFile(fixture.manifest, ManifestText({mua::kBundleLauncher, entry}));
    CHECK(InstallFiles(fixture.job, {}, false).status == InstallStatus::kInvalidJob);
    CHECK(UserAndGameFilesIntact(fixture.install));
    CHECK(ReadFile(fixture.install / mua::kBundleLauncher) == "old:marvel_ultimate_alliance.exe");
  }

  fixture.Stage("new");
  WriteFile(fixture.manifest, ManifestText({mua::kBundleLauncher, "not-staged.dll"}));
  CHECK(InstallFiles(fixture.job, {}, false).status == InstallStatus::kInvalidJob);
  CHECK(ReadFile(fixture.install / mua::kBundleLauncher) == "old:marvel_ultimate_alliance.exe");

  fixture.Stage("new");
  WriteFile(fixture.manifest, "");
  CHECK(InstallFiles(fixture.job, {}, false).status == InstallStatus::kInvalidJob);

  fixture.Stage("new");
  std::filesystem::remove(fixture.manifest);
  CHECK(InstallFiles(fixture.job, {}, false).status == InstallStatus::kInvalidJob);
  CHECK(UserAndGameFilesIntact(fixture.install));
  CHECK(!LeftoverTemporaryFiles(fixture.install));
}

void TestBundleDefinitions() {
  std::printf("bundle definitions\n");
  CHECK(std::string_view(mua::kUpdateReleasesUrl) ==
        "https://api.github.com/repos/saulob/marvel-ultimate-alliance-recompiled/releases/latest");
  CHECK(std::string_view(mua::kBundleLauncher) == "marvel_ultimate_alliance.exe");
  CHECK(std::string_view(mua::kUpdateHelper) == "trg-updater.exe");
  const auto version = ParseVersion("1.2.3-rc1");
  CHECK(version && mua::UpdateAssetName(*version) == "MarvelUltimateAlliance-v1.2.3-windows-x64.zip");
  CHECK(version && mua::UpdateChecksumAssetName(*version) == "MarvelUltimateAlliance-v1.2.3-windows-x64.sha256");

  const std::vector<std::string> bundle = BundleFileList();
  for (const char* required :
       {"marvel_ultimate_alliance.exe", "marvel_ultimate_alliance_mua1.exe", "marvel_ultimate_alliance_mua1_gold.exe",
        "marvel_ultimate_alliance_mua2.exe", "trg-updater.exe", "rexruntime.dll", "rexgpu-xenos.dll",
        "marvel_ultimate_alliance.toml", "assets/launcher/header-art-mua1.png",
        "assets/launcher/header-art-mua1-gold.png", "assets/launcher/header-art-mua2.png",
        "licenses/THIRD-PARTY-LICENSES.txt"}) {
    CHECK(std::find(bundle.begin(), bundle.end(), required) != bundle.end());
  }
  for (const std::string& file : bundle) {
    std::string normalized;
    CHECK(NormalizeRelativePath(file, normalized));
    CHECK(!IsProtectedRelativePath(normalized));
  }

  const std::vector<std::string> from_game = mua::UpdateRestartArguments(
      {"--mua_skip_launcher=true", "--user_data_root=C:/data", "--mua_launcher=false", "--mua_skip_launcher"});
  CHECK((from_game == std::vector<std::string>{"--user_data_root=C:/data", "--mua_launcher=true"}));
  const std::vector<std::string> plain = mua::UpdateRestartArguments({});
  CHECK((plain == std::vector<std::string>{"--mua_launcher=true"}));
}

void TestPaths() {
  std::printf("path safety\n");
  CHECK(IsSafeRelativePath("marvel_ultimate_alliance.exe"));
  CHECK(IsSafeRelativePath("assets/launcher/header-art-mua1-gold.png"));
  CHECK(IsSafeRelativePath("./marvel_ultimate_alliance.exe"));
  CHECK(!IsSafeRelativePath("../marvel_ultimate_alliance.exe"));
  CHECK(!IsSafeRelativePath("/absolute"));
  CHECK(!IsSafeRelativePath("C:/marvel_ultimate_alliance.exe"));
  CHECK(!IsSafeRelativePath("\\\\server\\share"));
  CHECK(!IsSafeRelativePath("a//b"));
  CHECK(!IsSafeRelativePath("a/./../b"));
  CHECK(IsProtectedRelativePath("game/default.xex"));
  CHECK(IsProtectedRelativePath("game/mua1-gold/default.xex"));
  CHECK(IsProtectedRelativePath("game/mua2.staging/default.xex"));
  CHECK(IsProtectedRelativePath("game/Marvel - Ultimate Alliance 2.iso"));
  CHECK(IsProtectedRelativePath("user_data/settings.toml"));
  CHECK(IsProtectedRelativePath("user_data/mua1_gold/game_settings.toml"));
  CHECK(IsProtectedRelativePath("user_data.migrating/mua1/saves/a.sav"));
  CHECK(IsProtectedRelativePath("logs/a.log"));
  CHECK(IsProtectedRelativePath("cache/shaders/a.bin"));
  CHECK(IsProtectedRelativePath("settings.toml"));
  CHECK(IsProtectedRelativePath("settings.toml.bak"));
  CHECK(IsProtectedRelativePath("4156082F/profile/User/a"));
  CHECK(IsProtectedRelativePath("415607DA/profile/User/a"));
  CHECK(IsProtectedRelativePath("default.xex"));
  CHECK(!IsProtectedRelativePath("marvel_ultimate_alliance.exe"));
  CHECK(!IsProtectedRelativePath("marvel_ultimate_alliance.toml"));
  CHECK(!IsProtectedRelativePath("trg-updater.exe"));
  CHECK(!IsProtectedRelativePath("rexruntime.dll"));
  CHECK(!IsProtectedRelativePath("assets/launcher/header-art-mua2.png"));
  CHECK(!IsProtectedRelativePath("licenses/THIRD-PARTY-LICENSES.txt"));
}

void TestSourceWiring() {
  std::printf("package, CMake and launcher wiring\n");
  const std::string src = MUA_SRC_DIR;
  const std::string package = ReadFile(src + "/../tools/package-release.ps1");
  const std::string cmake = ReadFile(src + "/../CMakeLists.txt");
  const std::string launcher = ReadFile(src + "/launcher.cpp");
  const std::string config = ReadFile(src + "/update_config.cpp");
  const std::string readme = ReadFile(src + "/../packaging/README.txt");
  CHECK(!package.empty() && !cmake.empty() && !launcher.empty() && !config.empty() && !readme.empty());

  CHECK(Contains(package, "MarvelUltimateAlliance-v$Version-windows-x64"));
  CHECK(Contains(package, "$Name.sha256"));
  CHECK(Contains(package, "project\\(\\s*marvel_ultimate_alliance\\s+VERSION"));
  CHECK(Contains(package, "MUA_VERSION_SUFFIX"));
  for (const char* file : mua::kBundleFiles) {
    const std::string_view name = file;
    if (name == "licenses/THIRD-PARTY-LICENSES.txt") {
      CHECK(Contains(package, "'THIRD-PARTY-LICENSES.txt'"));
    } else if (name.rfind("assets/", 0) == 0) {
      std::string windows_path(name);
      std::replace(windows_path.begin(), windows_path.end(), '/', '\\');
      CHECK(Contains(package, "'" + windows_path + "'"));
    } else {
      CHECK(Contains(package, "'" + std::string(name) + "'"));
    }
  }
  for (const char* forbidden : {"'default.xex'", "'user_data'", "'game'", "'logs'", "'settings.toml'", "'cache'",
                                "'*.xex'", "'*.iso'", "'*.dmp'", "'trg-update-*'", "'*DEBUG_SAULO*'", "'mua1_gold'",
                                "'415607DA'", "'4156082F'", "LIVE", "PIRS", "$LocalPaths"}) {
    CHECK(Contains(package, forbidden));
  }

  CHECK(Contains(cmake, "add_executable(trg-updater WIN32"));
  CHECK(Contains(cmake, "src/updater/helper_main.cpp"));
  CHECK(Contains(cmake, "src/updater/install_transaction.cpp"));
  CHECK(Contains(cmake, "src/update_config.cpp"));
  CHECK(Contains(cmake, "src/updater/update_controller.cpp"));
  CHECK(Contains(cmake, "src/updater/third_party/miniz/miniz.c"));
  CHECK(Contains(cmake, "MUA_PORT_VERSION=\"${PROJECT_VERSION}${MUA_VERSION_SUFFIX}\""));
  CHECK(Contains(cmake, "OUTPUT_NAME \"trg-updater\""));

  const std::string create = "std::make_unique<trg::update::UpdateController>";
  CHECK(launcher.find(create) != std::string::npos);
  CHECK(launcher.find(create) == launcher.rfind(create));
  CHECK(launcher.find("updater_->StartCheck()") == launcher.rfind("updater_->StartCheck()"));
  CHECK(Contains(launcher, "updater_->StartCheck()"));
  CHECK(Contains(launcher, "MakeUpdateConfig(PortFolder())"));
  CHECK(Contains(launcher, "PollUpdate();"));
  CHECK(Contains(launcher, "void StopUpdater()"));
  const size_t on_close = launcher.find("void OnClose() override");
  const size_t finish = launcher.find("void Finish(std::function<void()> action)");
  CHECK(on_close != std::string::npos && launcher.find("StopUpdater();", on_close) < on_close + 120);
  CHECK(finish != std::string::npos && launcher.find("StopUpdater();", finish) < finish + 120);
  CHECK(Contains(launcher, "updater_.release()"));
  const size_t start_game = launcher.find("void StartGame()");
  const size_t start_game_end = launcher.find("// Drawn outside the disabled frame", start_game);
  CHECK(start_game != std::string::npos && start_game_end != std::string::npos);
  CHECK(!Contains(launcher.substr(start_game, start_game_end - start_game), "updater_"));
  CHECK(Contains(launcher, "result == trg::Result::kNone && !starting_"));

  CHECK(Contains(config, "MUA_UPDATE_TEST"));
  CHECK(Contains(config, "MUA_UPDATE_TEST_CURRENT_VERSION"));
  CHECK(Contains(config, "MUA_UPDATE_TEST_RELEASE_JSON"));
  CHECK(Contains(config, "MUA_UPDATE_TEST_ASSET_SHA256"));
  CHECK(Contains(config, "MUA_PORT_VERSION"));
  CHECK(Contains(config, "kBundleLauncher"));

  CHECK(Contains(readme, "@VERSION@"));
  CHECK(Contains(readme, "saulob/marvel-ultimate-alliance-recompiled"));
}

void TestNoStaleNames() {
  std::printf("no copied project names\n");
  const std::string src = MUA_SRC_DIR;
  std::vector<std::filesystem::path> files;
  for (std::filesystem::recursive_directory_iterator it(src + "/updater"), end; it != end; ++it) {
    if (it->is_regular_file() && it->path().string().find("third_party") == std::string::npos) {
      files.push_back(it->path());
    }
  }
  for (const char* extra : {"/update_config.cpp", "/update_config.h", "/update_bundle.h", "/../tools/package-release.ps1",
                            "/../packaging/README.txt", "/../tools/package-linux.sh", "/../tools/build.sh",
                            "/../tools/test.sh", "/../tools/build-patched-rexruntime.sh", "/../tools/check-guest-code.sh",
                            "/../packaging/linux/AppRun",
                            "/../packaging/linux/io.github.saulob.marvel-ultimate-alliance.desktop"}) {
    files.emplace_back(src + extra);
  }
  const std::vector<std::string> stale = {
      std::string("Geometry") + "Wars", std::string("geometry") + "_wars", std::string("GW") + "2",
      std::string("Hard") + "Corps",    std::string("hard") + "-corps",    std::string("HC") + "U",
      std::string("584108") + "FF",     std::string("Retro") + "Evolved",  std::string("SSF2") + "T",
      std::string("ssf2") + "t",        std::string("Crackdown") + "Recompiled", std::string("crackdown") + "_",
  };
  CHECK(files.size() > 20);
  for (const std::filesystem::path& file : files) {
    const std::string text = ReadFile(file);
    CHECK(!text.empty());
    for (const std::string& name : stale) {
      if (Contains(text, name)) {
        std::printf("  stale name %s in %s\n", name.c_str(), file.string().c_str());
      }
      CHECK(!Contains(text, name));
    }
  }
}

// The Linux bundle follows the release conventions of the other ports: clang 20, glibc 2.31, one AppImage named
// like the Windows package, with the launcher and the three game programs.
void TestLinuxPackaging() {
  std::printf("Linux packaging conventions\n");
  const std::string src = MUA_SRC_DIR;
  const std::string package = ReadFile(src + "/../tools/package-linux.sh");
  CHECK(Contains(package, "MAX_GLIBC=2.31"));
  CHECK(Contains(package, "NAME=\"MarvelUltimateAlliance-v$VERSION-x86_64\""));
  CHECK(Contains(package, "sha256sum \"$NAME.AppImage\" > \"$NAME.sha256\""));
  CHECK(Contains(package, "GAME_PROGRAMS=(marvel_ultimate_alliance_mua1 marvel_ultimate_alliance_mua1_gold marvel_ultimate_alliance_mua2)"));
  CHECK(Contains(package, "fail \"$f needs $glibc, above the glibc $MAX_GLIBC baseline\""));
  CHECK(Contains(package, "SDK_LINUX_ZIP_SHA256"));
  for (const char* name : {"user_data", "default.xex", "settings.toml", "*.pdb", "*.xex", "*.iso"}) {
    CHECK(Contains(package, name));
  }
  const std::string runtime = ReadFile(src + "/../tools/build-patched-rexruntime.sh");
  CHECK(Contains(runtime, "CC=clang-20"));
  CHECK(Contains(runtime, "CXX=clang++-20"));
  CHECK(Contains(runtime, "MAX_GLIBC=2.31"));
  CHECK(Contains(runtime, "\"share/rexglue/rex_app.cpp:src/ui/rex_app.cpp\""));
  const std::string presets = ReadFile(src + "/../CMakePresets.json");
  CHECK(Contains(presets, "\"CMAKE_CXX_COMPILER\": \"clang++-20\""));
  for (const char* preset : {"linux-amd64-release", "linux-amd64-relwithdebinfo", "linux-amd64-debug"}) {
    CHECK(Contains(presets, preset));
  }
  const std::string baseline = ReadFile(src + "/../patches/rexglue-sdk/baseline.env");
  CHECK(Contains(baseline, "SDK_LINUX_ZIP_SHA256=0388edb3f8fb1444f1b0ad5d440cb02d9690ac20a76b04fd6b7dcc19f59328a6"));
  const std::string appimage_runtime = ReadFile(src + "/../patches/appimage-runtime/baseline.env");
  CHECK(Contains(appimage_runtime, "RUNTIME_COMMIT=dd6cebedcbddde9c82f89b011e8e1d40b6e43868"));
  CHECK(Contains(appimage_runtime, "RUNTIME_SHA256=9e458da0e30e2dd06a206af0f345d98a872bf7c1ce69eba31d44e139488e4ab8"));
  const std::string platform = ReadFile(src + "/platform.cpp");
  CHECK(Contains(platform, "constexpr char kApplicationId[] = \"io.github.saulob.marvel-ultimate-alliance\";"));
  const std::string desktop = ReadFile(src + "/../packaging/linux/io.github.saulob.marvel-ultimate-alliance.desktop");
  CHECK(Contains(desktop, "StartupWMClass=io.github.saulob.marvel-ultimate-alliance"));
  CHECK(Contains(desktop, "Exec=marvel_ultimate_alliance"));
}

void TestAppImageTarget() {
  std::printf("the updater replaces only the AppImage the running program was started from\n");
  TempDir dir;
  const std::filesystem::path root = dir.path;
  const std::filesystem::path appimage = root / "MarvelUltimateAlliance-v0.1.0-x86_64.AppImage";
  WriteFile(appimage, "appimage");
  const std::filesystem::path mount = root / "mount";
  const std::filesystem::path program = mount / "usr" / "bin" / "marvel_ultimate_alliance";
  WriteFile(program, "program");
  CHECK(IsRunningAppImage(appimage, mount, program));
  // The game programs run from the same AppImage.
  CHECK(IsRunningAppImage(appimage, mount, mount / "usr" / "bin" / "marvel_ultimate_alliance_mua2"));

  const std::filesystem::path extracted = root / ".extract" / "squashfs-root";
  const std::filesystem::path extracted_program = extracted / "usr" / "bin" / "marvel_ultimate_alliance";
  WriteFile(extracted_program, "program");
  CHECK(IsRunningAppImage(appimage, extracted, extracted_program));

  const std::filesystem::path build = root / "build" / "marvel_ultimate_alliance";
  WriteFile(build, "program");
  CHECK(!IsRunningAppImage(appimage, mount, build));

  const std::filesystem::path other = root / "Other-v1.0.0-x86_64.AppImage";
  WriteFile(other, "other");
  CHECK(!IsRunningAppImage(other, mount, program));
  const std::filesystem::path empty_version = root / "MarvelUltimateAlliance-v-x86_64.AppImage";
  WriteFile(empty_version, "other");
  CHECK(!IsRunningAppImage(empty_version, mount, program));
  const std::filesystem::path odd_version = root / "MarvelUltimateAlliance-vx-x86_64.AppImage";
  WriteFile(odd_version, "other");
  CHECK(!IsRunningAppImage(odd_version, mount, program));

  const std::filesystem::path inside = mount / "MarvelUltimateAlliance-v0.1.0-x86_64.AppImage";
  WriteFile(inside, "appimage");
  CHECK(!IsRunningAppImage(inside, mount, program));

  CHECK(!IsRunningAppImage({}, mount, program));
  CHECK(!IsRunningAppImage(appimage, {}, program));
  CHECK(!IsRunningAppImage(appimage, mount, {}));
  CHECK(!IsRunningAppImage(appimage, root / "not-a-mount", program));

  const SemanticVersion version = *ParseVersion("v0.1.0");
  CHECK(mua::UpdateAppImageName(version) == "MarvelUltimateAlliance-v0.1.0-x86_64.AppImage");
  CHECK(mua::UpdateAppImageChecksumName(version) == "MarvelUltimateAlliance-v0.1.0-x86_64.sha256");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TestVersions();
  TestReleases();
  TestSecureUrls();
  TestSha256();
  TestZipAndStaging();
  TestZipLimitsAndMalformed();
  TestJobs();
  TestBundleInstall();
  TestRollbackAtEveryStep();
  TestManifestRules();
  TestBundleDefinitions();
  TestPaths();
  TestSourceWiring();
  TestNoStaleNames();
  TestAppImageTarget();
  TestLinuxPackaging();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
