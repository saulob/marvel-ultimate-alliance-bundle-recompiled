// NVIDIA DLSS and AMD FSR 3.1 per game: patches/rexglue-sdk/0166 runs the scene upscaler only for a game whose renderer
// has a scene profile (d3d12_scene_profile), and MUA 1's profile keeps 0161's scene rule. 0167 gives Gold Edition its
// own, which also takes its Direct3D's 4x MSAA draws over the scene's memory into the scene's pass, and 0168 gives MUA 2
// its own, which pins its 1040x624 scene surface; 0169 draws MUA 2's shadow pass at 1x. All three games offer both
// upscalers. The app passes the profile only
// with DLSS or FSR 3.1 chosen and sizes MUA 2's scene from its own size, the launcher's choices follow each game's own
// list, each game keeps its own settings and runs in its own program, and the three games share one copy of each
// upscaler runtime.
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
constexpr char kMua2Patch[] = "0168-d3d12-scene-upscaler-mua2-profile.windows.patch";
constexpr char kShadowPassPatch[] = "0169-d3d12-scene-upscaler-mua2-shadow-pass.windows.patch";
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
  std::printf("each game has its own scene profile and offers FSR 3.1 and DLSS\n");
  CHECK(Mua1().scene_profile == "mua1" && Gold().scene_profile == "mua1-gold" && Mua2().scene_profile == "mua2");
  for (const mua::GameDefinition* game : {&Mua1(), &Gold(), &Mua2()}) {
    CHECK(game->upscalers == mua::kAllUpscalers);
    CHECK(mua::ParseUpscaler(mua::kUpscalerFsr3, game->upscalers) ==
          (mua::UpscalerOnBackend(mua::Upscaler::kFsr3) ? mua::Upscaler::kFsr3 : mua::Upscaler::kOff));
    CHECK(mua::ParseUpscaler(mua::kUpscalerDlss, game->upscalers) ==
          (mua::UpscalerOnBackend(mua::Upscaler::kDlss) ? mua::Upscaler::kDlss : mua::Upscaler::kOff));
    CHECK(mua::ParseUpscaler(mua::kUpscalerFsr1, game->upscalers) ==
          (mua::UpscalerOnBackend(mua::Upscaler::kFsr1) ? mua::Upscaler::kFsr1 : mua::Upscaler::kOff));
  }
  // MUA 2's scene is 1040x624 beside its 1280x720 framebuffer; the others' scene is the video mode's.
  CHECK(Mua2().scene_size.width == 1040 && Mua2().scene_size.height == 624);
  CHECK(Mua1().scene_size.width == 0 && Mua1().scene_size.height == 0 && Gold().scene_size.width == 0 &&
        Gold().scene_size.height == 0);
}

void TestMua2Patch() {
  std::printf("0168: mua2 pins MUA 2's scene; mua1, mua1-gold, the backends and the write-back are unchanged\n");
  const std::string patch = ReadFile(kPatches + kMua2Patch);
  CHECK(!patch.empty());
  const std::string added = Lines(patch, '+');
  const std::string removed = Lines(patch, '-');
  CHECK(Count(patch, "diff --git ") == 5);
  for (const char* path : {"include/rex/graphics/d3d12/scene_layout.h", "include/rex/graphics/d3d12/scene_upscaler.h",
                           "src/graphics/d3d12/scene_upscaler.cpp", "src/graphics/shaders/scene_motion.cs.hlsl",
                           "src/graphics/shaders/bytecode/d3d12_5_1/scene_motion_cs.h"}) {
    CHECK(Has(patch, std::string("diff --git a/") + path + " b/" + path + "\n"));
  }
  // The proven surface, scene size and behaviors, for mua2 alone.
  CHECK(Has(added, "    SceneProfile{.key = \"mua2\",\n"));
  CHECK(Has(added, ".pinned_surface = PinnedSceneSurface{0, 1034, kDepthFormatD24FS8, 1040, 1040, 624},"));
  for (const char* field : {".reduce_pre_scene_targets = false,", ".depth_at_each_resolve = true,",
                            ".depth_partitions = true,", ".camera_required = true}"}) {
    CHECK(Count(added, field) == 1);
  }
  // mua1 and mua1-gold keep their values, written out by name.
  CHECK(Has(removed, "SceneProfile{\"mua1\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20, false},"));
  CHECK(Has(removed, "SceneProfile{\"mua1-gold\", kMsaa2xLog2, {kColorFormat8888, kColorFormat8888Gamma}, 20, true},"));
  CHECK(Has(added, "    SceneProfile{.key = \"mua1\",\n"
                   "                 .min_msaa_samples_log2 = kMsaa2xLog2,\n"
                   "                 .color_formats = {kColorFormat8888, kColorFormat8888Gamma},\n"
                   "                 .min_draws = 20},\n"));
  CHECK(Has(added, "                 .min_draws = 20,\n                 .scene_memory_4x_draws = true},\n"));
  // Every new field defaults to what 0161 to 0167 did.
  for (const char* field :
       {"  std::optional<PinnedSceneSurface> pinned_surface = std::nullopt;", "  bool reduce_pre_scene_targets = true;",
        "  bool depth_at_each_resolve = false;", "  bool depth_partitions = false;", "  bool camera_required = false;"}) {
    CHECK(Has(added, field));
  }
  // Only the pinned pitch is the scene's, for draws and resolves; the scene's own size sizes the output.
  CHECK(Has(added, "if (surface_info.msaa_samples != surface_->msaa || !scene::IsScenePitch(*profile_, "
                   "surface_info.surface_pitch)) {"));
  CHECK(Has(added, "      !scene::IsScenePitch(*profile_, surface_info.surface_pitch)) {"));
  CHECK(Has(added, "  guest_width_ = profile_->pinned_surface ? profile_->pinned_surface->width : video_width;"));
  CHECK(Has(added, "  const uint32_t planes = planes_.Take(depth, profile_->depth_at_each_resolve);"));
  CHECK(Has(added, "  const bool pre_scene_scale = REXCVAR_GET(d3d12_dlss_pre_scene_scale) && "
                   "profile_->reduce_pre_scene_targets;"));
  // The safety checks turn the upscaler off for the session through the existing structure mismatch.
  CHECK(Has(added, "pass_matches = scene::PinnedOutputResolve(*pinned, rects).has_value();"));
  CHECK(Has(added, "not its whole {}x{} surface"));
  CHECK(Has(added, "no camera projection in the scene's draws for {} frames"));
  CHECK(Has(added, "  const bool upscaled = !UpscalerFailed() && !scenes_finished_this_frame_ && pass_matches;"));
  // Camera motion clamps depth only for depth partitions; the others read it unbounded, as before.
  CHECK(Has(added, "  float2 depth_range;\n"));
  CHECK(Has(added, "(clamp(depth[id.xy], depth_range.x, depth_range.y) - viewport_scale_offset_z.y) /"));
  CHECK(Has(added, "      profile_->depth_partitions ? scene::MotionDepthRange(camera_projection_, depth_inverted_)\n"
                   "                                 : std::array<float, 2>{-FLT_MAX, FLT_MAX};"));
  CHECK(Has(patch, "+//   float2 depth_range;                // Offset:  112 Size:     8\n"));
  // The write-back, FSR 3.1's alpha, jitter, the stretched copies and both backends' calls are 0161 to 0167's.
  for (const char* kept : {"kOutputSceneAlpha", "CoveredByLater", "EncodeStaging(", "CopyStagingTo(", "EvaluateDlss(",
                           "JitterForFrame", "StretchPreSceneResolve", "fsr3_->", "feature_."}) {
    CHECK(!Has(removed, kept));
  }
  CHECK(Has(added, "mua2 (Marvel Ultimate Alliance 2); empty leaves both \"\n"));
  CHECK(!Has(patch, "DEBUG_SAULO") && !Has(patch, "Frame Generation") && !Has(patch, "frame generation"));
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

void TestMua2ShadowPassPatch() {
  std::printf("0169: mua2's shadow pass at 1x; its surface after the scene, mua1 and mua1-gold keep their scales\n");
  const std::string patch = ReadFile(kPatches + kShadowPassPatch);
  CHECK(!patch.empty());
  const std::string added = Lines(patch, '+');
  const std::string removed = Lines(patch, '-');
  CHECK(Count(patch, "diff --git ") == 4);
  for (const char* path : {"include/rex/graphics/d3d12/scene_layout.h", "include/rex/graphics/d3d12/scene_upscaler.h",
                           "src/graphics/d3d12/scene_upscaler.cpp", "src/graphics/d3d12/command_processor.cpp"}) {
    CHECK(Has(patch, std::string("diff --git a/") + path + " b/" + path + "\n"));
  }
  // One named surface for mua2, unset for the others.
  CHECK(Count(added, ".native_pre_scene_key = PreSceneTargets::Key(1040, 0),") == 1);
  CHECK(Has(added, "  uint32_t native_pre_scene_key = 0;"));
  CHECK(Has(added, "  static constexpr uint32_t kAfterScene = 1u << 20;"));
  CHECK(Has(added, "  return profile.native_pre_scene_key && after_scene ? key | PreSceneTargets::kAfterScene : key;"));
  CHECK(Has(added, "    return key == profile.native_pre_scene_key && targets.IsNative(key);"));
  CHECK(Has(added, "  return targets.IsReduced(key);"));
  // Draws and resolves both take it; frames after one without the scene keep the full scale.
  CHECK(Count(added, "scene::IsPreSceneReduced(*profile_, pre_scene_targets_, key)") == 2);
  CHECK(Has(added, "profile_->native_pre_scene_key ? pre_scene_native_ && scene_drawn_"));
  // The cvar still switches it off, and the scene, its write-back and both backends are untouched.
  CHECK(Has(added, "  pre_scene_native_ = REXCVAR_GET(d3d12_dlss_pre_scene_native) &&"));
  for (const char* kept : {"kOutputSceneAlpha", "CoveredByLater", "EncodeStaging(", "CopyStagingTo(", "EvaluateDlss(",
                           "EvaluateFsr3(", "JitterForFrame", "IsSceneDraw(", "pinned_surface", "FinishScene("}) {
    CHECK(!Has(removed, kept) && !Has(added, kept));
  }
  CHECK(Has(added, "% of host pixels"));
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
  CHECK(Has(settings,
            "return game ? ParseUpscaler(REXCVAR_GET(mua_upscaler), game->upscalers & RuntimeUpscalers()) : Upscaler::kOff;"));
  // The temporal output is the game's scene at the Render resolution: MUA 2's 1040x624 times the scale.
  CHECK(Has(settings, "return SceneCanvas(ParseAspectRatio(REXCVAR_GET(mua_aspect_ratio)), game.scene_size);"));
  CHECK(Count(settings, "TemporalOutputSize(game)") == 2);
  CHECK(Has(settings, "Fsr3SceneSize(output, TemporalOutputCanvas(game).width, quality);"));
}

void TestLauncher() {
  std::printf("the launcher offers each game its own list, with the same availability rows for every game\n");
  const std::string launcher = ReadFile(kRoot + "src/launcher.cpp");
  CHECK(Has(launcher, "UpscalerMask offered = Selected().game->upscalers;"));
  CHECK(Has(launcher, "if ((offered & UpscalerBit(Upscaler::kFsr3)) && !Fsr3().available) {"));
  CHECK(Has(launcher, "if ((offered & UpscalerBit(Upscaler::kDlss)) && !Dlss().available) {"));
  CHECK(Has(launcher, "ui.Choice(\"Upscaling\", UpscalingHelp(Selected().game->upscalers & kBackendUpscalers), "
                      "\"mua_upscaler\",\n                kUpscalerOff, UpscalerOptions(offered));"));
  // No game is named where the choices are made.
  const size_t begin = launcher.find("UpscalerMask offered = Selected().game->upscalers;");
  const size_t end = launcher.find("ui.Choice(\"Upscaling\"", begin);
  CHECK(begin != std::string::npos && end != std::string::npos);
  CHECK(!Has(launcher.substr(begin, end - begin), "GameId"));
  // The quality rows size the scene from the game's own scene size.
  for (const char* rows : {"Fsr3QualityOptions(", "DlssQualityOptions("}) {
    CHECK(Has(launcher, std::string(rows) + "SelectedAspectRatio(), settings_.GetInt(\"resolution_scale\", 1),\n"
                                            "                                    Selected().game->scene_size));"));
  }
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
  CHECK(Mua1().program != Gold().program && Gold().program != Mua2().program &&
        Mua1().program != Mua2().program);
  CHECK(Mua1().data_folder != Gold().data_folder && Gold().data_folder != Mua2().data_folder);
}

void TestRuntimeFiles() {
  std::printf("one amd_fidelityfx_dx12.dll and one DLSS bridge serve all three programs\n");
  size_t fidelityfx = 0;
  for (const char* file : mua::kBundleFiles) {
    fidelityfx += std::string_view(file) == "amd_fidelityfx_dx12.dll";
  }
  CHECK(fidelityfx == 1);
  const std::string package = ReadFile(kRoot + "tools/package-release.ps1");
  CHECK(Count(package, "'amd_fidelityfx_dx12.dll'") == 1);
  CHECK(Has(package, "'mua_dlss_bridge.dll', 'nvngx_dlss.dll'"));
  const std::string cmake = ReadFile(kRoot + "CMakeLists.txt");
  CHECK(Count(cmake, "add_library(mua_dlss_bridge SHARED") == 1);
}

}  // namespace

int main() {
  TestGames();
  TestPatch();
  TestGoldPatch();
  TestMua2Patch();
  TestMua2ShadowPassPatch();
  TestApp();
  TestLauncher();
  TestIsolation();
  TestRuntimeFiles();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
