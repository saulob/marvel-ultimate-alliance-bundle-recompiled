# AMD FSR 3.1 Super Resolution

Status: **verified in all three games.** At Render resolution
3x with Performance (1920x1080 to 3840x2160) on an RTX 4070 the picture is correct in MUA1 and Gold Edition, at 66 to
71 FPS in MUA1's heavy gameplay and above 80 FPS in lighter sections; Gold Edition, through its own scene profile (see
[Games](#games)), stays playable and stable with FSR 3.1 and DLSS. MUA2's picture is correct with both, with its
shadow pass at 1x.

Platform: Windows only. FSR 3.1 here is a second backend of the Direct3D 12 GPU plugin's scene upscaler and calls AMD's
`amd_fidelityfx_dx12.dll`; the Linux Vulkan GPU plugin has no scene upscaler, so the Linux launcher offers AMD FSR 1
only.

All three games on Windows offer AMD FSR 3.1 in the launcher's Upscaling choice (Off, AMD FSR 1, AMD FSR 3.1, NVIDIA
DLSS). It runs on any Direct3D 12 graphics card that uses the rasterizer-ordered view (ROV) render path (NVIDIA, AMD
and Intel), with nothing to install: AMD's signed `amd_fidelityfx_dx12.dll` ships beside the programs. It is temporal
upscaling only, with no frame generation, and nothing about it runs unless it is selected.

FSR 3.1 is a second backend of the DLSS scene upscaler ([dlss.md](dlss.md)). It uses the same scene surface, scene-only
color, depth (0163), camera motion vectors, jitter, scale domains, pre-scene targets (0162, 0164) and write-back to the
scene's resolves. Only the runtime it calls is different.

## Games

| Game | Scene profile | FSR 3.1 and DLSS |
|---|---|---|
| Marvel Ultimate Alliance | `mua1` | Offered; verified |
| Gold Edition | `mua1-gold` | Offered; verified |
| Marvel Ultimate Alliance 2 | `mua2` | Offered; verified |

`d3d12_scene_profile` (patches 0166 to 0169) says how the game's renderer draws its 3D scene. The app sets it from the
game being run, only with FSR 3.1 or DLSS chosen; without one the GPU plugin leaves both off before loading anything.
The profile holds the rule that finds the scene surface (`kSceneProfiles` in `scene_layout.h`): `mua1` is 0161's rule
unchanged, 2x multisampling or more, `k_8_8_8_8` or `k_8_8_8_8_GAMMA`, and at least 20 depth-tested draws in a frame.
Each game runs in its own program, so no scene, history or upscaler context carries over from another game.

Gold Edition draws the same scene as MUA1, from the same `alchemy.ini`, assets and shaders (rebuilt by a later shader
compiler, with the same direct world transform the camera recovery reads): color at EDRAM base 0, 2x multisampling,
D24S8 depth at base 1128, in two predicated tiles (screen 0..672, and 640..1280 at window offset -640). Its program
links a later D3D library, which also draws full-surface quads over the scene's memory at 4x multisampling with half
the pitch (400, the same width in samples), between the scene's draws and resolves. `mua1-gold` (0167) is `mua1`'s
rule plus those quads: until the frame's scene is done they are part of its pass, drawn at the scene's scale without
jitter, and the plane they draw over counts as cleared for the depth capture. Any other draw between the scene's
resolves still ends the pass, and a second pass in a frame turns the upscaler off.

In gameplay each tile is resolved twice: first without clears, into a copy the scene's later draws sample, then into
the texture the glow and composite read, clearing color and depth. Of those 4 scene resolves, each tile's later one
covers its first, so the output goes into the 2 later ones (`written to 2 of 4`); depth comes from each tile's first.
The title screen resolves each tile once (`2 of 2`).

MUA2 is a later Alchemy renderer with a scene of its own size. It draws the scene into one surface that fits the EDRAM
at 2x multisampling: color at EDRAM base 0 (`k_8_8_8_8`), D24FS8 depth at base 1034 with 1 at the near plane, pitch
1040, 1040x624 guest pixels. Its 1280x720 framebuffer is a separate pitch 880 surface that also starts at color base 0,
into which its own composite scales the post-processed scene, in two resolves (screen 0..864 and 864..1280).
`mua2` (0168) pins the scene surface: only draws and resolves at pitch 1040 are the scene's, so the composite, HUD and
menus never are. The upscaler's output is the scene's own domain, 1040x624 times the Render resolution (3120x1872 at
3x), beside the 3840x2160 framebuffer, and DLSS's and FSR 3.1's modes size the scene from it.

Each gameplay frame draws a depth pre-pass, resolves the depth (read by particles and post-processing), draws the sky
into the depth range below the scene's and the rest of the scene, then resolves the whole scene twice: first without
clears, into a copy one more 3D draw of the scene samples, then, clearing color and depth, into the texture the bloom
chain reads. The output goes into that last resolve (`written to 1 of 2`), which `mua2` requires to be the whole
scene; the copy before it keeps the scene's input, stretched. Depth is taken at each resolve, so the upscaler gets the
scene's final depth, sky included, and the scene's viewport is the one with the widest depth range, not the sky's. The
camera is the scene's consensus projection, near 32 with an infinite far plane and depth 1 near, 0 far. The upscaler
turns off for the session if
the last resolve isn't the whole scene, the scene is resolved in two passes or its draws hold no camera projection for
600 frames. The sky's depth, below the scene's range, lies past its infinite far plane; the motion vectors take such
depths as infinitely far (`scene_motion`'s depth range, unbounded for the other games), so the sky moves with the
camera's turn only.

Before the scene, MUA2 draws two shadow maps into pitch 1040 1x surfaces (depth bases 1216 and 1632, 512x512 tiles
with the lights' projections), resolves them to 1024x1024 and 1024x512 textures, and filters each into a `k_16_16`
copy at color base 0 that the scene's draws sample. `mua2` (0169) draws that whole pass at 1x, the game's own size, in
every mode, once the shadow textures are seen sampled only by the scene, and stretches their resolves to the textures'
full size. The same pitch after the scene, a 1024x32 texture the composite reads, is kept apart and stays at the Render
resolution, as do the 4x quads clearing the shadow maps and the scene, the bloom chain, the composite, HUD and menus.
A shadow texture sampled after the scene puts the pass back at the Render resolution for the session, and a frame after
one without the scene draws it at the Render resolution.

## Version

FidelityFX SDK **v1.1.4** (tag `v1.1.4`, commit `c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`), whose upscaler is
**FSR 3.1.4**. The port uses AMD's own prebuilt, signed D3D12 DLL from that release and its C API (`ffx_api`), so no
FidelityFX source or shaders are compiled here. The log names the upscaler versions the DLL offers on the card.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| DLL download | `tools/setup-fidelityfx.ps1` | `build.ps1` runs it. It downloads `PrebuiltSignedDLL/amd_fidelityfx_dx12.dll` and `LICENSE.txt` from AMD's repository at the pinned commit into the git-ignored `tools\amd-fidelityfx\1.1.4\`, checking each file's SHA-256. If the download fails, the build continues without FSR 3.1. |
| Build | `CMakeLists.txt` | Copies the DLL beside the programs (`MUA_FSR3_RUNTIME`). |
| Headers | `patches/rexglue-sdk/0165` (`thirdparty/fidelityfx-api/`) | `ffx_api.h`, `ffx_api_types.h`, `ffx_upscale.h` and `dx12/ffx_api_dx12.h` of that release, unmodified, with AMD's MIT license. `mua_fsr3_tests` checks their SHA-256 against AMD's files. |
| Runtime | 0165, `Fsr3Upscaler` | Loads the DLL (never unloaded), lists the upscaler versions for the device, queries AMD's render size and jitter phase count, and creates the context and records each upscale on the command list. |
| Scene upscaler | 0165, `SceneUpscaler` | The backend choice: `d3d12_fsr3_mode` when `d3d12_dlss_mode` is off. DLSS's code is moved, not changed; the shared log lines carry the backend's name. |
| Scene profiles | 0166, 0167, 0168, 0169, `scene_layout.h` | `d3d12_scene_profile`: the game's rule for finding its scene surface and, for `mua1-gold`, its D3D library's 4x draws over it, and for `mua2`, its pinned scene surface and size and its shadow pass at 1x; none leaves FSR 3.1 and DLSS off. |
| App | `src/settings.cpp` (`ApplyFsr3`), `src/upscaling.h`, `src/fsr3_support.h`, `src/game_definition.cpp` | With FSR 3.1 chosen, sets `d3d12_fsr3_mode`, `d3d12_fsr3_sharpness`, `d3d12_fsr3_library` (the DLL beside the program) and `d3d12_scene_profile` (the game's `scene_profile`). Any other choice sets `d3d12_fsr3_mode=off`. |
| Launcher | `src/launcher.cpp` | Offers AMD FSR 3.1 when the DLL is beside the program and the card runs the ROV path. Otherwise an "AMD FSR 3.1: Unavailable: ..." row says why, and a saved FSR 3.1 choice becomes Off. Shows the five modes with their exact scene sizes, and Sharpness. |
| Release | `tools/package-release.ps1`, `src/update_bundle.h`, `packaging/licenses/THIRD-PARTY-LICENSES.txt` | The DLL is a release file, the updater installs it, and AMD's license ships with it. |

## Settings

| Key | Values | Default |
|---|---|---|
| `mua_upscaler` | `off`, `fsr` (AMD FSR 1), `fsr3` (AMD FSR 3.1), `dlss` | `off` |
| `mua_upscale_quality` | `native`, `quality`, `balanced`, `performance`, `ultra_performance` | `native` |
| `mua_fsr3_sharpness` | 0 to 100 | 80 |

All three are kept per game. `fsr` still means FSR 1. Ultra Performance is FSR 3.1's only: with FSR 1 or DLSS it acts as
Performance, and the launcher shows Performance for it. The GPU plugin's `d3d12_fsr3_mode`, `d3d12_fsr3_sharpness` and
`d3d12_fsr3_library` are set by the app at start.

## Modes and scene sizes

The Render resolution is the output. The scene draws at AMD's size for the mode (output / 1.0, 1.5, 1.7, 2.0 or 3.0,
rounded down, `ffxFsr3UpscalerGetRenderResolutionFromQualityMode`). Its width is snapped like DLSS's, so the EDRAM tile
edges where MUA1's two tiles meet land on whole render pixels:

| Render resolution | Native | Quality | Balanced | Performance | Ultra Performance |
|---|---|---|---|---|---|
| 1x 1280x720 | 1280x720 | 848x480 | 752x423 | 640x360 | 432x240 |
| 2x 2560x1440 | 2560x1440 | 1712x960 | 1504x847 | 1280x720 | 848x480 |
| 3x 3840x2160 | 3840x2160 | 2560x1440 | 2256x1270 | 1920x1080 | 1280x720 |
| 4x 5120x2880 | 5120x2880 | 3408x1920 | 3008x1694 | 2560x1440 | 1712x960 |

The HUD, menus, post-processing and the presenter stay at the Render resolution. The targets drawn before the scene
follow DLSS's rules: the smallest whole scale with the scene's detail, and 1x in Performance and Ultra Performance for
those only the scene samples.

## Inputs, in AMD's conventions

| Input | What FSR 3.1 gets |
|---|---|
| Color | The scene's own LDR `k_8_8_8_8` color before the HUD, `RGBA8`, render size. No HDR, no auto exposure, pre-exposure 1, no exposure texture. |
| Depth | The DLSS depth (0163: taken once between depth clears, D24S8 to `R32F`), 0 to 1, render size. `FFX_UPSCALE_ENABLE_DEPTH_INVERTED` follows the scene's `zfunc` (GREATER or GEQUAL); the context is made again if that ever changes. |
| Camera | `cameraNear`, `cameraFar` and `cameraFovAngleVertical` come from the game's matrices. For draws whose clip w is the view depth, the depth buffer value is alpha + beta / w (after the guest viewport's z scale and offset), which gives the view depths at depth 0 and 1. The y row over the w row gives the vertical field of view. The projection most draws share wins. An infinite far plane is passed as `FLT_MAX`, which makes FSR's conversion the infinite one. Near and far order doesn't matter (FSR takes the minimum and maximum). Until a projection is found it uses near 0.1 and an infinite far plane. |
| View space to meters | 1, AMD's default. MUA1's world units are not known. In FSR 3.1.4 the factor only moves a motion threshold between 0.25 and 0.75 pixels over the first 100 m. |
| Motion vectors | The DLSS camera motion vectors (`RG16F`), in render pixels, from where a surface is to where it was (previous minus current), with the jitter removed. `motionVectorScale` is 1. FSR divides by the render size and adds the result to the current UV. No `JITTER_CANCELLATION` and no `DISPLAY_RESOLUTION_MOTION_VECTORS`. |
| Jitter | AMD's sequence: `Halton(n % phases + 1, 2) - 0.5`, `Halton(n % phases + 1, 3) - 0.5` in render pixels (the same Halton(2,3) DLSS uses). AMD's phase count is `8 * (output width / render width)^2` (32 at 4K Performance); FSR derives the same count from each dispatch. The scene's draws are moved by +x right and +y down (NDC +2x/W, -2y/H), as AMD's projection jitter is, and the same values are passed as `jitterOffset`. The HUD is not jittered. |
| Reactive, transparency and composition masks | None. See below. |
| Frame time | The time between upscales (16.7 ms when outside 0 to 200 ms). |
| Reset | When the camera reprojection isn't found, after a frame without the scene, when the scene surface changes, and when the context is made. |

### Output alpha

FSR 3.1.4 upscales color only: every output pixel gets alpha 1 (`StoreUpscaledOutput` writes `float4(color, 1)`, RCAS
too). The game's passes after the scene (its glow and composite) read the scene texture's alpha, and alpha 1 everywhere
makes the whole picture glow. So the write-back of the FSR output (`scene_encode` mode 3) always keeps FSR 3.1's color
and takes alpha from the scene's own render-size color, sampled bilinearly at each output pixel's unjittered point
(output pixel center times render/output, plus the jitter) and clamped to the render size. DLSS's write-back is
separate and unchanged.

The color path is plain SDR: `RGBA8` in and out, no HDR flag, no auto exposure, pre-exposure 1 and no exposure texture
(FSR's default exposure texture holds 0, which its shaders read as 1), no sRGB or PQ dispatch flag, no transfer
function.

### Object motion

As with DLSS, there are only camera motion vectors. Characters, enemies and effects that move on their own get the
camera's motion, so FSR 3.1 can trail or ghost them. Per-object motion vectors are not part of this pass.

### Reactive mask

There is none. A reactive mask needs the scene's opaque-only color before its blended draws (an extra copy at the first
blended draw and a compute pass every frame); FSR 3.1 finds shading changes itself. Particles, energy and explosion
effects stay in the scene and are not moved to the HUD.

## Sharpening

FSR 3.1's own RCAS (`enableSharpening`, `sharpness` 0 to 1), from the launcher's 0 to 100, default 80 (AMD's samples
use 0.8). FSR 3.1.4 maps it to 2 - 2 x sharpness stops, so 80 is 0.4 stops. 0 turns it off.
With FSR 3.1 the app sets `present_effect=bilinear`, so FSR 1's RCAS never runs on its output (no double sharpening).
DLSS and FSR 1 are unchanged.

## Lifetime and command list

- The DLL is loaded, and its render size and phase count queried, at the first draw into the scene surface.
- The context (its history and internal textures, sized for the render and output sizes) is made on the command list's
  thread at the first upscale, then reused. Making it builds FidelityFX's pipelines, so that first upscaled frame can
  take noticeably longer.
- It is made again only if the depth convention changes, after waiting for the GPU to finish with the old one. The
  render and output sizes are fixed for the session (resolution and mode changes restart the game).
- Each frame, one `ffxDispatch` is recorded on the command processor's own command list through `ExternalCall`, so no
  other queue, flush or wait is involved. The command processor then forgets its cached state (FidelityFX sets its own
  descriptor heaps, root signatures and pipelines).
- Before the dispatch, color, depth and motion are in `NON_PIXEL_SHADER_RESOURCE` (FidelityFX's `COMPUTE_READ`) and the
  output is in `UNORDERED_ACCESS`; FidelityFX returns them to those states.
- There are no per-frame allocations and no readbacks.

## Fallbacks

Every failure is logged once, and the scene then draws at full resolution without an upscaler (Off). It never switches
to DLSS or FSR 1:

| Failure | Result |
|---|---|
| The DLL is not beside the program, or a runtime without 0165 | Logged by the app, mode stays off; the launcher shows the reason |
| The DLL can't be loaded, isn't AMD's FidelityFX API, or offers no upscaler for the card | `FSR 3.1: off for this session, AMD FSR 3.1 is unavailable ...` before any draw is scaled |
| Not the ROV path | `FSR 3.1: off, it needs the rasterizer-ordered view render target path` |
| Context creation or an upscale fails | That frame shows whatever the output texture holds; off from the next frame (`FSR 3.1 stopped working`) |
| The scene structure problems of DLSS's table | The same handling |

`d3d12_dlss_mode` takes precedence if both are set by hand.

## Logs

- `AMD FSR 3.1: Marvel Ultimate Alliance Gold Edition: performance requested | scene 1920x1080 -> output 3840x2160 |
  scene profile mua1-gold | sharpness 80 (FidelityFX SDK 1.1.4, amd_fidelityfx_dx12.dll) ...` (app), and the
  `Upscaling:` line.
- `FSR 3.1: scene surface found (scene profile mua1-gold): ...`.
- `FSR 3.1: AMD FSR 3.1 Performance | scene 1920x1080 -> output 3840x2160 | scene profile mua1-gold | AMD's render size
  1920x1080 | FidelityFX upscaler 3.1.4 | jitter Halton(2,3) 32 phases | motion vectors render pixels, current to
  previous, without jitter | depth R32F 0 to 1 | color RGBA8 SDR, no auto exposure, pre-exposure 1 | output alpha the
  scene's own | sharpness 80`.
- `FSR 3.1: off, no scene profile for this game ...` when the game has none (FSR 3.1 never starts).
- `FSR 3.1: scale domains | ...` and the targets drawn before the scene, as with DLSS; for MUA2, its shadow pass at 1x.
- `FSR 3.1: upscaler context created | FidelityFX upscaler ... | max render ... -> output ... | depth 0 near, 1 far`.
- `FSR 3.1: camera from the game's projection | near ... | far ... | vertical field of view ... degrees | depth ...`,
  once.
- `FSR 3.1: active | ...`, once, and the 600-frame summary.
- Up to 32 of FidelityFX's own error or warning messages.
