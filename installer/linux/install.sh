#!/bin/sh
# Super Hang-On Ultimate - Linux install from the source.
#
#   installer/linux/install.sh /path/to/shangon.zip [/path/to/megadrive-rom.bin]
#
# Run it from the source folder (the one with setup.py). It
#   1. checks for a C compiler, Python 3 and the SDL2 development files (and says what to install if they are missing),
#   2. makes the translated game code from your shangon.zip (setup.py, once) and builds the game (source/build_linux.sh,
#      which ends with the 1200-frame self-test),
#   3. installs it in ~/.local/share/super-hang-on-ultimate with your ROM(s) and the manuals, and adds a menu entry
#      (Games > Super Hang-On Ultimate) and a "superhangon" command in ~/.local/bin.
# Nothing is installed system-wide and no root is needed (only for installing the build tools, if they are missing).
# To uninstall: rm -rf ~/.local/share/super-hang-on-ultimate ~/.local/share/applications/super-hang-on-ultimate.desktop ~/.local/bin/superhangon
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ROM=$1; MDROM=$2
if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "Usage: installer/linux/install.sh /path/to/shangon.zip [/path/to/megadrive-rom]"
    echo "(shangon.zip = MAME's Super Hang-On set; the optional Mega Drive ROM = Super Hang-On (World) (Rev A), for Original Mode)"
    exit 1
fi
ROM=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
missing=""
command -v python3 >/dev/null 2>&1 || missing="$missing python3"
command -v cc >/dev/null 2>&1 || command -v gcc >/dev/null 2>&1 || command -v clang >/dev/null 2>&1 || missing="$missing compiler"
[ -n "$SDL2_CFLAGS" ] || pkg-config --exists sdl2 2>/dev/null || command -v sdl2-config >/dev/null 2>&1 || [ -f /usr/include/SDL2/SDL.h ] || missing="$missing sdl2"
if [ -n "$missing" ]; then
    echo "Missing:$missing. Install the build tools first, then run this again:"
    echo "   Debian / Ubuntu / Mint / Pop!_OS:  sudo apt install build-essential python3 libsdl2-dev"
    echo "   Fedora:                            sudo dnf install gcc python3 SDL2-devel"
    echo "   Arch / Manjaro / SteamOS desktop:  sudo pacman -S base-devel python sdl2"
    echo "   openSUSE:                          sudo zypper install gcc python3 libSDL2-devel"
    exit 1
fi

cd "$ROOT"
if [ ! -f source/src/gen/main_rc.c ] || [ ! -d source/recomp/overrides ]; then
    echo "== Making the game code from your ROM (setup.py) =="
    python3 setup.py --rom "$ROM"
fi
echo "== Building =="
ROM="$ROM" source/build_linux.sh

DEST=${XDG_DATA_HOME:-$HOME/.local/share}/super-hang-on-ultimate
echo "== Installing to $DEST =="
mkdir -p "$DEST"
cp source/build/linux/SuperHangOn "$DEST/"
cp "$ROM" "$DEST/shangon.zip"
if [ -n "$MDROM" ] && [ -f "$MDROM" ]; then cp "$MDROM" "$DEST/"; fi
cp README.txt README-LINUX.txt "$DEST/" 2>/dev/null || true
cp source/res/icon_preview.png "$DEST/icon.png" 2>/dev/null || true
mkdir -p "$DEST/music"
APPS=${XDG_DATA_HOME:-$HOME/.local/share}/applications
mkdir -p "$APPS" "$HOME/.local/bin"
cat > "$APPS/super-hang-on-ultimate.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Super Hang-On Ultimate
Comment=Native port of the arcade game, with the Mega Drive Original Mode
Exec="$DEST/SuperHangOn"
Path=$DEST
Icon=$DEST/icon.png
Terminal=false
Categories=Game;ArcadeGame;
DESK
ln -sf "$DEST/SuperHangOn" "$HOME/.local/bin/superhangon"
echo
echo "Installed. Start it from your applications menu (Super Hang-On Ultimate) or with: superhangon"
echo "The game folder is $DEST (settings, high scores, saves; put arranged MP3s in its music folder)."
