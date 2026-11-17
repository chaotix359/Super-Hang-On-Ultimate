/* Super Hang-On video: palette, 16B-alt tilemaps, text layer, Out Run road, System 16B sprites.
 * Layer order and priority rules follow MAME's screen_update_shangon. */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "sho_core.h"
#include "sho_internal.h"
#include <stdio.h>
#include "sho_mdgfx.h"
#ifdef SHO_VIDEO_EYE      /* the second copy (sho_video_eye.c): its own names, its own machine */
#define sho_md_scene sho_e_md_scene
#define sho_md_scene_ptr sho_e_md_scene_ptr
#define sho_md_scene_set sho_e_md_scene_set
#define sho_video_arcade_text sho_e_arcade_text
#define sho_video_extra_sprites sho_e_extra_sprites
#define sho_video_hide_player sho_e_hide_player
#define sho_video_hide_title_text sho_e_hide_title_text
#define sho_video_horizon sho_e_horizon
#define sho_video_init sho_e_init
#define sho_video_nextra sho_e_nextra
#define sho_video_render sho_e_render
#define sho_video_render_eye sho_e_render_eye
#define sho_video_road_extent sho_e_road_extent
#define sho_video_set_margin sho_e_set_margin
#define sho_video_stereo_cache sho_e_stereo_cache
#define sho_video_stereo_params sho_e_stereo_params
#define sho_video_title_recent sho_e_title_recent
#define sho_video_width sho_e_width
#define md_obj_kind sho_e_obj_kind
#define sho_frame_no sho_e_frame_no
#define sho_menu_last_frame sho_e_menu_last_frame
#define sho_obj_scale sho_e_obj_scale
extern sho_machine *sho_e_M;
#define M sho_e_M
extern int sho_e_sky_acc;         /* the Mega Drive sky's scroll position for the frame (from the first copy) */
#else
int sho_video_sky_acc;
#endif
const mdscene_t *sho_md_scene;
int sho_video_hide_player;   /* leave out the player's bike (for the goal sequence's background) */
void *sho_md_scene_ptr(void) { return (void *)sho_md_scene; }      /* (for the 3D pictures' copy of the video state, sho_core.c) */
void sho_md_scene_set(void *p) { sho_md_scene = (const mdscene_t *)p; }

#define NPAL 4096
static uint8_t tilepix[4096][64];          /* decoded 8x8x3 tiles */
static uint8_t roadgfx[513][512];          /* decoded road rows */
static uint8_t pal_normal[32], pal_shadow[32], pal_hilight[32];
static uint32_t rgb[NPAL * 2];
static uint16_t fb16[SHO_H][SHO_MAX_W * SHO_3D_SUB];    /* palette indices (0..8191) */
static uint8_t pri[SHO_H][SHO_MAX_W * SHO_3D_SUB];
static uint16_t sbm[SHO_H][SHO_MAX_W * SHO_3D_SUB];     /* sprite bitmap, 0xffff = empty */
/* Widescreen: the visible area extends g_xo pixels beyond each side of the original 320. */
static int g_xo = 0, g_w = SHO_W;
void sho_video_set_margin(int xo) { if (xo < 0) xo = 0; if (xo > (SHO_MAX_W - SHO_W) / 2) xo = (SHO_MAX_W - SHO_W) / 2; g_xo = xo; g_w = SHO_W + 2 * xo; }
int sho_video_width(void) { return g_w; }

/* Stereo 3D. The picture is drawn once more for each eye with every layer moved sideways by its depth:
 * the backdrop is far away, each road line and each sprite sit at the distance the game itself has for them
 * (the road generator's size index for the line; the object's size step, noted by the M_002A0C override), the
 * text layer / HUD is on the screen itself.
 * st_eye 0 = the normal picture (no shifts: bit-exact, and the only pass with side effects), -1 / +1 = left / right
 * eye, 2 = the normal picture again without side effects.
 * The eye pictures are SHO_3D_SUB times as wide (st_k; each game pixel that many columns), so that shifts are
 * fractions of a game pixel: depth then changes smoothly along the road instead of in whole-pixel bands. All
 * shifts below are in those columns. */
static int st_eye, st_depth = 8, st_pop = 0;
static int st_k = 1, st_lg = 0;    /* columns per game pixel in this pass, and its log2 */
static int st_far;                 /* this eye's shift (columns to the right) of the backdrop */
static int16_t st_row[SHO_H];      /* ... of each road line */
static int16_t st_step[128];       /* ... of an object of each size step */
static int st_road;                /* the frame has a road */
static int road_horizon(void);
extern uint8_t sho_obj_scale[256];
#define ST_BIAS (1 << 20)
static inline int st_px(int col) { return ((col + ST_BIAS) >> st_lg) - (ST_BIAS >> st_lg); }   /* column -> game pixel (floor) */
void sho_video_stereo_params(int depth, int pop) { st_depth = depth < 0 ? 0 : depth > 32 ? 32 : depth; st_pop = pop < 0 ? 0 : pop > 10 ? 10 : pop; }
static int eye_shift(int eye, double par) {      /* par = right-eye x minus left-eye x in game pixels (positive = behind the screen) */
    int P = (int)floor(par * st_k + 0.5), h = P >= 0 ? P / 2 : -((1 - P) / 2);   /* h = floor(P / 2): odd disparities are kept */
    return eye < 0 ? -h : P - h;
}
/* q = how many screen rows below the horizon a thing at this distance stands on a flat road (0 = infinitely far,
   ST_QNEAR = the bottom row). The road's far end stops short of the backdrop (3/4 of its distance) and the far
   rows come forward faster than plain perspective (^0.7), so distant objects stand clear of the backdrop. */
#define ST_QNEAR 95.0
static int q_shift(int eye, double q) {
    double D = st_depth, c = st_pop / 10.0, n = q <= 0 ? 0.0 : q >= ST_QNEAR ? 1.0 : q / ST_QNEAR, m = pow(n, 0.7);
    return eye_shift(eye, 0.75 * D * (1.0 - m) - D * c * m);
}
static inline uint16_t rb(int i);
static void stereo_setup(int eye) {
    st_eye = eye; st_far = 0; st_k = 1; st_lg = 0; st_road = 0;
    memset(st_row, 0, sizeof st_row); memset(st_step, 0, sizeof st_step);
    if (eye != -1 && eye != 1) return;
    st_k = SHO_3D_SUB; for (st_lg = 0; (1 << st_lg) < st_k; st_lg++) {}
    double D = st_depth;
    int yh = road_horizon(), ctl = M->road_control & 3;
    st_far = eye_shift(eye, D);     /* the horizon keeps its full distance whatever the pop-out */
    if (yh > SHO_H - 16) {          /* no road (title, menus): the backdrop behind, everything else on the screen */
        st_far = eye_shift(eye, D * 0.5); st_far -= st_far % st_k;
        return;
    }
    st_far -= st_far % st_k;        /* (whole game pixels: the tile layers are drawn pixel by pixel) */
    st_road = 1;
    int prev = q_shift(eye, 0);
    for (int y = 0; y < SHO_H; y++) {
        /* the road generator's size index for this line: 2 per screen row on a flat road, and the true distance on hills */
        uint16_t d0 = rb(y), d1 = rb(0x100 + y); int v = -1;
        if (ctl != 3 && !(d0 & 0x800)) v = (d0 >> 1) & 0xff;
        else if (ctl != 0 && !(d1 & 0x800)) v = (d1 >> 1) & 0xff;
        if (v >= 0) prev = q_shift(eye, v * 0.5);
        else if (y <= yh) prev = q_shift(eye, 0);
        st_row[y] = (int16_t)prev;                 /* (a line without road below the horizon keeps the line above's) */
    }
    /* objects: on a flat road an object of size step s stands 0.87 s - 8 rows below the horizon (measured) */
    for (int i = 0; i < 128; i++) st_step[i] = (int16_t)q_shift(eye, 0.87 * i - 8.0);
}

#define CB1 0x7f6
#define CB2 0x7c0
#define CB3 0x7c0

static void resistor_table(const double *r, int n, double *w) {
    /* compute_resistor_weights(0,255,-1,...): output = 255 * sum(on conductances)/sum(all), scaled so all-on = 255 */
    double tot = 0; for (int i = 0; i < n; i++) tot += 1.0 / r[i];
    for (int i = 0; i < n; i++) w[i] = 255.0 * (1.0 / r[i]) / tot;
}

void sho_video_init(void) {
    for (int t = 0; t < 4096; t++)
        for (int y = 0; y < 8; y++) {
            uint8_t b0 = M->tiles[t * 8 + y], b1 = M->tiles[0x8000 + t * 8 + y], b2 = M->tiles[0x10000 + t * 8 + y];
            for (int x = 0; x < 8; x++) {
                int s = 7 - x;
                tilepix[t][y * 8 + x] = (uint8_t)((((b2 >> s) & 1) << 2) | (((b1 >> s) & 1) << 1) | ((b0 >> s) & 1));
            }
        }
    for (int y = 0; y < 512; y++) {
        const uint8_t *src = M->road + (((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % 0x10000);
        for (int x = 0; x < 512; x++) {
            uint8_t v = (uint8_t)(((src[x >> 3] >> (~x & 7)) & 1) | (((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1));
            if (x >= 256 - 8 && x < 256 && v == 3) v |= 4;
            roadgfx[y][x] = v;
        }
    }
    memset(roadgfx[512], 3, 512);
    static const double rn[5] = { 3900, 2000, 1000, 500, 250 };
    static const double rs[6] = { 3900, 2000, 1000, 500, 250, 470 };
    double wn[5], ws[6];
    resistor_table(rn, 5, wn); resistor_table(rs, 6, ws);
    for (int v = 0; v < 32; v++) {
        double n = 0, s = 0;
        for (int b = 0; b < 5; b++) if (v >> b & 1) { n += wn[b]; s += ws[b]; }
        pal_normal[v] = (uint8_t)(n + 0.5); pal_shadow[v] = (uint8_t)(s + 0.5);
        double h = s + ws[5]; pal_hilight[v] = (uint8_t)(h > 255 ? 255 : h + 0.5);
    }
}

static void build_palette(void) {
    for (int i = 0; i < NPAL; i++) {
        uint16_t d = be16r(M->palram, i * 2);
        int r = ((d >> 12) & 1) | ((d << 1) & 0x1e), g = ((d >> 13) & 1) | ((d >> 3) & 0x1e), b = ((d >> 14) & 1) | ((d >> 7) & 0x1e);
        rgb[i] = ((uint32_t)pal_normal[r] << 16) | ((uint32_t)pal_normal[g] << 8) | pal_normal[b];
        const uint8_t *e = (d & 0x8000) ? pal_hilight : pal_shadow;
        rgb[NPAL + i] = ((uint32_t)e[r] << 16) | ((uint32_t)e[g] << 8) | e[b];
    }
}

/* ---------------- road ---------------- */
static inline uint16_t rb(int i) { return be16r(M->roadbuf, i * 2); }   /* displayed (buffered) road RAM */

/* The backdrop - the road generator's plain lines and the two tile layers - is the same for both eyes but for a
 * whole-pixel shift, and it is most of the work of a picture. With 3D on it is drawn once a frame, ST_BM pixels
 * wider on each side, into bk16 / bkpri (by the normal picture, or by the first eye if that did not); the eyes
 * copy their part of it. ly_* say where road_background and draw_layer draw. */
#define ST_BM 16
#define bk16 sho_bk16
#define bkpri sho_bkpri
#define bk_ok sho_bk_ok
#define bk_w sho_bk_w
#define bk_sc sho_bk_sc
#ifdef SHO_VIDEO_EYE      /* (the second copy reads the first copy's) */
extern uint16_t bk16[SHO_H][SHO_MAX_W + 2 * ST_BM]; extern uint8_t bkpri[SHO_H][SHO_MAX_W + 2 * ST_BM];
extern int bk_ok, bk_w, bk_sc;
#else
uint16_t bk16[SHO_H][SHO_MAX_W + 2 * ST_BM]; uint8_t bkpri[SHO_H][SHO_MAX_W + 2 * ST_BM];
int bk_ok, bk_w, bk_sc;
#endif
static int st_cache;      /* 3D is on, so the normal picture keeps the backdrop */
static uint16_t *ly16 = &fb16[0][0]; static uint8_t *lypri = &pri[0][0];
static int ly_pitch = SHO_MAX_W * SHO_3D_SUB, ly_w = SHO_W, ly_xoff = 0;
void sho_video_stereo_cache(int on) { st_cache = on; }
static void road_background(void) {
    int ctl = M->road_control & 3;
    for (int y = 0; y < SHO_H; y++) {
        uint16_t d0 = rb(y), d1 = rb(0x100 + y); int c = -1;
        switch (ctl) {
        case 0: if (d0 & 0x800) c = d0 & 0x7f; break;
        case 1: if (d0 & 0x800) c = d0 & 0x7f; else if (d1 & 0x800) c = d1 & 0x7f; break;
        case 2: if (d1 & 0x800) c = d1 & 0x7f; else if (d0 & 0x800) c = d0 & 0x7f; break;
        case 3: if (d1 & 0x800) c = d1 & 0x7f; break;
        }
        if (c != -1) { uint16_t *d = ly16 + (size_t)y * ly_pitch; for (int x = 0; x < ly_w; x++) d[x] = (uint16_t)(c | CB3); }
    }
}

static const uint8_t prio_map[2][8] = { { 0x80,0x81,0x81,0x87,0,0,0,0x00 }, { 0x81,0x81,0x81,0x8f,0,0,0,0x80 } };

static void road_foreground(void) {
    int ctl = M->road_control & 3;
    for (int y = 0; y < SHO_H; y++) {
        uint16_t d0 = rb(y), d1 = rb(0x100 + y);
        if ((d0 & 0x800) && (d1 & 0x800)) continue;
        const uint8_t *s0 = (d0 & 0x800) ? roadgfx[512] : roadgfx[0x000 + ((d0 >> 1) & 0xff)];
        const uint8_t *s1 = (d1 & 0x800) ? roadgfx[512] : roadgfx[0x100 + ((d1 >> 1) & 0xff)];
        int h0 = rb(0x200 + (d0 & 0x1ff)) & 0xfff, c0 = rb(0x600 + (d0 & 0x1ff));
        int h1 = rb(0x400 + (d1 & 0x1ff)) & 0xfff, c1 = rb(0x600 + (d1 & 0x1ff));
        uint16_t ct[32] = { 0 };
        ct[0x00] = CB1 ^ 0x00 ^ ((c0 >> 0) & 1); ct[0x01] = CB1 ^ 0x02 ^ ((c0 >> 1) & 1); ct[0x02] = CB1 ^ 0x04 ^ ((c0 >> 2) & 1);
        ct[0x03] = (d0 & 0x200) ? ct[0x00] : (CB2 ^ 0x00 ^ ((c0 >> 8) & 0xf)); ct[0x07] = CB1 ^ 0x06 ^ ((c0 >> 3) & 1);
        ct[0x10] = CB1 ^ 0x08 ^ ((c1 >> 4) & 1); ct[0x11] = CB1 ^ 0x0a ^ ((c1 >> 5) & 1); ct[0x12] = CB1 ^ 0x0c ^ ((c1 >> 6) & 1);
        ct[0x13] = (d1 & 0x200) ? ct[0x10] : (CB2 ^ 0x10 ^ ((c1 >> 8) & 0xf)); ct[0x17] = CB1 ^ 0x0e ^ ((c1 >> 7) & 1);
        h0 = (h0 - 0x5f8 - g_xo) & 0xfff; h1 = (h1 - 0x5f8 - g_xo) & 0xfff;
        uint16_t *dst = fb16[y];
        if (st_k > 1) {   /* an eye's picture: st_k columns per pixel, the line shifted by its depth */
            int H0 = (h0 << st_lg) - st_row[y] + ST_BIAS * 4, H1 = (h1 << st_lg) - st_row[y] + ST_BIAS * 4, W = g_w * st_k;   /* (ST_BIAS: a multiple of 0x1000 columns) */
            const uint8_t *pm = prio_map[ctl == 2];
            if ((ctl == 0 && (d0 & 0x800)) || (ctl == 3 && (d1 & 0x800))) continue;
            for (int x = 0; x < W; x++) {
                int a0 = ((H0 + x) >> st_lg) & 0xfff, a1 = ((H1 + x) >> st_lg) & 0xfff;
                int p0 = a0 < 0x200 ? s0[a0] : 3, p1 = a1 < 0x200 ? s1[a1] : 3;
                dst[x] = ctl == 0 ? ct[p0] : ctl == 3 ? ct[0x10 + p1] : ((pm[p0] >> p1) & 1) ? ct[0x10 + p1] : ct[p0];
            }
            continue;
        }
        switch (ctl) {
        case 0:
            if (d0 & 0x800) break;
            for (int x = 0; x < g_w; x++) { int p0 = h0 < 0x200 ? s0[h0] : 3; dst[x] = ct[p0]; h0 = (h0 + 1) & 0xfff; }
            break;
        case 1: case 2: {
            const uint8_t *pm = prio_map[ctl - 1];
            for (int x = 0; x < g_w; x++) {
                int p0 = h0 < 0x200 ? s0[h0] : 3, p1 = h1 < 0x200 ? s1[h1] : 3;
                dst[x] = ((pm[p0] >> p1) & 1) ? ct[0x10 + p1] : ct[p0];
                h0 = (h0 + 1) & 0xfff; h1 = (h1 + 1) & 0xfff;
            }
            break; }
        case 3:
            if (d1 & 0x800) break;
            for (int x = 0; x < g_w; x++) { int p1 = h1 < 0x200 ? s1[h1] : 3; dst[x] = ct[0x10 + p1]; h1 = (h1 + 1) & 0xfff; }
            break;
        }
    }
}

/* ---------------- tilemaps ---------------- */
static inline uint16_t txt(int wordidx) { return be16r(M->textram, (wordidx * 2) & 0xfff); }
static uint16_t lat_page[4], lat_ys[4], lat_xs[4];


static void draw_layer(int which, int category, uint8_t primask) {
    uint16_t pages = lat_page[which], ys = lat_ys[which], xs = lat_xs[which];
    for (int y = 0; y < SHO_H; y++) {
        uint16_t rs = txt(0xf80 / 2 + 0x40 / 2 * which + y / 8);
        uint16_t ex = (xs & 0x8000) ? rs : xs, ey = ys, ep = pages;
        int alt = (rs & 0x8000) != 0;
        if (alt) { ex = lat_xs[which + 2]; ey = lat_ys[which + 2]; ep = lat_page[which + 2]; }
        int sx = (0xc0 - ex) & 0x3ff;
        int colscroll = (ys & 0x8000) && !alt;
        int last_key = -1, color = 0, cat = 0; const uint8_t *tp = tilepix[0];
        uint16_t *d16 = ly16 + (size_t)y * ly_pitch; uint8_t *dpr = lypri + (size_t)y * ly_pitch;
        for (int x = 0; x < ly_w; x++) {
            int eyy = ey, scr_x = x - ly_xoff;      /* scr_x: position in original screen coordinates */
            if (colscroll) { int col = (scr_x + 8 + 16 * 4) / 16 - 4; if (col < 0) col = 0; if (col > 20) col = 20;
                             eyy = txt(0xf16 / 2 + 0x40 / 2 * which + col); }
            int vy = (y + (eyy & 0x1ff)) & 511, vx = (scr_x + sx) & 1023;
            /* Widescreen: race backdrops are two-page panoramas whose scroll is limited so the
             * join at virtual x=512 stays just beyond the 4:3 edge. In the extra right-hand area,
             * mirror the left page's end instead of showing the mismatched join (a mirrored skyline
             * reads as more of the same; a repeated column looked like a fence across 21:9's margin). */
            if (scr_x >= SHO_W && vx >= 512 && vx < 512 + g_xo + 16 + (scr_x >= SHO_W + g_xo ? ST_BM : 0)) vx = 1023 - vx;   /* the left page's end, mirrored */
            int key = ((vy >> 3) << 7) | (vx >> 3);
            if (key != last_key) {          /* new tile: fetch tilemap entry once per 8 pixels */
                last_key = key;
                int quad = ((vy >> 8) & 1) * 2 + ((vx >> 9) & 1);
                int page = (ep >> (quad * 4)) & 0xf;
                uint16_t d = be16r(M->tileram, (page * 64 * 32 + ((vy >> 3) & 31) * 64 + ((vx >> 3) & 63)) * 2);
                color = (d >> 5) & 0x7f; cat = (d >> 15) & 1;
                tp = tilepix[(d & 0x1fff) & 0xfff] + (vy & 7) * 8;
                if (cat != category) { /* skip quickly to next tile boundary */ }
            }
            if (cat != category) continue;
            int pix = tp[vx & 7];
            if (pix) { d16[x] = (uint16_t)(color * 8 + pix); dpr[x] |= primask; }
        }
    }
}

static void put_text_tile(uint16_t d, int ty, int sx) {
    const uint8_t *px = tilepix[d & 0xff];
    int color = (d >> 8) & 7;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        int p = px[y * 8 + x], X = sx + x;
        if (p && X >= 0 && X < g_w) for (int k = 0; k < st_k; k++) { fb16[ty * 8 + y][X * st_k + k] = (uint16_t)(color * 8 + p); pri[ty * 8 + y][X * st_k + k] |= 0x08; }
    }
}

/* The race HUD is recognised by its TOP / TIME / SCORE label boxes (text row 1). */
static int race_hud_visible(void) {
    return (txt(1 * 64 + 24 + 2) & 0xff) == 0x94 && (txt(1 * 64 + 24 + 16) & 0xff) == 0xA0 && (txt(1 * 64 + 24 + 23) & 0xff) == 0xB0;
}
/* 16:9 HUD layout: TOP score, course, stage and the progress bar move to the left edge, TIME stays
 * centred, SCORE and SPEED move to the right edge. col = text column within the 40-column screen. */
static int hud_shift(int row, int col) {
    if (row == 6) return -g_xo;                               /* progress bar (long on 18-stage courses) */
    if (row >= 1 && row <= 3) return col <= 15 ? -g_xo : col <= 22 ? 0 : g_xo;
    if (row == 4 || row == 5) return col <= 16 ? -g_xo : col <= 21 ? 0 : g_xo;
    return 0;
}

int sho_video_hide_title_text;
static int title_seen;   /* frames since the title screen's INSERT COINS was last up */
int sho_video_title_recent(void) { return title_seen < 150; }
/* text in the arcade's own 8x8 font and text palette (colour 0-7), straight into a finished frame */
void sho_video_arcade_text(uint32_t *fb, int fbw, int x, int y, const char *s, int color) {
    for (; *s; s++, x += 8) {
        const uint8_t *px = tilepix[(uint8_t)*s];
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
            int p = px[j * 8 + i], X = x + i, Y = y + j;
            if (p && X >= 0 && X < fbw && Y >= 0 && Y < SHO_H) fb[Y * fbw + X] = rgb[(color & 7) * 8 + p];
        }
    }
}   /* the mode choice is up under the logo: no INSERT COINS */
/* the title screen's INSERT COINS (text row 14, columns 13-24): left of the logo's centre on the board (152 v 167); drawn under it */
static int insert_coins_row(void) {
    static const char m[] = "INSERT COINS";
    for (int i = 0; i < 12; i++) if ((txt(14 * 64 + 24 + 13 + i) & 0xff) != (uint8_t)m[i]) return 0;
    return 1;
}
static void draw_text(int category) {
    int hud = g_xo && race_hud_visible();
    int coins = insert_coins_row();
    if (category == 0 && !st_eye) { if (coins) title_seen = 0; else if (title_seen < 1000) title_seen++; }
    int skip_hud = sho_md_scene && race_hud_visible();   /* Mega Drive course: its own HUD is drawn instead */
    for (int ty = 0; ty < 28; ty++) {
        if (skip_hud) continue;   /* (checkpoint messages too: the Mega Drive has none) */
        for (int tx = 24; tx < 64; tx++) {
            uint16_t d = txt(ty * 64 + tx);
            if (((d >> 15) & 1) != category) continue;
            int sx = tx * 8 - 192 + g_xo + (hud ? hud_shift(ty, tx - 24) : 0);
            if (coins && ty == 14) { if (sho_video_hide_title_text) continue; sx += 15; }   /* centred under the logo and SEGA (x 167) */
            put_text_tile(d, ty, sx);
        }
        /* Full-width text rows (e.g. the dissolving curtain at the start of a race) continue into the
         * 16:9 margins using the row's edge tiles. */
        if (g_xo) {
            uint16_t l = txt(ty * 64 + 24), r = txt(ty * 64 + 63);
            if ((l & 0xfff) && (r & 0xfff)) {
                if (((l >> 15) & 1) == category) for (int sx = g_xo - 8; sx > -8; sx -= 8) put_text_tile(l, ty, sx);
                if (((r >> 15) & 1) == category) for (int sx = g_xo + SHO_W; sx < g_w; sx += 8) put_text_tile(r, ty, sx);
            }
        }
    }
}

/* ---------------- sprites (System 16B) ---------------- */
/* Mega Drive course objects: drawn from the Mega Drive pictures into an RGB layer in place of the
   arcade sprite that carries them (same position, scaled from its height) */
extern unsigned char md_obj_kind[256];
static uint32_t mdo[SHO_H][SHO_MAX_W * SHO_3D_SUB]; static uint8_t mdo_pri[SHO_H][SHO_MAX_W * SHO_3D_SUB]; static int mdo_used;
#define MD_OBJ_REF_H 58
static void draw_md_object(const mdscene_t *sc, int kind, int x0, int x1, int top, int bottom, int sprpri) {
    const struct { int kind, w, h, ax, ay; uint32_t *pix; int norm_h; } *o = mdscene_object(sc, kind);
    if (!o || bottom <= top) return;
    int dh = (bottom - top) * o->norm_h / MD_OBJ_REF_H; if (dh < 1) return;
    int dw = o->w * dh / o->h; if (dw < 1) dw = 1;
    dw *= st_k;      /* (x0, x1 are columns) */
    int cx = (x0 + x1) / 2, mirror = (cx - g_xo < SHO_W / 2) != (o->ax < o->w / 2 ? 0 : 0);
    (void)mirror;
    int left = cx - dw / 2, ytop = bottom - dh;
    for (int y = 0; y < dh; y++) {
        int Y = ytop + y; if (Y < 0 || Y >= SHO_H) continue;
        int sy = y * o->h / dh;
        for (int x = 0; x < dw; x++) {
            int X = left + x; if (X < 0 || X >= g_w * st_k) continue;
            uint32_t c = o->pix[sy * o->w + x * o->w / dw];
            if (c >> 24) { mdo[Y][X] = c | 0xff000000u; mdo_pri[Y][X] = (uint8_t)sprpri; sbm[Y][X] = 0xffff; }
        }
    }
    mdo_used = 1;
}
/* sprites added to the list by the goal sequence: drawn by the same code as the game's own (hardware zoom, art tiers) */
uint16_t sho_video_extra_sprites[4][6]; int sho_video_nextra;
static void draw_one_sprite(const mdscene_t *sc, uint16_t d0, uint16_t d1, uint16_t d2, uint16_t d3, uint16_t d4, uint16_t d5, int base) {
    {
        /* the game stores X with 12 bits; the 4:3 hardware only decodes 9 */
        int bottom = d0 >> 8, top = d0 & 0xff, xpos = d1 & (g_xo ? 0xfff : 0x1ff);
        int hide = d2 & 0x4000, flip = d2 & 0x100, pitch = (int8_t)(d2 & 0xff);
        uint16_t addr = d3;
        int bank = ((d4 >> 8) & 0xf) % 8;
        int colpri = ((d4 & 0xff) << 4);
        int vzoom = (d5 >> 5) & 0x1f, hzoom = d5 & 0x1f;
        uint16_t scratch = addr;
        int wb = base >= 0 && !st_eye;      /* the hardware's write-back: once a frame, by the normal picture */
        /* stereo: at the object's own distance (its size step, when the override noted one for this slot), else at
           the distance of the road line it stands on */
        int ssh = 0;
        if (st_k > 1 && st_road) {
            int stp = base >= 0 ? sho_obj_scale[(base >> 4) & 0xff] : 0xff;
            ssh = stp < 128 ? st_step[stp] : st_row[bottom < 1 ? 0 : bottom > SHO_H ? SHO_H - 1 : bottom - 1];
        }
        if (hide || top >= bottom) { if (wb) be16w(M->sprram, base + 14, scratch); return; }
        if (base >= 0 && sho_video_hide_player && bottom >= 190 && xpos - 184 >= 80 && xpos - 184 <= 240) return;   /* the player's bike: bottom centre */
        int mdk = sc ? md_obj_kind[d4 & 0x3f] : 0, mx0 = 9999, mx1 = -9999, my0 = 9999, my1 = -9999;
        static uint16_t mrow[SHO_MAX_W * SHO_3D_SUB];
        const uint16_t *sd = M->sprites + 0x10000 * bank;
        uint16_t zacc = d5 & 0x03ff;
        for (int y = top; y < bottom; y++) {
            addr = (uint16_t)(addr + pitch);
            zacc = (uint16_t)(zacc + (vzoom << 10));
            if (zacc & 0x8000) { addr = (uint16_t)(addr + pitch); zacc &= ~0x8000; }
            if (y < 0 || y >= SHO_H) continue;
            int xacc = 4 * hzoom, x = xpos;
            uint16_t *dst = mdk ? mrow : sbm[y];
            if (mdk) for (int i = 0; i < g_w * st_k; i++) mrow[i] = 0xffff;
            if (!flip) {
                scratch = (uint16_t)(addr - 1);
                for (int guard = 0; ((xpos - x) & 0x1ff) != 1 && guard < 512; guard++) {
                    uint16_t w = sd[++scratch]; int pix = 0;
                    for (int s = 12; s >= 0; s -= 4) {
                        pix = (w >> s) & 0xf; xacc = (xacc & 0x3f) + hzoom;
                        if (xacc < 0x40) { if (pix != 0 && pix != 15) { int sx = (x - 184 + g_xo) * st_k + ssh; for (int k = 0; k < st_k; k++, sx++) if (sx >= 0 && sx < g_w * st_k) dst[sx] = (uint16_t)(colpri | pix); } x++; }
                    }
                    if (pix == 15) break;
                }
            } else {
                scratch = (uint16_t)(addr + 1);
                for (int guard = 0; ((xpos - x) & 0x1ff) != 1 && guard < 512; guard++) {
                    uint16_t w = sd[--scratch]; int pix = 0;
                    for (int s = 0; s <= 12; s += 4) {
                        pix = (w >> s) & 0xf; xacc = (xacc & 0x3f) + hzoom;
                        if (xacc < 0x40) { if (pix != 0 && pix != 15) { int sx = (x - 184 + g_xo) * st_k + ssh; for (int k = 0; k < st_k; k++, sx++) if (sx >= 0 && sx < g_w * st_k) dst[sx] = (uint16_t)(colpri | pix); } x++; }
                    }
                    if (pix == 15) break;
                }
            }
            if (mdk) for (int i = 0; i < g_w * st_k; i++) if (mrow[i] != 0xffff) { if (i < mx0) mx0 = i; if (i > mx1) mx1 = i; if (y < my0) my0 = y; if (y > my1) my1 = y; }
        }
        if (mdk && mx1 >= mx0) draw_md_object(sc, mdk - 1, mx0, mx1, my0, my1 + 1, (colpri >> 10) & 3);
        if (wb) { be16w(M->sprram, base + 10, zacc); be16w(M->sprram, base + 14, scratch); }
    }
}
static void draw_sprites(void) {
    const mdscene_t *sc = sho_md_scene && sho_md_scene->ok && sho_md_scene->nobj ? sho_md_scene : NULL;
    if (mdo_used) { memset(mdo, 0, sizeof mdo); mdo_used = 0; }
    for (int i = 0; i < 0x1000 / 2; i += 8) {
        unsigned base = i * 2;
        uint16_t d0 = be16r(M->sprram, base), d1 = be16r(M->sprram, base + 2), d2 = be16r(M->sprram, base + 4);
        uint16_t d3 = be16r(M->sprram, base + 6), d4 = be16r(M->sprram, base + 8), d5 = be16r(M->sprram, base + 10);
        if (d2 & 0x8000) break;
        draw_one_sprite(sc, d0, d1, d2, d3, d4, d5, (int)base);
    }
    for (int k = 0; k < sho_video_nextra; k++) { uint16_t *e = sho_video_extra_sprites[k]; draw_one_sprite(sc, e[0], e[1], e[2], e[3], e[4], e[5], -1); }
}

/* Mega Drive Original Mode course look (set by sho_md.c while racing a converted course):
 * road/ground/sky colours replace the arcade road palette (light/dark band pairs), the arcade's two
 * parallax tile layers are not drawn and the Mega Drive's single background strip fills the sky,
 * scrolled with the arcade's background scroll. */
static void md_scene_palette(const mdscene_t *sc) {
    static const int slot[4] = { 2, 3, 0, 4 };              /* arcade road pixel pairs: road, edge line, kerb, centre */
    for (int b = 0; b < 2; b++) {
        rgb[0x7f6 ^ b] = sc->road[b][slot[0]]; rgb[0x7f4 ^ b] = sc->road[b][slot[1]];
        rgb[0x7f2 ^ b] = sc->road[b][slot[2]]; rgb[0x7f0 ^ b] = sc->road[b][slot[3]];
        rgb[0x7fe ^ b] = sc->road[b][slot[0]]; rgb[0x7fc ^ b] = sc->road[b][slot[1]];
        rgb[0x7fa ^ b] = sc->road[b][slot[2]]; rgb[0x7f8 ^ b] = sc->road[b][slot[3]];
    }
    for (int i = 0; i < 16; i++) rgb[0x7c0 + i] = sc->road[i & 1][1];
    for (int i = 0x7d0; i < 0x7f0; i++) rgb[i] = sc->sky;
}
static int road_horizon(void) {   /* first line the road generator draws road on */
    int ctl = M->road_control & 3;
    for (int y = 0; y < SHO_H; y++) {
        int r0 = !(rb(y) & 0x800), r1 = !(rb(0x100 + y) & 0x800);
        if (ctl == 0 ? r0 : ctl == 3 ? r1 : (r0 || r1)) return y;
    }
    return SHO_H;
}

int sho_video_horizon(void) { return road_horizon(); }
/* the road's extent on a screen line straight from the road generator's data (not from colours): first and last
   pixel that is road (or kerb), 0 if the line has none */
int sho_video_road_extent(int y, int *l, int *r) {
    int ctl = M->road_control & 3; *l = *r = -1;
    uint16_t d0 = rb(y), d1 = rb(0x100 + y);
    for (int layer = 0; layer < 2; layer++) {
        uint16_t d = layer ? d1 : d0;
        if ((layer == 0 && (ctl == 3)) || (layer == 1 && (ctl == 0))) continue;
        if (d & 0x800) continue;
        const uint8_t *s = roadgfx[(layer ? 0x100 : 0x000) + ((d >> 1) & 0xff)];
        int h = rb((layer ? 0x400 : 0x200) + (d & 0x1ff)) & 0xfff; h = (h - 0x5f8 - g_xo) & 0xfff;
        for (int x = 0; x < g_w; x++) { int p = h < 0x200 ? s[h] : 3; if (p != 3) { if (*l < 0) *l = x; *r = x; } h = (h + 1) & 0xfff; }
    }
    return *l >= 0;
}

void sho_video_render(uint32_t *fb) {
    build_palette();
    const mdscene_t *sc = sho_md_scene && sho_md_scene->ok ? sho_md_scene : NULL;
    if (sc) md_scene_palette(sc);
    for (int i = 0; i < 4; i++) { lat_page[i] = txt(0xe80 / 2 + i); lat_ys[i] = txt(0xe90 / 2 + i); lat_xs[i] = txt(0xe98 / 2 + i); }
    for (int y = 0; y < SHO_H; y++) for (int x = 0; x < g_w * st_k; x++) sbm[y][x] = 0xffff;

    int eye = st_k > 1;
    if (!eye) bk_ok = 0;                               /* a new normal picture: the kept backdrop is the old frame's */
    if ((eye && !bk_ok) || (!eye && st_cache)) {      /* the backdrop, wide, into bk16 / bkpri */
        ly16 = &bk16[0][0]; lypri = &bkpri[0][0]; ly_pitch = SHO_MAX_W + 2 * ST_BM; ly_w = g_w + 2 * ST_BM; ly_xoff = g_xo + ST_BM;
        for (int y = 0; y < SHO_H; y++) { memset(bk16[y], 0, (size_t)ly_w * 2); memset(bkpri[y], 0, (size_t)ly_w); }
        road_background();
        if (!sc) {
            draw_layer(1, 0, 0x01); draw_layer(1, 1, 0x02);   /* background */
            draw_layer(0, 0, 0x02); draw_layer(0, 1, 0x04);   /* foreground */
        }
        bk_ok = 1; bk_w = g_w; bk_sc = sc != NULL;
        ly16 = &fb16[0][0]; lypri = &pri[0][0]; ly_pitch = SHO_MAX_W * SHO_3D_SUB;
    }
    if (bk_ok && bk_w == g_w && bk_sc == (sc != NULL)) {      /* this picture's part of it (an eye's: shifted, st_k columns a pixel) */
        int sh = st_far / st_k;
        for (int y = 0; y < SHO_H; y++) {
            const uint16_t *s16 = bk16[y] + ST_BM - sh; const uint8_t *spr = bkpri[y] + ST_BM - sh;
            if (st_k == 1) { memcpy(fb16[y], s16, (size_t)g_w * 2); memcpy(pri[y], spr, (size_t)g_w); }
            else { uint16_t *d = fb16[y]; uint8_t *q = pri[y];
                   for (int x = 0; x < g_w; x++) { uint16_t v = s16[x]; uint8_t pv = spr[x]; for (int k = 0; k < st_k; k++) { *d++ = v; *q++ = pv; } } }
        }
    } else {
        for (int y = 0; y < SHO_H; y++) { memset(fb16[y], 0, (size_t)g_w * 2); memset(pri[y], 0, (size_t)g_w); }
        ly_w = g_w; ly_xoff = g_xo;
        road_background();
        if (!sc) {
            draw_layer(1, 0, 0x01); draw_layer(1, 1, 0x02);   /* background */
            draw_layer(0, 0, 0x02); draw_layer(0, 1, 0x04);   /* foreground */
        }
    }
    road_foreground();
    draw_text(0); draw_text(1);
    draw_sprites();

    int hz = sc ? road_horizon() : 0, sxo = 0;
    if (sc) {   /* the arcade background layer's horizontal scroll moves the Mega Drive strip */
        uint16_t xs = lat_xs[1], rs = txt(0xf80 / 2 + 0x40 / 2 * 1 + (hz > 8 ? hz - 8 : 0) / 8);
        /* On long corners the arcade swaps its backdrop's tilemap pages and moves the scroll by 512 at the same moment
           (the same picture, one page along). Following the scroll alone made the Mega Drive strip jump half its width:
           follow the frame-to-frame movement instead, taking a 512 step as no movement. */
        static const mdscene_t *psc; static int pv = -1, acc;
        int v = (0xc0 - ((xs & 0x8000) ? rs : xs)) & 0x3ff;
#ifdef SHO_VIDEO_EYE
        psc = sc; pv = v; acc = sho_e_sky_acc;      /* (the first copy keeps count) */
#endif
        if (psc != sc || pv < 0) acc = v;
        else { int d = ((v - pv + 512) & 1023) - 512; if (d > 256) d -= 512; else if (d < -256) d += 512; acc += d; }   /* (unbounded: the strips wrap with their own width masks, near layer at 4/3 too) */
        psc = sc; pv = v;
#ifndef SHO_VIDEO_EYE
        sho_video_sky_acc = acc;
#endif
        sxo = (acc * sc->w) / 1024;
    }
    for (int y = 0; y < SHO_H; y++) {
        uint32_t *o = fb + y * g_w * st_k;
        if (sc && y < hz) {   /* sky: Mega Drive strip, bottom edge on the arcade horizon */
            int sy = sc->h - (hz - y);
            for (int x = 0; x < g_w * st_k; x++) {
                if (mdo_used && mdo[y][x]) { o[x] = mdo[y][x] & 0xffffff; continue; }
                if (pri[y][x] || sbm[y][x] != 0xffff) { int idx = fb16[y][x]; uint16_t s2 = sbm[y][x];
                    if (s2 != 0xffff && (1 << ((s2 >> 10) & 3)) > pri[y][x]) idx = ((s2 & 0x03f0) == 0x03f0) ? idx + NPAL : 0x400 | (s2 & 0x3ff);
                    o[x] = rgb[idx & (NPAL * 2 - 1)]; continue; }
                if (sy < 0) { o[x] = sc->sky; continue; }
                uint32_t a = sc->stripa[sy][(sc->basea[sy] + (st_px(x) - st_far / st_k - g_xo) + sxo * 4 / 3) & (sc->wa - 1)];   /* near layer moves 4/3 as fast */
                o[x] = (a >> 24) ? a & 0xffffff : sc->strip[sy][(sc->base[sy] + (st_px(x) - st_far / st_k - g_xo) + sxo) & (sc->w - 1)];
            }
            continue;
        }
        for (int x = 0; x < g_w * st_k; x++) {
            int idx = fb16[y][x];
            uint16_t s = sbm[y][x];
            if (mdo_used && mdo[y][x] && (1 << mdo_pri[y][x]) > pri[y][x]) { o[x] = mdo[y][x] & 0xffffff; continue; }
            if (s != 0xffff) {
                int sp = (s >> 10) & 3;
                if ((1 << sp) > pri[y][x]) {
                    if ((s & 0x03f0) == 0x03f0) idx += NPAL;      /* shadow */
                    else idx = 0x400 | (s & 0x3ff);
                }
            }
            o[x] = rgb[idx & (NPAL * 2 - 1)];
        }
    }
    {   /* wide views on the class and music select screens: their tilemaps are one page wide, so beyond the 4:3
           right-hand edge the next page (another copy of the map, or backwards text) would show. Fill that side
           with each row's plain background colour, as the left-hand side already shows it. */
        extern long sho_menu_last_frame, sho_frame_no;
        if (g_xo && sho_frame_no - sho_menu_last_frame <= 2)
            for (int y = 0; y < SHO_H; y++) { uint32_t *o = fb + y * g_w * st_k, c = o[0]; for (int x = (g_xo + SHO_W) * st_k; x < g_w * st_k; x++) o[x] = c; }
    }
}

/* one eye's picture (eye -1 left, +1 right, 2 = the normal picture without side effects); see stereo_setup */
void sho_video_render_eye(uint32_t *fb, int eye) { stereo_setup(eye); sho_video_render(fb); stereo_setup(0); }
