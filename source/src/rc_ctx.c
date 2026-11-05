/* Minimal stack switching for the recompiled CPUs' fibers (used on macOS, where the system's
 * makecontext/swapcontext do not work on Apple Silicon; RC_ASM_FIBERS selects it elsewhere for testing).
 *
 *   rc_ctx_switch(&save, load): saves the callee-saved registers on the current stack, stores the stack
 *   pointer in *save, switches to the stack `load` and restores that context's registers. To the code that
 *   called it, it returns when some other context switches back.
 *   rc_ctx_make(): prepares a new stack so that the first switch to it calls fn(arg). fn must never return.
 *
 * Saved per switch: arm64 (AAPCS64) x19-x28, fp, lr, d8-d15; x86-64 (System V) rbx, rbp, r12-r15, MXCSR and
 * the x87 control word. That is everything a C function must preserve across a call, so the switch looks
 * like an ordinary function call to the compiler.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "rc_ctx.h"

#if defined(__APPLE__)
#define SYM(n) "_" #n
#else
#define SYM(n) #n
#endif

#if defined(__aarch64__) || defined(__arm64__)

__asm__(
    ".text\n"
    ".p2align 2\n"
    ".globl " SYM(rc_ctx_switch) "\n"
    SYM(rc_ctx_switch) ":\n"
    "    sub  sp, sp, #176\n"
    "    stp  x19, x20, [sp, #0]\n"
    "    stp  x21, x22, [sp, #16]\n"
    "    stp  x23, x24, [sp, #32]\n"
    "    stp  x25, x26, [sp, #48]\n"
    "    stp  x27, x28, [sp, #64]\n"
    "    stp  x29, x30, [sp, #80]\n"
    "    stp  d8,  d9,  [sp, #96]\n"
    "    stp  d10, d11, [sp, #112]\n"
    "    stp  d12, d13, [sp, #128]\n"
    "    stp  d14, d15, [sp, #144]\n"
    "    mov  x9, sp\n"
    "    str  x9, [x0]\n"
    "    mov  sp, x1\n"
    "    ldp  x19, x20, [sp, #0]\n"
    "    ldp  x21, x22, [sp, #16]\n"
    "    ldp  x23, x24, [sp, #32]\n"
    "    ldp  x25, x26, [sp, #48]\n"
    "    ldp  x27, x28, [sp, #64]\n"
    "    ldp  x29, x30, [sp, #80]\n"
    "    ldp  d8,  d9,  [sp, #96]\n"
    "    ldp  d10, d11, [sp, #112]\n"
    "    ldp  d12, d13, [sp, #128]\n"
    "    ldp  d14, d15, [sp, #144]\n"
    "    add  sp, sp, #176\n"
    "    ret\n"
    ".p2align 2\n"
    ".globl " SYM(rc_ctx_entry) "\n"
    SYM(rc_ctx_entry) ":\n"           /* first switch to a new context lands here: x19 = fn, x20 = arg */
    "    mov  x0, x20\n"
    "    blr  x19\n"
    "    brk  #0\n"                    /* fn returned: not allowed */
);
#define FRAME_BYTES 176

void rc_ctx_entry(void);
void *rc_ctx_make(void *stack, size_t size, void (*fn)(int), int arg) {
    uintptr_t top = ((uintptr_t)stack + size) & ~(uintptr_t)15;
    uint64_t *f = (uint64_t *)(top - FRAME_BYTES);          /* the frame rc_ctx_switch pops */
    memset(f, 0, FRAME_BYTES);
    f[0] = (uint64_t)(uintptr_t)fn;                         /* x19 */
    f[1] = (uint64_t)(uintptr_t)(intptr_t)arg;              /* x20 */
    f[10] = 0;                                              /* x29: end of the frame chain */
    f[11] = (uint64_t)(uintptr_t)rc_ctx_entry;              /* x30: where `ret` goes */
    return f;
}

#elif defined(__x86_64__)

__asm__(
    ".text\n"
    ".p2align 4\n"
    ".globl " SYM(rc_ctx_switch) "\n"
    SYM(rc_ctx_switch) ":\n"           /* rdi = void **save, rsi = void *load */
    "    pushq %rbp\n"
    "    pushq %rbx\n"
    "    pushq %r12\n"
    "    pushq %r13\n"
    "    pushq %r14\n"
    "    pushq %r15\n"
    "    subq  $8, %rsp\n"
    "    stmxcsr (%rsp)\n"
    "    fnstcw 4(%rsp)\n"
    "    movq  %rsp, (%rdi)\n"
    "    movq  %rsi, %rsp\n"
    "    ldmxcsr (%rsp)\n"
    "    fldcw 4(%rsp)\n"
    "    addq  $8, %rsp\n"
    "    popq  %r15\n"
    "    popq  %r14\n"
    "    popq  %r13\n"
    "    popq  %r12\n"
    "    popq  %rbx\n"
    "    popq  %rbp\n"
    "    ret\n"
    ".p2align 4\n"
    ".globl " SYM(rc_ctx_entry) "\n"
    SYM(rc_ctx_entry) ":\n"           /* first switch to a new context lands here: r12 = fn, r13 = arg */
    "    movq  %r13, %rdi\n"
    "    callq *%r12\n"
    "    ud2\n"                        /* fn returned: not allowed */
);

void rc_ctx_entry(void);
void *rc_ctx_make(void *stack, size_t size, void (*fn)(int), int arg) {
    /* frame, lowest address first: MXCSR + x87 CW, r15, r14, r13, r12, rbx, rbp, return address.
       After `ret` pops the return address the stack pointer is 16-byte aligned, as at a call site. */
    uintptr_t top = ((uintptr_t)stack + size) & ~(uintptr_t)15;
    uint64_t *f = (uint64_t *)(top - 8 * 8);
    memset(f, 0, 8 * 8);
    uint32_t mxcsr = 0x1F80; uint16_t fcw = 0x037F;        /* power-on defaults */
    memcpy((char *)f, &mxcsr, 4); memcpy((char *)f + 4, &fcw, 2);
    f[4] = (uint64_t)(uintptr_t)fn;                         /* r12 */
    f[3] = (uint64_t)(intptr_t)arg;                         /* r13 */
    f[7] = (uint64_t)(uintptr_t)rc_ctx_entry;               /* return address */
    return f;
}

#else
#error "rc_ctx.c: no stack switch for this CPU (use the ucontext build)"
#endif
