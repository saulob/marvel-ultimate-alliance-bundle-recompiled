// D3D12 sampler parameter memoization (patches/rexglue-sdk/0078-d3d12-sampler-memo.windows.patch):
// GetSamplerParameters is called only when the active shader, the fetch constants or anisotropic_override
// changed. The memo keys and their invalidation are checked here; the command processor runs only with a
// GPU, so the patch text is inspected instead.
// Run: mua_d3d12_sampler_memo_tests

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

int Count(const std::string& text, const char* needle) {
  int count = 0;
  const size_t length = std::char_traits<char>::length(needle);
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + length)) {
    ++count;
  }
  return count;
}

// Mirrors the memo condition: the shader and the fetch generation must match, and anisotropic_override
// must be the value the cached parameters were computed with.
bool SamplersUnchanged(bool memo_enabled, const void* memo_shader, const void* shader,
                       uint64_t memo_fetch_generation, uint64_t fetch_generation,
                       int32_t memo_anisotropic_override, int32_t anisotropic_override) {
  return memo_enabled && memo_shader == shader && memo_fetch_generation == fetch_generation &&
         memo_anisotropic_override == anisotropic_override;
}

void TestMemoKey() {
  std::printf("memo key\n");
  // Unchanged shader, fetch constants and override reuse the cached parameters.
  CHECK(SamplersUnchanged(true, (const void*)1, (const void*)1, 7, 7, 3, 3));
  // A shader change invalidates.
  CHECK(!SamplersUnchanged(true, (const void*)1, (const void*)2, 7, 7, 3, 3));
  // A fetch constant change invalidates.
  CHECK(!SamplersUnchanged(true, (const void*)1, (const void*)1, 7, 8, 3, 3));
  // An anisotropic_override change invalidates.
  CHECK(!SamplersUnchanged(true, (const void*)1, (const void*)1, 7, 7, 3, 4));
  // The cvar turns it off.
  CHECK(!SamplersUnchanged(false, (const void*)1, (const void*)1, 7, 7, 3, 3));
}

void TestPatchContent() {
  std::printf("patch memo keys and invalidation\n");
  const std::string patch =
      ReadFile(std::string(MUA_SRC_DIR) +
               "/../patches/rexglue-sdk/0078-d3d12-sampler-memo.windows.patch");
  CHECK(!patch.empty());

  // The cvar and its default.
  CHECK(Has(patch, "REXCVAR_DECLARE(bool, mua_d3d12_sampler_memo);"));
  CHECK(Has(patch, "REXCVAR_DEFINE_BOOL(mua_d3d12_sampler_memo, true,"));

  // The memo state.
  CHECK(Has(patch, "uint64_t fetch_constants_generation_ = 1;"));
  CHECK(Has(patch, "const D3D12Shader* sampler_memo_shader_vertex_ = nullptr;"));
  CHECK(Has(patch, "const D3D12Shader* sampler_memo_shader_pixel_ = nullptr;"));
  CHECK(Has(patch, "uint64_t sampler_memo_fetch_generation_vertex_ = 0;"));
  CHECK(Has(patch, "uint64_t sampler_memo_fetch_generation_pixel_ = 0;"));
  CHECK(Has(patch, "int32_t sampler_memo_anisotropic_override_vertex_ = 0;"));
  CHECK(Has(patch, "int32_t sampler_memo_anisotropic_override_pixel_ = 0;"));

  // The fetch generation changes on every fetch constant write and at frame open.
  CHECK(Count(patch, "++fetch_constants_generation_;") == 3);

  // Vertex memo key and recompute guard.
  CHECK(Has(patch, "sampler_memo_shader_vertex_ == vertex_shader"));
  CHECK(Has(patch, "sampler_memo_fetch_generation_vertex_ == fetch_constants_generation_"));
  CHECK(Has(patch, "sampler_memo_anisotropic_override_vertex_ == anisotropic_override_value"));
  CHECK(Has(patch, "if (sampler_count_vertex && !samplers_vertex_unchanged) {"));
  CHECK(Has(patch, "sampler_memo_shader_vertex_ = vertex_shader;"));
  CHECK(Has(patch, "sampler_memo_fetch_generation_vertex_ = fetch_constants_generation_;"));
  CHECK(Has(patch, "sampler_memo_anisotropic_override_vertex_ = anisotropic_override_value;"));

  // Pixel memo key and recompute guard.
  CHECK(Has(patch, "sampler_memo_shader_pixel_ == pixel_shader"));
  CHECK(Has(patch, "sampler_memo_fetch_generation_pixel_ == fetch_constants_generation_"));
  CHECK(Has(patch, "sampler_memo_anisotropic_override_pixel_ == anisotropic_override_value"));
  CHECK(Has(patch, "if (sampler_count_pixel && !samplers_pixel_unchanged) {"));
  CHECK(Has(patch, "sampler_memo_shader_pixel_ = pixel_shader;"));
  CHECK(Has(patch, "sampler_memo_fetch_generation_pixel_ = fetch_constants_generation_;"));
  CHECK(Has(patch, "sampler_memo_anisotropic_override_pixel_ = anisotropic_override_value;"));

  // The memo compares only those three inputs.
  CHECK(!Has(patch, "sampler_memo_layout_uid"));
  CHECK(!Has(patch, "sampler_memo_texture"));
  CHECK(!Has(patch, "sampler_memo_clamp"));

  // Fetch writes still invalidate the constant buffer and the texture cache.
  CHECK(Has(patch, "cbuffer_binding_fetch_.up_to_date = false;"));
  CHECK(Has(patch, "texture_cache_->TextureFetchConstantWritten("));
}

}  // namespace

int main() {
  TestMemoKey();
  TestPatchContent();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
