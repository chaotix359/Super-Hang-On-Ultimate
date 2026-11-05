#!/usr/bin/env python3
"""
Builds SuperHangOnUltimate-Mac-Installer-<version>.zip  (runs on Windows, Mac or Linux - Python 3, no other tools)

    python3 installer/mac/build-installer.py [--version 0.9.26]

The zip holds "Install Super Hang-On Ultimate.app": a small launcher + the project source + install.sh. On the user's Mac it asks for the
arcade ROM, builds the game from it (Apple's Command Line Tools provide the compiler) and installs it.
The installer contains no ROM and nothing translated from a ROM; this script stops if it finds such files in what it would pack.
"""
import argparse, io, os, re, sys, tarfile, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SKIP_DIRS = {".git", "installer", "build", "gen", "overrides", "cov", "music", "__pycache__", ".vscode", ".idea"}
SKIP_EXT = {".exe", ".zip", ".o", ".obj", ".dasm", ".sav", ".nv", ".app"}
SKIP_NAMES = {"shangon.ini", "gamepad.log", ".DS_Store", "Thumbs.db"}
FORBIDDEN_EXT = {".exe", ".zip", ".dasm", ".sav", ".nv", ".ini", ".gen", ".md"}      # .md only allowed for README/NOTICE/LICENSE docs
TEXT_EXT = {".sh", ".py", ".c", ".h", ".m", ".plist", ".txt", ".md", ".patch", ".rc", ".manifest"}
EXEC_NAMES = {"build_mac.sh", "build_win.sh", "build_linux_test.sh", "make_sysroot.sh", "setup.py", "build.sh"}
REQUIRED = ["setup.py", "LICENSE", "NOTICE.md", "README-MAC.txt", "README.txt", "source/build_mac.sh", "source/recomp/recomp.py",
            "source/recomp/coverage.bin", "source/res/shangon.icns", "source/src/mac_main.m", "source/tests/selftest_1200.txt"]


def collect():
    files = []
    for d, dirs, names in os.walk(ROOT):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
        for n in sorted(names):
            if os.path.splitext(n)[1].lower() in SKIP_EXT or n in SKIP_NAMES:
                continue
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
    if bad:
        sys.exit("Refusing to build - these must not be in the installer:\n  " + "\n  ".join(bad))
    missing = [r for r in REQUIRED if r not in files]
    if missing:
        sys.exit("Missing from the source: " + ", ".join(missing))
    if not [f for f in files if f.endswith(".patch")]:
        sys.exit("No patches in source/recomp/patches - run: python3 setup.py --make-patches")


def payload(files):
    """tar.gz of the source (top folder 'src'); text files get Unix line endings, scripts the executable bit"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        for p in files:
            data = open(os.path.join(ROOT, p), "rb").read()
            if os.path.splitext(p)[1].lower() in TEXT_EXT:
                data = data.replace(b"\r\n", b"\n")
            ti = tarfile.TarInfo("src/" + p)
            ti.size, ti.mtime = len(data), int(time.time())
            ti.mode = 0o755 if os.path.basename(p) in EXEC_NAMES else 0o644
            ti.uname = ti.gname = "root"
            tf.addfile(ti, io.BytesIO(data))
    return buf.getvalue()


PLIST = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
 <key>CFBundleName</key><string>Install Super Hang-On Ultimate</string>
 <key>CFBundleDisplayName</key><string>Install Super Hang-On Ultimate</string>
 <key>CFBundleIdentifier</key><string>local.superhangon.installer</string>
 <key>CFBundleExecutable</key><string>installer</string>
 <key>CFBundleIconFile</key><string>shangon</string>
 <key>CFBundlePackageType</key><string>APPL</string>
 <key>CFBundleShortVersionString</key><string>@VERSION@</string>
 <key>CFBundleVersion</key><string>@VERSION@</string>
 <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict></plist>
"""

LAUNCHER = """#!/bin/bash
# Opens the installer in a Terminal window so the user can watch its progress.
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$HERE/../Resources"
T="$(mktemp -d "${TMPDIR:-/tmp}/superhangon-launch.XXXXXX")" || exit 1
cp "$RES/install.sh" "$T/Install Super Hang-On Ultimate.command"
cp "$RES/payload.tar.gz" "$T/payload.tar.gz"
chmod +x "$T/Install Super Hang-On Ultimate.command"
xattr -cr "$T" 2>/dev/null          # copies made here are not 'downloaded', so macOS does not ask a second time
open -a Terminal "$T/Install Super Hang-On Ultimate.command"
"""

READ_FIRST = """SUPER HANG-ON - macOS INSTALLER  (version @VERSION@)

1. Double-click "Install Super Hang-On Ultimate".
   macOS will probably say it cannot check the app for malicious software, because this installer is not signed by an
   Apple developer. This is expected. To open it anyway:
     - macOS 11-14:  right-click (or Control-click) "Install Super Hang-On Ultimate" > Open > Open.
     - macOS 15 and later:  double-click it once, then open System Settings > Privacy & Security, scroll down, and click
       "Open Anyway" next to "Install Super Hang-On Ultimate".
     - or, in Terminal:   xattr -dr com.apple.quarantine "Install Super Hang-On Ultimate.app"

2. You need your own copy of the arcade ROM set "shangon.zip" (MAME's unprotected Super Hang-On set). It is not included.
   Optional: the Mega Drive ROM "Super Hang-On (World) (En,Ja) (Rev A)" for Original Mode.

3. The installer opens a Terminal window and builds the game on your Mac. It offers to install Apple's free Command Line
   Tools if they are missing. Allow 5-15 minutes. The game ends up in  ~/Applications/Super Hang-On Ultimate.

To remove the game, drag that folder to the Trash (your settings and high scores are in it).
If something goes wrong, the log is ~/Library/Logs/SuperHangOn-install.log
"""


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--version", default="0.9.26"); a = ap.parse_args()
    files = collect(); check(files)
    pl = payload(files)
    install = open(os.path.join(HERE, "install.sh"), "rb").read().replace(b"\r\n", b"\n").replace(b"@VERSION@", a.version.encode())
    icns = open(os.path.join(ROOT, "source", "res", "shangon.icns"), "rb").read()
    out_dir = os.path.join(HERE, "output"); os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "SuperHangOnUltimate-Mac-Installer-%s.zip" % a.version)
    app = "Install Super Hang-On Ultimate.app/Contents/"

    def add(z, name, data, mode):
        zi = zipfile.ZipInfo(name, time.localtime()[:6])
        zi.external_attr = (0o100000 | mode) << 16
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, data)

    with zipfile.ZipFile(out, "w") as z:
        add(z, "READ ME FIRST.txt", READ_FIRST.replace("@VERSION@", a.version).encode(), 0o644)
        add(z, app + "Info.plist", PLIST.replace("@VERSION@", a.version).encode(), 0o644)
        add(z, app + "MacOS/installer", LAUNCHER.encode(), 0o755)
        add(z, app + "Resources/install.sh", install, 0o755)
        add(z, app + "Resources/payload.tar.gz", pl, 0o644)
        add(z, app + "Resources/shangon.icns", icns, 0o644)
    print("Built %s  (%.1f MB, %d source files)" % (out, os.path.getsize(out) / 1e6, len(files)))


if __name__ == "__main__":
    main()
