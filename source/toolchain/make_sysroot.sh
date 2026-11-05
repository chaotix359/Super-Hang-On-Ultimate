#!/bin/sh
# Builds a minimal Windows x86-64 sysroot for clang/lld from MinGW-w64 sources WITHOUT running
# MinGW's configure/make: headers are copied, and import libraries are generated from the .def files.
# Needs: git, clang, llvm-dlltool.   Usage: toolchain/make_sysroot.sh
set -e
D=$(cd "$(dirname "$0")" && pwd); S=$D/mingw-w64; R=$D/sysroot
[ -d "$S" ] || git clone --depth 1 https://github.com/mingw-w64/mingw-w64 "$S"
rm -rf "$R"; mkdir -p "$R/include" "$R/lib"
cp -r "$S/mingw-w64-headers/crt/"* "$R/include/"; cp -r "$S/mingw-w64-headers/include/"* "$R/include/"
sed -e 's/@DEFAULT_MSVCRT_VERSION@/0xE00/' -e 's/@DEFAULT_WIN32_WINNT@/0xA00/' "$S/mingw-w64-headers/crt/_mingw.h.in" > "$R/include/_mingw.h"
rm -f "$R/include/_mingw.h.in"
for d in kernel32 user32 gdi32 winmm ucrtbase d3d11 dxgi; do
  f=$S/mingw-w64-crt/lib-common/$d.def; [ -f "$f" ] || f=$f.in
  clang -E -P -w -x c -nostdinc -DDEF_X64 -D__x86_64__ -I"$S/mingw-w64-crt/def-include" -I"$S/mingw-w64-crt/lib-common" "$f" -o "$R/lib/$d.def"
  llvm-dlltool -m i386:x86-64 -d "$R/lib/$d.def" -l "$R/lib/lib$d.a"
done
echo "sysroot ready: $R"
