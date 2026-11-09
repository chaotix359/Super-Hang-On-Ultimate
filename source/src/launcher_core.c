/* Super Hang-On Ultimate launcher, the part shared by Windows, macOS and Linux (see launcher_core.h): the settings
 * drawn on a 640x360 canvas at the bottom left of the launcher picture, under the logo and clear of the bikes, in the
 * arcade's own 8x8 text font (from the text tiles of the user's shangon.zip; until the ROM is found, a built-in
 * stand-in in the same outlined style), and the version under the right-hand bike.
 * Keys: Up/Down choose a line, Left/Right (or a click) change it, Enter / A / Start on PLAY plays, Esc quits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "launcher_core.h"
#include "launcher_font.h"
#include "sho_core.h"
#include "sho_music.h"
#include "sho_mdgfx.h"
#include "sho_version.h"
#include "third_party/puff/puff.h"

#define CW LC_CW
#define CH LC_CH
static uint32_t cv[CW * CH];       /* ARGB: transparent where the picture shows through */

/* the arcade's text colours (as on the title screen and the race HUD) */
#define OUTLINE 0x000000
#define WHITE   0xF7F7F7
#define YELLOW  0xF7F700
#define GREEN   0x00F700
#define ORANGE  0xF74200
#define RED     0xF70000
#define LIGHT   0xC5C5E6
#define DIM     0x6B6B84
#define PANEL   0xA8040A24         /* translucent dark blue behind the settings */
#define LINE_H_WRAP 10             /* line spacing of the smaller print */

/* ------------------------------------------------------------------ the picture: a small PNG reader (8-bit RGB / RGBA) */
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }
static uint32_t *png_decode(const unsigned char *png, size_t len, int *w, int *h) {
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (!png || len < 8 || memcmp(png, sig, 8)) return NULL;
    size_t pos = 8, zl = 0; int W = 0, H = 0, bpp = 0;
    unsigned char *z = malloc(len); if (!z) return NULL;
    while (pos + 12 <= len) {
        uint32_t n = rd32(png + pos); const unsigned char *type = png + pos + 4, *d = png + pos + 8;
        if (pos + 12 + n > len) break;
        if (!memcmp(type, "IHDR", 4)) {
            W = (int)rd32(d); H = (int)rd32(d + 4);
            if (d[8] != 8 || (d[9] != 2 && d[9] != 6) || d[12] != 0) { free(z); return NULL; }   /* 8-bit RGB(A), not interlaced */
            bpp = d[9] == 6 ? 4 : 3;
        } else if (!memcmp(type, "IDAT", 4)) { memcpy(z + zl, d, n); zl += n; }
        else if (!memcmp(type, "IEND", 4)) break;
        pos += 12 + n;
    }
    if (!W || !H || zl < 6) { free(z); return NULL; }
    size_t stride = (size_t)W * bpp, rawlen = (stride + 1) * H;
    unsigned char *raw = malloc(rawlen); uint32_t *px = malloc((size_t)W * H * 4);
    unsigned long dl = rawlen, sl = zl - 2;
    if (!raw || !px || puff(raw, &dl, z + 2, &sl) != 0 || dl != rawlen) { free(z); free(raw); free(px); return NULL; }   /* zlib: skip the 2-byte header */
    free(z);
    for (int y = 0; y < H; y++) {   /* undo the row filters in place */
        unsigned char *r = raw + y * (stride + 1), f = r[0], *c = r + 1, *u = y ? c - (stride + 1) : NULL;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? c[i - bpp] : 0, b = u ? u[i] : 0, cc = u && i >= (size_t)bpp ? u[i - bpp] : 0;
            switch (f) { case 1: c[i] += a; break; case 2: c[i] += b; break; case 3: c[i] += (a + b) / 2; break; case 4: c[i] += paeth(a, b, cc); break; }
        }
        for (int x = 0; x < W; x++) px[y * W + x] = 0xFF000000u | c[x * bpp] << 16 | c[x * bpp + 1] << 8 | c[x * bpp + 2];
    }
    free(raw);
    *w = W; *h = H;
    return px;
}

/* ------------------------------------------------------------------ the font */
/* 8x8 glyphs, pixel values as in the arcade's text tiles: 0 clear, 1 outline, 2/3 letter */
static uint8_t glyph[128][64];
static int font_from_rom, font_ready;
static void stand_in(int c) {   /* the built-in 5x7 letter with an outline, in the arcade font's style */
    const uint8_t *g = lf_glyph((char)c);
    uint8_t *o = glyph[c];
    memset(o, 0, 64);
    for (int r = 0; r < 7; r++) for (int b = 0; b < 5; b++) if (g[r] & (0x10 >> b)) {
        int X = b + 1, Y = r + 1;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int x = X + dx, y = Y + dy;
            if (x >= 0 && x < 8 && y >= 0 && y < 8 && !o[y * 8 + x]) o[y * 8 + x] = 1;
        }
    }
    for (int r = 0; r < 7; r++) for (int b = 0; b < 5; b++) if (g[r] & (0x10 >> b) && r + 1 < 8) o[(r + 1) * 8 + b + 1] = 3;
}
static void load_font(int rom_ok) {
    if (font_from_rom) return;
    const uint8_t *t = rom_ok ? sho_text_font() : NULL;
    if (!t && font_ready) return;
    for (int c = 32; c < 128; c++) {
        int have = 0;
        if (t && !(c >= 'a' && c <= 'z')) {   /* the arcade's lower-case tiles are pieces of the logo */
            for (int y = 0; y < 8; y++) {
                uint8_t b0 = t[c * 8 + y], b1 = t[0x8000 + c * 8 + y], b2 = t[0x10000 + c * 8 + y];
                for (int x = 0; x < 8; x++) {
                    int s = 7 - x, p = ((b2 >> s) & 1) << 2 | ((b1 >> s) & 1) << 1 | ((b0 >> s) & 1);
                    glyph[c][y * 8 + x] = (uint8_t)(p > 3 ? 3 : p);
                    have |= p;
                }
            }
        }
        if (c >= 'a' && c <= 'z') continue;                          /* (drawn as capitals) */
        if (c == ':') have = 0;                                      /* the arcade's ':' tile is an arrow */
        if (!have && c != ' ') stand_in(c);                         /* not in the arcade font: ' " ( ) , / _ ... */
        if (c == ' ') memset(glyph[c], 0, 64);
    }
    font_from_rom = t != NULL; font_ready = 1;
}

static void put(int x, int y, uint32_t argb) { if (x >= 0 && x < CW && y >= 0 && y < CH) cv[y * CW + x] = argb; }
static void fill(int x, int y, int w, int h, uint32_t argb) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, argb); }
/* text in the game font at scale s (8 px per letter at scale 1) */
static int text(int x, int y, const char *t, uint32_t c, int s) {
    int x0 = x;
    for (; *t; t++, x += 8 * s) {
        int ch = (unsigned char)*t; if (ch >= 'a' && ch <= 'z') ch -= 32; if (ch < 32 || ch > 127) ch = '?';
        const uint8_t *g = glyph[ch];
        for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) if (g[j * 8 + i])
            fill(x + i * s, y + j * s, s, s, 0xFF000000u | (g[j * 8 + i] == 1 ? OUTLINE : c));
    }
    return x - x0;
}
static int text_w(const char *t, int s) { return (int)strlen(t) * 8 * s; }
/* word-wrapped to `cols` letters; returns the lines used (at most `maxl`) */
static int text_wrap(int x, int y, const char *t, uint32_t c, int cols, int maxl) {
    int lines = 0;
    while (*t && lines < maxl) {
        int n = (int)strlen(t);
        if (n > cols) { n = cols; while (n > 0 && t[n] != ' ') n--; if (n == 0) n = cols; }
        char ln[128]; snprintf(ln, sizeof ln, "%.*s", n < 127 ? n : 127, t);
        text(x, y + lines * LINE_H_WRAP, ln, c, 1);
        t += n; while (*t == ' ') t++;
        lines++;
    }
    return lines;
}

/* ------------------------------------------------------------------ the settings */
enum { O_DEMO, O_DIFF, O_TIME, O_MUSIC, O_VIEW, O_MDE, O_MDC, O_PLAY, NOPT };
static const char *const names[NOPT] = { "DEMO SOUNDS", "DIFFICULTY", "TIME LIMIT", "MUSIC", "DISPLAY", "MEGA DRIVE ENHANCEMENTS", "APPLY MEGA DRIVE CONTROLS", "PLAY" };
static const char *const onoff[2] = { "ON", "OFF" }, *const levels[4] = { "EASY", "NORMAL", "HARD", "HARDEST" };
static const char *const musics[3] = { "REGULAR SOUNDTRACK", "ARRANGE SOUNDTRACK", "MEGA DRIVE MUSIC" };
static const char *const default_views[3] = { "FIT WINDOW", "16:9 WIDESCREEN", "4:3 ORIGINAL" };

static launcher_settings S;        /* a copy of the frontend's settings (names, folder, checks) */
static launcher_rom_check_fn check_rom;
static int val[NOPT], cur, rom_ok, md_ok, last_nonmd, result = LC_RUNNING, folder_click, blink;
static double last_check = -1;
static char rom_msg[256], rom_detail[256], md_msg[256], md_detail[256];
static int loading;                /* PLAY chosen: the loading bar replaces the key help */
static float load_frac; static char load_msg[64];

static int nviews(void) { return S.view_names && S.view_count > 0 ? S.view_count : 3; }
static const char *view_name(int v) { return S.view_names && S.view_count > 0 ? S.view_names[v % S.view_count] : default_views[v % 3]; }
static int music_md_ok(void) { return md_ok && val[O_MDE]; }
static int nvals(int o) { return o == O_DEMO || o == O_MDE || o == O_MDC ? 2 : o == O_MUSIC ? 3 : o == O_VIEW ? nviews() : o == O_PLAY ? 1 : 4; }
static int enabled(int o) { return (o == O_MDE || o == O_MDC) ? md_ok : o == O_PLAY ? rom_ok : 1; }
static void change(int o, int d) {
    if (!enabled(o) || o == O_PLAY) return;
    int n = nvals(o);
    if (o == O_MDE || o == O_MDC) { val[o] = !val[o]; if (o == O_MDE && !music_md_ok() && val[O_MUSIC] == 2) val[O_MUSIC] = last_nonmd; return; }
    int v = val[o];
    for (int k = 0; k < n; k++) {
        v = (v + d + n) % n;
        if (o == O_MUSIC && v == 2 && !music_md_ok()) continue;   /* Mega Drive Music needs the ROM and the Enhancements */
        break;
    }
    val[o] = v;
    if (o == O_MUSIC && v != 2) last_nonmd = v;
}
static const char *value_text(int o) {
    switch (o) {
    case O_DEMO: return onoff[val[o] ? 0 : 1];
    case O_DIFF: case O_TIME: return levels[val[o] & 3];
    case O_MUSIC: return musics[val[o] % 3];
    case O_VIEW: return view_name(val[o]);
    case O_MDE: case O_MDC: return val[o] ? "[X]" : "[ ]";
    }
    return "";
}
static const char *desc(int o) {
    switch (o) {
    case O_DEMO: return "SOUND DURING THE ATTRACT MODE";
    case O_DIFF: return "HOW OFTEN RIVAL BIKES APPEAR";
    case O_TIME: return "TIME ALLOWED TO REACH EACH CHECKPOINT";
    case O_MUSIC: return S.music_note[0] ? S.music_note : "THE FOUR MUSIC-SELECT SONGS";
    case O_VIEW: return S.view_desc ? S.view_desc : "F9 SWITCHES DURING PLAY";
    case O_MDE: return md_ok ? "ORIGINAL MODE: PRESS START ON THE TITLE SCREEN TO CHOOSE IT" : "NEEDS THE SUPER HANG-ON (WORLD) MEGA DRIVE ROM IN THE GAME FOLDER";
    case O_MDC: return md_ok ? "MEGA DRIVE STEERING, IN ARCADE AND ORIGINAL MODE" : "NEEDS THE SUPER HANG-ON (WORLD) MEGA DRIVE ROM IN THE GAME FOLDER";
    case O_PLAY: return rom_ok ? "ENTER, A OR START TO PLAY" : "PUT SHANGON.ZIP IN THE GAME FOLDER";
    }
    return "";
}

static void refresh(void) {
    rom_ok = check_rom(rom_msg, sizeof rom_msg, rom_detail, sizeof rom_detail);
    if (S.md_check) md_ok = S.md_check(md_msg, sizeof md_msg, md_detail, sizeof md_detail);
    else { md_ok = 0; md_msg[0] = md_detail[0] = 0; }
    if (!music_md_ok() && val[O_MUSIC] == 2) val[O_MUSIC] = last_nonmd;
    load_font(rom_ok);
}

/* layout (canvas pixels): a panel at the bottom left, under the logo, left of the bikes */
#define PX 0                       /* panel: flush with the left and bottom edges */
#define PY 104
#define PW 322
#define PH (CH - PY)
#define X0 (PX + 16)               /* text */
#define VX (X0 + 104)              /* values of the first five settings */
#define BX (X0 + 208)              /* the Mega Drive tick boxes */
#define COLS 35                    /* letters per wrapped line */
#define FOLDER_Y (PY + PH - 25)
#define VER_CX 556                 /* the version: centred under the right-hand bike's rear wheel */
#define VER_Y 344
static int row_y(int i) { return i < O_MDE ? PY + 10 + i * 12 : i < O_PLAY ? PY + 76 + (i - O_MDE) * 12 : PY + 104; }

static void draw(void) {
    memset(cv, 0, sizeof cv);
    fill(PX, PY, PW, PH, PANEL);
    for (int i = 0; i < NOPT; i++) {
        int y = row_y(i), en = enabled(i), sel = i == cur;
        if (i == O_PLAY) {
            int w = text_w("PLAY", 2), x = PX + (PW - w) / 2;
            uint32_t c = loading ? WHITE : en ? (sel && blink ? WHITE : YELLOW) : DIM;
            text(x, y, "PLAY", c, 2);
            if (sel && en) { text(x - 20, y + 4, ">", YELLOW, 1); text(x + w + 12, y + 4, "<", YELLOW, 1); }
            continue;
        }
        if (sel && blink && !loading) text(X0 - 12, y, ">", YELLOW, 1);   /* the cursor of the title screen's mode choice */
        text(X0, y, names[i], !en ? DIM : sel ? YELLOW : WHITE, 1);
        const char *v = value_text(i);
        if (i == O_MDE || i == O_MDC) { text(BX, y, v, !en ? DIM : sel ? YELLOW : WHITE, 1); continue; }
        if (sel) { text(VX - 10, y, "<", YELLOW, 1); text(VX + text_w(v, 1) + 2, y, ">", YELLOW, 1); }
        text(VX, y, v, sel ? YELLOW : WHITE, 1);
    }
    int y = row_y(O_PLAY) + 26;
    text_wrap(X0, y, desc(cur), LIGHT, COLS, 2); y += 2 * LINE_H_WRAP + 6;
    /* the ROM checks: one line each when all is well, the detail as well when not */
    y += text_wrap(X0, y, rom_msg, rom_ok ? GREEN : RED, COLS, 2) * LINE_H_WRAP;
    if (!rom_ok) y += text_wrap(X0, y, rom_detail, ORANGE, COLS, 2) * LINE_H_WRAP;
    y += 4;
    if (S.md_check) {
        y += text_wrap(X0, y, md_msg, md_ok ? GREEN : LIGHT, COLS, 2) * LINE_H_WRAP;
        if (!md_ok) y += text_wrap(X0, y, md_detail, DIM, COLS, 2) * LINE_H_WRAP;
    }
    {   /* the game folder (the end of the path when it is long; a click opens it) and the keys, at the bottom */
        const char *d = S.game_dir ? S.game_dir : "";
        char gd[160]; int n = (int)strlen(d), room = COLS - 7;
        if (n > room) snprintf(gd, sizeof gd, "FOLDER ...%s", d + n - (room - 3)); else snprintf(gd, sizeof gd, "FOLDER %s", d);
        text(X0, FOLDER_Y, gd, DIM, 1);
        if (!loading) text(X0, PY + PH - 11, "ARROWS SET  ENTER PLAYS  ESC QUITS", DIM, 1);
        else {   /* the loading bar, in the gap between the ROM lines and the folder line: white frame, orange fill (the INSERT COINS colour), what is happening written in it */
            int bx = PX + 12, bw = PW - 24, by = FOLDER_Y - 24, bh = 12, fw = (int)((bw - 4) * (load_frac < 0 ? 0 : load_frac > 1 ? 1 : load_frac) + 0.5f);
            fill(bx - 1, by - 1, bw + 2, bh + 2, 0xFF000000u);
            fill(bx, by, bw, bh, 0xFF000000u | WHITE);
            fill(bx + 1, by + 1, bw - 2, bh - 2, 0xFF000000u);
            fill(bx + 2, by + 2, bw - 4, bh - 4, 0xFF10184A);
            if (fw > 0) { fill(bx + 2, by + 2, fw, bh - 4, 0xFF000000u | ORANGE); fill(bx + 2, by + 2, fw, 1, 0xFFF79C42); }
            char m[40]; snprintf(m, sizeof m, "%.36s", load_msg);
            text(bx + (bw - text_w(m, 1)) / 2, by + 2, m, WHITE, 1);
        }
    }
    {   /* the version, under the right-hand bike's rear wheel */
        char v[32]; snprintf(v, sizeof v, "V%s ALPHA", SHO_VERSION);
        text(VER_CX - text_w(v, 1) / 2, VER_Y, v, WHITE, 1);
    }
}

static int hit(int mx, int my) {   /* the setting under the mouse (canvas pixels), or -1 */
    for (int i = 0; i < NOPT; i++) {
        int y = row_y(i), h = i == O_PLAY ? 16 : 8;
        if (my >= y - 2 && my < y + h + 2 && mx >= PX && mx < PX + PW) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ the picture */
static uint32_t *bg; static int bg_w, bg_h;
static uint32_t *img;                       /* LC_IW x LC_IH, made on request */
static uint32_t *img_cv;                    /* the canvas img was made from */

void lc_begin(launcher_settings *s, launcher_rom_check_fn check, const unsigned char *png, size_t png_len) {
    S = *s; check_rom = check;
    val[O_DEMO] = s->demo_sounds != 0; val[O_DIFF] = s->difficulty & 3; val[O_TIME] = s->time_adj & 3;
    last_nonmd = s->arrange_music ? 1 : 0;
    val[O_MUSIC] = s->md_music && s->md_enhancements ? 2 : last_nonmd;
    val[O_VIEW] = s->view >= 0 && s->view < nviews() ? s->view : 0;
    val[O_MDE] = s->md_enhancements != 0; val[O_MDC] = s->md_controls != 0;
    result = LC_RUNNING; folder_click = 0; loading = 0; last_check = -1;
    refresh();
    cur = rom_ok ? O_PLAY : O_DEMO;
    if (!bg) bg = png_decode(png, png_len, &bg_w, &bg_h);
    free(img_cv); img_cv = NULL;
    draw();
}
void lc_key(int k) {
    if (result != LC_RUNNING || loading) return;
    switch (k) {
    case LK_UP: cur = (cur + NOPT - 1) % NOPT; break;
    case LK_DOWN: cur = (cur + 1) % NOPT; break;
    case LK_LEFT: change(cur, -1); break;
    case LK_RIGHT: change(cur, 1); break;
    case LK_ENTER: case LK_PAD_START: if (rom_ok) { cur = O_PLAY; result = LC_PLAY; } break;   /* Return / Start plays from any line */
    case LK_SPACE: case LK_PAD_A: if (cur == O_PLAY) { if (rom_ok) result = LC_PLAY; } else change(cur, 1); break;
    case LK_ESC: result = LC_QUIT; break;
    }
    draw();
}
void lc_click(int mx, int my) {
    if (result != LC_RUNNING || loading) return;
    int i = hit(mx, my);
    if (i == O_PLAY) { cur = i; if (rom_ok) result = LC_PLAY; }
    else if (i >= 0 && (cur == i || i == O_MDE || i == O_MDC)) { cur = i; change(i, mx < VX && i != O_MDE && i != O_MDC ? -1 : 1); }
    else if (i >= 0) cur = i;
    else if (my >= FOLDER_Y - 2 && my < FOLDER_Y + 10 && mx >= PX && mx < PX + PW && S.game_dir) folder_click = 1;
    draw();
}
int lc_folder_clicked(void) { int f = folder_click; folder_click = 0; return f; }
void lc_tick(double now) {
    if (last_check < 0) last_check = now;
    if (now - last_check > 2.5 && (!rom_ok || !md_ok) && result == LC_RUNNING) { refresh(); last_check = now; }   /* files added while it is up */
    blink = ((long)(now * 3.0)) & 1;
    draw();
}
int lc_result(void) { return result; }
void lc_finish(launcher_settings *s) {
    s->demo_sounds = val[O_DEMO]; s->difficulty = val[O_DIFF]; s->time_adj = val[O_TIME];
    s->arrange_music = val[O_MUSIC] == 1; s->md_music = val[O_MUSIC] == 2 && music_md_ok();
    s->view = val[O_VIEW];
    if (md_ok) { s->md_enhancements = val[O_MDE]; s->md_controls = val[O_MDC]; }   /* greyed out: keep the saved choices */
}
void lc_end(void) {
    free(bg); bg = NULL; free(img); img = NULL; free(img_cv); img_cv = NULL; loading = 0;
}

const uint32_t *lc_canvas(void) { return cv; }
const uint32_t *lc_background(int *w, int *h) { if (w) *w = bg_w; if (h) *h = bg_h; return bg; }
static uint32_t blend(uint32_t d, uint32_t s) {
    uint32_t a = s >> 24;
    if (a == 255) return s & 0xFFFFFF;
    uint32_t rb = ((s & 0xFF00FF) * a + (d & 0xFF00FF) * (255 - a)) >> 8 & 0xFF00FF;
    uint32_t g = ((s & 0x00FF00) * a + (d & 0x00FF00) * (255 - a)) >> 8 & 0x00FF00;
    return rb | g;
}
const uint32_t *lc_image(void) {
    if (!img) { img = malloc((size_t)LC_IW * LC_IH * 4); if (!img) return NULL; }
    int first = !img_cv;
    if (first) { img_cv = malloc(sizeof cv); if (!img_cv) return NULL; }
    for (int cy = 0; cy < CH; cy++) {   /* only the canvas rows that changed */
        const uint32_t *row = cv + cy * CW;
        if (!first && !memcmp(row, img_cv + cy * CW, CW * 4)) continue;
        memcpy(img_cv + cy * CW, row, CW * 4);
        for (int k = 0; k < 3; k++) {
            int y = cy * 3 + k;
            uint32_t *o = img + (size_t)y * LC_IW;
            if (bg && bg_w == LC_IW && bg_h == LC_IH) memcpy(o, bg + (size_t)y * LC_IW, LC_IW * 4);
            else if (bg) for (int x = 0; x < LC_IW; x++) o[x] = bg[(size_t)(y * bg_h / LC_IH) * bg_w + x * bg_w / LC_IW];
            else for (int x = 0; x < LC_IW; x++) o[x] = 0x0A0E28;
            for (int cx = 0; cx < CW; cx++) {
                uint32_t s = row[cx];
                if (!(s >> 24)) continue;
                uint32_t *p = o + cx * 3;
                p[0] = blend(p[0], s); p[1] = blend(p[1], s); p[2] = blend(p[2], s);
            }
        }
    }
    return img;
}
void lc_view_rect(int ow, int oh, int *x, int *y, int *w, int *h) {
    int W = ow, H = oh;
    if ((long)ow * 9 > (long)oh * 16) W = oh * 16 / 9; else H = ow * 9 / 16;
    *x = (ow - W) / 2; *y = (oh - H) / 2; *w = W; *h = H;
}
void lc_to_canvas(int ow, int oh, int px, int py, int *cx, int *cy) {
    int x, y, w, h; lc_view_rect(ow, oh, &x, &y, &w, &h);
    *cx = w > 0 ? (px - x) * CW / w : -1; *cy = h > 0 ? (py - y) * CH / h : -1;
}

/* ------------------------------------------------------------------ loading */
void (*lc_present_hook)(void);
double (*lc_clock)(void);
static float step_w[4], step_at[4], music_f;
static int mp3_before[4], mp3_total, md_p0 = -1;
void lc_load_begin(const int mp3_present[4], int md_pending) {
    loading = 1; load_frac = 0; load_msg[0] = 0; cur = O_PLAY;
    mp3_total = 0;
    for (int i = 0; i < 4; i++) { mp3_before[i] = mp3_total; if (mp3_present && mp3_present[i]) mp3_total++; }
    md_p0 = md_pending ? mdgfx_progress : -1;
    if (md_p0 >= 100) md_p0 = 99;
    float md_left = md_p0 >= 0 ? 6.0f * (float)(100 - md_p0) / 100.0f : 0;   /* seconds, roughly */
    step_w[0] = 0.05f; step_w[1] = 0.35f * (float)mp3_total; step_w[2] = md_left; step_w[3] = 0.05f;
    float tot = 0, a = 0;
    for (int i = 0; i < 4; i++) tot += step_w[i];
    for (int i = 0; i < 4; i++) { step_w[i] /= tot; step_at[i] = a; a += step_w[i]; }
    music_f = mp3_total ? 0 : 1;
}
static float md_fraction(void) {
    if (md_p0 < 0) return 1;
    float f = (float)(mdgfx_progress - md_p0) / (float)(100 - md_p0);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}
void lc_load_step(int step, float within, const char *what) {
    if (within < 0) within = 0;
    if (within > 1) within = 1;
    float at;
    if (step == 1 || step == 2) {   /* the MP3s and the capture run at the same time: one stretch of the bar for both */
        if (step == 1) music_f = within;
        at = step_at[1] + step_w[1] * music_f + step_w[2] * md_fraction();
    } else at = step_at[step] + step_w[step] * within;
    int changed = strncmp(load_msg, what ? what : "", sizeof load_msg - 1) != 0;
    load_frac = at;
    if (what) snprintf(load_msg, sizeof load_msg, "%s", what);
    static double last;
    double now = lc_clock ? lc_clock() : 0;
    if (!changed && at < 1.0f && lc_clock && now - last < 0.03) return;   /* about 30 pictures a second is plenty for a bar */
    last = now;
    draw();
    if (lc_present_hook) lc_present_hook();
}
const char *lc_debug_load_msg(void) { return load_msg; }   /* (development screenshots) */
void lc_music_progress(int track, float f) {
    char m[64], up[32]; const char *t = sho_music_tracks[track].title; int k = 0;
    for (; t[k] && k < 31; k++) up[k] = (char)(t[k] >= 'a' && t[k] <= 'z' ? t[k] - 32 : t[k]);
    up[k] = 0;
    snprintf(m, sizeof m, "LOADING MUSIC: %s", up);
    lc_load_step(1, mp3_total ? ((float)mp3_before[track] + f) / (float)mp3_total : 1.0f, m);
}
