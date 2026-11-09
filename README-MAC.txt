SUPER HANG-ON ULTIMATE - NATIVE macOS BUILD   v0.9.32 alpha (Mac 8)
Native Port + Mega Drive Original Mode Mod
====================================================================

The same game as the Windows v0.9.32 alpha (recompiled C, Original Mode, Mega Drive options, arranged
soundtrack), for macOS 11 Big Sur or later on Apple Silicon and Intel Macs.

WHAT'S NEW IN MAC 2
-------------------
* Builds and runs on a real Mac (tested on a MacBook Air, Apple Silicon).
* Gamepads that macOS lists under System Settings > Game Controllers but whose input never reached the game
  (seen with a GameSir Nova Lite) now work: pads are also read directly as HID devices, every frame.
* build_mac.sh passes the macOS SDK to the compiler itself (Mac 1 failed with "stdio.h not found") and links IOKit.
* gamepad.log in the game folder records the pads the game sees and every button/stick change.

WHAT'S DIFFERENT ON THE MAC
---------------------------
* "Fit screen" display (default): the picture takes the shape of your screen instead of being boxed into
  16:9. On MacBook screens such as 1710x1112 (MacBook Air 15") and 1470x956 (MacBook Air 13") the game is
  about 370 arcade pixels wide in a window and about 380 in fullscreen, where macOS keeps the picture below
  the notch. The game places roadside objects and rival bikes in the extra area exactly as in 16:9, and the
  HUD moves out to the edges of whatever width you have: TOP / COURSE / STAGE / the progress bar on the
  left edge, SCORE / SPEED on the right edge, TIME and the bike in the centre. The Original Mode LAP TIME,
  SPEED and progress bar follow the edges the same way. Nothing ever sits under the notch.
  The window title shows the width in use, e.g. "[fit screen 382, sharp-bilinear, Metal, C]".
  16:9 widescreen and 4:3 original are still there (launcher, Display menu, Cmd+1/2/3).
* Fullscreen is macOS's own fullscreen (its own Space). Ctrl+Cmd+F, Cmd+Return or F11 toggle it; Esc leaves it.
* Cmd+Q quits (high scores and settings are saved first).

BUILDING THE APP
----------------
1. Install Apple's Command Line Tools once (full Xcode is not needed):   xcode-select --install
2. In Terminal, in the source folder:                                     ./build_mac.sh
   This makes build/mac/SuperHangOn.app, one app for both Apple Silicon and Intel Macs.
   If the source folder sits inside your game folder (next to shangon.zip, as on Windows), the app is also
   copied there.
3. If shangon.zip is found (next to the source folder), the build finishes with a self-test: the game runs
   headless for 1200 frames and must match the Windows/Linux builds frame for frame. You should see
   "Self-test PASSED". If it says FAILED, please send build/mac/selftest/run.log.
   ARCHS=arm64 ./build_mac.sh builds for one kind of Mac only (faster while developing).

SETUP
-----
1. Put SuperHangOn.app in a folder of its own, together with your MAME ROM set "shangon.zip"
   (Super Hang-On sitdown/upright, unprotected - MAME 'shangon'). This is the game folder: the Mega Drive ROM,
   the music folder, shangon.ini, shangon.nv (high scores) and the Original Mode saves live here too, exactly
   as next to SuperHangOn.exe on Windows. Click the FOLDER line on the launcher to open it in Finder.
2. Double-click SuperHangOn.app. If macOS asks whether the app may use files in your Documents (or Desktop /
   Downloads) folder, click Allow - that is the game folder being read.
   An app you built yourself opens normally. A copy someone else built may be stopped by Gatekeeper the first
   time: right-click the app > Open > Open. (If macOS runs a downloaded copy straight from Downloads without it
   having been moved, the game uses ~/Library/Application Support/Super Hang-On Ultimate as its game folder instead;
   "Open game folder" shows where.)
3. Your Windows files work as they are: shangon.nv, shangon_original.sav, shangon_original_md.sav and the
   music folder can be copied across. A Windows shangon.ini is read too (widescreen=1 becomes Fit screen).
NOTE: this build contains C code translated from your copy of the game's program ROM.
It still needs your ROM to run, but please keep it for personal use and don't share it publicly.

ARRANGE SOUNDTRACK (OPTIONAL)
-----------------------------
The arranged MP3s are not included; you supply them. For the launcher's "Arrange Soundtrack" music option, make a
folder called  music  (lower case) in the game folder, next to SuperHangOn.app, holding exactly these four files:

    music/Outride a Crisis.mp3
    music/Sprinter.mp3
    music/Winning Run.mp3
    music/Hard Road.mp3

The names must match exactly (spaces and capitals as shown, .mp3 ending); any other name is ignored. The line under
the Music list in the launcher says how many were found, and a missing song falls back to the arcade original.
See README.txt (ARRANGE SOUNDTRACK) for the details.

GAMEPAD (Xbox, PlayStation and other pads macOS supports)
-------
  Left stick / D-pad ..... Steer (analogue handlebar)
  Right trigger .......... Throttle (analogue)    A = full throttle
  Left trigger ........... Brake (analogue)       B or X = full brake
  RB or Y ................ Supercharger (turbo - works at high speed)
  Start (Menu) ........... Start                  Back (View) ........ Insert coin
  (The Xbox / PS button itself opens Launchpad: that is macOS, not the game.)
  Any pad listed in System Settings > Game Controllers should work (GameSir, 8BitDo...). The window title shows
  how many the game sees ("pads: 1"), and gamepad.log in the game folder records each pad macOS reports and every
  button and stick change - send that file if a pad doesn't respond.
  Pads are also read directly as HID devices, in case macOS's controller system passes nothing to the game. Their
  buttons are numbered; the defaults suit GameSir / 8BitDo / Android-style pads. If buttons come out wrong, set
  [pad] map=A,B,X,Y,RB,Back,Start with the numbers gamepad.log shows when you press each one (default
  map=1,2,4,5,8,11,12).

KEYBOARD
--------
  Left/Right or A/D ...... Steer       Up / W / Z ............ Throttle
  Down / S / X ........... Brake       Space / Shift ......... Supercharger
  5 or C ................. Coin        1 or Return ........... Start
  P or Cmd+P ............. Pause       F3 or Cmd+R ........... Back to the title screen
  Cmd+1 / Cmd+2 / Cmd+3 .. Fit screen / 16:9 / 4:3   (F9 cycles)
  Cmd+4 / Cmd+5 / Cmd+6 .. Sharp / smooth / sharp-bilinear scaling   (F8 cycles)
  Ctrl+Cmd+F, Cmd+Return or F11 ... Fullscreen       Esc ... Leave fullscreen       Cmd+Q ... Quit
  F2 (hold) .............. Arcade test/service menu   9 ...... Service coin
  Original Mode screens: arrows move, Z / Space select, Delete / X back.
  On a MacBook keyboard the F-keys need the fn key (fn+F3 ...), so the Cmd shortcuts are easier.
  Unlike Windows, Ctrl is not a throttle key: Ctrl+arrows switch Spaces and open Mission Control on a Mac.
  Development keys as on Windows: Ctrl+0..7 Original Mode race, Ctrl+8 upgraded bike, Ctrl+9 money.

SETTINGS (shangon.ini)
----------------------
  As on Windows, with these [video] differences:
  view = fit (default), 16:9 or 4:3      (replaces widescreen=; an old widescreen= line is still understood)
  fullscreen, filter, stretch as on Windows; renderer= and vrr= are Windows-only (the Mac always uses Metal,
  locked to the display: 60 Hz and 120 Hz ProMotion screens give perfectly even motion).
  Command line (Terminal): SuperHangOn.app/Contents/MacOS/SuperHangOn [-window] [-fullscreen] [-interpreter] [-play] [path/to/shangon.zip]

KNOWN LIMITS (Mac 2)
--------------------
* HID pads number their buttons and the default numbers are a guess at the common GameSir / 8BitDo layout. If a
  button does the wrong thing, see [pad] map= above.
* The launcher is the in-window settings screen (as on Windows and Linux); it appears every time unless
  [launcher] show=0. Keyboard, mouse or pad. The Display menu items are greyed out while it is up (F11 / Ctrl+Cmd+F
  still switch fullscreen).
* The universal (Apple Silicon + Intel) app has only been run on Apple Silicon so far.

CREDITS / LICENCES (see licenses folder)
  Musashi 68000 core - Karl Stenerud (MIT-style)    Z80 core - superzazu (MIT)
  Nuked-OPM YM2151, Nuked-OPN2 YM2612 - nukeykt (LGPL-2.1)    puff inflate - Mark Adler (zlib)    dr_mp3 (MIT-0)
  Hardware behaviour documented by the MAME project. Super Hang-On is (c) SEGA; not affiliated with SEGA.
