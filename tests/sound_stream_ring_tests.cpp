// The sound ring hooks rely on the recompiled streamer's layout and on one return address in its consumer. Reads the
// generated code; skipped when it is not there.
// Run: mua_sound_stream_ring_tests

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

std::string Function(const fs::path& directory, const std::string& name) {
  const std::string marker = "DEFINE_REX_FUNC(" + name + ")";
  for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
    if (entry.path().extension() != ".cpp" || entry.path().filename().string().find("_recomp.") == std::string::npos) {
      continue;
    }
    std::ifstream in(entry.path(), std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    const std::string all = text.str();
    const size_t start = all.find(marker);
    if (start != std::string::npos) {
      const size_t end = all.find("\n}\n", start);
      return all.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
  }
  return {};
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

struct HostEvent {
  bool guest_signal = false;
  bool host_signal = false;
  void Set() { guest_signal = host_signal = true; }
  void GuestStoreReset() { guest_signal = false; }
  void ResetOnHost() { guest_signal = host_signal = false; }
  bool WaitWouldBlock() const { return !host_signal; }
};

}  // namespace

int Run() {
  const fs::path file = MUA_GENERATED_DIR;
  if (!fs::exists(file / "marvel_ultimate_alliance_recomp.33.cpp")) {
    std::printf("skipped: generated code is not there (%s)\n", file.string().c_str());
    return 0;
  }
  std::printf("the consumer keeps the layout the ring hooks assume\n");
  const std::string consumer = Function(file, "sub_823BE558");
  CHECK(!consumer.empty());
  CHECK(Has(consumer, "REX_LOAD_U16(ctx.r31.u32 + 2094)"));
  CHECK(Has(consumer, "REX_LOAD_U16(ctx.r31.u32 + 2092)"));
  CHECK(Has(consumer, "REX_LOAD_U32(ctx.r11.u32 + 72)"));
  CHECK(Has(consumer, "REX_LOAD_U32(ctx.r11.u32 + 260)"));
  CHECK(Has(consumer, "ctx.lr = 0x823BE6D4;\n\tsub_823BE070(ctx, base);"));
  CHECK(Has(consumer, "ctx.lr = 0x823BE5A4;\n\tsub_8282DE78(ctx, base);"));
  CHECK(Has(consumer, "REX_LOAD_U32(ctx.r31.u32 + 2116)"));
  CHECK(Has(consumer, "sub_824A6A38(ctx, base);\n\t// b 0x823be5c8\n\tgoto loc_823BE5C8;"));
  const std::string reset = Function(file, "sub_8282DE78");
  CHECK(Has(reset, "REX_STORE_U32(ctx.r3.u32 + 4, ctx.r11.u32)"));
  CHECK(!Has(reset, "__imp__"));

  const fs::path mua1 = MUA_GENERATED_DIR_MUA1;
  if (fs::exists(mua1 / "marvel_ultimate_alliance_recomp.33.cpp")) {
    std::printf("the MUA 1 consumer has the same layout and its own reset function\n");
    const std::string consumer1 = Function(mua1, "sub_823A8E58");
    CHECK(!consumer1.empty());
    CHECK(Has(consumer1, "REX_LOAD_U16(ctx.r31.u32 + 2094)"));
    CHECK(Has(consumer1, "REX_LOAD_U16(ctx.r31.u32 + 2092)"));
    CHECK(Has(consumer1, "REX_LOAD_U32(ctx.r11.u32 + 72)"));
    CHECK(Has(consumer1, "REX_LOAD_U32(ctx.r11.u32 + 260)"));
    CHECK(Has(consumer1, "REX_LOAD_U32(ctx.r31.u32 + 2116)"));
    CHECK(Has(consumer1, "sub_8248C688(ctx, base);"));
    const std::string reset1 = Function(mua1, "sub_8248C688");
    CHECK(Has(reset1, "REX_STORE_U32(ctx.r3.u32 + 4, ctx.r11.u32)"));
    CHECK(!Has(reset1, "__imp__"));
  }

  std::printf("a guest-side reset must clear the host event\n");
  HostEvent event;
  event.Set();
  event.GuestStoreReset();
  CHECK(event.WaitWouldBlock() == false);
  event.Set();
  event.ResetOnHost();
  CHECK(event.WaitWouldBlock() == true);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

int main() {
  try {
    return Run();
  } catch (const std::exception& error) {
    std::printf("  FAIL exception: %s\n", error.what());
    return 1;
  }
}
