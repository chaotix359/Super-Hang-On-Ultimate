# Windows installer

`SuperHangOnUltimate-Setup-<version>.exe` asks the user for their arcade ROM (`shangon.zip`; optionally the Mega Drive ROM),
builds the game on their PC and installs it. The installer holds no ROM and no code translated from a ROM: only the project
source, `recomp/patches`, `recomp/coverage.bin`, LLVM-MinGW (compiler) and Python (embeddable). The tools are used during
installation and thrown away afterwards.

## Building the installer (on Windows)

1. Install **Inno Setup 6.3 or newer** (free): <https://jrsoftware.org/isdl.php>
2. In the repository folder: `powershell -ExecutionPolicy Bypass -File installer\windows\build-installer.ps1`
3. The result is `installer\windows\output\SuperHangOnUltimate-Setup-0.9.26.exe`.

The first run downloads Python and LLVM-MinGW into `installer\windows\cache` and records their SHA-256 in `downloads.lock`
(commit that file; later runs verify against it). The script refuses to build if `src\gen`, `recomp\overrides`, a ROM or a built
program would end up in the installer.

## What the installer does on the user's PC

1. Shows the info page, lets them pick the folder (per-user, no admin rights, because the game saves next to its `.exe`).
2. Asks for `shangon.zip` (required) and the Mega Drive ROM (optional); both are copied into the game folder.
3. Unpacks the tools to a temporary folder, runs `build-game.cmd`: `setup.py` (translates from the ROM, then the 1200-frame
   behaviour check) and `build_win.ps1` (compiles `SuperHangOn.exe`).
4. Copies the `.exe` to the game folder, makes the Start-menu entry, deletes the temporary tools.

If the build fails, the installer rolls back and tells the user where the log is (`%TEMP%\SuperHangOn-install.log`).
Uninstalling removes the program but keeps settings, high scores, saves and the ROM copies in the game folder.

## Known limits

* Needs about 1.5 GB free while installing and 5-15 minutes (the compile is single-threaded).
* Not code-signed: Windows SmartScreen shows a warning until the installer has reputation or is signed with a certificate.
* A ROM set other than the unprotected MAME `shangon` set fails with a clear error from the game's own ROM check.
