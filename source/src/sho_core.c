/* Super Hang-On (arcade) – machine core.
 * Hardware: Sega Out Run board configured for Super Hang-On
 *   2 x 68000 @ 10MHz (main + sub), Z80 @ 4MHz, YM2151 @ 4MHz, Sega 315-5218 PCM @ 4MHz,
 *   315-5195 memory mapper, 16B-alt tilemaps, System 16B sprites, Out Run road generator.
 * Behaviour follows MAME's segaorun.cpp / 315_5195.cpp / segapcm.cpp descriptions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sho_core.h"
#include "sho_internal.h"
#include "third_party/musashi/m68k.h"
#include "third_party/z80/z80.h"
#include "third_party/nukedopm/opm.h"
#include "third_party/puff/puff.h"
#include "rc_runtime.h"
#include "sho_music.h"
#include "sho_mdsnd.h"
#include "sho_md.h"
int sho_recomp = 0;            /* 1 = run the recompiled C code, 0 = interpreter */

sho_machine *M;
/* initial entry (gamepad): requests consumed by the M_008F90 override */
int sho_entry_step, sho_entry_select, sho_menu_step;
long sho_entry_last_frame = -100, sho_menu_last_frame = -100, sho_frame_no;
unsigned long sho_debug_latch_writes, sho_debug_opm_writes, sho_debug_pcm_writes, sho_debug_tile_writes, sho_debug_last_tile;

/* ------------------------------------------------------------------ */
/* timing                                                             */
/* ------------------------------------------------------------------ */
#define LINES 262
#define M68K_PER_LINE (10000000.0 / (SHO_FPS * LINES))   /* ~635.55 */
#define Z80_PER_LINE  (4000000.0 / (SHO_FPS * LINES))    /* ~254.22 */
#define HBLANK_FRAC   (320.0 / 400.0)

/* ------------------------------------------------------------------ */
/* CPU / board state                                                  */
/* ------------------------------------------------------------------ */
static void *ctx[2];
static int cur;                    /* 0 = main, 1 = sub */
int sho_current_cpu(void) { return cur; }
static double cyc_acc[2];
static int main_halted, pending_sub_reset;
static uint8_t mapregs[0x20];
static uint8_t adc_sel;
static uint8_t in_unknown = 0xff, in_service = 0xff, in_coinage = 0xff, in_dsw = 0xfe;
static uint8_t adc[4] = { 0x80, 0x00, 0x00, 0x00 };

/* sound */
static z80 Z;
static opm_t OPM;
static int z_reset_held = 1;
static uint8_t sound_latch, pbf;
static uint8_t original_music_headers[4][10];
static double z_target;
static unsigned long z_clock;      /* t-states elapsed (incl. while held in reset) */
static unsigned long opm_clock_t, pcm_clock_t;
static unsigned opm_sub;

typedef struct { uint32_t addr; uint16_t loop; uint8_t end, freq, lvol, rvol, ctrl; } pcm_voice;
static pcm_voice voice[16];
static uint8_t pcmram[0x100];

#define AUD_MAX 4096
static float ym_l[AUD_MAX + 1], ym_r[AUD_MAX + 1]; static int ym_n;
static float pc_l[AUD_MAX + 1], pc_r[AUD_MAX + 1]; static int pc_n;

int sho_debug_adc;
static uint32_t framebuffer[SHO_MAX_W * SHO_H];
static int snd_music = 10, snd_sfx = 10;   /* 0-10, set from the pause menu */
int sho_wide_xo = 0;                      /* extra pixels each side (read by game-logic overrides) */
void sho_video_set_margin(int xo); int sho_video_width(void);
static void redraw_frame(void);
static int frame_drawn;                   /* a frame has been made (so it can be drawn again at another width) */
void sho_set_view_width(int w) {
    if (w < SHO_W) w = SHO_W;
    if (w > SHO_MAX_W) w = SHO_MAX_W;
    int old = sho_video_width();
    sho_wide_xo = (w - SHO_W) / 2;                /* the margin is the same on both sides, so the width is even */
    sho_video_set_margin(sho_wide_xo);
    if (sho_video_width() != old && frame_drawn) redraw_frame();   /* e.g. while paused: the kept frame was the old width */
}
void sho_set_widescreen(int on) { sho_set_view_width(on ? SHO_WIDE_W : SHO_W); }
int sho_view_width_for_aspect(double aspect) {
    /* the arcade monitor shows 320 pixels as 4:3, so one pixel is 4/3 / 320 * 224 of the height wide:
       a display of aspect A needs A * 240 pixels */
    if (aspect <= 0) return SHO_WIDE_W;
    int w = 2 * (int)(aspect * 240.0 / 2.0 + 0.5);
    if (w < SHO_W) w = SHO_W;
    if (w > SHO_MAX_W) w = SHO_MAX_W;
    return w;
}
int sho_width(void) { return sho_video_width(); }

/* ------------------------------------------------------------------ */
/* memory: sub CPU bus (visible to main at 0x200000)                  */
/* ------------------------------------------------------------------ */
static unsigned sub_read8(unsigned a) {
    a &= 0xfffff;
    if (a < 0x60000) return M->srom[a];
    if (a < 0x80000) return M->subram[a & 0x7fff];
    if (a < 0x90000) return M->roadram[a & 0xfff];
    if (a < 0xa0000) {   /* reading the road control port swaps the road buffers */
        for (int i = 0; i < 0x1000; i++) { uint8_t t = M->roadram[i]; M->roadram[i] = M->roadbuf[i]; M->roadbuf[i] = t; }
        return 0xff;
    }
    return 0xff;
}
static void sub_write8(unsigned a, unsigned v) {
    a &= 0xfffff;
    if (a < 0x60000) return;
    if (a < 0x80000) { M->subram[a & 0x7fff] = (uint8_t)v; return; }
    if (a < 0x90000) { M->roadram[a & 0xfff] = (uint8_t)v; return; }
    if (a < 0xa0000) { if (a & 1) M->road_control = v & 3; return; }
}

/* ------------------------------------------------------------------ */
/* memory: main CPU bus                                               */
/* ------------------------------------------------------------------ */
static void z_run_until(double target);
static double z_line_start;        /* Z80 time at the start of the current scanline */
static double main_line_done;      /* main-CPU cycles already executed in this scanline */
static void z_catch_up(void) {     /* bring the Z80 up to the main CPU's current moment */
    if (cur != 0) return;
    double elapsed = main_line_done + m68k_cycles_run();
    z_run_until(z_line_start + elapsed * (4.0 / 10.0));
}
static void sound_latch_write(uint8_t v) {
    z_catch_up();
    sound_latch = v; sho_debug_latch_writes++;
    if (!pbf) { pbf = 1; z80_gen_nmi(&Z); }
}
static void mapper_write(unsigned off, unsigned v) {
    off &= 0x1f; mapregs[off] = (uint8_t)v;
    if (off == 2) main_halted = (v & 3) == 3;
    else if (off == 3) sound_latch_write((uint8_t)v);
}
static unsigned io_read8(unsigned a) {
    if (!(a & 1)) return 0xff;
    switch (a & 0x303e) {
    case 0x1000: return in_service;
    case 0x1002: return in_unknown;
    case 0x1004: return in_coinage;
    case 0x1006: return in_dsw;
    case 0x3020: return adc[adc_sel];
    }
    return 0xff;
}
static void io_write8(unsigned a, unsigned v) {
    if (!(a & 1)) return;
    switch (a & 0x303e) {
    case 0x0000: M->out0 = (uint8_t)v; adc_sel = (v >> 6) & 3; break;
    case 0x0020: M->out20 = (uint8_t)v; z_reset_held = !(v & 1); break;
    }
}
static unsigned main_read8(unsigned a) {
    a &= 0xffffff;
    if (a < 0x60000) return M->mrom[a];
    if (a < 0x80000) return M->workram[a & 0x7fff];
    if (a >= 0x100000 && a < 0x110000) return M->tileram[a & 0xffff];
    if (a >= 0x110000 && a < 0x120000) return M->textram[a & 0xfff];
    if (a >= 0x120000 && a < 0x130000) return M->palram[a & 0x1fff];
    if (a >= 0x130000 && a < 0x140000) return M->sprram[a & 0xfff];
    if (a >= 0x140000 && a < 0x150000) return io_read8(a);
    if (a >= 0x200000 && a < 0x300000) return sub_read8(a);
    if (a & 1) {
        unsigned off = (a >> 1) & 0x1f;
        if (off < 2) return mapregs[off];
        if (off == 2) return (mapregs[2] & 3) == 3 ? 0x00 : 0x0f;
        if (off == 3) return 0x00;
    }
    return 0xff;
}
static void main_write8(unsigned a, unsigned v) {
    a &= 0xffffff;
    if (a < 0x60000) return;
    if (a < 0x80000) { M->workram[a & 0x7fff] = (uint8_t)v; return; }
    if (a >= 0x100000 && a < 0x110000) { M->tileram[a & 0xffff] = (uint8_t)v; sho_debug_tile_writes++; sho_debug_last_tile = a; return; }
    if (a >= 0x110000 && a < 0x120000) { M->textram[a & 0xfff] = (uint8_t)v; return; }
    if (a >= 0x120000 && a < 0x130000) { M->palram[a & 0x1fff] = (uint8_t)v; return; }
    if (a >= 0x130000 && a < 0x140000) { M->sprram[a & 0xfff] = (uint8_t)v; return; }
    if (a >= 0x140000 && a < 0x150000) { io_write8(a, v); return; }
    if (a >= 0x200000 && a < 0x300000) { sub_write8(a, v); return; }
    if (a & 1) mapper_write((a >> 1) & 0x1f, v);
}

/* another 68000 machine borrowing the CPU core (Mega Drive graphics capture at start-up) */
typedef struct { unsigned (*r8)(unsigned), (*r16)(unsigned); void (*w8)(unsigned, unsigned), (*w16)(unsigned, unsigned); } sho_altbus;
const sho_altbus *sho_alt_bus;
unsigned int m68k_read_memory_8(unsigned int a) { if (sho_alt_bus) return sho_alt_bus->r8(a); return cur ? sub_read8(a) : main_read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) {
    if (sho_alt_bus) return sho_alt_bus->r16(a);
    a &= 0xffffff;
    /* fast paths for ROM/RAM */
    if (cur == 0) { if (a < 0x60000) return be16r(M->mrom, a); if (a < 0x80000) return be16r(M->workram, a & 0x7fff); }
    else { unsigned s = a & 0xfffff; if (s < 0x60000) return be16r(M->srom, s); if (s >= 0x60000 && s < 0x80000) return be16r(M->subram, s & 0x7fff); }
    return (m68k_read_memory_8(a) << 8) | m68k_read_memory_8(a + 1);
}
unsigned int m68k_read_memory_32(unsigned int a) { return (m68k_read_memory_16(a) << 16) | m68k_read_memory_16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { if (sho_alt_bus) { sho_alt_bus->w8(a, v); return; } if (cur) sub_write8(a, v); else main_write8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { if (sho_alt_bus) { sho_alt_bus->w16(a, v); return; } m68k_write_memory_8(a, (v >> 8) & 0xff); m68k_write_memory_8(a + 1, v & 0xff); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xffff); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
void your_instruction_hook_function(unsigned int pc) { (void)pc; }
/* main CPU RESET instruction pulses the sub CPU reset line */
void your_reset_handler_function(void) { if (!sho_alt_bus && cur == 0) pending_sub_reset = 1; }

/* ------------------------------------------------------------------ */
/* sound: Z80 bus, SegaPCM, YM2151                                    */
/* ------------------------------------------------------------------ */
static uint8_t pcm_read(unsigned off) {
    off &= 0xff;
    pcm_voice *v = &voice[(off >> 3) & 0xf];
    switch (off & 0x87) {
    case 0x84: return (v->addr >> 8) & 0xff;
    case 0x85: return (v->addr >> 16) & 0xff;
    case 0x86: return v->ctrl;
    case 0x02: return v->lvol;
    case 0x03: return v->rvol;
    case 0x04: return v->loop & 0xff;
    case 0x05: return v->loop >> 8;
    case 0x06: return v->end;
    case 0x07: return v->freq;
    }
    return pcmram[off];
}
static void pcm_write(unsigned off, uint8_t d) {
    off &= 0xff; pcmram[off] = d; sho_debug_pcm_writes++;
    pcm_voice *v = &voice[(off >> 3) & 0xf];
    switch (off & 0x87) {
    case 0x84: v->addr = (v->addr & ~0x00ff00u) | ((uint32_t)d << 8); break;
    case 0x85: v->addr = (v->addr & ~0xff0000u) | ((uint32_t)d << 16); break;
    case 0x86: v->ctrl = d; break;
    case 0x02: v->lvol = d; break;
    case 0x03: v->rvol = d; break;
    case 0x04: v->loop = (v->loop & 0xff00) | d; break;
    case 0x05: v->loop = (v->loop & 0x00ff) | (d << 8); break;
    case 0x06: v->end = d; break;
    case 0x07: v->freq = d; break;
    }
}
static void pcm_tick(void) {
    int32_t l = 0, r = 0;
    for (int i = 0; i < 16; i++) {
        pcm_voice *v = &voice[i];
        if (v->ctrl & 1) { v->addr &= 0xffff00; continue; }
        if ((v->addr >> 16) == ((v->end + 1u) & 0xff)) {
            if (v->ctrl & 2) { v->ctrl |= 1; continue; }
            v->addr = (uint32_t)v->loop << 8;
        }
        uint32_t bank = (uint32_t)(v->ctrl & 0x70) << 12;
        int8_t s = (int8_t)(M->pcm[(bank + (v->addr >> 8)) & 0x3ffff] - 0x80);
        l += s * (v->lvol & 0x7f);
        r += s * (v->rvol & 0x7f);
        v->addr = (v->addr + v->freq) & 0xffffff;
    }
    if (pc_n < AUD_MAX) { pc_l[pc_n + 1] = l / 32768.0f; pc_r[pc_n + 1] = r / 32768.0f; pc_n++; }
}
static void sound_advance(unsigned long t) {   /* advance audio chips to Z80 time t */
    while (opm_clock_t + 2 <= t) {
        int32_t o[2]; uint8_t sh1, sh2, so;
        OPM_Clock(&OPM, o, &sh1, &sh2, &so);
        opm_clock_t += 2;
        if (++opm_sub == 32) {
            opm_sub = 0;
            if (ym_n < AUD_MAX) { ym_l[ym_n + 1] = o[0] / 32768.0f; ym_r[ym_n + 1] = o[1] / 32768.0f; ym_n++; }
        }
    }
    while (pcm_clock_t + 128 <= t) { pcm_clock_t += 128; pcm_tick(); }
}
static uint8_t z_read(void *u, uint16_t a) {
    (void)u;
    if (a < 0xf000) return M->zrom[a];
    if (a < 0xf800) return pcm_read(a);
    return M->zram[a & 0x7ff];
}
static void z_write(void *u, uint16_t a, uint8_t d) {
    (void)u;
    if (a < 0xf000) return;
    if (a < 0xf800) { pcm_write(a, d); return; }
    M->zram[a & 0x7ff] = d;
}
static uint8_t z_in(z80 *z, uint8_t port) {
    if (port < 0x40) { sound_advance(z_clock); return OPM_Read(&OPM, port & 1); }
    if (port < 0x80) {
        /* The verified sound ROM's NMI handler receives eight bytes per packet.
           Only IN at 0x006c is a command; the other seven bytes are engine data. */
        if (z->pc == 0x006e) {
            { extern uint8_t sho_md_route_sound(uint8_t); sound_latch = sho_md_route_sound(sound_latch); }   /* Original Mode filters songs and stops (and can put in its own) */
            if (getenv("SOUNDTRACE") && sound_latch != 0x80)
                fprintf(stderr, "sound f%ld cmd=%02X external=%d\n", sho_frame_no, sound_latch, sho_music_current_track());
        }
        pbf = 0; return sound_latch;
    }
    return 0xff;
}
static void z_out(z80 *z, uint8_t port, uint8_t d) {
    (void)z;
    if (port < 0x40) { sho_debug_opm_writes++; sound_advance(z_clock); OPM_Write(&OPM, port & 1, d); }
}
static void z_setup(void) {
    z80_init(&Z);
    Z.read_byte = z_read; Z.write_byte = z_write; Z.port_in = z_in; Z.port_out = z_out; Z.userdata = NULL;
}
static void z_run_until(double target) {
    while ((double)z_clock < target) {
        if (z_reset_held) {
            z_clock += 4;
            if (Z.pc != 0 || Z.cyc != 0) z_setup();   /* held in reset */
        } else {
            unsigned long before = Z.cyc;
            z80_step(&Z);
            z_clock += Z.cyc - before;
        }
        sound_advance(z_clock);
    }
}

/* ------------------------------------------------------------------ */
/* 68000 scheduling                                                   */
/* ------------------------------------------------------------------ */
static void run68(int which, double cycles) {
    cyc_acc[which] += cycles;
    int n = (int)cyc_acc[which];
    if (n <= 0) return;
    cyc_acc[which] -= n;
    if (which == 0 && main_halted) return;
    cur = which; m68k_set_context(ctx[which]);
    if (sho_recomp) rc_execute(which, n); else m68k_execute(n);
    m68k_get_context(ctx[which]);
    if (pending_sub_reset) {
        pending_sub_reset = 0;
        cur = 1; m68k_set_context(ctx[1]); m68k_pulse_reset(); m68k_get_context(ctx[1]);
        if (sho_recomp) rc_reset_cpu(1);
    }
}
static void irq68(int which, int level) { cur = which; m68k_set_context(ctx[which]); m68k_set_irq(level); m68k_get_context(ctx[which]); }

static void run_line(int line, double frac) {
    (void)line;
    run68(0, M68K_PER_LINE * frac);
    main_line_done += M68K_PER_LINE * frac;
    run68(1, M68K_PER_LINE * frac);
}

static void run_frame_internal(void) {
    ym_l[0] = ym_l[ym_n]; ym_r[0] = ym_r[ym_n]; pc_l[0] = pc_l[pc_n]; pc_r[0] = pc_r[pc_n];
    ym_n = 0; pc_n = 0;
    for (int line = 0; line < LINES; line++) {
        z_line_start = z_target; main_line_done = 0;
        if (line == 65 || line == 129 || line == 193) {          /* IRQ2 at HBLANK, cleared next line */
            run_line(line, HBLANK_FRAC);
            irq68(0, 2);
            run_line(line, 1.0 - HBLANK_FRAC);
            irq68(0, 0);
        } else if (line == 223) {                                 /* VBLANK IRQ4 on both CPUs */
            irq68(0, 4); irq68(1, 4);
            run_line(line, 1.0);
            irq68(0, 0); irq68(1, 0);
            /* tilemap scroll/page values are latched at line 261 in hardware; we render at vblank */
        } else {
            run_line(line, 1.0);
        }
        z_target += Z80_PER_LINE;
        z_run_until(z_target);
        if (line == 223) sho_video_render(framebuffer);
    }
    sho_frame_no++;
    /* requests the initials screen hasn't picked up within a few frames are dropped */
    { static int age; if (sho_entry_step || sho_entry_select || sho_menu_step) { if (++age > 8) { sho_entry_step = 0; sho_entry_select = 0; sho_menu_step = 0; age = 0; } } else age = 0; }
    /* keep counters bounded */
    if (z_clock > 0x40000000UL) {
        unsigned long base = z_clock - 1024;
        z_clock -= base; opm_clock_t -= base; pcm_clock_t -= base; z_target -= (double)base;
    }
}

void sho_run_frame(void) {
    frame_drawn = 1;
    if (!md_enabled) { run_frame_internal(); return; }
    if (md_frozen()) { md_pre_frame(); sho_video_render(framebuffer); md_compose(framebuffer, sho_video_width()); return; }   /* the arcade holds still (the title under the mode choice) */
    md_pre_frame(); run_frame_internal(); md_post_frame();
    for (int n = md_extra_frames(); n > 0; n--) { md_pre_frame(); run_frame_internal(); md_post_frame(); }
    md_compose(framebuffer, sho_video_width());
}

/* Mega Drive mode pause: the frame is kept and the pause menu drawn over it */
static uint32_t pause_keep[SHO_MAX_W * SHO_H]; static int pause_kept, pause_tick;
static char pause_display[24] = "16:9";
/* the current frame again at the current width (the game doesn't move: the video chips' state is drawn once more) */
static void redraw_frame(void) {
    sho_video_render(framebuffer);
    if (md_enabled) md_compose(framebuffer, sho_video_width());
    if (pause_kept) {   /* the pause menu goes over the new one */
        memcpy(pause_keep, framebuffer, sizeof pause_keep);
        md_pause_menu(framebuffer, sho_video_width(), pause_tick, NULL, &snd_music, &snd_sfx, pause_display);
    }
}
void sho_pause_set_display(const char *name) { snprintf(pause_display, sizeof pause_display, "%s", name ? name : ""); }
int sho_pause_begin(void) {
    if (!md_enabled || !sho_md_pause_allowed()) return 0;
    memcpy(pause_keep, framebuffer, sizeof pause_keep); pause_kept = 1; md_pause_reset();
    return 1;
}
int sho_pause_update(const sho_input *in, int tick) {
    if (!pause_kept) return 1;
    memcpy(framebuffer, pause_keep, sizeof pause_keep);
    pause_tick = tick;
    return md_pause_menu(framebuffer, sho_video_width(), tick, in, &snd_music, &snd_sfx, pause_display);
}
void sho_pause_end(void) { if (pause_kept) memcpy(framebuffer, pause_keep, sizeof pause_keep); pause_kept = 0; }
void sho_set_sound_levels(int music, int sfx) { snd_music = music < 0 ? 0 : music > 10 ? 10 : music; snd_sfx = sfx < 0 ? 0 : sfx > 10 ? 10 : sfx; }
void sho_get_sound_levels(int *music, int *sfx) { *music = snd_music; *sfx = snd_sfx; }

/* ------------------------------------------------------------------ */
/* public API                                                         */
/* ------------------------------------------------------------------ */
static int nav_held[4], pad_a_prev, rt_held;
static int dpad_repeat(int down, int *held) {      /* press, then auto-repeat after 18 frames every 5 */
    if (!down) { *held = 0; return 0; }
    int h = ++*held;
    return h == 1 || (h > 18 && (h - 18) % 5 == 0);
}
void sho_set_input(const sho_input *in_raw) {
    sho_input in_md; const sho_input *in = in_raw;
    if (md_enabled) { in_md = *in_raw; md_filter_input(&in_md); in = &in_md; }
    int entry = sho_frame_no - sho_entry_last_frame <= 2;   /* initials screen active (seen last frame) */
    int menu = sho_frame_no - sho_menu_last_frame <= 2;     /* class / music select active */
    int up = dpad_repeat(in->nav_up, &nav_held[0]), down = dpad_repeat(in->nav_down, &nav_held[1]);
    int left = dpad_repeat(in->nav_left, &nav_held[2]), right = dpad_repeat(in->nav_right, &nav_held[3]);
    /* initials: up = next character, down = previous */
    if (up) sho_entry_step += 1;
    if (down) sho_entry_step -= 1;
    /* class / music select: down or right = next item, up or left = previous */
    if (down || right) sho_menu_step += 1;
    if (up || left) sho_menu_step -= 1;
    if (in->pad_a && !pad_a_prev && entry) sho_entry_select = 1;
    pad_a_prev = in->pad_a;
    uint8_t u = 0xff;
    if (in->coin) u &= ~0x01;
    if (in->test) u &= ~0x04;
    if (in->service) u &= ~0x08;
    /* right trigger / throttle as a button on the select screens (hysteresis so a resting trigger can't chatter) */
    rt_held = rt_held ? in->throttle > 0.30f : in->throttle > 0.50f;
    if (in->start || (menu && (in->pad_a || rt_held))) u &= ~0x10;   /* A or right trigger = Start on the select screens */
    if (in->turbo) u &= ~0x20;
    in_unknown = u;
    float s = in->steer; if (s < -1) s = -1; if (s > 1) s = 1;
    /* steering is reversed on this cabinet (MAME: PORT_REVERSE): right = lower value */
    { int a; if (md_enabled && md_steer_adc(s, &a)) adc[0] = (uint8_t)a; else adc[0] = (uint8_t)(0x80 - (int)(s * 0x60)); sho_debug_adc = adc[0]; }
    float t = in->throttle < 0 ? 0 : in->throttle > 1 ? 1 : in->throttle;
    if (in->pad_a && !entry && !menu) t = 1.0f;   /* A accelerates, except on the initials/select screens */
    float b = in->brake < 0 ? 0 : in->brake > 1 ? 1 : in->brake;
    adc[1] = (uint8_t)(t * 255.0f + 0.5f);
    adc[2] = (uint8_t)(b * 255.0f + 0.5f);
}

/* hash of all machine state that game logic can observe (for interpreter/recompiler lockstep checks) */
uint64_t sho_state_hash(void) {
    uint64_t h = 1469598103934665603ULL;
#define HB(p, n) for (size_t i_ = 0; i_ < (size_t)(n); i_++) { h ^= ((const uint8_t *)(p))[i_]; h *= 1099511628211ULL; }
    HB(M->workram, sizeof M->workram); HB(M->subram, sizeof M->subram);
    HB(M->roadram, sizeof M->roadram); HB(M->roadbuf, sizeof M->roadbuf);
    HB(M->tileram, sizeof M->tileram); HB(M->textram, sizeof M->textram);
    HB(M->palram, sizeof M->palram); HB(M->sprram, sizeof M->sprram); HB(M->zram, sizeof M->zram);
    for (int c = 0; c < 2; c++) {
        static const int regs[] = { M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
            M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7, M68K_REG_PC, M68K_REG_SR };
        for (int r = 0; r < 18; r++) { uint32_t v = m68k_get_reg(ctx[c], regs[r]); HB(&v, 4); }
    }
#undef HB
    return h;
}
void sho_set_dips(uint8_t coinage, uint8_t dsw) { in_coinage = coinage; in_dsw = dsw; }
const uint32_t *sho_framebuffer(void) { return framebuffer; }
size_t sho_nvram_size(void) { return sizeof M->workram; }
uint8_t *sho_nvram(void) { return M->workram; }

static inline int16_t clamp16(float v) { v *= 32767.0f; if (v > 32767) v = 32767; if (v < -32768) v = -32768; return (int16_t)v; }
static float lerp_at(const float *src, int n, double pos) {
    if (n <= 0) return src[0];
    int i = (int)pos; if (i >= n) return src[n];
    double f = pos - i;
    return (float)(src[i] + (src[i + 1] - src[i]) * f);
}
void sho_debug_voices(void) {
    for (int i = 0; i < 16; i++) { pcm_voice *v = &voice[i]; fprintf(stderr, "v%2d ctrl=%02X addr=%06X loop=%04X end=%02X freq=%02X vol=%02X/%02X\n", i, v->ctrl, v->addr, v->loop, v->end, v->freq, v->lvol, v->rvol); }
}
int sho_debug_mix = 0; /* 1 = YM only, 2 = PCM only */
void sho_audio(int16_t *out, int count) {
    float gy = sho_debug_mix == 2 ? 0.0f : 0.30f, gp = sho_debug_mix == 1 ? 0.0f : 0.70f;
    extern int sho_md_preview(void);
    int pv = md_enabled ? sho_md_preview() : 0;   /* Original Mode's music select preview: 1 = the arcade's own song (FM, with its PCM drums), 2 = an arranged MP3 */
    /* sound levels: the FM chip carries the music (with the arranged soundtrack playing, only effects);
       the PCM chip the effects, voices and drums */
    gy *= (sho_music_current_track() >= 0 ? snd_sfx : snd_music) / 10.0f; gp *= snd_sfx / 10.0f;
    /* src[0] holds the previous frame's last sample; map output j -> src position (j+1)*n/count */
    for (int j = 0; j < count; j++) {
        double py = (double)(j + 1) * ym_n / count, pp = (double)(j + 1) * pc_n / count;
        float l = gy * lerp_at(ym_l, ym_n, py) + gp * lerp_at(pc_l, pc_n, pp);
        float r = gy * lerp_at(ym_r, ym_n, py) + gp * lerp_at(pc_r, pc_n, pp);
        out[j * 2] = clamp16(l); out[j * 2 + 1] = clamp16(r);
    }
    int amode = sho_md_audio_mode(), mute = md_enabled && md_mute();
    if (mute && pv != 1) memset(out, 0, (size_t)count * 4);
    if (amode != 1 && (!mute || pv == 2)) sho_music_mix(out, count);
    if (amode == 2 || (amode == 1 && !mute)) mdsnd_add(out, count, 48000, snd_music * 10);   /* the Mega Drive's sound driver */
}

void sho_reset(void) {
    sho_music_stop();
    memset(M->subram, 0, sizeof M->subram); memset(M->roadram, 0, sizeof M->roadram); memset(M->roadbuf, 0, sizeof M->roadbuf);
    memset(M->tileram, 0, sizeof M->tileram); memset(M->textram, 0, sizeof M->textram);
    memset(M->palram, 0, sizeof M->palram); memset(M->sprram, 0, sizeof M->sprram); memset(M->zram, 0, sizeof M->zram);
    memset(mapregs, 0, sizeof mapregs); main_halted = 0; pending_sub_reset = 0;
    M->road_control = 0; M->out0 = 0; M->out20 = 0;
    m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    if (!ctx[0]) { ctx[0] = malloc(m68k_context_size()); ctx[1] = malloc(m68k_context_size()); }
    cur = 1; m68k_pulse_reset(); m68k_get_context(ctx[1]);
    cur = 0; m68k_pulse_reset(); m68k_get_context(ctx[0]);
    cyc_acc[0] = cyc_acc[1] = 0;
    if (sho_recomp) rc_init();
    memset(voice, 0, sizeof voice); memset(pcmram, 0, sizeof pcmram);
    for (int i = 0; i < 16; i++) voice[i].ctrl = 1;
    OPM_Reset(&OPM, 0);
    z_setup(); z_reset_held = 1; pbf = 0; sound_latch = 0;
    z_clock = opm_clock_t = pcm_clock_t = 0; opm_sub = 0; z_target = 0;
    ym_n = pc_n = 0; memset(ym_l, 0, sizeof ym_l); memset(ym_r, 0, sizeof ym_r); memset(pc_l, 0, sizeof pc_l); memset(pc_r, 0, sizeof pc_r);
    sho_video_init();
}

/* ------------------------------------------------------------------ */
/* ROM loading (zip or folder)                                        */
/* ------------------------------------------------------------------ */
typedef struct { const char *name; uint32_t size, crc; uint8_t *data; } romfile;
static romfile roms[] = {
    { "epr-10886.133", 0x10000, 0x8be3cd36 }, { "epr-10884.118", 0x10000, 0xcb06150d },
    { "epr-10887.132", 0x10000, 0x8d248bb0 }, { "epr-10885.117", 0x10000, 0x70795f26 },
    { "epr-10792.76",  0x10000, 0x16299d25 }, { "epr-10790.58",  0x10000, 0x2246cbc1 },
    { "epr-10793.75",  0x10000, 0xd9525427 }, { "epr-10791.57",  0x10000, 0x5faf4cbe },
    { "epr-10652.54",  0x08000, 0x260286f9 }, { "epr-10651.55",  0x08000, 0xc609ee7b },
    { "epr-10650.56",  0x08000, 0xb236a403 },
    { "mpr-10794.8",   0x20000, 0x7c958e63 }, { "mpr-10798.16",  0x20000, 0x7d58f807 },
    { "mpr-10795.6",   0x20000, 0xd9d31f8c }, { "mpr-10799.14",  0x20000, 0x96d90d3d },
    { "mpr-10796.4",   0x20000, 0xfb48957c }, { "mpr-10800.12",  0x20000, 0xfeaff98e },
    { "mpr-10797.2",   0x20000, 0x27f2870d }, { "mpr-10801.10",  0x20000, 0x12781795 },
    { "epr-10642.47",  0x08000, 0x7836bcc3 }, { "epr-10649c.88", 0x08000, 0xf6c1ce71 },
    { "epr-10643.66",  0x08000, 0x06f55364 }, { "epr-10644.67",  0x08000, 0xb41d541d },
    { "epr-10645.68",  0x08000, 0xa60dabff }, { "epr-10646.69",  0x08000, 0x473cc411 },
};
#define NROMS (sizeof roms / sizeof roms[0])

static uint32_t crc32_buf(const uint8_t *p, size_t n) {
    static uint32_t t[256]; static int init;
    if (!init) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = 1; }
    uint32_t c = 0xffffffffu;
    while (n--) c = t[(c ^ *p++) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
static int name_eq(const char *a, size_t alen, const char *b) {
    /* compare basename of a (length alen) with b, case-insensitive */
    size_t s = 0; for (size_t i = 0; i < alen; i++) if (a[i] == '/' || a[i] == '\\') s = i + 1;
    size_t bl = strlen(b); if (alen - s != bl) return 0;
    for (size_t i = 0; i < bl; i++) if (tolower((unsigned char)a[s + i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}
static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); if (b) *len = (size_t)n; return b;
}
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int load_zip(const uint8_t *z, size_t zl) {
    if (zl < 22) return -1;
    size_t e = zl - 22, stop = zl > 0x10000 + 22 ? zl - 0x10000 - 22 : 0;
    for (;; e--) { if (le32(z + e) == 0x06054b50) break; if (e == stop) return -1; }
    uint32_t cd = le32(z + e + 16); uint16_t cnt = le16(z + e + 10);
    for (uint16_t i = 0; i < cnt && cd + 46 <= zl; i++) {
        const uint8_t *h = z + cd;
        if (le32(h) != 0x02014b50) return -1;
        uint16_t method = le16(h + 10), nl = le16(h + 28), xl = le16(h + 30), cl = le16(h + 32);
        uint32_t csize = le32(h + 20), usize = le32(h + 24), lho = le32(h + 42);
        const char *nm = (const char *)h + 46;
        for (size_t r = 0; r < NROMS; r++) {
            if (roms[r].data || !name_eq(nm, nl, roms[r].name) || usize != roms[r].size) continue;
            if (lho + 30 > zl) continue;
            const uint8_t *lh = z + lho;
            size_t off = lho + 30 + le16(lh + 26) + le16(lh + 28);
            if (off + csize > zl) continue;
            uint8_t *buf = malloc(usize); if (!buf) return -1;
            if (method == 0) memcpy(buf, z + off, usize);
            else if (method == 8) {
                unsigned long dl = usize, sl = csize;
                if (puff(buf, &dl, z + off, &sl) != 0 || dl != usize) { free(buf); continue; }
            } else { free(buf); continue; }
            roms[r].data = buf;
        }
        cd += 46 + nl + xl + cl;
    }
    return 0;
}

int sho_load_roms(const char *path, char *err, size_t errlen) {
    if (!M) { M = calloc(1, sizeof *M); if (!M) { snprintf(err, errlen, "Out of memory"); return -1; } }
    for (size_t r = 0; r < NROMS; r++) { free(roms[r].data); roms[r].data = NULL; }
    size_t zl = 0; uint8_t *z = read_file(path, &zl);
    if (z) { load_zip(z, zl); free(z); }
    else {
        for (size_t r = 0; r < NROMS; r++) {
            char p[1024]; size_t n = 0; snprintf(p, sizeof p, "%s/%s", path, roms[r].name);
            uint8_t *b = read_file(p, &n);
            if (b && n == roms[r].size) roms[r].data = b; else free(b);
        }
    }
    char missing[512] = ""; int bad = 0;
    for (size_t r = 0; r < NROMS; r++) {
        const char *why = NULL;
        if (!roms[r].data) why = "missing";
        else if (crc32_buf(roms[r].data, roms[r].size) != roms[r].crc) why = "bad checksum";
        if (why) { bad++; size_t l = strlen(missing); if (l < sizeof missing - 64) snprintf(missing + l, sizeof missing - l, "%s (%s)\n", roms[r].name, why); }
    }
    if (bad) { snprintf(err, errlen, "Could not load the Super Hang-On ROM set (MAME 'shangon') from:\n%s\n\n%s", path, missing); return -1; }

#define R(i) roms[i].data
    for (int k = 0; k < 2; k++) {   /* main: 10886/10884 then 10887/10885 ; sub: 10792/10790 then 10793/10791 */
        for (int h = 0; h < 2; h++) {
            const uint8_t *ev = R(k * 4 + h * 2), *od = R(k * 4 + h * 2 + 1);
            uint8_t *dst = (k == 0 ? M->mrom : M->srom) + h * 0x20000;
            for (int i = 0; i < 0x10000; i++) { dst[i * 2] = ev[i]; dst[i * 2 + 1] = od[i]; }
        }
    }
    memset(M->mrom + 0x40000, 0xff, 0x20000); memset(M->srom + 0x40000, 0xff, 0x20000);
    memcpy(M->tiles, R(8), 0x8000); memcpy(M->tiles + 0x8000, R(9), 0x8000); memcpy(M->tiles + 0x10000, R(10), 0x8000);
    for (int b = 0; b < 4; b++) {   /* sprites: odd byte = mpr-10794.., even byte = mpr-10798.. */
        const uint8_t *od = R(11 + b * 2), *ev = R(12 + b * 2);
        for (int i = 0; i < 0x20000; i++) M->sprites[b * 0x20000 + i] = (uint16_t)((ev[i] << 8) | od[i]);
    }
    memset(M->road, 0, sizeof M->road); memcpy(M->road, R(19), 0x8000);
    memset(M->zrom, 0, sizeof M->zrom); memcpy(M->zrom, R(20), 0x8000);
    for (int i = 0; i < 4; i++) memcpy(original_music_headers[i], M->zrom + sho_music_tracks[i].rom_header, 10);
    for (int i = 0; i < 4; i++) { memcpy(M->pcm + i * 0x10000, R(21 + i), 0x8000); memcpy(M->pcm + i * 0x10000 + 0x8000, R(21 + i), 0x8000); }
#undef R
    for (size_t r = 0; r < NROMS; r++) { free(roms[r].data); roms[r].data = NULL; }
    memset(M->workram, 0, sizeof M->workram);
    return 0;
}

const uint8_t *sho_text_font(void) { return M ? M->tiles : NULL; }   /* the text layer's 8x8 tiles (3 planes of 0x8000), after sho_load_roms */

void sho_silence_music(void) {   /* the arcade's four songs off (the Mega Drive's play instead); the ROM archive is never changed */
    if (!M) return;
    for (int i = 0; i < 4; i++) { uint8_t *header = M->zrom + sho_music_tracks[i].rom_header; memset(header, 0, 10); header[0] = 1; }
}
int sho_load_music(const char *directory, unsigned sample_rate, char *err, size_t errlen) {
    if (!M) { if (errlen) snprintf(err, errlen, "Load the ROM set before the music"); return 0; }
    int loaded = sho_music_load_files(directory, sample_rate, err, errlen);
    unsigned mask = sho_music_mask();
    for (int i = 0; i < 4; i++) {
        uint8_t *header = M->zrom + sho_music_tracks[i].rom_header;
        memcpy(header, original_music_headers[i], 10);
        if (mask & (1u << i)) {
            /* Let the original driver handle stop/start and preserve SFX. Replace
               this song's header with one inactive channel (the loader needs >=1).
               The on-disk ROM archive is never changed. */
            memset(header, 0, 10); header[0] = 1;
        }
    }
    return loaded;
}

/* ---- a small log for problem reports (shangon.log in the game folder): what the display and sound are doing ---- */
#include <stdarg.h>
static FILE *diag_f;
void sho_diag_open(const char *path) { if (diag_f) fclose(diag_f); diag_f = path ? fopen(path, "w") : NULL; }
void sho_diag(const char *fmt, ...) {
    if (!diag_f) return;
    va_list ap; va_start(ap, fmt); vfprintf(diag_f, fmt, ap); va_end(ap);
    fputc('\n', diag_f); fflush(diag_f);
}
