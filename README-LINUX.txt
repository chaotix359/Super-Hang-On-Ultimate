SUPER HANG-ON ULTIMATE - NATIVE LINUX BUILD   v0.9.32 alpha (Linux 7)
Native Port + Mega Drive Original Mode Mod
====================================================================

The same game as the Windows and Mac v0.9.32 alpha (recompiled C, Original Mode, Mega Drive options,
arranged soundtrack), for 64-bit Linux desktops with SDL2 (tested on x86-64; ARM64 should build the same
way but has not been tried). README.txt is the full manual;
this file lists what is different on Linux.

WHAT'S DIFFERENT ON LINUX
-------------------------
* The settings screen (launcher) is the same as on Windows and the Mac (see README.txt): drawn in the game window
  over the Super Hang-On Ultimate picture, in the arcade's own text font (read from your shangon.zip; until the ROM
  is found, look-alike letters stand in). After PLAY a loading bar shows what the game is getting ready (the
  arranged songs take a moment to decode; Original Mode's screens and courses are made from the Mega Drive ROM,
  usually while you are still on the settings). Display offers Fit window, 16:9 and 4:3.
* "Fit window" display (default): the picture takes the shape of your window or screen (as "Fit screen" on
  the Mac) - 16:9 on a 16:9 monitor, a little narrower on 16:10 laptops, up to the widest the game draws on
  ultrawide screens, with the HUD at its edges. 16:9 widescreen and 4:3 original are still there.
* Fullscreen is borderless fullscreen on the desktop's own resolution. F11 or Alt+Enter toggle it; Esc
  leaves it. Close the window to quit (high scores and settings are saved first).
* Gamepads: anything SDL knows - Xbox, PlayStation, Switch Pro, 8BitDo, GameSir, Steam Deck controls and many
  more - with the Xbox layout used on Windows. For a pad SDL doesn't know, put the community
  "gamecontrollerdb.txt" (github.com/mdqinc/SDL_GameControllerDB) in the game folder, or set the button
  numbers with [pad] map= in shangon.ini (A B X Y RB Back Start, e.g. map=1,2,3,4,6,7,8).
* Vsync: the game waits for the display and locks to 60, 120 or 240 Hz screens for even motion. If your
  desktop ignores vsync, the game notices and paces itself by the clock instead (vsync=0 under [video]
  forces that).

KEYS
----
  Steer               Left / Right  (or A / D)
  Accelerate          Up, Z, W or Ctrl
  Brake               Down, X or S
  Turbo               Space or Shift
  Coin / Start        5 or C / 1 or Enter
  Pause               P  (Start pauses during an Original Mode race, with the PAUSE menu)
  Original Mode menus arrows move, Z or Space select, Backspace or X back
  Back to title       F3
  Display / filter    F9 (fit window, 16:9, 4:3) / F8 (sharp, smooth, sharp-bilinear)
  Fullscreen          F11 or Alt+Enter; Esc leaves it
  Test / service      F2 / 9
  Development keys    Ctrl+1..7 always start that Original Mode race, Ctrl+0 off; on the Original Mode
                      command screen Ctrl+8 = fully upgraded bike, Ctrl+9 = maximum money

INSTALLING (THE EASY WAY)
-------------------------
From the source folder, with your ROM(s):

    installer/linux/install.sh /path/to/shangon.zip  [/path/to/your Mega Drive ROM]

It checks for the build tools (and tells you the one command to install them if any are missing), makes the
game code from your shangon.zip, builds the game, runs the 1200-frame self-test, and installs it in
~/.local/share/super-hang-on-ultimate with an entry in your applications menu and a "superhangon" command.
No root needed apart from installing the build tools. To remove it, delete that folder,
~/.local/share/applications/super-hang-on-ultimate.desktop and ~/.local/bin/superhangon.

BUILDING BY HAND
----------------
1. Install a compiler, Python 3 and the SDL2 development files, e.g.
       Debian / Ubuntu / Mint:   sudo apt install build-essential python3 libsdl2-dev
       Fedora:                   sudo dnf install gcc python3 SDL2-devel
       Arch / Manjaro:           sudo pacman -S base-devel python sdl2
2. Once, in the source folder: python3 setup.py --rom /path/to/shangon.zip  (makes the game code from your ROM;
   it ends with "SUCCESS").
3. ./build.sh linux   (or source/build_linux.sh). Result: source/build/linux/SuperHangOn. If shangon.zip is
   next to the source folder, the build ends with the self-test ("Self-test PASSED") and copies the program
   next to it.

SETUP
-----
Put SuperHangOn in a folder together with your MAME ROM set "shangon.zip" (Super Hang-On sitdown/upright,
unprotected - MAME 'shangon'). That is the game folder: the Mega Drive ROM, the music folder, shangon.ini,
shangon.nv (high scores) and the Original Mode saves live there, exactly as next to SuperHangOn.exe on Windows.
If the program sits in a folder you can't write to (e.g. /opt), ~/.local/share/super-hang-on-ultimate is used.
Your Windows or Mac files work as they are: shangon.nv, shangon_original.sav, shangon_original_md.sav,
shangon.ini and the music folder can be copied across.

The program needs the SDL2 library at run time; every desktop distribution has it installed or one command
away (libsdl2-2.0-0 on Debian/Ubuntu, SDL2 on Fedora/Arch).

NOTE: this build contains C code translated from your copy of the game's program ROM.
It still needs your ROM to run, but please keep it for personal use and don't share it publicly.
