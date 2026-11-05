/* Minimal process startup for a GUI program linked directly against ucrtbase.dll
   (ucrtbase initialises its own heap/stdio when the DLL loads). */
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
void WinMainCRTStartup(void) {
    char *cmd = GetCommandLineA();
    /* skip program name */
    if (*cmd == '"') { cmd++; while (*cmd && *cmd != '"') cmd++; if (*cmd) cmd++; }
    else while (*cmd > ' ') cmd++;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    ExitProcess((UINT)WinMain(GetModuleHandleA(NULL), NULL, cmd, SW_SHOWDEFAULT));
}
/* stack probe used by clang for frames > 4KB on x86_64-w64-mingw32 */
__asm__(
  ".globl ___chkstk_ms\n"
  "___chkstk_ms:\n"
  "  pushq %rcx\n  pushq %rax\n  cmpq $0x1000, %rax\n  leaq 24(%rsp), %rcx\n  jb 2f\n"
  "1:\n  subq $0x1000, %rcx\n  orq $0, (%rcx)\n  subq $0x1000, %rax\n  cmpq $0x1000, %rax\n  ja 1b\n"
  "2:\n  subq %rax, %rcx\n  orq $0, (%rcx)\n  popq %rax\n  popq %rcx\n  ret\n");

/* stdio shims: MinGW's UCRT headers expect these from a static helper library;
   forward them to ucrtbase's __stdio_common_* entry points. */
#include <stdio.h>
#include <stdarg.h>
int __cdecl __stdio_common_vsprintf(unsigned __int64, char *, size_t, const char *, _locale_t, va_list);
int __cdecl __stdio_common_vfprintf(unsigned __int64, FILE *, const char *, _locale_t, va_list);
#define SHIM_SNPRINTF_OPTS 0x2ULL /* _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR */
int __cdecl vsnprintf(char *b, size_t n, const char *f, va_list a) { return __stdio_common_vsprintf(SHIM_SNPRINTF_OPTS, b, n, f, NULL, a); }
int __cdecl snprintf(char *b, size_t n, const char *f, ...) { va_list a; va_start(a, f); int r = vsnprintf(b, n, f, a); va_end(a); return r; }
int __cdecl sprintf(char *b, const char *f, ...) { va_list a; va_start(a, f); int r = __stdio_common_vsprintf(0, b, (size_t)-1, f, NULL, a); va_end(a); return r; }
int __cdecl vfprintf(FILE *s, const char *f, va_list a) { return __stdio_common_vfprintf(0, s, f, NULL, a); }
int __cdecl fprintf(FILE *s, const char *f, ...) { va_list a; va_start(a, f); int r = vfprintf(s, f, a); va_end(a); return r; }
int __cdecl printf(const char *f, ...) { va_list a; va_start(a, f); int r = vfprintf(__acrt_iob_func(1), f, a); va_end(a); return r; }
int __cdecl __stdio_common_vsscanf(unsigned __int64, const char *, size_t, const char *, _locale_t, va_list);
int __cdecl sscanf(const char *s, const char *f, ...) { va_list a; va_start(a, f); int r = __stdio_common_vsscanf(0, s, (size_t)-1, f, NULL, a); va_end(a); return r; }
