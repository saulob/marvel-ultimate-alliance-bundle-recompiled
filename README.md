# Marvel Ultimate Alliance Recompiled

Native PC recompilation of Marvel Ultimate Alliance for Xbox 360 with [ReXGlue](https://github.com/rexglue/rexglue-sdk).
First target: Marvel Ultimate Alliance Gold Edition (title ID `415607DA`, media ID `5A20A6D4`).

Status: bring-up. The launcher detects the game files, the generated recompiled code builds and the runtime boots into guest
code; the first rendered frame has not been confirmed yet.

## Game files

The game is not included. Put your own disc image in `game` and run `.\tools\extract-disc.ps1` to fill `game/extracted`. `build.ps1 -Run` passes it as `--game_data_root`; started by hand, the
program looks in `game` next to the executable unless you pass that argument.

The program mounts that folder as the console's `game:` and `D:` volume. The game opens its own `.zip` archives from it
(`z\assetsfb.zip`, `muadlc_content.zip`, `muadlc_titleupdate.zip`), so nothing is unpacked.

## Launcher

The launcher is the TekRant Gaming launcher shared with the other ports: Play, Display, Graphics, Gameplay, Achievements,
Controls and About pages. The player's choices are written to `user_data\settings.toml`, loaded after the shipped
`marvel_ultimate_alliance.toml`. F11 switches between windowed and borderless fullscreen in the launcher and in the game.
Hold Shift while starting to open the launcher when it is turned off.

While playing, F2 shows or hides the frame counter, and the window title shows the frame rate (Gameplay page). F10 and F12
are reserved and do nothing yet; F9 is unassigned.

## Build (Windows)

```powershell
.\tools\build.ps1        # sets up the SDK, builds the patched runtime, generates the recompiled code and configures CMake when needed, then builds
.\tools\build.ps1 -Run   # the same, then starts the game with game\extracted
.\tools\test.ps1         # headless unit tests
.\tools\test.ps1 -GameDir game\extracted   # also checks that folder against the supported profile and the D: mapping
.\tools\codegen.ps1      # forces a codegen run with a log in generated\codegen.log
python tools\find-missing-functions.py --apply-high   # indirect-only functions -> config\discovered-functions.toml
```

Needs Visual Studio with the C++ Clang tools and the TRG Launcher: add the `third_party/trg-launcher` submodule or
pass `-TrgLauncherDir`.

The runtime and Xenos GPU plugin in `tools\rexglue` are built from the pinned ReXGlue v0.10.0 source with the patches in
`patches\rexglue-sdk` (the first build fetches the source and takes a while). `tools\build.ps1` rebuilds them whenever
the patches or the compiler change and refuses to start with other copies next to the executable. The Debug runtime
stays the stock one.
