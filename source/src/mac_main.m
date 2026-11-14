/* Super Hang-On – native macOS frontend (the Mac twin of win32_main.c).
 *  - Recompiled game code (C) by default; the original-CPU interpreter remains selectable
 *  - "Fit screen" view (default): the picture takes the shape of the screen, e.g. 1710x1112 or 1470x956
 *    MacBook screens (and the area below the notch in fullscreen), with the HUD at its edges as in 16:9;
 *    or 16:9 / 4:3 as on Windows. Cmd+1/2/3 (or F9) switch.
 *  - Metal presentation: display-locked smooth pacing, sharp-bilinear scaling
 *  - Native macOS fullscreen (default) / window: Ctrl+Cmd+F, Cmd+Return or F11; Cmd+Q quits
 *  - Xbox-style gamepads (GameController framework) + keyboard; CoreAudio output with drift correction
 *  - Battery-backed RAM (high scores) saved to shangon.nv; settings in shangon.ini
 *  - Optional Mega Drive Enhancements (Original Mode, saved to shangon_original.sav) and Mega Drive controls
 * The game folder (ROMs, music, settings, saves) is the folder that holds SuperHangOn.app.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <GameController/GameController.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreVideo/CoreVideo.h>
#import <IOKit/hid/IOHIDManager.h>
#import <IOKit/hid/IOHIDUsageTables.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "sho_core.h"
#include "sho_music.h"
#include "sho_md.h"
#include "sho_mdgfx.h"
#include "mac_metal.h"
#include "launcher_core.h"
#include "sho_views.h"
#include "sho_pace.h"
#include "sho_controls.h"
#include "sho_version.h"
#include "ini_file.h"

extern int sho_recomp;

#define AUDIO_RATE 48000
#define AUDIO_TARGET 3200            /* samples queued ahead (~67 ms) */
#define PATHN 1024

/* macOS virtual key codes (Carbon's kVK_* values) */
enum {
    K_A = 0x00, K_S = 0x01, K_D = 0x02, K_Z = 0x06, K_X = 0x07, K_C = 0x08, K_W = 0x0D,
    K_1 = 0x12, K_2 = 0x13, K_3 = 0x14, K_4 = 0x15, K_6 = 0x16, K_5 = 0x17, K_9 = 0x19, K_7 = 0x1A, K_8 = 0x1C, K_0 = 0x1D,
    K_P = 0x23, K_RETURN = 0x24, K_SPACE = 0x31, K_BACKSPACE = 0x33, K_ESCAPE = 0x35, K_KP_ENTER = 0x4C,
    K_F2 = 0x78, K_F3 = 0x63, K_F8 = 0x64, K_F9 = 0x65, K_F11 = 0x67,
    K_LEFT = 0x7B, K_RIGHT = 0x7C, K_DOWN = 0x7D, K_UP = 0x7E,
    K_SHIFT = 0x38, K_TAB = 0x30, K_FWD_DELETE = 0x75
};

static NSWindow *g_win;
static NSView *g_view;
static CAMetalLayer *g_layer;
static int g_md_present, g_pause_menu, g_in_launcher, g_quit;
static void reset_to_title(void);
static void apply_sound_levels(int save);
static double now_s(void);
static int g_fullscreen = 1, g_running = 1, g_paused = 0, g_filter = 2, g_stretch = 0, g_view_mode = VIEW_FIT, g_have_metal = 0;
static char g_dir[PATHN], g_ini[PATHN], g_nv[PATHN];   /* g_ini is handed to ini_set_path() */
static const char *filter_names[3] = { "sharp", "smooth", "sharp-bilinear" };
static const char *view_name(int m) { return m == VIEW_FIT ? "fit screen" : sho_shapes[(m - 1) % SHO_NSHAPES].name; }

/* input */
static uint8_t g_keys[128];
static int g_shift;
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
          "fullscreen=1      ; start in fullscreen (Ctrl+Cmd+F / Cmd+Return toggles)\n"
          "view=fit          ; fit = the screen's own shape (MacBook screens), or 16:9, 18:9, 19:9, 20:9, 21:9, 4:3 (Cmd+1..7 or F9)\n"
          "filter=2          ; 0 sharp, 1 smooth, 2 sharp-bilinear (Cmd+4/5/6 or F8)\n"
          "stretch=0         ; 1 = stretch to fill the screen (distorts the picture)\n"
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
static int read_view_mode(void) {
    char s[32]; ini_str("video", "view", "", s, sizeof s);
    if (!strcasecmp(s, "fit") || !strcasecmp(s, "auto")) return VIEW_FIT;
    if (!strcasecmp(s, "wide")) return VIEW_WIDE;
    int k = sho_shape_find(s); if (k >= 0) return 1 + k;
    ini_str("video", "aspect", "", s, sizeof s); k = sho_shape_find(s); if (k >= 0) return 1 + k;   /* an ini from the Windows build */
    return ini_int("video", "widescreen", 1) ? VIEW_FIT : VIEW_43;
}
static void save_view_mode(void) { ini_set("video", "view", g_view_mode == VIEW_FIT ? "fit" : sho_shapes[g_view_mode - 1].name); }

static void apply_game_settings(void) {
    int demo = ini_int("game", "demo_sounds", 1), diff = ini_int("game", "difficulty", 1), tim = ini_int("game", "time", 1);
    int free_play = ini_int("game", "free_play", 0);
    static const uint8_t dmap[4] = { 0x04, 0x06, 0x02, 0x00 }, tmap[4] = { 0x10, 0x18, 0x08, 0x00 };
    uint8_t dsw = 0xe0 | (demo ? 0 : 1) | dmap[diff & 3] | tmap[tim & 3];
    sho_set_dips(free_play ? 0x00 : 0xff, dsw);   /* coinage: both nibbles 0 = free play */
    /* the view width itself follows the window every frame (update_view_width) */
}

static void update_title(void) {
    if (!g_win) return;
    char t[200];
    extern int g_pad_count_for_title;
    char pads[24] = ""; if (g_pad_count_for_title >= 0) snprintf(pads, sizeof pads, ", pads: %d", g_pad_count_for_title);
    snprintf(t, sizeof t, "Super Hang-On Ultimate  [%s %d, %s, %s%s%s%s%s%s]", view_name(g_view_mode), sho_width(), filter_names[g_filter],
             g_have_metal ? "Metal" : "no Metal", sho_recomp ? ", C" : ", interpreter",
             g_md_present && ini_int("megadrive", "enhancements", 0) ? ", MD Enhancements" : "", g_md_present && ini_int("megadrive", "controls", 0) ? ", MD Controls" : "",
             g_md_present && ini_int("megadrive", "enhancements", 0) && ini_int("megadrive", "music", 0) ? ", MD Music" : "", pads);
    g_win.title = [NSString stringWithUTF8String:t];
}

/* ------------------------------------------------------------------ */
/* display                                                            */
/* ------------------------------------------------------------------ */
/* The usable part of the view, in points (top-left origin): in fullscreen on a MacBook with a notch, macOS
   normally keeps the window below the notch already; if the window does reach the top of such a screen, the
   strip beside the notch is left out so the HUD is never under the camera. */
static NSRect usable_rect(void) {
    NSRect b = g_view.bounds;
    if (g_fullscreen && g_win.screen) {
        if (@available(macOS 12.0, *)) {
            CGFloat top = g_win.screen.safeAreaInsets.top;
            NSRect wf = g_win.frame, sf = g_win.screen.frame;
            if (top > 0 && NSMaxY(wf) > NSMaxY(sf) - top + 0.5) {   /* window under the notch strip */
                CGFloat cut = NSMaxY(wf) - (NSMaxY(sf) - top);
                if (cut > 0 && cut < b.size.height / 2) { b.origin.y += cut; b.size.height -= cut; }
            }
        }
    }
    return b;
}

/* the game picture's width: in "fit screen" it takes the shape of the usable area (e.g. 370 pixels on a
   1710x1112 or 1470x956 desktop, ~380 below the notch in fullscreen); 16:9 = 428, 4:3 = 320 */
static void update_view_width(void) {   /* (also while paused: the core draws the kept frame again at the new width) */
    sho_pause_set_display(g_view_mode == VIEW_FIT ? "FIT" : sho_shapes[g_view_mode - 1].name);   /* (first: a new width redraws the pause menu) */
    int w = SHO_W;
    if (g_view_mode != VIEW_FIT) w = sho_shapes[g_view_mode - 1].width;
    else {
        NSRect u = usable_rect();
        w = u.size.height >= 1 ? sho_view_width_for_aspect(u.size.width / u.size.height) : SHO_WIDE_W;
    }
    if (w != sho_width()) { sho_set_view_width(w); update_title(); }
}

/* display aspect of the game image: the arcade monitor is 4:3 for 320 pixels; wider views keep that pixel shape */
static double image_aspect(void) {   /* a named shape uses its own aspect (fills a screen of that shape exactly) */
    if (g_stretch) return 0.0;
    return g_view_mode == VIEW_FIT ? (4.0 / 3.0) * sho_width() / (double)SHO_W : sho_shapes[g_view_mode - 1].aspect;
}

static int present(void) {
    if (!g_have_metal || !g_view) return 0;
    if (g_win.miniaturized || !(g_win.occlusionState & NSWindowOcclusionStateVisible)) return 0;   /* nothing to show: don't wait for a drawable */
    CGFloat sc = g_win.backingScaleFactor > 0 ? g_win.backingScaleFactor : 1.0;
    NSRect b = g_view.bounds;
    CGSize ds = CGSizeMake(floor(b.size.width * sc), floor(b.size.height * sc));
    if (ds.width < 1 || ds.height < 1) return 0;
    if (g_layer.contentsScale != sc) g_layer.contentsScale = sc;
    if (!CGSizeEqualToSize(g_layer.drawableSize, ds)) g_layer.drawableSize = ds;
    NSRect u = usable_rect();
    double ux = u.origin.x * sc, uy = u.origin.y * sc, uw = u.size.width * sc, uh = u.size.height * sc;
    double x = ux, y = uy, w = uw, h = uh, a = image_aspect();
    /* letterbox to the image aspect; a difference under 1.5% (the fit view's whole-pixel rounding) fills exactly */
    if (a > 0 && fabs((uw / uh) / a - 1.0) > 0.015) {
        if (uw / uh > a) { w = uh * a; x = ux + (uw - w) * 0.5; } else { h = uw / a; y = uy + (uh - h) * 0.5; }
    }
    x = floor(x + 0.5); y = floor(y + 0.5); w = floor(w + 0.5); h = floor(h + 0.5);
    return mtl_present(sho_framebuffer(), sho_width(), SHO_H, g_filter, x, y, w, h);
}

/* the launcher (settings screen, loading bar): its 1920x1080 picture, 16:9 with black bars, sharp-bilinear */
static int present_launcher(void) {
    if (!g_have_metal || !g_view) return 0;
    if (g_win.miniaturized || !(g_win.occlusionState & NSWindowOcclusionStateVisible)) return 0;
    CGFloat sc = g_win.backingScaleFactor > 0 ? g_win.backingScaleFactor : 1.0;
    NSRect b = g_view.bounds;
    CGSize ds = CGSizeMake(floor(b.size.width * sc), floor(b.size.height * sc));
    if (ds.width < 1 || ds.height < 1) return 0;
    if (g_layer.contentsScale != sc) g_layer.contentsScale = sc;
    if (!CGSizeEqualToSize(g_layer.drawableSize, ds)) g_layer.drawableSize = ds;
    NSRect u = usable_rect();
    int x, y, w, h;
    lc_view_rect((int)(u.size.width * sc), (int)(u.size.height * sc), &x, &y, &w, &h);
    const uint32_t *img = lc_image();
    if (!img) return 0;
    return mtl_present(img, LC_IW, LC_IH, 2, floor(u.origin.x * sc + 0.5) + x, floor(u.origin.y * sc + 0.5) + y, w, h);
}
static void launcher_mouse(NSEvent *e) {   /* a click on the launcher, in canvas pixels */
    if (!g_in_launcher || !g_view) return;
    NSPoint p = [g_view convertPoint:e.locationInWindow fromView:nil];   /* the view is flipped: top-left origin */
    NSRect u = usable_rect();
    int cx, cy;
    lc_to_canvas((int)u.size.width, (int)u.size.height, (int)(p.x - u.origin.x), (int)(p.y - u.origin.y), &cx, &cy);
    lc_click(cx, cy);
}

static void toggle_fullscreen(void) { if (g_win) [g_win toggleFullScreen:nil]; }
static void set_view_mode(int m) { g_view_mode = ((m % VIEW_COUNT) + VIEW_COUNT) % VIEW_COUNT; save_view_mode(); update_view_width(); update_title(); }
static void set_filter(int f) { g_filter = f % 3; ini_set_int("video", "filter", g_filter); update_title(); }
static void toggle_pause(void) {   /* in Mega Drive mode during a race this opens the pause menu */
    if (!g_paused) { g_paused = 1; g_pause_menu = sho_pause_begin(); }
    else { if (g_pause_menu) { sho_pause_end(); apply_sound_levels(1); } g_paused = 0; g_pause_menu = 0; }
}

/* display link: one tick per display refresh, used like the Windows build's vsync'd presents */
static CVDisplayLinkRef g_dl;
static dispatch_semaphore_t g_vsync;
static CVReturn dl_callback(CVDisplayLinkRef l, const CVTimeStamp *now, const CVTimeStamp *out, CVOptionFlags fi, CVOptionFlags *fo, void *ctx) {
    (void)l; (void)now; (void)out; (void)fi; (void)fo; (void)ctx;
    dispatch_semaphore_signal(g_vsync);
    return kCVReturnSuccess;
}
static void display_link_follow_screen(void) {
    if (!g_dl || !g_win.screen) return;
    NSNumber *n = g_win.screen.deviceDescription[@"NSScreenNumber"];
    if (n) CVDisplayLinkSetCurrentCGDisplay(g_dl, (CGDirectDisplayID)n.unsignedIntValue);
}
static double display_refresh(void) {
    if (!g_dl) return 60.0;
    double p = CVDisplayLinkGetActualOutputVideoRefreshPeriod(g_dl);
    if (p <= 0) { CVTime t = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(g_dl);
                  if (!(t.flags & kCVTimeIsIndefinite) && t.timeValue > 0) p = (double)t.timeValue / (double)t.timeScale; }
    if (p <= 0.002 || p > 0.1) return 60.0;
    double hz = 1.0 / p;
    if (fabs(hz - 59.94) < 0.05) return 59.94;
    return hz;
}

/* ------------------------------------------------------------------ */
/* keyboard                                                           */
/* ------------------------------------------------------------------ */
static int mac_key_is_fkey(unsigned kc) {   /* F1..F12: the game's own keys, not assignable */
    static const unsigned char f[] = { 0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F };
    for (unsigned i = 0; i < sizeof f; i++) if (f[i] == kc) return 1;
    return 0;
}
static const char *mac_key_name(int kc) {   /* macOS key codes (the positions of a US keyboard) */
    static const struct { int kc; const char *n; } t[] = {
        { 0x00, "A" }, { 0x01, "S" }, { 0x02, "D" }, { 0x03, "F" }, { 0x04, "H" }, { 0x05, "G" }, { 0x06, "Z" }, { 0x07, "X" }, { 0x08, "C" }, { 0x09, "V" },
        { 0x0B, "B" }, { 0x0C, "Q" }, { 0x0D, "W" }, { 0x0E, "E" }, { 0x0F, "R" }, { 0x10, "Y" }, { 0x11, "T" }, { 0x12, "1" }, { 0x13, "2" }, { 0x14, "3" },
        { 0x15, "4" }, { 0x16, "6" }, { 0x17, "5" }, { 0x18, "=" }, { 0x19, "9" }, { 0x1A, "7" }, { 0x1B, "-" }, { 0x1C, "8" }, { 0x1D, "0" }, { 0x1E, "]" },
        { 0x1F, "O" }, { 0x20, "U" }, { 0x21, "[" }, { 0x22, "I" }, { 0x23, "P" }, { 0x24, "RETURN" }, { 0x25, "L" }, { 0x26, "J" }, { 0x27, "'" }, { 0x28, "K" },
        { 0x29, ";" }, { 0x2A, "BACKSLASH" }, { 0x2B, "," }, { 0x2C, "/" }, { 0x2D, "N" }, { 0x2E, "M" }, { 0x2F, "." }, { 0x30, "TAB" }, { 0x31, "SPACE" },
        { 0x32, "`" }, { 0x33, "DELETE" }, { 0x38, "SHIFT" }, { 0x41, "KP ." }, { 0x43, "KP *" }, { 0x45, "KP +" }, { 0x4B, "KP /" }, { 0x4C, "KP ENTER" },
        { 0x4E, "KP -" }, { 0x51, "KP =" }, { 0x52, "KP 0" }, { 0x53, "KP 1" }, { 0x54, "KP 2" }, { 0x55, "KP 3" }, { 0x56, "KP 4" }, { 0x57, "KP 5" },
        { 0x58, "KP 6" }, { 0x59, "KP 7" }, { 0x5B, "KP 8" }, { 0x5C, "KP 9" }, { 0x73, "HOME" }, { 0x74, "PAGE UP" }, { 0x75, "FWD DELETE" }, { 0x77, "END" },
        { 0x79, "PAGE DOWN" }, { 0x7B, "LEFT" }, { 0x7C, "RIGHT" }, { 0x7D, "DOWN" }, { 0x7E, "UP" } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) if (t[i].kc == kc) return t[i].n;
    static char other[16]; snprintf(other, sizeof other, "KEY %d", kc);
    return other;
}
static void key_event(NSEvent *e, int down) {
    unsigned kc = e.keyCode;
    NSEventModifierFlags fl = e.modifierFlags;
    if (fl & NSEventModifierFlagCommand) { if (!down && kc < 128) g_keys[kc] = 0; return; }   /* Cmd+key belongs to the menus */
    if (g_in_launcher) {   /* the settings screen: arrows (repeat), Return, Space, Esc; F11 still switches fullscreen */
        if (!down) return;
        if (lc_wants_key()) {   /* the controls screen is waiting for a key to assign */
            if (e.isARepeat) return;
            if (kc == K_ESCAPE) lc_key(LK_ESC);
            else if (kc < 128 && !mac_key_is_fkey(kc)) lc_raw_key((int)kc);
            return;
        }
        if (kc == K_TAB) { if (!e.isARepeat) lc_key(LK_TAB); return; }
        if (kc == K_BACKSPACE || kc == K_FWD_DELETE) { if (!e.isARepeat) lc_key(LK_CLEAR); return; }
        if (kc == K_UP) lc_key(LK_UP);
        else if (kc == K_DOWN) lc_key(LK_DOWN);
        else if (kc == K_LEFT) lc_key(LK_LEFT);
        else if (kc == K_RIGHT) lc_key(LK_RIGHT);
        else if (e.isARepeat) return;
        else if (kc == K_RETURN || kc == K_KP_ENTER) lc_key(LK_ENTER);
        else if (kc == K_SPACE) lc_key(LK_SPACE);
        else if (kc == K_ESCAPE) lc_key(LK_ESC);
        else if (kc == K_F11) toggle_fullscreen();
        return;
    }
    if (kc < 128) g_keys[kc] = (uint8_t)down;
    if (!down || e.isARepeat) return;
    int ctrl = (fl & NSEventModifierFlagControl) != 0;
    if (kc == K_F11) toggle_fullscreen();
    if (kc == K_ESCAPE && g_fullscreen) toggle_fullscreen();
    if (sho_key_is_action(KA_PAUSE, (int)kc)) toggle_pause();   /* the pause key (P) */
    if (kc == K_8 && ctrl) sho_md_cheat_upgrade();      /* development: fully upgraded bike */
    if (kc == K_9 && ctrl) sho_md_cheat_money();        /* development: maximum money */
    if (ctrl) {   /* development: Original Mode race select */
        static const unsigned dk[8] = { K_0, K_1, K_2, K_3, K_4, K_5, K_6, K_7 };
        for (int i = 0; i < 8; i++) if (kc == dk[i]) { sho_md_dev_race(i); ini_set_int("dev", "original_race", i); }
    }
    if (kc == K_F3) reset_to_title();
    if (kc == K_F9) set_view_mode(g_view_mode + (g_shift ? -1 : 1));   /* Shift+F9 steps back */
    if (kc == K_F8) set_filter(g_filter + 1);
}
static void flags_event(NSEvent *e) {
    NSEventModifierFlags fl = e.modifierFlags;
    int was = g_shift;
    g_shift = (fl & NSEventModifierFlagShift) != 0;
    g_keys[K_SHIFT] = (uint8_t)g_shift;                       /* Shift as a key that can be assigned (either one) */
    if (g_in_launcher && g_shift && !was && lc_wants_key()) lc_raw_key(K_SHIFT);
    if (fl & NSEventModifierFlagCommand) memset(g_keys, 0, sizeof g_keys);   /* keys released while Cmd is held send no key-up */
}

@interface GameView : NSView
@end
@implementation GameView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isFlipped { return YES; }
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (void)updateLayer { }
- (void)keyDown:(NSEvent *)e { key_event(e, 1); }
- (void)keyUp:(NSEvent *)e { key_event(e, 0); }
- (void)flagsChanged:(NSEvent *)e { flags_event(e); }
- (void)mouseDown:(NSEvent *)e { launcher_mouse(e); }
- (void)viewDidChangeBackingProperties { [super viewDidChangeBackingProperties]; if (g_layer && self.window) g_layer.contentsScale = self.window.backingScaleFactor; }
@end

/* ------------------------------------------------------------------ */
/* input                                                              */
/* ------------------------------------------------------------------ */
/* one pad, read the same way whatever kind macOS reports it as */
typedef struct { float lx, ly, lt, rt; int a, b, x, y, rb, start, back, du, dd, dl, dr, lb, l3, r3; } padstate;
static int g_pad_count = -1;
int g_pad_count_for_title = -1;
static float btn(GCControllerButtonInput *b) { return b ? b.value : 0.0f; }
static int pressed(GCControllerButtonInput *b) { return b && b.isPressed; }
static void dpad_dirs(GCControllerDirectionPad *d, padstate *p) {
    if (!d) return;
    if (d.up.isPressed || d.yAxis.value > 0.5f) p->du = 1;
    if (d.down.isPressed || d.yAxis.value < -0.5f) p->dd = 1;
    if (d.left.isPressed || d.xAxis.value < -0.5f) p->dl = 1;
    if (d.right.isPressed || d.xAxis.value > 0.5f) p->dr = 1;
}
static int read_pad(GCController *c, padstate *p) {
    memset(p, 0, sizeof *p);
    GCExtendedGamepad *g = c.extendedGamepad;
    if (g) {   /* the full Xbox-style layout */
        p->lx = g.leftThumbstick.xAxis.value; p->ly = g.leftThumbstick.yAxis.value;
        p->lt = g.leftTrigger.value; p->rt = g.rightTrigger.value;
        p->a = pressed(g.buttonA); p->b = pressed(g.buttonB); p->x = pressed(g.buttonX); p->y = pressed(g.buttonY);
        p->rb = pressed(g.rightShoulder); p->lb = pressed(g.leftShoulder);
        if (@available(macOS 10.14.1, *)) { p->l3 = pressed(g.leftThumbstickButton); p->r3 = pressed(g.rightThumbstickButton); }
        if (@available(macOS 10.15, *)) { p->start = pressed(g.buttonMenu); p->back = pressed(g.buttonOptions); }
        dpad_dirs(g.dpad, p);
        return 1;
    }
    if (@available(macOS 11.0, *)) {   /* pads with their own layout: look the controls up by name */
        GCPhysicalInputProfile *pr = c.physicalInputProfile;
        if (pr) {
            NSDictionary<NSString *, GCControllerButtonInput *> *bs = pr.buttons;
            NSDictionary<NSString *, GCControllerDirectionPad *> *ds = pr.dpads;
            GCControllerDirectionPad *ls = ds[GCInputLeftThumbstick];
            if (ls) { p->lx = ls.xAxis.value; p->ly = ls.yAxis.value; }
            p->lt = btn(bs[GCInputLeftTrigger]); p->rt = btn(bs[GCInputRightTrigger]);
            p->a = pressed(bs[GCInputButtonA]); p->b = pressed(bs[GCInputButtonB]);
            p->x = pressed(bs[GCInputButtonX]); p->y = pressed(bs[GCInputButtonY]);
            p->rb = pressed(bs[GCInputRightShoulder]); p->lb = pressed(bs[GCInputLeftShoulder]);
            p->l3 = pressed(bs[GCInputLeftThumbstickButton]); p->r3 = pressed(bs[GCInputRightThumbstickButton]);
            p->start = pressed(bs[GCInputButtonMenu]); p->back = pressed(bs[GCInputButtonOptions]);
            dpad_dirs(ds[GCInputDirectionPad], p);
            return ls || ds[GCInputDirectionPad] || bs[GCInputButtonA];
        }
    }
    GCMicroGamepad *m = c.microGamepad;
    if (m) {   /* the small Siri-remote style layout: d-pad, A, X */
        dpad_dirs(m.dpad, p);
        p->lx = p->dr ? 1.0f : p->dl ? -1.0f : 0.0f;
        p->a = pressed(m.buttonA); p->x = pressed(m.buttonX);
        if (@available(macOS 10.15, *)) p->start = pressed(m.buttonMenu);
        return 1;
    }
    return 0;
}

/* gamepad.log in the game folder: which pads the game sees, and their controls for a minute after each one
   connects - for finding out why a pad doesn't work */
static FILE *g_padlog; static double g_padlog_until; static int g_padlog_lines;
static _Atomic long g_pad_events;    /* value changes the pad framework has delivered (its own handler) */
static void padlog(const char *fmt, ...) {
    if (g_padlog_lines > 5000) return;
    g_padlog_lines++;
    if (!g_padlog) { char p[PATHN]; path_join(p, g_dir, "gamepad.log"); g_padlog = fopen(p, "w"); if (!g_padlog) return; }
    va_list ap; va_start(ap, fmt); fprintf(g_padlog, "%8.2f  ", now_s()); vfprintf(g_padlog, fmt, ap); va_end(ap);
    fputc('\n', g_padlog); fflush(g_padlog);
}
static void padlog_controller(GCController *c, const char *what) {
    const char *vn = c.vendorName ? c.vendorName.UTF8String : "(no name)", *cat = "";
    int phys = 0;
    if (@available(macOS 10.15, *)) cat = c.productCategory ? c.productCategory.UTF8String : "";
    if (@available(macOS 11.0, *)) phys = c.physicalInputProfile != nil;
    padlog("%s: %s [%s] extended=%d micro=%d physical=%d", what, vn, cat, c.extendedGamepad != nil, c.microGamepad != nil, phys);
    GCExtendedGamepad *g = c.extendedGamepad;
    if (g) {
        int menu = 0, opts = 0;
        if (@available(macOS 10.15, *)) { menu = g.buttonMenu != nil; opts = g.buttonOptions != nil; }
        padlog("  controls: menu(Start)=%d options(Back)=%d", menu, opts);
        /* count what the framework delivers, independently of polling */
        g.valueChangedHandler = ^(GCExtendedGamepad *gp, GCControllerElement *el) { (void)gp; (void)el; g_pad_events++; };
    }
    if (@available(macOS 11.0, *)) {
        NSArray *names = c.physicalInputProfile.buttons.allKeys;
        NSString *list = [names componentsJoinedByString:@", "];
        if (list) padlog("  buttons by name: %s", list.UTF8String);
    }
    g_padlog_until = now_s() + 1e9;   /* log control changes for the whole session (capped) */
}
/* set up the pad framework at the very start, before the app is activated */
static void gamepads_init(void) {
    if (@available(macOS 11.3, *)) GCController.shouldMonitorBackgroundEvents = YES;   /* input even if macOS thinks another app is in front */
    [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidConnectNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification *n) { padlog_controller((GCController *)n.object, "connected"); }];
    [[NSNotificationCenter defaultCenter] addObserverForName:GCControllerDidDisconnectNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification *n) { padlog_controller((GCController *)n.object, "disconnected"); }];
    for (GCController *c in [GCController controllers]) padlog_controller(c, "present at start");
    padlog("macOS reports %d controller(s) at start", (int)[GCController controllers].count);
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidBecomeActiveNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification *n) { (void)n; padlog("app became active"); }];
    [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification object:nil queue:nil
                                                  usingBlock:^(NSNotification *n) { (void)n; padlog("app went to the background"); }];
}
static void padlog_state(int i, const padstate *p) {
    static padstate last[8]; static int have[8];
    if (now_s() > g_padlog_until) return;
    unsigned b = p->a | p->b << 1 | p->x << 2 | p->y << 3 | p->rb << 4 | p->start << 5 | p->back << 6 | p->du << 7 | p->dd << 8 | p->dl << 9 | p->dr << 10;
    const padstate *q = &last[i];
    unsigned qb = q->a | q->b << 1 | q->x << 2 | q->y << 3 | q->rb << 4 | q->start << 5 | q->back << 6 | q->du << 7 | q->dd << 8 | q->dl << 9 | q->dr << 10;
    if (have[i] && b == qb && fabsf(p->lx - q->lx) < 0.2f && fabsf(p->ly - q->ly) < 0.2f && fabsf(p->lt - q->lt) < 0.2f && fabsf(p->rt - q->rt) < 0.2f) return;
    padlog("pad%d  stick %+.2f %+.2f  LT %.2f RT %.2f  %s%s%s%s%s%s%s%s%s%s%s", i, p->lx, p->ly, p->lt, p->rt,
           p->a ? "A " : "", p->b ? "B " : "", p->x ? "X " : "", p->y ? "Y " : "", p->rb ? "RB " : "", p->start ? "START " : "",
           p->back ? "BACK " : "", p->du ? "UP " : "", p->dd ? "DOWN " : "", p->dl ? "LEFT " : "", p->dr ? "RIGHT " : "");
    last[i] = *p; have[i] = 1;
}

/* one pad as the shared Xbox-style state (sho_controls.h): what each button does is set on the launcher's controls screen */
static void to_sho_pad(const padstate *p, sho_pad *o) {
    memset(o, 0, sizeof *o);
    o->lx = p->lx; o->ly = p->ly; o->lt = p->lt; o->rt = p->rt;
    if (p->a) o->buttons |= 1u << PB_A;
    if (p->b) o->buttons |= 1u << PB_B;
    if (p->x) o->buttons |= 1u << PB_X;
    if (p->y) o->buttons |= 1u << PB_Y;
    if (p->lb) o->buttons |= 1u << PB_LB;
    if (p->rb) o->buttons |= 1u << PB_RB;
    if (p->back) o->buttons |= 1u << PB_BACK;
    if (p->start) o->buttons |= 1u << PB_START;
    if (p->l3) o->buttons |= 1u << PB_L3;
    if (p->r3) o->buttons |= 1u << PB_R3;
    if (p->du) o->buttons |= 1u << PB_UP;
    if (p->dd) o->buttons |= 1u << PB_DOWN;
    if (p->dl) o->buttons |= 1u << PB_LEFT;
    if (p->dr) o->buttons |= 1u << PB_RIGHT;
    sho_pad_finish(o);
}
static void apply_pad(int slot, const padstate *p, int menu, sho_input *in, float *steer, float *thr, float *brk) {
    sho_pad sp; to_sho_pad(p, &sp);
    sho_controls_apply_pad(slot, &sp, menu, in, steer, thr, brk);
}

/* ------------------------------------------------------------------ */
/* gamepads read directly as HID devices (IOKit), polled every frame  */
/* ------------------------------------------------------------------ */
/* Generic Desktop usages */
enum { U_X = 0x30, U_Y = 0x31, U_Z = 0x32, U_RX = 0x33, U_RY = 0x34, U_RZ = 0x35, U_HAT = 0x39 };
typedef struct { IOHIDElementRef e; CFIndex min, max, rest; } hidaxis;
typedef struct {
    IOHIDDeviceRef dev;
    char name[128];
    hidaxis x, y, lt, rt;               /* left stick and analogue triggers (any may be missing) */
    IOHIDElementRef hat; CFIndex hmin, hmax;
    IOHIDElementRef btn[33];            /* HID buttons 1..32 */
    CFIndex last_btn[33], last_misc[16];
    IOHIDElementRef misc[16]; int misc_usage[16], nmisc;
} hidpad;
static IOHIDManagerRef g_hidm;
static hidpad g_hid[4];
static int g_nhid;
/* button numbers for A B X Y RB Back Start: [pad] map= in shangon.ini; default = the common HID layout of
   GameSir / 8BitDo / Android-mode pads (1 A, 2 B, 4 X, 5 Y, 8 RB, 11 Select, 12 Start) */
static int g_hidmap[7] = { 1, 2, 4, 5, 8, 11, 12 };

static CFIndex hid_value(IOHIDDeviceRef d, IOHIDElementRef e) {
    IOHIDValueRef v = NULL;
    if (!e || IOHIDDeviceGetValue(d, e, &v) != kIOReturnSuccess || !v) return 0;
    return IOHIDValueGetIntegerValue(v);
}
static float axis_norm(const hidaxis *a, CFIndex v) { return a->max > a->min ? (float)(v - a->min) / (float)(a->max - a->min) * 2.0f - 1.0f : 0.0f; }
static void hid_setup(hidpad *h, IOHIDDeviceRef dev) {
    memset(h, 0, sizeof *h);
    h->dev = dev; CFRetain(dev);
    CFStringRef pn = (CFStringRef)IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductKey));
    if (!pn || !CFStringGetCString(pn, h->name, sizeof h->name, kCFStringEncodingUTF8)) snprintf(h->name, sizeof h->name, "(no name)");
    int vid = 0, pid = 0; CFNumberRef n;
    if ((n = (CFNumberRef)IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDVendorIDKey)))) CFNumberGetValue(n, kCFNumberIntType, &vid);
    if ((n = (CFNumberRef)IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductIDKey)))) CFNumberGetValue(n, kCFNumberIntType, &pid);
    hidaxis z = { 0 }, rz = { 0 }, rx = { 0 }, ry = { 0 }, accel = { 0 }, brake = { 0 };
    CFArrayRef els = IOHIDDeviceCopyMatchingElements(dev, NULL, kIOHIDOptionsTypeNone);
    int nbtn = 0; char axes[160] = "";
    for (CFIndex i = 0; els && i < CFArrayGetCount(els); i++) {
        IOHIDElementRef e = (IOHIDElementRef)CFArrayGetValueAtIndex(els, i);
        IOHIDElementType t = IOHIDElementGetType(e);
        if (t != kIOHIDElementTypeInput_Misc && t != kIOHIDElementTypeInput_Button && t != kIOHIDElementTypeInput_Axis) continue;
        uint32_t pg = IOHIDElementGetUsagePage(e), u = IOHIDElementGetUsage(e);
        hidaxis a = { e, IOHIDElementGetLogicalMin(e), IOHIDElementGetLogicalMax(e), 0 };
        if (pg == kHIDPage_Button && u >= 1 && u <= 32) { if (!h->btn[u]) { h->btn[u] = e; nbtn++; } continue; }
        if (pg == kHIDPage_GenericDesktop) {
            if (u == U_HAT) { h->hat = e; h->hmin = a.min; h->hmax = a.max; strcat(axes, "hat "); continue; }
            if (u == U_X && !h->x.e) { h->x = a; strcat(axes, "X "); continue; }
            if (u == U_Y && !h->y.e) { h->y = a; strcat(axes, "Y "); continue; }
            if (u == U_Z && !z.e) { z = a; strcat(axes, "Z "); continue; }
            if (u == U_RZ && !rz.e) { rz = a; strcat(axes, "Rz "); continue; }
            if (u == U_RX && !rx.e) { rx = a; strcat(axes, "Rx "); continue; }
            if (u == U_RY && !ry.e) { ry = a; strcat(axes, "Ry "); continue; }
        }
        if (pg == kHIDPage_Simulation && u == kHIDUsage_Sim_Accelerator && !accel.e) { accel = a; strcat(axes, "Accelerator "); continue; }
        if (pg == kHIDPage_Simulation && u == kHIDUsage_Sim_Brake && !brake.e) { brake = a; strcat(axes, "Brake "); continue; }
        if (h->nmisc < 16 && a.max > a.min) { h->misc[h->nmisc] = e; h->misc_usage[h->nmisc] = (int)(pg << 16 | u); h->nmisc++; }
    }
    if (els) CFRelease(els);
    /* analogue triggers: the Simulation page's Brake/Accelerator if there are any (Android-style pads); otherwise
       a pair of axes that rest at their minimum, as triggers do (sticks rest in the middle) */
    if (brake.e && accel.e) { h->lt = brake; h->rt = accel; }
    else {
        hidaxis cand[4] = { z, rz, rx, ry };
        for (int k = 0; k < 4; k++) { cand[k].rest = hid_value(dev, cand[k].e); }
        int lo[4]; for (int k = 0; k < 4; k++) lo[k] = cand[k].e && cand[k].max > cand[k].min && cand[k].rest <= cand[k].min + (cand[k].max - cand[k].min) / 8;
        if (lo[0] && lo[1]) { h->lt = cand[0]; h->rt = cand[1]; }
        else if (lo[2] && lo[3]) { h->lt = cand[2]; h->rt = cand[3]; }
    }
    padlog("HID pad: %s (vendor %04x product %04x): %d buttons, axes: %s; triggers: %s", h->name, vid, pid, nbtn, axes,
           h->lt.e ? "found" : "none (use A/B for full throttle/brake)");
}
/* the pad list, re-read every 2 seconds (pads plugged in or out while the game runs) */
static void hid_poll(void) {
    static double next;
    double t = now_s();
    if (!g_hidm || t < next) return;
    next = t + 2.0;
    CFSetRef set = IOHIDManagerCopyDevices(g_hidm);
    CFIndex n = set ? CFSetGetCount(set) : 0;
    IOHIDDeviceRef devs[8] = { 0 };
    if (n > 8) n = 8;
    if (set) CFSetGetValues(set, (const void **)devs);
    /* drop pads that are gone */
    for (int h = 0; h < g_nhid; ) {
        int found = 0; for (CFIndex k = 0; k < n; k++) if (devs[k] == g_hid[h].dev) found = 1;
        if (found) { h++; continue; }
        padlog("HID pad removed: %s", g_hid[h].name);
        CFRelease(g_hid[h].dev); g_hid[h] = g_hid[--g_nhid];
    }
    /* add new ones */
    for (CFIndex k = 0; k < n && g_nhid < 4; k++) {
        int known = 0; for (int h = 0; h < g_nhid; h++) if (g_hid[h].dev == devs[k]) known = 1;
        if (!known) hid_setup(&g_hid[g_nhid++], devs[k]);
    }
    if (set) CFRelease(set);
}
static int hid_read(hidpad *h, padstate *p) {
    memset(p, 0, sizeof *p);
    if (h->x.e) p->lx = axis_norm(&h->x, hid_value(h->dev, h->x.e));
    if (h->y.e) p->ly = -axis_norm(&h->y, hid_value(h->dev, h->y.e));   /* HID: down is positive */
    if (h->lt.e) p->lt = (axis_norm(&h->lt, hid_value(h->dev, h->lt.e)) + 1.0f) * 0.5f;
    if (h->rt.e) p->rt = (axis_norm(&h->rt, hid_value(h->dev, h->rt.e)) + 1.0f) * 0.5f;
    if (h->hat) {   /* 8 ways: 0 up, 1 up-right ... 7 up-left; 4 ways: 0 up, 1 right, 2 down, 3 left; outside = centred */
        CFIndex v = hid_value(h->dev, h->hat) - h->hmin, range = h->hmax - h->hmin + 1;
        if (v >= 0 && v < range) {
            if (range == 4) v *= 2;
            if (v == 7 || v == 0 || v == 1) p->du = 1;
            if (v >= 1 && v <= 3) p->dr = 1;
            if (v >= 3 && v <= 5) p->dd = 1;
            if (v >= 5 && v <= 7) p->dl = 1;
        }
    }
    int *out[7] = { &p->a, &p->b, &p->x, &p->y, &p->rb, &p->back, &p->start };
    for (int k = 0; k < 7; k++) { int b = g_hidmap[k]; if (b >= 1 && b <= 32 && h->btn[b]) *out[k] = hid_value(h->dev, h->btn[b]) != 0; }
    {   /* the rest of the usual layout, where [pad] map= has not taken the number: 7 LB, 9 / 10 the triggers as buttons, 14 / 15 stick clicks */
        static const int more[5] = { 7, 9, 10, 14, 15 };
        int v[5] = { 0, 0, 0, 0, 0 };
        for (int k = 0; k < 5; k++) {
            int b = more[k], taken = 0;
            for (int q = 0; q < 7; q++) if (g_hidmap[q] == b) taken = 1;
            if (!taken && h->btn[b]) v[k] = hid_value(h->dev, h->btn[b]) != 0;
        }
        p->lb = v[0]; p->l3 = v[3]; p->r3 = v[4];
        if (v[1] && !h->lt.e) p->lt = 1.0f;                /* (only pads without analogue triggers) */
        if (v[2] && !h->rt.e) p->rt = 1.0f;
    }
    /* log raw button numbers and unknown controls as they change (to fill in [pad] map= for an unusual pad) */
    for (int b = 1; b <= 32; b++) if (h->btn[b]) {
        CFIndex v = hid_value(h->dev, h->btn[b]);
        if (v != h->last_btn[b]) { padlog("HID %s: button %d %s", h->name, b, v ? "pressed" : "released"); h->last_btn[b] = v; }
    }
    for (int k = 0; k < h->nmisc; k++) {
        CFIndex v = hid_value(h->dev, h->misc[k]);
        if (v != h->last_misc[k]) { padlog("HID %s: control %08x = %ld", h->name, h->misc_usage[k], (long)v); h->last_misc[k] = v; }
    }
    return 1;
}
static void hid_init(void) {
    char m[96]; ini_str("pad", "map", "", m, sizeof m);   /* e.g. map=1,2,4,5,8,11,12  (A B X Y RB Back Start) */
    if (m[0]) { int v[7]; if (sscanf(m, "%d,%d,%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7) memcpy(g_hidmap, v, sizeof v); }
    g_hidm = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!g_hidm) { padlog("HID: no manager"); return; }
    NSArray *match = @[ @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_GamePad) },
                        @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_Joystick) },
                        @{ @kIOHIDDeviceUsagePageKey: @(kHIDPage_GenericDesktop), @kIOHIDDeviceUsageKey: @(kHIDUsage_GD_MultiAxisController) } ];
    IOHIDManagerSetDeviceMatchingMultiple(g_hidm, (__bridge CFArrayRef)match);
    IOHIDManagerScheduleWithRunLoop(g_hidm, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    IOReturn r = IOHIDManagerOpen(g_hidm, kIOHIDOptionsTypeNone);
    padlog("HID: manager open (%s)", r == kIOReturnSuccess ? "ok" : "failed");
}

/* ---- the launcher's controls screen: the controllers macOS has ----
 * Those in System Settings > Game Controllers (the GameController framework), by their names there, then the same or other
 * pads read directly as USB / Bluetooth devices, marked (HID). With ANY CONTROLLER the game uses the first kind, and the
 * second only while the framework delivers nothing; a pad chosen by name is used whichever kind it is. */
static void gc_name(GCController *c, int n, char *out, size_t len) {
    snprintf(out, len, "%.80s", c.vendorName.length ? c.vendorName.UTF8String : "Controller");
    if (n > 0) { size_t l = strlen(out); snprintf(out + l, len - l, " (%d)", n + 1); }   /* two of the same */
}
static int gc_dup(NSArray<GCController *> *pads, int i) {   /* how many earlier ones have this one's name */
    int n = 0; NSString *a = pads[(NSUInteger)i].vendorName;
    for (int k = 0; k < i; k++) { NSString *b = pads[(NSUInteger)k].vendorName; if ((!a && !b) || (a && b && [a isEqualToString:b])) n++; }
    return n;
}
static void hid_name(int h, char *out, size_t len) { snprintf(out, len, "%.80s (HID)", g_hid[h].name[0] ? g_hid[h].name : "Controller"); }
static int host_pad_count(void) { hid_poll(); int n = (int)[GCController controllers].count; if (n > 4) n = 4; return n + g_nhid; }
static const char *host_pad_name(int i) {
    static char nm[160]; nm[0] = 0;
    NSArray<GCController *> *pads = [GCController controllers];
    int n = (int)pads.count; if (n > 4) n = 4;
    if (i >= 0 && i < n) gc_name(pads[(NSUInteger)i], gc_dup(pads, i), nm, sizeof nm);
    else if (i >= n && i - n < g_nhid) hid_name(i - n, nm, sizeof nm);
    return nm;
}
static void pad_merge(sho_pad *out, const sho_pad *p) {
    out->buttons |= p->buttons;
    if (fabsf(p->lx) > fabsf(out->lx)) out->lx = p->lx;
    if (fabsf(p->ly) > fabsf(out->ly)) out->ly = p->ly;
    if (p->lt > out->lt) out->lt = p->lt;
    if (p->rt > out->rt) out->rt = p->rt;
}
static int host_pad_read(int i, sho_pad *out) {
    NSArray<GCController *> *pads = [GCController controllers];
    int n = (int)pads.count; if (n > 4) n = 4;
    padstate p; sho_pad sp;
    memset(out, 0, sizeof *out);
    hid_poll();
    if (i >= 0) {
        if (i < n) { if (!read_pad(pads[(NSUInteger)i], &p)) return 0; }
        else if (i - n < g_nhid) { if (!hid_read(&g_hid[i - n], &p)) return 0; }
        else return 0;
        to_sho_pad(&p, out);
        return 1;
    }
    int any = 0;                                           /* any: all of them, merged */
    for (int k = 0; k < n; k++) if (read_pad(pads[(NSUInteger)k], &p)) { to_sho_pad(&p, &sp); pad_merge(out, &sp); any = 1; }
    for (int h = 0; h < g_nhid; h++) if (hid_read(&g_hid[h], &p)) { to_sho_pad(&p, &sp); pad_merge(out, &sp); any = 1; }
    return any;
}
static void controls_save(void) {
    char t[256];
    sho_controls_pad_to_str(t, sizeof t); ini_set("pad", "actions", t);
    ini_set("pad", "device", sho_ctl.device);
    sho_controls_keys_to_str(t, sizeof t); ini_set("keys_mac", "keys", t);
}
static void controls_load(void) {
    /* (Ctrl is not a throttle on the Mac: Ctrl+arrows switch Spaces / open Mission Control) */
    static const int def[KA_COUNT][KEY_SLOTS] = {
        [KA_LEFT] = { K_LEFT, K_A, -1, -1 }, [KA_RIGHT] = { K_RIGHT, K_D, -1, -1 },
        [KA_ACCEL] = { K_UP, K_Z, K_W, -1 }, [KA_BRAKE] = { K_DOWN, K_X, K_S, -1 },
        [KA_TURBO] = { K_SPACE, K_SHIFT, -1, -1 }, [KA_START] = { K_RETURN, K_1, K_KP_ENTER, -1 },
        [KA_COIN] = { K_5, K_C, -1, -1 }, [KA_PAUSE] = { K_P, -1, -1, -1 } };
    char t[256];
    sho_ctl.deadzone = g_deadzone; sho_ctl.curve = g_steer_curve;
    sho_controls_pad_defaults(g_swap_pedals);
    ini_str("pad", "actions", "", t, sizeof t); if (t[0]) sho_controls_pad_from_str(t);
    ini_str("pad", "device", "", sho_ctl.device, sizeof sho_ctl.device);
    sho_controls_key_defaults(def);
    ini_str("keys_mac", "keys", "", t, sizeof t); if (t[0]) sho_controls_keys_from_str(t);
}
static const lc_controls_host g_controls_host = { host_pad_count, host_pad_name, host_pad_read, mac_key_name, controls_save };

static int g_ctrl_down;
static int key_held(int kc) {
    if (kc < 0 || kc >= 128 || !g_keys[kc]) return 0;
    if (g_ctrl_down && (kc == K_0 || kc == K_1 || kc == K_2 || kc == K_3 || kc == K_4 || kc == K_5 || kc == K_6 || kc == K_7 || kc == K_8 || kc == K_9)) return 0;   /* Ctrl+digits are the development keys */
    return 1;
}
static int is_arrow(int kc) { return kc == K_UP || kc == K_DOWN || kc == K_LEFT || kc == K_RIGHT; }
static int act_key_not_arrow(int ka) {   /* one of the action's keys is down, arrows aside (they move through menus) */
    for (int i = 0; i < KEY_SLOTS; i++) { int c = sho_ctl.key[ka][i]; if (c >= 0 && !is_arrow(c) && key_held(c)) return 1; }
    return 0;
}
static void read_input(sho_input *in, float dt) {
    memset(in, 0, sizeof *in);
    int focused = g_win && g_win.isKeyWindow && NSApp.isActive;
    g_ctrl_down = ([NSEvent modifierFlags] & NSEventModifierFlagControl) != 0;
    int menu = sho_md_overlay_active() || g_pause_menu;   /* the Original Mode screens, the pause menu */
    int kl = focused && sho_key_action_down(KA_LEFT, key_held), kr = focused && sho_key_action_down(KA_RIGHT, key_held);
    float target = (float)(kr - kl), rate = (target == 0.0f) ? 6.0f : 3.5f;   /* keyboard: handlebar ramps */
    if (g_kb_steer < target) { g_kb_steer += rate * dt; if (g_kb_steer > target) g_kb_steer = target; }
    else if (g_kb_steer > target) { g_kb_steer -= rate * dt; if (g_kb_steer < target) g_kb_steer = target; }
    float steer = g_kb_steer, thr = 0, brk = 0;
    if (focused && menu) {   /* there: arrows move, Space or the accelerate keys select, Delete or the brake keys go back (Return is Start) */
        in->nav_up = g_keys[K_UP]; in->nav_down = g_keys[K_DOWN]; in->nav_left = g_keys[K_LEFT]; in->nav_right = g_keys[K_RIGHT];
        in->pad_a = g_keys[K_SPACE] || act_key_not_arrow(KA_ACCEL); in->pad_b = g_keys[K_BACKSPACE] || act_key_not_arrow(KA_BRAKE);
    }
    if (focused) {
        if (sho_key_action_down(KA_ACCEL, key_held)) thr = 1;
        if (sho_key_action_down(KA_BRAKE, key_held)) brk = 1;
        in->turbo = sho_key_action_down(KA_TURBO, key_held);
        in->coin = sho_key_action_down(KA_COIN, key_held);
        in->start = sho_key_action_down(KA_START, key_held);
        in->test = g_keys[K_F2];
        in->service = !g_ctrl_down && g_keys[K_9];
    }
    /* gamepads through the GameController framework: Xbox, PlayStation, MFi, and the third-party pads macOS lists
       under System Settings > Game Controllers (GameSir, 8BitDo...) */
    { static long last_ev = -1; static double last_t;
      long ev = g_pad_events; double t = now_s();
      if (ev != last_ev && t - last_t > 1.0) { padlog("pad events delivered so far: %ld", ev); last_ev = ev; last_t = t; } }
    NSArray<GCController *> *pads = [GCController controllers];
    hid_poll();
    int any = !sho_ctl.device[0];                          /* no controller chosen, or the chosen one is not there: any of them */
    if (!any) {
        int there = 0, n = (int)pads.count; char nm[160]; if (n > 4) n = 4;
        for (int k = 0; k < n; k++) { gc_name(pads[(NSUInteger)k], gc_dup(pads, k), nm, sizeof nm); if (!strcmp(nm, sho_ctl.device)) there = 1; }
        for (int h = 0; h < g_nhid; h++) { hid_name(h, nm, sizeof nm); if (!strcmp(nm, sho_ctl.device)) there = 1; }
        if (!there) any = 1;
    }
    if ((int)pads.count != g_pad_count) { g_pad_count = g_pad_count_for_title = (int)pads.count; padlog("controllers: %d", g_pad_count); update_title(); }
    int i = -1;
    for (GCController *c in pads) {
        i++;
        if (i >= 4) break;
        padstate p;
        if (!read_pad(c, &p)) continue;
        padlog_state(i, &p);
        char nm[160]; gc_name(c, gc_dup(pads, i), nm, sizeof nm);
        if (any || !strcmp(nm, sho_ctl.device)) apply_pad(i, &p, menu, in, &steer, &thr, &brk);   /* any, or the chosen one */
    }
    /* the same pads read directly as USB/Bluetooth HID devices: works even when the framework above delivers
       nothing to the game (it is set aside once the framework does deliver) */
    for (int h = 0; h < g_nhid; h++) {
        padstate p;
        if (!hid_read(&g_hid[h], &p)) continue;
        padlog_state(4 + h, &p);
        char nm[160]; hid_name(h, nm, sizeof nm);
        if (any ? g_pad_events == 0 : !strcmp(nm, sho_ctl.device)) apply_pad(4 + h, &p, menu, in, &steer, &thr, &brk);
    }
    in->steer = steer; in->throttle = thr; in->brake = brk;
}

/* ------------------------------------------------------------------ */
/* audio: CoreAudio output unit pulling from a ring buffer            */
/* ------------------------------------------------------------------ */
#define RING 32768                     /* stereo frames, power of two */
static int16_t g_ring[RING * 2];
static _Atomic unsigned long long g_wr, g_rd;   /* frames written / played */
static AudioComponentInstance g_au;
static OSStatus render_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 n, AudioBufferList *io) {
    (void)ref; (void)flags; (void)ts; (void)bus;
    int16_t *out = (int16_t *)io->mBuffers[0].mData;
    unsigned long long r = atomic_load(&g_rd), w = atomic_load(&g_wr);
    for (UInt32 i = 0; i < n; i++) {
        if (r < w) { unsigned k = (unsigned)(r & (RING - 1)); out[2 * i] = g_ring[2 * k]; out[2 * i + 1] = g_ring[2 * k + 1]; r++; }
        else out[2 * i] = out[2 * i + 1] = 0;   /* underrun: silence */
    }
    atomic_store(&g_rd, r);
    return noErr;
}
static int audio_init(void) {
    AudioComponentDescription d = { kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput, kAudioUnitManufacturer_Apple, 0, 0 };
    AudioComponent c = AudioComponentFindNext(NULL, &d);
    if (!c || AudioComponentInstanceNew(c, &g_au) != noErr) { g_au = NULL; return 0; }
    AudioStreamBasicDescription f; memset(&f, 0, sizeof f);
    f.mSampleRate = AUDIO_RATE; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = 2; f.mBitsPerChannel = 16;
    AURenderCallbackStruct cb = { render_cb, NULL };
    if (AudioUnitSetProperty(g_au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) != noErr ||
        AudioUnitSetProperty(g_au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) != noErr ||
        AudioUnitInitialize(g_au) != noErr) { AudioComponentInstanceDispose(g_au); g_au = NULL; return 0; }
    if (AudioOutputUnitStart(g_au) != noErr) { AudioUnitUninitialize(g_au); AudioComponentInstanceDispose(g_au); g_au = NULL; return 0; }
    return 1;
}
static long audio_queued(void) { unsigned long long w = atomic_load(&g_wr), r = atomic_load(&g_rd); return w > r ? (long)(w - r) : 0; }
static void audio_submit(const int16_t *samples, int count) {
    if (audio_queued() + count > RING - 16) return;          /* full: drop */
    unsigned long long w = atomic_load(&g_wr);
    for (int i = 0; i < count; i++) { unsigned k = (unsigned)((w + (unsigned long long)i) & (RING - 1)); g_ring[2 * k] = samples[2 * i]; g_ring[2 * k + 1] = samples[2 * i + 1]; }
    atomic_store(&g_wr, w + (unsigned long long)count);
}
static void audio_shutdown(void) { if (g_au) { AudioOutputUnitStop(g_au); AudioUnitUninitialize(g_au); AudioComponentInstanceDispose(g_au); g_au = NULL; } }

/* ------------------------------------------------------------------ */
static double now_s(void) {
    static mach_timebase_info_data_t tb;
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}
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
        snprintf(detail, (size_t)detlen, "Put shangon.zip in the game folder (Open game folder)");
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
    const char *name = strrchr(first_path, '/'); name = name ? name + 1 : first_path;
    if (missing == 25) snprintf(msg, (size_t)msglen, "ROM problem: %s is not the Super Hang-On set", name);
    else snprintf(msg, (size_t)msglen, "ROM problem: %s - %d missing, %d wrong", name, missing, bad);
    char line[128]; snprintf(line, sizeof line, "%s", firstline); char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    snprintf(detail, (size_t)detlen, "First issue: %s", line);
    snprintf(g_rom_err, sizeof g_rom_err, "%s", first_err);
    return 0;
}

/* Mega Drive ROM for the Original Mode visuals: [megadrive] rom= in the ini, else any .md/.gen/.bin/.zip
   in the game folder that is Super Hang-On (World) (Rev A). Returns 1 and the path if found. */
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

/* back to the title screen (F3 / Cmd+R, or EXIT on the pause menu) */
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
    snprintf(detail, (size_t)detlen, "Add Super Hang-On (World) Mega Drive ROM to enable its options");
    return 0;
}
static int md_rom_present(void) { char p[PATHN]; return find_md_rom(p, sizeof p); }

/* Mega Drive capture runs on a worker thread while the launcher is open (the arcade has not booted yet,
   so the shared 68000 core is free). Big stack: macOS gives secondary threads only 512 KB by default. */
static char g_md_path[PATHN]; static pthread_t g_md_thread; static int g_md_thread_on, g_md_ok;
static volatile int g_md_done;   /* the capture thread has finished */
static void *md_capture_thread(void *p) { char err[256]; (void)p; g_md_ok = mdgfx_capture(g_md_path, err, sizeof err); g_md_done = 1; return NULL; }
static void start_md_capture(void) {
    g_md_done = 0;
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64u << 20);
    g_md_thread_on = pthread_create(&g_md_thread, &a, md_capture_thread, NULL) == 0;
    pthread_attr_destroy(&a);
}

/* The game folder: the folder that holds SuperHangOn.app (as the exe's folder on Windows). If macOS runs the app
   from a read-only copy (a downloaded app that was never moved: "App Translocation") or the folder can't be written,
   ~/Library/Application Support/Super Hang-On Ultimate is used instead; the launcher's "Open game folder" shows it. */
static void find_game_dir(void) {
    NSString *bundle = [[NSBundle mainBundle] bundlePath];
    NSString *dir = [bundle.pathExtension.lowercaseString isEqualToString:@"app"] ? bundle.stringByDeletingLastPathComponent : bundle;
    BOOL translocated = [bundle rangeOfString:@"/AppTranslocation/"].location != NSNotFound;
    if (translocated || access(dir.fileSystemRepresentation, W_OK) != 0) {
        NSArray *sup = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        dir = [(sup.count ? sup[0] : NSHomeDirectory()) stringByAppendingPathComponent:@"Super Hang-On Ultimate"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL];
    }
    snprintf(g_dir, sizeof g_dir, "%s", dir.fileSystemRepresentation);
}

/* ------------------------------------------------------------------ */
/* app, menus, window                                                 */
/* ------------------------------------------------------------------ */
@interface Ctl : NSObject <NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation>
@end
@implementation Ctl
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)app {
    (void)app;
    /* Cmd+Q: leave our own loop so the high scores and settings are saved, then main() returns */
    g_quit = 1; g_running = 0;
    if (g_in_launcher) lc_key(LK_ESC);
    return NSTerminateCancel;
}
- (void)windowWillClose:(NSNotification *)n { (void)n; g_running = 0; }
- (void)windowDidEnterFullScreen:(NSNotification *)n { (void)n; g_fullscreen = 1; }
- (void)windowDidExitFullScreen:(NSNotification *)n { (void)n; g_fullscreen = 0; }
- (void)windowDidResignKey:(NSNotification *)n { (void)n; memset(g_keys, 0, sizeof g_keys); g_shift = 0; }
- (void)windowDidChangeScreen:(NSNotification *)n { (void)n; display_link_follow_screen(); }
- (void)fullScreen:(id)s { (void)s; toggle_fullscreen(); }
- (void)viewMode:(NSMenuItem *)s { set_view_mode((int)s.tag); }   /* Display menu: the item's tag is the mode */
- (void)filterSharp:(id)s { (void)s; set_filter(0); }
- (void)filterSmooth:(id)s { (void)s; set_filter(1); }
- (void)filterSharpBilinear:(id)s { (void)s; set_filter(2); }
- (void)pauseGame:(id)s { (void)s; toggle_pause(); }
- (void)resetGame:(id)s { (void)s; reset_to_title(); }
- (BOOL)validateMenuItem:(NSMenuItem *)it {
    SEL a = it.action;
    int game = g_win != nil && !g_in_launcher;
    if (a == @selector(viewMode:)) it.state = it.tag == g_view_mode ? NSControlStateValueOn : NSControlStateValueOff;
    if (a == @selector(filterSharp:)) it.state = g_filter == 0 ? NSControlStateValueOn : NSControlStateValueOff;
    if (a == @selector(filterSmooth:)) it.state = g_filter == 1 ? NSControlStateValueOn : NSControlStateValueOff;
    if (a == @selector(filterSharpBilinear:)) it.state = g_filter == 2 ? NSControlStateValueOn : NSControlStateValueOff;
    if (a == @selector(pauseGame:)) it.title = g_paused ? @"Resume" : @"Pause";
    if (a == @selector(fullScreen:)) { it.title = g_fullscreen ? @"Exit Full Screen" : @"Enter Full Screen"; return g_win != nil; }   /* (the launcher too) */
    return game;
}
@end
static Ctl *g_ctl;

static NSMenuItem *add_item(NSMenu *m, NSString *title, SEL act, NSString *key, NSEventModifierFlags mods, id target) {
    NSMenuItem *it = [m addItemWithTitle:title action:act keyEquivalent:key];
    it.keyEquivalentModifierMask = mods;
    if (target) it.target = target;
    return it;
}
static void build_menus(void) {
    NSMenu *bar = [NSMenu new];
    NSMenuItem *appItem = [NSMenuItem new]; [bar addItem:appItem];
    NSMenu *app = [NSMenu new];
    add_item(app, @"About Super Hang-On Ultimate", @selector(orderFrontStandardAboutPanel:), @"", 0, nil);
    [app addItem:[NSMenuItem separatorItem]];
    add_item(app, @"Hide Super Hang-On Ultimate", @selector(hide:), @"h", NSEventModifierFlagCommand, nil);
    add_item(app, @"Hide Others", @selector(hideOtherApplications:), @"h", NSEventModifierFlagCommand | NSEventModifierFlagOption, nil);
    add_item(app, @"Show All", @selector(unhideAllApplications:), @"", 0, nil);
    [app addItem:[NSMenuItem separatorItem]];
    add_item(app, @"Quit Super Hang-On Ultimate", @selector(terminate:), @"q", NSEventModifierFlagCommand, nil);
    appItem.submenu = app;

    /* "Display", not "View": AppKit would add its own full screen item to a menu called View */
    NSMenuItem *dispItem = [NSMenuItem new]; [bar addItem:dispItem];
    NSMenu *disp = [[NSMenu alloc] initWithTitle:@"Display"];
    add_item(disp, @"Enter Full Screen", @selector(fullScreen:), @"f", NSEventModifierFlagCommand | NSEventModifierFlagControl, g_ctl);
    {   NSMenuItem *alt = add_item(disp, @"Toggle Full Screen", @selector(fullScreen:), @"\r", NSEventModifierFlagCommand, g_ctl);   /* Cmd+Return too */
        alt.hidden = YES; alt.allowsKeyEquivalentWhenHidden = YES; }
    [disp addItem:[NSMenuItem separatorItem]];
    add_item(disp, @"Fit Screen", @selector(viewMode:), @"1", NSEventModifierFlagCommand, g_ctl).tag = VIEW_FIT;
    for (int i = 0; i < SHO_NSHAPES; i++) {   /* 16:9, 18:9, 19:9, 20:9, 21:9, 4:3: Cmd+2..7, in F9 order */
        static NSString *const titles[SHO_NSHAPES] = { @"16:9 Widescreen", @"18:9 Ultrawide", @"19:9 Ultrawide", @"20:9 Ultrawide", @"21:9 Ultrawide", @"4:3 Original" };
        add_item(disp, titles[i], @selector(viewMode:), [NSString stringWithFormat:@"%d", i + 2], NSEventModifierFlagCommand, g_ctl).tag = 1 + i;
    }
    [disp addItem:[NSMenuItem separatorItem]];
    add_item(disp, @"Sharp Pixels", @selector(filterSharp:), @"4", NSEventModifierFlagCommand, g_ctl);
    add_item(disp, @"Smooth", @selector(filterSmooth:), @"5", NSEventModifierFlagCommand, g_ctl);
    add_item(disp, @"Sharp-Bilinear", @selector(filterSharpBilinear:), @"6", NSEventModifierFlagCommand, g_ctl);
    dispItem.submenu = disp;

    NSMenuItem *gameItem = [NSMenuItem new]; [bar addItem:gameItem];
    NSMenu *game = [[NSMenu alloc] initWithTitle:@"Game"];
    add_item(game, @"Pause", @selector(pauseGame:), @"p", NSEventModifierFlagCommand, g_ctl);
    add_item(game, @"Reset to Title", @selector(resetGame:), @"r", NSEventModifierFlagCommand, g_ctl);
    gameItem.submenu = game;

    NSMenuItem *winItem = [NSMenuItem new]; [bar addItem:winItem];
    NSMenu *wm = [[NSMenu alloc] initWithTitle:@"Window"];
    add_item(wm, @"Minimize", @selector(performMiniaturize:), @"m", NSEventModifierFlagCommand, nil);
    winItem.submenu = wm;
    NSApp.windowsMenu = wm;
    NSApp.mainMenu = bar;
}

/* the window starts at 80% of the screen height, in the shape the picture will have */
static void create_window(void) {
    NSScreen *scr = [NSScreen mainScreen];
    NSRect vis = scr ? scr.visibleFrame : NSMakeRect(0, 0, 1440, 900);
    double a;
    if (g_view_mode != VIEW_FIT) a = sho_shapes[g_view_mode - 1].aspect;
    else { NSRect f = scr ? scr.frame : vis; double top = 0;
           if (@available(macOS 12.0, *)) top = scr ? scr.safeAreaInsets.top : 0;
           a = f.size.width / (f.size.height - top); }          /* the shape fullscreen will have */
    double h = floor(vis.size.height * 0.8), w = floor(h * a);
    if (w > vis.size.width * 0.9) { w = floor(vis.size.width * 0.9); h = floor(w / a); }
    g_win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, w, h)
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered defer:NO];
    g_win.releasedWhenClosed = NO;
    g_win.delegate = g_ctl;
    g_win.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    g_win.backgroundColor = NSColor.blackColor;
    g_win.contentMinSize = NSMakeSize(SHO_W, SHO_H);
    g_win.acceptsMouseMovedEvents = NO;
    GameView *v = [[GameView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
    v.wantsLayer = YES;
    v.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
    g_win.contentView = v;
    g_view = v;
    g_layer = [v.layer isKindOfClass:[CAMetalLayer class]] ? (CAMetalLayer *)v.layer : nil;
    if (g_layer) { g_layer.contentsScale = g_win.backingScaleFactor; g_layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack); }
    [g_win center];
    [g_win makeKeyAndOrderFront:nil];
    [g_win makeFirstResponder:v];
}

static void pump_events(void) {
    for (;;) {
        NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
        if (!e) break;
        [NSApp sendEvent:e];
    }
    /* let the main run loop handle anything else waiting for it (the GameController framework announces pads
       through it; the game runs its own loop rather than [NSApp run]) */
    for (int k = 0; k < 16 && CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource; k++) {}
    /* fullscreen: the pointer hides once the mouse has been still for 2 seconds (any movement shows it again) */
    static NSPoint last; static double still_since; static int hidden;
    NSPoint m = [NSEvent mouseLocation]; double t = now_s();
    if (m.x != last.x || m.y != last.y) { last = m; still_since = t; hidden = 0; }
    else if (g_fullscreen && !g_in_launcher && !hidden && t - still_since > 2.0 && g_win.isKeyWindow) { [NSCursor setHiddenUntilMouseMoves:YES]; hidden = 1; }
}

/* the launcher: d-pad / stick directions, A and Start of any pad, as bits (up, down, left, right, A, Start) */
static int pad_bits(const padstate *p) {
    return (p->du || p->ly > 0.6f) | (p->dd || p->ly < -0.6f) << 1 | (p->dl || p->lx < -0.6f) << 2 | (p->dr || p->lx > 0.6f) << 3
         | (p->a != 0) << 4 | (p->start != 0) << 5;
}
static int launcher_pad_buttons(void) {
    int b = 0; padstate p;
    for (GCController *c in [GCController controllers]) if (read_pad(c, &p)) b |= pad_bits(&p);
    hid_poll();
    for (int h = 0; h < g_nhid; h++) if (hid_read(&g_hid[h], &p)) b |= pad_bits(&p);
    return b;
}
static void present_loading(void) { @autoreleasepool { pump_events(); present_launcher(); } }   /* lc_load_step: the loading bar */

static int run(int argc, char **argv) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    g_ctl = [Ctl new];
    NSApp.delegate = g_ctl;
    NSWindow.allowsAutomaticWindowTabbing = NO;
    build_menus();
    [NSApp finishLaunching];

    find_game_dir();
    gamepads_init();   /* before the app is activated (launcher): the pad framework learns which app is in front from that */
    path_join(g_ini, g_dir, "shangon.ini"); path_join(g_nv, g_dir, "shangon.nv");
    { char lg[PATHN]; path_join(lg, g_dir, "shangon.log"); sho_diag_open(lg); sho_diag("Super Hang-On Ultimate %s (macOS)", SHO_VERSION); }
    ini_set_path(g_ini);
    write_default_ini();
    hid_init();
    char s[64];
    g_fullscreen = ini_int("video", "fullscreen", 1);
    g_view_mode = read_view_mode();
    g_filter = ini_int("video", "filter", 2) % 3; if (g_filter < 0) g_filter = 2;
    g_stretch = ini_int("video", "stretch", 0);
    g_deadzone = ini_int("input", "deadzone", 15) / 100.0f;
    g_steer_curve = ini_int("input", "steer_curve", 100) / 100.0f;
    g_swap_pedals = ini_int("input", "swap_pedals", 0);
    controls_load();
    ini_str("advanced", "cpu", "recompiled", s, sizeof s); sho_recomp = strcasecmp(s, "interpreter") != 0;
    int play_now = 0;
    for (int i = 1; i < argc; i++) {   /* -window, -fullscreen, -interpreter, -play, or a ROM path */
        if (!strcmp(argv[i], "-window")) g_fullscreen = 0;
        else if (!strcmp(argv[i], "-fullscreen")) g_fullscreen = 1;
        else if (!strcmp(argv[i], "-interpreter")) sho_recomp = 0;
        else if (!strcmp(argv[i], "-play")) play_now = 1;
        else if (argv[i][0] != '-') snprintf(g_cmd_rom, sizeof g_cmd_rom, "%s", argv[i]);   /* (-psn_... from old Finder launches is ignored) */
    }

    int show_launcher = ini_int("launcher", "show", 1) && !play_now;
    if (ini_int("megadrive", "enhancements", 0) && find_md_rom(g_md_path, sizeof g_md_path)) start_md_capture();

    /* the game window first: the launcher (settings screen) is shown in it */
    create_window();
    g_have_metal = g_layer && mtl_init(g_layer, SHO_MAX_W > LC_IW ? SHO_MAX_W : LC_IW, SHO_H > LC_IH ? SHO_H : LC_IH);   /* big enough for the launcher picture */
    if (!g_have_metal) {
        NSAlert *al = [NSAlert new];
        al.messageText = @"Metal is not available";
        al.informativeText = @"Super Hang-On Ultimate needs a Mac with Metal graphics (any Mac from 2012 on, macOS 11 or later).";
        [al runModal];
        if (g_md_thread_on) pthread_join(g_md_thread, NULL);
        return 1;
    }
    [NSApp activateIgnoringOtherApps:YES];
    if (g_fullscreen) { g_fullscreen = 0; toggle_fullscreen(); }   /* g_fullscreen follows the window's notifications */

    if (show_launcher) {
        launcher_settings ls; memset(&ls, 0, sizeof ls);
        static const char *views[VIEW_COUNT] = { "FIT SCREEN" };
        for (int i = 0; i < SHO_NSHAPES; i++) views[1 + i] = sho_shapes[i].long_name;
        ls.view_names = views; ls.view_count = VIEW_COUNT; ls.view_desc = "F9 OR CMD+1 TO CMD+7 SWITCH DURING PLAY";
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
            int found = 0; char mp[PATHN];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music/%s.mp3", sho_music_tracks[i].title); path_join(mp, g_dir, n);
                                          if (file_exists(mp)) found++; }
            if (found == 4) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: all 4 tracks found in the music folder");
            else if (found) snprintf(ls.music_note, sizeof ls.music_note, "Arrange: %d of 4 tracks found (others play regular)", found);
            else snprintf(ls.music_note, sizeof ls.music_note, "Arrange tracks not found: regular music will play");
        }
        ls.md_check = md_rom_check;
        NSString *pngpath = [[NSBundle mainBundle] pathForResource:@"launcher_bg" ofType:@"png"];   /* the launcher picture, in the app's Resources */
        NSData *png = pngpath ? [NSData dataWithContentsOfFile:pngpath] : nil;
        lc_set_controls_host(&g_controls_host);
        lc_begin(&ls, rom_check, png ? (const unsigned char *)png.bytes : NULL, png ? (size_t)png.length : 0);
        lc_clock = now_s; lc_present_hook = present_loading;
        g_in_launcher = 1;
        g_win.title = @"Super Hang-On Ultimate";
        int pad_prev = 0;
        while (g_running && !g_quit && lc_result() == LC_RUNNING) {
            @autoreleasepool {
                pump_events();
                int b = launcher_pad_buttons(), e = b & ~pad_prev; pad_prev = b;
                if (e & 1) lc_key(LK_PAD_UP);
                if (e & 2) lc_key(LK_PAD_DOWN);
                if (e & 4) lc_key(LK_PAD_LEFT);
                if (e & 8) lc_key(LK_PAD_RIGHT);
                if (e & 16) lc_key(LK_PAD_A);
                if (e & 32) lc_key(LK_PAD_START);
                if (lc_folder_clicked()) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:g_dir] isDirectory:YES]];
                lc_tick(now_s());
                if (!present_launcher()) usleep(16000);   /* (hidden or minimised: no drawable to wait for) */
            }
        }
        if (!g_running || g_quit || lc_result() != LC_PLAY) {   /* window closed, Cmd+Q or Esc: quit */
            if (g_md_thread_on) pthread_join(g_md_thread, NULL);
            lc_end(); g_in_launcher = 0;
            if (g_win) { g_win.delegate = nil; [g_win orderOut:nil]; }
            return 0;
        }
        lc_finish(&ls);
        ini_set_int("audio", "external_music", ls.arrange_music);
        ini_set_int("game", "demo_sounds", ls.demo_sounds);
        ini_set_int("game", "difficulty", ls.difficulty);
        ini_set_int("game", "time", ls.time_adj);
        g_view_mode = ls.view; save_view_mode();
        ini_set_int("megadrive", "enhancements", ls.md_enhancements);
        ini_set_int("megadrive", "controls", ls.md_controls);
        ini_set_int("megadrive", "music", ls.md_music);
        if (ls.md_enhancements && !g_md_thread_on && find_md_rom(g_md_path, sizeof g_md_path)) start_md_capture();
        {   /* the loading bar: which arranged MP3s will be decoded, and whether the Mega Drive capture is still running */
            int have[4]; char mp[PATHN];
            for (int i = 0; i < 4; i++) { char n[96]; snprintf(n, sizeof n, "music/%s.mp3", sho_music_tracks[i].title); path_join(mp, g_dir, n);
                                          have[i] = ls.arrange_music && file_exists(mp); }
            lc_load_begin(have, g_md_thread_on && !g_md_done);
        }
        lc_load_step(0, 0, "CHECKING THE ARCADE ROM");
    }
    char rmsg[256], detail[256];
    if (!rom_check(rmsg, sizeof rmsg, detail, sizeof detail)) {
        if (g_md_thread_on) pthread_join(g_md_thread, NULL);
        NSAlert *al = [NSAlert new];
        al.alertStyle = NSAlertStyleCritical;
        al.messageText = [NSString stringWithFormat:@"%s", rmsg];
        al.informativeText = [NSString stringWithFormat:@"%s\n\n%s\n\nPut your MAME 'shangon.zip' (Super Hang-On, sitdown/upright, unprotected) in:\n%s",
                              detail, g_rom_err, g_dir];
        [NSApp activateIgnoringOtherApps:YES];
        [al runModal];
        if (g_in_launcher) { lc_end(); g_in_launcher = 0; }
        return 1;
    }
    if (ini_int("audio", "external_music", 1)) {
        char music_dir[PATHN], music_err[512];
        path_join(music_dir, g_dir, "music");
        if (g_in_launcher) sho_music_load_progress = lc_music_progress;
        sho_load_music(music_dir, AUDIO_RATE, music_err, sizeof music_err);
        sho_music_load_progress = NULL;
    }
    g_md_present = md_rom_present();   /* the Mega Drive options only apply with the Mega Drive ROM there */
    load_sound_levels();
    apply_sound_levels(0);
    if (g_md_thread_on) {   /* Original Mode's screens and courses from the Mega Drive ROM: usually ready by now */
        while (!g_md_done) { if (g_in_launcher) lc_load_step(2, 0, "PREPARING ORIGINAL MODE"); usleep(5000); }
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

    if (g_in_launcher) { lc_load_step(3, 1.0f, "STARTING THE GAME"); lc_present_hook = NULL; lc_end(); g_in_launcher = 0; }
    memset(g_keys, 0, sizeof g_keys);
    update_view_width();
    update_title();

    /* keep the display awake and the app out of App Nap while it runs */
    id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical | NSActivityIdleDisplaySleepDisabled
                                                                 reason:@"Super Hang-On Ultimate is running"];
    g_vsync = dispatch_semaphore_create(0);
    if (CVDisplayLinkCreateWithActiveCGDisplays(&g_dl) == kCVReturnSuccess) {
        CVDisplayLinkSetOutputCallback(g_dl, dl_callback, NULL);
        display_link_follow_screen();
        if (CVDisplayLinkStart(g_dl) != kCVReturnSuccess) { CVDisplayLinkRelease(g_dl); g_dl = NULL; }
    } else g_dl = NULL;

    int have_audio = audio_init();
    int16_t samples[2 * 1100];
    int frames_since_save = 0, refresh_check = 0;
    double refresh = display_refresh();
    if (have_audio) { memset(samples, 0, sizeof samples); audio_submit(samples, 1100); audio_submit(samples, 1100); audio_submit(samples, 1000); }
    double acc = 0, next_frame = now_s(), audio_frac = 0;
    int audio_target = AUDIO_TARGET, underruns = 0, diag_n = 0; long game_frames = 0, diag_frames = 0; double diag_t = now_s(), diag_t0 = diag_t;
    sho_diag("display: Metal, reported refresh %.2f Hz, display link %s", refresh, g_dl ? "on" : "off");

    while (g_running) {
        @autoreleasepool {
        pump_events();
        if (!g_running) break;
        if (++refresh_check >= 120) { refresh_check = 0; refresh = display_refresh(); }
        update_view_width();

        /* ---- how many game frames this iteration ----
         * display-locked mode: one loop iteration per display refresh (display link tick). If the refresh is
         * (close to) a multiple of 60 Hz (60 Hz, 120 Hz ProMotion), lock the game to it (k refreshes per game
         * frame) - perfectly even motion. Otherwise distribute 60.05 Hz game frames over refreshes.
         * Without ticks (display asleep, link unavailable): pace by the clock. */
        double period;                      /* game frame period actually used */
        int k = (int)(refresh / 60.0 + 0.5); if (k < 1) k = 1;
        int frames = 0, ticks = 0;
        if (g_dl) {
            if (dispatch_semaphore_wait(g_vsync, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3e9 / refresh))) == 0) ticks = 1;
            while (ticks < 8 && dispatch_semaphore_wait(g_vsync, DISPATCH_TIME_NOW) == 0) ticks++;   /* refreshes we were too slow for */
        }
        if (ticks) {
            int locked = fabs(refresh - 60.0 * k) <= 0.012 * 60.0 * k;
            period = locked ? (double)k / refresh : 1.0 / SHO_FPS;
            acc += ticks / refresh;
            while (acc >= period - 1e-9 && frames < 4) { frames++; acc -= period; }
            if (acc > period * 4) acc = 0;                       /* fell far behind: resync */
            next_frame = now_s();
        } else {
            period = 1.0 / SHO_FPS;
            double t = now_s();
            if (t < next_frame) { wait_until(next_frame); t = now_s(); }
            frames = 1; next_frame += period;
            if (t - next_frame > 0.1) next_frame = t;            /* fell far behind: resync */
        }

        for (int f = 0; f < frames; f++) {
            sho_input in; read_input(&in, (float)period);
            /* Mega Drive mode: Start pauses during a race, as on the Mega Drive, with a menu under PAUSE */
            { static int start_prev, ptick; int start_edge = in.start && !start_prev; start_prev = in.start;
              if (!g_paused && start_edge && sho_md_pause_allowed()) { g_paused = 1; g_pause_menu = sho_pause_begin(); ptick = 0; }
              else if (g_paused && g_pause_menu) {
                  int act = sho_pause_update(&in, ptick++);
                  if (act == 3 || act == 4) { set_view_mode(g_view_mode + (act == 3 ? 1 : -1)); act = 0; }   /* DISPLAY: stays paused */
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
                if (!g_paused && game_frames > 120 && sho_audio_target_step(q, &audio_target)) {   /* ran dry: more cushion */
                    underruns++; sho_diag("sound: queue ran dry (%ld samples); cushion now %d ms", (long)q, audio_target * 1000 / AUDIO_RATE); }
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
        {   /* the log: the speed after 5 and 10 seconds, then every 30 */
            double tp = now_s();
            if (tp - diag_t >= (diag_n < 2 ? 5.0 : 30.0)) {
                double fps = (game_frames - diag_frames) / (tp - diag_t);
                if (diag_n++ && !g_paused) sho_diag("%4.0f s: speed %.1f%% (%.2f game frames a second), reported refresh %.2f Hz, sound cushion %d ms, ran dry %d times",
                                                    tp - diag_t0, fps / SHO_FPS * 100.0, fps, refresh, audio_target * 1000 / AUDIO_RATE, underruns);
                diag_t = tp; diag_frames = game_frames;
            }
        }
        present();
        }
    }

    save_nvram();
    sho_music_unload();
    audio_shutdown();
    if (g_dl) { CVDisplayLinkStop(g_dl); CVDisplayLinkRelease(g_dl); g_dl = NULL; }
    [[NSProcessInfo processInfo] endActivity:activity];
    if (g_win) { g_win.delegate = nil; [g_win orderOut:nil]; }
    return 0;
}

int main(int argc, char **argv) {
    int r;
    @autoreleasepool { r = run(argc, argv); }
    return r;
}
