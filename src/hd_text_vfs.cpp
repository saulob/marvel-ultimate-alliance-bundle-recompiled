#include "hd_text_vfs.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

#include "hd_text.h"

namespace mua::hd_text {
namespace {

using rex::X_STATUS;
using rex::filesystem::Entry;
using rex::filesystem::File;
using rex::filesystem::FileAccess;

constexpr uint32_t kWriteAccess = FileAccess::kGenericWrite | FileAccess::kFileWriteData | FileAccess::kFileAppendData;
constexpr uint32_t kReadAccess = FileAccess::kGenericRead | FileAccess::kFileReadData;

// Entry::children_ is protected; a member pointer named through a derived class reaches it. The archive's entry is
// swapped in place because re-registering the game volume would put it behind the null device's prefix.
struct ChildrenAccess : Entry {
  static std::vector<std::unique_ptr<Entry>> Entry::*Member() { return &ChildrenAccess::children_; }
};

class OverlayFile final : public File {
 public:
  OverlayFile(uint32_t file_access, Entry* entry, File* original, std::shared_ptr<const zip::Overlay> overlay)
      : File(file_access, entry), original_(original), overlay_(std::move(overlay)) {}
  ~OverlayFile() override { original_->Destroy(); }

  void Destroy() override { delete this; }

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset, size_t* out_bytes_read) override {
    *out_bytes_read = 0;
    if (!(file_access_ & kReadAccess)) {
      return X_STATUS_ACCESS_DENIED;
    }
    const uint64_t size = overlay_->size();
    const size_t count = byte_offset >= size ? 0 : size_t(std::min<uint64_t>(buffer.size(), size - byte_offset));
    const zip::ReadAt original = [this](uint64_t offset, std::span<uint8_t> out) {
      size_t read = 0;
      return original_->ReadSync(out, size_t(offset), &read) == X_STATUS_SUCCESS && read == out.size();
    };
    if (!zip::ReadOverlay(*overlay_, original, byte_offset, buffer.first(count))) {
      return X_STATUS_END_OF_FILE;
    }
    *out_bytes_read = count;
    return X_STATUS_SUCCESS;
  }

  X_STATUS WriteSync(std::span<const uint8_t>, size_t, size_t* out_bytes_written) override {
    *out_bytes_written = 0;
    return X_STATUS_ACCESS_DENIED;
  }

 private:
  File* original_;
  std::shared_ptr<const zip::Overlay> overlay_;
};

class OverlayEntry final : public Entry {
 public:
  OverlayEntry(std::unique_ptr<Entry> original, std::shared_ptr<const zip::Overlay> overlay)
      : Entry(original->device(), original->parent(), original->path()),
        original_(std::move(original)),
        overlay_(std::move(overlay)) {
    attributes_ = original_->attributes();
    size_ = size_t(overlay_->size());
    allocation_size_ = size_;
    create_timestamp_ = original_->create_timestamp();
    access_timestamp_ = original_->access_timestamp();
    write_timestamp_ = original_->write_timestamp();
  }

  X_STATUS Open(uint32_t desired_access, File** out_file) override {
    if (desired_access & kWriteAccess) {
      return X_STATUS_ACCESS_DENIED;
    }
    File* original = nullptr;
    const X_STATUS status = original_->Open(desired_access, &original);
    if (XFAILED(status)) {
      return status;
    }
    *out_file = new OverlayFile(desired_access, this, original, overlay_);
    return X_STATUS_SUCCESS;
  }

 private:
  std::unique_ptr<Entry> original_;
  std::shared_ptr<const zip::Overlay> overlay_;
};

}  // namespace

bool InstallHdText(rex::filesystem::VirtualFileSystem* vfs, GameId game, int render_scale, bool on) {
  const Plan plan = PlanFor(game, on, render_scale);
  if (!plan.assets || !vfs) {
    return false;
  }
  if (!on) {
    REXLOG_INFO("HD text: Off");
    return false;
  }
  if (!plan.Builds()) {
    REXLOG_INFO("HD text: On | scale=1x | original atlases");
    return false;
  }
  Entry* entry = vfs->ResolvePath(plan.assets->archive);
  if (!entry || !entry->parent()) {
    REXLOG_WARN("HD text: {} not found; original atlases", plan.assets->archive);
    return false;
  }
  File* file = nullptr;
  if (XFAILED(entry->Open(FileAccess::kGenericRead, &file)) || !file) {
    REXLOG_WARN("HD text: {} could not be opened; original atlases", plan.assets->archive);
    return false;
  }
  const zip::ReadAt read = [file](uint64_t offset, std::span<uint8_t> out) {
    size_t got = 0;
    return file->ReadSync(out, size_t(offset), &got) == X_STATUS_SUCCESS && got == out.size();
  };
  const auto start = std::chrono::steady_clock::now();
  Result result = BuildFontOverlay(read, entry->size(), plan.assets->font_set, plan.scale);
  file->Destroy();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  if (!result.overlay) {
    REXLOG_WARN("HD text: {}; original atlases", result.error);
    return false;
  }

  auto& siblings = entry->parent()->*ChildrenAccess::Member();
  const auto slot =
      std::find_if(siblings.begin(), siblings.end(), [&](const std::unique_ptr<Entry>& e) { return e.get() == entry; });
  if (slot == siblings.end()) {
    REXLOG_WARN("HD text: {} is not in its folder's entries; original atlases", plan.assets->archive);
    return false;
  }
  const uint64_t added = result.overlay->tail.size();
  auto overlay = std::make_shared<const zip::Overlay>(std::move(*result.overlay));
  *slot = std::make_unique<OverlayEntry>(std::move(*slot), std::move(overlay));
  REXLOG_INFO("{}", SummaryOn(plan.scale, result.fonts.size(), added, elapsed));
  return true;
}

}  // namespace mua::hd_text
