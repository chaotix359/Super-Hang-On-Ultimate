#!/usr/bin/env python3
"""
Builds SuperHangOnUltimate-Linux-<version>.zip, the Linux download for a GitHub release (runs anywhere with Python 3).

    python3 installer/linux/build-release.py [--version 0.9.34]

The zip holds the project source and installer/linux/install.sh, plus INSTALL-LINUX.txt. On the user's PC, install.sh makes
the game code from their own shangon.zip, builds the game and installs it. Like the Windows and Mac installers, the zip
contains no ROM, no code translated from a ROM and no built program; this script stops if it finds such files in what it
would pack. The result is written to installer/linux/output/.
"""
import argparse, os, re, sys, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SKIP_DIRS = {".git", "build", "gen", "overrides", "cov", "music", "output", "cache", "__pycache__", ".vscode", ".idea"}
SKIP_TOP = {"installer"}                      # of installer/, only installer/linux goes in
SKIP_EXT = {".exe", ".zip", ".o", ".obj", ".dasm", ".sav", ".nv", ".app"}
SKIP_NAMES = {"shangon.ini", "gamepad.log", ".DS_Store", "Thumbs.db", "SuperHangOn", "shotest"}
FORBIDDEN_EXT = {".exe", ".zip", ".dasm", ".sav", ".nv", ".ini", ".gen", ".md"}      # .md only allowed for README/NOTICE/LICENSE docs
TEXT_EXT = {".sh", ".py", ".c", ".h", ".m", ".plist", ".txt", ".md", ".patch", ".rc", ".manifest", ".iss", ".cmd", ".ps1"}
EXEC_NAMES = {"install.sh", "build_linux.sh", "build_linux_test.sh", "build_mac.sh", "build_win.sh", "make_sysroot.sh", "setup.py", "build.sh"}
REQUIRED = ["setup.py", "LICENSE", "NOTICE.md", "README-LINUX.txt", "README.txt", "installer/linux/install.sh", "source/build_linux.sh",
            "source/recomp/recomp.py", "source/recomp/coverage.bin", "source/src/linux_main.c", "source/src/launcher_core.c",
            "source/tests/selftest_1200.txt"]

INSTALL_TXT = """SUPER HANG-ON ULTIMATE {v} - LINUX
==========================================

This download is the source code and an install script. It contains no game ROM and nothing made from one:
the install script builds the game on your PC from YOUR OWN copy of the arcade ROM set.

You need:
  * shangon.zip - MAME's "Super Hang-On (sitdown/upright, unprotected)" ROM set
  * optional: the Mega Drive ROM "Super Hang-On (World) (Rev A)" for Original Mode
  * a C compiler, Python 3 and the SDL2 development files. The script tells you the command if any are missing:
        Debian / Ubuntu / Mint / Pop!_OS:  sudo apt install build-essential python3 libsdl2-dev
        Fedora:                            sudo dnf install gcc python3 SDL2-devel
        Arch / Manjaro / SteamOS desktop:  sudo pacman -S base-devel python sdl2
        openSUSE:                          sudo zypper install gcc python3 libSDL2-devel
    On Bazzite and other "immutable" systems, install them inside a Distrobox container and run the script there.

Install:
  1. Unzip this folder somewhere in your home folder.
  2. Open a terminal in it and run (with your own paths; put quotes around names with spaces):
        sh installer/linux/install.sh /path/to/shangon.zip  "/path/to/Super Hang-On (World) (Rev A).bin"
  3. It takes several minutes: it makes the game code from your ROM, builds the game, runs a self-test and installs it
     in ~/.local/share/super-hang-on-ultimate with a menu entry (Games > Super Hang-On Ultimate) and a "superhangon" command.

README-LINUX.txt and README.txt are the manuals. Please don't share the game you build: it contains code made from your ROM.
"""


def version():
    try:
        m = re.search(r'SHO_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, "source", "src", "sho_version.h")).read())
        return m.group(1)
    except (OSError, AttributeError):
        return "0.0.0"


def collect():
    files = []
    for d, dirs, names in os.walk(ROOT):
        rel = os.path.relpath(d, ROOT).replace(os.sep, "/")
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS and not (rel == "." and x in SKIP_TOP))
        for n in sorted(names):
            if os.path.splitext(n)[1].lower() in SKIP_EXT or n in SKIP_NAMES:
                continue
            files.append(os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, "/"))
    for d, dirs, names in os.walk(os.path.join(ROOT, "installer", "linux")):   # the installer itself
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
        for n in sorted(names):
            if not n.endswith(".zip"):
                files.append(os.path.relpath(os.path.join(d, n), ROOT).replace(os.sep, "/"))
    return files


def check(files):
    bad = []
    for p in files:
        n, e = os.path.basename(p), os.path.splitext(p)[1].lower()
        if "/gen/" in "/" + p or "/overrides/" in "/" + p or p.startswith("source/build/"):
            bad.append(p)
        elif e in FORBIDDEN_EXT and not (e == ".md" and re.match(r"^(README|NOTICE|LICENSE).*\.md$", n)):
            bad.append(p)
        elif e == ".bin" and n != "coverage.bin":
            bad.append(p)
        elif e == "" and open(os.path.join(ROOT, p), "rb").read(4) == b"\x7fELF":   # a built program
            bad.append(p)
    if bad:
        sys.exit("Refusing to build - these must not be in the download:\n  " + "\n  ".join(bad))
    missing = [r for r in REQUIRED if r not in files]
    if missing:
        sys.exit("Missing from the source: " + ", ".join(missing))
    if not [f for f in files if f.endswith(".patch")]:
        sys.exit("No patches in source/recomp/patches - run: python3 setup.py --make-patches")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=version())
    a = ap.parse_args()
    files = collect()
    check(files)
    top = "SuperHangOnUltimate-Linux-%s" % a.version
    out_dir = os.path.join(HERE, "output")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, top + ".zip")
    now = time.localtime()[:6]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        def add(name, data, mode):
            zi = zipfile.ZipInfo(top + "/" + name, now)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 3                     # Unix, so the permissions below are kept when unzipped
            zi.external_attr = (0o100000 | mode) << 16
            z.writestr(zi, data)
        add("INSTALL-LINUX.txt", INSTALL_TXT.format(v=a.version).encode(), 0o644)
        for p in files:
            data = open(os.path.join(ROOT, p), "rb").read()
            if os.path.splitext(p)[1].lower() in TEXT_EXT:
                data = data.replace(b"\r\n", b"\n")   # (a checkout on Windows may have Windows line endings)
            add(p, data, 0o755 if os.path.basename(p) in EXEC_NAMES else 0o644)
    print("Built %s  (%.1f MB, %d source files)" % (out, os.path.getsize(out) / 1e6, len(files)))


if __name__ == "__main__":
    main()
