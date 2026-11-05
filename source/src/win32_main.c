/* Super Hang-On – native Windows frontend.
 *  - Recompiled game code (C) by default; the original-CPU interpreter remains selectable
 *  - 16:9 extended view (default) or original 4:3; F9 toggles
 *  - Direct3D 11 presentation: vsync-locked smooth pacing, sharp-bilinear scaling; GDI fallback
 *  - Borderless fullscreen (default) / windowed: Alt+Enter or F11; Alt+F4 quits
 *  - XInput (Xbox-style) gamepads + keyboard; waveOut audio with drift correction
 *  - Battery-backed RAM (high scores) saved to shangon.nv; settings in shangon.ini
 *  - Optional Mega Drive Enhancements (Original Mode, saved to shangon_original.sav) and Mega Drive controls
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sho_core.h"
#include "win32_d3d11.h"
#include "win32_launcher.h"
#include "sho_music.h"
#include "sho_md.h"
#include "sho_mdgfx.h"

extern int sho_recomp;

#define AUDIO_RATE 48000
#define NUM_AUDIO_BUFS 32
#define AUDIO_TARGET 3200            /* samples queued ahead (~67 ms) */

static HWND g_hwnd;
static int g_md_present, g_pause_menu;
static void reset_to_title(void);
static void apply_sound_levels(int save);
static int g_fullscreen = 1, g_running = 1, g_paused = 0, g_filter = 2, g_stretch = 0, g_wide = 1, g_vrr = 0, g_use_d3d = 0;
static WINDOWPLACEMENT g_wp = { sizeof(WINDOWPLACEMENT) };
static BITMAPINFO g_bmi;
static char g_dir[MAX_PATH], g_ini[MAX_PATH], g_nv[MAX_PATH];
static const char *filter_names[3] = { "sharp", "smooth", "sharp-bilinear" };

/* input */
static int g_keys[256];
static float g_kb_steer, g_deadzone = 0.15f, g_steer_curve = 1.0f;
static int g_swap_pedals = 0;
typedef DWORD (WINAPI *XInputGetState_t)(DWORD, XINPUT_STATE *);
static XInputGetState_t pXInputGetState;
static int g_pad_poll_cooldown[4];

/* audio */
static HWAVEOUT g_wo;
static WAVEHDR g_hdr[NUM_AUDIO_BUFS];
static int16_t g_abuf[NUM_AUDIO_BUFS][2 * 1100];
static int g_next_buf;
static unsigned long long g_written;   /* samples submitted */

/* ------------------------------------------------------------------ */
static void path_join(char *out, const char *dir, const char *name) { snprintf(out, MAX_PATH, "%s\\%s", dir, name); }
static void load_nvram(void) {
    FILE *f = fopen(g_nv, "rb"); if (!f) return; size_t n = fread(sho_nvram(), 1, sho_nvram_size(), f); (void)n; fclose(f);
    /* credits left over from the last session are not kept: with credits in hand the arcade skips its
       title/attract screens and goes straight to the class-select map (work RAM $66052-$66053) */
    sho_nvram()[0x6052] = 0; sho_nvram()[0x6053] = 0;
}
static void save_nvram(void) { FILE *f = fopen(g_nv, "wb"); if (!f) return; fwrite(sho_nvram(), 1, sho_nvram_size(), f); fclose(f); }

static void write_default_ini(void) {
    if (GetFileAttributesA(g_ini) != INVALID_FILE_ATTRIBUTES) return;
    FILE *f = fopen(g_ini, "w"); if (!f) return;
    fputs("; Super Hang-On Ultimate settings\n"
          "[video]\n"
          "fullscreen=1      ; start in fullscreen (Alt+Enter / F11 toggles)\n"
          "widescreen=1      ; 1 = 16:9 extended view, 0 = original 4:3 (F9 toggles)\n"
          "filter=2          ; 0 sharp, 1 smooth, 2 sharp-bilinear (F8 cycles)\n"
          "stretch=0         ; 1 = stretch to fill the screen (distorts the picture)\n"
          "renderer=d3d11    ; d3d11 or gdi\n"
          "vrr=0             ; 1 = for G-Sync/FreeSync displays: present at the arcade rate with tearing allowed\n"
          "[input]\n"
          "deadzone=15       ; left stick dead zone in percent\n"
          "steer_curve=100   ; 100 = linear; higher = finer control near centre\n"
          "swap_pedals=0     ; 1 = left trigger accelerates, right trigger brakes\n"
          "[audio]\n"
          "external_music=1  ; use MP3s from the music folder\n"
          "music_volume=55   ; replacement music volume, 0..100\n"
          "music_level_regular=10    ; pause menu SOUND levels, 0..10, per soundtrack\n"
          "effects_level_regular=10\n"
          "music_level_arrange=6\n"
          "effects_level_arrange=10\n"
          "music_level_megadrive=10\n"
          "effects_level_megadrive=10\n"
          "[game]\n"
          "demo_sounds=1     ; sound during attract mode\n"
          "difficulty=1      ; 0 easy, 1 normal, 2 hard, 3 hardest\n"
          "time=1            ; time limit: 0 easy, 1 normal, 2 hard, 3 hardest\n"
          "free_play=0       ; 1 = no coins needed (press Start)\n"
          "[megadrive]\n"
          "enhancements=0    ; 1 = Original Mode (press Start on the title screen to choose it)\n"
          "controls=0        ; 1 = Mega Drive steering response (virtual handlebar)\n"
          "music=0           ; 1 = Mega Drive soundtrack throughout the game\n"
          "[launcher]\n"
          "show=1            ; 1 = show the settings launcher first (or start with -play to skip it)\n"
          "[advanced]\n"
          "cpu=recompiled    ; recompiled (C version) or interpreter (original CPU emulation)\n", f);
    fclose(f);
}
static int ini_int(const char *sec, const char *key, int def) { return (int)GetPrivateProfileIntA(sec, key, def, g_ini); }
static void ini_str(const char *sec, const char *key, const char *def, char *out, int n) { GetPrivateProfileStringA(sec, key, def, out, (DWORD)n, g_ini); char *c = strchr(out, ';'); if (c) *c = 0; while (*out && out[strlen(out) - 1] == ' ') out[strlen(out) - 1] = 0; }
static void ini_set_int(const char *sec, const char *key, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); WritePrivateProfileStringA(sec, key, b, g_ini); }

static void apply_game_settings(void) {
    int demo = ini_int("game", "demo_sounds", 1), diff = ini_int("game", "difficulty", 1), tim = ini_int("game", "time", 1);
    int free_play = ini_int("game", "free_play", 0);
    static const uint8_t dmap[4] = { 0x04, 0x06, 0x02, 0x00 }, tmap[4] = { 0x10, 0x18, 0x08, 0x00 };
    uint8_t dsw = 0xe0 | (demo ? 0 : 1) | dmap[diff & 3] | tmap[tim & 3];
    sho_set_dips(free_play ? 0x00 : 0xff, dsw);   /* coinage: both nibbles 0 = free play */
    sho_set_widescreen(g_wide);
}

static void update_title(void) {
    char t[160];
    snprintf(t, sizeof t, "Super Hang-On Ultimate  [%s, %s, %s%s%s%s]", g_wide ? "16:9" : "4:3", filter_names[g_filter],
             g_use_d3d ? "Direct3D 11" : "GDI", sho_recomp ? ", C" : ", interpreter",
             g_md_present && ini_int("megadrive", "enhancements", 0) ? ", MD Enhancements" : "", g_md_present && ini_int("megadrive", "controls", 0) ? ", MD Controls" : "", g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0) ? ", MD Music" : "");
    SetWindowTextA(g_hwnd, t);
}

/* ------------------------------------------------------------------ */
/* display                                                            */
/* ------------------------------------------------------------------ */
static void set_fullscreen(int on) {
    DWORD style = GetWindowLongA(g_hwnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi = { sizeof mi };
        GetWindowPlacement(g_hwnd, &g_wp);
        GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_wp);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        while (ShowCursor(TRUE) < 0) {}
    }
    g_fullscreen = on;
    InvalidateRect(g_hwnd, NULL, TRUE);
}

/* display aspect of the game image: the arcade monitor is 4:3 for 320 pixels; the 16:9 view keeps that pixel shape */
static double image_aspect(void) { return g_stretch ? 0.0 : (4.0 / 3.0) * sho_width() / (double)SHO_W; }

static void present_gdi(HDC dc) {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int cw = rc.right, ch = rc.bottom; if (cw <= 0 || ch <= 0) return;
    int w = cw, h = ch, x = 0, y = 0;
    double a = image_aspect();
    if (a > 0) { if (cw > ch * a) { w = (int)(ch * a); x = (cw - w) / 2; } else { h = (int)(cw / a); y = (ch - h) / 2; } }
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (x > 0) { RECT l = { 0, 0, x, ch }, r = { x + w, 0, cw, ch }; FillRect(dc, &l, black); FillRect(dc, &r, black); }
    if (y > 0) { RECT t = { 0, 0, cw, y }, b = { 0, y + h, cw, ch }; FillRect(dc, &t, black); FillRect(dc, &b, black); }
    SetStretchBltMode(dc, g_filter == 1 ? HALFTONE : COLORONCOLOR);
    if (g_filter == 1) SetBrushOrgEx(dc, 0, 0, NULL);
    g_bmi.bmiHeader.biWidth = sho_width();
    StretchDIBits(dc, x, y, w, h, 0, 0, sho_width(), SHO_H, sho_framebuffer(), &g_bmi, DIB_RGB_COLORS, SRCCOPY);
}

static int present(int sync_interval) {     /* returns 1 if the present waited for vblank */
    if (g_use_d3d) {
        int r = d3d_present(sho_framebuffer(), sho_width(), SHO_H, image_aspect(), g_filter, sync_interval, g_vrr);
        if (r == 0) { g_use_d3d = 0; d3d_shutdown(); update_title(); }   /* device lost etc.: fall back */
        else return r == 1 && sync_interval > 0;
    }
    HDC dc = GetDC(g_hwnd); present_gdi(dc); ReleaseDC(g_hwnd, dc);
    return 0;
}

static double monitor_refresh(void) {
    MONITORINFOEXA mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi);
    DEVMODEA dm; memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
        int hz = (int)dm.dmDisplayFrequency;
        if (hz % 60 == 59) return (hz + 1) * 1000.0 / 1001.0;    /* "59" = 59.94 Hz, "119" = 119.88 Hz ... */
        return hz;
    }
    return 60.0;
}

/* ------------------------------------------------------------------ */
/* window procedure                                                   */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CLOSE: g_running = 0; DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN && (l & (1 << 29))) { if (!(l & (1 << 30))) set_fullscreen(!g_fullscreen); return 0; }   /* Alt+Enter */
        break;   /* everything else (incl. Alt+F4) goes to DefWindowProc */
    case WM_SYSCHAR: if (w == VK_RETURN) return 0; break;
    case WM_KEYDOWN:
        if (w < 256) g_keys[w] = 1;
        if (l & (1 << 30)) return 0;   /* auto-repeat */
        if (w == VK_F11) set_fullscreen(!g_fullscreen);
        if (w == VK_ESCAPE && g_fullscreen) set_fullscreen(0);
        if (w == 'P') {   /* in Mega Drive mode during a race this opens the pause menu */
            if (!g_paused) { g_paused = 1; g_pause_menu = sho_pause_begin(); }
            else { if (g_pause_menu) { sho_pause_end(); apply_sound_levels(1); } g_paused = 0; g_pause_menu = 0; }
        }
        if (w == '8' && (GetKeyState(VK_CONTROL) & 0x8000)) sho_md_cheat_upgrade();
        if (w == '9' && (GetKeyState(VK_CONTROL) & 0x8000)) sho_md_cheat_money();   /* development: maximum money */   /* development: fully upgraded bike */
        if (w >= '0' && w <= '7' && (GetKeyState(VK_CONTROL) & 0x8000)) { sho_md_dev_race(w - '0'); ini_set_int("dev", "original_race", w - '0'); }   /* development: Original Mode race select */
        if (w == VK_F3) reset_to_title();
        if (w == VK_F9) { g_wide = !g_wide; sho_set_widescreen(g_wide); ini_set_int("video", "widescreen", g_wide); update_title(); InvalidateRect(h, NULL, TRUE); }
        if (w == VK_F8) { g_filter = (g_filter + 1) % 3; ini_set_int("video", "filter", g_filter); update_title(); }
        return 0;
    case WM_KEYUP: if (w < 256) g_keys[w] = 0; return 0;
    case WM_KILLFOCUS: memset(g_keys, 0, sizeof g_keys); return 0;
    case WM_SIZE: if (g_use_d3d) d3d_resize(); return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); if (!g_use_d3d) present_gdi(dc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: if (g_fullscreen && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; } break;
    }
    return DefWindowProcA(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */
static float axis_dz(float v) {
    float a = fabsf(v);
    if (a < g_deadzone) return 0.0f;
    a = (a - g_deadzone) / (1.0f - g_deadzone); if (a > 1) a = 1;
    a = powf(a, g_steer_curve);
    return v < 0 ? -a : a;
}
static void read_input(sho_input *in, float dt) {
    memset(in, 0, sizeof *in);
    int focused = GetForegroundWindow() == g_hwnd;
    int kl = focused && (g_keys[VK_LEFT] || g_keys['A']), kr = focused && (g_keys[VK_RIGHT] || g_keys['D']);
    float target = (float)(kr - kl), rate = (target == 0.0f) ? 6.0f : 3.5f;   /* keyboard: handlebar ramps */
    if (g_kb_steer < target) { g_kb_steer += rate * dt; if (g_kb_steer > target) g_kb_steer = target; }
    else if (g_kb_steer > target) { g_kb_steer -= rate * dt; if (g_kb_steer < target) g_kb_steer = target; }
    float steer = g_kb_steer, thr = 0, brk = 0;
    if (focused && (sho_md_overlay_active() || g_pause_menu)) {   /* Original Mode screens: arrows move, Enter/Z/Space select, Backspace/X back */
        in->nav_up = g_keys[VK_UP]; in->nav_down = g_keys[VK_DOWN]; in->nav_left = g_keys[VK_LEFT]; in->nav_right = g_keys[VK_RIGHT];
        in->pad_a = g_keys['Z'] || g_keys[VK_SPACE]; in->pad_b = g_keys[VK_BACK] || g_keys['X'];
    }
    if (focused) {
        if (g_keys[VK_UP] || g_keys['Z'] || g_keys[VK_CONTROL] || g_keys['W']) thr = 1;
        if (g_keys[VK_DOWN] || g_keys['X'] || g_keys['S']) brk = 1;
        in->turbo = g_keys[VK_SPACE] || g_keys[VK_SHIFT];
        in->coin = g_keys['5'] || g_keys['C'];
        in->start = g_keys['1'] || g_keys[VK_RETURN];
        in->test = g_keys[VK_F2];
        in->service = g_keys['9'];
    }
    if (pXInputGetState) {
        for (DWORD i = 0; i < 4; i++) {
            if (g_pad_poll_cooldown[i] > 0) { g_pad_poll_cooldown[i]--; continue; }
            XINPUT_STATE st; ZeroMemory(&st, sizeof st);
            if (pXInputGetState(i, &st) != ERROR_SUCCESS) { g_pad_poll_cooldown[i] = 120; continue; }
            XINPUT_GAMEPAD *g = &st.Gamepad;
            float sx = axis_dz(g->sThumbLX / 32767.0f);
            if (g->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) sx = -1;
            if (g->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) sx = 1;
            if (fabsf(sx) > fabsf(steer)) steer = sx;
            float rt = g->bRightTrigger / 255.0f, lt = g->bLeftTrigger / 255.0f;
            if (g_swap_pedals) { float t = rt; rt = lt; lt = t; }
            if (rt < 0.04f) rt = 0;
            if (lt < 0.04f) lt = 0;
            if (g->wButtons & XINPUT_GAMEPAD_A) in->pad_a = 1;               /* throttle, or enter on initials */
            /* directions for menus / initials: d-pad or left stick (with hysteresis) */
            { static int stick_dir[4][4];   /* [pad][up,down,left,right] */
              float lx = g->sThumbLX / 32767.0f, ly = g->sThumbLY / 32767.0f;
              float v[4] = { ly, -ly, -lx, lx };
              for (int d = 0; d < 4; d++) stick_dir[i][d] = stick_dir[i][d] ? v[d] > 0.35f : v[d] > 0.6f;
              if ((g->wButtons & XINPUT_GAMEPAD_DPAD_UP) || stick_dir[i][0]) in->nav_up = 1;
              if ((g->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || stick_dir[i][1]) in->nav_down = 1;
              if ((g->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || stick_dir[i][2]) in->nav_left = 1;
              if ((g->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || stick_dir[i][3]) in->nav_right = 1; }
            if (g->wButtons & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X)) lt = 1;
            if (g->wButtons & XINPUT_GAMEPAD_B) in->pad_b = 1;               /* back on the Original Mode screens */
            if (rt > thr) thr = rt;
            if (lt > brk) brk = lt;
            if (g->wButtons & (XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_Y)) in->turbo = 1;
            if (g->wButtons & XINPUT_GAMEPAD_START) in->start = 1;
            if (g->wButtons & XINPUT_GAMEPAD_BACK) in->coin = 1;
        }
    }
    in->steer = steer; in->throttle = thr; in->brake = brk;
}

/* ------------------------------------------------------------------ */
/* audio                                                              */
/* ------------------------------------------------------------------ */
static int audio_init(void) {
    WAVEFORMATEX wf; memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = AUDIO_RATE;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = AUDIO_RATE * 4;
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { g_wo = NULL; return 0; }
    for (int i = 0; i < NUM_AUDIO_BUFS; i++) g_hdr[i].dwFlags = WHDR_DONE;
    return 1;
}
static long audio_queued(void) {   /* samples written but not yet played */
    MMTIME t; t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(g_wo, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES) return AUDIO_TARGET;
    long q = (long)(g_written - (unsigned long long)t.u.sample);
    return q < 0 ? 0 : q;
}
static void audio_submit(int16_t *samples, int count) {
    WAVEHDR *h = &g_hdr[g_next_buf];
    if (!(h->dwFlags & WHDR_DONE)) return;
    if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wo, h, sizeof *h);
    memcpy(g_abuf[g_next_buf], samples, (size_t)count * 4);
    h->lpData = (LPSTR)g_abuf[g_next_buf]; h->dwBufferLength = (DWORD)count * 4; h->dwFlags = 0;
    waveOutPrepareHeader(g_wo, h, sizeof *h);
    waveOutWrite(g_wo, h, sizeof *h);
    g_written += (unsigned long long)count;
    g_next_buf = (g_next_buf + 1) % NUM_AUDIO_BUFS;
}

/* ------------------------------------------------------------------ */
static void enable_dpi_awareness(void) {
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef BOOL (WINAPI *ctx_t)(HANDLE);
    ctx_t f = u ? (ctx_t)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (!f || !f((HANDLE)-4)) SetProcessDPIAware();
}
static void load_xinput(void) {
    const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !pXInputGetState; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (m) pXInputGetState = (XInputGetState_t)(void *)GetProcAddress(m, "XInputGetState");
    }
}
static double now_s(void) { static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }
static void wait_until(double t) {   /* sleep most of the way, then spin for precision */
    for (;;) { double d = t - now_s(); if (d <= 0) return; if (d > 0.002) Sleep((DWORD)((d - 0.0015) * 1000)); }
}

/* Verify and load the ROM set (command-line path, then shangon.zip / folders next to the exe).
 * Used by the launcher's status line and before the game starts. */
static char g_cmd_rom[MAX_PATH], g_rom_err[2048];
static int rom_check(char *msg, int msglen, char *detail, int detlen) {
    const char *candidates[] = { "shangon.zip", "roms\\shangon.zip", "shangon", "roms\\shangon" };
    char path[MAX_PATH], err[2048], first_err[2048] = "", first_path[MAX_PATH] = "";
    for (int i = -1; i < 4; i++) {
        if (i < 0) { if (!g_cmd_rom[0]) continue; snprintf(path, sizeof path, "%s", g_cmd_rom); }
        else path_join(path, g_dir, candidates[i]);
        int exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
        if (exists && sho_load_roms(path, err, sizeof err) == 0) {
            const char *name = strrchr(path, '\\'); name = name ? name + 1 : path;
            snprintf(msg, msglen, "ROM OK: %s verified", name);
            snprintf(detail, detlen, "All 25 files match Super Hang-On (MAME 'shangon')");
            g_rom_err[0] = 0;
            return 1;
        }
        if (exists && !first_err[0]) { snprintf(first_err, sizeof first_err, "%s", err); snprintf(first_path, sizeof first_path, "%s", path); }
    }
    if (!first_err[0]) {
        snprintf(msg, msglen, "ROM missing: shangon.zip not found");
        snprintf(detail, detlen, "Put shangon.zip in the same folder as SuperHangOn.exe");
        snprintf(g_rom_err, sizeof g_rom_err, "Looked in: %s", g_dir);
        return 0;
    }
    /* the file exists but is wrong: count problems from the loader's report */
    int missing = 0, bad = 0; const char *firstline = NULL;
    for (const char *p = first_err; (p = strchr(p, '(')) != NULL; p++) {
        if (!strncmp(p, "(missing)", 9)) missing++;
        else if (!strncmp(p, "(bad checksum)", 14)) bad++;
    }
    firstline = strstr(first_err, "\n\n"); firstline = firstline ? firstline + 2 : first_err;
    const char *name = strrchr(first_path, '\\'); name = name ? name + 1 : first_path;
    if (missing == 25) snprintf(msg, msglen, "ROM problem: %s is not the Super Hang-On set", name);
    else snprintf(msg, msglen, "ROM problem: %s - %d missing, %d wrong", name, missing, bad);
    char line[128]; snprintf(line, sizeof line, "%s", firstline); char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    snprintf(detail, detlen, "First issue: %s", line);
    snprintf(g_rom_err, sizeof g_rom_err, "%s", first_err);
    return 0;
}

/* Mega Drive ROM for the Original Mode visuals: [megadrive] rom= in the ini, else any .md/.gen/.bin/.zip
   next to the exe that is Super Hang-On (World) (Rev A). Returns 1 and the path if found. */
static int find_md_rom(char *out, size_t n) {
    char p[MAX_PATH], msg[128];
    ini_str("megadrive", "rom", "", p, sizeof p);
    if (p[0]) { char full[MAX_PATH]; if (!strchr(p, ':') && p[0] != '\\') { path_join(full, g_dir, p); snprintf(p, sizeof p, "%s", full); }
                if (mdgfx_check(p, msg, sizeof msg)) { snprintf(out, n, "%s", p); return 1; } }
    static const char *pats[] = { "*.md", "*.gen", "*.bin", "*.zip" };
    for (int i = 0; i < 4; i++) {
        char pat[MAX_PATH]; path_join(pat, g_dir, pats[i]);
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!_stricmp(fd.cFileName, "shangon.zip") || fd.nFileSizeLow > 8 * 1024 * 1024) continue;
            path_join(p, g_dir, fd.cFileName);
            if (mdgfx_check(p, msg, sizeof msg)) { FindClose(h); snprintf(out, n, "%s", p); return 1; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return 0;
}

/* back to the title screen (F3, or EXIT on the pause menu) */
static void reset_to_title(void) {
    sho_md_before_reset();
    save_nvram(); sho_reset(); load_nvram(); apply_game_settings();
    sho_md_configure(g_md_present && ini_int("megadrive", "enhancements", 0), g_md_present && ini_int("megadrive", "controls", 0));
}
/* pause menu sound levels (0-10): music scales the arranged soundtrack's volume and the FM music,
   effects the sound effects; kept in the ini */
/* the levels are kept per soundtrack: Regular and Mega Drive Music start at music 10 / effects 10, the Arrange
   Soundtrack at music 6 / effects 10 */
static int sound_mode(void) {   /* 0 regular, 1 arrange, 2 Mega Drive music */
    if (g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0)) return 2;
    return ini_int("audio", "external_music", 1) ? 1 : 0;
}
static const char *const level_keys[3][2] = { { "music_level_regular", "effects_level_regular" }, { "music_level_arrange", "effects_level_arrange" }, { "music_level_megadrive", "effects_level_megadrive" } };
static void load_sound_levels(void) {
    int md = sound_mode();
    sho_set_sound_levels(ini_int("audio", level_keys[md][0], md == 1 ? 6 : 10), ini_int("audio", level_keys[md][1], 10));
}
static void apply_sound_levels(int save) {
    int m, e; sho_get_sound_levels(&m, &e);
    sho_music_set_volume(ini_int("audio", "music_volume", 55) * m / 10);
    if (save) { int md = sound_mode(); ini_set_int("audio", level_keys[md][0], m); ini_set_int("audio", level_keys[md][1], e); }
}

/* launcher status lines for the Mega Drive ROM */
static int md_rom_check(char *msg, int msglen, char *detail, int detlen) {
    char p[MAX_PATH], info[128] = "";
    if (find_md_rom(p, sizeof p)) {
        const char *nm = strrchr(p, '\\'); nm = nm ? nm + 1 : p;
        mdgfx_check(p, info, sizeof info);
        snprintf(msg, msglen, "Mega Drive ROM OK: %s verified", nm);
        snprintf(detail, detlen, "Super Hang-On (World) - Mega Drive options available");
        return 1;
    }
    snprintf(msg, msglen, "No Mega Drive ROM found");
    snprintf(detail, detlen, "Add Super Hang-On (World) Mega Drive ROM to enable its options");
    return 0;
}
static int md_rom_present(void) { char p[MAX_PATH]; return find_md_rom(p, sizeof p); }

/* Mega Drive capture runs on a worker thread while the launcher is open (the arcade has not booted yet,
   so the shared 68000 core is free) */
static char g_md_path[MAX_PATH]; static HANDLE g_md_thread; static int g_md_ok;
static DWORD WINAPI md_capture_thread(LPVOID p) { char err[256]; (void)p; g_md_ok = mdgfx_capture(g_md_path, err, sizeof err); return 0; }

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)prev; (void)show;
    enable_dpi_awareness();
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    char *slash = strrchr(g_dir, '\\'); if (slash) *slash = 0;
    path_join(g_ini, g_dir, "shangon.ini"); path_join(g_nv, g_dir, "shangon.nv");
    write_default_ini();
    char s[64];
    g_fullscreen = ini_int("video", "fullscreen", 1);
    g_wide = ini_int("video", "widescreen", 1);
    g_filter = ini_int("video", "filter", 2) % 3;
    g_stretch = ini_int("video", "stretch", 0);
    g_vrr = ini_int("video", "vrr", 0);
    ini_str("video", "renderer", "d3d11", s, sizeof s); int want_d3d = _stricmp(s, "gdi") != 0;
    g_deadzone = ini_int("input", "deadzone", 15) / 100.0f;
    g_steer_curve = ini_int("input", "steer_curve", 100) / 100.0f;
    g_swap_pedals = ini_int("input", "swap_pedals", 0);
    ini_str("advanced", "cpu", "recompiled", s, sizeof s); sho_recomp = _stricmp(s, "interpreter") != 0;
    if (strstr(cmd, "-window")) g_fullscreen = 0;
    if (strstr(cmd, "-fullscreen")) g_fullscreen = 1;
    if (strstr(cmd, "-interpreter")) sho_recomp = 0;
    if (strstr(cmd, "-gdi")) want_d3d = 0;

    /* ROM set: command line path, then next to the exe */
    if (cmd && *cmd && *cmd != '-') {
        snprintf(g_cmd_rom, sizeof g_cmd_rom, "%s", cmd);
        char *e = g_cmd_rom + strlen(g_cmd_rom); while (e > g_cmd_rom && (e[-1] == ' ' || e[-1] == '"')) *--e = 0;
        if (g_cmd_rom[0] == '"') memmove(g_cmd_rom, g_cmd_rom + 1, strlen(g_cmd_rom));
    }
    int show_launcher = ini_int("launcher", "show", 1) && !strstr(cmd, "-play");
    if (show_launcher) {
        launcher_settings ls;
        ls.demo_sounds = ini_int("game", "demo_sounds", 1);
        ls.difficulty = ini_int("game", "difficulty", 1) & 3;
        ls.time_adj = ini_int("game", "time", 1) & 3;
        ls.widescreen = g_wide;
        ls.arrange_music = ini_int("audio", "external_music", 1) != 0;
        ls.md_enhancements = ini_int("megadrive", "enhancements", 0) != 0;
        ls.md_controls = ini_int("megadrive", "controls", 0) != 0;
        ls.md_music = ini_int("megadrive", "music", 0) != 0;
        {   /* how many arranged tracks are in the music folder */
            static const char *titles[4] = { "Outride a Crisis", "Sprinter", "Winning Run", "Hard Road" };
            int found = 0; char mp[MAX_PATH];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music\\%s.mp3", titles[i]); path_join(mp, g_dir, n);
                                          if (GetFileAttributesA(mp) != INVALID_FILE_ATTRIBUTES) found++; }
            if (found == 4) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: all 4 tracks found in the music folder");
            else if (found) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: %d of 4 tracks found (others play regular)", found);
            else snprintf(ls.music_note, sizeof ls.music_note, "Arrange tracks not found: regular music will play");
        }
        ls.md_check = md_rom_check;
        if (ini_int("megadrive", "enhancements", 0) && find_md_rom(g_md_path, sizeof g_md_path))
            g_md_thread = CreateThread(NULL, 0, md_capture_thread, NULL, 0, NULL);
        if (!launcher_run(inst, &ls, rom_check)) return 0;          /* window closed: quit */
        ini_set_int("audio", "external_music", ls.arrange_music);
        ini_set_int("game", "demo_sounds", ls.demo_sounds);
        ini_set_int("game", "difficulty", ls.difficulty);
        ini_set_int("game", "time", ls.time_adj);
        ini_set_int("video", "widescreen", ls.widescreen);
        ini_set_int("megadrive", "enhancements", ls.md_enhancements);
        ini_set_int("megadrive", "controls", ls.md_controls);
        ini_set_int("megadrive", "music", ls.md_music);
        g_wide = ls.widescreen;
    }
    char rmsg[256], detail[256];
    if (!rom_check(rmsg, sizeof rmsg, detail, sizeof detail)) {
        char box[3072];
        snprintf(box, sizeof box, "%s\n%s\n\n%s\n\nPut your MAME 'shangon.zip' (Super Hang-On, sitdown/upright, unprotected) next to SuperHangOn.exe.",
                 rmsg, detail, g_rom_err);
        MessageBoxA(NULL, box, "Super Hang-On Ultimate", MB_ICONERROR);
        return 1;
    }
    if (ini_int("audio", "external_music", 1)) {
        char music_dir[MAX_PATH], music_err[512];
        path_join(music_dir, g_dir, "music");
        sho_load_music(music_dir, AUDIO_RATE, music_err, sizeof music_err);
    }
    g_md_present = md_rom_present();   /* the Mega Drive options only apply with the Mega Drive ROM there */
    load_sound_levels();
    apply_sound_levels(0);
    if (g_md_thread) { WaitForSingleObject(g_md_thread, INFINITE); CloseHandle(g_md_thread); g_md_thread = NULL; }
    else if (ini_int("megadrive", "enhancements", 0)) {   /* Original Mode graphics/courses from the Mega Drive ROM, before the arcade boots */
        char merr[256];
        if (find_md_rom(g_md_path, sizeof g_md_path)) mdgfx_capture(g_md_path, merr, sizeof merr);
    }
    sho_reset();
    load_nvram();
    apply_game_settings();
    {   char sav[MAX_PATH]; path_join(sav, g_dir, "shangon_original.sav"); sho_md_set_save_path(sav);
        sho_md_configure(g_md_present && ini_int("megadrive", "enhancements", 0), g_md_present && ini_int("megadrive", "controls", 0));
        sho_md_dev_race(ini_int("dev", "original_race", 0));
        sho_md_set_music(g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0));   /* the Mega Drive's own sound driver */
        if (sho_md_music_active()) sho_silence_music(); }                      /* the arcade's songs off: the Mega Drive's play instead */
    load_xinput();

    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = wndproc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = "SuperHangOn";
    RegisterClassA(&wc);
    RECT r = { 0, 0, 1280, 720 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExA(0, "SuperHangOn", "Super Hang-On Ultimate", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
    if (g_fullscreen) set_fullscreen(1);
    memset(&g_bmi, 0, sizeof g_bmi);
    g_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); g_bmi.bmiHeader.biWidth = SHO_W; g_bmi.bmiHeader.biHeight = -SHO_H;
    g_bmi.bmiHeader.biPlanes = 1; g_bmi.bmiHeader.biBitCount = 32; g_bmi.bmiHeader.biCompression = BI_RGB;
    g_use_d3d = want_d3d && d3d_init(g_hwnd, SHO_MAX_W, SHO_H);
    if (g_vrr && !(g_use_d3d && d3d_tearing_supported())) g_vrr = 0;
    update_title();

    timeBeginPeriod(1);
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    int have_audio = audio_init();
    int16_t samples[2 * 1100];
    int frames_since_save = 0, refresh_check = 0;
    double refresh = monitor_refresh();
    if (have_audio) { memset(samples, 0, sizeof samples); audio_submit(samples, 1100); audio_submit(samples, 1100); audio_submit(samples, 1000); }
    double acc = 0, last = now_s(), next_frame = last, audio_frac = 0;

    MSG msg;
    while (g_running) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = 0; break; }
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (!g_running) break;
        if (++refresh_check >= 120) { refresh_check = 0; refresh = monitor_refresh(); }

        /* ---- how many game frames this iteration ----
         * vsync mode: one loop iteration per display refresh. If the refresh is (close to) a multiple of
         * 60 Hz, lock the game to it (k refreshes per game frame) - perfectly even motion. Otherwise
         * distribute 60.05 Hz game frames over refreshes. VRR/GDI: pace by the clock. */
        int vsync_mode = g_use_d3d && !g_vrr;
        double period;                      /* game frame period actually used */
        int k = (int)(refresh / 60.0 + 0.5); if (k < 1) k = 1;
        int locked = vsync_mode && fabs(refresh - 60.0 * k) <= 0.012 * 60.0 * k;
        int frames = 0;
        double t = now_s();
        if (vsync_mode) {
            period = locked ? (double)k / refresh : 1.0 / SHO_FPS;
            acc += 1.0 / refresh;
            /* if presents are not waiting for vblank (minimised, occluded), fall back to the clock */
            if (t - last < 0.5 / refresh) { wait_until(last + 1.0 / refresh); t = now_s(); }
            while (acc >= period - 1e-9 && frames < 4) { frames++; acc -= period; }
        } else {
            period = 1.0 / SHO_FPS;
            if (t < next_frame) { wait_until(next_frame); t = now_s(); }
            frames = 1; next_frame += period;
            if (t - next_frame > 0.1) next_frame = t;      /* fell far behind: resync */
        }
        last = t;

        for (int f = 0; f < frames; f++) {
            sho_input in; read_input(&in, (float)period);
            /* Mega Drive mode: Start pauses during a race, as on the Mega Drive, with a menu under PAUSE */
            { static int start_prev, ptick; int start_edge = in.start && !start_prev; start_prev = in.start;
              if (!g_paused && start_edge && sho_md_pause_allowed()) { g_paused = 1; g_pause_menu = sho_pause_begin(); ptick = 0; }
              else if (g_paused && g_pause_menu) {
                  int act = sho_pause_update(&in, ptick++);
                  if (act) { sho_pause_end(); apply_sound_levels(1); g_paused = 0; g_pause_menu = 0; if (act == 2) reset_to_title(); }
              }
              if (g_paused) in.start = 0; }
            if (!g_paused) { sho_set_input(&in); sho_run_frame();
                if (sho_md_exit_requested()) { g_running = 0; PostMessageA(g_hwnd, WM_CLOSE, 0, 0); }
                int rq = sho_md_reset_requested();
                if (rq == 1) reset_to_title();
                else if (rq == 2) { sho_md_before_reset(); save_nvram(); sho_reset(); load_nvram(); apply_game_settings(); } }
            if (have_audio) {
                /* samples for this frame; small correction keeps the device queue near its target */
                long q = audio_queued();
                double corr = (double)(AUDIO_TARGET - q) / AUDIO_TARGET * 0.01;
                if (corr > 0.01) corr = 0.01;
                if (corr < -0.01) corr = -0.01;
                audio_frac += AUDIO_RATE * period * (1.0 + corr);
                int n = (int)audio_frac; audio_frac -= n; if (n > 1100) n = 1100;
                if (g_paused) memset(samples, 0, sizeof samples); else sho_audio(samples, n);
                if (q < AUDIO_TARGET * 4) audio_submit(samples, n);   /* drop if far too much is queued */
            }
            if (++frames_since_save >= 60 * 60) { save_nvram(); frames_since_save = 0; }
        }
        present(vsync_mode ? 1 : 0);
    }

    save_nvram();
    sho_music_unload();
    if (g_use_d3d) d3d_shutdown();
    if (g_wo) { waveOutReset(g_wo); for (int i = 0; i < NUM_AUDIO_BUFS; i++) if (g_hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wo, &g_hdr[i], sizeof g_hdr[i]); waveOutClose(g_wo); }
    timeEndPeriod(1);
    SetThreadExecutionState(ES_CONTINUOUS);
    return 0;
}
