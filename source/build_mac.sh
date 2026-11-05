#!/bin/sh
# Build SuperHangOn.app for macOS 11 or later - one app for both Apple Silicon and Intel Macs.
#
# Needs only Apple's Command Line Tools (run `xcode-select --install` once; full Xcode is not needed).
#   ./build_mac.sh                   universal app (arm64 + x86_64) in build/mac/SuperHangOn.app
#   ARCHS=arm64 ./build_mac.sh       one architecture only (faster while developing)
#   ROM=/path/to/shangon.zip ./build_mac.sh
#                                    also run the self-test with that ROM (it is found automatically
#                                    in ../shangon.zip or build/mac/shangon.zip)
#
# The self-test runs the game headless for 1200 frames on this Mac and compares the machine state, frame by
# frame, with tests/selftest_1200.txt (recorded with the verified build). It proves the recompiled CPUs and
# their fiber switch (src/rc_ctx.c) behave exactly as on Windows/Linux.
set -e
cd "$(dirname "$0")"

CC=${CC:-$(xcrun -f clang 2>/dev/null || echo clang)}
ARCHS=${ARCHS:-"arm64 x86_64"}
MIN=11.0
OUT=build/mac
APP=$OUT/SuperHangOn.app
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
CFLAGS="-O2 -DNDEBUG -fno-strict-aliasing -ffp-contract=off -w -mmacosx-version-min=$MIN"
OBJCFLAGS="$CFLAGS -fobjc-arc"
FRAMEWORKS="-framework Cocoa -framework Metal -framework QuartzCore -framework GameController -framework AudioToolbox -framework CoreAudio -framework CoreVideo -framework IOKit"

# the macOS SDK (system headers and frameworks): clang must be told where it is
SDKROOT=${SDKROOT:-$(xcrun --sdk macosx --show-sdk-path 2>/dev/null || true)}
if [ -z "$SDKROOT" ] || [ ! -f "$SDKROOT/usr/include/stdio.h" ]; then
    echo "The macOS SDK was not found (${SDKROOT:-no path})."
    echo "Install the Command Line Tools with:   xcode-select --install"
    echo "If they are already installed but broken (this happens after some macOS updates), reinstall them:"
    echo "   sudo rm -rf /Library/Developer/CommandLineTools"
    echo "   xcode-select --install"
    exit 1
fi
export SDKROOT
echo "Using the SDK at $SDKROOT"

M=src/third_party/musashi
CORE="src/rc_runtime.c src/rc_ctx.c src/gen/main_rc.c src/gen/sub_rc.c src/sho_core.c src/sho_video.c src/sho_music.c src/sho_md.c src/sho_mdgfx.c src/sho_mdcourse.c src/sho_mdsnd.c
      $M/m68kcpu.c $M/m68kops.c $M/m68kdasm.c $M/softfloat/softfloat.c src/third_party/z80/z80.c src/third_party/nukedopn2/ym3438.c src/third_party/nukedopm/opm.c src/third_party/puff/puff.c"
FRONT_C="src/ini_file.c"
FRONT_M="src/mac_main.m src/mac_launcher.m src/mac_metal.m"

mkdir -p $OUT
cat recomp/overrides/*.c > $OUT/overrides_all.c

# compile: one job per source file, at most $JOBS at a time
compile_all() {   # $1 = arch, $2 = object folder, then the sources
    a=$1; d=$2; shift 2
    mkdir -p "$d"
    n=0
    for s in "$@"; do
        o="$d/$(basename "$s" | sed 's/\.[cm]$//').o"
        case "$s" in *.m) f="$OBJCFLAGS" ;; *) f="$CFLAGS" ;; esac
        rm -f "$o"
        $CC -arch $a -isysroot "$SDKROOT" $f -Isrc -c "$s" -o "$o" &
        n=$((n + 1)); if [ $n -ge $JOBS ]; then wait; n=0; fi
    done
    wait
}

BINS=""
for a in $ARCHS; do
    echo "Compiling for $a..."
    compile_all $a $OUT/$a $CORE $OUT/overrides_all.c $FRONT_C $FRONT_M
    objs=""
    for s in $CORE $OUT/overrides_all.c $FRONT_C $FRONT_M; do
        o="$OUT/$a/$(basename "$s" | sed 's/\.[cm]$//').o"
        if [ ! -f "$o" ]; then echo "Compile failed for $s ($a) - see the errors above."; exit 1; fi
        objs="$objs $o"
    done
    $CC -arch $a -isysroot "$SDKROOT" -mmacosx-version-min=$MIN $objs $FRAMEWORKS -o $OUT/$a/SuperHangOn
    BINS="$BINS $OUT/$a/SuperHangOn"
done

echo "Making the app bundle..."
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
lipo -create $BINS -output "$APP/Contents/MacOS/SuperHangOn"
cp res/shangon.icns "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                 <string>Super Hang-On Ultimate</string>
    <key>CFBundleDisplayName</key>          <string>Super Hang-On Ultimate</string>
    <key>CFBundleIdentifier</key>           <string>local.superhangon.port</string>
    <key>CFBundleExecutable</key>           <string>SuperHangOn</string>
    <key>CFBundleIconFile</key>             <string>shangon</string>
    <key>CFBundlePackageType</key>          <string>APPL</string>
    <key>CFBundleShortVersionString</key>   <string>0.9.26</string>
    <key>CFBundleVersion</key>              <string>0.9.26</string>
    <key>LSMinimumSystemVersion</key>       <string>11.0</string>
    <key>LSApplicationCategoryType</key>    <string>public.app-category.racing-games</string>
    <key>NSHighResolutionCapable</key>      <true/>
    <key>NSSupportsAutomaticGraphicsSwitching</key> <true/>
    <key>NSDocumentsFolderUsageDescription</key> <string>Super Hang-On Ultimate reads its ROMs and music, and keeps its settings and saves, in the folder it is in.</string>
    <key>NSDesktopFolderUsageDescription</key>   <string>Super Hang-On Ultimate reads its ROMs and music, and keeps its settings and saves, in the folder it is in.</string>
    <key>NSDownloadsFolderUsageDescription</key> <string>Super Hang-On Ultimate reads its ROMs and music, and keeps its settings and saves, in the folder it is in.</string>
</dict>
</plist>
PLIST
codesign --force --sign - "$APP" >/dev/null 2>&1 || echo "(codesign failed: the app may need right-click > Open the first time)"
echo "Built $APP ($(lipo -archs "$APP/Contents/MacOS/SuperHangOn"))"
# the source folder inside the game folder (as on Windows, where SuperHangOn.exe sits next to source/):
# put the app next to shangon.zip, where it looks for its ROMs, music and settings
if [ -f ../shangon.zip ] || [ -f ../SuperHangOn.exe ]; then
    rm -rf ../SuperHangOn.app && cp -R "$APP" ../SuperHangOn.app && echo "Copied to $(cd .. && pwd)/SuperHangOn.app"
fi

# ---- self-test (needs the arcade ROM) ----
if [ -z "$ROM" ]; then for r in ../shangon.zip $OUT/shangon.zip; do if [ -f "$r" ]; then ROM=$r; fi; done; fi
if [ -n "$ROM" ] && [ -f "$ROM" ]; then
    HOST=$(uname -m)
    echo "Self-test on $HOST with $ROM..."
    T=$OUT/selftest; mkdir -p $T
    $CC -arch $HOST -isysroot "$SDKROOT" $CFLAGS -Isrc -o $T/shotest src/test_main.c $OUT/$HOST/rc_runtime.o $OUT/$HOST/rc_ctx.o $OUT/$HOST/main_rc.o $OUT/$HOST/sub_rc.o \
        $OUT/$HOST/overrides_all.o $OUT/$HOST/sho_core.o $OUT/$HOST/sho_video.o $OUT/$HOST/sho_music.o $OUT/$HOST/sho_md.o $OUT/$HOST/sho_mdgfx.o \
        $OUT/$HOST/sho_mdcourse.o $OUT/$HOST/sho_mdsnd.o $OUT/$HOST/m68kcpu.o $OUT/$HOST/m68kops.o $OUT/$HOST/m68kdasm.o $OUT/$HOST/softfloat.o \
        $OUT/$HOST/z80.o $OUT/$HOST/ym3438.o $OUT/$HOST/opm.o $OUT/$HOST/puff.o 2>/dev/null || { echo "Self-test skipped: build $HOST too (ARCHS)"; exit 0; }
    ROMABS=$(cd "$(dirname "$ROM")" && pwd)/$(basename "$ROM")
    rm -f $T/hashes.txt
    ( cd $T && RECOMP=1 HASHLOG=hashes.txt ./shotest "$ROMABS" 1200 auto > run.log 2>&1 ) || echo "(the self-test run stopped early)"
    touch $T/hashes.txt
    if cmp -s $T/hashes.txt tests/selftest_1200.txt; then
        echo "Self-test PASSED: 1200 frames identical to the reference (recompiled CPUs and fiber switch OK on $HOST)"
    else
        echo "Self-test FAILED: see $T/run.log; first difference:"
        cmp $T/hashes.txt tests/selftest_1200.txt || true
        exit 2
    fi
else
    echo "(Self-test skipped: put shangon.zip next to the source folder, or run with ROM=/path/to/shangon.zip)"
fi
