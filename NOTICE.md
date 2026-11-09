# Notices – Super Hang-On Ultimate

## What the MIT licence covers

The `LICENSE` file (MIT, © 2026 chaotix359) covers this project's **own source code**: the Windows, macOS and Linux frontends (including the launcher's stand-in font),
the game engine (`source/src/sho_*.c`, `rc_runtime.c`, `rc_ctx.c`, `ini_file.c`), the recompiler and tools in `source/recomp/`,
`setup.py`, the build scripts, and the documentation.

It does **not** cover:

* **Super Hang-On itself.** The game, its program, graphics, sound and music are © SEGA. This project is not affiliated
  with or endorsed by SEGA, and contains none of SEGA's ROMs or code. `setup.py` makes the translated program code on your
  own computer from your own ROM; that generated code (`source/src/gen/`, `source/recomp/overrides/`) is derived from SEGA's
  program, is not part of this repository, and is not licensed by this project.
* **Programs you build.** A compiled `SuperHangOn.exe` / `SuperHangOn.app` / Linux `SuperHangOn` contains that translated program code. It is for
  your own use with your own ROMs; do not redistribute it.
* **The launcher picture** (`source/res/launcher_bg.png`, made from the game's graphics and logo) shows SEGA's artwork.
  It is built into the programs for your own use; keep it out of anything you publish.
* **Third-party code** listed below, which keeps its own licence.

## Credits

* Arranged-soundtrack support (`source/src/sho_music.c`, MP3 playback) – **ts743**
* Hardware behaviour as documented by the MAME project (<https://www.mamedev.org>)

## Third-party code included in the source

Full licence texts are in `licenses/` (and, for some, next to the code).

| Component | Location | Licence | Author |
|---|---|---|---|
| Musashi 68000 core (with its SoftFloat 2b, repackaged for MAME) | `source/src/third_party/musashi/` | MIT-style – `licenses/Musashi.txt`; SoftFloat terms in `softfloat/README.txt` | Karl Stenerud; John R. Hauser |
| Z80 core | `source/src/third_party/z80/` | MIT – `licenses/z80-MIT.txt` | superzazu |
| Nuked-OPM (YM2151) | `source/src/third_party/nukedopm/` | LGPL-2.1 – `licenses/Nuked-OPM-LGPL-2.1.txt` | nukeykt |
| Nuked-OPN2 (YM2612) | `source/src/third_party/nukedopn2/` | LGPL-2.1 – `licenses/Nuked-OPN2-LGPL-2.1.txt` | nukeykt |
| puff (inflate) | `source/src/third_party/puff/` | zlib – `licenses/puff-zlib.txt` | Mark Adler |
| dr_mp3 | `source/src/third_party/dr_mp3/` | MIT-0 – `licenses/dr_mp3-MIT0.txt` | David Reid |
| SDL2 (Linux build only; the system's shared library, linked at build time and not stored here) | – | zlib – `licenses/SDL2-zlib.txt` | Sam Lantinga and the SDL contributors |
| MinGW-w64 headers and import libraries (Windows build only; fetched by `source/toolchain/make_sysroot.sh`, not stored here) | – | BSD-style – `licenses/MinGW-w64-headers.txt` | MinGW-w64 project |

### Note on the LGPL components

Nuked-OPM and Nuked-OPN2 are compiled into the game. Their complete source is in this repository, and you may modify them
and rebuild the game with the supplied build scripts, which is what the LGPL asks for.

## Installer

The Windows installer (`installer/windows/`) carries, besides this project's source, **LLVM-MinGW** (LLVM under Apache-2.0 with
LLVM exceptions; MinGW-w64 under permissive licences) and the **embeddable Python** distribution (PSF licence). They are used only
while the game is being built on the user's computer and are deleted afterwards; their licence texts are inside their folders in
the installer. The installer is built with Inno Setup (<https://jrsoftware.org/>), which is not part of the product.
