## Marvel Ultimate Alliance PC Port v0.2.0

Marvel Ultimate Alliance, Gold Edition and Marvel Ultimate Alliance 2 now have NVIDIA DLSS and AMD FSR 3.1 Super Resolution, gameplay cheats, and a much improved Linux and Steam Deck build. Play all three natively from one launcher on Windows 10/11 x64 or a Linux x86_64 AppImage.

### Super Resolution and Graphics
- **NVIDIA DLSS Super Resolution** in all three games: DLAA, Quality, Balanced and Performance. Windows, Direct3D 12, GeForce RTX graphics cards. DLSS rebuilds only the 3D scene from several frames using motion and depth, so the HUD and menus stay sharp at the Render resolution. It uses NVIDIA DLSS SDK 310.9.16, and the required DLSS runtime files are included in the Windows download: there is nothing else to install. DLSS Frame Generation is not used.
- **AMD FidelityFX Super Resolution 3.1** in all three games: Native, Quality, Balanced, Performance and Ultra Performance, with its own sharpness slider. Windows, Direct3D 12, on any NVIDIA, AMD or Intel graphics card. AMD's signed runtime is included.
- **AMD FSR 1** on both Windows and Linux (Vulkan), with Native, Quality, Balanced and Performance modes.
- **Render resolution** up to 4x, which the Super Resolution modes use as their output size, so you can draw the scene below it and scale it up.
- Performance and rendering improvements across the graphics plugin: shader and pipeline handling, constant and index buffer reuse, command submission, and fixes for render targets and textures at high Render resolutions.

### Gameplay Cheats
- A new **Cheats** page in the launcher. Settings are saved separately for each game.
- **Infinite Health** and **Infinite Energy**: every party hero stays at their own maximum. Marvel Ultimate Alliance, Gold Edition and Marvel Ultimate Alliance 2.
- **Turbo Attack**: repeats the basic attack while the attack button stays held. All three games.
- **Coin Multiplier** (2x, 5x or 10x): multiplies the coins the game awards. Marvel Ultimate Alliance and Gold Edition only.
- Press **F10** while playing to open the in-game cheat menu. What you change there applies to the current session only and is not saved. F10 needs a keyboard; on Steam Deck, set cheats on the launcher's Cheats page.

### Linux and Steam Deck
- **Fixed (issue #3):** the AppImage now correctly finds and launches the bundled runtimes for Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2, so Play is no longer blocked with "the runtime is not part of this build".
- Native Vulkan graphics in one x86_64 AppImage that holds the launcher and all three game runtimes.
- Vulkan graphics-device selection and VSync handling that follows Vulkan's present modes.
- AMD FSR 1 upscaling.
- Dynamic page streaming, shader handling improvements and a number of rendering correctness fixes.
- Performance improvements across the Vulkan renderer, and AppImage launcher and runtime fixes, including starting where FUSE is unavailable.
- Linux and Steam Deck are still **experimental**. The AppImage has been built and checked by script, but gameplay on a real Steam Deck and on desktop Linux has not been manually verified. **Broader Linux and Steam Deck hardware testing is welcome**: please report what you find.

### Install
**Windows:**
1. Unzip **MarvelUltimateAlliance-v0.2.0-windows-x64.zip** anywhere you can write to (not "Program Files") and run **marvel_ultimate_alliance_bundle.exe**.
2. Choose the game in the strip under the launcher's header.
3. On the **Play** page click **Install from package...** and pick your own Xbox 360 disc image of that game.
4. Wait for **Ready to play**, then press **PLAY**.

**Linux:** download **MarvelUltimateAlliance-v0.2.0-x86_64.AppImage**, run `chmod +x` on it, start it from a folder you can write to and install each game from its disc image the same way. `game`, `logs` and `user_data` are kept next to the AppImage.

**Steam Deck:** in Desktop Mode, download the AppImage and make it executable (right-click > Properties > Permissions > Is executable), run it once to install your games, add it with **Games > Add a Non-Steam Game to My Library**, then play it from Game Mode. Set cheats on the launcher's Cheats page.

Existing installs can update from the launcher: it offers **UPDATE NOW** when it sees this release. Game files, saves, settings and logs are kept.

**No game files are included.** You need your own disc image of each game you want to play, these exact versions:

| Game | Title ID | Media ID | `default.xex` SHA-256 |
| --- | --- | --- | --- |
| Marvel Ultimate Alliance | `415607DA` | `26848425` | `44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A` |
| Marvel Ultimate Alliance Gold Edition | `415607DA` | `5A20A6D4` | `06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7` |
| Marvel Ultimate Alliance 2 | `4156082F` | `39B3E2C9` | `2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440` |

The launcher checks the Title ID and Media ID when you pick the disc image and checks `default.xex` after extracting it. It explains if your disc image is a different version.

### What's in it
- Marvel Ultimate Alliance, Gold Edition and Marvel Ultimate Alliance 2 in one PC bundle, each statically recompiled from its own game code into a native program
- Windows (Direct3D 12) and Linux (Vulkan) builds with ReXGlue SDK 0.10.0 and a patched runtime and GPU plugin
- The TRG launcher, with a game selector and Play, Display, Graphics, Gameplay, Cheats, Achievements, Controls and About pages for each game
- Disc image installer per game with Title ID, Media ID and game version checks, and **Reinstall from package...**
- NVIDIA DLSS Super Resolution, AMD FSR 3.1 and AMD FSR 1, with Render resolution from the original 720p picture up to 4x
- Windowed or Borderless Fullscreen, switched while playing with F11; window size, monitor and VSync choices; graphics card selection
- Native aspect ratios 16:9, 16:10, 21:9 and 32:9 ultrawide, 4:3 and 5:4
- Anti-aliasing and texture filtering choices
- HD Text for Marvel Ultimate Alliance and Gold Edition: text and controller icons rebuilt at higher resolution
- Frame rate per game: 30 original, 60 by default, 90, 120, 144, 165, 240 or Unlimited for high refresh rate displays
- Infinite Health, Infinite Energy, Turbo Attack and Coin Multiplier cheats, with an F10 in-game menu
- Local saves and profiles for each game, and player names for up to four local players
- Offline achievements for all three games, listed in the launcher and in the game with F7
- Xbox controller support with no setup, and remappable keyboard play
- Skip intro, in-game frame counter (F2) and frame rate or render resolution in the window title
- In-launcher updates with SHA-256 verification; game files, saves, settings and logs are kept
- One diagnostic log per session, and a `CRASH REPORT` section after a crash on Windows
- Portable player data in a `user_data` folder next to the program

### Known limitations
- Windows is the supported platform. The Linux AppImage, which is also the Steam Deck build, is experimental.
- DLSS needs a GeForce RTX card; DLSS and FSR 3.1 are Windows only. Linux offers FSR 1.
- The F10 cheats menu needs a keyboard.
- The CRASH REPORT section is written on Windows only.
- HD Text is not available for Marvel Ultimate Alliance 2.
- Offline only: there is no Xbox Live connection or online play.
- Only the game versions listed above are supported.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

### Reporting problems
If a game does not start, crashes or misbehaves, please open an issue and attach the newest `marvel_ultimate_alliance_*.log` file from the `logs` folder next to the program (the launcher's About page opens the folder), with a line about which game you played and what you were doing. On Linux or Steam Deck, also say your device, distribution, GPU and Mesa or driver version, and whether you start the AppImage from Steam.

### Requirements
**Windows:** Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64). NVIDIA DLSS needs a GeForce RTX card.
**Linux:** 64-bit x86 with glibc 2.31 or newer (Ubuntu 20.04 or later), a Vulkan driver and an X11 or Wayland desktop.

### Third-party notice
NVIDIA DLSS Super Resolution uses NVIDIA DLSS SDK 310.9.16. `nvngx_dlss.dll` is distributed under NVIDIA's DLSS license, and `mua_dlss_bridge.dll` is this project's MIT-licensed bridge. NVIDIA, NVIDIA RTX, GeForce RTX and DLSS are trademarks and/or registered trademarks of NVIDIA Corporation. This project is not sponsored or endorsed by NVIDIA. AMD FidelityFX Super Resolution 3.1 uses AMD's signed `amd_fidelityfx_dx12.dll` under the MIT License. The licenses are in the `licenses` folder of the Windows download.

### Security
The final release artifacts were scanned with VirusTotal before publication. A scan is an extra check, not a guarantee of safety, so please also verify the SHA-256 of your download.

| File | VirusTotal |
| --- | --- |
| `MarvelUltimateAlliance-v0.2.0-windows-x64.zip` | https://www.virustotal.com/gui/file/2bcab1cee6afedcfd5ebaf366a81a798c454792742d9fae9f0c2fa4065adb8a6 |
| `MarvelUltimateAlliance-v0.2.0-x86_64.AppImage` | https://www.virustotal.com/gui/file/d3aa486c227aafa6067b9b9c2137bcbac760225bcbfaf77f1e8edc4b01ee2256 |

### Checksums
The SHA-256 of each file is also published with the release as `MarvelUltimateAlliance-v0.2.0-windows-x64.sha256` and `MarvelUltimateAlliance-v0.2.0-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `MarvelUltimateAlliance-v0.2.0-windows-x64.zip` | `2bcab1cee6afedcfd5ebaf366a81a798c454792742d9fae9f0c2fa4065adb8a6` |
| `MarvelUltimateAlliance-v0.2.0-x86_64.AppImage` | `d3aa486c227aafa6067b9b9c2137bcbac760225bcbfaf77f1e8edc4b01ee2256` |
