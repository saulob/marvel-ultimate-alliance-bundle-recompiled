// The sound ring hooks rely on the recompiled streamer's layout and on one return address in its consumer. Reads the
// generated code; skipped when it is not there.
// Run: mua_sound_stream_ring_tests

#include <cstdint>
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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

size_t Count(const std::string& text, const char* needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// The string at a guest address of a decompressed image (game/analysis, built by dump_image), or the bytes of a
// big-endian pointer stored there; empty when the image is not there.
std::string ImageString(const std::string& image, uint32_t address) {
  const size_t offset = address - 0x82000000u;
  if (offset >= image.size()) {
    return {};
  }
  return std::string(image.c_str() + offset);
}

uint32_t ImageWord(const std::string& image, uint32_t address) {
  const size_t offset = address - 0x82000000u;
  if (offset + 4 > image.size()) {
    return 0;
  }
  const auto byte = [&](size_t i) { return uint32_t(uint8_t(image[offset + i])); };
  return byte(0) << 24 | byte(1) << 16 | byte(2) << 8 | byte(3);
}

// Gold Edition's sound files go through two extra roots MUA1 does not have, update: (a title update package) after D:
// for resident banks and muadlc: (installed Marketplace DLC) before D: for streams. Neither exists without such a
// package, on the console as here, and both lookups end on the disc, which holds every Gold Edition sound file.
void CheckGoldSoundRoots(const fs::path& gold, const fs::path& mua1) {
  std::printf("Gold Edition audio hooks and sound file roots\n");
  const std::string render = Function(gold, "sub_824AF110");
  CHECK(Has(render, "mua_audio_pcm_firewall(ctx.r4);\n\tctx.lr = 0x824AF15C;\n"
                    "\t__imp__XAudioSubmitRenderDriverFrame(ctx, base);"));
  CHECK(Has(Function(gold, "sub_824A6BA8"), "mua_audio_barrier_escape()"));

  const std::string stream = Function(gold, "sub_823BD4D8");
  CHECK(Has(stream, "// lis r11,-32252\n") && Has(stream, "// addi r11,r11,-14512\n"));
  CHECK(Count(stream, "\tsub_8247F288(ctx, base);") == 2);
  const size_t first_open = stream.find("\tsub_8247F288(ctx, base);");
  const size_t failed = stream.find("// cmpwi cr6,r3,-1\n", first_open);
  const size_t fallback = stream.find("// addi r9,r10,27136\n", failed);
  CHECK(failed != std::string::npos && fallback != std::string::npos);
  CHECK(fallback < stream.find("\tsub_8247F288(ctx, base);", first_open + 1));
  CHECK(Has(stream, "// addi r9,r11,-15436\n"));
  CHECK(Has(Function(gold, "sub_823B7F48"), "// addi r10,r10,27136\n"));

  const std::string media = Function(gold, "sub_82362218");
  CHECK(Has(media, "// addi r9,r11,4748\n") && Has(media, "// lwz r9,-7404(r11)\n") &&
        Has(media, "// addi r9,r11,4744\n"));
  const size_t set_roots = media.find("\tsub_8257C818(ctx, base);");
  CHECK(set_roots != std::string::npos && set_roots < media.find("\tsub_82361EE0(ctx, base);"));

  if (fs::exists(mua1 / "marvel_ultimate_alliance_recomp.33.cpp")) {
    const std::string stream1 = Function(mua1, "sub_823A7E68");
    CHECK(Count(stream1, "\tsub_82466E38(ctx, base);") == 1);
    CHECK(Has(stream1, "// addi r9,r10,14440\n"));
    CHECK(Has(Function(mua1, "sub_823A28D0"), "// addi r10,r10,14440\n"));
    CHECK(Has(Function(mua1, "sub_82494B40"), "mua_audio_pcm_firewall(ctx.r4);\n\tctx.lr = 0x82494B74;\n"
                                              "\t__imp__XAudioSubmitRenderDriverFrame(ctx, base);"));
    CHECK(Has(Function(mua1, "sub_8248C808"), "mua_audio_barrier_escape()"));
  }

  const std::string image = ReadFile(std::string(MUA_SRC_DIR) + "/../game/analysis/default.image.bin");
  if (image.empty()) {
    std::printf("  Gold Edition image not dumped, strings skipped\n");
    return;
  }
  CHECK(ImageString(image, 0x8203C750) == "muadlc:\\");
  CHECK(ImageString(image, 0x8203C3B4) == "\\");
  CHECK(ImageString(image, 0x8203128C) == ",");
  CHECK(ImageString(image, 0x82031288) == ":");
  CHECK(ImageString(image, ImageWord(image, 0x828AE314)) == "update");
  CHECK(ImageString(image, 0x82031270) == "muadlc_titleupdate.zip");
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

  const fs::path mua2 = MUA_GENERATED_DIR_MUA2;
  if (fs::exists(mua2 / "marvel_ultimate_alliance_recomp.33.cpp")) {
    std::printf("MUA 2 has no inline event reset and no mix worker rendezvous to hook\n");
    const std::string render = Function(mua2, "sub_82A25E50");
    CHECK(!render.empty());
    CHECK(Has(render, "__imp__KeWaitForMultipleObjects(ctx, base);"));
    CHECK(Has(render, "__imp__XAudioSubmitRenderDriverFrame(ctx, base);"));
    CHECK(Has(render, "mua_audio_pcm_firewall(ctx.r4);\n\tctx.lr = 0x82A25F18;"));
    CHECK(Has(render, "REX_LOAD_U32(ctx.r31.u32 + 32)"));
    const std::string map = ReadFile(std::string(MUA_SRC_DIR) + "/guest_map_mua2.h");
    CHECK(Has(map, "#define MUA_GUEST_HAS_RESET_EVENT 0"));
    CHECK(Has(map, "#define MUA_GUEST_HAS_AUDIO_BARRIER 0"));
    CHECK(Has(map, "#define MUA_GUEST_AUDIO_RENDER sub_82A25E50"));
    const std::string overrides = ReadFile(std::string(MUA_SRC_DIR) + "/../config/mua2/overrides.toml");
    CHECK(Has(overrides, "address = 0x82A25F14"));
    CHECK(Has(overrides, "name = \"mua_audio_pcm_firewall\""));
    CHECK(!Has(overrides, "mua_audio_barrier_escape"));
  }

  CheckGoldSoundRoots(file, mua1);

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
