/* Mega Drive graphics capture - see sho_mdgfx.h.
 * A minimal Mega Drive: 68000 (the shared Musashi core, switched to this bus while it runs), 64K RAM,
 * VDP (VRAM/CRAM/VSRAM, DMA, planes A/B, window, sprites, per-line scroll, rendered line by line),
 * 3-button pad. The Z80 is not run (bus requests are granted at once); there is no sound. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sho_mdgfx.h"
#include "sho_mdsnd.h"
#include "third_party/musashi/m68k.h"
#include "third_party/puff/puff.h"

mdgfx_t *mdgfx;
mdscene_t *mdscene[MD_NRACES];
/* per-line plane B state of the last rendered frame (for the course background strip) */
static int lb_hs[224], lb_vs[224], lb_nt[224], lb_pw[224], lb_ph[224];
static uint8_t lb_pix[224][320];
static int la_hs[224], la_vs[224], la_nt[224], la_pw[224], la_ph[224];
mdhud_t *mdhud;

/* the arcade core's 68000 memory callbacks forward here while sho_alt_bus is set */
typedef struct { unsigned (*r8)(unsigned), (*r16)(unsigned); void (*w8)(unsigned, unsigned), (*w16)(unsigned, unsigned); } sho_altbus;
extern const sho_altbus *sho_alt_bus;

#define MD_ROM_SIZE 0x80000
#define MD_ROM_CRC 0x3877d107u     /* Super Hang-On (World) (En,Ja) (Rev A) */

static uint8_t *rom; static long romsz;
static int snd_live;   /* the live Mega Drive's Z80 memory and reset line go to the sound module */
static uint8_t ram[0x10000], zram[0x2000], vram[0x10000];
static uint16_t cram[64], vsram[40];
static uint8_t reg[24];
static uint16_t cmd_code, cmd_addr; static int cmd_pending, dma_fill_pending, vint_pending, in_vblank, line;
static uint8_t pad_th = 0x40, pad_ctrl;
static unsigned buttons;      /* U D L R A B C S */
static uint8_t pix[224][320];
static int no_window, capturing, sprites_only;   /* lines are only drawn on capture frames */

static inline unsigned r16(const uint8_t *p, unsigned a) { return (p[a] << 8) | p[a + 1]; }

/* ---------------- VDP ---------------- */
static void vdp_data_write(uint16_t v) {
    switch (cmd_code & 0xf) {
    case 1: { unsigned a = cmd_addr & 0xffff;
        if (a & 1) v = (uint16_t)((v >> 8) | (v << 8)); vram[a & 0xfffe] = v >> 8; vram[(a & 0xfffe) | 1] = (uint8_t)v; break; }
    case 3: cram[(cmd_addr >> 1) & 63] = v; break;
    case 5: if (((cmd_addr >> 1) & 63) < 40) vsram[(cmd_addr >> 1) & 63] = v; break;
    }
    cmd_addr += reg[15];
}
static uint16_t vdp_data_read(void) {
    uint16_t v = 0;
    switch (cmd_code & 0xf) {
    case 0: v = (uint16_t)r16(vram, cmd_addr & 0xfffe); break;
    case 8: v = cram[(cmd_addr >> 1) & 63]; break;
    case 4: v = vsram[(cmd_addr >> 1) % 40]; break;
    }
    cmd_addr += reg[15];
    return v;
}
static unsigned md_r8(unsigned a);
static unsigned md_r16_raw(unsigned a) { return (md_r8(a) << 8) | md_r8(a + 1); }
static void vdp_do_dma(void) {
    if (!(reg[1] & 0x10)) return;
    unsigned len = reg[19] | (reg[20] << 8); if (!len) len = 0x10000;
    unsigned mode = reg[23] >> 6;
    if (mode <= 1) {
        unsigned src = (reg[21] | (reg[22] << 8) | ((reg[23] & 0x7f) << 16)) << 1;
        for (unsigned i = 0; i < len; i++) { vdp_data_write((uint16_t)md_r16_raw(src)); src += 2; }
        reg[19] = reg[20] = 0;
    } else if (mode == 2) dma_fill_pending = 1;
    else {
        unsigned src = reg[21] | (reg[22] << 8);
        for (unsigned i = 0; i < len; i++) { vram[cmd_addr & 0xffff] = vram[src & 0xffff]; src++; cmd_addr += reg[15]; }
    }
}
static void vdp_ctrl_write(uint16_t v) {
    if (!cmd_pending && (v & 0xc000) == 0x8000) { unsigned r = (v >> 8) & 0x1f; if (r < 24) reg[r] = v & 0xff; cmd_code = 0; return; }
    if (!cmd_pending) { cmd_addr = (uint16_t)((cmd_addr & 0xc000) | (v & 0x3fff)); cmd_code = (uint16_t)((cmd_code & 0x3c) | (v >> 14)); cmd_pending = 1; }
    else {
        cmd_pending = 0;
        cmd_addr = (uint16_t)((cmd_addr & 0x3fff) | ((v & 3) << 14));
        cmd_code = (uint16_t)((cmd_code & 3) | ((v >> 2) & 0x3c));
        if (cmd_code & 0x20) vdp_do_dma();
    }
}
static void vdp_data_port_write(uint16_t v) {
    cmd_pending = 0;
    if (dma_fill_pending) {
        dma_fill_pending = 0;
        unsigned len = reg[19] | (reg[20] << 8); if (!len) len = 0x10000;
        vdp_data_write(v);
        for (unsigned i = 0; i < len; i++) { vram[cmd_addr & 0xffff] = v >> 8; cmd_addr += reg[15]; }
        return;
    }
    vdp_data_write(v);
}
static uint16_t vdp_status(void) {
    cmd_pending = 0;
    uint16_t s = 0x3600;
    if (in_vblank || !(reg[1] & 0x40)) s |= 8;
    if (vint_pending) s |= 0x80;
    return s;
}
static unsigned hv(void) { return (((line > 0xea ? line - 6 : line) & 0xff) << 8) | 0x80; }

/* ---------------- bus ---------------- */
static unsigned io_read(unsigned a) {
    a &= 0x1f;
    if (a == 0x01) return 0xa0;
    if (a == 0x03) {
        unsigned b = ~buttons;
        if (pad_th & 0x40) return 0x40 | (b & 0xf) | ((b & 0x20) ? 0x10 : 0) | ((b & 0x40) ? 0x20 : 0);
        return (b & 3) | ((b & 0x10) ? 0x10 : 0) | ((b & 0x80) ? 0x20 : 0);
    }
    if (a == 0x05) return 0x7f;
    if (a == 0x09) return pad_ctrl;
    return 0;
}
static unsigned md_r8(unsigned a) {
    a &= 0xffffff;
    if (a < 0x400000) return a < (unsigned)romsz ? rom[a] : 0xff;
    if (a >= 0xe00000) return ram[a & 0xffff];
    if (a >= 0xa00000 && a < 0xa10000) return snd_live && mdsnd_ready() ? mdsnd_zread(a) : zram[a & 0x1fff];
    if (a >= 0xa10000 && a < 0xa10020) return io_read(a);
    if (a == 0xa11100 || a == 0xa11101) return 0;
    if (a >= 0xc00000 && a < 0xc00020) {
        unsigned p = a & 0x1f, v;
        if (p < 4) v = vdp_data_read(); else if (p < 8) v = vdp_status(); else if (p < 0x10) v = hv(); else return 0xff;
        return (a & 1) ? v & 0xff : v >> 8;
    }
    return 0xff;
}
static unsigned md_r16(unsigned a) {
    a &= 0xffffff;
    if (a < 0x400000) return a + 1 < (unsigned)romsz ? r16(rom, a) : 0xffff;
    if (a >= 0xe00000) return r16(ram, a & 0xffff);
    if (a >= 0xc00000 && a < 0xc00020) { unsigned p = a & 0x1f; if (p < 4) return vdp_data_read(); if (p < 8) return vdp_status(); return hv(); }
    return md_r16_raw(a);
}
static void md_w8(unsigned a, unsigned v) {
    a &= 0xffffff;
    if (a >= 0xe00000) { ram[a & 0xffff] = (uint8_t)v; return; }
    if (a >= 0xa00000 && a < 0xa10000) { zram[a & 0x1fff] = (uint8_t)v; if (snd_live) mdsnd_zwrite(a & 0x1fff, (uint8_t)v); return; }
    if (a == 0xa11200) { if (snd_live) mdsnd_zreset(v & 1); return; }
    if (a >= 0xa10000 && a < 0xa10020) { if ((a & 0x1f) == 0x03) pad_th = v & 0x40; if ((a & 0x1f) == 0x09) pad_ctrl = (uint8_t)v; return; }
    if (a >= 0xc00000 && a < 0xc00020) { unsigned p = a & 0x1f; v &= 0xff;
        if (p < 4) vdp_data_port_write((uint16_t)(v | (v << 8))); else if (p < 8) vdp_ctrl_write((uint16_t)(v | (v << 8))); }
}
static void md_w16(unsigned a, unsigned v) {
    a &= 0xffffff;
    if (a >= 0xe00000) { ram[a & 0xffff] = (uint8_t)(v >> 8); ram[(a + 1) & 0xffff] = (uint8_t)v; return; }
    if (a >= 0xc00000 && a < 0xc00020) { unsigned p = a & 0x1f; if (p < 4) vdp_data_port_write((uint16_t)v); else if (p < 8) vdp_ctrl_write((uint16_t)v); return; }
    md_w8(a, (v >> 8) & 0xff); md_w8(a + 1, v & 0xff);
}
static const sho_altbus md_bus = { md_r8, md_r16, md_w8, md_w16 };

/* ---------------- line renderer ---------------- */
static int plane_dim(int code) { return code == 1 ? 64 : code == 3 ? 128 : 32; }
static void render_line(int y) {
    int w = (reg[12] & 1) ? 320 : 256;
    if (!(reg[1] & 0x40)) { memset(pix[y], reg[7] & 63, 320); return; }
    int pw = plane_dim(reg[16] & 3), ph = plane_dim((reg[16] >> 4) & 3);
    unsigned hs_base = (reg[13] & 0x3f) << 10;
    int hmode = reg[11] & 3, vmode = (reg[11] >> 2) & 1;
    int wh = reg[17] & 0x1f, wright = reg[17] & 0x80, wv = reg[18] & 0x1f, wdown = reg[18] & 0x80;
    int win_line = wdown ? y >= wv * 8 : y < wv * 8;
    uint8_t ca[2][320], cpr[2][320];
    for (int layer = 0; layer < 2; layer++) {
        unsigned nt = layer == 0 ? (reg[2] & 0x38) << 10 : (reg[4] & 7) << 13;
        unsigned hsa = hs_base + (hmode == 3 ? y * 4 : hmode == 2 ? (y & ~7) * 4 : 0) + layer * 2;
        int hs = (int)(r16(vram, hsa & 0xffff) & 0x3ff);
        if (layer == 1) { lb_hs[y] = hs; lb_vs[y] = vsram[1] & 0x3ff; lb_nt[y] = (int)nt; lb_pw[y] = pw; lb_ph[y] = ph; }
        else { la_hs[y] = hs; la_vs[y] = vsram[0] & 0x3ff; la_nt[y] = (int)nt; la_pw[y] = pw; la_ph[y] = ph; }
        for (int x = 0; x < w; x++) {
            unsigned e; int tx, ty;
            int inwin = layer == 0 && (win_line || (wright ? x >= wh * 16 : x < wh * 16));
            if (inwin) {
                if (no_window) { ca[0][x] = 0; cpr[0][x] = 0; continue; }   /* text layer left out */
                unsigned wnt = (reg[3] & (w == 320 ? 0x3c : 0x3e)) << 10; int wpw = w == 320 ? 64 : 32;
                e = r16(vram, (wnt + ((y >> 3) * wpw + (x >> 3)) * 2) & 0xffff); tx = x & 7; ty = y & 7;
            } else {
                int vs = vsram[vmode ? ((x >> 4) * 2 + layer) % 40 : layer] & 0x3ff;
                int vx = (x - hs) & (pw * 8 - 1), vy = (y + vs) & (ph * 8 - 1);
                e = r16(vram, (nt + ((vy >> 3) * pw + (vx >> 3)) * 2) & 0xffff); tx = vx & 7; ty = vy & 7;
            }
            unsigned t = e & 0x7ff, pal = (e >> 13) & 3;
            if ((e >> 11) & 1) tx = 7 - tx;
            if ((e >> 12) & 1) ty = 7 - ty;
            unsigned b = vram[(t * 32 + ty * 4 + tx / 2) & 0xffff], c = (tx & 1) ? b & 15 : b >> 4;
            ca[layer][x] = (uint8_t)(c ? pal * 16 + c : 0); cpr[layer][x] = (uint8_t)(e >> 15);
        }
    }
    memcpy(lb_pix[y], ca[1], 320);
    uint8_t sp[320], spr[320]; memset(sp, 0, sizeof sp); memset(spr, 0, sizeof spr);
    unsigned sat = (reg[5] & (w == 320 ? 0x7e : 0x7f)) << 9; int link = 0, count = 0;
    for (int i = 0; i < 80; i++) {
        unsigned b = sat + link * 8;
        int sy = (int)(r16(vram, b & 0xffff) & 0x3ff) - 128, sz = vram[(b + 2) & 0xffff], sxx = (int)(r16(vram, (b + 6) & 0xffff) & 0x1ff) - 128;
        unsigned e = r16(vram, (b + 4) & 0xffff); int hw = ((sz >> 2) & 3) + 1, vh = (sz & 3) + 1;
        if (y >= sy && y < sy + vh * 8 && count < 20) {
            count++;
            unsigned t = e & 0x7ff, pal = (e >> 13) & 3, hf = (e >> 11) & 1, vf = (e >> 12) & 1;
            int py = y - sy, cy = vf ? vh * 8 - 1 - py : py;
            for (int px = 0; px < hw * 8; px++) {
                int X = sxx + px; if (X < 0 || X >= w || sp[X]) continue;
                int cx = hf ? hw * 8 - 1 - px : px;
                unsigned tile = t + (cx >> 3) * vh + (cy >> 3);
                unsigned bb = vram[(tile * 32 + (cy & 7) * 4 + (cx & 7) / 2) & 0xffff], c = (cx & 1) ? bb & 15 : bb >> 4;
                if (c) { sp[X] = (uint8_t)(pal * 16 + c); spr[X] = (uint8_t)(e >> 15); }
            }
        }
        link = vram[(b + 3) & 0xffff] & 0x7f; if (!link) break;
    }
    if (sprites_only) { for (int x = 0; x < 320; x++) pix[y][x] = x < w ? sp[x] : 0; return; }
    for (int x = 0; x < w; x++) {
        int c = reg[7] & 63;
        int cand[6] = { spr[x] ? sp[x] : 0, cpr[0][x] ? ca[0][x] : 0, cpr[1][x] ? ca[1][x] : 0,
                        !spr[x] ? sp[x] : 0, !cpr[0][x] ? ca[0][x] : 0, !cpr[1][x] ? ca[1][x] : 0 };
        for (int k = 0; k < 6; k++) if (cand[k]) { c = cand[k]; break; }
        pix[y][x] = (uint8_t)c;
    }
    for (int x = w; x < 320; x++) pix[y][x] = 0;
}
static uint32_t md_col(uint16_t c) {
    static const uint8_t lv[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };   /* Mega Drive DAC levels */
    return (uint32_t)lv[(c >> 1) & 7] << 16 | (uint32_t)lv[(c >> 5) & 7] << 8 | lv[(c >> 9) & 7];
}
static void grab(uint32_t *dst) { for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) dst[y * 320 + x] = md_col(cram[pix[y][x]]); }

/* ---------------- run ---------------- */
static void run_frame(void) {
    int hint_counter = reg[10];
    for (line = 0; line < 262; line++) {
        in_vblank = line >= 224;
        m68k_execute(388);
        if (line < 224 && --hint_counter < 0) { hint_counter = reg[10]; if (reg[0] & 0x10) m68k_set_irq(4); }
        if (line == 224) { vint_pending = 1; if (reg[1] & 0x20) m68k_set_irq(6); }
        m68k_execute(100);
        if (line < 224 && capturing) render_line(line);
        m68k_set_irq(0);
        if (line == 224) vint_pending = 0;
    }
}
typedef struct { long f; unsigned b; } press;
/* run to frame `until`, pressing each button for 5 frames from its frame; capture on the frame given */
static long cur_frame;
static void run_to(long until, const press *ps, int n) {
    for (; cur_frame < until; cur_frame++) {
        buttons = 0;
        for (int i = 0; i < n; i++) if (cur_frame >= ps[i].f && cur_frame < ps[i].f + (ps[i].b & 0x100 ? 400 : 5)) buttons |= ps[i].b & 0xff;
        run_frame();
    }
}
static void capture_frame(uint32_t *dst) { no_window = capturing = 1; run_frame(); no_window = capturing = 0; cur_frame++; grab(dst); }
static void md_power_on(void) {
    memset(ram, 0, sizeof ram); memset(zram, 0, sizeof zram); memset(vram, 0, sizeof vram);
    memset(cram, 0, sizeof cram); memset(vsram, 0, sizeof vsram); memset(reg, 0, sizeof reg);
    cmd_code = cmd_addr = 0; cmd_pending = dma_fill_pending = vint_pending = 0; pad_th = 0x40; pad_ctrl = 0; buttons = 0; cur_frame = 0;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); m68k_pulse_reset();
}

/* ---------------- ROM loading ---------------- */
static uint32_t crc32_buf(const uint8_t *p, size_t n) {
    static uint32_t t[256]; static int init;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = 1; }
    uint32_t c = 0xffffffffu; while (n--) c = t[(c ^ *p++) & 0xff] ^ (c >> 8); return c ^ 0xffffffffu;
}
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint8_t *load_rom_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n); if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    if (n > 4 && le32(b) == 0x04034b50) {   /* zip: first file of a plausible size */
        size_t e = (size_t)n - 22, stop = n > 0x10000 + 22 ? (size_t)n - 0x10000 - 22 : 0;
        for (;; e--) { if (le32(b + e) == 0x06054b50) break; if (e == stop) { free(b); return NULL; } }
        uint32_t cd = le32(b + e + 16); uint16_t cnt = le16(b + e + 10);
        for (uint16_t i = 0; i < cnt && cd + 46 <= (uint32_t)n; i++) {
            const uint8_t *h = b + cd;
            uint16_t method = le16(h + 10), nl = le16(h + 28), xl = le16(h + 30), cl = le16(h + 32);
            uint32_t csize = le32(h + 20), usize = le32(h + 24), lho = le32(h + 42);
            if (usize >= 0x40000 && usize <= 0x400000 && lho + 30 <= (uint32_t)n) {
                const uint8_t *lh = b + lho; size_t off = lho + 30 + le16(lh + 26) + le16(lh + 28);
                if (off + csize <= (size_t)n) {
                    uint8_t *out = malloc(usize);
                    if (out) {
                        int ok = 0;
                        if (method == 0) { memcpy(out, b + off, usize); ok = 1; }
                        else if (method == 8) { unsigned long dl = usize, sl = csize; ok = puff(out, &dl, b + off, &sl) == 0 && dl == usize; }
                        if (ok) { free(b); *len = (long)usize; return out; }
                        free(out);
                    }
                }
            }
            cd += 46 + nl + xl + cl;
        }
        free(b); return NULL;
    }
    *len = n; return b;
}
int mdgfx_check(const char *path, char *msg, size_t msglen) {
    long n = 0; uint8_t *b = load_rom_file(path, &n);
    if (!b) { snprintf(msg, msglen, "not found or unreadable"); return 0; }
    int ok = n == MD_ROM_SIZE && crc32_buf(b, (size_t)n) == MD_ROM_CRC;
    if (!ok) snprintf(msg, msglen, n == MD_ROM_SIZE ? "wrong revision (needs World Rev A)" : "not Super Hang-On (Mega Drive)");
    else snprintf(msg, msglen, "verified");
    free(b); return ok;
}

/* ---------------- course scenes ---------------- */
static uint32_t planeB_pixel(int y, int X) {   /* plane B at plane column X on line y (that line's vertical scroll) */
    int pw = lb_pw[y], ph = lb_ph[y], vy = (y + lb_vs[y]) & (ph * 8 - 1), vx = X & (pw * 8 - 1);
    unsigned e = r16(vram, (lb_nt[y] + ((vy >> 3) * pw + (vx >> 3)) * 2) & 0xffff);
    int tx = vx & 7, ty = vy & 7; if ((e >> 11) & 1) tx = 7 - tx; if ((e >> 12) & 1) ty = 7 - ty;
    unsigned b = vram[((e & 0x7ff) * 32 + ty * 4 + tx / 2) & 0xffff], c = (tx & 1) ? b & 15 : b >> 4;
    return md_col(cram[c ? ((e >> 13) & 3) * 16 + c : reg[7] & 63]);
}
static uint32_t planeA_pixel(int y, int X) {   /* 0 = transparent */
    int pw = la_pw[y], ph = la_ph[y], vy = (y + la_vs[y]) & (ph * 8 - 1), vx = X & (pw * 8 - 1);
    unsigned e = r16(vram, (la_nt[y] + ((vy >> 3) * pw + (vx >> 3)) * 2) & 0xffff);
    int tx = vx & 7, ty = vy & 7; if ((e >> 11) & 1) tx = 7 - tx; if ((e >> 12) & 1) ty = 7 - ty;
    unsigned b = vram[((e & 0x7ff) * 32 + ty * 4 + tx / 2) & 0xffff], c = (tx & 1) ? b & 15 : b >> 4;
    return c ? 0xff000000u | md_col(cram[((e >> 13) & 3) * 16 + c]) : 0;
}
static void tile_pixels(int t, uint8_t *out) { for (int i = 0; i < 64; i++) { unsigned b = vram[(t * 32 + (i >> 3) * 4 + (i & 7) / 2) & 0xffff]; out[i] = (uint8_t)((i & 1) ? b & 15 : b >> 4); } }
static void capture_hud(void) {   /* window plane rows 0-4: everything except the time and speed digits */
    if (!mdhud) mdhud = calloc(1, sizeof *mdhud);
    if (!mdhud) return;
    unsigned wnt = (reg[3] & 0x3c) << 10;
    memset(mdhud->img, 0, sizeof mdhud->img);
    for (int row = 0; row < 5; row++) for (int col = 0; col < 40; col++) {
        unsigned e = r16(vram, (wnt + (row * 64 + col) * 2) & 0xffff); int t = e & 0x7ff;
        if (row == 2 && ((col >= 13 && col <= 21) || (col >= 30 && col <= 33))) continue;   /* time and speed fields */
        if (row == 4 && col >= 3 && col <= 10) continue;                                    /* bar: drawn live */
        uint8_t px[64]; tile_pixels(t, px);
        for (int i = 0; i < 64; i++) if (px[i]) mdhud->img[row * 8 + (i >> 3)][col * 8 + (i & 7)] = 0xff000000u | md_col(cram[((e >> 13) & 3) * 16 + px[i]]);
    }
    tile_pixels(0x6c3, mdhud->bar_empty); tile_pixels(0x6bf, mdhud->bar_tick); tile_pixels(0x6cb, mdhud->bar_start);
    for (int p = 0; p < 4; p++) for (int c = 0; c < 16; c++) mdhud->pal[p][c] = md_col(cram[p * 16 + c]);
    mdhud->ok = 1;
}
/* ---------------- roadside objects ---------------- */
typedef struct { uint8_t ram[0x10000], vram[0x10000]; uint16_t cram[64], vsram[40]; uint8_t reg[24];
                 uint16_t cmd_code, cmd_addr; int cmd_pending; long frame; uint32_t regs[20]; } mdsnap;
static const int snap_regs[] = { M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7, M68K_REG_PC, M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP };
static void snap_take(mdsnap *s) {
    memcpy(s->ram, ram, sizeof ram); memcpy(s->vram, vram, sizeof vram); memcpy(s->cram, cram, sizeof cram); memcpy(s->vsram, vsram, sizeof vsram);
    memcpy(s->reg, reg, sizeof reg); s->cmd_code = cmd_code; s->cmd_addr = cmd_addr; s->cmd_pending = cmd_pending; s->frame = cur_frame;
    for (int i = 0; i < 20; i++) s->regs[i] = m68k_get_reg(NULL, snap_regs[i]);
}
static void snap_restore(const mdsnap *s) {
    memcpy(ram, s->ram, sizeof ram); memcpy(vram, s->vram, sizeof vram); memcpy(cram, s->cram, sizeof cram); memcpy(vsram, s->vsram, sizeof vsram);
    memcpy(reg, s->reg, sizeof reg); cmd_code = s->cmd_code; cmd_addr = s->cmd_addr; cmd_pending = s->cmd_pending; cur_frame = s->frame;
    for (int i = 0; i < 20; i++) m68k_set_reg(snap_regs[i], s->regs[i]);
}
static void sprite_frame(uint8_t out[224][320]) {   /* run one frame and keep only the sprite layer */
    capturing = 1; sprites_only = 1; run_frame(); capturing = 0; sprites_only = 0; cur_frame++;
    memcpy(out, pix, sizeof pix);
}
const void *mdscene_object(const mdscene_t *sc, int kind) {
    for (int i = 0; sc && i < sc->nobj; i++) if (sc->obj[i].kind == kind && sc->obj[i].pix) return &sc->obj[i];
    return NULL;
}
/* the kinds (and attribute words) of every object this course places */
static int course_kinds(const uint8_t *r, int race, int *kinds, int *attrs, int max) {
    int cls = r16(r, 0x8e58 + race * 6), stg = r16(r, 0x8e58 + race * 6 + 2), s = r16(r, 0xfbc2 + (cls * 9 + stg) * 2), n = 0;
    if (s >= 24) return 0;
    uint32_t d = (uint32_t)r16(r, 0xfb62 + s * 4) << 16 | r16(r, 0xfb62 + s * 4 + 2);
    uint32_t os = (uint32_t)r16(r, d + 0x10) << 16 | r16(r, d + 0x12);
    for (int i = 0; i < 600; i++) {
        int pos = (int16_t)r16(r, os + i * 6), pat = r16(r, os + i * 6 + 4);
        if (pos < 0) break;
        uint32_t pp = (uint32_t)r16(r, 0x5771c + pat * 4) << 16 | r16(r, 0x5771c + pat * 4 + 2), q = pp + 2;
        for (int k = 0; k < 16 && r[q] < 0x80; k++, q += 6) {
            int kind = r[q + 1], attr = r16(r, q + 4), seen = 0;
            for (int j = 0; j < n; j++) if (kinds[j] == kind) seen = 1;
            if (!seen && n < max) { kinds[n] = kind; attrs[n] = attr; n++; }
        }
    }
    return n;
}
/* Drive the Mega Drive race (throttle held) and, whenever an object of a not-yet-captured kind is near
   (its largest frames), render the sprite layer with and without that object: the difference is the
   object as the Mega Drive draws it. */
/* The sprite table's pieces for one object: pieces sit together in link order around the object's
   screen anchor (slot +$10/+$14); the player's bike is drawn first and left out. */
typedef struct { int x, y, w, h; unsigned e; } satent;
static int read_sat(satent *o) {
    unsigned sat = (reg[5] & 0x7e) << 9; int link = 0, n = 0;
    for (int i = 0; i < 80; i++) {
        unsigned bb = (sat + link * 8) & 0xffff; int sz = vram[(bb + 2) & 0xffff];
        o[n].y = (int)(r16(vram, bb) & 0x3ff) - 128; o[n].x = (int)(r16(vram, (bb + 6) & 0xffff) & 0x1ff) - 128;
        o[n].w = (((sz >> 2) & 3) + 1) * 8; o[n].h = ((sz & 3) + 1) * 8; o[n].e = r16(vram, (bb + 4) & 0xffff); n++;
        link = vram[(bb + 3) & 0xffff] & 0x7f; if (!link) break;
    }
    return n;
}
static void draw_sat_piece(const satent *s, uint32_t *img, int iw, int ih, int ox, int oy) {
    int hw = s->w / 8, vh = s->h / 8; unsigned t = s->e & 0x7ff, pal = (s->e >> 13) & 3, hf = (s->e >> 11) & 1, vf = (s->e >> 12) & 1;
    for (int py = 0; py < s->h; py++) for (int pxx = 0; pxx < s->w; pxx++) {
        int X = s->x + pxx - ox, Y = s->y + py - oy; if (X < 0 || Y < 0 || X >= iw || Y >= ih || img[Y * iw + X]) continue;
        int cx = hf ? s->w - 1 - pxx : pxx, cy = vf ? s->h - 1 - py : py;
        unsigned tile = t + (cx >> 3) * vh + (cy >> 3);
        unsigned bb = vram[(tile * 32 + (cy & 7) * 4 + (cx & 7) / 2) & 0xffff], c = (cx & 1) ? bb & 15 : bb >> 4;
        if (c) img[Y * iw + X] = 0xff000000u | md_col(cram[pal * 16 + c]);
    }
    (void)hw;
}
/* course position of the first placement of a kind at or after `from` (or -1) */
static int kind_next_pos(const uint8_t *r, int race, int kind, int from) {
    int cls = r16(r, 0x8e58 + race * 6), stg = r16(r, 0x8e58 + race * 6 + 2), s = r16(r, 0xfbc2 + (cls * 9 + stg) * 2);
    if (s >= 24) return -1;
    uint32_t d = (uint32_t)r16(r, 0xfb62 + s * 4) << 16 | r16(r, 0xfb62 + s * 4 + 2);
    uint32_t os = (uint32_t)r16(r, d + 0x10) << 16 | r16(r, d + 0x12);
    for (int i = 0; i < 600; i++) {
        int pos = (int16_t)r16(r, os + i * 6), pat = r16(r, os + i * 6 + 4);
        if (pos < 0) break;
        if (pos < from) continue;
        uint32_t pp = (uint32_t)r16(r, 0x5771c + pat * 4) << 16 | r16(r, 0x5771c + pat * 4 + 2), q = pp + 2;
        for (int k = 0; k < 16 && r[q] < 0x80; k++, q += 6) if (r[q + 1] == kind) return pos;
    }
    return -1;
}
/* An object's picture from the Mega Drive's own sprite frame definition: the kind table ($204: 10 frames per
   kind, smallest to largest, picked by distance through $6EC8) points at a frame - a count word, then per
   piece: y offset byte, size byte, tile/attribute word (added to the object's base word, slot +2), x offset
   word. The tiles come from video memory once the object's base is known. */
static int frame_pieces(int kind, int idx, unsigned base, satent *o, int max) {
    unsigned a = r16(rom, 0x204 + kind * 20 + idx * 2);
    int n = r16(rom, a) + 1; if (n > max || n < 1) return 0;
    for (int i = 0; i < n; i++) {
        const uint8_t *q = rom + a + 2 + i * 6; int sz = q[1];
        o[i].y = q[0] - 256;   /* the y byte goes into a word preset to -1 */ o[i].x = (int16_t)(q[4] << 8 | q[5]);
        o[i].w = (((sz >> 2) & 3) + 1) * 8; o[i].h = ((sz & 3) + 1) * 8; o[i].e = (unsigned)(q[2] << 8 | q[3]) + base;
    }
    return n;
}
static int frame_box(const satent *o, int n, int *x0, int *y0, int *x1, int *y1) {
    *x0 = *y0 = 9999; *x1 = *y1 = -9999;
    for (int i = 0; i < n; i++) {
        if (o[i].x < *x0) *x0 = o[i].x; if (o[i].y < *y0) *y0 = o[i].y;
        if (o[i].x + o[i].w > *x1) *x1 = o[i].x + o[i].w; if (o[i].y + o[i].h > *y1) *y1 = o[i].y + o[i].h;
    }
    return n > 0;
}
/* objects first placed this near the end of the lap (the finish area) use tiles loaded on the way there,
   which the capture drive does not reproduce: they are left out for now */
#define END_ZONE 0x7ff
/* render one of a kind's frames from video memory into scene entry o (allocated by the caller) */
static int render_object(mdscene_t *sc, int o, int race, int kind, unsigned base, int idx, int refidx) {
    satent pc[80]; int np = frame_pieces(kind, idx, base, pc, 80), x0, y0, x1, y1;
    if (!np || !frame_box(pc, np, &x0, &y0, &x1, &y1)) return 0;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 2 || h <= 2 || w > 512 || h > 400) return 0;
    uint32_t *pxl = calloc((size_t)w * h, 4); if (!pxl) return 0;
    for (int i = 0; i < np; i++) draw_sat_piece(&pc[i], pxl, w, h, x0, y0);   /* first piece is on top */
    satent pr2[80]; int nr = frame_pieces(kind, refidx, base, pr2, 80), rx0, ry0, rx1, ry1;
    int rh = nr && frame_box(pr2, nr, &rx0, &ry0, &rx1, &ry1) ? ry1 - ry0 : h;
    free(sc->obj[o].pix);
    sc->obj[o].kind = kind; sc->obj[o].w = w; sc->obj[o].h = h; sc->obj[o].ax = -x0; sc->obj[o].ay = -y0 - 1; sc->obj[o].pix = pxl;
    sc->obj[o].norm_h = rh;   /* height at the reference distance, for sizing (the picture is scaled to it) */
    if (getenv("OBJPPM")) { char pn[256]; snprintf(pn, sizeof pn, "%s/o_r%d_k%02x_f%d.ppm", getenv("OBJPPM"), race, kind, idx); FILE *pf = fopen(pn, "wb");
        if (pf) { fprintf(pf, "P6\n%d %d\n255\n", w, h); for (int i = 0; i < w * h; i++) { uint32_t c = pxl[i]; if (!(c >> 24)) c = 0x2000ff; fputc(c >> 16, pf); fputc(c >> 8, pf); fputc(c, pf); } fclose(pf); } }
    if (getenv("OBJDBG")) fprintf(stderr, "obj race %d kind %02x base %04x frame %d pieces %d %dx%d ref_h %d\n", race, kind, base, idx, np, w, h, rh);
    return 1;
}
/* Drive the Mega Drive race and take each kind's picture from the Mega Drive's own frame definitions and
   video memory. Tiles are only trusted while the Mega Drive is drawing that very frame (its pieces are in
   the sprite table): some objects (billboards) share a tile area that is reloaded per object and size.
   The largest frame seen on screen is kept. */
static void capture_objects(mdscene_t *sc, int race) {
    int kinds[32], attrs[32], n = course_kinds(rom, race, kinds, attrs, 32), bestidx[32], slot_of[32];
    int refidx = rom[0x6ec8 + 0x58] / 2;      /* frame shown at the reference distance */
    for (int i = 0; i < 32; i++) { bestidx[i] = -1; slot_of[i] = -1; }
    /* billboards of different kinds share one tile area (same base word) and only the course's first one
       is loaded with it; the others are loaded later in the race in ways this drive does not reach and are
       taken from a race where they come first (fill_objects) */
    for (int i = 0; i < n; i++) if (kind_next_pos(rom, race, kinds[i], 0) >= END_ZONE) bestidx[i] = 99;
    for (int i = 0; i < n; i++) if (attrs[i]) {
        int pi = kind_next_pos(rom, race, kinds[i], 0);
        for (int j = 0; j < n; j++) if (j != i && attrs[j] == attrs[i]) { int pj = kind_next_pos(rom, race, kinds[j], 0); if (pj >= 0 && pj < pi) bestidx[i] = 99; }
    }
    long jumped = -1000; int prev_fr[9] = { 0 }, held[9] = { 0 };
    const int need = 2;   /* frames the same size must have been shown */
    for (long f = 0; f < 7200; f++) {
        if (f % 64 == 0) { int all = 1; for (int i = 0; i < n; i++) if (bestidx[i] < 8) all = 0;   /* 99 = not taken here */ if (all) break; }
        /* the bike is slow and crashes a lot unsteered: every few seconds move it up to just before the
           nearest placement of a kind not yet seen at full size */
        if (f % 300 == 299) {
            int cur = r16(ram, 0x0616), best = 0x7fff;
            for (int i = 0; i < n; i++) if (bestidx[i] < 7) {
                int np = kind_next_pos(rom, race, kinds[i], cur + 0x10);
                if (np > cur + 0x40 && np < 0x7f8 && np < best) best = np;
            }
            if (best < 0x7fff) { int tgt = best - 0x60; if (best >= 0x6e0 && tgt > 0x690) tgt = 0x690;   /* the finish area's tiles load at the 1800 event: run through it */
                if (tgt > cur) { ram[0x0616] = (uint8_t)(tgt >> 8); ram[0x0617] = (uint8_t)tgt; jumped = f; } }
        }
        buttons = 32;                                             /* B: accelerate */
        run_frame(); cur_frame++;
        if (f - jumped < 60) continue;
        satent se[80]; int ns = -1;
        for (int s8 = 0; s8 < 9; s8++) {
            uint8_t *sl = ram + 0xce00 + s8 * 0x40;
            if (sl[1] != 0x34) { held[s8] = 0; continue; }
            int kind = sl[0x2d], k = -1, fr = r16(sl, 0x24), idx = -1;
            held[s8] = fr == prev_fr[s8] ? held[s8] + 1 : 0; prev_fr[s8] = fr;
            if (held[s8] < need) continue;
            for (int i = 0; i < n; i++) if (kinds[i] == kind) k = i;
            if (k < 0) continue;
            for (int i = 9; i >= 0 && idx < 0; i--) if (r16(rom, 0x204 + kind * 20 + i * 2) == fr) idx = i;
            if (idx <= bestidx[k] || idx < 4) continue;
            unsigned base = r16(sl, 2);
            satent pc[80]; int np = frame_pieces(kind, idx, base, pc, 80), hit = 0;
            if (ns < 0) ns = read_sat(se);
            int ox = r16(sl, 0x10), oy = r16(sl, 0x14);
            for (int i = 0; i < np; i++) for (int j = 0; j < ns; j++)
                if (se[j].e == (pc[i].e & 0xffff) && abs(se[j].x + 128 - ((pc[i].x + ox) & 0x1ff)) <= 12 && abs(se[j].y + 128 - ((pc[i].y + oy) & 0x3ff)) <= 12) { hit++; break; }
            if (hit * 2 < np) continue;                           /* not (wholly) being drawn */
            int o = slot_of[k];
            if (o < 0) { if (sc->nobj >= 32) continue; o = sc->nobj++; sc->obj[o].pix = NULL; }
            if (render_object(sc, o, race, kind, base, idx, refidx)) { slot_of[k] = o; bestidx[k] = idx; }
            else if (slot_of[k] < 0) sc->nobj--;
        }
    }
}
/* kinds a race uses but could not capture: copy the picture from another race that has it */
static void fill_objects(void) {
    for (int r = 0; r < MD_NRACES; r++) {
        mdscene_t *sc = mdscene[r]; if (!sc || !sc->ok) continue;
        int kinds[32], attrs[32], n = course_kinds(rom, r, kinds, attrs, 32);
        for (int i = 0; i < n; i++) {
            if (mdscene_object(sc, kinds[i]) || sc->nobj >= 32 || kind_next_pos(rom, r, kinds[i], 0) >= END_ZONE) continue;
            for (int r2 = 0; r2 < MD_NRACES; r2++) {
                const mdscene_t *s2 = mdscene[r2]; if (r2 == r || !s2) continue;
                int o2 = -1; for (int j = 0; j < s2->nobj; j++) if (s2->obj[j].kind == kinds[i] && s2->obj[j].pix) o2 = j;
                if (o2 < 0) continue;
                size_t sz = (size_t)s2->obj[o2].w * s2->obj[o2].h * 4; uint32_t *px = malloc(sz); if (!px) break;
                memcpy(px, s2->obj[o2].pix, sz);
                int o = sc->nobj++; sc->obj[o] = s2->obj[o2]; sc->obj[o].pix = px;
                break;
            }
        }
    }
}
static void capture_scene(int r) {
    enum { D = 2, Bb = 32, S = 128 };
    static const press pr[] = { { 300, S }, { 360, S }, { 420, D }, { 470, S }, { 560, D }, { 600, D }, { 640, S }, { 800, S } };   /* Start: music chosen */
    md_power_on();
    run_to(550, pr, 8);
    ram[0xb606] = 0; ram[0xb607] = (uint8_t)r; ram[0xc85a] = 0; ram[0xc85b] = (uint8_t)r;   /* which Original Mode race ($B606 is copied to $C85A) */
    run_to(1150, pr, 8);
    capturing = 1; no_window = 1; run_frame(); capturing = 0; no_window = 0; cur_frame++;
    if (r == 0) capture_hud();
    mdscene_t *sc = mdscene[r]; if (!sc) sc = mdscene[r] = calloc(1, sizeof *sc);
    if (!sc) return;
    sc->ok = 0;
    for (int i = 0; i < sc->nobj; i++) free(sc->obj[i].pix);
    sc->nobj = 0;
    /* road colours: palette lines 2 and 3 are the light and dark bands; entries B-F = kerb, ground, road, edge, centre */
    for (int b = 0; b < 2; b++) for (int k = 0; k < 5; k++) sc->road[b][k] = md_col(cram[(2 + b) * 16 + 0xb + k]);
    sc->sky = md_col(cram[0x30]);
    /* horizon: the first line where plane B shows the road/ground colours (entries B-F of lines 2/3) */
    int hz = 0;
    for (int y = 40; y < 200 && !hz; y++) { int c = lb_pix[y][160]; if ((c >> 4) >= 2 && (c & 15) >= 0xb) hz = y; }
    if (hz < 8 || hz > MDS_MAX_H) return;
    int w = lb_pw[hz - 1] * 8; if (w > MDS_MAX_W) w = MDS_MAX_W;
    sc->w = w; sc->h = hz;
    int wa = la_pw[hz - 1] * 8; if (wa > MDS_MAX_W) wa = MDS_MAX_W;
    sc->wa = wa;
    for (int y = 0; y < hz; y++) {
        sc->base[y] = (-lb_hs[y]) & (lb_pw[y] * 8 - 1);
        for (int X = 0; X < w; X++) sc->strip[y][X] = planeB_pixel(y, X);
        sc->basea[y] = (-la_hs[y]) & (la_pw[y] * 8 - 1);
        for (int X = 0; X < wa; X++) sc->stripa[y][X] = y >= 40 ? planeA_pixel(y, X) : 0;   /* above line 40 = the HUD window */
    }
    sc->ok = 1;
    if (r == 0 && getenv("STEERLOG")) {   /* debug: Mega Drive steering response */
        for (int i = 0; i < 400; i++) { buttons = 32; run_frame(); cur_frame++; }
        for (int i = 0; i < 160; i++) { buttons = 32 | (i < 40 ? 8 : 0); run_frame(); cur_frame++;
            fprintf(stderr, "MDST %d c73c %d st %d spd %d c706 %02x\n", i, (int16_t)r16(ram, 0xc73c), (int16_t)r16(ram, 0x063a), r16(ram, 0x064a), ram[0xc707]); } }
    capture_objects(sc, r);
}

/* ---------------- capture ---------------- */
static const struct { char ch; uint16_t tile; } symbols[] = {
    { '$', 0x34a }, { '-', 0x34b }, { ':', 0x34c }, { '!', 0x34d }, { '?', 0x34e }, { ',', 0x362 },
    { '.', 0x638 }, { '\'', 0x63c }, { '"', 0x63d }, { '>', 0x349 }, { '<', 0x639 },
};
/* The title logo's chequered bits (left of the S, under the P) are leftovers of the Mega Drive's flag tiles:
   8x8 cells made only of white and black in a 2-pixel chequer are cleared to the background */
static void clean_logo(uint32_t *img) {
    for (int ty = 0; ty < 12; ty++) for (int tx = 0; tx < 40; tx++) {
        int white = 0, other = 0, chq = 0; uint32_t bg = 0;
        for (int y = 0; y < 8; y++) {
            int bits = 0;
            for (int x = 0; x < 8; x++) {
                uint32_t c = img[(ty * 8 + y) * 320 + tx * 8 + x]; int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (r > 200 && g > 200 && b > 200) { white++; bits |= 1 << x; }
                else if (r < 40 && g < 40 && b < 40) bg = c; else other++;
            }
            if (bits == 0x33 || bits == 0xcc) chq++;
        }
        if (other || !white || chq < 3) continue;
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) img[(ty * 8 + y) * 320 + tx * 8 + x] = bg;
    }
}
static void take_font(void) {
    for (int ch = 0; ch < 128; ch++) {
        int t = -1;
        if (ch >= 'A' && ch <= 'Z') t = 0x61e + ch - 'A';
        else if (ch >= '0' && ch <= '9') t = 0x614 + ch - '0';
        else for (size_t i = 0; i < sizeof symbols / sizeof symbols[0]; i++) if (symbols[i].ch == ch) t = symbols[i].tile;
        if (t < 0) continue;
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            unsigned b = vram[t * 32 + y * 4 + x / 2]; mdgfx->glyph[ch][y * 8 + x] = (uint8_t)((x & 1) ? b & 15 : b >> 4);
        }
        mdgfx->has_glyph[ch] = 1;
    }
    for (int p = 0; p < 4; p++) for (int c = 0; c < 16; c++) mdgfx->pal[p][c] = md_col(cram[p * 16 + c]);
}

volatile int mdgfx_progress;   /* 0..100 while mdgfx_capture runs (it may run on another thread) */
int mdgfx_capture(const char *path, char *err, size_t errlen) {
    mdgfx_progress = 0;
    long n = 0; uint8_t *b = load_rom_file(path, &n);
    if (!b) { snprintf(err, errlen, "Mega Drive ROM not found"); return 0; }
    if (n != MD_ROM_SIZE || crc32_buf(b, (size_t)n) != MD_ROM_CRC) { free(b); snprintf(err, errlen, "Mega Drive ROM is not Super Hang-On (World) (Rev A)"); return 0; }
    if (!mdgfx) mdgfx = calloc(1, sizeof *mdgfx);
    if (!mdgfx) { free(b); snprintf(err, errlen, "out of memory"); return 0; }
    rom = b; romsz = n; mdgfx->ok = 0;
    sho_alt_bus = &md_bus;
    enum { U = 1, D = 2, L = 4, R = 8, A = 16, Bb = 32, C = 64, S = 128 };
    /* run 1: title (ARCADE / ORIGINAL), then the command screen with every portrait and box up */
    md_power_on();
    static const press p1[] = { { 300, S }, { 360, S }, { 420, D }, { 470, S }, { 560, D }, { 620, S } };
    run_to(455, p1, 6); capture_frame(mdgfx->img[MDG_TITLE]); clean_logo(mdgfx->img[MDG_TITLE]); mdgfx_progress = 4;
    run_to(720, p1, 6); capture_frame(mdgfx->img[MDG_COMMAND]); mdgfx_progress = 7;
    take_font();
    mdgfx->backdrop = md_col(cram[reg[7] & 63]);
    if (getenv("MDFLOW")) {   /* research: state flow from the command screen through a race and back */
        static press p3[24] = { { 760, D }, { 790, D }, { 820, S }, { 900, A } }; int np3 = 4;
        if (getenv("MDP3")) { np3 = 0; for (const char *q = getenv("MDP3"); q && np3 < 24; ) { long ff; int bb; if (sscanf(q, "%ld:%d", &ff, &bb) != 2) break; p3[np3].f = ff; p3[np3].b = (unsigned)bb; np3++; q = strchr(q, ','); if (q) q++; } }
        md_power_on();
        static uint8_t snap0[0x10000]; memcpy(snap0, ram, 0x10000);
        int last = -1; long lim = atol(getenv("MDFLOW"));
        for (long f = cur_frame; f < lim; f++) {
            buttons = 0; for (int i = 0; i < np3; i++) if (f >= p3[i].f && f < p3[i].f + 5) buttons |= p3[i].b;
            if (getenv("MDGAS") && f > atol(getenv("MDGAS"))) buttons |= 32;
            if (getenv("MDSHOT") && f % atol(getenv("MDSHOT")) == 0) { static uint32_t im[320 * 224]; capturing = 1; run_frame(); capturing = 0; grab(im); char fn[32]; snprintf(fn, sizeof fn, "flow_%05ld.ppm", f); FILE *fp = fopen(fn, "wb");
                fprintf(fp, "P6 320 224 255\n"); for (int i = 0; i < 320 * 224; i++) { fputc(im[i] >> 16 & 255, fp); fputc(im[i] >> 8 & 255, fp); fputc(im[i] & 255, fp); } fclose(fp); }
            else run_frame();
            cur_frame = f + 1;
            if (getenv("MDSNAPAT")) { static int k; long a1, a2; sscanf(getenv("MDSNAPAT"), "%ld,%ld", &a1, &a2);
                if (f == a1 || f == a2) { char fn[32]; snprintf(fn, sizeof fn, "snap_%d.bin", k++); FILE *fp = fopen(fn, "wb"); fwrite(ram, 1, 0x10000, fp); fclose(fp); } }
            int st = r16(ram, 0xc704);
            if (st != last) { fprintf(stderr, "FLOW f%ld c704 %04x c85a %d 0616 %04x pc %06x\n", f, st, r16(ram, 0xc85a), r16(ram, 0x616), m68k_get_reg(NULL, M68K_REG_PC)); last = st; }
        }
        FILE *fp = fopen("flow_ram0.bin", "wb"); fwrite(snap0, 1, 0x10000, fp); fclose(fp);
        fp = fopen("flow_ram1.bin", "wb"); fwrite(ram, 1, 0x10000, fp); fclose(fp);
    }
    /* run 2: the parts screen with the cursor on each category (the bike highlights that part) */
    md_power_on();
    static const press p2[] = { { 300, S }, { 360, S }, { 420, D }, { 470, S }, { 560, S },
                                { 720, D }, { 800, D }, { 880, D }, { 960, D }, { 1040, D } };
    for (int k = 0; k < 6; k++) { run_to(700 + k * 80, p2, 10); capture_frame(mdgfx->img[MDG_PARTS0 + k]); mdgfx_progress = 7 + 2 * (k + 1); }
    /* run 3..: each Original Mode race's course at the starting grid: colours and background strip */
    for (int r = 0; r < MD_NRACES; r++) { capture_scene(r); mdgfx_progress = 19 + 80 * (r + 1) / MD_NRACES; }
    fill_objects();
    sho_alt_bus = NULL;
    rom = NULL; romsz = 0;
    free(mdgfx->rom); mdgfx->rom = b; mdgfx->romsz = n;   /* kept for the course converter */
    /* sanity: the captures must not be blank */
    for (int i = 0; i < MDG_NIMG; i++) {
        int diff = 0; for (int p = 1; p < 320 * 224 && !diff; p++) diff = mdgfx->img[i][p] != mdgfx->img[i][0];
        if (!diff) { snprintf(err, errlen, "Mega Drive screens could not be captured"); return 0; }   /* rom stays for courses */
    }
    mdgfx->ok = 1;
    mdgfx_progress = 100;
    return 1;
}

/* ---------------- live Mega Drive (Original Mode screens) ----------------
 * The embedded Mega Drive keeps running between arcade frames while its Original Mode screens are up. The
 * shared 68000 core is switched to the Mega Drive's own context for each frame (the arcade core sets its
 * contexts before every slice it runs). */
static void *live_ctx; static int live_on;
static void live_enter(void) { rom = mdgfx->rom; romsz = mdgfx->romsz; sho_alt_bus = &md_bus; snd_live = 1; m68k_set_context(live_ctx); }
static void live_leave(void) { m68k_get_context(live_ctx); sho_alt_bus = NULL; snd_live = 0; }
static void live_run(unsigned b, uint32_t *img) {
    buttons = b;
    if (img) { capturing = 1; run_frame(); capturing = 0; grab(img); } else run_frame();
    cur_frame++;
}
int mdlive_boot(void) {
    if (!mdgfx || !mdgfx->rom) return 0;
    if (!live_ctx) live_ctx = malloc(m68k_context_size());
    if (!live_ctx) return 0;
    rom = mdgfx->rom; romsz = mdgfx->romsz; sho_alt_bus = &md_bus;
    mdsnd_boot(rom, romsz); snd_live = 1;   /* the Mega Drive's own sound driver, started by its program as on the real machine */
    md_power_on();
    m68k_get_context(live_ctx);
    /* through the logos and title to a new Original Mode game's command screen */
    static const press pb[] = { { 300, 128 }, { 360, 128 }, { 420, 2 }, { 470, 128 } };
    for (long f = 0; f < 900 && r16(ram, 0xc704) != 0x25; f++) {
        buttons = 0; for (int i = 0; i < 4; i++) if (f >= pb[i].f && f < pb[i].f + 5) buttons |= pb[i].b;
        run_frame(); cur_frame++;
    }
    m68k_get_context(live_ctx); sho_alt_bus = NULL; snd_live = 0;
    mdsnd_command((uint8_t)mdsnd_last_music());   /* the Z80 did not run during the fast start: play the screen's music */
    live_on = r16(ram, 0xc704) == 0x25;
    return live_on;
}
void mdlive_frame(unsigned b, uint32_t *img) { if (!live_on) return; live_enter(); live_run(b, img); live_leave(); }
void mdlive_frames(int n) { if (!live_on) return; live_enter(); for (int i = 0; i < n; i++) live_run(0, NULL); live_leave(); }
unsigned mdlive_state(void) { return r16(ram, 0xc704); }
unsigned mdlive_r16(unsigned a) { return r16(ram, a & 0xffff); }
uint32_t mdlive_r32(unsigned a) { return (uint32_t)r16(ram, a & 0xffff) << 16 | r16(ram, (a + 2) & 0xffff); }
void mdlive_w16(unsigned a, unsigned v) { ram[a & 0xffff] = (uint8_t)(v >> 8); ram[(a + 1) & 0xffff] = (uint8_t)v; }
void mdlive_w32(unsigned a, uint32_t v) { mdlive_w16(a, v >> 16); mdlive_w16(a + 2, v & 0xffff); }
uint32_t mdlive_backdrop(void) { return md_col(cram[reg[7] & 63]); }
/* save: the whole machine at a frame boundary (memory, video chip, 68000 registers) */
#define LIVE_MAGIC 0x4d44534eu   /* "MDSN" */
typedef struct { uint32_t magic, version; mdsnap s; uint8_t zram[0x2000]; uint8_t pad_th, pad_ctrl; int32_t vint_pending, dma_fill_pending; } livesave;
int mdlive_save(const char *path) {
    if (!live_on) return 0;
    livesave *ls = calloc(1, sizeof *ls); if (!ls) return 0;
    live_enter(); snap_take(&ls->s); live_leave();
    ls->magic = LIVE_MAGIC; ls->version = 1; memcpy(ls->zram, zram, sizeof zram); ls->zram[0x1fff] = (uint8_t)mdsnd_last_music();   /* top of Z80 memory is unused: remember the screen's music */ ls->pad_th = pad_th; ls->pad_ctrl = pad_ctrl;
    ls->vint_pending = vint_pending; ls->dma_fill_pending = dma_fill_pending;
    FILE *f = fopen(path, "wb"); int ok = 0;
    if (f) { ok = fwrite(ls, sizeof *ls, 1, f) == 1; fclose(f); }
    free(ls); return ok;
}
int mdlive_load(const char *path) {
    if (!mdgfx || !mdgfx->rom) return 0;
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    livesave *ls = calloc(1, sizeof *ls); int ok = ls && fread(ls, sizeof *ls, 1, f) == 1; fclose(f);
    if (!ok || ls->magic != LIVE_MAGIC || ls->version != 1) { free(ls); return 0; }
    if (!live_ctx) live_ctx = malloc(m68k_context_size());
    if (!live_ctx) { free(ls); return 0; }
    rom = mdgfx->rom; romsz = mdgfx->romsz; sho_alt_bus = &md_bus;
    md_power_on();
    snap_restore(&ls->s); memcpy(zram, ls->zram, sizeof zram); pad_th = ls->pad_th; pad_ctrl = ls->pad_ctrl;
    mdsnd_boot(rom, romsz); mdsnd_command(ls->zram[0x1fff]);   /* the sound driver starts afresh with the screen's music */
    vint_pending = ls->vint_pending; dma_fill_pending = ls->dma_fill_pending;
    m68k_get_context(live_ctx); sho_alt_bus = NULL;
    free(ls);
    live_on = 1;
    return 1;
}
int mdlive_saved(const char *path) { FILE *f = fopen(path, "rb"); if (!f) return 0; uint32_t m = 0; size_t n = fread(&m, 4, 1, f); fclose(f); return n == 1 && m == LIVE_MAGIC; }
void mdlive_stop(void) { live_on = 0; }
