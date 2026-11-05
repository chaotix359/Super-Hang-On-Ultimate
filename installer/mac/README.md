# macOS installer

`SuperHangOnUltimate-Mac-Installer-<version>.zip` contains **Install Super Hang-On Ultimate.app**. It asks the user for their arcade ROM
(`shangon.zip`, optionally the Mega Drive ROM), builds the game on their Mac and installs it to `~/Applications/Super Hang-On Ultimate`.
It holds no ROM and nothing translated from a ROM: only the project source, `recomp/patches` and `recomp/coverage.bin`.

## Building the installer

Anywhere with Python 3 (Windows, Mac or Linux):

    python3 installer/mac/build-installer.py

The result is `installer/mac/output/SuperHangOnUltimate-Mac-Installer-0.9.26.zip`. The script refuses to build if `src/gen`,
`recomp/overrides`, a ROM or a built program would end up in it. (No compiler is bundled: Macs use Apple's Command Line Tools.)

## What the installer does on the user's Mac

1. The app opens a Terminal window (so the progress is visible) and shows a welcome dialog.
2. Asks for `shangon.zip` and, optionally, the Mega Drive ROM.
3. Checks for Apple's Command Line Tools; if missing, starts Apple's installer and waits for it to finish.
4. Unpacks the source to a temporary folder; runs `setup.py` (translates from the ROM, then the 1200-frame behaviour check,
   which also proves the Apple Silicon / Intel stack switch) and `build_mac.sh` (compiles the app for this Mac's architecture).
5. Copies the app, the ROM(s) and the documentation to `~/Applications/Super Hang-On Ultimate`, offers to start the game.

The log is `~/Library/Logs/SuperHangOn-install.log`. To uninstall, drag the folder to the Trash.

## Known limits

* **Not signed or notarized**: macOS Gatekeeper blocks the downloaded app until the user right-clicks > Open (macOS 11-14) or uses
  System Settings > Privacy & Security > Open Anyway (macOS 15+). The zip's `READ ME FIRST.txt` explains this. Removing the
  warning needs an Apple Developer account (paid) to sign and notarize.
* Builds for the architecture of the Mac it runs on (not a universal app).
* Test hooks for developers: `SHO_ROM=<zip>` skips the dialogs; with `SHO_TEST=1` the Mac-only steps are skipped too.
