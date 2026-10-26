## Marvel Ultimate Alliance PC Port v0.1.0

Play Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel Ultimate Alliance 2 for Xbox 360 natively on Windows, from one launcher with modern PC options. This is the first release.

### Install
1. Unzip **MarvelUltimateAlliance-v0.1.0-windows-x64.zip** anywhere you can write to and run **marvel_ultimate_alliance_bundle.exe**.
2. Choose the game in the strip under the launcher's header.
3. On the **Play** page click **Install from package...** and pick your own Xbox 360 disc image of that game.
4. Wait for **Ready to play**, then press **PLAY**.

**No game files are included.** You need your own disc image of each game you want to play, these exact versions:

| Game | Title ID | Media ID | `default.xex` SHA-256 |
| --- | --- | --- | --- |
| Marvel Ultimate Alliance | `415607DA` | `26848425` | `44E5EEC135073224F2ABE09DEAEF765A6D130C81AA575D5C376338F470BEF92A` |
| Marvel Ultimate Alliance Gold Edition | `415607DA` | `5A20A6D4` | `06D1D7811E1668A5C16AFEF5102CA59B00684DC1EA6184849418F59F022B86B7` |
| Marvel Ultimate Alliance 2 | `4156082F` | `39B3E2C9` | `2613DC17AF39CEFACB7226855F62281D83B6E991C9D467D9E8EBB0A681AB0440` |

The launcher checks the Title ID and Media ID when you pick the disc image and checks `default.xex` after extracting it. It explains if your disc image is a different version.

### Linux AppImage (experimental)
Download **MarvelUltimateAlliance-v0.1.0-x86_64.AppImage**, run `chmod +x` on it and start it from a folder you can write to. It is a native Linux build using Vulkan, with the launcher and the three games in one x86_64 AppImage; the launcher and the **Install from package...** flow are the same as on Windows, and `game`, `logs` and `user_data` are kept next to the AppImage. It needs glibc 2.31 or newer (Ubuntu 20.04 generation or later) and a Vulkan driver, and unpacks itself to run where FUSE is unavailable (see `docs/linux.md`). It has been built and checked by script, with little testing on real Linux hardware yet, so please report what you find.

### What's in it
- Marvel Ultimate Alliance, Gold Edition and Marvel Ultimate Alliance 2 in one PC bundle, each statically recompiled from its own game code into a native program
- Native Windows build with ReXGlue SDK 0.10.0 and a patched Release runtime and Direct3D 12 GPU plugin
- The TRG launcher, with a game selector and Play, Display, Graphics, Gameplay, Achievements, Controls and About pages for each game
- Disc image installer per game with Title ID, Media ID and game version checks, and **Reinstall from package...**
- Windowed or Borderless Fullscreen, switched while playing with F11; window size, monitor and VSync choices
- Native aspect ratios 16:9, 16:10, 21:9 and 32:9 ultrawide, 4:3 and 5:4
- Render resolution from the original 720p picture up to 4x
- AMD FSR 1 upscaling on Windows, with Native, Quality, Balanced and Performance modes
- HD Text for Marvel Ultimate Alliance and Gold Edition: text and controller icons rebuilt at higher resolution
- Frame rate per game: 30 original, 60 by default, 90, 120, 144, 165, 240 or Unlimited for high refresh rate displays
- Performance work in the runtime, GPU plugin and audio, aimed at high frame rates and render resolutions
- Graphics card selection, anti-aliasing and texture filtering choices
- Local saves and profiles for each game, and player names for up to four local players
- Offline achievements for all three games, listed in the launcher and in the game with F7
- Xbox controller support with no setup, and remappable keyboard play
- Skip intro, in-game frame counter (F2) and frame rate or render resolution in the window title
- In-place launcher updates with SHA-256 verification; game files, saves, settings and logs are kept
- A small, self-contained update helper built with a static runtime that uses only Windows system libraries, with its debug symbols kept outside the release ZIP
- One diagnostic log per session, and a `CRASH REPORT` section after a crash on Windows
- Portable player data in a `user_data` folder next to the program

### Known limitations
- Windows is the supported platform. The Linux AppImage is experimental; Upscaling and the **Graphics card** selector are Windows-only, and the CRASH REPORT section is written on Windows only.
- HD Text is not available for Marvel Ultimate Alliance 2.
- Offline only: there is no Xbox Live connection or online play.
- Only the game versions listed above are supported.
- The program is not code-signed, so Windows SmartScreen may show a warning the first time.

### Reporting problems
If a game does not start, crashes or misbehaves, please open an issue and attach the newest `marvel_ultimate_alliance_*.log` file from the `logs` folder next to the program, with a line about which game you played and what you were doing.

### Requirements
Windows 10 or 11 (64-bit), a DirectX 12 graphics card, and the latest Microsoft Visual C++ 2015-2022 Redistributable (x64).

### Checksums
The SHA-256 of each file is published with the release as `MarvelUltimateAlliance-v0.1.0-windows-x64.sha256` and `MarvelUltimateAlliance-v0.1.0-x86_64.sha256`, so you can verify your download.

| File | SHA-256 |
| --- | --- |
| `MarvelUltimateAlliance-v0.1.0-windows-x64.zip` | `43268e7cb214e6912b6596baeadaba5a4d52fdebfdafb8d1ead96ca8f7ab9289` |
| `MarvelUltimateAlliance-v0.1.0-x86_64.AppImage` | `7ded3e2d339a8d6e0a287f17d237cf0dfc6cb44cac5cee8242ee6059aefdfc1d` |
