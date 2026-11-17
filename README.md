<img width="1672" height="941" alt="Neon Geist_ Recompiled PC Port" src="https://github.com/user-attachments/assets/c5321b54-3f21-4d28-ae8d-fe02e7c839ec" />



GEIST RECOMPILED  v1.0.7
An unofficial, fan-made native PC port of Geist (GameCube, 2005)
=====================================================================

NO GAME IS INCLUDED. You need your own legally dumped Geist disc image.
Geist was developed by n-Space and published by Nintendo. Geist is a
trademark of Nintendo. This project is not affiliated with or endorsed by
Nintendo or n-Space.

---------------------------------------------------------------------
WHAT YOU NEED
---------------------------------------------------------------------
  * Windows 10 or 11, 64-bit
  * A graphics card with OpenGL 3.3 (any GeForce GTX/RTX, Radeon HD 5000
    or newer, Intel HD 4000 or newer)
  * About 2 GB of free disk space for the build
  * YOUR OWN dump of Geist (USA), disc ID GITE01, revision 0, as a plain
    .iso / .gcm, or a .ciso.
    .rvz / .gcz / NKit images: convert them to ISO first in Dolphin
    (right-click the game > Convert File... > ISO).

---------------------------------------------------------------------
HOW TO PLAY
---------------------------------------------------------------------
  1. Extract this folder anywhere you like, for example C:\Games\GeistRecompiled
  2. Run GeistLauncher.exe
  3. Click PLAY and select your Geist disc image
     (or drag the .iso onto the launcher window).
  4. The first time, the launcher builds Geist.exe from YOUR disc. It
     checks the disc, recompiles the game's program and compiles it. This
     takes a few minutes; it is faster on more CPU cores.
  5. After that, PLAY starts the game straight away.

  Your disc image is only read, never changed or copied.

---------------------------------------------------------------------
SETTINGS (in the launcher)
---------------------------------------------------------------------
  Game disc .......... choose a different disc image
  Geist.exe .......... rebuild the game (after a launcher update)
  Controls ........... edit controls.txt: keyboard, mouse, Xbox,
                       PlayStation (DualSense / DualShock 4), Switch Pro /
                       Joy-Con and GameCube adapters. Every button is
                       listed at the top of the file.
  Game folder ........ where Geist.exe is built
  Build log .......... details if a build fails

  In game: click to capture the mouse, F2 releases it, Esc is Start/pause,
  close with the window's X or Alt+F4.

---------------------------------------------------------------------
PC SETTINGS (in game: F1)
---------------------------------------------------------------------
  Press F1 while playing (on a controller: the Home / Guide / PS button,
  or click both sticks in together). The game keeps running behind it;
  pause with Start first if you need to.

  Display mode ........ Windowed, Borderless fullscreen, Exclusive fullscreen
  Monitor ............. which screen to use
  Fullscreen resolution any mode your monitor offers (exclusive mode)
  Window size ......... 640x480 up to 2880x2160, or drag the window edge
  Internal resolution . what the game is rendered at: Auto picks enough
                        for your screen, or choose 1x (original 640x480)
                        up to 8x (5120x3840)
  Aspect ratio ........ 4:3 original: the picture keeps its shape with
                        black bars at the sides, never stretched (default).
                        Stretch to fill is there if you want it.
  Scaling filter ...... smooth or sharp
  VSync, Show FPS
  Quit game

  Settings are saved in game\settings.ini.

---------------------------------------------------------------------
FOLDERS
---------------------------------------------------------------------
  GeistLauncher.exe  the launcher
  sdk\               recompiler, runtime library and Geist tables
                     (contains no code or data from the game)
  toolchain\         compiler used to build the game on your PC
                     (llvm-mingw, includes Python 3.12)
  assets\            launcher art and fonts. background.png + skin.ini
                     are the launcher's look: skin.ini says where the
                     buttons painted into the image are. Delete skin.ini
                     to fall back to the built-in animated look.
  licenses\          licences of the included open-source components
  work\   (created)  recompiled program, generated during the build
  game\   (created)  your Geist.exe

  work\ and game\ are made from YOUR disc. Do not share them. Share only
  the original download.

---------------------------------------------------------------------
CREDITS
---------------------------------------------------------------------
  Port, launcher and online multiplayer ... ThyHeroOfTime
  gcn-recomp ............................... Ethan Nicholas (MIT)
  SDL2 (zlib) - LLVM / llvm-mingw (Apache-2.0 with LLVM exception) -
  Python (PSF) - stb (public domain / MIT) - Orbitron and Rajdhani fonts (OFL)
