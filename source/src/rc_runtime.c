/* Runtime for recompiled 68000 code: fibers, dispatcher, yields, exceptions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rc_runtime.h"

extern int m68ki_initial_cycles;
extern void (*m68ki_instruction_jump_table[0x10000])(void);
extern unsigned char m68ki_cycles[][0x10000];

int rc_cpu, rc_unwind, rc_abort[2], rc_depth;
unsigned long rc_stats_interp, rc_stats_dispatch, rc_stats_unwind;

/* ------------------------------------------------------------------ fibers */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LPVOID sched_fiber, cpu_fiber[2];
static void fiber_switch_to_cpu(int w) { SwitchToFiber(cpu_fiber[w]); }
static void fiber_switch_to_sched(void) { SwitchToFiber(sched_fiber); }
#elif defined(__APPLE__) || defined(RC_ASM_FIBERS)
/* macOS: our own stack switch (the system's ucontext routines are broken on Apple Silicon) */
#include "rc_ctx.h"
static void *sched_sp, *cpu_sp[2];
static void *cpu_stack[2];
static void fiber_switch_to_cpu(int w) { rc_ctx_switch(&sched_sp, cpu_sp[w]); }
static void fiber_switch_to_sched(void) { rc_ctx_switch(&cpu_sp[rc_cpu], sched_sp); }
#define RC_CTX_FIBERS 1
#else
#include <ucontext.h>
static ucontext_t sched_ctx, cpu_ctx[2];
static void *cpu_stack[2];
static void fiber_switch_to_cpu(int w) { swapcontext(&sched_ctx, &cpu_ctx[w]); }
static void fiber_switch_to_sched(void) { swapcontext(&cpu_ctx[rc_cpu], &sched_ctx); }
#endif
#define FIBER_STACK (8 << 20)

static inline const rc_fn *table(void) { return rc_cpu ? S_table : M_table; }
static inline rc_fn lookup(uint32_t pc) {
    pc &= 0xffffff;
    if (pc >= 0x40000 || (pc & 1)) return NULL;
    return table()[pc >> 1];
}

/* one instruction through the interpreter (code the recompiler never saw) */
static void interp_one(void) {
    rc_stats_interp++;
    REG_PPC = REG_PC;
    REG_IR = m68ki_read_imm_16();
    m68ki_instruction_jump_table[REG_IR]();
    USE_CYCLES(m68ki_cycles[0][REG_IR]);
    if (GET_CYCLES() <= 0) rc_yield();
}

/* Run code from REG_PC until the 68000 returns to ret_pc with SP == ret_sp (nested == 1),
 * or forever at top level (nested == 0). Returns early on CPU reset. */
static void dispatch(uint32_t ret_pc, uint32_t ret_sp, int nested) {
    int saved_depth = rc_depth;
    for (;;) {
        if (rc_abort[rc_cpu]) return;
        if (nested && REG_PC == ret_pc && REG_A[7] == ret_sp) { rc_unwind = 0; return; }
        if (rc_unwind) { rc_stats_unwind++; if (getenv("UWDBG") && rc_stats_unwind < 20) fprintf(stderr, "unwind -> cpu%d PC=%06X depth=%d\n", rc_cpu, REG_PC, rc_depth); }
        rc_unwind = 0; rc_depth = saved_depth;
        rc_fn f = lookup(REG_PC);
        if (f) { rc_stats_dispatch++; f(REG_PC & 0xffffff); }
        else interp_one();
    }
}

void rc_exception(uint32_t ret) {
    /* a group-2 exception or interrupt pushed SR+PC (6 bytes); run the handler until it returns */
    uint32_t sp = REG_A[7] + 6;
    if (GET_CYCLES() <= 0) { rc_yield(); if (rc_abort[rc_cpu]) return; }
    dispatch(ret, sp, 1);
}

void rc_yield(void) {
    for (;;) {
        uint32_t next = REG_PC;
        int my_cpu = rc_cpu, d = rc_depth, u = rc_unwind;   /* per-CPU dispatcher state */
        fiber_switch_to_sched();
        rc_cpu = my_cpu; rc_depth = d; rc_unwind = u;
        if (rc_abort[rc_cpu]) return;
        /* start of a new slice: same order as m68k_execute() */
        uint32_t sp = REG_A[7];
        m68ki_check_interrupts();
        if (REG_PC != next) {                    /* interrupt taken: run its handler first */
            int u = rc_unwind; rc_unwind = 0;
            dispatch(next, sp, 1);
            rc_unwind = u;
            if (rc_abort[rc_cpu]) return;
        }
        if (CPU_STOPPED) { SET_CYCLES(0); continue; }
        if (GET_CYCLES() > 0) return;
    }
}

void rc_jump_dyn(void) {
    rc_fn f = lookup(REG_PC);
    if (!f || rc_depth > RC_MAX_DEPTH) { rc_mismatch(f ? 0xdee902 : 0xdee903); return; }
    rc_depth++; f(REG_PC & 0xffffff); rc_depth--;
}
void rc_call_dyn(uint32_t ret) {
    uint32_t sp = REG_A[7];
    rc_fn f = lookup(REG_PC);
    if (!f || rc_depth > RC_MAX_DEPTH) { rc_unwind = 1; return; }
    rc_depth++; f(REG_PC & 0xffffff); rc_depth--;
    if (RC_STOP) return;
    if (REG_PC != ret || REG_A[7] != sp + 4) rc_mismatch(ret);
}
void rc_bad_entry(uint32_t e) { rc_mismatch(0xbad000 | (e & 0xfff)); }
uint32_t rc_mm_exp[64], rc_mm_got[64]; unsigned long rc_mm_n[64];
void rc_mismatch(uint32_t expected_ret) {
    rc_unwind = 1;
    uint32_t key = (expected_ret * 2654435761u) >> 26;
    if (rc_mm_n[key] == 0 || (rc_mm_exp[key] == expected_ret)) { rc_mm_exp[key] = expected_ret | (rc_cpu << 31); rc_mm_got[key] = REG_PC; rc_mm_n[key]++; }
}

/* ------------------------------------------------------------------ fiber entry */
static void cpu_main(int which) {
    for (;;) {
        rc_abort[which] = 0; rc_unwind = 0; rc_depth = 0;
        /* fiber (re)start happens at the beginning of a slice */
        m68ki_check_interrupts();
        while (CPU_STOPPED) {
            SET_CYCLES(0); rc_yield(); if (rc_abort[which]) break;
        }
        if (rc_abort[which]) continue;
        dispatch(0, 0, 0);
    }
}

#ifdef _WIN32
static void WINAPI fiber_proc(LPVOID p) { cpu_main((int)(intptr_t)p); }
#elif defined(RC_CTX_FIBERS)
static void fiber_proc(int w) { cpu_main(w); }
#else
static void fiber_proc(int w) { cpu_main(w); }
#endif

static int started[2];
void rc_init(void) {
#ifdef _WIN32
    if (!sched_fiber) {
        sched_fiber = ConvertThreadToFiber(NULL);
        if (!sched_fiber) sched_fiber = GetCurrentFiber();
    }
    for (int w = 0; w < 2; w++) {
        if (cpu_fiber[w]) DeleteFiber(cpu_fiber[w]);
        cpu_fiber[w] = CreateFiber(FIBER_STACK, fiber_proc, (LPVOID)(intptr_t)w);
    }
#elif defined(RC_CTX_FIBERS)
    for (int w = 0; w < 2; w++) {
        if (!cpu_stack[w]) cpu_stack[w] = malloc(FIBER_STACK);
        cpu_sp[w] = rc_ctx_make(cpu_stack[w], FIBER_STACK, fiber_proc, w);
    }
#else
    for (int w = 0; w < 2; w++) {
        if (!cpu_stack[w]) cpu_stack[w] = malloc(FIBER_STACK);
        getcontext(&cpu_ctx[w]);
        cpu_ctx[w].uc_stack.ss_sp = cpu_stack[w];
        cpu_ctx[w].uc_stack.ss_size = FIBER_STACK;
        cpu_ctx[w].uc_link = NULL;
        makecontext(&cpu_ctx[w], (void (*)(void))fiber_proc, 1, w);
    }
#endif
    started[0] = started[1] = 0;
    rc_abort[0] = rc_abort[1] = 0;
}

void rc_reset_cpu(int which) { if (started[which]) rc_abort[which] = 1; }

void rc_execute(int which, int n) {
    /* mirrors m68k_execute(): reset cycles, budget, then run until the budget is used */
    if (RESET_CYCLES) {
        int rc = RESET_CYCLES; RESET_CYCLES = 0;
        n -= rc; if (n <= 0) return;
    }
    SET_CYCLES(n);
    m68ki_initial_cycles = n;
    rc_cpu = which;
    started[which] = 1;
    fiber_switch_to_cpu(which);
}
