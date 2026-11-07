# Super Hang-On Ultimate

**Native Port + Mega Drive Original Mode Mod** – a native Windows and macOS port of the arcade game Super Hang-On,
with an optional Mega Drive Original Mode.

I have made a video explaining this project here: <https://www.youtube.com/watch?v=mqXxxJ72bdk>

One source tree that builds both the Windows `SuperHangOn.exe` and the macOS `SuperHangOn.app`
(universal: Apple Silicon + Intel). Version 0.9.26 alpha.

**You must supply your own game data.** No ROMs are included.

* **Arcade ROM set (required):** the MAME `shangon.zip` (unprotected Super Hang-On set), next to the program. `setup.py` also
  needs it once, to generate the game code (see "Not in Git").
* **Mega Drive ROM (optional):** only for Original Mode. The game loads it when it starts, runs it headless to take the
  pictures and course data it needs, and keeps them in memory only. Nothing from it is stored, and `setup.py` does not need it.

Super Hang-On is © SEGA; Super Hang-On Ultimate is a fan project and is not affiliated with SEGA.

## What is where

| Path | What |
|---|---|
| `source/src/` | the game. Shared core (`sho_*.c`, `rc_runtime.c`) plus one frontend per platform |
| `source/src/win32_*.c` | Windows frontend (Direct3D 11, launcher) |
| `source/src/mac_*.m`, `ini_file.c`, `rc_ctx.c` | macOS frontend (Metal, launcher, own fiber stack switch) |
| `source/src/third_party/` | Musashi (68000), Z80, Nuked OPM/OPN2, puff, dr_mp3 – licences in `licenses/` |
| `source/src/gen/` | **generated** by `setup.py` from your ROM – see "Not in Git" below |
| `source/recomp/` | the 68000 → C recompiler, `patches/` (our hand edits), `coverage.bin` (which code the game runs) |
| `source/res/` | icons, Windows resources |
| `source/tests/selftest_1200.txt` | state hashes the Mac build's self-test must reproduce |
| `LICENSE`, `NOTICE.md`, `licenses/` | the project's MIT licence, credits, and the licences of bundled third-party code |
| `installer/windows/`, `installer/mac/` | build the installers, which create the game from the user's ROM (see their READMEs) |
| `README.txt`, `README-MAC.txt` | the player manuals (Windows / Mac differences) |
| `source/README-DEV.md` | developer notes for the whole project, including the Mac build |

The two builds share all game logic. Only the frontends differ.

## Building

Run the commands from the repository root.

### macOS (run on a Mac)

```sh
xcode-select --install        # once; Command Line Tools only, full Xcode is not needed
./build.sh mac                # or: cd source && ./build_mac.sh
```

Result: `source/build/mac/SuperHangOn.app`. `ARCHS=arm64 ./build.sh mac` builds for one architecture only
(faster). If `shangon.zip` sits in the repository root, the build ends with a 1200-frame self-test and
should print `Self-test PASSED`.

### Windows – on Windows

1. Download **llvm-mingw** (<https://github.com/mstorsjo/llvm-mingw/releases>, the `x86_64` UCRT zip) and unzip it, e.g. to `C:\llvm-mingw`.
2. In PowerShell:

```powershell
.\build.ps1 -Toolchain C:\llvm-mingw
```

Result: `source\build\win\SuperHangOn.exe`. (Setting the `LLVM_MINGW` environment variable once lets you leave out `-Toolchain`.)
If PowerShell refuses to run the script: `Set-ExecutionPolicy -Scope Process Bypass`.

### Windows – cross-compiled from a Mac or Linux

```sh
source/toolchain/make_sysroot.sh     # once: builds MinGW headers/import libs (needs git, clang, llvm-dlltool)
./build.sh win
```

Result: `source/build/win/SuperHangOn.exe`.

### Quick headless test (any platform)

`source/build_linux_test.sh` builds `shotest`, a headless runner that writes screenshots and audio – useful for checking changes to the shared core.

## Installers

`installer/windows/` builds `SuperHangOnUltimate-Setup-<version>.exe`: it asks for the arcade ROM, runs `setup.py` and the Windows build
on the user's PC, and installs the result (details in `installer/windows/README.md`). `installer/mac/` builds `SuperHangOnUltimate-Mac-Installer-<version>.zip` (`python3 installer/mac/build-installer.py`): an app that does the same on a Mac, using Apple's Command Line Tools as the compiler.

## Arrange soundtrack (optional)

The launcher's *Arrange Soundtrack* option plays arranged MP3s for the four music-select songs. They are not included and
`music/` is git-ignored. Put them in a `music` folder in the game folder (next to the exe / app), named exactly
`Outride a Crisis.mp3`, `Sprinter.mp3`, `Winning Run.mp3` and `Hard Road.mp3`. Anything missing falls back to the arcade
music; the launcher shows how many tracks it found. Details are in `README.txt`.

## Running

Put `SuperHangOn.exe` / `SuperHangOn.app` in a folder together with `shangon.zip`; that folder is the game folder
(settings, high scores and saves live there). See `README.txt` and `README-MAC.txt` for controls and options.

## Not in Git (on purpose)

`source/src/gen/` and `source/recomp/overrides/` are C code translated from the arcade game's program ROM.
They are kept out of the repository (see `.gitignore`) so that a public repo does not carry Sega's code.
Instead, **`setup.py` makes them on your own machine from your own `shangon.zip`**:

```sh
python3 setup.py                       # looks for shangon.zip in this folder or the one above
python3 setup.py --rom path/to/shangon.zip
```

It needs Python 3 and a C compiler (Mac: `xcode-select --install`; Windows: the clang from llvm-mingw; Linux: clang or gcc).
It unpacks the ROM, builds the program images (`source/recomp/build_images.py`), disassembles them, records which code the
game really runs (a scripted coverage pass), translates it with `recomp/recomp.py`, rebuilds the hand-written overrides from
`recomp/patches/*.patch` (only our edits, applied to the freshly translated functions) and finally plays 1200 frames and
compares them with `source/tests/selftest_1200.txt`. It prints `SUCCESS` when the result behaves exactly like the reference build.
Run it once after cloning, then build as described above.

Code the coverage pass misses is not lost: the game falls back to the interpreter for it automatically and stays bit-identical.

Never commit ROMs, `shangon.ini`, saves (`*.sav`, `*.nv`), `music/`, `gamepad.log` or release zips – all are in `.gitignore`.

## Licence

The project's own source code is under the **MIT licence** (`LICENSE`, © 2026 chaotix359). Bundled third-party code keeps
its own licences – see `NOTICE.md` and `licenses/`.

The licence covers source code only. It does not cover Super Hang-On (© SEGA), the code `setup.py` generates from your ROM,
or programs built from it: a compiled `SuperHangOn.exe` / `.app` contains that generated code, so build it for yourself and
**do not upload builds** (GitHub Releases included). Share the source; everyone builds their own with their own ROM.
