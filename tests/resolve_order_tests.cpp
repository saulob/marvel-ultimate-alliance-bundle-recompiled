// Resolve ordering in the patched Direct3D 12 plugin source: a resolve is recorded in an open submission, its clear
// waits for the copy through a state transition, and the resolved range is marked GPU-written before textures
// sample it. Reads the staged ReXGlue source; skipped when it is not there.
// Run: mua_resolve_order_tests

#include <cstdio>
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

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  std::string result = text.str();
  std::erase(result, '\r');
  return result;
}

size_t Find(const std::string& text, const std::string& needle, size_t from = 0) {
  return text.find(needle, from);
}

std::string Body(const std::string& text, const std::string& signature) {
  const size_t start = Find(text, signature);
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = Find(text, "\n}\n", start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

void TestResolveBody(const fs::path& source) {
  std::printf("a resolve copies, marks the range written, then clears behind a state transition\n");
  const std::string cache = Read(source / "src/graphics/d3d12/render_target_cache.cpp");
  const std::string resolve = Body(cache, "bool D3D12RenderTargetCache::Resolve(");
  CHECK(!resolve.empty());
  const size_t dispatch = Find(resolve, "D3DDispatch(copy_group_count_x");
  const size_t mark = Find(resolve, "MarkRangeAsResolved(");
  const size_t clearing = Find(resolve, "// Clearing.");
  const size_t rov_transition = Find(resolve, "TransitionEdramBuffer(D3D12_RESOURCE_STATE_UNORDERED_ACCESS)", clearing);
  const size_t rov_clear = Find(resolve, "resolve_rov_clear_root_signature_", clearing);
  CHECK(dispatch != std::string::npos && mark != std::string::npos && clearing != std::string::npos);
  CHECK(dispatch < mark);
  CHECK(mark < clearing);
  CHECK(rov_transition != std::string::npos && rov_clear != std::string::npos && rov_transition < rov_clear);
  CHECK(Find(resolve, "TransitionEdramBuffer(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)") < dispatch);
}

void TestCommandProcessor(const fs::path& source) {
  std::printf("copies and swaps open a submission before touching resources\n");
  const std::string processor = Read(source / "src/graphics/d3d12/command_processor.cpp");
  const std::string copy = Body(processor, "bool D3D12CommandProcessor::IssueCopy()");
  const size_t copy_begin = Find(copy, "BeginSubmission(true)");
  const size_t copy_resolve = Find(copy, "render_target_cache_->Resolve(");
  CHECK(copy_begin != std::string::npos && copy_resolve != std::string::npos && copy_begin < copy_resolve);
  const std::string swap = Body(processor, "void D3D12CommandProcessor::IssueSwap(");
  const size_t swap_begin = Find(swap, "BeginSubmission(true)");
  const size_t swap_texture = Find(swap, "RequestSwapTexture(");
  const size_t swap_refresh = Find(swap, "presenter->RefreshGuestOutput(");
  CHECK(swap_begin != std::string::npos && swap_texture != std::string::npos);
  CHECK(swap_begin < swap_texture && swap_texture < swap_refresh);
}

void TestTextureVisibility(const fs::path& source) {
  std::printf("a resolved range is written to shared memory before textures use it\n");
  const std::string texture_cache = Read(source / "src/graphics/pipeline/texture/cache.cpp");
  const std::string mark = Body(texture_cache, "void TextureCache::MarkRangeAsResolved(");
  CHECK(!mark.empty() && Find(mark, "shared_memory().RangeWrittenByGpu(") != std::string::npos);
}

}  // namespace

int main() {
  const fs::path source = MUA_SDK_SOURCE_DIR;
  if (!fs::exists(source / "src/graphics/d3d12/render_target_cache.cpp")) {
    std::printf("skipped: the patched ReXGlue source is not staged (%s)\n", source.string().c_str());
    return 0;
  }
  TestResolveBody(source);
  TestCommandProcessor(source);
  TestTextureVisibility(source);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
