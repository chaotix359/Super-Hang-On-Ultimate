#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sho_mdsnd.h"
#include "third_party/z80/z80.h"
#include "third_party/nukedopn2/ym3438.h"

#define DRIVER_ADDR 0x7D7F4
#define DRIVER_LEN  0x1A24
#define YM_RATE     53267.0           /* 7670454 / 144 */
#define Z80_PER_SAMPLE (3579545.0 / YM_RATE)
#define SAMPLES_PER_INT (YM_RATE / 59.92)

static const uint8_t *rom; static long romsz;
static uint8_t zram[0x2000];
static z80 cpu; static ym3438_t ym;
static int ready, running, follow;
static unsigned bank;
static double z80_target, int_acc;
static double opn_clk, opn_int;
int mdsnd_log; static unsigned last_cmd, last_music;
static long warm_s; static int pend = -1;
/* commands wait in a short queue: the driver clears its mailbox while it starts, takes one command at a time, and only runs while audio is pulled */
static uint8_t cq[16]; static int cq_n;
static int cq_gap, cq_wait; static uint8_t cq_last;
static int cq_song(uint8_t c) { return (c >= 0x81 && c <= 0x8a) || c == 0xb0 || c == 0xb1; }
static void cq_put(uint8_t c) {
    if (cq_song(c)) { int k = 0; for (int i = 0; i < cq_n; i++) if (!cq_song(cq[i])) cq[k++] = cq[i]; cq_n = k; }   /* a newer song/stop command replaces waiting ones */
    if (cq_n == 16) { memmove(cq, cq + 1, 15); cq_n = 15; } cq[cq_n++] = c; }    /* the driver clears its mailbox while it starts: early commands wait */

/* ---- SN76489 ---- */
static struct { unsigned tone[3], vol[4], noise, latch; int cnt[4], out[4]; unsigned lfsr; } psg;
static double psg_acc;
static void psg_reset(void) { memset(&psg, 0, sizeof psg); for (int i = 0; i < 4; i++) psg.vol[i] = 15; psg.lfsr = 0x8000; psg.noise = 0; }
static void psg_write(uint8_t v) {
    if (v & 0x80) {
        psg.latch = (v >> 4) & 7;
        unsigned r = psg.latch;
        if (r & 1) psg.vol[r >> 1] = v & 15;
        else if (r == 6) { psg.noise = v & 7; psg.lfsr = 0x8000; }
        else psg.tone[r >> 1] = (psg.tone[r >> 1] & 0x3f0) | (v & 15);
    } else {
        unsigned r = psg.latch;
        if (r & 1) psg.vol[r >> 1] = v & 15;
        else if (r == 6) { psg.noise = v & 7; psg.lfsr = 0x8000; }
        else psg.tone[r >> 1] = (psg.tone[r >> 1] & 15) | ((v & 0x3f) << 4);
    }
}
static float psg_vol_tab[16];
static float psg_sample(void) {
    psg_acc += 3579545.0 / 16.0 / YM_RATE;
    float sum = 0; int n = 0;
    while (psg_acc >= 1.0) {
        psg_acc -= 1.0;
        for (int c = 0; c < 3; c++) if (--psg.cnt[c] <= 0) { psg.cnt[c] = psg.tone[c] ? (int)psg.tone[c] : 1; psg.out[c] ^= 1; }
        if (--psg.cnt[3] <= 0) {
            unsigned n3 = psg.noise & 3;
            psg.cnt[3] = n3 == 0 ? 16 : n3 == 1 ? 32 : n3 == 2 ? 64 : (psg.tone[2] ? (int)psg.tone[2] * 1 : 1) * 1;
            psg.out[3] ^= 1;
            if (psg.out[3]) {   /* shift on the rising edge */
                unsigned fb = (psg.noise & 4) ? ((psg.lfsr ^ (psg.lfsr >> 3)) & 1) : (psg.lfsr & 1);
                psg.lfsr = (psg.lfsr >> 1) | (fb << 15);
            }
        }
        float s = 0;
        for (int c = 0; c < 3; c++) s += (psg.out[c] ? 1 : -1) * psg_vol_tab[psg.vol[c]] * (psg.tone[c] < 2 ? 0 : 1);
        s += ((psg.lfsr & 1) ? 1 : -1) * psg_vol_tab[psg.vol[3]];
        sum += s; n++;
    }
    return n ? sum / n : 0;
}

/* ---- Z80 bus ---- */
static uint8_t z_read(void *u, uint16_t a) {
    (void)u;
    if (a < 0x4000) { if (mdsnd_log > 2 && (a & 0x1fff) >= 0x1c00) { static unsigned seen[0x400]; unsigned k = (a & 0x3ff); if (mdsnd_log > 3 || seen[k] != cpu.pc) { seen[k] = cpu.pc; fprintf(stderr, "ZR %04X pc=%04X -> %02X\n", a, cpu.pc, zram[a & 0x1fff]); } } return zram[a & 0x1fff]; }
    if (a < 0x6000) return OPN2_Read(&ym, a & 3);
    if (a >= 0x8000) { unsigned p = (bank << 15) | (a & 0x7fff); return (rom && p < (unsigned)romsz) ? rom[p] : 0xff; }
    return 0xff;
}
static void z_write(void *u, uint16_t a, uint8_t v) {
    (void)u;
    if (a < 0x4000) { if (mdsnd_log > 3 && (a & 0x1fff) >= 0x1c00 && (a & 0x1fff) < 0x1c40) fprintf(stderr, "ZW %04X <- %02X pc=%04X\n", a & 0x1fff, v, cpu.pc); zram[a & 0x1fff] = v; return; }
    if (a < 0x6000) { if (mdsnd_log > 1) fprintf(stderr, "YM %d %02X pc=%04X\n", a & 3, v, cpu.pc); OPN2_Write(&ym, a & 3, v); return; }
    if (a >= 0x6000 && a < 0x6100) { bank = (bank >> 1) | ((unsigned)(v & 1) << 8); return; }
    if (a == 0x7f11 || a == 0x7f13 || a == 0x7f15 || a == 0x7f17) { psg_write(v); return; }
}
static uint8_t z_in(z80 *z, uint8_t p) { (void)z; (void)p; return 0xff; }
static void z_out(z80 *z, uint8_t p, uint8_t v) { (void)z; (void)p; (void)v; }

static void chip_reset(void) {
    OPN2_SetChipType(ym3438_mode_ym2612);
    OPN2_Reset(&ym);
    psg_reset(); psg_acc = 0; bank = 0;
    z80_init(&cpu);
    cpu.read_byte = z_read; cpu.write_byte = z_write; cpu.port_in = z_in; cpu.port_out = z_out;
    z80_target = 0; cpu.cyc = 0; int_acc = 0; opn_clk = 0; opn_int = 0; warm_s = 0;
}
int mdsnd_boot(const uint8_t *r, long n) {
    if (!r || n < DRIVER_ADDR + DRIVER_LEN) return 0;
    rom = r; romsz = n;
    for (int i = 0; i < 16; i++) psg_vol_tab[i] = i == 15 ? 0.f : 110.f * (float)__builtin_pow(10.0, -0.1 * i);
    memset(zram, 0, sizeof zram);
    memcpy(zram, rom + DRIVER_ADDR, DRIVER_LEN);
    chip_reset(); pend = -1; cq_n = 0; cq_gap = 0; cq_wait = 0; cq_last = 0xff; last_music = 0;
    ready = running = 1; follow = 0;
    return 1;
}
int mdsnd_ready(void) { return ready; }
void mdsnd_shutdown(void) { ready = running = follow = 0; }
/* the live Mega Drive was run ahead of the sound (a fast-forward): restart the driver cleanly on the screen's music
   rather than let the Z80 pick up halfway through the 68000's commands */
void mdsnd_resync(void) {
    if (!ready) return;
    unsigned m = last_music;
    memset(zram, 0, sizeof zram);   /* as a first start: the driver's variables from the old song must not survive */
    memcpy(zram, rom + DRIVER_ADDR, DRIVER_LEN);
    chip_reset(); running = 1; pend = -1; cq_n = 0; cq_gap = 0; cq_wait = 0; cq_last = 0xff; last_music = m;
    if (m) cq_put((uint8_t)m);
}
void mdsnd_set_follow(int on) { follow = on; }
unsigned mdsnd_last_command(void) { return last_cmd; }
unsigned mdsnd_last_music(void) { return last_music; }
void mdsnd_command(uint8_t c) {
    if (!ready) return;
    last_cmd = c; if (c >= 0x81 && c <= 0x8a) last_music = c; if (c == 0xb0 || c == 0xb1) last_music = 0; if (mdsnd_log) fprintf(stderr, "MDSND cmd %02X\n", c);
    cq_put(c);
}
unsigned (*mdsnd_command_hook)(unsigned cmd);   /* may replace the Mega Drive program's sound commands */
void mdsnd_zwrite(unsigned a, uint8_t v) {
    if (!ready) return;
    a &= 0x1fff;
    if (a == 0x1c03 && v && mdsnd_command_hook) v = (uint8_t)mdsnd_command_hook(v);
    if (a == 0x1c03) { if (!v) return; cq_put(v); }   /* the mailbox: queued, delivered when the driver is ready for it */
    else zram[a] = v;
    if (a == 0x1c03) { last_cmd = v; if (v >= 0x81 && v <= 0x8a) last_music = v; if (v == 0xb0 || v == 0xb1) last_music = 0; if (mdsnd_log) fprintf(stderr, "MDSND cmd %02X\n", v); }
}
uint8_t mdsnd_zread(unsigned a) { return zram[a & 0x1fff]; }
void mdsnd_zreset(int run) {
    if (!ready) return;
    if (mdsnd_log) fprintf(stderr, "MDSND zreset run=%d (was %d)\n", run, running);
    if (run && !running) chip_reset();
    running = run;
}

/* one chip sample (stereo, float): the YM2612 runs 24 internal clocks per sample (1.278 MHz); the Z80 (3.58 MHz) is
   interleaved clock by clock so that its register writes land at the right chip times */
#define OPN_PER_FRAME (1278408.0 / 59.92)
static void chip_sample(float *l, float *r) {
    int sl = 0, sr = 0;
    if (warm_s < 15000) warm_s++;
    else if (cq_gap > 0) cq_gap--;
    else if (cq_n) {   /* the driver leaves its own marker (0 or $80) in the mailbox once it has taken a command; a command nobody took in half a second is replaced */
        if (zram[0x1c03] != cq_last || ++cq_wait > 24000) { cq_last = cq[0]; zram[0x1c03] = cq[0]; memmove(cq, cq + 1, (size_t)--cq_n); cq_gap = 2400; cq_wait = 0; }
    }
    for (int i = 0; i < 24; i++) {
        if (running) {
            if (opn_int >= OPN_PER_FRAME) { opn_int -= OPN_PER_FRAME; z80_gen_int(&cpu, 0xff); }
            while ((double)cpu.cyc <= opn_clk * 2.8) z80_step(&cpu);   /* 3.58 MHz Z80 : 1.278 MHz YM clock */
        }
        int16_t b[2]; OPN2_Clock(&ym, b); sl += b[0]; sr += b[1];
        opn_clk += 1.0; opn_int += 1.0;
    }
    if (mdsnd_log == 5) { static int mxc[6], cnt; for (int c = 0; c < 6; c++) { int v = ym.ch_out[c]; if (v < 0) v = -v; if (v > mxc[c]) mxc[c] = v; }
        if (++cnt == 53267) { cnt = 0; fprintf(stderr, "CH %d %d %d %d %d %d\n", mxc[0], mxc[1], mxc[2], mxc[3], mxc[4], mxc[5]); memset(mxc, 0, sizeof mxc); } }
    float p = psg_sample();
    static float hl, hr;   /* one-pole high-pass: removes the YM2612 DAC's offset */
    float xl = (float)sl + p, xr = (float)sr + p;
    hl += (xl - hl) * 0.0005f; hr += (xr - hr) * 0.0005f;
    *l = xl - hl; *r = xr - hr;
}
void mdsnd_add(int16_t *out, int frames, unsigned rate, int gain) {
    static float pl, pr, nl, nr; static double pos = 1.0;
    if (!ready || frames <= 0 || !rate) return;
    double step = YM_RATE / rate; float g = gain / 100.0f * 18.0f;
    for (int j = 0; j < frames; j++) {
        pos += step;
        while (pos >= 1.0) { pos -= 1.0; pl = nl; pr = nr; chip_sample(&nl, &nr); }
        float l = pl + (nl - pl) * (float)pos, r = pr + (nr - pr) * (float)pos;
        int a = out[j * 2] + (int)(l * g), b = out[j * 2 + 1] + (int)(r * g);
        out[j * 2] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        out[j * 2 + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }
}
