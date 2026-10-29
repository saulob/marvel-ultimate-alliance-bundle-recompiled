<div align="center">

# Marvel Ultimate Alliance - PC Port

### Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2 for Xbox 360, running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/marvel-ultimate-alliance-bundle-recompiled?style=for-the-badge&label=release&color=D4A42C&labelColor=07101f&include_prereleases&cacheSeconds=300)](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/marvel-ultimate-alliance-bundle-recompiled/total?style=for-the-badge&color=D4A42C&labelColor=07101f&cacheSeconds=300)](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Xbox 360 disc images.</sub>

</div>

<img width="940" height="529" alt="MarvelBundle" src="https://github.com/user-attachments/assets/698fc8fc-23c1-4b56-9309-6330b743f5d0" />

## What's new in v0.2.0

### Super Resolution and Graphics

- **NVIDIA DLSS Super Resolution** in all three games: DLAA, Quality, Balanced and Performance. Windows, Direct3D 12, GeForce RTX graphics cards. It uses NVIDIA DLSS SDK 310.9.16, and the DLSS runtime files it needs are included in the Windows release, so nothing needs downloading. DLSS Frame Generation is not used.
- **AMD FidelityFX Super Resolution 3.1** in all three games: Native, Quality, Balanced, Performance and Ultra Performance, with a sharpness slider. Windows, Direct3D 12, on NVIDIA, AMD and Intel graphics cards; AMD's runtime is included in the Windows release.
- **AMD FSR 1** on Windows and on Linux (Vulkan).
- **Render resolution** from the original 720p picture up to 4x, which DLSS and FSR 3.1 use as their output size, plus a round of performance and rendering fixes in the graphics plugin.

### Gameplay cheats

New **Cheats** page in the launcher, saved separately for each game, and an in-game cheat menu on **F10**.

| Cheat | Marvel Ultimate Alliance | Gold Edition | Marvel Ultimate Alliance 2 |
| --- | --- | --- | --- |
| Infinite Health | yes | yes | yes |
| Infinite Energy | yes | yes | yes |
| Turbo Attack | yes | yes | yes |
| Coin Multiplier (2x, 5x, 10x) | yes | yes | not offered |

### Linux and Steam Deck

- **Issue #3 is fixed:** the AppImage now correctly finds and launches the bundled runtimes for Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2.
- Native Vulkan backend in one x86_64 AppImage that holds the launcher and all three game runtimes (glibc 2.31 or newer).
- Vulkan graphics-device selection, Vulkan VSync handling, AMD FSR 1, dynamic page streaming, shader handling and rendering correctness fixes, and performance improvements.
- Linux and Steam Deck remain experimental. Broader testing on real Steam Deck and Linux hardware is welcome.

## Features

- **Three games in one bundle**: Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2, each recompiled from its own game code into its own program, chosen from one launcher.
- **Native static recompilation**: the games' own code, not an emulator. Windows 10/11 x64 (Direct3D 12), plus an experimental Linux x86_64 AppImage (Vulkan) that also runs on Steam Deck.
- **TRG launcher** with a game selector, a disc image installer per game, and Display, Graphics, Gameplay, Cheats, Achievements, Controls and About pages for the selected game.
- **Launcher updates**: the launcher checks GitHub releases once per start and installs a newer release only when you choose UPDATE NOW. Game files, saves, settings and logs are kept.
- **Display**: Windowed or Borderless Fullscreen (**F11**), window size, monitor and VSync.
- **Native aspect ratios**: 16:9, 16:10, 21:9 and 32:9 ultrawide, 4:3 and 5:4. The games render at the chosen shape instead of stretching a 16:9 picture.
- **Render resolution** from the original 720p picture up to 4x, independent of the window size.
- **NVIDIA DLSS Super Resolution** (Windows, Direct3D 12, GeForce RTX): DLAA, Quality, Balanced and Performance, rebuilding only the 3D scene from several frames with motion and depth while the HUD and menus stay at the Render resolution. NVIDIA DLSS SDK 310.9.16; the required DLSS runtime files are included in the Windows release. No Frame Generation. See [docs/dlss.md](docs/dlss.md).
- **AMD FidelityFX Super Resolution 3.1** (Windows, Direct3D 12, NVIDIA, AMD and Intel graphics): Native, Quality, Balanced, Performance and Ultra Performance, with a sharpness slider; AMD's runtime is included in the Windows release. See [docs/fsr3.md](docs/fsr3.md).
- **AMD FSR 1** (Windows and Linux/Vulkan): Native, Quality, Balanced and Performance modes that draw the game below the Render resolution and scale it up.
- **HD Text (Marvel Ultimate Alliance and Gold Edition)**: the games' text and controller icons rebuilt at higher resolution to match the Render resolution.
- **Frame rate**: 30 (the games' original rate), 60 (default), 90, 120, 144, 165, 240 or Unlimited, chosen per game.
- **Graphics card selection, anti-aliasing and texture filtering** choices.
- **Local saves and profiles** for each game, and **player names** for up to four local players.
- **Offline achievements**: each game's achievements are kept on this PC, listed with **F7** and in the launcher. There is no Xbox Live connection.
- **Xbox controller support** with no setup, and remappable keyboard play.
- **Skip intro**: start directly at the title screen.
- **Cheats**: Infinite Health, Infinite Energy and Turbo Attack in all three games, and a Coin Multiplier (2x, 5x or 10x) in Marvel Ultimate Alliance and Gold Edition only. They are off by default and saved per game on the launcher's Cheats page; **F10** opens an in-game menu that changes them for the current session only (it needs a keyboard).
- **In-game frame counter** (**F2**) and optional frame rate or render resolution in the window title.
- **Diagnostics**: one log per session with the system, settings and frame timing, and a `CRASH REPORT` section after a crash on Windows.
- **Portable user data**: saves, settings and achievements are in a `user_data` folder beside the program.

Online play and Xbox Live are not supported.

## What you need

- **Windows:** Windows 10 or 11, 64-bit, and a DirectX 12 graphics card, with the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs have it.
- **Linux and Steam Deck (experimental):** 64-bit x86 Linux or a Steam Deck, with glibc 2.31 or newer (Ubuntu 20.04 or later), a Vulkan driver and an X11 or Wayland desktop. See [docs/linux.md](docs/linux.md).
- **Your own Xbox 360 disc image** (`.iso`) of each game you want to play, these exact versions:

| | Marvel Ultimate Alliance | Gold Edition | Marvel Ultimate Alliance 2 |
| --- | --- | --- | --- |
| Title ID | `415607DA` | `415607DA` | `4156082F` |
| Media ID | `26848425` | `5A20A6D4` | `39B3E2C9` |
| `default.xex` size | 3,850,240 bytes | 3,981,312 bytes | 12,664,832 bytes |
| Game files | about 5.9 GB | about 5.9 GB | about 7.1 GB |

`default.xex` SHA-256:

- Marvel Ultimate Alliance: `44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A`
- Gold Edition: `06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7`
- Marvel Ultimate Alliance 2: `2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440`

The launcher checks the Title ID and Media ID when you pick the disc image and checks `default.xex` after extracting it. Any other version is refused, because each program is built for that exact game code.

## Installing

1. Download **MarvelUltimateAlliance-v0.2.0-windows-x64.zip** from the [latest release](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases/latest) and extract it anywhere you can write to (not "Program Files"). Keep its files together: the launcher starts the game programs beside it.
2. Run **marvel_ultimate_alliance_bundle.exe**. The launcher opens.
3. Choose the game in the strip under the header: **MARVEL ULTIMATE ALLIANCE**, **GOLD EDITION** or **MARVEL ULTIMATE ALLIANCE 2**.
4. On the **Play** page click **Install from package...** and pick that game's disc image.
5. Wait until **Ready to play** appears. The game files are copied into the game's folder under `game` next to the program (`mua1`, `mua1-gold` or `mua2`).
6. Press **PLAY**.

**No game files are included** in the release. Each game is installed on its own; **Reinstall from package...** repairs a game's files and keeps your settings and saves. A new install is extracted and verified next to the old one, which is kept until the new one is in place.

### Linux and Steam Deck

**Experimental.** This is a native Linux build (ReXGlue's patched Linux runtime and Vulkan GPU plugin), not Proton or Wine, with the launcher and the runtimes of all three games in one x86_64 AppImage. It is built and checked by script on Ubuntu 20.04 (it needs glibc 2.31 or newer), but it has not been broadly validated on real Linux PCs or Steam Deck hardware: Steam Deck is expected to work through the AppImage, and testing and reports from real devices are welcome. The launcher offers the same settings as on Windows; the Graphics card selector lists the Vulkan devices, VSync follows Vulkan's present modes, and Upscaling offers AMD FSR 1. AMD FSR 3.1 and NVIDIA DLSS run in the Direct3D 12 graphics plugin and are Windows only.

The v0.2.0 AppImage fixes issue #3: it now correctly finds and launches the bundled runtimes for Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2. v0.2.0 also brings Vulkan graphics-device selection and VSync handling, dynamic page streaming, shader handling and rendering correctness fixes, and performance improvements to Linux.

1. Download **MarvelUltimateAlliance-v0.2.0-x86_64.AppImage** from the release and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it from a folder you can write to, then install each game from its disc image on the **Play** page as above. The launcher works as on Windows.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

`game`, `logs` and `user_data` are created next to the AppImage, outside the read-only AppImage. Where FUSE is not available, the AppImage unpacks itself to a temporary folder and runs from there. Details, requirements and known limits are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. Choose **More info**, then **Run anyway**.

## The launcher

The selector under the header chooses the game, and the pages beside it belong to that game. Your choices are saved to `settings.toml` in the `user_data` folder next to the program; the settings that belong to one game, such as the frame rate, are saved in that game's folder under `user_data`. To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back.

- **Play**: disc image install and reinstall, status
- **Display**: Display mode, Window size, Monitor, Aspect Ratio and VSync
- **Graphics**: Graphics card, Render resolution, Upscaling, Anti-aliasing, Multisampling where it applies, Texture filtering, and HD Text (Marvel Ultimate Alliance and Gold Edition)
- **Gameplay**: Skip intro, Frame rate, frame rate and render resolution in the title, frame counter, and Player names
- **Cheats**: Infinite Health, Infinite Energy, Coin Multiplier (Marvel Ultimate Alliance and Gold Edition) and Turbo Attack
- **Achievements**: the selected game's unlocked achievements and gamerscore
- **Controls**: keyboard on or off, every key remappable, and **Keyboard defaults** to restore the standard layout
- **About**: versions, folders (game files, saves and settings, logs), the settings file, **Detailed logging** and **Open logs folder**

**Render resolution** draws the game at a multiple of its own picture (Original, 2x, 3x or 4x, sized for the chosen aspect ratio) and then scales it to the window. **Upscaling** uses AMD FSR 1 to scale the picture to the window; Quality, Balanced and Performance draw the game 1.5, 1.7 and 2 times smaller along each axis than the Render resolution, which costs less GPU time. In all three games, **AMD FSR 3.1** (NVIDIA, AMD and Intel graphics) and **NVIDIA DLSS** (GeForce RTX) draw only the 3D scene below the Render resolution and rebuild it there from several frames with motion and depth; AMD's `amd_fidelityfx_dx12.dll` and NVIDIA's `nvngx_dlss.dll` (with this project's `mua_dlss_bridge.dll`) ship beside the programs. DLSS needs a GeForce RTX card; FSR 3.1 and DLSS run in the Direct3D 12 graphics plugin and are Windows only; Linux offers FSR 1. DLSS Frame Generation is not used.

**HD Text** (Marvel Ultimate Alliance and Gold Edition, On by default) rebuilds the games' font atlases at 2x the size at Render resolution 2x, and at 4x the size at 3x and 4x, when the game starts. The layout, colours and animation stay the game's, the game files are not changed, and nothing taken from the game ships with the port. It uses about 6 MB of memory at 2x and 24 MB at 3x and 4x. HD Text is not available for Marvel Ultimate Alliance 2.

**Frame rate** 30 is the games' original rate; 60 is the default, and the higher rates and Unlimited are for high refresh rate displays. The setting is kept per game.

**Cheats** are saved per game and take effect the next time that game starts. Infinite Health and Infinite Energy keep every party hero at their own maximum; Coin Multiplier multiplies the coins the game awards (it is not offered for Marvel Ultimate Alliance 2); Turbo Attack repeats the basic attack while the attack button stays held. Press **F10** while playing to open the Cheats menu: what it changes applies to the current session only and is not saved, and it needs a keyboard (on Steam Deck, set cheats on the launcher's Cheats page).

**Player names** are shared by the three games: player N is the controller in port N. They only change the names the games show; profiles, saves and achievements stay where they are.

## Hotkeys

| Key | While playing |
| --- | --- |
| **F11** | Windowed or Borderless Fullscreen (in the launcher too) |
| **F2** | Show or hide the frame counter |
| **F7** | Achievements |
| **F10** | Cheats menu (session only) |
| **F9** | Write the last seconds of audio activity to the log, for an audio problem report |

During gameplay the mouse cursor follows the last input device: mouse activity shows it, keyboard or controller activity hides it, and overlays show it.

## Saves, achievements and settings

Each game keeps its saves, profile, achievements and own settings in its folder under `user_data`: `mua1`, `mua1_gold` and `mua2`. Copy the whole program folder to another PC to take them with you. To update by hand, extract the new version over the old folder, or move `user_data` (and `game`) into the new one: deleting the old folder deletes `user_data` with it.

Achievements are unlocked offline and kept on this PC: 46 in Marvel Ultimate Alliance, 58 in Gold Edition and 50 in Marvel Ultimate Alliance 2.

## Updates

The launcher checks the latest GitHub release once per start. A newer release shows a banner in the header; nothing is installed until **UPDATE NOW** has downloaded and verified the package and **RESTART TO UPDATE** is pressed. On Windows the update is `MarvelUltimateAlliance-v<version>-windows-x64.zip`: its SHA-256 must match the release's checksum before anything is unpacked, and `trg-updater.exe` replaces the launcher, the three game programs, the runtime and the other bundle files together, backs up what it replaces, rolls back on any failure and restarts the launcher. On Linux the AppImage downloads `MarvelUltimateAlliance-v<version>-x86_64.AppImage`, checks it the same way, replaces itself and starts the new AppImage. `game`, `user_data`, saves, settings and logs are never replaced.

## Checksums and security

The v0.2.0 release files have these SHA-256 values, also published with the release as `MarvelUltimateAlliance-v0.2.0-windows-x64.sha256` and `MarvelUltimateAlliance-v0.2.0-x86_64.sha256`:

| File | SHA-256 |
| --- | --- |
| `MarvelUltimateAlliance-v0.2.0-windows-x64.zip` | `2bcab1cee6afedcfd5ebaf366a81a798c454792742d9fae9f0c2fa4065adb8a6` |
| `MarvelUltimateAlliance-v0.2.0-x86_64.AppImage` | `d3aa486c227aafa6067b9b9c2137bcbac760225bcbfaf77f1e8edc4b01ee2256` |

The final release artifacts were scanned with VirusTotal before publication: [Windows ZIP](https://www.virustotal.com/gui/file/2bcab1cee6afedcfd5ebaf366a81a798c454792742d9fae9f0c2fa4065adb8a6), [AppImage](https://www.virustotal.com/gui/file/d3aa486c227aafa6067b9b9c2137bcbac760225bcbfaf77f1e8edc4b01ee2256). A scan is a useful extra check, not a guarantee of safety; verify the SHA-256 of your download.

## Screenshots

<img width="1288" height="774" alt="2026-10-25_22-23" src="https://github.com/user-attachments/assets/3c5a6bd3-fa0e-48f3-b973-7ec885fff9c0" />

## Reporting problems

If a game does not start, crashes, runs slowly, or has a graphics, audio, save, controller or launcher problem, please open an [issue](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/issues). Attach the newest `marvel_ultimate_alliance_*.log` file from the `logs` folder next to the program (the launcher's About page opens the folder), and say which game you played and what you were doing. The log already records the port version, the game and its files' fingerprint, the system, graphics card and driver, display, the launcher settings and the frame timing, so there is no need to list them. For an audio glitch, press **F9** right after it and then attach the log.

The launcher and the game it starts write to the same log for one session, and the newest 30 sessions are kept. **Detailed logging** (About page) is on by default; turned off, a normal session's log is discarded on exit, while a crash keeps it. After a crash on Windows the log ends with a `CRASH REPORT` section; no separate crash file or minidump is written. On Linux the log also records the distribution, kernel, glibc, Vulkan device and driver; please say whether you start the AppImage from Steam.

## Building it yourself

The recompiled C++ is generated from your own game files, so the repository does not contain it.

```powershell
.\tools\build.ps1          # sets up the SDK, builds the patched runtime, generates the recompiled code when needed, then builds Release
.\tools\test.ps1           # headless unit tests
.\tools\codegen.ps1        # forces a codegen run for every game, with logs in generated\codegen-<game>.log
.\tools\package-release.ps1   # Release build and the release ZIP with its .sha256 in dist\ (publishes nothing)
```

- Needs Visual Studio with the C++ Clang tools and the TRG launcher: add `third_party\trg-launcher` or pass `-TrgLauncherDir`.
- Each game's `default.xex` goes in its folder under `game` (`game\mua1`, `game\extracted` for Gold Edition, `game\mua2`); `.\tools\extract-disc.ps1` fills `game\extracted`.
- The ReXGlue runtime and Xenos GPU plugin in `tools\rexglue` are built from the pinned ReXGlue SDK v0.10.0 source with the patches in `patches\rexglue-sdk`. `tools\build.ps1` rebuilds them whenever the patches or the compiler change and refuses to start with other copies next to the executable.
- The Linux AppImage is built on Ubuntu 20.04 with `tools/package-linux.sh`, from the code `tools/codegen.ps1` generated on Windows. See [docs/linux.md](docs/linux.md).

## Sponsoring

Marvel Ultimate Alliance Bundle Recompiled is maintained and developed in my free time. If you enjoy the project or find it useful, consider supporting its continued development.

<p align="center">
<a href="https://github.com/sponsors/saulob" target="_blank"><img src="https://img.shields.io/badge/GitHub%20Sponsors-Sponsor-EA4AAA?style=for-the-badge&logo=githubsponsors&logoColor=white" alt="GitHub Sponsors"></a>
</p>

## Credits

- Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2 are games by their original rights holders. This is an unofficial fan-made PC port. It is not affiliated with or endorsed by them, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0 (revision b384a28), licensed under the MIT License
- [NVIDIA DLSS](https://github.com/NVIDIA/DLSS): DLSS Super Resolution uses NVIDIA DLSS SDK 310.9.16. `nvngx_dlss.dll` is distributed under NVIDIA's DLSS license (`licenses/nvidia-dlss`). `mua_dlss_bridge.dll` is this project's MIT-licensed bridge (`licenses/mua-dlss-bridge`). NVIDIA, NVIDIA RTX, GeForce RTX and DLSS are trademarks and/or registered trademarks of NVIDIA Corporation. This project is not sponsored or endorsed by NVIDIA.
- [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) 1.1.4 for FSR 3.1: AMD's signed `amd_fidelityfx_dx12.dll` (MIT License)
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP, and in `usr/share/doc/marvel_ultimate_alliance` inside the AppImage

## Licenses

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt), [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt) and, for the Linux AppImage, [licenses/THIRD-PARTY-LICENSES-LINUX.txt](packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt).

No game files are included. Marvel Ultimate Alliance, Marvel Ultimate Alliance 2 and their assets are not covered by this project.
