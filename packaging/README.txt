Marvel Ultimate Alliance - PC port
Version @VERSION@

Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel
Ultimate Alliance 2 for Xbox 360, statically recompiled to run natively on
Windows with the ReXGlue SDK. This is a native recompilation, not an
emulator.

Source code and issue tracker:
https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled


NO GAME FILES ARE INCLUDED
--------------------------
You need your own Xbox 360 disc image (.iso) of each game you want to play.
This port only runs these exact releases:

  Marvel Ultimate Alliance               Title ID 415607DA, Media ID 26848425
  Marvel Ultimate Alliance Gold Edition  Title ID 415607DA, Media ID 5A20A6D4
  Marvel Ultimate Alliance 2             Title ID 4156082F, Media ID 39B3E2C9

The launcher checks the disc image against the selected game and refuses any
other version.


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- A DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64). Most PCs
  have it. If Windows reports a missing VCRUNTIME140.dll or MSVCP140.dll,
  install it from https://aka.ms/vs/17/release/vc_redist.x64.exe
- About 6 GB of free disk space next to the program for each Marvel Ultimate
  Alliance game you install, and about 7 GB for Marvel Ultimate Alliance 2


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files"). Keep all
   of its files together: the launcher starts the game programs beside it.
2. Run marvel_ultimate_alliance_bundle.exe. The launcher opens.
3. Choose the game in the strip under the header.
4. On the Play page, click "Install from package..." and select your disc
   image of that game.
5. Wait while the launcher checks the image and copies the game files into
   the "game" folder next to the program.
6. When the Play page says "Ready to play", press PLAY.

To repair a game's files, use "Reinstall from package..." on the Play page.
Your settings and saves are kept.


UPDATES
-------
The launcher checks the project's GitHub releases once per start. When a newer
release exists, an update banner appears in the header; nothing is installed
until you choose UPDATE NOW and then RESTART TO UPDATE. The update replaces
the whole program bundle together: the launcher, the three game programs, the
update helper, the runtime and the launcher assets. The "game" folder,
"user_data", saves, settings and logs are kept.


WINDOWS SMARTSCREEN
-------------------
The first time you run marvel_ultimate_alliance_bundle.exe, Windows
SmartScreen may show a warning, because the program is not digitally signed. If
you downloaded the release from the project's GitHub releases page, choose
"More info", then "Run anyway".


PLAYING
-------
- Xbox controllers work without setup. The keyboard works too; the Controls
  page lists the keys and lets you change them.
- F11 switches between Windowed and Borderless Fullscreen.
- F7 opens the achievements list. Achievements are kept on this PC only.
- F2 shows or hides a frame counter in the corner of the window.
- Each game saves and loads as on the console. There is no Xbox Live
  connection and no online play.
- To open the launcher again after turning it off, hold Shift while starting.

Launcher options, per game:
- Display: Windowed or Borderless Fullscreen, window size, monitor, aspect
  ratio (16:9, 16:10, 21:9, 32:9, 4:3, 5:4) and VSync.
- Graphics: graphics card, render resolution (up to 4x), AMD FSR 1
  upscaling (and, in all three games, AMD FSR 3.1 on any graphics card and
  NVIDIA DLSS on GeForce RTX), anti-aliasing and texture filtering. HD
  Text (sharper text and controller icons) is available for Marvel
  Ultimate Alliance and Gold Edition.
- Gameplay: skip intro, frame rate (30 original, 60 default, up to 240 or
  Unlimited), what the window title and the corner counter show, and player
  names.
- Achievements: unlocked achievements and gamerscore.


WHERE THINGS ARE
----------------
All of these are next to marvel_ultimate_alliance_bundle.exe:
- Game files:     the "game" folder
                  (one folder per game: mua1, mua1-gold and mua2)
- Logs:           the "logs" folder
- Settings, profiles, saves and achievements:
                  the "user_data" folder
                  (one folder per game: mua1, mua1_gold and mua2)

Copy the whole program folder to another PC to take your profiles, saves and
settings with it. To update by hand, extract the new version over the old
folder, or move "user_data" (and "game") into the new one: deleting the old
folder deletes "user_data" with it.


REPORTING A PROBLEM
-------------------
If a game does not start, crashes, runs slowly, or has a graphics, audio,
save, controller or launcher problem, please attach the newest
marvel_ultimate_alliance_*.log file from the "logs" folder (the launcher's
About page opens it) to a report at
https://github.com/saulob/marvel-ultimate-alliance-bundle-recompiled/issues
and say which game you played and what you were doing. The log already holds
your Windows version, CPU, graphics card, settings and the frame rate. For an
audio glitch, press F9 right after it happens, then attach the log. After a
crash, the same log ends with a CRASH REPORT section.


LICENSES
--------
Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder: ReXGlue-LICENSE.txt and
THIRD-PARTY-LICENSES.txt.

The launcher is TRG Launcher 1.1.0 (https://github.com/TekRantGaming/trg-launcher),
licensed under the MIT License; its notice is in THIRD-PARTY-LICENSES.txt.

Game files are not included and are not covered by this project. This port is
not affiliated with or endorsed by the makers or publishers of Marvel Ultimate
Alliance or Marvel Ultimate Alliance 2.
