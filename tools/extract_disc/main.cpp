// Extracts the game filesystem of an Xbox 360 disc image (XISO, XGD1, XGD2 or XGD3) with TRG Launcher's reader.
//   extract_disc <image> <out dir>
// Prints the title ID of the image and "files directories bytes" of the extracted tree.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

#include "trg/platform.h"
#include "trg/xbox360.h"

namespace fs = std::filesystem;

namespace {

struct TreeTotals {
  uint64_t files = 0;
  uint64_t directories = 0;
  uint64_t bytes = 0;
};

TreeTotals Measure(const fs::path& root) {
  TreeTotals totals;
  for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root)) {
    if (entry.is_directory()) {
      ++totals.directories;
    } else {
      ++totals.files;
      totals.bytes += entry.file_size();
    }
  }
  return totals;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: extract_disc <image> <out dir>\n");
    return 2;
  }
  const std::string image = argv[1];
  const std::string out_dir = argv[2];
  const uint32_t title_id = trg::ReadXbox360TitleId(image);
  if (!title_id) {
    std::fprintf(stderr, "not an Xbox 360 disc image with a readable default.xex: %s\n", image.c_str());
    return 1;
  }
  std::printf("title id %08X\n", title_id);

  trg::Task task;
  task.Start("Extracting", [&](trg::Task& t) { return trg::ExtractXbox360Disc(t, image, out_dir); });
  int last_percent = -1;
  while (task.running()) {
    const int percent = int(task.fraction() * 100.0f);
    if (percent >= 0 && percent / 10 != last_percent / 10) {
      std::printf("%d%%\n", percent);
      std::fflush(stdout);
    }
    last_percent = percent;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  std::string error;
  if (task.Finish(&error) != trg::Task::State::kDone) {
    std::fprintf(stderr, "extraction failed: %s\n", error.c_str());
    return 1;
  }
  const TreeTotals totals = Measure(out_dir);
  std::printf("extracted %llu files, %llu directories, %llu bytes\n", (unsigned long long)totals.files,
              (unsigned long long)totals.directories, (unsigned long long)totals.bytes);
  return 0;
}
