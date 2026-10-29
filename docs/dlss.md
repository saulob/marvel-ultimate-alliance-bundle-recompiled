# NVIDIA DLSS Super Resolution

Status: **fifth version, verified.** At Render resolution 3x with Performance (1920x1080 to 3840x2160) the picture
is correct, the depth is valid and gameplay runs at 58.5 to 73.7 FPS. The picture was correct at every mode tried
before it too (logs 246 to 250). The third
version (0162) draws the targets the game draws before its 3D scene, such as shadow maps, at the scene's scale; at
Render resolution 4x with Performance (2560x1440 to 5120x2880) that took a comparable heavy section from 38.4 to 45.2
FPS and its GPU wait from 23.67 to 19.66 ms (log 250). The fifth version takes the DLSS depth from the right resolve
(0163; it was the clear value in every gameplay frame, see "Fourth run") and, in Performance, draws the pre-scene
targets only the scene samples at the game's own 1x (0164).

Platform: Windows only. DLSS here is a scene upscaler inside the Direct3D 12 GPU plugin (patches 0160 to 0169) that calls
NVIDIA's Direct3D 12 NGX path; the Linux Vulkan GPU plugin has no scene upscaler, so the Linux launcher does not offer it.

AMD FSR 3.1 ([fsr3.md](fsr3.md)) is a second backend of the same scene upscaler, with the same scene, depth, motion
vectors, jitter, pre-scene targets and write-back.

All three games on Windows offer NVIDIA DLSS in the launcher's Upscaling choice (Off, AMD FSR 1, AMD FSR 3.1, NVIDIA
DLSS), on a GeForce RTX card that uses the rasterizer-ordered view (ROV) render path. DLSS upscales the 3D scene only,
from the scene's own color and depth, camera motion vectors and a jitter the port adds; the HUD and menus keep drawing
at the Render resolution. Gold Edition uses its own scene profile, `mua1-gold` (0167), and is verified at 3x
Performance. MUA2 uses `mua2` (0168), whose scene has its own 1040x624 domain beside the 1280x720 framebuffer, and
is verified at 3x Performance, with its shadow pass at 1x (0169) (see [fsr3.md](fsr3.md#games)).
Nothing about DLSS runs unless it is selected.

## Pieces

| Piece | Where | What it does |
|---|---|---|
| Bridge DLL | `src/dlss_bridge/` | `mua_dlss_bridge.dll`, the only code built against NVIDIA's SDK. One export, `RexDlssBridgeGetApi`, returns a versioned table of C functions: adapter requirements, NGX init, optimal render sizes, feature create, evaluate and release, shutdown, last error. Every call holds one lock (NGX is not thread-safe), and the last error is per thread. |
| Interface | `src/dlss_bridge/rex_dlss_bridge.h` | Plain C, no NVIDIA types. Patch 0160 carries an identical copy for the GPU plugin; `mua_dlss_bridge_tests` checks that the two match. |
| SDK download | `tools/setup-dlss-sdk.ps1` | `build.ps1` runs it. It downloads the NGX headers, `nvsdk_ngx_s(_dbg).lib`, `LICENSE.txt` and the release `nvngx_dlss.dll` from NVIDIA/DLSS at commit `9981c63` (SDK 310.9.16) into the git-ignored `tools\nvidia-dlss\`, checking each file's SHA-256. If the download fails, the build continues without DLSS. |
| Build | `CMakeLists.txt` | Builds the bridge with the programs' clang++/lld-link and the static CRT NVIDIA's `_s` libraries need, and copies `nvngx_dlss.dll` next to it. |
| Bridge loader | `patches/rexglue-sdk/0160-d3d12-dlss-bridge.windows.patch` | `DlssBridge` in the GPU plugin: loads the DLL, runs NVIDIA's adapter check, initializes NGX on the game's device. |
| Scene upscaler | `patches/rexglue-sdk/0161-d3d12-dlss-scene-upscaler.windows.patch` | `SceneUpscaler`, its three compute shaders, the command processor hooks, `DeferredCommandList::ExternalCall` and `D3D12RenderTargetCache::CopyResolveToMemory`. The GPU-free geometry and math are in `scene_layout.h`, which `mua_scene_upscaler_math_tests` checks. |
| Pre-scene targets | `patches/rexglue-sdk/0162-d3d12-dlss-pre-scene-target-scale.windows.patch` | Draws the surfaces drawn only before the scene at the scene's whole scale, `scene_edram_stretch` for their resolves, `Resolve`'s `copy_source`, the scale log lines and the GPU targets line's effective scales. |
| Depth capture | `patches/rexglue-sdk/0163-d3d12-dlss-depth-capture.windows.patch` | Takes the DLSS depth once between depth clears, at the first resolve after draws wrote it (`scene::ScenePlanes`). |
| Native pre-scene targets | `patches/rexglue-sdk/0164-d3d12-dlss-native-pre-scene-targets.windows.patch` | In Performance and Ultra Performance, draws the pre-scene targets the scene's draws sample at 1x, with the game's own depth bias. |
| MUA2 shadow pass | `patches/rexglue-sdk/0169-d3d12-scene-upscaler-mua2-shadow-pass.windows.patch` | Draws MUA2's shadow pass (pitch 1040 1x, before the scene) at 1x in every mode, apart from the same surface after the scene; the GPU targets line lists a target once per scale, with its share of host pixels. |
| Scene profiles | `patches/rexglue-sdk/0166-d3d12-scene-upscaler-profiles.windows.patch`, `0167-d3d12-scene-upscaler-gold-profile.windows.patch`, `0168-d3d12-scene-upscaler-mua2-profile.windows.patch` | `d3d12_scene_profile`: the game's rule for finding its scene surface (`mua1`; `mua1-gold`, which also takes Gold Edition's 4x draws over the scene's memory into the scene's pass; and `mua2`, which pins MUA2's scene surface and its own size); none leaves DLSS and FSR 3.1 off. |
| App | `src/settings.cpp` (`ApplyDlss`) | When DLSS is the Upscaling choice, sets `d3d12_dlss_mode` from the Upscaling quality, `d3d12_scene_profile` from the game, and points `d3d12_dlss_bridge` and `d3d12_dlss_data_path` (`<game cache>\ngx`) at the bridge beside the program. Any other choice sets `d3d12_dlss_mode=off`. |
| Launcher | `src/launcher.cpp`, `src/dlss_support.cpp` | Offers DLSS only when the effective card is NVIDIA, NVIDIA's requirements check (through the bridge) accepts it, and the card runs the ROV path. Otherwise an "NVIDIA DLSS: Unavailable: ..." row says why, and a saved DLSS choice becomes Off. |

## Scale domains

The Render resolution is the output scale: DLSS's output, the HUD, menus, post-processing and the presenter's input all
stay there. The 3D scene uses DLSS's render size, and the targets drawn before it use the smallest whole scale with at
least the scene's detail:

| Render resolution, mode | Output (HUD, menus, final) | Scene (DLSS input) | Targets drawn before the scene |
|---|---|---|---|
| 4x, Performance | 5120x2880 (4x) | 2560x1440 (2x) | 1x for those only the scene samples, 2x for the others |
| 4x, Balanced | 5120x2880 (4x) | about 2976x1670 (2.32x) | 3x |
| 4x, Quality | 5120x2880 (4x) | 3408x1920 (2.66x) | 3x |
| 3x, Performance | 3840x2160 (3x) | 1920x1080 (1.5x) | 1x for those only the scene samples, 2x for the others |
| 2x, Performance | 2560x1440 (2x) | 1280x720 (1x) | 1x |
| 2x, Quality; any DLAA; Original | the Render resolution | | the Render resolution (no smaller whole scale keeps the scene's detail) |

That table is MUA1's and Gold Edition's, whose scene is the 1280x720 canvas. MUA2's scene is 1040x624, so its output
is 1040x624 times the Render resolution (3120x1872 at 3x, which NVIDIA sizes the scene for) and the game's composite
scales it into the 1280x720 framebuffer at the Render resolution (3840x2160), where the HUD and menus draw. Its shadow
pass (0169) draws at 1x in every mode; its other targets stay at the Render resolution (see
[fsr3.md](fsr3.md#games)).

MUA1's GPU targets in log 249 (4x, Performance) and what 0162 does with them. The roles of the two pre-scene surfaces
are read from their formats, sizes and draw order, not from their shaders:

| Surface | Role | Before 0162 | 0162 |
|---|---|---|---|
| Color base 0, `k_8_8_8_8`, 2x MSAA, pitch 800, depth base 1128 | the 3D scene (color and depth) | 2x within the 4x EDRAM (the line printed the 4x size) | unchanged, 2x |
| `k_32_FLOAT`, 1x, pitch 560, 512x512, resolved twice a frame to 512x512 textures | single-channel depth, drawn before the scene: shadow maps | 4x (2048x2048) | 2x (1024x1024) |
| `k_8_8_8_8`, 1x, pitch 1040, 512x512 viewports, with depth base 1216 (1024x1024); resolved to 1024x2048 and 1024x1024 textures | an atlas of 512x512 tiles drawn before the scene (shadow or projected light textures) | 4x (2048x2048 per tile, 4096x4096 depth) | 2x |
| 1x, pitch 320, 320x180 | the glow chain, after the scene | 4x | 4x |
| 1x, pitch 1280, 1280x720 | the composite, HUD and menus | 4x | 4x |

No post-processing runs between the scene and DLSS: everything after the scene's last resolve reads the DLSS output.

### How a surface qualifies

A surface is a pitch and a sample count, so a pass's color and depth targets go together. In frames that draw the
scene:

- drawn before the scene in three such frames, and never after it: drawn at the smaller scale from the next frame;
- drawn after the scene once, or any draw after the scene samples a texture resolved from it (the resolve's
  destination range; a later resolve into the same memory replaces it): kept at the Render resolution for the session;
- the scene's own surface and pitch 0 never qualify;
- in Performance and Ultra Performance, a reduced surface whose resolved textures the scene's own draws sample in three
  frames goes to 1x, the game's own size, and its draws keep the game's own depth-bias slope. `d3d12_dlss_pre_scene_native
  = false` keeps them at the scene's scale.

Changes apply between frames only, so every draw and resolve of a surface in a frame agree. In frames without the scene
(menus), the surfaces already chosen stay smaller.

### Drawing smaller and resolving full size

Draws into a chosen surface get the host viewport and scissor scaled about the EDRAM origin, like the scene's, without
jitter. At each of its resolves, `scene_edram_stretch` copies the resolve rectangle from the EDRAM into a buffer the size
of the EDRAM, at the same addresses, each sample from the nearest smaller-scale pixel of its own guest pixel; the
resolve then copies from that buffer instead of the EDRAM. Clears still go to the EDRAM. The copy is raw, so it works
for every format, 64bpp included, every sample count and depth. The resolved textures are at the Render resolution, with
the detail of the smaller scale. The buffer is DLSS's staging buffer, made the size of the EDRAM (168 MB at 4x) when a
smaller scale applies.

The depth bias keeps the Render resolution's slope factor, as before, which is somewhat more bias than a smaller target
needs. `d3d12_dlss_pre_scene_scale = false` in `settings.toml` keeps every pre-scene target at the Render resolution
(read at start).

## How a frame goes

1. **Scene surface.** In a frame where it isn't known yet, every multisampled `k_8_8_8_8` color + depth surface with
   depth-tested draws is counted. At the swap, the one with the most draws (at least 20) becomes the scene surface. For
   MUA1 that is EDRAM color base 0, 2xMSAA, with D24S8 depth.
2. **Lazy start.** The first draw into the scene surface loads the bridge, initializes NGX, asks NVIDIA's optimal
   settings for the mode at the output size (guest video mode x Render resolution), picks the pre-scene scale, and
   creates the textures. The mode is `off` by default, which loads nothing.
3. **Pre-scene targets** (from the next frames on): drawn at the pre-scene scale and resolved full size, as above.
4. **Scene draws** (every draw into the scene surface, including particles) get:
   - a host viewport and scissor scaled to the render size about the EDRAM origin;
   - a Halton(2,3) jitter in render pixels, added to the NDC offset, with 8 x (output/render)^2 phases (8 to 72).

   The render width is snapped so that EDRAM tile edges, where predicated tiles meet, fall on whole render pixels.
5. **Capture.** Before each resolve of the scene surface (color or depth), `scene_capture` copies the resolved
   rectangle from the EDRAM into render-size textures:
   - color: the average of the samples, `RGBA8`;
   - depth: sample 0, D24 unorm or float, as `R32F`.

   Color is taken at every resolve after scene draws, before a resolve that clears it, so the last one wins. Depth is
   taken once between depth clears, at the first resolve after draws wrote it (a frame that writes none takes what the
   EDRAM holds then). A later resolve of the
   same pixels overwrites an earlier one; with MUA1's two predicated tiles (screen 0..672 and 640..1280 at 720p, the
   second at window offset -640), they meet exactly at render x 424 (Quality at Original) or 1280 (Performance at 4x).
6. **Right after each of those resolves**, the game's own copy holds only the smaller scene in a corner of its texture.
   `scene_encode` stretches that resolve's captured pixels to full size (bilinear for color, nearest for depth) into a
   staging buffer laid out like a 1x EDRAM, and the resolve copy shaders copy it into the same texture with the game's
   destination info. Draws that read the texture before the scene is done see the whole scene.
7. **DLSS step.** At the first draw outside the scene, or the swap:
   - `scene_motion` makes camera motion vectors (`RG16F`, render pixels, current to previous, `MVLowRes`) from the depth
     and a clip-to-previous-clip matrix.
   - DLSS is created (once) and evaluated on the real command list through `ExternalCall`, which can run on the async
     submission thread. The command processor then forgets its cached command list state, because NGX changes it.
   - The DLSS output goes the same way into the textures of the color resolves that no later resolve of the frame
     covers again (both tiles, but not a first copy of a tile that is resolved twice). The others keep their stretched
     copies.
8. **Everything after the scene** reads those textures at full size: glow, composite, HUD and menus.

### Inputs

- **Color:** the scene's LDR `k_8_8_8_8`, so `IsHDR` is off and no exposure texture is needed (pre-exposure 1).
- **Depth:** `DepthInverted` follows the scene's `zfunc` (GREATER or GEQUAL).
- **Camera motion:** draws whose vertex shader writes the position as one constant matrix times a fetched position
  (`scene_motion_analysis.h` reads that from the ucode disassembly; 18 of MUA1's 43 vertex shaders) give an
  object-to-clip matrix per draw. Draws are matched across frames by shader, vertex buffer, indices and occurrence. The
  candidates are the previous matrix times the inverse of the current one; the one at least a fifth of them agree with
  (static objects all agree) is the camera's. With none, the frame resets DLSS's history.
- **Jitter:** passed to DLSS in render pixels, with the same convention as the motion vectors.
- **Object motion:** none. Characters and other objects that move on their own get only the camera's motion, so DLSS
  can trail them. The way to add it is per-draw vertex position history in the DXBC translator (the Uncharted port's
  approach): store each vertex's clip position per draw identity, and write the difference to a velocity target. It is
  not started.

### Modes

Launcher Upscaling quality to `d3d12_dlss_mode`:

| Upscaling quality | DLSS mode |
|---|---|
| Native (shown as DLAA) | `dlaa`: render size = output size |
| Quality | `quality` |
| Balanced | `balanced` |
| Performance | `performance` |

`ultra_performance` works through the cvar but isn't in the launcher. NVIDIA's optimal-settings call picks each size.
`d3d12_dlss_preset` (0 = the DLL's default) sets the render preset.

## Fallbacks

Every failure is logged once, and the scene draws at full resolution from then on; so do the pre-scene targets, from the
next frame:

| Failure | Result |
|---|---|
| No bridge beside the program, or a runtime without 0161 | Logged by the app, mode stays off |
| Not an NVIDIA card, NVIDIA's check fails, NGX init fails, no render size for the mode | `DLSS: off for this session, ...` before any draw is scaled |
| Not the ROV path | `DLSS: off, it needs the rasterizer-ordered view render target path` |
| The textures or pipelines can't be created | Off for the session |
| Feature creation or evaluation fails | That frame shows whatever the output texture holds; off from the next frame |
| The scene was drawn smaller but never resolved where DLSS expects | That frame shows the small scene; off from the next frame |
| The scene is resolved in two passes in one frame | The second pass keeps its stretched copies; off from the next frame |
| A scene resolve outside the screen | Left as the game drew it; off from the next frame |

## Logs

- `NVIDIA DLSS: <game>: <mode> requested | scene at NVIDIA's size -> output ... | scene profile <mua1, mua1-gold or mua2>`
  (app) and the `Upscaling:` line.
- `DLSS: scene surface found (scene profile <profile>): ...`.
- `DLSS: Super Resolution <mode> | scene profile <profile> | output ... | NVIDIA recommends ... | scene input ... |
  jitter ... | preset ...`.
- `DLSS: off, no scene profile for this game ...` when the game has none (NGX is never loaded).
- `DLSS: scale domains | output, HUD and menus 5120x2880 (4x) | scene 2560x1440 (2x) | targets drawn before the scene
  2x`.
- `DLSS: targets drawn before the scene at 2x: pitch 560 1x MSAA, pitch 1040 1x MSAA | kept at 4x (drawn into or
  sampled after the scene): pitch 320 1x MSAA, pitch 1280 1x MSAA`, whenever that changes (a few times a session).
- `DLSS: active | scene color ... | depth ... | motion ... | output ... written to N of M color resolve(s), K depth
  resolve(s) stretched`, once.
- Every 600 frames: frames with a camera matrix found and with it moving, history resets, and static draws agreeing.
- MUA2: `DLSS: pitch 1040 1x MSAA before the scene (shadow maps and their filtered copies) at 1x - ...`, once the
  checks pass, or `... back at 3x for the session - sampled after the scene`.
- Every 10 seconds, `GPU targets`: each target's guest size and its host size at the scale its draws used, for example
  `512x512 guest -> 1024x1024 host at 2x`, once per scale, with its share of the guest and of the host pixels; with the
  `Performance` line's FPS and GPU wait.

What a correct run at 4x Performance shows:

- `scene 2560x1440 (2x)` and `targets drawn before the scene 2x` in the scale domains line;
- pitch 560 and pitch 1040 at 1x (sampled only by the scene), pitch 320 and pitch 1280 kept at 4x;
- the GPU targets line with the scene and the pre-scene targets at 2x and the 1280x720 composite at 4x.

## Fourth run (log 251)

The new probes measured the EDRAM depth before and after each scene resolve of the sampled frame. The first resolve of
each tile (no clears) had real depth on every pixel (D24 0x49B32A to 0xFDF30A); right before the second one (which
clears color and depth), sample 0 was at the clear value everywhere: the 9 scene draws between the two cover the tile
with depth at the clear value. 0163's first version counted those as depth writes and took depth again at the second
resolve. Now depth is taken once between depth clears. (That log's "before" of the first resolve read the probe's
starting values, a bug of the probe, fixed.)

## Third run (log 250)

With 0162 at 4x Performance, the pre-scene targets were at 2x and the composite and glow at 4x, and FPS went up as
above. The depth check sampled a gameplay frame this time: 179 of its 413 scene draws wrote depth, yet the DLSS depth
was the clear value everywhere, while camera motion worked (11 of 20 static draws agreeing, 8.65 render pixels mean).

Gameplay draws each tile as `S199 C S8 C`: 199 scene draws, a resolve, 8 more scene draws, a second resolve. Any scene
draw marked depth as fresh, so the second resolve took depth again. When the first resolve clears depth, that took the
clear value over the tile's real depth, for both tiles. The capture's addressing and decode match the ROV writer (color
uses the same addressing and arrives right; unorm depth is round_ne(z x 0xFFFFFF) in bits 8 to 31 and float depth
`xenos::Float32To20e4`, both tested against the decode). 0163 counts only draws that write depth. Which of the first
resolve's clears are on wasn't logged; the new probes log it.

## Second run (logs 246 to 249)

The picture was right at every mode tried. The diagnostics showed:

- Gameplay resolves the scene surface 4 times per frame (twice per tile), 2 of them getting the DLSS output.
- The check fired on a light frame right after loading (38 to 55 scene draws). Its depth was all at the clear value in
  logs 246, 247 and 249, and nearly so in log 248 (min 0.89, mean 0.99996, DLAA at 4x); the motion vectors, computed
  from the same texture, were uniform. So in those frames the DLSS depth really was the clear value, because almost
  nothing in them drew depth; whether gameplay's depth is right is still unmeasured, which the gameplay trigger and
  the depth-writing draw count now settle.
- Check C most likely read the guest textures in another channel order (the front buffer's means are the DLSS
  output's with red and blue swapped), and it is skipped above Original, so it is gone, with D.
- At 4x Performance, heavy scenes: 33 to 38 FPS, 23 to 28 ms GPU wait, with the two pre-scene surfaces drawn at 4x.

## First run (log 245)

DLSS started and evaluated every frame, but the first version left the game's own reduced copy (the scene in a corner,
the rest stale or cleared) in each scene texture until the end of the scene, wrote the DLSS output into textures later
resolves replaced, took depth only at color resolves, and read the single-sampled staging buffer with a two-sample
average. The black rectangles and displaced pieces came from the first of these. The second version fixed all four.

## Licensing before DLSS ships

Releases do not include the bridge or `nvngx_dlss.dll`. `tools/package-release.ps1` copies a fixed file list, and
`mua_dlss_bridge_tests` checks that the list names no DLSS file. Shipping is governed by the NVIDIA RTX SDKs License
(`tools\nvidia-dlss\310.9.16\LICENSE.txt`):

- Ship only `lib/Windows_x86_64/rel/nvngx_dlss.dll`, unmodified, with NVIDIA's signature intact. Never ship the
  watermarked development DLL.
- Never commit or ship the headers or libraries.
- Include NVIDIA's license text with the DLL.
- Add the Programming Guide's third-party notices (section 9.6) to `packaging/licenses/THIRD-PARTY-LICENSES.txt`.
- Follow the attribution and trademark terms (license supplement section 7).
- Read the supplement's notification clause before a public release.
