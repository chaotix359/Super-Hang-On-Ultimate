/* Mega Drive graphics for Original Mode, taken at start-up from the player's own Mega Drive ROM.
 * The Mega Drive game is run headless for a few seconds of game time (68000 + VDP, no sound), driven
 * into its Original Mode screens, and the finished pictures (with the text layer removed), the font
 * and the text palettes are kept. Nothing from the Mega Drive ROM is stored on disk. */
#ifndef SHO_MDGFX_H
#define SHO_MDGFX_H
#include <stdint.h>
#include <stddef.h>

enum { MDG_TITLE, MDG_COMMAND, MDG_PARTS0, MDG_NIMG = MDG_PARTS0 + 6 };   /* parts screen with each category's highlight */
enum { MDP_RED, MDP_YELLOW, MDP_GREEN, MDP_WHITE };                   /* text palettes */

typedef struct {
    int ok;
    uint32_t img[MDG_NIMG][320 * 224];
    uint32_t backdrop;                 /* border colour for the 16:9 sides */
    uint8_t glyph[128][64];            /* 8x8 pixel indices (0 = transparent) per ASCII character */
    uint8_t has_glyph[128];
    uint32_t pal[4][16];               /* text palettes */
    uint8_t *rom; long romsz;          /* the Mega Drive ROM (kept in memory for its course data) */
} mdgfx_t;

/* a Mega Drive Original Mode course's look, for the arcade renderer */
#define MDS_MAX_W 1024
#define MDS_MAX_H 160
typedef struct {
    int ok;
    uint32_t road[2][5];          /* [light/dark band][kerb, ground, road, edge line, centre line] */
    uint32_t sky;                 /* colour above the background strip */
    int w, h;                     /* background strip: full plane width, lines down to the horizon */
    uint32_t strip[MDS_MAX_H][MDS_MAX_W];
    int base[MDS_MAX_H];          /* strip column shown at screen x = 0 when captured */
    /* plane A: the nearer layer (sea and rocks, hills...) - top byte 0xFF = opaque; scrolls 4/3 as fast */
    int wa;
    uint32_t stripa[MDS_MAX_H][MDS_MAX_W];
    int basea[MDS_MAX_H];
    /* roadside objects, as the Mega Drive draws them at their nearest (largest) size */
    int nobj;
    struct { int kind, w, h, ax, ay; uint32_t *pix; int norm_h; } obj[32];   /* norm_h: height at the reference distance */   /* pix: w*h, top byte 0xFF = opaque; (ax,ay) = anchor */
} mdscene_t;
const void *mdscene_object(const mdscene_t *sc, int kind);   /* the obj entry for a kind, or NULL */
/* the Mega Drive race HUD (LAP TIME, SPEED, progress bar), static parts; top byte 0xFF = opaque */
typedef struct {
    int ok;
    uint32_t img[40][320];
    uint32_t pal[4][16];          /* HUD palettes (time = line 1, ticks = line 2, speed = line 3) */
    uint8_t bar_empty[64], bar_tick[64], bar_start[64];   /* 8x8 tiles of the bar (pixel indices, palette line 1/2) */
} mdhud_t;
extern mdhud_t *mdhud;
#define MD_NRACES 7

extern mdgfx_t *mdgfx;
extern mdscene_t *mdscene[MD_NRACES];  /* per Original Mode race (NULL if not captured) */                 /* NULL until captured */

/* path: Mega Drive ROM (.md/.bin/.gen) or a .zip containing it. Returns 1 on success. */
int mdgfx_capture(const char *path, char *err, size_t errlen);
extern volatile int mdgfx_progress;   /* 0..100 while mdgfx_capture runs */
/* just check the file (for the launcher): 1 = the right ROM */
int mdgfx_check(const char *path, char *msg, size_t msglen);

/* live Mega Drive for the Original Mode screens */
int mdlive_boot(void);                         /* new game: power on and go to the command screen */
int mdlive_load(const char *path);             /* continue from a save */
int mdlive_save(const char *path);
int mdlive_saved(const char *path);            /* a save exists */
void mdlive_frame(unsigned buttons, uint32_t *img);   /* one frame with the pad (U D L R A B C S bits); img: 320x224 */
void mdlive_frames(int n);                     /* n frames without a picture or input */
unsigned mdlive_state(void);                   /* the game's state ($FFC704) */
unsigned mdlive_r16(unsigned a); uint32_t mdlive_r32(unsigned a);
void mdlive_w16(unsigned a, unsigned v); void mdlive_w32(unsigned a, uint32_t v);
uint32_t mdlive_backdrop(void);
void mdlive_stop(void);
#endif
