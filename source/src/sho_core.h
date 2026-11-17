/* Super Hang-On (arcade, Out Run hardware) – portable game core.
 * Runs the original 68000/Z80 program code against a native re-implementation of the board.
 * No platform dependencies: the frontend supplies ROM data, input, and consumes video/audio.
 */
#ifndef SHO_CORE_H
#define SHO_CORE_H
#include <stdint.h>
#include <stddef.h>

#define SHO_W 320
#define SHO_H 224
#define SHO_MAX_W 576            /* widest framebuffer (16:9 = 428, 21:9 = 568; a 2.4:1 screen) */
#define SHO_WIDE_W 428           /* 16:9 at the arcade monitor's pixel shape */
#define SHO_FPS 60.05460   /* 25.1748MHz/4 / (400 x 262) */

typedef struct {
    float steer;      /* -1 = full left, +1 = full right */
    float throttle;   /* 0..1 */
    float brake;      /* 0..1 */
    int coin, start, turbo, service, test;   /* 1 = pressed */
    int pad_a;        /* gamepad A: full throttle while racing; Start on the class/music screens; enter on initials */
    int nav_up, nav_down, nav_left, nav_right;   /* gamepad d-pad or left stick as directions (menus, initials) */
    int pad_b;        /* back / cancel on the Original Mode screens */
} sho_input;

/* Load the ROM set. path = shangon.zip or a folder containing the individual ROM files.
 * Returns 0 on success; on failure fills err with a human-readable message. */
int sho_load_roms(const char *path, char *err, size_t errlen);

/* Optional MP3 replacements, named after the four original music selections.
 * Call after ROM loading and before reset. Missing/invalid tracks retain original audio.
 * Returns the number of replacement tracks loaded (0..4). */
void sho_silence_music(void);
const uint8_t *sho_text_font(void);   /* the arcade's text tiles (ASCII codes = tile numbers), once a ROM set has loaded */
int sho_load_music(const char *directory, unsigned sample_rate, char *err, size_t errlen);

void sho_reset(void);
/* DIP switches (active-low, MAME defaults: coinage 0xFF, dsw 0xFE)
 *  dsw bit0: demo sounds (0 = on), bits1-2: difficulty (11 normal), bits3-4: time adj (11 normal) */
void sho_set_dips(uint8_t coinage, uint8_t dsw);
void sho_set_input(const sho_input *in);
void sho_run_frame(void);

/* sho_width() x 224 pixels, 0x00RRGGBB (320 wide in 4:3 mode, SHO_WIDE_W in 16:9, or the sho_set_view_width width) */
const uint32_t *sho_framebuffer(void);
/* Mega Drive mode pause menu: begin (0 = not allowed now), update every paused frame (returns 0 = stay,
   1 = continue, 2 = exit to the title screen, 3 / 4 = next / previous display shape: the frontend switches it and
   stays paused), end on resuming. sho_pause_set_display: the display shape's name for the menu's DISPLAY line. */
int sho_pause_begin(void);
/* A frontend that shows stereo 3D (sho_present_frame) calls sho_pause_enable_3d(1): the pause menu then has a 3D
   page with the 3D settings and the sho_stereo_menu_extra lines, and sho_pause_update can also return 5 (a 3D
   setting changed: save it) and 6 (one of the frontend's lines changed: apply and save it); both stay paused.
   sho_pause_goto_3d, right after sho_pause_begin, opens the menu on that page. */
void sho_pause_enable_3d(int on);
void sho_pause_goto_3d(void);
int sho_pause_update(const sho_input *in, int tick);
void sho_pause_end(void);
void sho_pause_set_display(const char *name);

/* a small log for problem reports: sho_diag_open("<game folder>/shangon.log") once, then sho_diag(printf-style) lines */
void sho_diag_open(const char *path);
void sho_diag(const char *fmt, ...);
void sho_set_sound_levels(int music, int sfx);   /* 0-10 each */
void sho_get_sound_levels(int *music, int *sfx);
int sho_width(void);

/* 16:9 mode: extends the view and lets the game place objects in the extra area.
 * Off = bit-exact original behaviour. Takes effect immediately. */
void sho_set_widescreen(int on);
/* Any width between 320 (4:3) and SHO_MAX_W (21:9 and a little more), for displays of other shapes (MacBook
 * screens, ultrawides; sho_views.h lists the named shapes). The game and the HUD use the extra area exactly as in
 * 16:9. sho_set_widescreen(1) = sho_set_view_width(SHO_WIDE_W). Takes effect immediately: the current frame is
 * drawn again at the new width (so a paused picture changes shape too). */
void sho_set_view_width(int w);
/* the view width that fills a display (or window) of this aspect ratio (width / height) */
int sho_view_width_for_aspect(double aspect);

/* Stereo 3D, shown side by side (left eye | right eye) for 3D TVs, 3D glasses and VR desktop viewers.
 * Every layer is drawn at its own depth: the backdrop far away, the road and everything standing on it at its
 * distance, the HUD and menus on the screen itself. Off = the game exactly as before.
 *  mode   SHO_3D_HALF_SBS: both eyes squeezed into the normal picture shape (3D TVs; "half SBS" in viewers);
 *         SHO_3D_FULL_SBS: each eye keeps the full picture shape, the whole image is twice as wide.
 *         (The core draws the same pixels for both; the frontend shows them at aspect x 1 or x 2.)
 *  depth  0..SHO_3D_MAX_DEPTH: how far apart the two eyes' pictures of the horizon are, in game pixels.
 *  pop    0..10: how far the nearest road comes out in front of the screen, in tenths of depth (0 = nothing
 *         in front of it). The horizon keeps its distance behind the screen whatever this is.
 *  swap   1 = right eye first (cross-eyed viewing, or a display that has them the other way round). */
enum { SHO_3D_OFF, SHO_3D_HALF_SBS, SHO_3D_FULL_SBS };
/* The 3D picture has this many columns for each game pixel (a power of 2), so that depth moves things by fractions
 * of a pixel: sho_present_frame's 3D picture is 2 x SHO_3D_SUB x sho_width() wide. */
#define SHO_3D_SUB 4
#define SHO_3D_MAX_DEPTH 24
typedef struct { int mode, depth, pop, swap; } sho_stereo;
void sho_set_stereo(const sho_stereo *s);
/* Optional: a worker thread of the frontend's, so the two eyes are drawn at the same time on two cores.
 * start(job) makes the worker call job() once and returns at once; wait() returns when that job has finished. */
void sho_stereo_set_worker(void (*start)(void (*job)(void)), void (*wait)(void));
void sho_get_stereo(sho_stereo *s);
/* The picture to put on screen: sho_framebuffer() itself with 3D off, else left | right, *w = 2 x SHO_3D_SUB x sho_width()
 * (224 lines either way; valid until the next core call). Also carries the 3D menu when that is shown. */
const uint32_t *sho_present_frame(int *w);
/* The 3D menu drawn over the picture: show/hide it, pass it keys (0 up, 1 down, 2 left, 3 right; returns 1 when
 * a setting changed, for the frontend to save). The game keeps running underneath, so changes are seen at once. */
void sho_stereo_menu(int show);
void sho_stereo_menu_hint(const char *text);   /* the menu's bottom line (at most 22 letters): how to work it */
/* more lines under the 3D settings, each an int of the frontend's (name in capitals, at most 13 letters);
 * left / right change it by step within lo..hi and sho_stereo_menu_key then returns 2. label NULL removes them. */
void sho_stereo_menu_extra(const char *label, int *value, int lo, int hi, int step);
/* ... or a line that steps through named choices (value 0..count-1, going round; names at most 8 letters).
 * Up to 8 lines in all. */
void sho_stereo_menu_extra_choice(const char *label, int *value, int count, const char *const *names);
void sho_stereo_menu_extra_text(int i, char *buf, int n);
int sho_stereo_menu_shown(void);
int sho_stereo_menu_key(int key);

/* Produce exactly `count` stereo 16-bit samples (interleaved L,R) for the frame just run,
 * resampled to whatever rate the frontend uses (count ~= rate / SHO_FPS). */
void sho_audio(int16_t *out, int count);

/* Battery-backed work RAM (high scores, bookkeeping) */
size_t sho_nvram_size(void);
uint8_t *sho_nvram(void);

#endif
