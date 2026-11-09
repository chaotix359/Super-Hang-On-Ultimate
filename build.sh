#!/bin/sh
# One entry point for all three builds.
#   ./build.sh          builds for the machine you are on (Mac -> the Mac app; Linux -> the Linux program)
#   ./build.sh mac      macOS app   (run on a Mac)            -> source/build/mac/SuperHangOn.app
#   ./build.sh linux    Linux program (SDL2; run on Linux)    -> source/build/linux/SuperHangOn
#   ./build.sh win      Windows exe (cross-compiled; needs clang, lld, llvm-rc and toolchain/make_sysroot.sh run once)
#                                                             -> source/build/win/SuperHangOn.exe
# On Windows itself, use build.ps1 instead.
set -e
cd "$(dirname "$0")/source"
target=$1
[ -n "$target" ] && shift
if [ -z "$target" ]; then
    case "$(uname -s)" in Darwin) target=mac ;; Linux) target=linux ;; *) target=win ;; esac
fi
case "$target" in
    mac) exec ./build_mac.sh "$@" ;;
    win) exec ./build_win.sh "$@" ;;
    linux) exec ./build_linux.sh "$@" ;;
    *)   echo "usage: ./build.sh [mac|linux|win]"; exit 1 ;;
esac
