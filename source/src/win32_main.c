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
#include "launcher_core.h"
#include "sho_views.h"
#include "sho_pace.h"
#include "sho_controls.h"
#include "sho_version.h"
#include "sho_music.h"
#include "sho_md.h"
#include "sho_mdgfx.h"

extern int sho_recomp;

#define AUDIO_RATE 48000
#define NUM_AUDIO_BUFS 32
#define AUDIO_TARGET 3200            /* samples queued ahead (~67 ms) */

static HWND g_hwnd;
static int g_dev_dirty = 1;   /* the controller list needs making again (WM_DEVICECHANGE) */
static int g_md_present, g_pause_menu, g_in_launcher;   /* g_in_launcher: the settings screen is in the window */
static void reset_to_title(void);
static void apply_sound_levels(int save);
static int g_fullscreen = 1, g_running = 1, g_paused = 0, g_filter = 2, g_stretch = 0, g_shape = 0, g_vrr = 0, g_use_d3d = 0;   /* g_shape: the display shape (sho_views.h; F9) */
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
          "aspect=16:9       ; 16:9, 18:9, 19:9, 20:9, 21:9 or 4:3 (F9 steps through them, Shift+F9 back)\n"
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
static void ini_set(const char *sec, const char *key, const char *v) { WritePrivateProfileStringA(sec, key, v, g_ini); }
static void ini_set_int(const char *sec, const char *key, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); WritePrivateProfileStringA(sec, key, b, g_ini); }

static void apply_game_settings(void) {
    int demo = ini_int("game", "demo_sounds", 1), diff = ini_int("game", "difficulty", 1), tim = ini_int("game", "time", 1);
    int free_play = ini_int("game", "free_play", 0);
    static const uint8_t dmap[4] = { 0x04, 0x06, 0x02, 0x00 }, tmap[4] = { 0x10, 0x18, 0x08, 0x00 };
    uint8_t dsw = 0xe0 | (demo ? 0 : 1) | dmap[diff & 3] | tmap[tim & 3];
    sho_set_dips(free_play ? 0x00 : 0xff, dsw);   /* coinage: both nibbles 0 = free play */
    sho_pause_set_display(sho_shapes[g_shape].name);
    sho_set_view_width(sho_shapes[g_shape].width);
}
/* the display shape: F9 steps through them (Shift+F9 back), as does DISPLAY on the pause menu */
static void update_title(void);
static void set_shape(int i) {
    g_shape = ((i % SHO_NSHAPES) + SHO_NSHAPES) % SHO_NSHAPES;
    sho_pause_set_display(sho_shapes[g_shape].name);     /* (first: changing the width redraws the pause menu) */
    sho_set_view_width(sho_shapes[g_shape].width);
    ini_set("video", "aspect", sho_shapes[g_shape].name);
    ini_set_int("video", "widescreen", g_shape != SHO_SHAPE_43);   /* (for older versions) */
    update_title();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, TRUE);
}
static int read_shape(void) {
    char a[16]; ini_str("video", "aspect", "", a, sizeof a);
    int i = sho_shape_find(a);
    return i >= 0 ? i : ini_int("video", "widescreen", 1) ? 0 : SHO_SHAPE_43;
}

static void update_title(void) {
    char t[160];
    snprintf(t, sizeof t, "Super Hang-On Ultimate  [%s, %s, %s%s%s%s]", sho_shapes[g_shape].name, filter_names[g_filter],
             g_use_d3d ? "Direct3D 11" : "GDI", sho_recomp ? ", C" : ", interpreter",
             g_md_present && ini_int("megadrive", "enhancements", 0) ? ", MD Enhancements" : "", g_md_present && ini_int("megadrive", "controls", 0) ? ", MD Controls" : "", g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0) ? ", MD Music" : "");
    SetWindowTextA(g_hwnd, t);
}

/* ------------------------------------------------------------------ */
/* display                                                            */
/* ------------------------------------------------------------------ */
/* the mouse pointer: hidden in fullscreen once the game is running; the launcher always has it (its menus take clicks) */
static void sync_cursor(void) {
    if (g_fullscreen && !g_in_launcher) { while (ShowCursor(FALSE) >= 0) {} }
    else { while (ShowCursor(TRUE) < 0) {} }
}
static void set_fullscreen(int on) {
    DWORD style = GetWindowLongA(g_hwnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi = { sizeof mi };
        GetWindowPlacement(g_hwnd, &g_wp);
        GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongA(g_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_hwnd, &g_wp);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    g_fullscreen = on;
    sync_cursor();
    InvalidateRect(g_hwnd, NULL, TRUE);
}

/* display aspect of the game image: the arcade monitor is 4:3 for 320 pixels; the 16:9 view keeps that pixel shape */
static double image_aspect(void) { return g_stretch ? 0.0 : sho_shapes[g_shape].aspect; }   /* (the shape's own aspect: fills a screen of that shape exactly) */

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

/* the launcher (settings screen, loading bar): its 1920x1080 picture, 16:9 with black bars */
static void present_launcher_gdi(HDC dc) {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int x, y, w, h; lc_view_rect(rc.right, rc.bottom, &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (x > 0) { RECT l = { 0, 0, x, rc.bottom }, r = { x + w, 0, rc.right, rc.bottom }; FillRect(dc, &l, black); FillRect(dc, &r, black); }
    if (y > 0) { RECT t = { 0, 0, rc.right, y }, b = { 0, y + h, rc.right, rc.bottom }; FillRect(dc, &t, black); FillRect(dc, &b, black); }
    const uint32_t *img = lc_image(); if (!img) return;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = LC_IW; bi.bmiHeader.biHeight = -LC_IH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, NULL);
    StretchDIBits(dc, x, y, w, h, 0, 0, LC_IW, LC_IH, img, &bi, DIB_RGB_COLORS, SRCCOPY);
}
static void present_launcher(void) {
    if (g_use_d3d) {
        const uint32_t *img = lc_image();
        int r = img ? d3d_present(img, LC_IW, LC_IH, 16.0 / 9.0, 2, 1, 0) : 1;   /* sharp-bilinear: crisp letters at any size */
        if (r == 0) { g_use_d3d = 0; d3d_shutdown(); }
        else { if (r == 2) Sleep(16); return; }
    }
    HDC dc = GetDC(g_hwnd); present_launcher_gdi(dc); ReleaseDC(g_hwnd, dc);
    Sleep(16);
}
static void pump_messages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { g_running = 0; break; }
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
}
static void present_loading(void) { pump_messages(); present_launcher(); }   /* lc_load_step: the loading bar */

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
        if (g_in_launcher) {   /* the settings screen: arrows, Enter, Space, Esc; F11 still switches fullscreen */
            if (w == VK_F11 && !(l & (1 << 30))) set_fullscreen(!g_fullscreen);
            if (lc_wants_key()) {   /* the controls screen is waiting for a key to assign (Esc cancels; the F keys keep their jobs) */
                if (w == VK_ESCAPE) lc_key(LK_ESC); else if (!(l & (1 << 30)) && !(w >= VK_F1 && w <= VK_F24) && w < 256) lc_raw_key((int)w);
                return 0;
            }
            switch (w) {
            case VK_TAB: lc_key(LK_TAB); break;
            case VK_DELETE: case VK_BACK: lc_key(LK_CLEAR); break;
            case VK_UP: lc_key(LK_UP); break;
            case VK_DOWN: lc_key(LK_DOWN); break;
            case VK_LEFT: lc_key(LK_LEFT); break;
            case VK_RIGHT: lc_key(LK_RIGHT); break;
            case VK_RETURN: if (!(l & (1 << 30))) lc_key(LK_ENTER); break;
            case VK_SPACE: if (!(l & (1 << 30))) lc_key(LK_SPACE); break;
            case VK_ESCAPE: if (!(l & (1 << 30))) lc_key(LK_ESC); break;
            }
            return 0;
        }
        if (w < 256) g_keys[w] = 1;
        if (l & (1 << 30)) return 0;   /* auto-repeat */
        if (w == VK_F11) set_fullscreen(!g_fullscreen);
        if (w == VK_ESCAPE && g_fullscreen) set_fullscreen(0);
        if (sho_key_is_action(KA_PAUSE, (int)w) && !(l & (1 << 30))) {   /* the pause key (P): in Mega Drive mode during a race this opens the pause menu */
            if (!g_paused) { g_paused = 1; g_pause_menu = sho_pause_begin(); }
            else { if (g_pause_menu) { sho_pause_end(); apply_sound_levels(1); } g_paused = 0; g_pause_menu = 0; }
        }
        if (w == '8' && (GetKeyState(VK_CONTROL) & 0x8000)) sho_md_cheat_upgrade();
        if (w == '9' && (GetKeyState(VK_CONTROL) & 0x8000)) sho_md_cheat_money();   /* development: maximum money */   /* development: fully upgraded bike */
        if (w >= '0' && w <= '7' && (GetKeyState(VK_CONTROL) & 0x8000)) { sho_md_dev_race(w - '0'); ini_set_int("dev", "original_race", w - '0'); }   /* development: Original Mode race select */
        if (w == VK_F3) reset_to_title();
        if (w == VK_F9) set_shape(g_shape + ((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1));
        if (w == VK_F8) { g_filter = (g_filter + 1) % 3; ini_set_int("video", "filter", g_filter); update_title(); }
        return 0;
    case WM_KEYUP: if (w < 256) g_keys[w] = 0; return 0;
    case WM_KILLFOCUS: memset(g_keys, 0, sizeof g_keys); return 0;
    case WM_SIZE: if (g_use_d3d) d3d_resize(); return 0;
    case WM_LBUTTONDOWN:
        if (g_in_launcher) {
            RECT rc; GetClientRect(h, &rc); int cx, cy;
            lc_to_canvas(rc.right, rc.bottom, (short)LOWORD(l), (short)HIWORD(l), &cx, &cy);
            lc_click(cx, cy);
        }
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); if (!g_use_d3d) { if (g_in_launcher) present_launcher_gdi(dc); else present_gdi(dc); } EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_DEVICECHANGE: g_dev_dirty = 1; break;        /* a controller plugged in or removed: list them again */
    case WM_SETCURSOR: if (g_fullscreen && !g_in_launcher && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; } break;
    }
    return DefWindowProcA(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */
/* ---- controllers ----
 * Windows has two kinds: XInput pads (Xbox pads and whatever presents itself as one: most modern pads, or others through
 * Steam / DS4Windows), read in the Xbox layout; and every other game controller Windows knows (Control Panel > Game
 * Controllers: the WinMM joystick list), read by button number in the usual DirectInput order (1-4 the face buttons,
 * 5/6 bumpers, 7/8 triggers, 9/10 Back/Start, 11/12 stick clicks, the hat = d-pad). "Any controller" is every XInput
 * pad, as it always was; the others are used when chosen on the launcher's controls screen (their buttons can be set
 * there, with CONFIGURE WITH CONTROLLER or one by one). */
typedef struct { int kind, id; char name[96]; } paddev;     /* kind 0 = XInput slot id, 1 = WinMM joystick id */
#define MAXDEV 12
static paddev g_dev[MAXDEV]; static int g_ndev; static DWORD g_dev_scan_tick;
static void xinput_ids(DWORD *vidpid, int *n, int max) {   /* the VID/PID of the HID devices that are XInput pads (their path has "IG_") */
    *n = 0;
    UINT cnt = 0;
    if (GetRawInputDeviceList(NULL, &cnt, sizeof(RAWINPUTDEVICELIST)) != 0 || !cnt || cnt > 256) return;
    RAWINPUTDEVICELIST list[256];
    if (GetRawInputDeviceList(list, &cnt, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return;
    for (UINT i = 0; i < cnt && *n < max; i++) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        char path[512]; UINT sz = sizeof path;
        if ((int)GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICENAME, path, &sz) <= 0 || !strstr(path, "IG_")) continue;
        RID_DEVICE_INFO di; memset(&di, 0, sizeof di); di.cbSize = sizeof di; sz = sizeof di;
        if ((int)GetRawInputDeviceInfoA(list[i].hDevice, RIDI_DEVICEINFO, &di, &sz) <= 0) continue;
        vidpid[(*n)++] = (di.hid.dwVendorId << 16) | (di.hid.dwProductId & 0xffff);
    }
}
static void winmm_name(UINT id, const JOYCAPSA *caps, char *out, int n) {   /* the name Game Controllers shows (the registry's OEM name) */
    snprintf(out, (size_t)n, "GAME CONTROLLER %u", id + 1);
    typedef LONG (WINAPI *open_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
    typedef LONG (WINAPI *query_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
    typedef LONG (WINAPI *close_t)(HKEY);
    static HMODULE adv; static open_t ro; static query_t rq; static close_t rcl;
    if (!adv) { adv = LoadLibraryA("advapi32.dll");
        if (adv) { ro = (open_t)(void *)GetProcAddress(adv, "RegOpenKeyExA"); rq = (query_t)(void *)GetProcAddress(adv, "RegQueryValueExA"); rcl = (close_t)(void *)GetProcAddress(adv, "RegCloseKey"); } }
    if (!ro || !rq || !rcl) return;
    char key[160]; HKEY k; char val[96]; DWORD len = sizeof val - 1, type = 0;
    snprintf(key, sizeof key, "System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\VID_%04X&PID_%04X", caps->wMid, caps->wPid);
    if (ro(HKEY_CURRENT_USER, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    if (rq(k, "OEMName", NULL, &type, (LPBYTE)val, &len) == ERROR_SUCCESS && type == REG_SZ && len > 1) { val[len] = 0; snprintf(out, (size_t)n, "%.90s", val); }
    rcl(k);
}
static void devices_scan(int force) {   /* at most every two seconds (asking WinMM about joysticks that aren't there is slow) */
    DWORD now = GetTickCount();
    if (!force && g_dev_scan_tick && now - g_dev_scan_tick < 2000) return;
    g_dev_scan_tick = now ? now : 1;
    g_ndev = 0;
    if (pXInputGetState) for (DWORD i = 0; i < 4 && g_ndev < MAXDEV; i++) {
        XINPUT_STATE st; ZeroMemory(&st, sizeof st);
        if (pXInputGetState(i, &st) != ERROR_SUCCESS) continue;
        g_dev[g_ndev].kind = 0; g_dev[g_ndev].id = (int)i; snprintf(g_dev[g_ndev].name, sizeof g_dev[g_ndev].name, "XBOX CONTROLLER %lu", (unsigned long)i + 1); g_ndev++;
    }
    DWORD xi[16]; int nxi; xinput_ids(xi, &nxi, 16);
    UINT nj = joyGetNumDevs(); if (nj > 16) nj = 16;
    for (UINT id = 0; id < nj && g_ndev < MAXDEV; id++) {
        JOYCAPSA caps; JOYINFOEX ji; memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNBUTTONS;
        if (joyGetDevCapsA(id, &caps, sizeof caps) != JOYERR_NOERROR || joyGetPosEx(id, &ji) != JOYERR_NOERROR) continue;
        int is_x = 0; for (int k = 0; k < nxi; k++) if (xi[k] == (((DWORD)caps.wMid << 16) | caps.wPid)) is_x = 1;
        if (is_x) continue;                                 /* already in the list as an XInput pad */
        g_dev[g_ndev].kind = 1; g_dev[g_ndev].id = (int)id; winmm_name(id, &caps, g_dev[g_ndev].name, sizeof g_dev[g_ndev].name);
        for (int k = 0; k < g_ndev; k++) if (!strcmp(g_dev[k].name, g_dev[g_ndev].name)) {   /* two of the same: number the second */
            size_t l = strlen(g_dev[g_ndev].name); snprintf(g_dev[g_ndev].name + l, sizeof g_dev[g_ndev].name - l, " (%d)", g_ndev + 1); break; }
        g_ndev++;
    }
}
static int read_xinput(DWORD i, sho_pad *p) {
    memset(p, 0, sizeof *p);
    if (!pXInputGetState) return 0;
    if (g_pad_poll_cooldown[i] > 0) { g_pad_poll_cooldown[i]--; return 0; }
    XINPUT_STATE st; ZeroMemory(&st, sizeof st);
    if (pXInputGetState(i, &st) != ERROR_SUCCESS) { g_pad_poll_cooldown[i] = 120; return 0; }
    XINPUT_GAMEPAD *g = &st.Gamepad; WORD b = g->wButtons;
    p->lx = g->sThumbLX / 32767.0f; p->ly = g->sThumbLY / 32767.0f;
    p->lt = g->bLeftTrigger / 255.0f; p->rt = g->bRightTrigger / 255.0f;
    static const struct { WORD x; int pb; } m[] = {
        { XINPUT_GAMEPAD_A, PB_A }, { XINPUT_GAMEPAD_B, PB_B }, { XINPUT_GAMEPAD_X, PB_X }, { XINPUT_GAMEPAD_Y, PB_Y },
        { XINPUT_GAMEPAD_LEFT_SHOULDER, PB_LB }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, PB_RB }, { XINPUT_GAMEPAD_BACK, PB_BACK }, { XINPUT_GAMEPAD_START, PB_START },
        { XINPUT_GAMEPAD_LEFT_THUMB, PB_L3 }, { XINPUT_GAMEPAD_RIGHT_THUMB, PB_R3 },
        { XINPUT_GAMEPAD_DPAD_UP, PB_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, PB_DOWN }, { XINPUT_GAMEPAD_DPAD_LEFT, PB_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, PB_RIGHT } };
    for (unsigned k = 0; k < sizeof m / sizeof m[0]; k++) if (b & m[k].x) p->buttons |= 1u << m[k].pb;
    sho_pad_finish(p);
    return 1;
}
static int read_winmm(UINT id, sho_pad *p) {
    memset(p, 0, sizeof *p);
    static JOYCAPSA caps[16]; static int have[16];
    if (id >= 16) return 0;
    if (!have[id]) { if (joyGetDevCapsA(id, &caps[id], sizeof caps[id]) != JOYERR_NOERROR) return 0; have[id] = 1; }
    JOYINFOEX ji; memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
    if (joyGetPosEx(id, &ji) != JOYERR_NOERROR) { have[id] = 0; return 0; }
    const JOYCAPSA *c = &caps[id];
    if (c->wXmax > c->wXmin) p->lx = ((float)ji.dwXpos - c->wXmin) / (float)(c->wXmax - c->wXmin) * 2.0f - 1.0f;
    if (c->wYmax > c->wYmin) p->ly = -(((float)ji.dwYpos - c->wYmin) / (float)(c->wYmax - c->wYmin) * 2.0f - 1.0f);
    {   /* the stick only counts once it has been seen near its centre: a throttle lever or pedal that rests at one end
           (flight sticks, wheels) must not hold the steering over */
        static unsigned char centred[16];
        if (fabsf(p->lx) < 0.3f && fabsf(p->ly) < 0.3f) centred[id] = 1;
        if (!centred[id]) p->lx = p->ly = 0;
    }
    static const int order[12] = { PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT, PB_BACK, PB_START, PB_L3, PB_R3 };
    /* PlayStation pads (DualShock 4, DualSense; Sony's vendor number): buttons 1-12 are Square, Cross, Circle, Triangle,
       L1, R1, L2, R2, Share, Options, L3, R3 - Cross is the Xbox A, Circle B, Square X, Triangle Y */
    static const int sony[12] = { PB_X, PB_A, PB_B, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT, PB_BACK, PB_START, PB_L3, PB_R3 };
    const int *ord = c->wMid == 0x054C ? sony : order;
    for (int k = 0; k < 12; k++) if (ji.dwButtons & (1u << k)) p->buttons |= 1u << ord[k];
    if ((c->wCaps & JOYCAPS_HASPOV) && (ji.dwPOV & 0xffff) != 0xffff) {   /* the hat, in hundredths of a degree */
        DWORD a = ji.dwPOV % 36000;
        if (a >= 29250 || a <= 6750) p->buttons |= 1u << PB_UP;
        if (a >= 2250 && a <= 15750) p->buttons |= 1u << PB_RIGHT;
        if (a >= 11250 && a <= 24750) p->buttons |= 1u << PB_DOWN;
        if (a >= 20250 && a <= 33750) p->buttons |= 1u << PB_LEFT;
    }
    sho_pad_finish(p);
    return 1;
}
static int read_dev(const paddev *d, sho_pad *p) { return d->kind == 0 ? read_xinput((DWORD)d->id, p) : read_winmm((UINT)d->id, p); }
static const paddev *chosen_dev(void) {   /* the controller picked on the controls screen, if it is there */
    if (!sho_ctl.device[0]) return NULL;
    for (int i = 0; i < g_ndev; i++) if (!strcmp(g_dev[i].name, sho_ctl.device)) return &g_dev[i];
    return NULL;
}
/* the launcher's controls screen */
static int host_pad_count(void) { devices_scan(0); return g_ndev; }
static const char *host_pad_name(int i) { return i >= 0 && i < g_ndev ? g_dev[i].name : ""; }
static int host_pad_read(int i, sho_pad *out) {
    if (i >= 0) return i < g_ndev ? read_dev(&g_dev[i], out) : 0;
    int any = 0; memset(out, 0, sizeof *out);            /* any: every XInput pad and every other controller, merged */
    for (int k = 0; k < 4 + g_ndev; k++) {
        sho_pad p;
        if (k < 4) { if (!read_xinput((DWORD)k, &p)) continue; }
        else if (g_dev[k - 4].kind != 1 || !read_dev(&g_dev[k - 4], &p)) continue;
        any = 1; out->buttons |= p.buttons;
        if (fabsf(p.lx) > fabsf(out->lx)) out->lx = p.lx;
        if (fabsf(p.ly) > fabsf(out->ly)) out->ly = p.ly;
        if (p.lt > out->lt) out->lt = p.lt;
        if (p.rt > out->rt) out->rt = p.rt; }
    return any;
}
static const char *host_key_name(int vk) {
    static char b[32];
    static const struct { int vk; const char *n; } t[] = {
        { VK_LEFT, "LEFT" }, { VK_RIGHT, "RIGHT" }, { VK_UP, "UP" }, { VK_DOWN, "DOWN" }, { VK_SPACE, "SPACE" }, { VK_RETURN, "ENTER" },
        { VK_SHIFT, "SHIFT" }, { VK_CONTROL, "CTRL" }, { VK_MENU, "ALT" }, { VK_BACK, "BACKSPACE" }, { VK_TAB, "TAB" }, { VK_DELETE, "DELETE" },
        { VK_INSERT, "INSERT" }, { VK_HOME, "HOME" }, { VK_END, "END" }, { VK_PRIOR, "PAGE UP" }, { VK_NEXT, "PAGE DOWN" }, { VK_CAPITAL, "CAPS LOCK" } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].vk == vk) return t[i].n;
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(b, sizeof b, "NUMPAD %d", vk - VK_NUMPAD0); return b; }
    UINT sc = MapVirtualKeyA((UINT)vk, 0);
    if (sc && GetKeyNameTextA((LONG)(sc << 16), b, sizeof b) > 0) return b;
    snprintf(b, sizeof b, "KEY %d", vk);
    return b;
}
static void controls_save(void) {
    char t[256];
    sho_controls_pad_to_str(t, sizeof t); ini_set("pad", "actions", t);
    ini_set("pad", "device", sho_ctl.device);
    sho_controls_keys_to_str(t, sizeof t); ini_set("keys_windows", "keys", t);
}
static void controls_load(void) {
    static const int def[KA_COUNT][KEY_SLOTS] = {
        [KA_LEFT] = { VK_LEFT, 'A', -1, -1 }, [KA_RIGHT] = { VK_RIGHT, 'D', -1, -1 }, [KA_ACCEL] = { VK_UP, 'Z', 'W', VK_CONTROL },
        [KA_BRAKE] = { VK_DOWN, 'X', 'S', -1 }, [KA_TURBO] = { VK_SPACE, VK_SHIFT, -1, -1 }, [KA_START] = { VK_RETURN, '1', -1, -1 },
        [KA_COIN] = { '5', 'C', -1, -1 }, [KA_PAUSE] = { 'P', -1, -1, -1 } };
    char t[256];
    sho_ctl.deadzone = g_deadzone; sho_ctl.curve = g_steer_curve;
    sho_controls_pad_defaults(g_swap_pedals);
    ini_str("pad", "actions", "", t, sizeof t); if (t[0]) sho_controls_pad_from_str(t);
    ini_str("pad", "device", "", sho_ctl.device, sizeof sho_ctl.device);
    sho_controls_key_defaults(def);
    ini_str("keys_windows", "keys", "", t, sizeof t); if (t[0]) sho_controls_keys_from_str(t);
}
static const lc_controls_host g_controls_host = { host_pad_count, host_pad_name, host_pad_read, host_key_name, controls_save };

static int key_held(int vk) { return vk >= 0 && vk < 256 && g_keys[vk]; }
static int is_arrow(int vk) { return vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT; }
static int act_key_not_arrow(int ka) {   /* one of the action's keys is down, arrows aside (they move through menus) */
    for (int i = 0; i < KEY_SLOTS; i++) { int c = sho_ctl.key[ka][i]; if (c >= 0 && !is_arrow(c) && key_held(c)) return 1; }
    return 0;
}
static void read_input(sho_input *in, float dt) {
    memset(in, 0, sizeof *in);
    int focused = GetForegroundWindow() == g_hwnd;
    int menu = sho_md_overlay_active() || g_pause_menu;   /* the Original Mode screens, the pause menu */
    int kl = focused && sho_key_action_down(KA_LEFT, key_held), kr = focused && sho_key_action_down(KA_RIGHT, key_held);
    float target = (float)(kr - kl), rate = (target == 0.0f) ? 6.0f : 3.5f;   /* keyboard: handlebar ramps */
    if (g_kb_steer < target) { g_kb_steer += rate * dt; if (g_kb_steer > target) g_kb_steer = target; }
    else if (g_kb_steer > target) { g_kb_steer -= rate * dt; if (g_kb_steer < target) g_kb_steer = target; }
    float steer = g_kb_steer, thr = 0, brk = 0;
    if (focused && menu) {   /* there: arrows move, Space or the accelerate keys select, Backspace or the brake keys go back (Enter is Start) */
        in->nav_up = g_keys[VK_UP]; in->nav_down = g_keys[VK_DOWN]; in->nav_left = g_keys[VK_LEFT]; in->nav_right = g_keys[VK_RIGHT];
        in->pad_a = g_keys[VK_SPACE] || act_key_not_arrow(KA_ACCEL); in->pad_b = g_keys[VK_BACK] || act_key_not_arrow(KA_BRAKE);
    }
    if (focused) {
        int dev = g_keys[VK_CONTROL] && !sho_key_is_action(KA_ACCEL, VK_CONTROL);   /* Ctrl+digits are the development keys */
        if (sho_key_action_down(KA_ACCEL, key_held)) thr = 1;
        if (sho_key_action_down(KA_BRAKE, key_held)) brk = 1;
        in->turbo = sho_key_action_down(KA_TURBO, key_held);
        in->coin = !dev && sho_key_action_down(KA_COIN, key_held);
        in->start = !dev && sho_key_action_down(KA_START, key_held);
        in->test = g_keys[VK_F2];
        in->service = !dev && g_keys['9'];
    }
    /* controllers: the one chosen on the controls screen, or any of them (XInput pads, and the others Windows lists:
       PlayStation pads and the like). The list is made again when Windows says a device came or went. */
    if (g_dev_dirty) { g_dev_dirty = 0; devices_scan(1); }
    static int scan_div, misses;
    const paddev *d = chosen_dev();
    sho_pad p;
    if (sho_ctl.device[0] && (!d || misses > 30) && (++scan_div % 180) == 1) { devices_scan(1); d = chosen_dev(); misses = 0; }   /* not there (yet, or any more): look again every 3 s */
    if (d) { if (read_dev(d, &p)) { misses = 0; sho_controls_apply_pad(d->kind * 4 + (d->id & 3), &p, menu, in, &steer, &thr, &brk); } else misses++; }
    else {   /* any (and when the chosen one is unplugged) */
        for (DWORD i = 0; i < 4; i++) if (read_xinput(i, &p)) sho_controls_apply_pad((int)i, &p, menu, in, &steer, &thr, &brk);
        for (int i = 0; i < g_ndev; i++) if (g_dev[i].kind == 1 && read_dev(&g_dev[i], &p)) sho_controls_apply_pad(4 + (g_dev[i].id & 3), &p, menu, in, &steer, &thr, &brk);
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
    { char lg[MAX_PATH]; path_join(lg, g_dir, "shangon.log"); sho_diag_open(lg); sho_diag("Super Hang-On Ultimate %s (Windows)", SHO_VERSION); }
    write_default_ini();
    char s[64];
    g_fullscreen = ini_int("video", "fullscreen", 1);
    g_shape = read_shape();
    g_filter = ini_int("video", "filter", 2) % 3;
    g_stretch = ini_int("video", "stretch", 0);
    g_vrr = ini_int("video", "vrr", 0);
    ini_str("video", "renderer", "d3d11", s, sizeof s); int want_d3d = _stricmp(s, "gdi") != 0;
    g_deadzone = ini_int("input", "deadzone", 15) / 100.0f;
    g_steer_curve = ini_int("input", "steer_curve", 100) / 100.0f;
    g_swap_pedals = ini_int("input", "swap_pedals", 0);
    controls_load();
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
    load_xinput();

    /* the game window first: the launcher (settings screen) is shown in it */
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
    g_use_d3d = want_d3d && d3d_init(g_hwnd, LC_IW > SHO_MAX_W ? LC_IW : SHO_MAX_W, LC_IH > SHO_H ? LC_IH : SHO_H);   /* big enough for the launcher picture */
    if (g_vrr && !(g_use_d3d && d3d_tearing_supported())) g_vrr = 0;

    if (show_launcher) {
        launcher_settings ls; memset(&ls, 0, sizeof ls);
        static const char *views[SHO_NSHAPES];
        for (int i = 0; i < SHO_NSHAPES; i++) views[i] = sho_shapes[i].long_name;
        ls.view_names = views; ls.view_count = SHO_NSHAPES; ls.view_desc = "F9 SWITCHES DURING PLAY";
        ls.demo_sounds = ini_int("game", "demo_sounds", 1);
        ls.difficulty = ini_int("game", "difficulty", 1) & 3;
        ls.time_adj = ini_int("game", "time", 1) & 3;
        ls.view = g_shape;
        ls.arrange_music = ini_int("audio", "external_music", 1) != 0;
        ls.md_enhancements = ini_int("megadrive", "enhancements", 0) != 0;
        ls.md_controls = ini_int("megadrive", "controls", 0) != 0;
        ls.md_music = ini_int("megadrive", "music", 0) != 0;
        ls.game_dir = g_dir;
        {   /* how many arranged tracks are in the music folder */
            int found = 0; char mp[MAX_PATH];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music\\%s.mp3", sho_music_tracks[i].title); path_join(mp, g_dir, n);
                                          if (GetFileAttributesA(mp) != INVALID_FILE_ATTRIBUTES) found++; }
            if (found == 4) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: all 4 tracks found in the music folder");
            else if (found) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: %d of 4 tracks found (others play regular)", found);
            else snprintf(ls.music_note, sizeof ls.music_note, "Arrange tracks not found: regular music will play");
        }
        ls.md_check = md_rom_check;
        if (ini_int("megadrive", "enhancements", 0) && find_md_rom(g_md_path, sizeof g_md_path))
            g_md_thread = CreateThread(NULL, 0, md_capture_thread, NULL, 0, NULL);
        {   /* the launcher picture: res/launcher_bg.png, resource 3 */
            const unsigned char *png = NULL; size_t png_len = 0;
            HRSRC rs = FindResourceA(inst, MAKEINTRESOURCEA(3), MAKEINTRESOURCEA(10));   /* RT_RCDATA */
            HGLOBAL rg = rs ? LoadResource(inst, rs) : NULL;
            if (rg) { png = (const unsigned char *)LockResource(rg); png_len = SizeofResource(inst, rs); }
            lc_set_controls_host(&g_controls_host);
            lc_begin(&ls, rom_check, png, png_len);
        }
        lc_clock = now_s; lc_present_hook = present_loading;
        g_in_launcher = 1;
        sync_cursor();
        WORD pad_prev = 0;
        while (g_running && lc_result() == LC_RUNNING) {
            pump_messages();
            {   /* any controller: d-pad, A, Start */
                sho_pad pp; WORD b = 0;
                devices_scan(0);
                if (host_pad_read(-1, &pp)) b = (WORD)pp.buttons;
                WORD e = (WORD)(b & ~pad_prev); pad_prev = b;
                if (e & (1u << PB_UP)) lc_key(LK_PAD_UP);
                if (e & (1u << PB_DOWN)) lc_key(LK_PAD_DOWN);
                if (e & (1u << PB_LEFT)) lc_key(LK_PAD_LEFT);
                if (e & (1u << PB_RIGHT)) lc_key(LK_PAD_RIGHT);
                if (e & (1u << PB_A)) lc_key(LK_PAD_A);
                if (e & (1u << PB_START)) lc_key(LK_PAD_START);
            }
            if (lc_folder_clicked()) {   /* the FOLDER line: open the game folder in Explorer */
                typedef HINSTANCE (WINAPI *ShellExecuteA_t)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
                HMODULE sh = LoadLibraryA("shell32.dll");
                ShellExecuteA_t se = sh ? (ShellExecuteA_t)(void *)GetProcAddress(sh, "ShellExecuteA") : NULL;
                if (se) se(g_hwnd, "open", g_dir, NULL, NULL, SW_SHOWNORMAL);
            }
            lc_tick(now_s());
            present_launcher();
        }
        if (!g_running || lc_result() != LC_PLAY) {   /* window closed / Esc: quit */
            if (g_md_thread) WaitForSingleObject(g_md_thread, INFINITE);
            lc_end();
            if (g_use_d3d) d3d_shutdown();
            return 0;
        }
        lc_finish(&ls);
        ini_set_int("audio", "external_music", ls.arrange_music);
        ini_set_int("game", "demo_sounds", ls.demo_sounds);
        ini_set_int("game", "difficulty", ls.difficulty);
        ini_set_int("game", "time", ls.time_adj);
        set_shape(ls.view);
        ini_set_int("megadrive", "enhancements", ls.md_enhancements);
        ini_set_int("megadrive", "controls", ls.md_controls);
        ini_set_int("megadrive", "music", ls.md_music);
        {   /* the loading bar: which arranged MP3s will be decoded, and whether the Mega Drive capture is still running */
            int have[4]; char mp[MAX_PATH];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music\\%s.mp3", sho_music_tracks[i].title); path_join(mp, g_dir, n);
                                          have[i] = ls.arrange_music && GetFileAttributesA(mp) != INVALID_FILE_ATTRIBUTES; }
            lc_load_begin(have, g_md_thread && WaitForSingleObject(g_md_thread, 0) == WAIT_TIMEOUT);
        }
        lc_load_step(0, 0, "CHECKING THE ARCADE ROM");
    }
    char rmsg[256], detail[256];
    if (!rom_check(rmsg, sizeof rmsg, detail, sizeof detail)) {
        char box[3072];
        snprintf(box, sizeof box, "%s\n%s\n\n%s\n\nPut your MAME 'shangon.zip' (Super Hang-On, sitdown/upright, unprotected) next to SuperHangOn.exe.",
                 rmsg, detail, g_rom_err);
        MessageBoxA(g_hwnd, box, "Super Hang-On Ultimate", MB_ICONERROR);
        if (g_md_thread) WaitForSingleObject(g_md_thread, INFINITE);
        if (g_use_d3d) d3d_shutdown();
        return 1;
    }
    if (ini_int("audio", "external_music", 1)) {
        char music_dir[MAX_PATH], music_err[512];
        path_join(music_dir, g_dir, "music");
        if (g_in_launcher) sho_music_load_progress = lc_music_progress;
        sho_load_music(music_dir, AUDIO_RATE, music_err, sizeof music_err);
        sho_music_load_progress = NULL;
    }
    g_md_present = md_rom_present();   /* the Mega Drive options only apply with the Mega Drive ROM there */
    load_sound_levels();
    apply_sound_levels(0);
    if (g_md_thread) {   /* Original Mode's screens and courses from the Mega Drive ROM: usually ready by now */
        while (WaitForSingleObject(g_md_thread, 5) == WAIT_TIMEOUT) if (g_in_launcher) lc_load_step(2, 0, "PREPARING ORIGINAL MODE");
        CloseHandle(g_md_thread); g_md_thread = NULL;
    }
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
    if (g_in_launcher) { lc_load_step(3, 1.0f, "STARTING THE GAME"); lc_present_hook = NULL; lc_end(); g_in_launcher = 0; sync_cursor(); }
    memset(g_keys, 0, sizeof g_keys);
    update_title();
    InvalidateRect(g_hwnd, NULL, TRUE);

    timeBeginPeriod(1);
    SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    int have_audio = audio_init();
    int16_t samples[2 * 1100];
    int frames_since_save = 0, refresh_check = 0;
    double refresh = monitor_refresh();
    if (have_audio) { memset(samples, 0, sizeof samples); audio_submit(samples, 1100); audio_submit(samples, 1100); audio_submit(samples, 1000); }
    double acc = 0, last = now_s(), next_frame = last, audio_frac = 0;
    sho_pacer pacer; memset(&pacer, 0, sizeof pacer);
    int audio_target = AUDIO_TARGET, underruns = 0, diag_n = 0; long game_frames = 0, diag_frames = 0; double diag_t = last, diag_t0 = last;
    sho_diag("display: %s, reported refresh %.2f Hz, %s", g_use_d3d ? "Direct3D 11" : "GDI", refresh, g_vrr ? "VRR (clock paced)" : "vsync");
    sho_diag("sound: %s", have_audio ? "waveOut 48000 Hz" : "none");

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
            /* if presents are not waiting for vblank at all (minimised, occluded), fall back to the clock */
            if (t - last < 0.2 / refresh) { wait_until(last + 1.0 / refresh); t = now_s(); }
            /* how many refreshes really went by, at the rate presents really come at (sho_pace.h): the reported
               refresh rate isn't always it, and a present can miss one */
            int was = pacer.adopted;
            int ticks = sho_pace_step(&pacer, refresh, t - last);
            double r = pacer.used;
            if (pacer.adopted != was) sho_diag("pacing: presents come at %.2f Hz, not the reported %.2f: using that", r, refresh);
            k = (int)(r / 60.0 + 0.5); if (k < 1) k = 1;
            locked = fabs(r - 60.0 * k) <= 0.012 * 60.0 * k;
            period = locked ? (double)k / r : 1.0 / SHO_FPS;
            acc += ticks / r;
            while (acc >= period - 1e-9 && frames < 4) { frames++; acc -= period; }
            if (acc > period * 4) acc = 0;                 /* fell far behind (a long stall): resync */
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
                  if (act == 3 || act == 4) { set_shape(g_shape + (act == 3 ? 1 : -1)); act = 0; }   /* DISPLAY: stays paused */
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
                if (!g_paused && game_frames > 120 && sho_audio_target_step(q, &audio_target)) {   /* ran dry: more cushion */
                    underruns++; sho_diag("sound: queue ran dry (%ld samples); target now %d ms", q, audio_target * 1000 / AUDIO_RATE); }
                double corr = (double)(audio_target - q) / audio_target * 0.01;
                if (corr > 0.01) corr = 0.01;
                if (corr < -0.01) corr = -0.01;
                audio_frac += AUDIO_RATE * period * (1.0 + corr);
                int n = (int)audio_frac; audio_frac -= n; if (n > 1100) n = 1100;
                if (g_paused) memset(samples, 0, sizeof samples); else sho_audio(samples, n);
                if (q < audio_target * 4) audio_submit(samples, n);   /* drop if far too much is queued */
            }
            if (++frames_since_save >= 60 * 60) { save_nvram(); frames_since_save = 0; }
            game_frames++;
        }
        if (t - diag_t >= (diag_n < 2 ? 5.0 : 30.0)) {   /* the log: the speed after 5 and 10 seconds, then every 30 */
            double fps = (game_frames - diag_frames) / (t - diag_t);
            if (diag_n++ && !g_paused) sho_diag("%4.0f s: speed %.1f%% (%.2f game frames a second), refresh used %.2f Hz (reported %.2f), sound cushion %d ms, ran dry %d times",
                                                t - diag_t0, fps / SHO_FPS * 100.0, fps, vsync_mode ? pacer.used : 0.0, refresh, audio_target * 1000 / AUDIO_RATE, underruns);
            diag_t = t; diag_frames = game_frames;
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
