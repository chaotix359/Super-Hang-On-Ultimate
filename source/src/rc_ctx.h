/* Minimal stack switching for the recompiled CPUs' fibers (macOS). See rc_ctx.c. */
#ifndef RC_CTX_H
#define RC_CTX_H
#include <stddef.h>
void rc_ctx_switch(void **save_sp, void *load_sp);
void *rc_ctx_make(void *stack, size_t size, void (*fn)(int), int arg);
#endif
