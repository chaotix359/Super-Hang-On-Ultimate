#!/bin/bash
# Super Hang-On Ultimate - macOS installer. Builds the game ON THIS MAC from the user's own arcade ROM, then installs it.
# Runs inside a Terminal window (so progress is visible), started by "Install Super Hang-On Ultimate.app".
#
# Automation / testing: set SHO_ROM=/path/to/shangon.zip (and optionally SHO_MDROM, SHO_DEST) to skip the dialogs.
#                       SHO_TEST=1 additionally skips the Mac-only steps (tool check, app build) so the rest can be tried on Linux.
set -u
VERSION="@VERSION@"
HERE="$(cd "$(dirname "$0")" && pwd)"
PAYLOAD="${SHO_PAYLOAD:-$HERE/payload.tar.gz}"
mkdir -p "$HOME/Library/Logs"
LOG="$HOME/Library/Logs/SuperHangOn-install.log"
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

say()  { printf '\n==> %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
gui()  { [ -z "${SHO_ROM:-}" ]; }                        # are we allowed to show dialogs?

dlg() {   # dlg "message" "left button" "right (default) button"  -> prints the button clicked ("" if closed)
    osascript -e 'on run argv' -e 'try' \
        -e 'set r to display dialog (item 1 of argv) with title "Super Hang-On Ultimate" buttons {item 2 of argv, item 3 of argv} default button (item 3 of argv)' \
        -e 'return button returned of r' -e 'on error' -e 'return ""' -e 'end try' -e 'end run' "$1" "$2" "$3" 2>/dev/null
}
pick() {  # pick "prompt" [zip]   -> prints a POSIX path, or "" if cancelled
    if [ "${2:-}" = "zip" ]; then
        osascript -e 'on run argv' -e 'try' -e 'return POSIX path of (choose file with prompt (item 1 of argv) of type {"public.zip-archive"})' \
            -e 'on error' -e 'return ""' -e 'end try' -e 'end run' "$1" 2>/dev/null
    else
        osascript -e 'on run argv' -e 'try' -e 'return POSIX path of (choose file with prompt (item 1 of argv))' \
            -e 'on error' -e 'return ""' -e 'end try' -e 'end run' "$1" 2>/dev/null
    fi
}
die() {
    echo; echo "ERROR: $*"; echo "The full log is in: $LOG"
    if gui; then
        msg=$(printf '%s' "$*" | tr -d '"\\' | cut -c1-300)
        osascript -e "display alert \"Super Hang-On Ultimate could not be installed\" message \"$msg  (Log: ~/Library/Logs/SuperHangOn-install.log)\" as critical" >/dev/null 2>&1
    fi
    exit 1
}

clt_ok() {
    xcode-select -p >/dev/null 2>&1 || return 1
    cc_path="$(xcrun -f clang 2>/dev/null)" || return 1
    sdk="$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)" || return 1
    [ -x "$cc_path" ] && [ -f "$sdk/usr/include/stdio.h" ]
}

echo "Super Hang-On Ultimate installer $VERSION"
[ -f "$PAYLOAD" ] || die "The installer's files are missing ($PAYLOAD). Please download the installer again."

if [ -z "${SHO_TEST:-}" ]; then
    [ "$(uname -s)" = "Darwin" ] || die "This installer is for macOS."
    major="$(sw_vers -productVersion | cut -d. -f1)"
    [ "$major" -ge 11 ] || die "macOS 11 (Big Sur) or later is required."
fi
ARCH="$(uname -m)"

# ---------------------------------------------------------------- welcome
if gui; then
    r="$(dlg "This installer BUILDS Super Hang-On Ultimate on your Mac from your own arcade ROM set (shangon.zip). The ROM is not included.

It needs Apple's free Command Line Tools (it will offer to install them) and takes about 5 to 15 minutes. Nothing from SEGA's game is contained in this installer; the program it builds is for your own use. Super Hang-On is (c) SEGA; this project is not affiliated with SEGA." "Cancel" "Continue")"
    [ "$r" = "Continue" ] || { echo "Cancelled."; exit 0; }
fi

# ---------------------------------------------------------------- the ROMs
ROM="${SHO_ROM:-}"
if [ -z "$ROM" ]; then
    say "Choose your arcade ROM set"
    ROM="$(pick "Select your arcade ROM set (shangon.zip)" zip)"
    [ -n "$ROM" ] || { echo "Cancelled."; exit 0; }
fi
[ -f "$ROM" ] || die "ROM file not found: $ROM"
MDROM="${SHO_MDROM:-}"
if gui; then
    r="$(dlg "Optional: do you also have the Mega Drive ROM, 'Super Hang-On (World) (En,Ja) (Rev A)'? It enables the Mega Drive Original Mode. You can add the file to the game folder later instead." "Skip" "Choose file...")"
    if [ "$r" = "Choose file..." ]; then MDROM="$(pick "Select your Mega Drive ROM")"; fi
fi
DEST="${SHO_DEST:-$HOME/Applications/Super Hang-On Ultimate}"
note "Arcade ROM: $ROM"
[ -n "$MDROM" ] && note "Mega Drive ROM: $MDROM"
note "Installing to: $DEST"

# ---------------------------------------------------------------- tools
if [ -z "${SHO_TEST:-}" ]; then
    say "Checking for Apple's Command Line Tools"
    if ! clt_ok; then
        if gui; then
            r="$(dlg "Super Hang-On Ultimate is compiled on your Mac, which needs Apple's free Command Line Tools. A window from Apple will open: click Install and wait for it to finish (this can take a while). This installer continues by itself afterwards." "Cancel" "Install Tools")"
            [ "$r" = "Install Tools" ] || { echo "Cancelled."; exit 0; }
        fi
        xcode-select --install >/dev/null 2>&1
        note "Waiting for the Command Line Tools installation to finish (up to 60 minutes)..."
        n=0
        until clt_ok; do
            n=$((n + 1)); [ "$n" -gt 720 ] && die "The Command Line Tools were not installed. Install them (xcode-select --install) and run this installer again."
            sleep 5
        done
    fi
    CC="$(xcrun -f clang)"
    SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"; export SDKROOT      # the compiler must be told where Apple's system headers are
    note "SDK: $SDKROOT"
    PY="$(xcrun -f python3 2>/dev/null || command -v python3)"
else
    CC="${CC:-cc}"; PY="$(command -v python3)"
fi
note "Compiler: $CC"
[ -n "$PY" ] && "$PY" -c 'import zipfile, difflib' 2>/dev/null || die "Python 3 was not found (it comes with the Command Line Tools)."
note "Python: $PY"

# ---------------------------------------------------------------- build
WORK="$(mktemp -d "${TMPDIR:-/tmp}/superhangon-build.XXXXXX")" || die "Could not make a temporary folder."
cleanup() { rm -rf "$WORK"; case "$HERE" in */superhangon-launch.*) rm -rf "$HERE" ;; esac; }
trap cleanup EXIT
say "Unpacking the source"
tar -xzf "$PAYLOAD" -C "$WORK" || die "Could not unpack the installer's files."
SRC="$WORK/src"
cd "$SRC" || die "Unpacked source not found."

say "Step 1 of 2: translating the game from your ROM, then checking it (a few minutes)"
"$PY" setup.py --rom "$ROM" --cc "$CC" || die "Translating the game failed - see the messages above (a wrong ROM set, for example, is reported by name)."

APP_BUILT="$SRC/source/build/mac/SuperHangOn.app"
if [ -z "${SHO_TEST:-}" ]; then
    say "Step 2 of 2: compiling the Mac app for this Mac ($ARCH) (a few minutes)"
    ( cd source && ARCHS="$ARCH" ./build_mac.sh ) || die "Compiling the app failed."
    [ -d "$APP_BUILT" ] || die "The app was not produced."
else
    say "Test mode: skipping the app build"
    mkdir -p "$APP_BUILT"
fi

# ---------------------------------------------------------------- install
say "Installing"
mkdir -p "$DEST" || die "Could not create $DEST"
rm -rf "$DEST/SuperHangOn.app"
ditto "$APP_BUILT" "$DEST/SuperHangOn.app" 2>/dev/null || cp -R "$APP_BUILT" "$DEST/SuperHangOn.app" || die "Could not copy the app to $DEST"
[ "$ROM" -ef "$DEST/shangon.zip" ] || cp "$ROM" "$DEST/shangon.zip" || die "Could not copy the ROM to $DEST"
if [ -n "$MDROM" ] && [ -f "$MDROM" ]; then cp "$MDROM" "$DEST/" 2>/dev/null; fi
for f in README-MAC.txt README.txt LICENSE NOTICE.md; do cp "$SRC/$f" "$DEST/" 2>/dev/null; done
mkdir -p "$DEST/licenses" && cp "$SRC"/licenses/* "$DEST/licenses/" 2>/dev/null
xattr -cr "$DEST/SuperHangOn.app" 2>/dev/null

say "Done: Super Hang-On Ultimate is installed in $DEST"
if gui; then
    r="$(dlg "Super Hang-On Ultimate is installed in:
$DEST

The first time it starts, macOS may ask whether it may use files in your Applications/Documents folder: click Allow (that is the game folder, where your ROMs and saves live)." "Show in Finder" "Play")"
    if [ "$r" = "Play" ]; then open "$DEST/SuperHangOn.app"; else open "$DEST"; fi
fi
exit 0
