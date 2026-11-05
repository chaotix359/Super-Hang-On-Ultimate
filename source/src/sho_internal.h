/* Shared machine state between core and video renderer. */
#ifndef SHO_INTERNAL_H
#define SHO_INTERNAL_H
#include <stdint.h>

typedef struct {
    /* ROM regions */
    uint8_t mrom[0x60000], srom[0x60000];
    uint8_t tiles[0x18000];          /* 3 bitplanes x 0x8000 */
    uint16_t sprites[0x80000];       /* 1MB, big-endian words decoded to native */
    uint8_t road[0x10000];           /* 0x8000 loaded, rest 0 */
    uint8_t zrom[0x10000];           /* sound CPU */
    uint8_t pcm[0x40000];            /* SegaPCM samples */

    /* RAM (stored big-endian byte order, as seen by the 68000) */
    uint8_t workram[0x8000], subram[0x8000];
    uint8_t roadram[0x1000], roadbuf[0x1000];
    uint8_t tileram[0x10000], textram[0x1000], palram[0x2000], sprram[0x1000];
    uint8_t zram[0x800];

    int road_control;
    uint8_t out0, out20;
} sho_machine;

extern sho_machine *M;

void sho_video_init(void);
void sho_video_render(uint32_t *fb);

static inline uint16_t be16r(const uint8_t *p, unsigned a) { return (uint16_t)((p[a] << 8) | p[a + 1]); }
static inline void be16w(uint8_t *p, unsigned a, uint16_t v) { p[a] = (uint8_t)(v >> 8); p[a + 1] = (uint8_t)v; }

#endif
