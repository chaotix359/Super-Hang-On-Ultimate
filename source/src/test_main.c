/* Headless test frontend (Linux): runs the core with a scripted input sequence,
 * writes PPM screenshots and a WAV of the audio, and reports timing. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "sho_core.h"
#include "sho_internal.h"
#include "sho_music.h"
#include "sho_md.h"
#include "sho_mdgfx.h"
static int ap_centre = -1;
static float autopilot(void) {   /* steer to keep the road centred (reads the displayed road buffer) */
    uint16_t d0 = be16r(M->roadbuf, 190 * 2);
    int h = be16r(M->roadbuf, (0x200 + (d0 & 0x1ff)) * 2) & 0xfff;
    if (ap_centre < 0) { ap_centre = h; return 0; }
    int err = ((h - ap_centre + 0x800) & 0xfff) - 0x800;
    float v = -1.5f * err / 96.0f; if (v > 1) v = 1; if (v < -1) v = -1; return v;
}

static void write_ppm(const char *p) {
    FILE *f = fopen(p, "wb"); const uint32_t *fb = sho_framebuffer();
    fprintf(f, "P6\n%d %d\n255\n", sho_width(), SHO_H);
    for (int i = 0; i < sho_width() * SHO_H; i++) { uint8_t c[3] = { fb[i] >> 16, fb[i] >> 8, fb[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *rom = argc > 1 ? argv[1] : "shangon.zip";
    int frames = argc > 2 ? atoi(argv[2]) : 3000;
    const char *steer_mode = argc > 3 ? argv[3] : "weave";
    char err[2048];
    if (sho_load_roms(rom, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    if (getenv("MUSIC_DIR")) {
        int loaded = sho_load_music(getenv("MUSIC_DIR"), 48000, err, sizeof err);
        printf("external music: %d/4 loaded%s%s\n", loaded, *err ? "; " : "", err);
    }
    { extern int sho_recomp; if (getenv("RECOMP")) sho_recomp = 1; }
    if (getenv("MDROM")) {
        clock_t c0 = clock();
        int ok = mdgfx_capture(getenv("MDROM"), err, sizeof err);
        printf("mdgfx capture: %s (%.2fs)\n", ok ? "ok" : err, (double)(clock() - c0) / CLOCKS_PER_SEC);
        if (ok && getenv("MDGFXDUMP")) for (int i = 0; i < MDG_NIMG; i++) {
            char p[64]; snprintf(p, sizeof p, "mdg_%d.ppm", i); FILE *f = fopen(p, "wb"); fprintf(f, "P6\n320 224\n255\n");
            for (int k = 0; k < 320 * 224; k++) { uint8_t c[3] = { mdgfx->img[i][k] >> 16, mdgfx->img[i][k] >> 8, mdgfx->img[i][k] }; fwrite(c, 1, 3, f); }
            fclose(f); }
        if (ok && getenv("MDGFXDUMP")) for (int r = 0; r < MD_NRACES; r++) if (mdscene[r] && mdscene[r]->ok) {
            mdscene_t *sc = mdscene[r]; char p[64]; snprintf(p, sizeof p, "scene_%d.ppm", r); FILE *f = fopen(p, "wb");
            fprintf(f, "P6\n%d %d\n255\n", sc->w, sc->h + 20);
            for (int y = 0; y < sc->h + 20; y++) for (int x = 0; x < sc->w; x++) {
                uint32_t c = y < sc->h ? sc->strip[y][x] : sc->road[(y - sc->h) / 10][x * 5 / sc->w];
                uint8_t b[3] = { c >> 16, c >> 8, c }; fwrite(b, 1, 3, f); }
            fclose(f); printf("scene %d: %dx%d, %d objects\n", r, sc->w, sc->h, sc->nobj);
            for (int o = 0; o < sc->nobj; o++) { snprintf(p, sizeof p, "obj_%d_%02x.ppm", r, sc->obj[o].kind); FILE *g = fopen(p, "wb");
                fprintf(g, "P6\n%d %d\n255\n", sc->obj[o].w, sc->obj[o].h);
                for (int k = 0; k < sc->obj[o].w * sc->obj[o].h; k++) { uint32_t c = sc->obj[o].pix[k]; uint8_t bb[3] = { c >> 24 ? c >> 16 : 255, c >> 24 ? c >> 8 : 0, c >> 24 ? c : 255 }; fwrite(bb, 1, 3, g); }
                fclose(g); } }
    }
    sho_reset();
    if (getenv("NVFILE")) { FILE *nf = fopen(getenv("NVFILE"), "rb"); if (nf) { size_t n = fread(M->workram, 1, 0x8000, nf); (void)n; fclose(nf); }
                            if (getenv("NVCLEAR")) { M->workram[0x6052] = 0; M->workram[0x6053] = 0; } }
    if (getenv("NVCREDITS")) M->workram[0x6052] = (uint8_t)atoi(getenv("NVCREDITS"));
    if (getenv("NVPLAYING")) { M->workram[0x408] = M->workram[0x409] = M->workram[0x40a] = M->workram[0x40b] = 0xff; }
    if (getenv("DEVRACE")) { extern void sho_md_dev_race(int); sho_md_dev_race(atoi(getenv("DEVRACE"))); }
    if (getenv("ROMPEEK")) { unsigned a_ = (unsigned)strtoul(getenv("ROMPEEK"), 0, 16); printf("ROM %06x:", a_); for (int j = 0; j < 32; j++) printf(" %02x", M->mrom[a_ + j]); printf("\n"); }
    if (getenv("DESCDUMP")) for (int i = 0; i < 24; i++) { uint32_t d = (M->mrom[0x8aec+i*4]<<24)|(M->mrom[0x8aec+i*4+1]<<16)|(M->mrom[0x8aec+i*4+2]<<8)|M->mrom[0x8aec+i*4+3];
        printf("DESC %2d @%06x:", i, d); for (int j = 0; j < 0x24; j += 2) printf(" %04x", be16r(M->mrom, d + j)); printf("\n"); }
    if (getenv("WIDE")) sho_set_widescreen(1);
    if (getenv("VIEWW")) sho_set_view_width(atoi(getenv("VIEWW")));   /* any view width, e.g. 368 or 380 (MacBook screens) */
    if (getenv("MDMUSIC")) { sho_md_set_music(1); printf("md music: %s\n", sho_md_music_active() ? "on" : "off (no ROM)"); if (sho_md_music_active()) sho_silence_music(); }
    if (getenv("MDSNDLOG")) { extern int mdsnd_log; mdsnd_log = atoi(getenv("MDSNDLOG")); }
    if (getenv("MD")) { sho_md_configure(strchr(getenv("MD"), 'e') != NULL, strchr(getenv("MD"), 'c') != NULL);
                        sho_md_set_save_path(getenv("MDSAVE") ? getenv("MDSAVE") : "md_test.sav");
                        { extern int md_test_direct_steer; md_test_direct_steer = getenv("MDDIRECT") != NULL; } }
    FILE *hashlog = getenv("HASHLOG") ? fopen(getenv("HASHLOG"), "w") : NULL;
    if (getenv("DSW")) sho_set_dips(0xff, (uint8_t)strtol(getenv("DSW"), 0, 16));
    int freeplay = getenv("FREEPLAY") != NULL; if (freeplay) sho_set_dips(0x00, 0xfe);
    { extern int sho_debug_mix; if (getenv("MIX")) sho_debug_mix = atoi(getenv("MIX")); }
    FILE *wav = fopen("out.wav", "wb");
    long total = 0; int16_t buf[2 * 2048];
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
    double acc = 0; int peak = 0; long clipped = 0;
    clock_t t0 = clock();
    for (int f = 0; f < frames; f++) {
        sho_input in = { 0 };
        int c = f % 4000;
        in.coin = !freeplay && !getenv("NOSTART") && (c >= 600 && c < 606);
        in.start = !getenv("NOSTART") && (c >= 700 && c < 706);
        in.throttle = f >= (getenv("THROTTLE_FROM") ? atoi(getenv("THROTTLE_FROM")) : 760) ? 1.0f : 0.0f;
        if (getenv("CLASS")) { int n = atoi(getenv("CLASS")), k = c - 610; if (k >= 0 && k < n * 20) in.brake = (k % 20) < 10; }
        if (getenv("MUSIC_SELECT")) {
            int n = atoi(getenv("MUSIC_SELECT")) & 3, k = c - 800;
            if (k >= 0 && k < n * 20) in.brake = (k % 20) < 10;
            if (c >= 900 && c < 906) in.start = 1;
        }
        if (!strcmp(steer_mode, "weave")) in.steer = 0.6f * sinf(f / 45.0f);
        else if (!strcmp(steer_mode, "auto")) { in.steer = f >= 1500 ? autopilot() : 0; in.turbo = ((f / 300) % 4) == 1; }
        else if (!strcmp(steer_mode, "right")) in.steer = (f > 1900 && f < 1960) ? 1.0f : 0.0f;
        else if (!strcmp(steer_mode, "left")) in.steer = (f > 1900 && f < 1960) ? -1.0f : 0.0f;
        if (getenv("ENTRYTEST") && f >= atoi(getenv("ENTRYAT"))) {   /* scripted d-pad/A presses: U, D, A */
            static int idx = 0, phase = 0, timer = 0; const char *seq = getenv("ENTRYTEST");
            in.throttle = 0; in.brake = 0; in.steer = 0; in.start = 0;
            if (f == atoi(getenv("ENTRYAT"))) printf("entry start: cursor index %d\n", be16r(M->workram, 0x65c2));
            while (seq[idx] == ' ') idx++;
            if (seq[idx]) {
                if (phase == 0) { char ch = seq[idx]; in.nav_up = ch == 'U'; in.nav_down = ch == 'D'; in.nav_left = ch == 'L'; in.nav_right = ch == 'R'; in.pad_a = ch == 'A'; if (ch == 'T') in.throttle = 1.0f;
                                  if (++timer >= 3) { phase = 1; timer = 0; } }
                else if (++timer >= 5) { phase = 0; timer = 0; idx++; }
            }
        }
        /* lockstep runs: no throttle outside races, so the recompiled build's gamepad shortcuts on the
           select screens (right trigger = Start) can't make it differ from the interpreter by design */
        if (getenv("SAFEMENUS") && be16r(M->subram, 0) != 2) in.throttle = 0;
        if (getenv("MDKEYS")) {   /* "frame:keys,..." keys U D L R A B S C held for 4 frames */
            const char *p = getenv("MDKEYS");
            if (getenv("MDNOAUTO")) { } else { in.coin = 0; in.start = 0; }
            while (p && *p) { long fr = strtol(p, (char **)&p, 10); if (*p != ':') break; p++;
                if (f >= fr && f < fr + 4) for (const char *q = p; *q && *q != ','; q++) switch (*q) {
                    case 'U': in.nav_up = 1; break; case 'D': in.nav_down = 1; break; case 'L': in.nav_left = 1; break; case 'R': in.nav_right = 1; break;
                    case 'A': in.pad_a = 1; break; case 'B': in.pad_b = 1; break; case 'S': in.start = 1; break; case 'C': in.coin = 1; break; }
                p = strchr(p, ','); if (p) p++; }
        }
        if (getenv("HOLDTIME") && be16r(M->subram, 0) == 2) M->workram[0x500] = 0x60;
        if (getenv("MDAUTO")) {   /* Original Mode loop driven by the Mega Drive's state: race, result, menu, again */
            extern unsigned mdlive_state(void); extern int md_test_phase(void); extern void md_test_result(int);
            static int st_prev = -1, t_in = 0, races = 0, ph_t = 0; int s_ = (int)mdlive_state(), ph = md_test_phase();
            in.coin = 0; in.start = 0;
            if (f == 400) in.start = 1; if (f == 440) in.nav_down = 1; if (f == 470 || f == 540) in.pad_a = 1;   /* (ARCADE MODE is the default: down to ORIGINAL MODE) */
            if (s_ != st_prev) { t_in = 0; st_prev = s_; } else t_in++;
            if (ph == 11 && s_ == 0x25 && races < atoi(getenv("MDAUTO"))) { if (t_in >= 90 && t_in < 94) in.nav_down = 1; if (t_in >= 120 && t_in < 124) in.nav_down = 1; if (t_in >= 150 && t_in < 154) in.pad_a = 1; }
            if (ph == 11 && (s_ == 0x0d || s_ == 0x0c) && t_in >= 60 && t_in < 64) in.pad_a = 1;
            if (getenv("AUTODBG") && (in.nav_down || in.pad_a)) printf("AUTOKEY f%d s %02x t %d d%d a%d ph %d\n", f, s_, t_in, in.nav_down, in.pad_a, ph);
            if ((ph == 8 || ph == 9) && !getenv("MDAUTORACE")) { if (++ph_t == 500) { printf("AUTO f%d result (race %d)\n", f, races + 1); md_test_result(1); races++; ph_t = 0; } } else ph_t = 0;
        }
        if (getenv("FINISHTEST")) { int ft = atoi(getenv("FINISHTEST")); extern void md_test_finish(void);
            { extern void md_test_rival_sample(void); if (f >= ft - 150 && f < ft) md_test_rival_sample(); }
            if (f == ft) md_test_finish();
            static const int ks[] = { 1, 10, 25, 45, 65, 85, 110, 140, 165 };
            for (int q = 0; q < 9; q++) if (f == ft + ks[q] + 1) { char p3[64]; snprintf(p3, sizeof p3, "fin_%02d.ppm", ks[q]); write_ppm(p3); } }
        if (getenv("POSDBG") && f % 10 == 0) printf("POS f%d st=%d pos=%04x spd=%d state=%d\n", f, be16r(M->workram,0x438), be16r(M->subram,0x30), be16r(M->subram,0x78), be16r(M->subram,0));
        if (getenv("MDINST") && f == 100) { extern int mdcourse_install(int); printf("INST %d\n", mdcourse_install(atoi(getenv("MDINST")))); }
        if (getenv("STDBG")) { static int lst = -1, lpos = 0; int st_ = be16r(M->workram,0x438), p_ = be16r(M->subram,0x30);
            if (st_ != lst) { printf("ST f%d %d->%d lastpos=%04x newpos=%04x\n", f, lst, st_, lpos, p_); lst = st_; } lpos = p_; }
        if (getenv("SUBDUMP") && f % 30 == 0 && be16r(M->subram, 0) == 2) { printf("SUB f%d st=%d:", f, be16r(M->workram,0x438)); for (int i = 0; i < 0x200; i += 2) printf(" %04x", be16r(M->subram, i)); printf("\n"); }
        if (getenv("POS90") && be16r(M->subram, 0) == 2) { int a_ = atoi(getenv("POS90")), step_ = atoi(getenv("POS90STEP")), every_ = atoi(getenv("POS90EVERY")); int ff_ = a_ + ((f - 1700) / every_) * step_;
            if (f >= 1700 && (f - 1700) % every_ == 0) { be16w(M->subram, 0x90, (uint16_t)ff_); be16w(M->subram, 0x30, (uint16_t)(ff_ / 13)); } }
        if (getenv("SPRLOG") && f >= atoi(getenv("SPRLOG")) && f < atoi(getenv("SPRLOG")) + 400) { for (int i = 0; i < 0x1000 / 2; i += 8) { unsigned b = i * 2;
            uint16_t d0 = be16r(M->sprram, b), d1 = be16r(M->sprram, b + 2), d2 = be16r(M->sprram, b + 4), d3 = be16r(M->sprram, b + 6), d4 = be16r(M->sprram, b + 8), d5 = be16r(M->sprram, b + 10);
            if (d2 & 0x8000) break; if ((d2 & 0x4000) || (d0 >> 8) <= (d0 & 0xff)) continue;
            printf("SPR f%d i%d top %d bot %d x %d d2 %04x addr %04x d4 %04x d5 %04x\n", f, i / 8, d0 & 0xff, d0 >> 8, d1 & 0xfff, d2, d3, d4, d5); } }
        if (getenv("MDRESULT") && f == atoi(getenv("MDRESULT"))) { extern void md_test_result(int); md_test_result(getenv("MDWIN") != NULL); }
        if (getenv("SUBLOG") && f % atoi(getenv("SUBLOG")) == 0) { printf("SUB f%d", f); for (int a = 0x60; a < 0xc0; a += 2) printf(" %02x:%04x", a, be16r(M->subram, a)); printf("\n"); }
        if (getenv("POKESUB")) { unsigned a_, v_; if (sscanf(getenv("POKESUB"), "%x:%x", &a_, &v_) == 2) be16w(M->subram, a_, (uint16_t)v_); }
        if (getenv("SUBSET")) { int f_; unsigned a_, v_; for (const char *q = getenv("SUBSET"); q && sscanf(q, "%d@%x:%x", &f_, &a_, &v_) == 3; q = strchr(q, ','), q = q ? q + 1 : NULL) if (f == f_) be16w(M->subram, a_, (uint16_t)v_); }
        if (getenv("GOD") && be16r(M->subram, 0) == 2) be16w(M->subram, 0x22, 0);   /* test: keep the bike on the road centre */
        if (getenv("WATCHSUB")) { static uint16_t prev_[64]; int n_ = 0; unsigned a_[16]; for (const char *q = getenv("WATCHSUB"); q && *q && n_ < 16; ) { a_[n_++] = (unsigned)strtoul(q, (char **)&q, 16); if (*q == ',') q++; else break; }
            for (int i = 0; i < n_; i++) { uint16_t v_ = be16r(M->subram, a_[i]); if (v_ != prev_[i]) { printf("WS f%d %02x %04x -> %04x pos %04x\n", f, a_[i], prev_[i], v_, be16r(M->subram, 0x30)); prev_[i] = v_; } } }
        if (getenv("TXTROW")) { int r_, f_; if (sscanf(getenv("TXTROW"), "%d:%d", &r_, &f_) == 2 && f == f_) { printf("TXT row %d:", r_); for (int c = 24; c < 64; c++) printf(" %04x", be16r(M->textram, (r_ * 64 + c) * 2)); printf("\n"); } }
        if (getenv("CHEAT") && f == atoi(getenv("CHEAT"))) { extern void sho_md_cheat_upgrade(void); sho_md_cheat_upgrade(); }
        if (getenv("CHEATM") && f == atoi(getenv("CHEATM"))) { extern void sho_md_cheat_money(void); sho_md_cheat_money(); }
        if (getenv("POSLOG") && f % atoi(getenv("POSLOG")) == 0) printf("POS f%d pos %04x%04x spd %d stage %d seg %d ea %04x ec %04x lat %d\n", f, be16r(M->subram, 0x30), be16r(M->subram, 0x32), be16r(M->subram, 0x78), be16r(M->workram, 0x438), be16r(M->subram, 0x10), be16r(M->subram, 0xea), be16r(M->subram, 0xec), (int16_t)be16r(M->subram, 0x22));
        if (getenv("STAGEJUMP")) { char b_[64]; snprintf(b_, sizeof b_, ",%s,", getenv("STAGEJUMP")); char k_[16]; snprintf(k_, sizeof k_, ",%d,", f); if (strstr(b_, k_)) be16w(M->workram, 0x438, be16r(M->workram, 0x438) + 1); }   /* test: pass a lap/checkpoint */
        if (getenv("POSJUMP")) { int pf_, pv_; for (const char *q = getenv("POSJUMP"); q && sscanf(q, "%d:%x", &pf_, &pv_) == 2; q = strchr(q, ','), q = q ? q + 1 : NULL)
                                     if (f == pf_) be16w(M->subram, 0x30, (uint16_t)pv_); }   /* test: move the bike along the course */
        if (getenv("OBJDBG") && f == atoi(getenv("OBJDBG"))) { extern unsigned char md_obj_kind[256]; extern int md_obj_active;
            printf("active %d kinds:", md_obj_active); for (int i = 0; i < 256; i++) if (md_obj_kind[i]) printf(" %02x->%02x", i, md_obj_kind[i] - 1); printf("\nsprites:");
            for (int i = 0; i < 0x800; i += 8) { uint16_t d2 = be16r(M->sprram, i * 2 + 4), d4 = be16r(M->sprram, i * 2 + 8); if (d2 & 0x8000) break; if (!(d2 & 0x4000)) printf(" %02x", d4 & 0xff); } printf("\n"); }
        if (getenv("SPRDUMP") && f == atoi(getenv("SPRDUMP"))) { for (int i = 0; i < 0x800; i += 8) { uint16_t d0 = be16r(M->sprram, i * 2), d1 = be16r(M->sprram, i * 2 + 2), d2 = be16r(M->sprram, i * 2 + 4), d3 = be16r(M->sprram, i * 2 + 6), d4 = be16r(M->sprram, i * 2 + 8), d5 = be16r(M->sprram, i * 2 + 10);
            if (d2 & 0x8000) break; if (d2 & 0x4000) continue; printf("spr %3d top %3d bot %3d x %4d d2 %04x addr %04x d4 %04x d5 %04x\n", i / 8, d0 & 0xff, d0 >> 8, d1 & 0xfff, d2, d3, d4, d5); } }
        if (getenv("HOLDCOIN") && f >= atoi(getenv("HOLDCOIN")) && f < atoi(getenv("HOLDCOIN")) + 100) in.coin = 1;
        if (getenv("PAUSEAT") && f >= atoi(getenv("PAUSEAT"))) { static int pt, done_; if (done_) goto run;
            if (f == atoi(getenv("PAUSEAT"))) printf("pause begin %d\n", sho_pause_begin());
            int act = sho_pause_update(&in, pt++); int m, e; sho_get_sound_levels(&m, &e);
            if (act) { printf("f%d pause action %d levels %d %d\n", f, act, m, e); sho_pause_end(); done_ = 1; }
            if (getenv("SHOTS") && f % atoi(getenv("SHOTS")) == 0) { char p2[64]; snprintf(p2, sizeof p2, "shot_%05d.ppm", f); write_ppm(p2); } continue; }
        run:
        sho_set_input(&in);
        if (getenv("FREEZE") && be16r(M->subram, 0) == 2) M->workram[0x500] = 0x45;
        sho_run_frame();
        { int rq = sho_md_reset_requested(); if (rq) { sho_md_before_reset(); sho_reset(); M->workram[0x6052] = M->workram[0x6053] = 0; if (rq == 1 && getenv("MD")) sho_md_configure(strchr(getenv("MD"), 'e') != NULL, strchr(getenv("MD"), 'c') != NULL); if (getenv("LIVEDBG")) printf("f%d reset request %d\n", f, rq); } }
        if (getenv("STEERTRACE") && f >= 1890 && f < 2060) { extern int sho_debug_adc; printf("ST f%d steer68 %d adc %02x\n", f, (int16_t)be16r(M->workram, 0x68), sho_debug_adc); } /* adc byte logged below */
        if (getenv("PHDBG")) { extern int md_test_phase(void); static int lp = -1; int p_ = md_test_phase(); if (p_ != lp) { printf("PH f%d %d\n", f, p_); lp = p_; } { extern unsigned mdlive_state(void); static int ls = -1; int s_ = mdlive_state(); if (s_ != ls) { printf("MDST f%d %02x\n", f, s_); ls = s_; } } }
        if (getenv("LIVETRACE") && f % 50 == 0) printf("f%d st=%d stage=%d pos=%04x\n", f, be16r(M->subram, 0), be16r(M->workram, 0x438), be16r(M->subram, 0x30));
        if (getenv("PLAYTRACE") && f % 60 == 0) printf("f%d st=%d playing=%04x\n", f, be16r(M->subram, 0), be16r(M->workram, 0x408));
        if (getenv("SPDTRACE") && f % 30 == 0) printf("f%d st=%d spd=%d.%04x adc=%d turbo=%d\n", f, be16r(M->subram, 0), be16r(M->subram, 0x78), be16r(M->subram, 0x7a), be16r(M->subram, 0x6c), in.turbo);
        if (getenv("MENUTRACE") && (f % 20 == 0) && f >= 600 && f < 1500)
            printf("f%d class=%d music=%d musiccode=%02x\n", f, be16r(M->subram, 0x12), be16r(M->workram, 0x41a), M->workram[0x41c]);
        if (getenv("ENTRYTRACE") && f >= atoi(getenv("ENTRYAT")) - 40 && f < atoi(getenv("ENTRYAT")) + 400) {
            extern long sho_entry_last_frame, sho_frame_no;
            printf("f%d act=%d pos=%d cur=%d rpt=%d press=%04x prevbrake=%d in:%d%d%d\n", f, sho_frame_no - sho_entry_last_frame <= 1,
                be16r(M->workram, 0x65c0), be16r(M->workram, 0x65c2), be16r(M->workram, 0x65c4), be16r(M->workram, 0x65c6), be16r(M->workram, 0x65c8), in.nav_up, in.nav_down, in.pad_a); }
        if (getenv("TEXTDUMP") && f == atoi(getenv("TEXTDUMP"))) {   /* text layer rows 0-27, cols 24-63: tile code (hex) */
            for (int r = 0; r < 28; r++) { printf("r%02d ", r); for (int cc = 24; cc < 64; cc++) { uint16_t d = be16r(M->textram, (r * 64 + cc) * 2); printf("%03X ", d & 0x8fff); } printf("\n"); } }
        if (getenv("TEXTSURVEY") && f % 15 == 0) { static FILE *ts; if (!ts) ts = fopen("textsurvey.txt", "w");
            fprintf(ts, "F %d\n", f);
            for (int r = 0; r < 28; r++) for (int cc = 0; cc < 64; cc++) { uint16_t d = be16r(M->textram, (r * 64 + cc) * 2); if (d & 0xfff) fprintf(ts, "%d %d %03X\n", r, cc, d & 0x8fff); } }
        if (getenv("SCROLLLOG")) { static FILE *sl; if (!sl) sl = fopen("scroll.txt", "w");
            fprintf(sl, "%d %d %d %d %d\n", f, be16r(M->textram, 0xe98), be16r(M->textram, 0xe9a), be16r(M->textram, 0xe80), be16r(M->textram, 0xe82)); }
        if (getenv("TILELOG")) { extern unsigned long sho_debug_tile_writes, sho_debug_last_tile; static unsigned long last; if (sho_debug_tile_writes != last) printf("frame %d tile writes %lu (last addr %06lX)\n", f, sho_debug_tile_writes - last, sho_debug_last_tile); last = sho_debug_tile_writes; }
        if (getenv("RAMDUMP") && f % atoi(getenv("RAMDUMP")) == 0) { char rp[64]; snprintf(rp, sizeof rp, "ram_%05d.bin", f); FILE *rf = fopen(rp, "wb"); fwrite(M->workram, 1, 0x8000, rf); fwrite(M->subram, 1, 0x8000, rf); fclose(rf); }
        if (getenv("PALDUMP") && f == atoi(getenv("PALDUMP"))) { for (int i = 0; i < 64; i++) printf("pal %02x %04x\n", i, be16r(M->palram, i * 2)); }
        if (hashlog) { extern uint64_t sho_state_hash(void); fprintf(hashlog, "%d %016llx\n", f, (unsigned long long)sho_state_hash()); }
        acc += 48000.0 / SHO_FPS; int n = (int)acc; acc -= n;
        sho_audio(buf, n);
        for (int i = 0; i < n * 2; i++) { int a = abs(buf[i]); if (a > peak) peak = a; if (a >= 32767) clipped++; }
        fwrite(buf, 4, n, wav); total += n;
        char p[64];
        if (getenv("VOICES") && f == atoi(getenv("VOICES"))) { extern void sho_debug_voices(void); sho_debug_voices(); }
        if (getenv("SHOTS") && f % atoi(getenv("SHOTS")) == 0) { snprintf(p, sizeof p, "shot_%05d.ppm", f); write_ppm(p); }
        if (getenv("FINDUMP")) { extern int md_finish_t(void); int ft = md_finish_t(); static const int ks[] = { 1, 3, 6, 10, 15, 22, 30, 40, 50 };
            for (int q = 0; q < 9; q++) if (ft == ks[q]) { char p3[64]; snprintf(p3, sizeof p3, "fin_%02d.ppm", ks[q]); write_ppm(p3); } }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    uint32_t ds = total * 4, rs = ds + 36;
    fseek(wav, 4, SEEK_SET); fwrite(&rs, 4, 1, wav); fseek(wav, 40, SEEK_SET); fwrite(&ds, 4, 1, wav); fclose(wav);
    write_ppm("last.ppm");
    if (hashlog) fclose(hashlog);
    { extern int sho_recomp; extern unsigned long rc_stats_interp, rc_stats_dispatch, rc_stats_unwind;
      extern uint32_t rc_mm_exp[64], rc_mm_got[64]; extern unsigned long rc_mm_n[64];
      if (sho_recomp && getenv("MMDUMP")) for (int i = 0; i < 64; i++) if (rc_mm_n[i]) printf("  mismatch cpu%u expected %06X got %06X x%lu\n", rc_mm_exp[i] >> 31, rc_mm_exp[i] & 0xffffff, rc_mm_got[i], rc_mm_n[i]);
      if (sho_recomp) printf("recomp: interpreted instrs=%lu dispatches=%lu unwinds=%lu\n", rc_stats_interp, rc_stats_dispatch, rc_stats_unwind); }
    printf("frames=%d emu_time=%.2fs (%.1fx realtime) audio_samples=%ld peak=%d clipped=%ld\n",
           frames, secs, frames / SHO_FPS / secs, total, peak, clipped);
    { extern unsigned long sho_debug_latch_writes, sho_debug_opm_writes, sho_debug_pcm_writes;
      printf("latch writes=%lu opm writes=%lu pcm writes=%lu\n", sho_debug_latch_writes, sho_debug_opm_writes, sho_debug_pcm_writes); }
    sho_music_unload();
    return 0;
}
