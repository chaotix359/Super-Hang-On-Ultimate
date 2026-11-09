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
#define SHO_MAX_W 432            /* widest framebuffer (16:9 = 428) */
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
   1 = continue, 2 = exit to the title screen), end on resuming */
int sho_pause_begin(void);
int sho_pause_update(const sho_input *in, int tick);
void sho_pause_end(void);
void sho_set_sound_levels(int music, int sfx);   /* 0-10 each */
void sho_get_sound_levels(int *music, int *sfx);
int sho_width(void);

/* 16:9 mode: extends the view and lets the game place objects in the extra area.
 * Off = bit-exact original behaviour. Takes effect immediately. */
void sho_set_widescreen(int on);
/* Any width between 320 (4:3) and SHO_MAX_W (a little wider than 16:9), for displays of other shapes
 * (e.g. MacBook screens, about 16:10). The game and the HUD use the extra area exactly as in 16:9.
 * sho_set_widescreen(1) = sho_set_view_width(SHO_WIDE_W). Takes effect immediately. */
void sho_set_view_width(int w);
/* the view width that fills a display (or window) of this aspect ratio (width / height) */
int sho_view_width_for_aspect(double aspect);

/* Produce exactly `count` stereo 16-bit samples (interleaved L,R) for the frame just run,
 * resampled to whatever rate the frontend uses (count ~= rate / SHO_FPS). */
void sho_audio(int16_t *out, int count);

/* Battery-backed work RAM (high scores, bookkeeping) */
size_t sho_nvram_size(void);
uint8_t *sho_nvram(void);

#endif
