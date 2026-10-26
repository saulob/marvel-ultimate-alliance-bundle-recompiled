Marvel Ultimate Alliance - PC port
Version @VERSION@

Marvel Ultimate Alliance, Marvel Ultimate Alliance Gold Edition and Marvel
Ultimate Alliance 2 for Xbox 360, statically recompiled to run natively on
Windows with the ReXGlue SDK.

Source code and issue tracker:
https://github.com/saulob/marvel-ultimate-alliance-recompiled


NO GAME FILES ARE INCLUDED
--------------------------
You need your own copy of each game you want to play: its Xbox 360 disc
image. The launcher checks the image against the selected game and refuses a
different one.


REQUIREMENTS
------------
- Windows 10 or 11, 64-bit
- A DirectX 12 capable graphics card
- The latest Microsoft Visual C++ Redistributable 2015-2022 (x64). Most PCs
  have it. If Windows reports a missing VCRUNTIME140.dll or MSVCP140.dll,
  install it from https://aka.ms/vs/17/release/vc_redist.x64.exe
- Disk space next to the program for the game files of each game you install


INSTALL
-------
1. Extract the ZIP anywhere you can write to (not "Program Files"). Keep all
   of its files together: the launcher starts the game programs beside it.
2. Run marvel_ultimate_alliance.exe. The launcher opens.
3. Choose the game in the strip under the header.
4. On the Play page, click "Install from package..." and select your disc
   image of that game.
5. When the Play page says the game is ready, press PLAY.


UPDATES
-------
The launcher checks the project's GitHub releases once per start. When a newer
release exists, an update banner appears in the header; nothing is installed
until you choose UPDATE NOW and then RESTART TO UPDATE. The update replaces the
whole program bundle together: the launcher, the three game programs, the
update helper, the runtime and the launcher assets. Game files, user_data,
saves, settings, logs and caches are kept.


WHERE THINGS ARE
----------------
- Game files:     the "game" folder next to marvel_ultimate_alliance.exe
- Logs:           the "logs" folder next to marvel_ultimate_alliance.exe
- Settings, profiles, saves and achievements:
                  the "user_data" folder next to marvel_ultimate_alliance.exe
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
https://github.com/saulob/marvel-ultimate-alliance-recompiled/issues
and say which game you played and what you were doing. The log already holds
your Windows version, CPU, graphics card, settings and the frame rate. For an
audio glitch, press F9 right after it happens, then attach the log. After a
crash, the same log ends with a CRASH REPORT section.


WINDOWS SMARTSCREEN
-------------------
The first time you run marvel_ultimate_alliance.exe, Windows SmartScreen may
show a warning, because the program is not digitally signed. If you downloaded
the release from the project's GitHub releases page, choose "More info", then
"Run anyway".


LICENSES
--------
Third-party components keep their own licenses. Their license files and
notices are in the "licenses" folder: ReXGlue-LICENSE.txt and
THIRD-PARTY-LICENSES.txt.

Game files are not included and are not covered by this project. This port is
not affiliated with or endorsed by the makers or publishers of Marvel Ultimate
Alliance.
