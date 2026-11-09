/* Super Hang-On – native Linux frontend (SDL2; the Linux twin of win32_main.c and mac_main.m).
 *  - Recompiled game code (C) by default; the original-CPU interpreter remains selectable
 *  - "Fit window" view (default): the picture takes the shape of the window or screen, with the HUD at its
 *    edges as in 16:9; or 16:9 / 4:3. F9 switches. F8: sharp / smooth / sharp-bilinear scaling
 *  - Vsync'd presentation, display-locked pacing at 60 Hz multiples (60/120/240 Hz), clock pacing otherwise
 *  - Fullscreen (default) / window: F11 or Alt+Enter; Esc leaves fullscreen
 *  - Gamepads through SDL's game controller support (Xbox, PlayStation, Switch Pro, 8BitDo...) + keyboard
 *  - Battery-backed RAM (high scores) saved to shangon.nv; settings in shangon.ini
 *  - Optional Mega Drive Enhancements (Original Mode, saved to shangon_original.sav) and Mega Drive controls
 * The game folder (ROMs, music, settings, saves) is the folder that holds the SuperHangOn program; if that
 * folder can't be written, ~/.local/share/super-hang-on-ultimate is used.
 */
#include <SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "sho_core.h"
#include "sho_music.h"
#include "sho_md.h"
#include "sho_mdgfx.h"
#include "linux_launcher.h"
#include "ini_file.h"

extern int sho_recomp;

#define AUDIO_RATE 48000
#define AUDIO_TARGET 3200            /* samples queued ahead (~67 ms) */
#define PATHN 4096

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex, *g_big;   /* the game picture; sharp-bilinear's whole-number enlargement */
static int g_big_k, g_big_w;
static int g_md_present, g_pause_menu;
static void reset_to_title(void);
static void apply_sound_levels(int save);
static double now_s(void);
static int g_fullscreen = 1, g_running = 1, g_paused = 0, g_filter = 2, g_stretch = 0, g_view_mode = VIEW_FIT;
static char g_dir[PATHN], g_ini[PATHN], g_nv[PATHN];
static const char *filter_names[3] = { "sharp", "smooth", "sharp-bilinear" };
static const char *view_names[3] = { "fit window", "16:9", "4:3" };

/* input */
static float g_kb_steer, g_deadzone = 0.15f, g_steer_curve = 1.0f;
static int g_swap_pedals = 0;

/* ------------------------------------------------------------------ */
/* files and settings                                                 */
/* ------------------------------------------------------------------ */
static void path_join(char *out, const char *dir, const char *name) { snprintf(out, PATHN, "%s/%s", dir, name); }
static int file_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

static void load_nvram(void) {
    FILE *f = fopen(g_nv, "rb"); if (!f) return; size_t n = fread(sho_nvram(), 1, sho_nvram_size(), f); (void)n; fclose(f);
    /* credits left over from the last session are not kept: with credits in hand the arcade skips its
       title/attract screens and goes straight to the class-select map (work RAM $66052-$66053) */
    sho_nvram()[0x6052] = 0; sho_nvram()[0x6053] = 0;
}
static void save_nvram(void) { FILE *f = fopen(g_nv, "wb"); if (!f) return; fwrite(sho_nvram(), 1, sho_nvram_size(), f); fclose(f); }

static void write_default_ini(void) {
    if (file_exists(g_ini)) return;
    FILE *f = fopen(g_ini, "w"); if (!f) return;
    fputs("; Super Hang-On Ultimate settings\n"
          "[video]\n"
          "fullscreen=1      ; start in fullscreen (F11 or Alt+Enter toggles)\n"
          "view=fit          ; fit = the window's / screen's own shape, 16:9, or 4:3 (F9)\n"
          "filter=2          ; 0 sharp, 1 smooth, 2 sharp-bilinear (F8)\n"
          "stretch=0         ; 1 = stretch to fill the screen (distorts the picture)\n"
          "vsync=1           ; 0 = don't wait for the display (pace by the clock)\n"
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
          "show=1            ; 1 = show the settings screen first (or start with -play to skip it)\n"
          "[advanced]\n"
          "cpu=recompiled    ; recompiled (C version) or interpreter (original CPU emulation)\n", f);
    fclose(f);
}
static int read_view_mode(void) {
    char s[32]; ini_str("video", "view", "", s, sizeof s);
    if (!strcasecmp(s, "fit") || !strcasecmp(s, "auto")) return VIEW_FIT;
    if (!strcasecmp(s, "16:9") || !strcasecmp(s, "wide")) return VIEW_WIDE;
    if (!strcasecmp(s, "4:3")) return VIEW_43;
    return ini_int("video", "widescreen", 1) ? VIEW_FIT : VIEW_43;   /* an ini from the Windows build */
}
static void save_view_mode(void) { static const char *v[3] = { "fit", "16:9", "4:3" }; ini_set("video", "view", v[g_view_mode]); }

static void apply_game_settings(void) {
    int demo = ini_int("game", "demo_sounds", 1), diff = ini_int("game", "difficulty", 1), tim = ini_int("game", "time", 1);
    int free_play = ini_int("game", "free_play", 0);
    static const uint8_t dmap[4] = { 0x04, 0x06, 0x02, 0x00 }, tmap[4] = { 0x10, 0x18, 0x08, 0x00 };
    uint8_t dsw = 0xe0 | (demo ? 0 : 1) | dmap[diff & 3] | tmap[tim & 3];
    sho_set_dips(free_play ? 0x00 : 0xff, dsw);   /* coinage: both nibbles 0 = free play */
}

static int g_pad_count;
static void update_title(void) {
    if (!g_win) return;
    char t[200], pads[24] = "";
    if (g_pad_count) snprintf(pads, sizeof pads, ", pads: %d", g_pad_count);
    snprintf(t, sizeof t, "Super Hang-On Ultimate  [%s %d, %s%s%s%s%s%s]", view_names[g_view_mode], sho_width(), filter_names[g_filter],
             sho_recomp ? ", C" : ", interpreter",
             g_md_present && ini_int("megadrive", "enhancements", 0) ? ", MD Enhancements" : "", g_md_present && ini_int("megadrive", "controls", 0) ? ", MD Controls" : "",
             g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0) ? ", MD Music" : "", pads);
    SDL_SetWindowTitle(g_win, t);
}

/* ------------------------------------------------------------------ */
/* display                                                            */
/* ------------------------------------------------------------------ */
/* the game picture's width: in "fit window" it takes the shape of the window (a fullscreen 16:10 laptop
   gives 384, 16:9 gives 428, 21:9 the widest the game draws); 16:9 = 428, 4:3 = 320 */
static void update_view_width(void) {
    if (g_paused) return;   /* the pause menu redraws a kept frame of the old width; follow the window on resuming */
    int w = SHO_W;
    if (g_view_mode == VIEW_WIDE) w = SHO_WIDE_W;
    else if (g_view_mode == VIEW_FIT) {
        int ow = 0, oh = 0; SDL_GetRendererOutputSize(g_ren, &ow, &oh);
        w = oh >= 1 ? sho_view_width_for_aspect((double)ow / oh) : SHO_WIDE_W;
    }
    if (w != sho_width()) { sho_set_view_width(w); update_title(); }
}
/* display aspect of the game image: the arcade monitor is 4:3 for 320 pixels; wider views keep that pixel shape */
static double image_aspect(void) { return g_stretch ? 0.0 : (4.0 / 3.0) * sho_width() / (double)SHO_W; }

static void present(void) {
    int ow = 0, oh = 0; SDL_GetRendererOutputSize(g_ren, &ow, &oh);
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    SDL_RenderClear(g_ren);
    if (ow < 1 || oh < 1) { SDL_RenderPresent(g_ren); return; }
    double x = 0, y = 0, w = ow, h = oh, a = image_aspect();
    /* letterbox to the image aspect; a difference under 1.5% (the fit view's whole-pixel rounding) fills exactly */
    if (a > 0 && fabs(((double)ow / oh) / a - 1.0) > 0.015) {
        if ((double)ow / oh > a) { w = oh * a; x = (ow - w) * 0.5; } else { h = ow / a; y = (oh - h) * 0.5; }
    }
    SDL_Rect dst = { (int)floor(x + 0.5), (int)floor(y + 0.5), (int)floor(w + 0.5), (int)floor(h + 0.5) };
    int sw = sho_width();
    SDL_UpdateTexture(g_tex, &(SDL_Rect){ 0, 0, sw, SHO_H }, sho_framebuffer(), sw * 4);
    SDL_Rect src = { 0, 0, sw, SHO_H };
    if (g_filter == 2) {
        /* sharp-bilinear: enlarge by the whole number that fits with sharp pixels, then smooth the last step */
        int k = dst.h / SHO_H; if (k < 1) k = 1; if (k > 8) k = 8;
        if (!g_big || g_big_k != k || g_big_w < sw * k) {
            if (g_big) SDL_DestroyTexture(g_big);
            g_big = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, SHO_MAX_W * k, SHO_H * k);
            g_big_k = k; g_big_w = SHO_MAX_W * k;
            if (g_big) SDL_SetTextureScaleMode(g_big, SDL_ScaleModeLinear);
        }
        if (g_big && SDL_SetRenderTarget(g_ren, g_big) == 0) {
            SDL_SetTextureScaleMode(g_tex, SDL_ScaleModeNearest);
            SDL_Rect mid = { 0, 0, sw * k, SHO_H * k };
            SDL_RenderCopy(g_ren, g_tex, &src, &mid);
            SDL_SetRenderTarget(g_ren, NULL);
            SDL_RenderCopy(g_ren, g_big, &mid, &dst);
            SDL_RenderPresent(g_ren);
            return;
        }
    }
    SDL_SetTextureScaleMode(g_tex, g_filter == 1 ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    SDL_RenderCopy(g_ren, g_tex, &src, &dst);
    SDL_RenderPresent(g_ren);
}

static void set_fullscreen(int on) {
    if (!g_win) return;
    g_fullscreen = on;
    SDL_SetWindowFullscreen(g_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    ini_set_int("video", "fullscreen", on);
}
static void set_view_mode(int m) { g_view_mode = m; save_view_mode(); update_view_width(); update_title(); }
static void set_filter(int f) { g_filter = f % 3; ini_set_int("video", "filter", g_filter); update_title(); }
static void toggle_pause(void) {   /* in Mega Drive mode during a race this opens the pause menu */
    if (!g_paused) { g_paused = 1; g_pause_menu = sho_pause_begin(); }
    else { if (g_pause_menu) { sho_pause_end(); apply_sound_levels(1); } g_paused = 0; g_pause_menu = 0; }
}
static double display_refresh(void) {
    SDL_DisplayMode m;
    int d = g_win ? SDL_GetWindowDisplayIndex(g_win) : 0; if (d < 0) d = 0;
    if (SDL_GetCurrentDisplayMode(d, &m) != 0 || m.refresh_rate <= 0) return 60.0;
    if (m.refresh_rate == 59) return 59.94;
    return m.refresh_rate;
}

/* ------------------------------------------------------------------ */
/* keyboard                                                           */
/* ------------------------------------------------------------------ */
static void key_down(const SDL_KeyboardEvent *k) {
    if (k->repeat) return;
    SDL_Keycode s = k->keysym.sym;
    int ctrl = (k->keysym.mod & KMOD_CTRL) != 0, alt = (k->keysym.mod & KMOD_ALT) != 0;
    if (s == SDLK_F11 || ((s == SDLK_RETURN || s == SDLK_KP_ENTER) && alt)) set_fullscreen(!g_fullscreen);
    if (s == SDLK_ESCAPE && g_fullscreen) set_fullscreen(0);
    if (s == SDLK_p) toggle_pause();
    if (ctrl && s == SDLK_8) sho_md_cheat_upgrade();      /* development: fully upgraded bike */
    if (ctrl && s == SDLK_9) sho_md_cheat_money();        /* development: maximum money */
    if (ctrl && s >= SDLK_0 && s <= SDLK_7) { sho_md_dev_race(s - SDLK_0); ini_set_int("dev", "original_race", s - SDLK_0); }   /* development: Original Mode race select */
    if (s == SDLK_F3) reset_to_title();
    if (s == SDLK_F9) set_view_mode((g_view_mode + 1) % 3);
    if (s == SDLK_F8) set_filter(g_filter + 1);
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
/* one pad, read the same way whatever it is */
typedef struct { float lx, ly, lt, rt; int a, b, x, y, rb, start, back, du, dd, dl, dr; } padstate;
#define MAXPADS 4
static SDL_GameController *g_gc[MAXPADS];
static SDL_Joystick *g_js[MAXPADS];          /* pads SDL has no layout for: read as a plain joystick */

static void pads_rescan(void) {
    for (int i = 0; i < MAXPADS; i++) {
        if (g_gc[i] && !SDL_GameControllerGetAttached(g_gc[i])) { SDL_GameControllerClose(g_gc[i]); g_gc[i] = NULL; }
        if (g_js[i] && !SDL_JoystickGetAttached(g_js[i])) { SDL_JoystickClose(g_js[i]); g_js[i] = NULL; }
    }
    for (int d = 0; d < SDL_NumJoysticks(); d++) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(d);
        int known = 0;
        for (int i = 0; i < MAXPADS; i++) {
            if (g_gc[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_gc[i])) == id) known = 1;
            if (g_js[i] && SDL_JoystickInstanceID(g_js[i]) == id) known = 1;
        }
        if (known) continue;
        for (int i = 0; i < MAXPADS; i++) if (!g_gc[i] && !g_js[i]) {
            if (SDL_IsGameController(d)) g_gc[i] = SDL_GameControllerOpen(d); else g_js[i] = SDL_JoystickOpen(d);
            break;
        }
    }
    int n = 0; for (int i = 0; i < MAXPADS; i++) if (g_gc[i] || g_js[i]) n++;
    if (n != g_pad_count) { g_pad_count = n; update_title(); }
}
static float gc_axis(SDL_GameController *c, SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.0f; }
static int read_pad(int i, padstate *p) {
    memset(p, 0, sizeof *p);
    SDL_GameController *c = g_gc[i];
    if (c) {   /* SDL's Xbox-style layout (Xbox, PlayStation, Switch Pro, 8BitDo, GameSir... in its database) */
        p->lx = gc_axis(c, SDL_CONTROLLER_AXIS_LEFTX); p->ly = -gc_axis(c, SDL_CONTROLLER_AXIS_LEFTY);
        p->lt = gc_axis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT); p->rt = gc_axis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (p->lt < 0) p->lt = 0;
        if (p->rt < 0) p->rt = 0;
        p->a = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A); p->b = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B);
        p->x = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X); p->y = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y);
        p->rb = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        p->start = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START); p->back = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK);
        p->du = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP); p->dd = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        p->dl = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT); p->dr = SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        return 1;
    }
    SDL_Joystick *j = g_js[i];
    if (j) {   /* unknown pad: axis 0/1 = stick, buttons 0 A, 1 B, 2 X, 3 Y, 5 RB, 6 Back, 7 Start, hat = d-pad
                  ([pad] map= in shangon.ini changes the button numbers, as on the Mac) */
        static int map[7] = { 0, 1, 2, 3, 5, 6, 7 }, map_read;
        if (!map_read) { char m[96]; ini_str("pad", "map", "", m, sizeof m); int v[7];
                         if (m[0] && sscanf(m, "%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7) for (int k = 0; k < 7; k++) map[k] = v[k] - 1;
                         map_read = 1; }
        if (SDL_JoystickNumAxes(j) > 0) p->lx = SDL_JoystickGetAxis(j, 0) / 32767.0f;
        if (SDL_JoystickNumAxes(j) > 1) p->ly = -SDL_JoystickGetAxis(j, 1) / 32767.0f;
        int nb = SDL_JoystickNumButtons(j);
        int *out[7] = { &p->a, &p->b, &p->x, &p->y, &p->rb, &p->back, &p->start };
        for (int k = 0; k < 7; k++) if (map[k] >= 0 && map[k] < nb) *out[k] = SDL_JoystickGetButton(j, map[k]);
        if (SDL_JoystickNumHats(j) > 0) {
            Uint8 h = SDL_JoystickGetHat(j, 0);
            p->du = (h & SDL_HAT_UP) != 0; p->dd = (h & SDL_HAT_DOWN) != 0; p->dl = (h & SDL_HAT_LEFT) != 0; p->dr = (h & SDL_HAT_RIGHT) != 0;
        }
        return 1;
    }
    return 0;
}
/* one pad's controls into the game's input (the Windows build's XInput mapping) */
static void apply_pad(int slot, const padstate *p, sho_input *in, float *steer, float *thr, float *brk) {
    float sx = axis_dz(p->lx);
    if (p->dl) sx = -1;
    if (p->dr) sx = 1;
    if (fabsf(sx) > fabsf(*steer)) *steer = sx;
    float rt = p->rt, lt = p->lt;
    if (g_swap_pedals) { float t = rt; rt = lt; lt = t; }
    if (rt < 0.04f) rt = 0;
    if (lt < 0.04f) lt = 0;
    if (p->a) in->pad_a = 1;                              /* throttle, or enter on initials */
    /* directions for menus / initials: d-pad or left stick (with hysteresis) */
    static int stick_dir[MAXPADS][4];   /* [pad][up,down,left,right] */
    float v[4] = { p->ly, -p->ly, -p->lx, p->lx };
    for (int d = 0; d < 4; d++) stick_dir[slot][d] = stick_dir[slot][d] ? v[d] > 0.35f : v[d] > 0.6f;
    if (p->du || stick_dir[slot][0]) in->nav_up = 1;
    if (p->dd || stick_dir[slot][1]) in->nav_down = 1;
    if (p->dl || stick_dir[slot][2]) in->nav_left = 1;
    if (p->dr || stick_dir[slot][3]) in->nav_right = 1;
    if (p->b || p->x) lt = 1;
    if (p->b) in->pad_b = 1;                              /* back on the Original Mode screens */
    if (rt > *thr) *thr = rt;
    if (lt > *brk) *brk = lt;
    if (p->rb || p->y) in->turbo = 1;
    if (p->start) in->start = 1;                          /* Start / Menu */
    if (p->back) in->coin = 1;                            /* Back / View / Select */
}

static void read_input(sho_input *in, float dt) {
    memset(in, 0, sizeof *in);
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    int focused = g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS);
    SDL_Keymod mod = SDL_GetModState();
    int kl = focused && (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]), kr = focused && (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]);
    float target = (float)(kr - kl), rate = (target == 0.0f) ? 6.0f : 3.5f;   /* keyboard: handlebar ramps */
    if (g_kb_steer < target) { g_kb_steer += rate * dt; if (g_kb_steer > target) g_kb_steer = target; }
    else if (g_kb_steer > target) { g_kb_steer -= rate * dt; if (g_kb_steer < target) g_kb_steer = target; }
    float steer = g_kb_steer, thr = 0, brk = 0;
    if (focused && (sho_md_overlay_active() || g_pause_menu)) {   /* Original Mode screens: arrows move, Z/Space select, Backspace/X back */
        in->nav_up = k[SDL_SCANCODE_UP]; in->nav_down = k[SDL_SCANCODE_DOWN]; in->nav_left = k[SDL_SCANCODE_LEFT]; in->nav_right = k[SDL_SCANCODE_RIGHT];
        in->pad_a = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE]; in->pad_b = k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_X];
    }
    if (focused) {
        int digits_free = !(mod & KMOD_CTRL);   /* Ctrl+digits are the development keys */
        if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) thr = 1;
        if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_X] || k[SDL_SCANCODE_S]) brk = 1;
        in->turbo = k[SDL_SCANCODE_SPACE] || (mod & KMOD_SHIFT);
        in->coin = (digits_free && k[SDL_SCANCODE_5]) || k[SDL_SCANCODE_C];
        in->start = (digits_free && k[SDL_SCANCODE_1]) || (k[SDL_SCANCODE_RETURN] && !(mod & KMOD_ALT)) || k[SDL_SCANCODE_KP_ENTER];
        in->test = k[SDL_SCANCODE_F2];
        in->service = digits_free && k[SDL_SCANCODE_9];
    }
    for (int i = 0; i < MAXPADS; i++) {
        padstate p;
        if (read_pad(i, &p)) apply_pad(i, &p, in, &steer, &thr, &brk);
    }
    in->steer = steer; in->throttle = thr; in->brake = brk;
}

/* ------------------------------------------------------------------ */
/* audio: SDL audio callback pulling from a ring buffer               */
/* ------------------------------------------------------------------ */
#define RING 32768                     /* stereo frames, power of two */
static int16_t g_ring[RING * 2];
static _Atomic unsigned long long g_wr, g_rd;   /* frames written / played */
static SDL_AudioDeviceID g_audio;
static void audio_cb(void *u, Uint8 *stream, int len) {
    (void)u;
    int16_t *out = (int16_t *)stream; int n = len / 4;
    unsigned long long r = atomic_load(&g_rd), w = atomic_load(&g_wr);
    for (int i = 0; i < n; i++) {
        if (r < w) { unsigned k = (unsigned)(r & (RING - 1)); out[2 * i] = g_ring[2 * k]; out[2 * i + 1] = g_ring[2 * k + 1]; r++; }
        else out[2 * i] = out[2 * i + 1] = 0;   /* underrun: silence */
    }
    atomic_store(&g_rd, r);
}
static int audio_init(void) {
    SDL_AudioSpec want, have; memset(&want, 0, sizeof want);
    want.freq = AUDIO_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio) return 0;
    SDL_PauseAudioDevice(g_audio, 0);
    return 1;
}
static long audio_queued(void) { unsigned long long w = atomic_load(&g_wr), r = atomic_load(&g_rd); return w > r ? (long)(w - r) : 0; }
static void audio_submit(const int16_t *samples, int count) {
    if (audio_queued() + count > RING - 16) return;          /* full: drop */
    unsigned long long w = atomic_load(&g_wr);
    for (int i = 0; i < count; i++) { unsigned k = (unsigned)((w + (unsigned long long)i) & (RING - 1)); g_ring[2 * k] = samples[2 * i]; g_ring[2 * k + 1] = samples[2 * i + 1]; }
    atomic_store(&g_wr, w + (unsigned long long)count);
}
static void audio_shutdown(void) { if (g_audio) { SDL_CloseAudioDevice(g_audio); g_audio = 0; } }

/* ------------------------------------------------------------------ */
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void wait_until(double t) {   /* sleep most of the way, then spin for precision */
    for (;;) { double d = t - now_s(); if (d <= 0) return; if (d > 0.002) usleep((useconds_t)((d - 0.0015) * 1e6)); }
}

/* Verify and load the ROM set (command-line path, then shangon.zip / folders in the game folder).
 * Used by the launcher's status line and before the game starts. */
static char g_cmd_rom[PATHN], g_rom_err[2048];
static int rom_check(char *msg, int msglen, char *detail, int detlen) {
    const char *candidates[] = { "shangon.zip", "roms/shangon.zip", "shangon", "roms/shangon" };
    char path[PATHN], err[2048], first_err[2048] = "", first_path[PATHN] = "";
    for (int i = -1; i < 4; i++) {
        if (i < 0) { if (!g_cmd_rom[0]) continue; snprintf(path, sizeof path, "%s", g_cmd_rom); }
        else path_join(path, g_dir, candidates[i]);
        int exists = file_exists(path);
        if (exists && sho_load_roms(path, err, sizeof err) == 0) {
            const char *name = strrchr(path, '/'); name = name ? name + 1 : path;
            snprintf(msg, (size_t)msglen, "ROM OK: %s verified", name);
            snprintf(detail, (size_t)detlen, "All 25 files match Super Hang-On (MAME 'shangon')");
            g_rom_err[0] = 0;
            return 1;
        }
        if (exists && !first_err[0]) { snprintf(first_err, sizeof first_err, "%s", err); snprintf(first_path, sizeof first_path, "%s", path); }
    }
    if (!first_err[0]) {
        snprintf(msg, (size_t)msglen, "ROM missing: shangon.zip not found");
        snprintf(detail, (size_t)detlen, "Put shangon.zip in the game folder");
        snprintf(g_rom_err, sizeof g_rom_err, "Looked in: %s", g_dir);
        return 0;
    }
    int missing = 0, bad = 0; const char *firstline = NULL;
    for (const char *p = first_err; (p = strchr(p, '(')) != NULL; p++) {
        if (!strncmp(p, "(missing)", 9)) missing++;
        else if (!strncmp(p, "(bad checksum)", 14)) bad++;
    }
    firstline = strstr(first_err, "\n\n"); firstline = firstline ? firstline + 2 : first_err;
    const char *name = strrchr(first_path, '/'); name = name ? name + 1 : first_path;
    if (missing == 25) snprintf(msg, (size_t)msglen, "ROM problem: %s is not the Super Hang-On set", name);
    else snprintf(msg, (size_t)msglen, "ROM problem: %s - %d missing, %d wrong", name, missing, bad);
    char line[128]; snprintf(line, sizeof line, "%s", firstline); char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    snprintf(detail, (size_t)detlen, "First issue: %s", line);
    snprintf(g_rom_err, sizeof g_rom_err, "%s", first_err);
    return 0;
}

/* Mega Drive ROM for Original Mode: [megadrive] rom= in the ini, else any .md/.gen/.bin/.zip in the game folder
   that is Super Hang-On (World) (Rev A). Returns 1 and the path if found. */
static int find_md_rom(char *out, size_t n) {
    char p[PATHN], msg[128];
    ini_str("megadrive", "rom", "", p, sizeof p);
    if (p[0]) { char full[PATHN];
                if (p[0] == '~') { const char *h = getenv("HOME"); snprintf(full, sizeof full, "%s%s", h ? h : "", p + 1); snprintf(p, sizeof p, "%s", full); }
                else if (p[0] != '/') { path_join(full, g_dir, p); snprintf(p, sizeof p, "%s", full); }
                if (mdgfx_check(p, msg, sizeof msg)) { snprintf(out, n, "%s", p); return 1; } }
    static const char *exts[] = { ".md", ".gen", ".bin", ".zip" };
    for (int i = 0; i < 4; i++) {
        DIR *d = opendir(g_dir); if (!d) return 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            const char *nm = de->d_name; size_t l = strlen(nm), el = strlen(exts[i]);
            if (nm[0] == '.' || l <= el || strcasecmp(nm + l - el, exts[i]) != 0 || !strcasecmp(nm, "shangon.zip")) continue;
            path_join(p, g_dir, nm);
            struct stat st; if (stat(p, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 8 * 1024 * 1024) continue;
            if (mdgfx_check(p, msg, sizeof msg)) { closedir(d); snprintf(out, n, "%s", p); return 1; }
        }
        closedir(d);
    }
    return 0;
}

/* back to the title screen (F3, or EXIT on the pause menu) */
static void reset_to_title(void) {
    sho_md_before_reset();
    save_nvram(); sho_reset(); load_nvram(); apply_game_settings();
    sho_md_configure(g_md_present && ini_int("megadrive", "enhancements", 0), g_md_present && ini_int("megadrive", "controls", 0));
}
/* pause menu sound levels (0-10), kept per soundtrack: Regular and Mega Drive Music start at music 10 /
   effects 10, the Arrange Soundtrack at music 6 / effects 10 */
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
    char p[PATHN], info[128] = "";
    if (find_md_rom(p, sizeof p)) {
        const char *nm = strrchr(p, '/'); nm = nm ? nm + 1 : p;
        mdgfx_check(p, info, sizeof info);
        snprintf(msg, (size_t)msglen, "Mega Drive ROM OK: %s verified", nm);
        snprintf(detail, (size_t)detlen, "Super Hang-On (World) - Mega Drive options available");
        return 1;
    }
    snprintf(msg, (size_t)msglen, "No Mega Drive ROM found");
    snprintf(detail, (size_t)detlen, "Add the Super Hang-On (World) Mega Drive ROM to enable its options");
    return 0;
}
static int md_rom_present(void) { char p[PATHN]; return find_md_rom(p, sizeof p); }

/* Mega Drive capture runs on a worker thread while the launcher is open (the arcade has not booted yet,
   so the shared 68000 core is free); big stack, as on the Mac */
static char g_md_path[PATHN]; static pthread_t g_md_thread; static int g_md_thread_on, g_md_ok;
static volatile int g_md_done;   /* the capture thread has finished (it can then be joined at once) */
static void *md_capture_thread(void *p) { char err[256]; (void)p; g_md_ok = mdgfx_capture(g_md_path, err, sizeof err); g_md_done = 1; return NULL; }
static void start_md_capture(void) {
    g_md_done = 0;
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64u << 20);
    g_md_thread_on = pthread_create(&g_md_thread, &a, md_capture_thread, NULL) == 0;
    pthread_attr_destroy(&a);
}

/* loading after PLAY: which arranged MP3s are there (their share of the launcher's loading bar) */
static void plan_loading(void) {
    int have[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) {
        char n[96], mp[PATHN]; snprintf(n, sizeof n, "music/%s.mp3", sho_music_tracks[i].title); path_join(mp, g_dir, n);
        have[i] = ini_int("audio", "external_music", 1) && file_exists(mp);
    }
    lc_load_begin(have, g_md_thread_on && !g_md_done);
}

/* The game folder: the folder that holds the program (as on Windows and the Mac). If it can't be written
   (installed under /opt or /usr), $XDG_DATA_HOME or ~/.local/share/super-hang-on-ultimate is used. */
static void find_game_dir(void) {
    char exe[PATHN]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) { exe[n] = 0; char *sl = strrchr(exe, '/'); if (sl) *sl = 0; snprintf(g_dir, sizeof g_dir, "%s", exe); }
    else snprintf(g_dir, sizeof g_dir, ".");
    char probe[PATHN]; path_join(probe, g_dir, "shangon.zip");
    if (access(g_dir, W_OK) != 0 && !file_exists(probe)) {
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        char base[PATHN];
        if (x && x[0]) snprintf(base, sizeof base, "%s", x); else snprintf(base, sizeof base, "%s/.local/share", h ? h : ".");
        mkdir(base, 0755);
        snprintf(g_dir, sizeof g_dir, "%s/super-hang-on-ultimate", base);
        mkdir(g_dir, 0755);
    }
}

/* the window starts at 80% of the screen height, in the shape the picture will have */
static int create_window(void) {
    SDL_DisplayMode dm; double sw = 1920, sh = 1080;
    SDL_Rect usable;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { sw = dm.w; sh = dm.h; }
    double uw = sw, uh = sh;
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) { uw = usable.w; uh = usable.h; }
    double a = g_view_mode == VIEW_WIDE ? 16.0 / 9.0 : g_view_mode == VIEW_43 ? 4.0 / 3.0 : sw / sh;
    double h = floor(uh * 0.8), w = floor(h * a);
    if (w > uw * 0.9) { w = floor(uw * 0.9); h = floor(w / a); }
    g_win = SDL_CreateWindow("Super Hang-On Ultimate", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)w, (int)h,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win) return 0;
    SDL_SetWindowMinimumSize(g_win, SHO_W, SHO_H);
    int vs = ini_int("video", "vsync", 1);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE | (vs ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);   /* no GPU driver: still playable */
    if (!g_ren) return 0;
    return 1;
}

static int run(int argc, char **argv) {
    find_game_dir();
    path_join(g_ini, g_dir, "shangon.ini"); path_join(g_nv, g_dir, "shangon.nv");
    ini_set_path(g_ini);
    write_default_ini();
    char s[64];
    g_fullscreen = ini_int("video", "fullscreen", 1);
    g_view_mode = read_view_mode();
    g_filter = ini_int("video", "filter", 2) % 3; if (g_filter < 0) g_filter = 2;
    g_stretch = ini_int("video", "stretch", 0);
    g_deadzone = ini_int("input", "deadzone", 15) / 100.0f;
    g_steer_curve = ini_int("input", "steer_curve", 100) / 100.0f;
    g_swap_pedals = ini_int("input", "swap_pedals", 0);
    ini_str("advanced", "cpu", "recompiled", s, sizeof s); sho_recomp = strcasecmp(s, "interpreter") != 0;
    int play_now = 0;
    for (int i = 1; i < argc; i++) {   /* -window, -fullscreen, -interpreter, -play, or a ROM path */
        if (!strcmp(argv[i], "-window")) g_fullscreen = 0;
        else if (!strcmp(argv[i], "-fullscreen")) g_fullscreen = 1;
        else if (!strcmp(argv[i], "-interpreter")) sho_recomp = 0;
        else if (!strcmp(argv[i], "-play")) play_now = 1;
        else if (argv[i][0] != '-') snprintf(g_cmd_rom, sizeof g_cmd_rom, "%s", argv[i]);
    }

    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "Super Hang-On Ultimate: SDL could not start: %s\n", SDL_GetError());
        return 1;
    }
    {   char db[PATHN]; path_join(db, g_dir, "gamecontrollerdb.txt");   /* extra pad layouts, if the player adds the community list */
        if (file_exists(db)) SDL_GameControllerAddMappingsFromFile(db); }
    if (!create_window()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Hang-On Ultimate", SDL_GetError(), NULL);
        SDL_Quit(); return 1;
    }

    int show_launcher = ini_int("launcher", "show", 1) && !play_now;
    if (ini_int("megadrive", "enhancements", 0) && find_md_rom(g_md_path, sizeof g_md_path)) start_md_capture();
    if (show_launcher) {
        launcher_settings ls; memset(&ls, 0, sizeof ls);
        ls.demo_sounds = ini_int("game", "demo_sounds", 1);
        ls.difficulty = ini_int("game", "difficulty", 1) & 3;
        ls.time_adj = ini_int("game", "time", 1) & 3;
        ls.view = g_view_mode;
        ls.arrange_music = ini_int("audio", "external_music", 1) != 0;
        ls.md_enhancements = ini_int("megadrive", "enhancements", 0) != 0;
        ls.md_controls = ini_int("megadrive", "controls", 0) != 0;
        ls.md_music = ini_int("megadrive", "music", 0) != 0;
        ls.game_dir = g_dir;
        {   /* how many arranged tracks are in the music folder */
            static const char *titles[4] = { "Outride a Crisis", "Sprinter", "Winning Run", "Hard Road" };
            int found = 0; char mp[PATHN];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music/%s.mp3", titles[i]); path_join(mp, g_dir, n);
                                          if (file_exists(mp)) found++; }
            if (found == 4) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: all 4 tracks found in the music folder");
            else if (found) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: %d of 4 tracks found (others play regular)", found);
            else snprintf(ls.music_note, sizeof ls.music_note, "Arrange tracks not found: regular music will play");
        }
        ls.md_check = md_rom_check;
        int go = launcher_run(g_win, g_ren, &ls, rom_check);
        if (go != 1) { if (g_md_thread_on) pthread_join(g_md_thread, NULL); SDL_Quit(); return 0; }   /* closed / Esc: quit */
        ini_set_int("audio", "external_music", ls.arrange_music);
        ini_set_int("game", "demo_sounds", ls.demo_sounds);
        ini_set_int("game", "difficulty", ls.difficulty);
        ini_set_int("game", "time", ls.time_adj);
        g_view_mode = ls.view; save_view_mode();
        ini_set_int("megadrive", "enhancements", ls.md_enhancements);
        ini_set_int("megadrive", "controls", ls.md_controls);
        ini_set_int("megadrive", "music", ls.md_music);
        if (ls.md_enhancements && !g_md_thread_on && find_md_rom(g_md_path, sizeof g_md_path)) start_md_capture();
    }
    plan_loading();
    lc_load_step(0, 0, "CHECKING THE ARCADE ROM");
    double load_t0 = now_s();
    char rmsg[256], detail[256];
    if (!rom_check(rmsg, sizeof rmsg, detail, sizeof detail)) {
        launcher_close(g_ren);
        if (g_md_thread_on) pthread_join(g_md_thread, NULL);
        char box[4096];
        snprintf(box, sizeof box, "%s\n%s\n\n%s\n\nPut your MAME 'shangon.zip' (Super Hang-On, sitdown/upright, unprotected) in:\n%s", rmsg, detail, g_rom_err, g_dir);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Hang-On Ultimate", box, g_win);
        SDL_Quit();
        return 1;
    }
    if (ini_int("audio", "external_music", 1)) {
        char music_dir[PATHN], music_err[512];
        path_join(music_dir, g_dir, "music");
        sho_music_load_progress = lc_music_progress;
        sho_load_music(music_dir, AUDIO_RATE, music_err, sizeof music_err);
        sho_music_load_progress = NULL;
    }
    g_md_present = md_rom_present();   /* the Mega Drive options only apply with the Mega Drive ROM there */
    load_sound_levels();
    apply_sound_levels(0);
    if (g_md_thread_on) {   /* Original Mode's screens and courses from the Mega Drive ROM: usually ready by now */
        if (getenv("LOADDBG")) fprintf(stderr, "md wait: done %d progress %d\n", g_md_done, mdgfx_progress);
        while (!g_md_done) { lc_load_step(2, 0, "PREPARING ORIGINAL MODE"); SDL_Delay(5); }
        pthread_join(g_md_thread, NULL); g_md_thread_on = 0;
    }
    else if (ini_int("megadrive", "enhancements", 0)) {   /* Original Mode graphics/courses from the Mega Drive ROM, before the arcade boots */
        char merr[256];
        if (find_md_rom(g_md_path, sizeof g_md_path)) mdgfx_capture(g_md_path, merr, sizeof merr);
    }
    sho_reset();
    load_nvram();
    apply_game_settings();
    {   char sav[PATHN]; path_join(sav, g_dir, "shangon_original.sav"); sho_md_set_save_path(sav);
        sho_md_configure(g_md_present && ini_int("megadrive", "enhancements", 0), g_md_present && ini_int("megadrive", "controls", 0));
        sho_md_dev_race(ini_int("dev", "original_race", 0));
        sho_md_set_music(g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0));   /* the Mega Drive's own sound driver */
        if (sho_md_music_active()) sho_silence_music(); }                      /* the arcade's songs off: the Mega Drive's play instead */

    lc_load_step(3, 1.0f, "STARTING THE GAME");
    if (getenv("LOADDBG")) fprintf(stderr, "loading took %.2f s\n", now_s() - load_t0);
    launcher_close(g_ren);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SHO_MAX_W, SHO_H);
    if (!g_tex) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Hang-On Ultimate", SDL_GetError(), g_win); SDL_Quit(); return 1; }
    if (g_fullscreen) set_fullscreen(1);
    update_view_width();
    update_title();
    SDL_DisableScreenSaver();

    int have_audio = audio_init();
    int16_t samples[2 * 1100];
    int frames_since_save = 0, refresh_check = 0;
    double refresh = display_refresh();
    if (have_audio) { memset(samples, 0, sizeof samples); audio_submit(samples, 1100); audio_submit(samples, 1100); audio_submit(samples, 1000); }
    double acc = 0, next_frame = now_s(), audio_frac = 0, last_present = now_s(), avg_iv = 1.0 / refresh;
    int vsync_req = ini_int("video", "vsync", 1), vsync_broken = 0;
    double mouse_still = now_s(); int cursor_hidden = 0;
    pads_rescan();

    while (g_running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: g_running = 0; break;
            case SDL_KEYDOWN: key_down(&e.key); break;
            case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED: pads_rescan(); break;
            case SDL_MOUSEMOTION: mouse_still = now_s(); if (cursor_hidden) { SDL_ShowCursor(SDL_ENABLE); cursor_hidden = 0; } break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) refresh = display_refresh();
                break;
            }
        }
        if (!g_running) break;
        if (g_fullscreen && !cursor_hidden && now_s() - mouse_still > 2.0) { SDL_ShowCursor(SDL_DISABLE); cursor_hidden = 1; }   /* fullscreen: hide a still pointer */
        if (!g_fullscreen && cursor_hidden) { SDL_ShowCursor(SDL_ENABLE); cursor_hidden = 0; }
        if (++refresh_check >= 120) { refresh_check = 0; refresh = display_refresh(); }
        update_view_width();

        /* ---- how many game frames this iteration ----
         * With vsync, one loop iteration per display refresh (the present waits for it). If the refresh is
         * (close to) a multiple of 60 Hz, lock the game to it (k refreshes per game frame) - perfectly even
         * motion. Otherwise distribute 60.05 Hz game frames over refreshes. Without working vsync (presents
         * return at once, e.g. a compositor that ignores it, or vsync=0): pace by the clock. */
        double period;
        int k = (int)(refresh / 60.0 + 0.5); if (k < 1) k = 1;
        int frames = 0;
        double t = now_s(), iv = t - last_present;
        /* vsync that doesn't wait (presents ~instantly for a second) is set aside until the display changes */
        static int vs_checks; static double vs_refresh;
        if (refresh != vs_refresh) { vs_refresh = refresh; vs_checks = 0; vsync_broken = 0; avg_iv = 1.0 / refresh; }
        int vsync_ok = vsync_req && !vsync_broken;
        if (vsync_ok && ++vs_checks > 60 && avg_iv < 0.5 / refresh) vsync_broken = 1;
        if (vsync_ok) {
            int ticks = (int)(iv * refresh + 0.5); if (ticks < 1) ticks = 1; if (ticks > 8) ticks = 8;
            int locked = fabs(refresh - 60.0 * k) <= 0.012 * 60.0 * k;
            period = locked ? (double)k / refresh : 1.0 / SHO_FPS;
            acc += ticks / refresh;
            while (acc >= period - 1e-9 && frames < 4) { frames++; acc -= period; }
            if (acc > period * 4) acc = 0;
            next_frame = t;
        } else {
            period = 1.0 / SHO_FPS;
            if (t < next_frame) { wait_until(next_frame); t = now_s(); }
            frames = 1; next_frame += period;
            if (t - next_frame > 0.1) next_frame = t;
        }

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
                if (sho_md_exit_requested()) g_running = 0;
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
        Uint32 wf = SDL_GetWindowFlags(g_win);
        if (wf & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) { SDL_Delay(5); last_present = now_s(); continue; }   /* nothing to show */
        present();
        double tp = now_s(); avg_iv = avg_iv * 0.95 + (tp - last_present) * 0.05; last_present = tp;
    }

    save_nvram();
    sho_music_unload();
    audio_shutdown();
    for (int i = 0; i < MAXPADS; i++) { if (g_gc[i]) SDL_GameControllerClose(g_gc[i]); if (g_js[i]) SDL_JoystickClose(g_js[i]); }
    if (g_big) SDL_DestroyTexture(g_big);
    if (g_tex) SDL_DestroyTexture(g_tex);
    SDL_DestroyRenderer(g_ren); SDL_DestroyWindow(g_win);
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv) { return run(argc, argv); }
