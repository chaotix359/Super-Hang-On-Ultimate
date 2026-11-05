/* Mega Drive Original Mode courses running in the arcade engine.
 *
 * The Mega Drive game is a port of the arcade program and keeps its course formats:
 *   - road path: per-step heading vectors (sin, cos x 4096). The Mega Drive table ($FAAA, one per stage)
 *     has one entry per 2 arcade steps; the arcade table (sub CPU $19E8) one per step.
 *   - curve stream: 6-byte records (position, curve amount, curve type), identical in both.
 *   - section events: arcade 8-byte records (position, value, 0, 0); Mega Drive 4-byte (position, value).
 * Mega Drive stage descriptors are at $FB62 (curve stream +8, events +$C, objects +$10); the stage id for
 * a class/stage comes from $FBC2. Original Mode races ($8E58: class, stage, extra; laps at $FA92) are one
 * stage driven round several times.
 *
 * Installing a course writes the converted data into unused space in the arcade program ROM images
 * (main and sub $40000-$5FFFF, empty on this board) and points every entry of the arcade's stage
 * descriptor table (main $8AEC) and road path table (sub $19E8) at it. The original bytes are restored
 * afterwards. Objects and colours still come from the arcade stage with the same number for now. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sho_mdcourse.h"
#include "sho_mdgfx.h"
#include "sho_internal.h"

#define FREE_BASE 0x40000
#define FREE_SIZE 0x20000
#define ARC_DESC_TABLE 0x8aec
#define ARC_PATH_TABLE 0x19e8
#define NSTAGES 24
#define ARC_PATH_ENTRIES 2308

static uint8_t save_cnt[4], save_midgate[2], save_banner;   /* $6E43: banner of the gate at a segment end ($2F CHECK); the Mega Drive shows START on each lap line */   /* $6DA6: the arcade's second checkpoint of each $800 segment (position $400) */   /* arcade stage counts per class ($8870): set to the lap count so the arcade's own goal comes on the last lap */
static uint8_t save_main[FREE_SIZE], save_sub[FREE_SIZE], save_desc[NSTAGES * 4], save_path[NSTAGES * 4], save_pat[256 * 4];
#define ARC_PATTERN_TABLE 0x13f04
#define ARC_MIDGATE 0x6da6             /* cmpi.w #$400,D0 at $6DA4: a checkpoint gate at mid-segment (arcade stages are $400 long) */
#define ARC_LAPBANNER 0x6e43           /* move.b #$2f,($16,A0,D0.w) at $6E40: $2E = START, $2F = CHECK, $30 = GOAL */
#define ARC_STAGE_COUNT 0x8870         /* per class: $800-position segments before the goal ($6786) */
#define ARC_OBJ_DEF 0x4754             /* arcade object definition used to carry Mega Drive objects (placement, size, collision) */
int md_obj_active;                     /* read by the M_006F16 override */
unsigned char md_obj_kind[256];        /* sprite palette -> Mega Drive kind + 1 */
static int installed;

static uint32_t rd32(const uint8_t *p, uint32_t a) { return (uint32_t)p[a] << 24 | p[a + 1] << 16 | p[a + 2] << 8 | p[a + 3]; }
static uint16_t rd16(const uint8_t *p, uint32_t a) { return (uint16_t)(p[a] << 8 | p[a + 1]); }
static void wr32(uint8_t *p, uint32_t a, uint32_t v) { p[a] = v >> 24; p[a + 1] = v >> 16; p[a + 2] = v >> 8; p[a + 3] = (uint8_t)v; }
static void wr16(uint8_t *p, uint32_t a, uint16_t v) { p[a] = v >> 8; p[a + 1] = (uint8_t)v; }

static const uint8_t *mdrom(void) { return mdgfx && mdgfx->rom && mdgfx->romsz >= 0x80000 ? mdgfx->rom : NULL; }

int mdcourse_available(void) { return mdrom() != NULL; }
int mdcourse_count(void) { return 7; }

int mdcourse_info(int race, mdcourse_t *out) {
    const uint8_t *r = mdrom(); if (!r || race < 0 || race >= 7) return 0;
    int cls = rd16(r, 0x8e58 + race * 6), stg = rd16(r, 0x8e58 + race * 6 + 2);
    out->race = race; out->md_class = cls; out->md_stage = stg;
    out->stage_id = rd16(r, 0xfbc2 + (cls * 9 + stg) * 2);
    out->laps = r[0xfa92 + race];
    return out->stage_id < NSTAGES;
}

void mdcourse_uninstall(void) {
    if (!installed) return;
    memcpy(M->mrom + FREE_BASE, save_main, FREE_SIZE); memcpy(M->srom + FREE_BASE, save_sub, FREE_SIZE);
    memcpy(M->mrom + ARC_DESC_TABLE, save_desc, sizeof save_desc); memcpy(M->srom + ARC_PATH_TABLE, save_path, sizeof save_path);
    memcpy(M->mrom + ARC_PATTERN_TABLE, save_pat, sizeof save_pat);
    memcpy(M->mrom + ARC_STAGE_COUNT, save_cnt, sizeof save_cnt);
    memcpy(M->mrom + ARC_MIDGATE, save_midgate, 2);
    M->mrom[ARC_LAPBANNER] = save_banner;
    installed = 0; md_obj_active = 0; memset(md_obj_kind, 0, sizeof md_obj_kind);
}

int mdcourse_install(int race) {
    const uint8_t *r = mdrom(); mdcourse_t ci;
    if (!r || !mdcourse_info(race, &ci)) return 0;
    mdcourse_uninstall();
    memcpy(save_main, M->mrom + FREE_BASE, FREE_SIZE); memcpy(save_sub, M->srom + FREE_BASE, FREE_SIZE);
    memcpy(save_desc, M->mrom + ARC_DESC_TABLE, sizeof save_desc); memcpy(save_path, M->srom + ARC_PATH_TABLE, sizeof save_path);
    memcpy(save_pat, M->mrom + ARC_PATTERN_TABLE, sizeof save_pat);
    memcpy(save_cnt, M->mrom + ARC_STAGE_COUNT, sizeof save_cnt);
    for (int c = 0; c < 4; c++) M->mrom[ARC_STAGE_COUNT + c] = (uint8_t)ci.laps;
    memcpy(save_midgate, M->mrom + ARC_MIDGATE, 2);
    M->mrom[ARC_MIDGATE] = 0x7f; M->mrom[ARC_MIDGATE + 1] = 0xff;
    save_banner = M->mrom[ARC_LAPBANNER]; M->mrom[ARC_LAPBANNER] = 0x2e;   /* a Mega Drive lap has one gate, at its end: never reached */
    int s = ci.stage_id;

    /* road path: double the Mega Drive's half-resolution heading table */
    uint32_t mp = rd32(r, 0xfaaa + s * 4), sp = FREE_BASE;
    int mdn = 1154;
    for (int k = 0; k < ARC_PATH_ENTRIES; k++) {
        int i = k / 2, j = i + 1 < mdn ? i + 1 : i;
        double x = (int16_t)rd16(r, mp + i * 4), y = (int16_t)rd16(r, mp + i * 4 + 2);
        if (k & 1) {
            double x2 = (int16_t)rd16(r, mp + j * 4), y2 = (int16_t)rd16(r, mp + j * 4 + 2);
            x = (x + x2) / 2; y = (y + y2) / 2;
            double l = sqrt(x * x + y * y); if (l > 1) { x = x * 4096 / l; y = y * 4096 / l; }
        }
        wr16(M->srom, sp + k * 4, (uint16_t)(int16_t)lrint(x)); wr16(M->srom, sp + k * 4 + 2, (uint16_t)(int16_t)lrint(y));
    }
    wr16(M->srom, sp + ARC_PATH_ENTRIES * 4, 0);

    /* stage descriptor: start from the arcade stage with the same number (colours, objects), then
       replace the curve stream (+$18) and section events (+$1C) with the Mega Drive's */
    uint32_t base = rd32(M->mrom, ARC_DESC_TABLE + s * 4), md = rd32(r, 0xfb62 + s * 4);
    uint32_t desc = FREE_BASE, p = FREE_BASE + 0x40;
    memcpy(M->mrom + desc, M->mrom + base, 0x24);
    uint32_t cs = rd32(r, md + 8), curve = p;
    for (int n = 0; n < 400; n++) {
        memcpy(M->mrom + p, r + cs + n * 6, 6); p += 6;
        if ((int16_t)rd16(r, cs + n * 6) < 0) break;
    }
    uint32_t es = rd32(r, md + 0xc), events = p;
    for (int n = 0; n < 400; n++) {
        uint16_t pos = rd16(r, es + n * 4), val = rd16(r, es + n * 4 + 2);
        wr16(M->mrom, p, pos); wr16(M->mrom, p + 2, val); wr32(M->mrom, p + 4, 0); p += 8;
        if (pos == 0x7fff || (int16_t)pos < 0) break;
    }
    /* roadside objects: Mega Drive stream (pos, count, pattern: words) -> arcade (pos word, count byte, pattern byte);
       each Mega Drive pattern (spacing mask + entries of end flag, kind, x, attribute) becomes an arcade pattern
       (mask + entries of side flag, [kind], x, object definition), the kind riding in the byte the arcade skips */
    uint32_t os = rd32(r, md + 0x10), objects = p; uint8_t used[256] = { 0 };
    for (int n = 0; n < 600; n++) {
        int pos = (int16_t)rd16(r, os + n * 6), cnt = rd16(r, os + n * 6 + 2), pat = rd16(r, os + n * 6 + 4);
        if (pos < 0) { wr16(M->mrom, p, 0xffff); p += 4; break; }
        if (pat > 255) continue;
        wr16(M->mrom, p, (uint16_t)pos); M->mrom[p + 2] = (uint8_t)(cnt > 255 ? 255 : cnt); M->mrom[p + 3] = (uint8_t)pat; p += 4;
        used[pat] = 1;
    }
    for (int pat = 0; pat < 256; pat++) if (used[pat]) {
        uint32_t src = rd32(r, 0x5771c + pat * 4), q = src + 2;
        p = (p + 1) & ~1u;
        wr32(M->mrom, ARC_PATTERN_TABLE + pat * 4, p);
        wr16(M->mrom, p, rd16(r, src)); p += 2;                       /* spacing mask */
        for (int k = 0; k < 16 && r[q] < 0x80; k++, q += 6) {
            M->mrom[p] = (int16_t)rd16(r, q + 2) < 0 ? 1 : 0;           /* side flag as the arcade uses it (left = 1) */
            M->mrom[p + 1] = r[q + 1];                                 /* Mega Drive kind (not read by the arcade) */
            wr16(M->mrom, p + 2, rd16(r, q + 2));                      /* x */
            wr16(M->mrom, p + 4, ARC_OBJ_DEF);
            p += 6;
        }
        M->mrom[p] = 0xff; p += 2;
    }
    wr32(M->mrom, desc + 0x20, objects);
    md_obj_active = 1; memset(md_obj_kind, 0, sizeof md_obj_kind);
    wr32(M->mrom, desc + 0x18, curve); wr32(M->mrom, desc + 0x1c, events);
    for (int i = 0; i < NSTAGES; i++) { wr32(M->mrom, ARC_DESC_TABLE + i * 4, desc); wr32(M->srom, ARC_PATH_TABLE + i * 4, sp); }
    installed = 1;
    return 1;
}
