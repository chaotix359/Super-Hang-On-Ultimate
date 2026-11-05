#!/bin/sh
# One entry point for both builds.
#   ./build.sh          builds for the machine you are on (Mac -> the Mac app; otherwise -> Windows .exe)
#   ./build.sh mac      macOS app   (run on a Mac)            -> source/build/mac/SuperHangOn.app
#   ./build.sh win      Windows exe (cross-compiled; needs clang, lld, llvm-rc and toolchain/make_sysroot.sh run once)
#                                                             -> source/build/win/SuperHangOn.exe
# On Windows itself, use build.ps1 instead.
set -e
cd "$(dirname "$0")/source"
target=$1
[ -n "$target" ] && shift
if [ -z "$target" ]; then
    if [ "$(uname -s)" = "Darwin" ]; then target=mac; else target=win; fi
fi
case "$target" in
    mac) exec ./build_mac.sh "$@" ;;
    win) exec ./build_win.sh "$@" ;;
    *)   echo "usage: ./build.sh [mac|win]"; exit 1 ;;
esac
