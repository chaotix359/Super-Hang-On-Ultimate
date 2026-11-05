/* Helpers for hand-written overrides of recompiled functions.
 * Native versions of individual instructions whose operands we change; flag and cycle behaviour
 * matches Musashi's handlers exactly, so overrides stay bit-exact when the change is disabled. */
#ifndef RC_OVERRIDES_H
#define RC_OVERRIDES_H
#include "rc_runtime.h"
#include "gen/main_rc.h"
#include "gen/sub_rc.h"

extern int sho_wide_xo;   /* widescreen: extra pixels on each side of the original 320 (0 = 4:3) */

/* cmpi.w #imm, Dn   (8 cycles, 4 bytes) */
#define OVR_CMPI16_D(pc_next, reg, imm) do { uint src_ = (uint)(imm) & 0xffff; uint dst_ = MASK_OUT_ABOVE_16(REG_D[reg]); \
        uint res_ = dst_ - src_; FLAG_N = NFLAG_16(res_); FLAG_Z = MASK_OUT_ABOVE_16(res_); \
        FLAG_V = VFLAG_SUB_16(src_, dst_, res_); FLAG_C = CFLAG_16(res_); REG_PC = (pc_next); USE_CYCLES(8); } while (0)
#endif
