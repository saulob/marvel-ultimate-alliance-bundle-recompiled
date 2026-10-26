<div align="center">

# Marvel Ultimate Alliance - PC Port

### Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2 for Xbox 360, running natively on Windows, and experimentally on Linux and Steam Deck, through static recompilation, with the TRG launcher and modern PC options

[![Latest release](https://img.shields.io/github/v/release/saulob/marvel-ultimate-alliance-bundle-recompiled?style=for-the-badge&label=release&color=D4A42C&labelColor=07101f&include_prereleases&cacheSeconds=300)](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/saulob/marvel-ultimate-alliance-bundle-recompiled/total?style=for-the-badge&color=D4A42C&labelColor=07101f&cacheSeconds=300)](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20experimental%20%7C%20Steam%20Deck%20experimental-00acc1?style=for-the-badge&labelColor=07101f)

<sub>The original Xbox 360 game code, statically recompiled to native PC code with the <a href="https://github.com/rexglue/rexglue-sdk">ReXGlue SDK</a>. <b>No game files included</b>: bring your own Xbox 360 disc images.</sub>

</div>

## Features

- **Three games in one bundle**: Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2, each recompiled from its own game code into its own program, chosen from one launcher.
- **Native static recompilation**: the games' own code, not an emulator. Windows (Direct3D 12), plus an experimental Linux / Steam Deck AppImage (Vulkan).
- **TRG launcher** with a game selector, a disc image installer per game, and Display, Graphics, Gameplay, Achievements, Controls and About pages for the selected game.
- **Launcher updates**: the launcher checks GitHub releases once per start and installs a newer release only when you choose UPDATE NOW. Game files, saves, settings and logs are kept.
- **Display**: Windowed or Borderless Fullscreen (**F11**), window size, monitor and VSync.
- **Native aspect ratios**: 16:9, 16:10, 21:9 and 32:9 ultrawide, 4:3 and 5:4. The games render at the chosen shape instead of stretching a 16:9 picture.
- **Render resolution** from the original 720p picture up to 4x, independent of the window size.
- **Upscaling (Windows)**: AMD FSR 1, with Native, Quality, Balanced and Performance modes that draw the game below the Render resolution and scale it up. Marvel Ultimate Alliance also has NVIDIA Image Scaling (NIS), with the same modes and a sharpness setting, and AMD CAS sharpening.
- **HD Text (Marvel Ultimate Alliance and Gold Edition)**: the games' text and controller icons rebuilt at higher resolution to match the Render resolution.
- **Frame rate**: 30 (the games' original rate), 60 (default), 90, 120, 144, 165, 240 or Unlimited, chosen per game.
- **Graphics card selection (Windows), anti-aliasing and texture filtering** choices.
- **Local saves and profiles** for each game, and **player names** for up to four local players.
- **Offline achievements**: each game's achievements are kept on this PC, listed with **F7** and in the launcher. There is no Xbox Live connection.
- **Xbox controller support** with no setup, and remappable keyboard play.
- **Skip intro**: start directly at the title screen.
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

1. Download **MarvelUltimateAlliance-v0.1.0-windows-x64.zip** from the [latest release](https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/releases/latest) and extract it anywhere you can write to (not "Program Files"). Keep its files together: the launcher starts the game programs beside it.
2. Run **marvel_ultimate_alliance_bundle.exe**. The launcher opens.
3. Choose the game in the strip under the header: **MARVEL ULTIMATE ALLIANCE**, **GOLD EDITION** or **MARVEL ULTIMATE ALLIANCE 2**.
4. On the **Play** page click **Install from package...** and pick that game's disc image.
5. Wait until **Ready to play** appears. The game files are copied into the game's folder under `game` next to the program (`mua1`, `mua1-gold` or `mua2`).
6. Press **PLAY**.

**No game files are included** in the release. Each game is installed on its own; **Reinstall from package...** repairs a game's files and keeps your settings and saves. A new install is extracted and verified next to the old one, which is kept until the new one is in place.

### Linux and Steam Deck

**Experimental.** This is a native Linux build (ReXGlue's patched Linux runtime and Vulkan GPU plugin), not Proton or Wine, with the launcher and the three games in one x86_64 AppImage. It is built and checked by script on Ubuntu 20.04 (it needs glibc 2.31 or newer), but it has not been broadly validated on real Linux PCs or Steam Deck hardware: Steam Deck is expected to work through the AppImage, and reports are welcome. Upscaling and the Graphics card selector are Windows only.

1. Download **MarvelUltimateAlliance-v0.1.0-x86_64.AppImage** from the release and mark it executable (right-click > **Properties** > **Permissions** > **Is executable**, or `chmod +x`).
2. Run it from a folder you can write to, then install each game from its disc image on the **Play** page as above. The launcher works as on Windows.
3. On a Steam Deck, do this in **Desktop Mode**, then add the AppImage with **Games > Add a Non-Steam Game to My Library** and play it from **Game Mode**.

`game`, `logs` and `user_data` are created next to the AppImage, outside the read-only AppImage. Where FUSE is not available, the AppImage unpacks itself to a temporary folder and runs from there. Details, requirements and known limits are in [docs/linux.md](docs/linux.md).

### Windows SmartScreen

Windows may warn the first time you run the program, because the release is not digitally signed. Choose **More info**, then **Run anyway**.

## The launcher

The selector under the header chooses the game, and the pages beside it belong to that game. Your choices are saved to `settings.toml` in the `user_data` folder next to the program; the settings that belong to one game, such as the frame rate, are saved in that game's folder under `user_data`. To turn the launcher off, set **Show this launcher** to **Off** on the Play page. Hold **Shift** while starting to bring it back.

- **Play**: disc image install and reinstall, status
- **Display**: Display mode, Window size, Monitor, Aspect Ratio and VSync
- **Graphics**: Graphics card (Windows), Render resolution, Upscaling (Windows), Anti-aliasing, Multisampling where it applies, Texture filtering, and HD Text (Marvel Ultimate Alliance and Gold Edition)
- **Gameplay**: Skip intro, Frame rate, frame rate and render resolution in the title, frame counter, and Player names
- **Achievements**: the selected game's unlocked achievements and gamerscore
- **Controls**: keyboard on or off, every key remappable, and **Keyboard defaults** to restore the standard layout
- **About**: versions, folders (game files, saves and settings, logs), the settings file, **Detailed logging** and **Open logs folder**

**Render resolution** draws the game at a multiple of its own picture (Original, 2x, 3x or 4x, sized for the chosen aspect ratio) and then scales it to the window. **Upscaling** (Windows) uses AMD FSR 1 to scale the picture to the window; Quality, Balanced and Performance draw the game 1.5, 1.7 and 2 times smaller along each axis than the Render resolution, which costs less GPU time. In Marvel Ultimate Alliance it can also use NVIDIA NIS, with the same modes and a Sharpness slider (50% is NVIDIA's default), or AMD CAS, which keeps the Render resolution and only sharpens. On Linux the game always draws at the Render resolution.

**HD Text** (Marvel Ultimate Alliance and Gold Edition, On by default) rebuilds the games' font atlases at 2x the size at Render resolution 2x, and at 4x the size at 3x and 4x, when the game starts. The layout, colours and animation stay the game's, the game files are not changed, and nothing taken from the game ships with the port. It uses about 6 MB of memory at 2x and 24 MB at 3x and 4x. HD Text is not available for Marvel Ultimate Alliance 2.

**Frame rate** 30 is the games' original rate; 60 is the default, and the higher rates and Unlimited are for high refresh rate displays. The setting is kept per game.

**Player names** are shared by the three games: player N is the controller in port N. They only change the names the games show; profiles, saves and achievements stay where they are.

## Hotkeys

| Key | While playing |
| --- | --- |
| **F11** | Windowed or Borderless Fullscreen (in the launcher too) |
| **F2** | Show or hide the frame counter |
| **F7** | Achievements |
| **F9** | Write the last seconds of audio activity to the log, for an audio problem report |

During gameplay the mouse cursor follows the last input device: mouse activity shows it, keyboard or controller activity hides it, and overlays show it.

## Saves, achievements and settings

Each game keeps its saves, profile, achievements and own settings in its folder under `user_data`: `mua1`, `mua1_gold` and `mua2`. Copy the whole program folder to another PC to take them with you. To update by hand, extract the new version over the old folder, or move `user_data` (and `game`) into the new one: deleting the old folder deletes `user_data` with it.

Achievements are unlocked offline and kept on this PC: 46 in Marvel Ultimate Alliance, 58 in Gold Edition and 50 in Marvel Ultimate Alliance 2.

## Updates

The launcher checks the latest GitHub release once per start. A newer release shows a banner in the header; nothing is installed until **UPDATE NOW** has downloaded and verified the package and **RESTART TO UPDATE** is pressed. On Windows the update is `MarvelUltimateAlliance-v<version>-windows-x64.zip`: its SHA-256 must match the release's checksum before anything is unpacked, and `trg-updater.exe` replaces the launcher, the three game programs, the runtime and the other bundle files together, backs up what it replaces, rolls back on any failure and restarts the launcher. On Linux the AppImage downloads `MarvelUltimateAlliance-v<version>-x86_64.AppImage`, checks it the same way, replaces itself and starts the new AppImage. `game`, `user_data`, saves, settings and logs are never replaced.

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

## Credits

- Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2 are games by their original rights holders. This is an unofficial fan-made PC port. It is not affiliated with or endorsed by them, and it contains none of their files.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) for the recompiler and runtime (BSD 3-Clause)
- [TRG Launcher](https://github.com/TekRantGaming/trg-launcher), the launcher interface, pinned at v1.1.0 (revision b384a28), licensed under the MIT License
- The licenses of the libraries the release includes are in the `licenses` folder of the ZIP, and in `usr/share/doc/marvel_ultimate_alliance` inside the AppImage

## Licenses

Third-party components retain their respective licenses and notices. See [licenses/ReXGlue-LICENSE.txt](packaging/licenses/ReXGlue-LICENSE.txt), [licenses/THIRD-PARTY-LICENSES.txt](packaging/licenses/THIRD-PARTY-LICENSES.txt) and, for the Linux AppImage, [licenses/THIRD-PARTY-LICENSES-LINUX.txt](packaging/licenses/THIRD-PARTY-LICENSES-LINUX.txt).

No game files are included. Marvel Ultimate Alliance, Marvel Ultimate Alliance 2 and their assets are not covered by this project.
