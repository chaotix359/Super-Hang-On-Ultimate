#!/bin/sh
# Build the Linux version (SDL2) -> build/linux/SuperHangOn
#
# Needs a C compiler and the SDL2 development files:
#   Debian/Ubuntu/Mint:  sudo apt install build-essential libsdl2-dev
#   Fedora:              sudo dnf install gcc SDL2-devel
#   Arch/Manjaro:        sudo pacman -S base-devel sdl2
# (SDL2_CFLAGS / SDL2_LIBS override where SDL2 is found.)
#
#   ./build_linux.sh                  build
#   ROM=/path/to/shangon.zip ./build_linux.sh
#                                     also run the 1200-frame self-test with that ROM (found automatically in
#                                     ../shangon.zip or build/linux/shangon.zip): the machine state must match
#                                     tests/selftest_1200.txt frame for frame, as on Windows and the Mac
set -e
cd "$(dirname "$0")"
CC=${CC:-cc}
OUT=build/linux
JOBS=$(nproc 2>/dev/null || echo 4)
CFLAGS="-O2 -DNDEBUG -fno-strict-aliasing -ffp-contract=off -w"
if [ -z "$SDL2_CFLAGS" ]; then
    SDL2_CFLAGS=$(pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null || true)
    SDL2_LIBS=$(pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null || true)
fi
if [ -z "$SDL2_CFLAGS" ] && [ ! -f /usr/include/SDL2/SDL.h ]; then
    echo "SDL2 development files not found. Install them, e.g.:"
    echo "   sudo apt install libsdl2-dev      (Debian, Ubuntu, Mint)"
    echo "   sudo dnf install SDL2-devel       (Fedora)"
    echo "   sudo pacman -S sdl2               (Arch)"
    exit 1
fi
[ -z "$SDL2_CFLAGS" ] && SDL2_CFLAGS="-I/usr/include/SDL2 -D_REENTRANT"
[ -z "$SDL2_LIBS" ] && SDL2_LIBS="-lSDL2"

M=src/third_party/musashi
CORE="src/rc_runtime.c src/rc_ctx.c src/gen/main_rc.c src/gen/sub_rc.c src/sho_core.c src/sho_video.c src/sho_music.c src/sho_md.c src/sho_mdgfx.c src/sho_mdcourse.c src/sho_mdsnd.c
      $M/m68kcpu.c $M/m68kops.c $M/m68kdasm.c $M/softfloat/softfloat.c src/third_party/z80/z80.c src/third_party/nukedopn2/ym3438.c src/third_party/nukedopm/opm.c src/third_party/puff/puff.c"
FRONT="src/ini_file.c src/launcher_core.c src/linux_main.c src/linux_launcher.c"

mkdir -p $OUT/obj
cat recomp/overrides/*.c > $OUT/overrides_all.c
echo "Compiling..."
n=0
for s in $CORE $OUT/overrides_all.c $FRONT; do
    o="$OUT/obj/$(basename "$s" .c).o"
    rm -f "$o"
    case "$s" in src/linux_*) f="$CFLAGS $SDL2_CFLAGS" ;; *) f="$CFLAGS" ;; esac
    $CC $f -Isrc -c "$s" -o "$o" &
    n=$((n + 1)); if [ $n -ge $JOBS ]; then wait; n=0; fi
done
wait
objs=""
for s in $CORE $OUT/overrides_all.c $FRONT; do
    o="$OUT/obj/$(basename "$s" .c).o"
    if [ ! -f "$o" ]; then echo "Compile failed for $s - see the errors above."; exit 1; fi
    objs="$objs $o"
done
$CC $objs $SDL2_LIBS -lm -lpthread -o $OUT/SuperHangOn
echo "Built $OUT/SuperHangOn"
# next to shangon.zip (the game folder), as on Windows and the Mac
if [ -f ../shangon.zip ] || [ -f ../SuperHangOn.exe ]; then cp $OUT/SuperHangOn ../SuperHangOn && echo "Copied to $(cd .. && pwd)/SuperHangOn"; fi

# ---- self-test (needs the arcade ROM) ----
if [ -z "$ROM" ]; then for r in ../shangon.zip $OUT/shangon.zip; do if [ -f "$r" ]; then ROM=$r; fi; done; fi
if [ -n "$ROM" ] && [ -f "$ROM" ]; then
    echo "Self-test with $ROM..."
    T=$OUT/selftest; mkdir -p $T
    core_objs=""; for s in $CORE $OUT/overrides_all.c; do core_objs="$core_objs $OUT/obj/$(basename "$s" .c).o"; done
    $CC $CFLAGS -Isrc -o $T/shotest src/test_main.c $core_objs -lm -lpthread
    ROMABS=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
    rm -f $T/hashes.txt
    ( cd $T && RECOMP=1 HASHLOG=hashes.txt ./shotest "$ROMABS" 1200 auto > run.log 2>&1 ) || echo "(the self-test run stopped early)"
    touch $T/hashes.txt
    if cmp -s $T/hashes.txt tests/selftest_1200.txt; then
        echo "Self-test PASSED: 1200 frames identical to the reference"
    else
        echo "Self-test FAILED: see $T/run.log; first difference:"
        cmp $T/hashes.txt tests/selftest_1200.txt || true
        exit 2
    fi
else
    echo "(no shangon.zip found: self-test skipped)"
fi
