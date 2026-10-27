// NVIDIA DLSS and AMD FSR 3.1 per game: patches/rexglue-sdk/0166 runs the scene upscaler only for a game whose renderer
// has a scene profile (d3d12_scene_profile), and MUA 1's profile keeps 0161's scene rule. 0167 gives Gold Edition its
// own, which also takes its Direct3D's 4x MSAA draws over the scene's memory into the scene's pass. Both games offer
// both upscalers; MUA 2 has no profile yet and keeps Off and FSR 1. The app passes the profile only with DLSS or
// FSR 3.1 chosen, the launcher's choices follow each game's own list, each game keeps its own settings and runs in its
// own program, and the three games share one copy of each upscaler runtime.
// Run: mua_scene_profile_tests

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "game_definition.h"
#include "update_bundle.h"
#include "upscaling.h"

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
  std::string content = text.str();
  std::erase(content, '\r');
  return content;
}

bool Has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, std::string_view needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
    ++count;
  }
  return count;
}

const std::string kRoot = std::string(MUA_SRC_DIR) + "/../";
const std::string kPatches = kRoot + "patches/rexglue-sdk/";
constexpr char kProfilePatch[] = "0166-d3d12-scene-upscaler-profiles.windows.patch";
constexpr char kGoldPatch[] = "0167-d3d12-scene-upscaler-gold-profile.windows.patch";
constexpr char kScenePatch[] = "0161-d3d12-dlss-scene-upscaler.windows.patch";

// The lines of a patch with the given prefix ('+' added, '-' removed), without the file headers.
std::string Lines(const std::string& patch, char prefix) {
  std::string lines;
  std::istringstream in(patch);
  for (std::string line; std::getline(in, line);) {
    if (line.size() > 1 && line[0] == prefix && line.rfind("+++", 0) != 0 && line.rfind("---", 0) != 0) {
      lines += line.substr(1) + "\n";
    }
  }
  return lines;
}

const mua::GameDefinition& Mua1() { return mua::Game(mua::GameId::kMua1); }
const mua::GameDefinition& Gold() { return mua::Game(mua::GameId::kMua1Gold); }
const mua::GameDefinition& Mua2() { return mua::Game(mua::GameId::kMua2); }

void TestGames() {
  std::printf("MUA 1 and Gold Edition have their own scene profiles and offer FSR 3.1 and DLSS; MUA 2 has neither\n");
  CHECK(Mua1().scene_profile == "mua1" && Gold().scene_profile == "mua1-gold" && Mua2().scene_profile.empty());
  for (const mua::GameDefinition* game : {&Mua1(), &Gold()}) {
    CHECK(game->upscalers == mua::kAllUpscalers);
  }
  CHECK(Mua2().upscalers == mua::kFsr1Upscalers);
  // A saved temporal choice in MUA 2's settings is Off, so its program never sets a d3d12_* upscaler mode.
  CHECK(mua::ParseUpscaler(mua::kUpscalerFsr3, Mua2().upscalers) == mua::Upscaler::kOff);
  CHECK(mua::ParseUpscaler(mua::kUpscalerDlss, Mua2().upscalers) == mua::Upscaler::kOff);
  CHECK(mua::ParseUpscaler(mua::kUpscalerFsr1, Mua2().upscalers) ==
        (mua::kUpscalerAvailable ? mua::Upscaler::kFsr1 : mua::Upscaler::kOff));
}

void TestPatch() {
  std::printf("0166: no scene profile leaves both upscalers off, and mua1 keeps 0161's scene rule\n");
  const std::string patch = ReadFile(kPatches + kProfilePatch);
  CHECK(!patch.empty());
  const std::string added = Lines(patch, '+');
  const std::string removed = Lines(patch, '-');
  CHECK(Has(added, "REXCVAR_DEFINE_STRING(d3d12_scene_profile, \"\", \"GPU/D3D12\","));
  CHECK(Has(patch, "Alliance and its Gold Edition); empty leaves both off\")\n"
                   "+    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);"));
  // Checked in Initialize, before the DLSS bridge or the FidelityFX DLL is even looked at.
  CHECK(Has(patch, "+  profile_ = scene::FindSceneProfile(REXCVAR_GET(d3d12_scene_profile));\n"
                   "+  if (!profile_) {\n"
                   "+    REXGPU_WARN(\"{}: off, no scene profile for this game (d3d12_scene_profile \\\"{}\\\") - the "
                   "scene renders without it\",\n"
                   "+                Tag(), REXCVAR_GET(d3d12_scene_profile));\n"
                   "+    return true;\n"
                   "+  }\n"
                   "   if (backend_ == Backend::kDlss && REXCVAR_GET(d3d12_dlss_bridge).empty()) {"));
  CHECK(Has(added, "SceneProfile{\"mua1\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20},"));
  CHECK(Count(added, "SceneProfile{") == 1);
  CHECK(Has(added, "static_assert(scene::kMsaa2xLog2 == uint32_t(xenos::MsaaSamples::k2X));"));
  CHECK(Has(added, "static_assert(scene::kColorFormat8888 == uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8));"));
  CHECK(Has(added, "static_assert(scene::kColorFormat8888Gamma == "
                   "uint32_t(xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA));"));
  CHECK(Has(added, "if (best_draws >= profile_->min_draws) {"));
  CHECK(Has(added, "if (scene::IsSceneSurfaceCandidate(*profile_, uint32_t(surface_info.msaa_samples),"));

  // What 0166 replaces is exactly 0161's rule: 2x or more, k_8_8_8_8 or its gamma variant, 20 draws.
  const std::string scene = ReadFile(kPatches + kScenePatch);
  for (const char* rule : {"constexpr uint32_t kSceneSurfaceMinDraws = 20;\n",
                           "        surface_info.msaa_samples >= xenos::MsaaSamples::k2X && (depth_control.z_enable || "
                           "depth_control.z_write_enable)) {\n",
                           "      if (color_info.color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8 ||\n"
                           "          color_info.color_format == xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA) {\n",
                           "    if (best_draws >= kSceneSurfaceMinDraws) {\n"}) {
    CHECK(Has(removed, rule));
    CHECK(Has(Lines(scene, '+'), rule));
  }
  CHECK(!Has(added, "kSceneSurfaceMinDraws"));

  // Capture, depth, motion, jitter, write-back, the shaders and both backends are untouched.
  CHECK(Count(patch, "diff --git ") == 3);
  for (const char* path : {"include/rex/graphics/d3d12/scene_layout.h", "include/rex/graphics/d3d12/scene_upscaler.h",
                           "src/graphics/d3d12/scene_upscaler.cpp"}) {
    CHECK(Has(patch, std::string("diff --git a/") + path + " b/" + path + "\n"));
  }
  for (const char* text : {"FSR 3.1: AMD FSR 3.1 {} | scene {}x{} -> output {}x{} | scene profile {} |",
                           "DLSS: Super Resolution {} | scene profile {} |",
                           "scene surface found (scene profile {})"}) {
    CHECK(Has(added, text));
  }
  CHECK(!Has(patch, "DEBUG_SAULO") && !Has(patch, "Frame Generation") && !Has(patch, "frame generation"));
}

void TestGoldPatch() {
  std::printf("0167: mua1-gold's 4x draws stay in the scene's pass; mua1 and the pass checks are unchanged\n");
  const std::string patch = ReadFile(kPatches + kGoldPatch);
  CHECK(!patch.empty());
  const std::string added = Lines(patch, '+');
  const std::string removed = Lines(patch, '-');
  // The same rule as mua1's, plus the 4x draws; mua1's own entry only gains the flag, off.
  CHECK(Has(removed, "    SceneProfile{\"mua1\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20},\n"));
  CHECK(Has(added, "    SceneProfile{\"mua1\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20, false},\n"));
  CHECK(Has(added,
            "    SceneProfile{\"mua1-gold\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20, true},\n"));
  CHECK(Count(added, "SceneProfile{") == 2);
  // Checked before a draw outside the scene finishes it, and only until the frame's scene is done.
  CHECK(Has(patch, "   if (!scene) {\n"
                   "+    if (IsSceneMemoryDraw(regs, edram_mode, pixel_shader_writes_color)) {\n"
                   "+      NoteSceneMemoryDraw(regs, edram_mode, pixel_shader_writes_color, adjustment);\n"
                   "+      return adjustment;\n"
                   "+    }\n"
                   "     if (scene_pending_) {\n"
                   "       FinishScene();"));
  CHECK(Has(added, "  if (!profile_->scene_memory_4x_draws || !surface_ || scenes_finished_this_frame_ ||\n"));
  CHECK(Has(added, "  return profile.scene_memory_4x_draws && PreSceneTargets::SamplesLog2(key) == kMsaa4xLog2 &&\n"));
  // At the scene's scale, without its jitter, for DLSS and FSR 3.1 alike.
  const size_t begin = patch.find("+void SceneUpscaler::NoteSceneMemoryDraw(");
  const size_t end = patch.find("\n+}\n", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  const std::string body = patch.substr(begin, end - begin);
  CHECK(Has(body, "adjustment.scale_x = float(render_width_) / float(output_width_);"));
  CHECK(Has(body, "adjustment.scale_y = float(render_height_) / float(output_height_);"));
  CHECK(Has(body, "planes_.Clear(planes);"));
  CHECK(!Has(body, "jitter") && !Has(body, "adjustment.scene") && !Has(body, "backend_") && !Has(body, "Fsr3") &&
        !Has(body, "Dlss"));
  // The pass checks, the write-back, the FSR 3.1 alpha and the capture are 0161 to 0166's.
  for (const char* kept : {"more than one pass", "FinishScene", "CoveredByLater", "EncodeStaging", "CopyStagingTo",
                           "kOutputSceneAlpha", "StretchPreSceneResolve", "NoteOtherDraw", "Take("}) {
    CHECK(!Has(removed, kept));
  }
  CHECK(Count(patch, "diff --git ") == 3);
  CHECK(Has(added, "Alliance), mua1-gold (its Gold Edition); empty leaves both off\")"));
  CHECK(!Has(patch, "DEBUG_SAULO") && !Has(patch, "Frame Generation") && !Has(patch, "frame generation"));
}

void TestApp() {
  std::printf("the app names the game's scene profile only with DLSS or FSR 3.1 chosen, and logs the game\n");
  const std::string settings = ReadFile(kRoot + "src/settings.cpp");
  CHECK(Has(settings, "void ApplySceneProfile(const GameDefinition& game) {\n"
                      "  if (rex::cvar::GetFlagInfo(\"d3d12_scene_profile\")) {\n"
                      "    rex::cvar::SetFlagByName(\"d3d12_scene_profile\", std::string(game.scene_profile));"));
  CHECK(Count(settings, "ApplySceneProfile(game);") == 2);
  // Each call comes after the function returns for an unselected upscaler or a missing runtime file.
  for (const char* function : {"void ApplyDlss(", "void ApplyFsr3("}) {
    const size_t begin = settings.find(function);
    const size_t end = settings.find("\n}\n", begin);
    CHECK(begin != std::string::npos && end != std::string::npos);
    const std::string body = settings.substr(begin, end - begin);
    const size_t unselected = body.find("if (!selected) {");
    const size_t missing = body.find("is not next to the program");
    const size_t profile = body.find("ApplySceneProfile(game);");
    CHECK(unselected < missing && missing < profile && profile != std::string::npos);
    CHECK(Has(body, "const GameDefinition& game = *ActiveGame();"));
    CHECK(Has(body, "game.display_name"));
    CHECK(Has(body, "scene profile {}"));
  }
  CHECK(Has(settings, "AMD FSR 3.1: {}: {} requested | scene {} -> output {} | scene profile {} |"));
  CHECK(Has(settings, "NVIDIA DLSS: {}: {} requested | scene at NVIDIA's size -> output {} | scene profile {};"));
  // The upscaler of a game comes from its own list.
  CHECK(Has(settings, "return game ? ParseUpscaler(REXCVAR_GET(mua_upscaler), game->upscalers) : Upscaler::kOff;"));
}

void TestLauncher() {
  std::printf("the launcher offers each game its own list, with the same availability rows for every game\n");
  const std::string launcher = ReadFile(kRoot + "src/launcher.cpp");
  CHECK(Has(launcher, "UpscalerMask offered = Selected().game->upscalers;"));
  CHECK(Has(launcher, "if ((offered & UpscalerBit(Upscaler::kFsr3)) && !Fsr3().available) {"));
  CHECK(Has(launcher, "if ((offered & UpscalerBit(Upscaler::kDlss)) && !Dlss().available) {"));
  CHECK(Has(launcher, "ui.Choice(\"Upscaling\", UpscalingHelp(Selected().game->upscalers), \"mua_upscaler\", "
                      "kUpscalerOff,\n                UpscalerOptions(offered));"));
  // No game is named where the choices are made.
  const size_t begin = launcher.find("UpscalerMask offered = Selected().game->upscalers;");
  const size_t end = launcher.find("ui.Choice(\"Upscaling\"", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  CHECK(!Has(launcher.substr(begin, end - begin), "GameId"));
  // FSR 1 stays for every game, and Ultra Performance stays FSR 3.1's own.
  for (const mua::GameDefinition& game : mua::AllGames()) {
    CHECK((game.upscalers & mua::UpscalerBit(mua::Upscaler::kOff)) != 0);
    CHECK((game.upscalers & mua::UpscalerBit(mua::Upscaler::kFsr1)) != 0);
  }
  CHECK(mua::OffersQuality(mua::Upscaler::kFsr3, mua::UpscaleQuality::kUltraPerformance));
  CHECK(!mua::OffersQuality(mua::Upscaler::kDlss, mua::UpscaleQuality::kUltraPerformance));
  CHECK(std::string(mua::QualityLabel(mua::Upscaler::kDlss, mua::UpscaleQuality::kNative)) == "DLAA");
}

void TestIsolation() {
  std::printf("each game keeps its own upscaler settings and runs in its own program\n");
  const std::string settings = ReadFile(kRoot + "src/settings.cpp");
  const size_t begin = settings.find("constexpr std::string_view kGameScopedKeys[] = {");
  const size_t end = settings.find("};", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  const std::string scoped = settings.substr(begin, end - begin);
  for (const char* key : {"\"mua_upscaler\"", "\"mua_upscale_quality\"", "\"mua_fsr3_sharpness\""}) {
    CHECK(Has(scoped, key));
  }
  CHECK(Mua1().executable != Gold().executable && Gold().executable != Mua2().executable &&
        Mua1().executable != Mua2().executable);
  CHECK(Mua1().data_folder != Gold().data_folder && Gold().data_folder != Mua2().data_folder);
}

void TestRuntimeFiles() {
  std::printf("one amd_fidelityfx_dx12.dll and one DLSS bridge serve all three programs\n");
  size_t fidelityfx = 0;
  for (const char* file : mua::kBundleFiles) {
    fidelityfx += std::string_view(file) == "amd_fidelityfx_dx12.dll";
    CHECK(!Has(file, "nvngx") && !Has(file, "mua_dlss_bridge"));
  }
  CHECK(fidelityfx == 1);
  const std::string package = ReadFile(kRoot + "tools/package-release.ps1");
  CHECK(Count(package, "'amd_fidelityfx_dx12.dll'") == 1);
  CHECK(!Has(package, "nvngx_dlss") && !Has(package, "mua_dlss_bridge"));
  const std::string cmake = ReadFile(kRoot + "CMakeLists.txt");
  CHECK(Count(cmake, "add_library(mua_dlss_bridge SHARED") == 1);
}

}  // namespace

int main() {
  TestGames();
  TestPatch();
  TestGoldPatch();
  TestApp();
  TestLauncher();
  TestIsolation();
  TestRuntimeFiles();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
