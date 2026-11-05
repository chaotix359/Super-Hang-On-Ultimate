/* Runtime support for the recompiled 68000 code.
 *
 * Execution model
 *  - Each CPU runs on its own fiber. The board scheduler grants a cycle budget (rc_execute),
 *    exactly like m68k_execute(); recompiled code charges the same cycles as the interpreter
 *    after every instruction and yields back to the scheduler when the budget runs out.
 *  - CPU state lives in Musashi's register file, so recompiled code, the interpreter fallback
 *    and save/compare tools all see the same state.
 *  - 68000 subroutines are C functions. A call pushes the 68000 return address as normal; when
 *    the callee returns, the caller checks that the 68000 PC/SP match what a normal return would
 *    give. If not (the game manipulated its stack), control unwinds to the dispatcher, which can
 *    enter any instruction address.
 */
#ifndef RC_RUNTIME_H
#define RC_RUNTIME_H
#include <stdint.h>
#include "third_party/musashi/m68kcpu.h"
#include "third_party/musashi/m68kops.h"
#include "third_party/musashi/m68kops_handlers.h"

typedef void (*rc_fn)(uint32_t entry);

extern int rc_cpu;                 /* CPU currently executing: 0 main, 1 sub */
extern int rc_unwind;              /* set when control must return to the nearest dispatcher */
extern int rc_abort[2];            /* set when a CPU was reset: unwind everything */
extern int rc_depth;
extern const rc_fn M_table[0x20000], S_table[0x20000];

void rc_yield(void);               /* budget exhausted: give the slice back */
void rc_exception(uint32_t ret);   /* an exception/interrupt was taken mid-instruction */
void rc_jump_dyn(void);            /* computed jump to REG_PC */
void rc_call_dyn(uint32_t ret);    /* computed call; return address already pushed */
void rc_bad_entry(uint32_t e);
void rc_mismatch(uint32_t expected_ret);   /* callee returned somewhere unexpected */

#define RC_STOP (rc_unwind | rc_abort[rc_cpu])

/* execute one instruction with the interpreter's own handler (opcode fixed at compile time) */
#define OP(pc2, op, handler, cyc) do { REG_PC = (pc2); REG_IR = (op); handler(); USE_CYCLES(cyc); } while (0)
/* end-of-instruction budget check (matches m68k_execute's loop condition) */
#define STEP() do { if (GET_CYCLES() <= 0) { rc_yield(); if (RC_STOP) return; } } while (0)
/* instructions that may raise an exception (trap, divide by zero, SR writes, stop...) */
#define EXC_CHECK(nxt) do { if (REG_PC != (nxt)) { rc_exception(nxt); if (RC_STOP) return; } else STEP(); } while (0)

#define RC_MAX_DEPTH 200
/* call a known subroutine: check the 68000 return lands where a normal RTS would */
#define CALL(fn, target, ret) do { uint32_t sp_ = REG_A[7]; \
        if (rc_depth > RC_MAX_DEPTH) { rc_mismatch(0xdee900); return; } \
        rc_depth++; fn(target); rc_depth--; if (RC_STOP) return; \
        if (REG_PC != (ret) || REG_A[7] != sp_ + 4) { rc_mismatch(ret); return; } } while (0)
#define CALL_DYN(ret) do { rc_call_dyn(ret); if (RC_STOP) return; } while (0)
/* flow continues in another function (branch or fall-through into its entry) */
#define TAIL(fn, target) do { if (rc_depth > RC_MAX_DEPTH) { rc_mismatch(0xdee901); return; } \
        rc_depth++; fn(target); rc_depth--; return; } while (0)
#define JUMP_DYN() do { rc_jump_dyn(); return; } while (0)

/* scheduler interface */
void rc_init(void);
void rc_reset_cpu(int which);      /* CPU was reset: its fiber restarts at the new PC */
void rc_execute(int which, int cycles);

#endif
