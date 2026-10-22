# Marvel Ultimate Alliance Recompiled

Native PC recompilation of Marvel Ultimate Alliance for Xbox 360 with [ReXGlue](https://github.com/rexglue/rexglue-sdk).
Targets: Marvel Ultimate Alliance Gold Edition (title ID `415607DA`, media ID `5A20A6D4`), Marvel Ultimate Alliance
(title ID `415607DA`, media ID `26848425`) and Marvel Ultimate Alliance 2 (title ID `4156082F`, media ID `39B3E2C9`).

Each game is recompiled from its own `default.xex` into its own module (`generated\mua1`, `generated\mua1-gold`,
`generated\mua2`) and each module is its own program: `marvel_ultimate_alliance_mua1.exe`,
`marvel_ultimate_alliance_mua1_gold.exe` and `marvel_ultimate_alliance_mua2.exe`.
`marvel_ultimate_alliance.exe` is the launcher: it holds no game module, and PLAY starts the selected game's program.
Each game keeps its saves, profiles, achievement progress and own settings in its folder under `user_data`: `mua1`,
`mua1_gold` and `mua2`. Earlier layouts (`user_data\games`, and Gold Edition's title folder directly
in `user_data`) are moved there at the next start unless the new folder already exists.

Status: bring-up. The launcher detects the game files, the generated recompiled code builds and the runtime boots into guest
code; the first rendered frame has not been confirmed yet.

## Game files

The games are not included. Each game has its own folder under `game`: `mua1`, `mua1-gold` and `mua2`. The Play page's
"Install from package..." (or "Reinstall from package...") takes your own Xbox 360 disc image, checks its title ID and
media ID against the selected game, extracts it next to the folder (`<game>.staging`), verifies it and only then swaps it
in, keeping the previous install until the new one is in place. Gold Edition is also accepted in `game/extracted`, which
`.\tools\extract-disc.ps1` fills; `game/mua1-gold` is preferred when both are valid. `--mua_games_root` names the folder
holding the game folders (default: `game` next to the program; `build.ps1 -Run` passes the repository's `game`), and
`--game_data_root` names one game's folder, which the game its files identify uses before the other folders.

The program mounts that folder as the console's `game:` and `D:` volume. Marvel Ultimate Alliance and Gold Edition open
their own `.zip` archives from it (`z\assetsfb.zip`, `muadlc_content.zip`, `muadlc_titleupdate.zip`), so nothing is
unpacked; Marvel Ultimate Alliance 2 reads its `packages\generated\*.pak` files in place.

## Launcher

The launcher is the TekRant Gaming launcher shared with the other ports. The selector under the header chooses the game;
the pages beside it (Play, Display, Graphics, Gameplay, Achievements, Controls and About) belong to the selected game, and
PLAY starts only a game that is installed and supported. The player's choices are written to `user_data\settings.toml`,
loaded after the shipped `marvel_ultimate_alliance.toml`; settings that belong to one game, such as the frame rate, go to
`user_data\<game>\game_settings.toml` and fall back to `settings.toml`. F11 switches between windowed and borderless fullscreen in
the launcher and in the game. Hold Shift while starting to open the launcher when it is turned off.

While playing, F2 shows or hides the frame counter, and the window title shows the frame rate (Gameplay page). F10 and F12
are reserved and do nothing yet; F9 is unassigned.

## Build (Windows)

```powershell
.\tools\build.ps1        # sets up the SDK, builds the patched runtime, generates the recompiled code and configures CMake when needed, then builds
.\tools\build.ps1 -Run   # the same, then starts the game with game\extracted
.\tools\test.ps1         # headless unit tests
.\tools\test.ps1 -GameDir game\extracted   # also checks that folder against the supported profile and the D: mapping
.\tools\codegen.ps1      # forces a codegen run for every game, with logs in generated\codegen-<game>.log
python tools\find-missing-functions.py --game mua1 --apply-high   # indirect-only functions -> config\mua1\discovered-functions.toml (also mua1-gold, mua2)
```

Needs Visual Studio with the C++ Clang tools and the TRG Launcher: add the `third_party/trg-launcher` submodule or
pass `-TrgLauncherDir`.

The runtime and Xenos GPU plugin in `tools\rexglue` are built from the pinned ReXGlue v0.10.0 source with the patches in
`patches\rexglue-sdk` (the first build fetches the source and takes a while). `tools\build.ps1` rebuilds them whenever
the patches or the compiler change and refuses to start with other copies next to the executable. The Debug runtime
stays the stock one.
