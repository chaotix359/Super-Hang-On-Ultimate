/* Coverage driver: runs the interpreter core with an instruction hook and scripted play,
 * recording executed instruction addresses, opcode counts and indirect-jump targets per CPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sho_core.h"
#include "sho_internal.h"
#include "m68k.h"
int sho_current_cpu(void);
static uint8_t visited[2][0x60000];
static unsigned long long opcount[2][65536];
static uint8_t dyn_target[2][0x60000];
static unsigned prevop[2] = {0, 0};
static unsigned long long total[2];
void cov_hook(unsigned int pc) {
    int c = sho_current_cpu(); pc &= 0xffffff;
    unsigned op = m68k_read_memory_16(pc);
    if (pc < 0x60000) {
        visited[c][pc] = 1;
        unsigned p = prevop[c];
        if ((p & 0xffc0) == 0x4ec0 || (p & 0xffc0) == 0x4e80 || p == 0x4e75 || p == 0x4e73 || p == 0x4e77 || (p & 0xfff0) == 0x4e40)
            dyn_target[c][pc] = 1;
    }
    opcount[c][op]++; total[c]++;
    prevop[c] = op;
}
static int autop_centre = -1;
static float autopilot(int f) {
    int y = 190; uint16_t d0 = be16r(M->roadbuf, y * 2);
    int h = be16r(M->roadbuf, (0x200 + (d0 & 0x1ff)) * 2) & 0xfff;
    if (autop_centre < 0) return 0;
    int err = ((h - autop_centre + 0x800) & 0xfff) - 0x800;
    float v = -1.5f * err / 96.0f;   /* adc = 0x80 - steer*0x60 ; harness used adc = 0x80 + 1.5*err */
    if (v > 1) v = 1; if (v < -1) v = -1; return v;
}
int main(int argc, char **argv) {
    const char *rom = argv[1]; const char *mode = argv[2]; int frames = atoi(argv[3]); const char *out = argv[4];
    char err[2048];
    if (sho_load_roms(rom, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    FILE *f = fopen(out, "rb");
    if (f) { fread(visited, 1, sizeof visited, f); fread(dyn_target, 1, sizeof dyn_target, f); fread(opcount, 1, sizeof opcount, f); fclose(f); }
    sho_reset();
    int freeplay = strstr(mode, "free") != NULL;
    if (freeplay) sho_set_dips(0x00, 0xfe);
    for (int fr = 0; fr < frames; fr++) {
        sho_input in = {0};
        int c = fr % 5000;
        if (strstr(mode, "test")) {           /* service menu: hold test at boot, then poke start/brake/throttle */
            in.test = (fr > 300 && fr < 320) || (fr % 600 > 590);
            in.start = (fr % 97) < 3; in.brake = ((fr / 50) % 3 == 0); in.throttle = ((fr / 70) % 2);
            in.steer = sinf(fr / 30.0f);
        } else {
            if (!freeplay) in.coin = (c >= 600 && c < 606);
            in.start = (c >= 700 && c < 706) || (strstr(mode, "cont") && (fr % 211) < 3);
            /* class/music selection: tap brake a number of times depending on cycle */
            int sel = (fr / 5000) % 4;
            if (c >= 620 && c < 700) in.brake = ((c - 620) / 10) % 2 && ((c - 620) / 20) < sel;
            in.throttle = c >= 760 ? 1.0f : 0.0f;
            in.turbo = ((fr / 300) % 4) == 1;
            if (strstr(mode, "auto")) { if (fr == 1500) autop_centre = -2; in.steer = autopilot(fr); }
            else in.steer = 0.6f * sinf(fr / 45.0f);
            if (strstr(mode, "brake") && (fr % 400) < 60) { in.throttle = 0; in.brake = 1; }
        }
        sho_set_input(&in);
        sho_run_frame();
        if (autop_centre == -2) { uint16_t d0 = be16r(M->roadbuf, 190 * 2); autop_centre = be16r(M->roadbuf, (0x200 + (d0 & 0x1ff)) * 2) & 0xfff; }
    }
    f = fopen(out, "wb"); fwrite(visited, 1, sizeof visited, f); fwrite(dyn_target, 1, sizeof dyn_target, f); fwrite(opcount, 1, sizeof opcount, f); fclose(f);
    long v0 = 0, v1 = 0; for (int i = 0; i < 0x60000; i++) { v0 += visited[0][i]; v1 += visited[1][i]; }
    printf("%s: main instr addrs %ld, sub %ld\n", mode, v0, v1);
    return 0;
}
