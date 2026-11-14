#!/bin/sh
# Cross-compile SuperHangOn.exe (x86-64, Windows 10/11) with clang + lld against MinGW-w64 headers
# and import libraries generated from MinGW-w64's .def files. Links only to system DLLs.
set -e
R=${SYSROOT:-$(dirname "$0")/toolchain/sysroot}
CC="clang --target=x86_64-w64-mingw32 -nostdinc -isystem $(clang -print-resource-dir)/include -isystem $R/include"
CFLAGS="-O2 -DNDEBUG -fno-strict-aliasing -w"
mkdir -p build/win
cat recomp/overrides/*.c > build/win/overrides_all.c
SRCS="src/win32_main.c src/launcher_core.c src/sho_controls.c src/win32_d3d11.c src/rc_runtime.c src/gen/main_rc.c src/gen/sub_rc.c build/win/overrides_all.c src/sho_core.c src/sho_video.c src/sho_music.c src/sho_md.c src/sho_mdgfx.c src/sho_mdcourse.c src/sho_mdsnd.c src/third_party/musashi/m68kcpu.c src/third_party/musashi/m68kops.c src/third_party/musashi/m68kdasm.c src/third_party/musashi/softfloat/softfloat.c src/third_party/z80/z80.c src/third_party/nukedopn2/ym3438.c src/third_party/nukedopm/opm.c src/third_party/puff/puff.c src/win32_crt0.c"
OBJS=""
for s in $SRCS; do o=build/win/$(basename $s .c).o; $CC $CFLAGS -Isrc -c $s -o $o & OBJS="$OBJS $o"; done
wait
llvm-rc /FO build/win/app.res res/app.rc
$CC -fuse-ld=lld -nostdlib -Wl,-subsystem,windows:6.2 -Wl,-entry,WinMainCRTStartup -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va \
   $OBJS build/win/app.res -L$R/lib -lucrtbase -lkernel32 -luser32 -lgdi32 -lwinmm -ld3d11 -ldxgi -o build/win/SuperHangOn.exe
ls -la build/win/SuperHangOn.exe
