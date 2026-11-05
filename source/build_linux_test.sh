#!/bin/sh
# Headless test build (Linux/WSL/macOS): writes screenshots and out.wav; RECOMP=1 runs the recompiled C
set -e
cd "$(dirname "$0")/src"
M=third_party/musashi
cc -O2 -w -I. -c gen/main_rc.c -o /tmp/main_rc_$$.o &
cc -O2 -w -I. -c gen/sub_rc.c -o /tmp/sub_rc_$$.o
cat ../recomp/overrides/*.c > /tmp/ovr_$$.c && cc -O2 -w -I. -c /tmp/ovr_$$.c -o /tmp/ovr_$$.o
wait
cc -O2 -w -I. -o ../shotest test_main.c sho_core.c sho_video.c sho_music.c sho_md.c sho_mdgfx.c sho_mdcourse.c sho_mdsnd.c rc_runtime.c /tmp/main_rc_$$.o /tmp/sub_rc_$$.o /tmp/ovr_$$.o \
   $M/m68kcpu.c $M/m68kops.c $M/m68kdasm.c $M/softfloat/softfloat.c third_party/z80/z80.c third_party/nukedopn2/ym3438.c third_party/nukedopm/opm.c third_party/puff/puff.c -lm
rm -f /tmp/main_rc_$$.o /tmp/sub_rc_$$.o /tmp/ovr_$$.o /tmp/ovr_$$.c
