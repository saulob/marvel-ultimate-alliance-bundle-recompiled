// Update controller tests: the whole check, download and verify flow against a fake release and a server on the
// loopback address, up to the point where the helper would start. Nothing leaves this machine, no window opens and
// no helper or game program is launched. Scratch folders only.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "update_bundle.h"
#include "updater/sha256.h"
#include "updater/third_party/miniz/miniz.h"
#include "updater/update_controller.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"

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
    path = std::filesystem::temp_directory_path() / ("mua-controller-tests-" + std::to_string(now));
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
    mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION);
  }
  void* data = nullptr;
  size_t size = 0;
  mz_zip_writer_finalize_heap_archive(&archive, &data, &size);
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

struct Route {
  std::string body;
  int status = 200;
  size_t claimed_extra = 0;  // bytes the response promises beyond the body
  bool stall = false;        // send half of the body, then wait until released
};

class LoopbackServer {
 public:
  LoopbackServer() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    listen(listener_, 8);
    int size = sizeof(address);
    getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }
  ~LoopbackServer() {
    stop_ = true;
    release_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
    closesocket(listener_);
    WSACleanup();
  }

  std::string Url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }
  void Set(const std::string& path, Route route) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_[path] = std::move(route);
  }
  void Release() { release_ = true; }
  int requests() const { return requests_; }
  bool stalled() const { return stalled_; }

 private:
  void Serve() {
    while (!stop_) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(listener_, &set);
      timeval timeout{0, 50000};
      if (select(0, &set, nullptr, nullptr, &timeout) <= 0) {
        continue;
      }
      SOCKET client = accept(listener_, nullptr, nullptr);
      if (client == INVALID_SOCKET) {
        continue;
      }
      Handle(client);
      closesocket(client);
    }
  }

  void Handle(SOCKET client) {
    std::string request;
    char buffer[2048];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const int received = recv(client, buffer, sizeof(buffer), 0);
      if (received <= 0) {
        return;
      }
      request.append(buffer, size_t(received));
    }
    ++requests_;
    const size_t start = request.find(' ');
    const size_t end = request.find(' ', start + 1);
    const std::string path = request.substr(start + 1, end - start - 1);
    Route route;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto found = routes_.find(path);
      route = found == routes_.end() ? Route{"not found", 404} : found->second;
    }
    const std::string head = "HTTP/1.1 " + std::to_string(route.status) + (route.status == 200 ? " OK" : " Error") +
                             "\r\nContent-Length: " + std::to_string(route.body.size() + route.claimed_extra) +
                             "\r\nConnection: close\r\n\r\n";
    SendAll(client, head);
    if (route.stall) {
      SendAll(client, route.body.substr(0, route.body.size() / 2));
      stalled_ = true;
      for (int waited = 0; !release_ && !stop_ && waited < 400; ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      return;
    }
    SendAll(client, route.body);
  }

  static void SendAll(SOCKET client, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
      const int result = send(client, data.data() + sent, int(data.size() - sent), 0);
      if (result <= 0) {
        return;
      }
      sent += size_t(result);
    }
  }

  SOCKET listener_ = INVALID_SOCKET;
  unsigned short port_ = 0;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> release_{false};
  std::atomic<bool> stalled_{false};
  std::atomic<int> requests_{0};
  std::mutex mutex_;
  std::map<std::string, Route> routes_;
};

struct AssetSpec {
  std::string name;
  std::string url;
  size_t size = 0;
  std::string digest;
};

std::string ReleaseJson(const std::string& tag, bool draft, bool prerelease, const std::vector<AssetSpec>& assets) {
  std::string json = "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") +
                     ",\"prerelease\":" + (prerelease ? "true" : "false") +
                     ",\"html_url\":\"https://github.com/saulob/marvel-ultimate-alliance-recompiled/releases/tag/" + tag +
                     "\",\"assets\":[";
  for (size_t i = 0; i < assets.size(); ++i) {
    const AssetSpec& a = assets[i];
    json += std::string(i ? "," : "") + "{\"name\":\"" + a.name + "\",\"browser_download_url\":\"" + a.url +
            "\",\"size\":" + std::to_string(a.size);
    if (!a.digest.empty()) {
      json += ",\"digest\":\"" + a.digest + "\"";
    }
    json += "}";
  }
  return json + "]}";
}

constexpr const char* kOldBundle = "old";

// An installation with a placeholder helper and the files an update must never touch.
void MakeInstall(const std::filesystem::path& install) {
  for (const char* file : mua::kBundleFiles) {
    WriteFile(install / std::filesystem::path(file), std::string(kOldBundle) + ":" + file);
  }
  WriteFile(install / "user_data" / "settings.toml", "player settings");
  WriteFile(install / "user_data" / "mua2" / "saves" / "slot0.sav", "mua2 save");
  WriteFile(install / "game" / "mua1-gold" / "default.xex", "gold game");
}

bool InstallUntouched(const std::filesystem::path& install) {
  for (const char* file : mua::kBundleFiles) {
    if (ReadFile(install / std::filesystem::path(file)) != std::string(kOldBundle) + ":" + file) {
      return false;
    }
  }
  return ReadFile(install / "user_data" / "settings.toml") == "player settings" &&
         ReadFile(install / "user_data" / "mua2" / "saves" / "slot0.sav") == "mua2 save" &&
         ReadFile(install / "game" / "mua1-gold" / "default.xex") == "gold game";
}

std::vector<std::filesystem::path> UpdateWorkspaces() {
  std::vector<std::filesystem::path> found;
  const std::string prefix = "trg-update-" + std::to_string(CurrentProcessId()) + "-";
  std::error_code ec;
  for (std::filesystem::directory_iterator it(std::filesystem::temp_directory_path(), ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->path().filename().string().rfind(prefix, 0) == 0) {
      found.push_back(it->path());
    }
  }
  return found;
}

struct Scenario {
  TempDir temp;
  LoopbackServer server;
  std::filesystem::path install = temp.path / "install";
  std::filesystem::path release_json = temp.path / "release.json";
  std::string zip = MakeZip(BundleEntries("new"));
  std::string current = "0.1.0-dev";
  std::string tag = "v0.2.0";
  bool draft = false;
  bool prerelease = false;
  bool loopback_allowed = true;
  std::optional<std::string> sha_override;
  std::string zip_digest = "sha256:" + Sha256Hex(zip);
  std::string checksum_body;
  bool publish_checksum_asset = false;
  size_t listed_size = 0;

  Scenario() { MakeInstall(install); }

  std::string AssetName() const { return mua::UpdateAssetName(*ParseVersion(tag)); }
  std::string ChecksumName() const { return mua::UpdateChecksumAssetName(*ParseVersion(tag)); }

  void Publish(const std::string& windows_name = {}) {
    const std::string name = windows_name.empty() ? AssetName() : windows_name;
    server.Set("/package.zip", {zip});
    std::vector<AssetSpec> assets;
    assets.push_back({name, server.Url("/package.zip"), listed_size ? listed_size : zip.size(), zip_digest});
    assets.push_back({"MarvelUltimateAlliance-" + tag + "-linux-x86_64.tar.gz", server.Url("/linux.tar.gz"), 10, ""});
    if (publish_checksum_asset) {
      server.Set("/package.sha256", {checksum_body});
      assets.push_back({ChecksumName(), server.Url("/package.sha256"), checksum_body.size(), ""});
    }
    WriteFile(release_json, ReleaseJson(tag, draft, prerelease, assets));
  }

  UpdateConfig Config() const {
    UpdateConfig config;
    config.current_version = current;
    config.releases_url = mua::kUpdateReleasesUrl;
    config.user_agent = "MarvelUltimateAlliance-Updater/test";
    config.package_kind = UpdatePackageKind::kZip;
    config.asset_name = mua::UpdateAssetName;
    config.checksum_asset_name = mua::UpdateChecksumAssetName;
    config.install_dir = install;
    config.restart_executable = install / mua::kBundleLauncher;
    config.restart_arguments = mua::UpdateRestartArguments({"--mua_skip_launcher=true", "--user_data_root=C:/data"});
    config.required_staged_files.assign(mua::kBundleFiles.begin(), mua::kBundleFiles.end());
    config.install_supported = std::filesystem::exists(install / mua::kUpdateHelper);
    config.release_json_override = release_json;
    config.allow_loopback_http = loopback_allowed;
    config.sha256_override = sha_override;
    return config;
  }
};

bool Settle(UpdateController& controller, int timeout_ms = 20000) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    controller.Poll();
    const UpdateState state = controller.snapshot().state;
    if (state != UpdateState::kChecking && state != UpdateState::kDownloading && state != UpdateState::kVerifying) {
      return true;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

UpdateState CheckState(Scenario& s, bool manual = false) {
  UpdateController controller(s.Config());
  controller.StartCheck(manual);
  CHECK(Settle(controller));
  return controller.snapshot().state;
}

void TestCheck() {
  std::printf("check\n");
  {
    Scenario s;
    s.Publish();
    UpdateController controller(s.Config());
    CHECK(controller.snapshot().state == UpdateState::kIdle);
    controller.StartCheck();
    CHECK(controller.snapshot().state == UpdateState::kChecking);
    controller.StartCheck();
    CHECK(Settle(controller));
    const UpdateSnapshot& snapshot = controller.snapshot();
    CHECK(snapshot.state == UpdateState::kAvailable);
    CHECK(snapshot.latest_version == "0.2.0");
    CHECK(snapshot.current_version == "0.1.0-dev");
    CHECK(snapshot.asset_name == "MarvelUltimateAlliance-v0.2.0-windows-x64.zip");
    CHECK(snapshot.release_url.rfind("https://github.com/saulob/marvel-ultimate-alliance-recompiled/", 0) == 0);
    CHECK(snapshot.install_supported);
    CHECK(s.server.requests() == 0);
    controller.StartCheck();
    CHECK(controller.snapshot().state == UpdateState::kAvailable);
  }
  for (const char* current : {"0.2.0", "0.2.1", "1.0.0", "v0.2.0"}) {
    Scenario s;
    s.current = current;
    s.Publish();
    CHECK(CheckState(s) == UpdateState::kNoUpdate);
  }
  {
    Scenario s;
    s.current = "0.1.0";
    s.Publish();
    CHECK(CheckState(s) == UpdateState::kAvailable);
  }
  {
    Scenario s;
    s.draft = true;
    s.Publish();
    CHECK(CheckState(s) == UpdateState::kNoUpdate);
  }
  {
    Scenario s;
    s.prerelease = true;
    s.Publish();
    CHECK(CheckState(s) == UpdateState::kNoUpdate);
  }
  {
    Scenario s;
    s.tag = "v0.2.0-rc1";
    s.Publish();
    CHECK(CheckState(s) == UpdateState::kNoUpdate);
  }
  {
    Scenario s;
    s.current = "not a version";
    s.Publish();
    UpdateController controller(s.Config());
    CHECK(controller.snapshot().state == UpdateState::kFailed);
    controller.StartCheck(true);
    CHECK(controller.snapshot().state == UpdateState::kFailed);
  }
  {
    Scenario s;
    s.Publish("MarvelUltimateAlliance-v0.2.0-linux-x64.zip");
    CHECK(CheckState(s) == UpdateState::kIdle);
    UpdateController manual(s.Config());
    manual.StartCheck(true);
    CHECK(Settle(manual));
    CHECK(manual.snapshot().state == UpdateState::kFailed);
    CHECK(Contains(manual.snapshot().error, "MarvelUltimateAlliance-v0.2.0-windows-x64.zip"));
    CHECK(manual.snapshot().retry_available);
    CHECK(manual.snapshot().retry_check);
  }
  {
    Scenario s;
    WriteFile(s.release_json, "{ not json");
    CHECK(CheckState(s) == UpdateState::kIdle);
    UpdateController manual(s.Config());
    manual.StartCheck(true);
    CHECK(Settle(manual));
    CHECK(manual.snapshot().state == UpdateState::kFailed);
    CHECK(!manual.snapshot().error.empty());
    WriteFile(s.release_json, ReleaseJson("not-a-version", false, false, {}));
    CHECK(CheckState(s) == UpdateState::kIdle);
  }
  {
    Scenario s;
    s.listed_size = 0;
    s.Publish();
    s.loopback_allowed = false;
    UpdateController manual(s.Config());
    manual.StartCheck(true);
    CHECK(Settle(manual));
    CHECK(manual.snapshot().state == UpdateState::kFailed);
    CHECK(Contains(manual.snapshot().error, "not secure"));
  }
  {
    Scenario s;
    s.Publish();
    std::filesystem::remove(s.release_json);
    CHECK(CheckState(s) == UpdateState::kIdle);
  }
  CHECK(UpdateWorkspaces().empty());
}

UpdateState Download(Scenario& s, std::string* error = nullptr, bool* installed_untouched = nullptr) {
  UpdateController controller(s.Config());
  controller.StartCheck();
  CHECK(Settle(controller));
  if (controller.snapshot().state != UpdateState::kAvailable) {
    if (error) {
      *error = "not available";
    }
    return controller.snapshot().state;
  }
  controller.StartDownload();
  CHECK(Settle(controller));
  if (error) {
    *error = controller.snapshot().error;
  }
  if (installed_untouched) {
    *installed_untouched = InstallUntouched(s.install);
  }
  return controller.snapshot().state;
}

void TestDownloadVerifiedAndStaged() {
  std::printf("download, verify and stage\n");
  Scenario s;
  s.Publish();
  UpdateController controller(s.Config());
  controller.StartCheck();
  CHECK(Settle(controller));
  CHECK(controller.snapshot().state == UpdateState::kAvailable);
  controller.StartDownload();
  CHECK(controller.snapshot().state == UpdateState::kDownloading);
  CHECK(Settle(controller));
  CHECK(controller.snapshot().state == UpdateState::kReadyToRestart);
  CHECK(controller.snapshot().progress == 1.0f);
  CHECK(InstallUntouched(s.install));
  CHECK(s.server.requests() >= 1);

  const std::vector<std::filesystem::path> workspaces = UpdateWorkspaces();
  CHECK(workspaces.size() == 1);
  if (workspaces.size() == 1) {
    const std::filesystem::path workspace = workspaces[0];
    CHECK(!std::filesystem::equivalent(workspace, s.install));
    for (const char* file : mua::kBundleFiles) {
      CHECK(ReadFile(workspace / "staging" / std::filesystem::path(file)) == std::string("new:") + file);
    }
    const std::string manifest = ReadFile(workspace / "manifest.txt");
    for (const char* file : mua::kBundleFiles) {
      CHECK(Contains(manifest, std::string(file) + "\n"));
    }
    CHECK(!Contains(manifest, "user_data"));
    CHECK(!Contains(manifest, "default.xex"));

    // The helper cannot start from the placeholder, so the job it would have received is what is left to read.
    CHECK(!controller.BeginInstall());
    CHECK(controller.snapshot().state == UpdateState::kFailed);
    CHECK(controller.RetryNeedsInstall());
    CHECK(controller.snapshot().retry_available);
    CHECK(ReadFile(workspace / "trg-updater.exe") == std::string(kOldBundle) + ":trg-updater.exe");
    const std::string job = ReadFile(workspace / "job.txt");
    CHECK(Contains(job, "\nrestart=" + Utf8Path(s.install / "marvel_ultimate_alliance.exe") + "\n"));
    CHECK(!Contains(job, "restart=" + Utf8Path(s.install / "marvel_ultimate_alliance_mua")));
    CHECK(Contains(job, "install_dir=" + Utf8Path(s.install)));
    CHECK(Contains(job, "staging_dir=" + Utf8Path(workspace / "staging")));
    CHECK(Contains(job, "parent_pid=" + std::to_string(CurrentProcessId())));
    CHECK(Contains(job, "\narg=--mua_launcher=true\n"));
    CHECK(Contains(job, "\narg=--user_data_root=C:/data\n"));
    CHECK(!Contains(job, "--mua_skip_launcher"));
    CHECK(InstallUntouched(s.install));
  }
}

void TestIntegrity() {
  std::printf("integrity\n");
  std::string error;
  bool untouched = false;
  {
    Scenario s;
    s.Publish();
    CHECK(Download(s) == UpdateState::kReadyToRestart);
  }
  {
    Scenario s;
    std::string upper = Sha256Hex(s.zip);
    for (char& c : upper) {
      c = char(std::toupper(static_cast<unsigned char>(c)));
    }
    s.zip_digest = "sha256:" + upper;
    s.Publish();
    CHECK(Download(s) == UpdateState::kReadyToRestart);
  }
  {
    Scenario s;
    s.zip_digest = "sha256:" + std::string(64, '0');
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "SHA-256"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip_digest = "sha256:abc123";
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "trusted SHA-256"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip_digest.clear();
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "trusted SHA-256"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip_digest.clear();
    s.publish_checksum_asset = true;
    s.checksum_body = Sha256Hex(s.zip) + "  " + s.AssetName() + "\n";
    s.Publish();
    CHECK(Download(s) == UpdateState::kReadyToRestart);
  }
  {
    Scenario s;
    s.zip_digest.clear();
    s.publish_checksum_asset = true;
    s.checksum_body = std::string(64, 'a') + "  " + s.AssetName() + "\n";
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "SHA-256"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip_digest.clear();
    s.publish_checksum_asset = true;
    s.checksum_body = "garbage\n";
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "not valid"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.sha_override = Sha256Hex(s.zip);
    s.zip_digest = "sha256:" + std::string(64, '1');
    s.Publish();
    CHECK(Download(s) == UpdateState::kReadyToRestart);
  }
  {
    Scenario s;
    s.sha_override = std::string(64, 'f');
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "SHA-256"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.sha_override = "not-a-hash";
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(Contains(error, "test checksum"));
    CHECK(untouched);
  }
  CHECK(UpdateWorkspaces().empty());
}

void TestBadPackages() {
  std::printf("bad packages\n");
  std::string error;
  bool untouched = false;
  const auto run = [&](Scenario& s) {
    s.zip_digest = "sha256:" + Sha256Hex(s.zip);
    s.Publish();
    return Download(s, &error, &untouched);
  };
  {
    Scenario s;
    auto entries = BundleEntries("new");
    entries.erase(entries.begin() + 2);
    s.zip = MakeZip(entries);
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "marvel_ultimate_alliance_mua1_gold.exe"));
    CHECK(untouched);
  }
  {
    Scenario s;
    auto entries = BundleEntries("new");
    entries.erase(entries.begin());
    s.zip = MakeZip(entries);
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "marvel_ultimate_alliance.exe"));
    CHECK(untouched);
  }
  {
    Scenario s;
    auto entries = BundleEntries("new");
    entries.emplace_back("../escape.exe", "bad");
    s.zip = MakeZip(entries);
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "unsafe path"));
    CHECK(untouched);
    CHECK(!std::filesystem::exists(s.temp.path / "escape.exe"));
  }
  {
    Scenario s;
    auto entries = BundleEntries("new");
    entries.emplace_back("user_data/settings.toml", "overwrite");
    s.zip = MakeZip(entries);
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "protected"));
    CHECK(untouched);
  }
  {
    Scenario s;
    auto entries = BundleEntries("new");
    entries.emplace_back("game/mua2/default.xex", "overwrite");
    s.zip = MakeZip(entries);
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "protected"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip = "this is not a zip archive";
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(Contains(error, "ZIP"));
    CHECK(untouched);
  }
  {
    Scenario s;
    s.zip = MakeZip({});
    CHECK(run(s) == UpdateState::kFailed);
    CHECK(untouched);
  }
  CHECK(UpdateWorkspaces().empty());
}

void TestTransferFailures() {
  std::printf("transfer failures\n");
  std::string error;
  bool untouched = false;
  {
    Scenario s;
    s.Publish();
    const size_t half = s.zip.size() / 2;
    s.server.Set("/package.zip", {s.zip.substr(0, half), 200, s.zip.size() - half});
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(!error.empty());
    CHECK(untouched);
  }
  {
    Scenario s;
    s.Publish();
    s.server.Set("/package.zip", {s.zip.substr(0, s.zip.size() / 2), 200, 0});
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(untouched);
  }
  {
    Scenario s;
    s.Publish();
    s.server.Set("/package.zip", {"server error", 500});
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(!error.empty());
    CHECK(untouched);
  }
  {
    Scenario s;
    s.listed_size = s.zip.size() + 7;
    s.Publish();
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(untouched);
  }
  {
    Scenario s;
    s.Publish();
    UpdateController controller(s.Config());
    controller.StartCheck();
    CHECK(Settle(controller));
    CHECK(controller.snapshot().state == UpdateState::kAvailable);
    s.server.Set("/package.zip", {"server error", 500});
    controller.StartDownload();
    CHECK(Settle(controller));
    CHECK(controller.snapshot().state == UpdateState::kFailed);
    CHECK(controller.snapshot().retry_available);
    CHECK(!controller.snapshot().retry_check);
    CHECK(!controller.RetryNeedsInstall());
    s.server.Set("/package.zip", {s.zip});
    CHECK(!controller.Retry());
    CHECK(Settle(controller));
    CHECK(controller.snapshot().state == UpdateState::kReadyToRestart);
    CHECK(InstallUntouched(s.install));
  }
  {
    Scenario s;
    s.Publish();
    WriteFile(s.release_json,
              ReleaseJson("v0.2.0", false, false,
                          {{s.AssetName(), "http://127.0.0.1:1/package.zip", s.zip.size(), s.zip_digest}}));
    CHECK(Download(s, &error, &untouched) == UpdateState::kFailed);
    CHECK(!error.empty());
    CHECK(untouched);
  }
  CHECK(UpdateWorkspaces().empty());
}

void TestCancel() {
  std::printf("cancel\n");
  {
    Scenario s;
    s.listed_size = 200000 + s.zip.size();
    s.Publish();
    s.server.Set("/package.zip", {std::string(200000, 'z') + s.zip, 200, 0, true});
    UpdateController controller(s.Config());
    controller.StartCheck();
    CHECK(Settle(controller));
    controller.StartDownload();
    for (int waited = 0; !s.server.stalled() && waited < 400; ++waited) {
      controller.Poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(s.server.stalled());
    CHECK(controller.Busy());
    controller.Cancel();
    s.server.Release();
    CHECK(Settle(controller));
    CHECK(!controller.Busy());
    CHECK(controller.snapshot().state == UpdateState::kFailed);
    CHECK(!controller.BeginInstall());
    CHECK(InstallUntouched(s.install));
    CHECK(UpdateWorkspaces().empty());
  }
  {
    Scenario s;
    s.Publish();
    UpdateController controller(s.Config());
    controller.StartCheck();
    controller.Cancel();
    CHECK(Settle(controller));
    CHECK(!controller.Busy());
  }
  {
    // Leaving the launcher mid-download destroys the controller; the stalled transfer must not block it for long.
    Scenario s;
    s.listed_size = 200000;
    s.Publish();
    s.server.Set("/package.zip", {std::string(200000, 'z'), 200, 0, true});
    const auto start = std::chrono::steady_clock::now();
    {
      UpdateController controller(s.Config());
      controller.StartCheck();
      CHECK(Settle(controller));
      controller.StartDownload();
      for (int waited = 0; !s.server.stalled() && waited < 400; ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      controller.Cancel();
      s.server.Release();
    }
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(15));
    CHECK(InstallUntouched(s.install));
  }
  CHECK(UpdateWorkspaces().empty());
}

void TestUnsupportedInstall() {
  std::printf("missing helper\n");
  Scenario s;
  s.Publish();
  std::filesystem::remove(s.install / mua::kUpdateHelper);
  UpdateConfig config = s.Config();
  config.install_unsupported_reason = "The update helper is missing from this installation.";
  UpdateController controller(config);
  controller.StartCheck();
  CHECK(Settle(controller));
  CHECK(controller.snapshot().state == UpdateState::kAvailable);
  CHECK(!controller.snapshot().install_supported);
  controller.StartDownload();
  CHECK(controller.snapshot().state == UpdateState::kFailed);
  CHECK(Contains(controller.snapshot().error, "helper"));
  CHECK(!controller.BeginInstall());
  CHECK(s.server.requests() == 0);
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  TestCheck();
  TestDownloadVerifiedAndStaged();
  TestIntegrity();
  TestBadPackages();
  TestTransferFailures();
  TestCancel();
  TestUnsupportedInstall();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
