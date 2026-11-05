/* Metal presentation for the macOS frontend (the Mac twin of win32_d3d11.c). */
#ifndef MAC_METAL_H
#define MAC_METAL_H
#import <QuartzCore/QuartzCore.h>
#include <stdint.h>

/* layer: the game view's CAMetalLayer. max_w x max_h: largest game picture. 1 = ok, 0 = no Metal device */
int mtl_init(CAMetalLayer *layer, int max_w, int max_h);
/* draws fb (src_w x src_h, 0x00RRGGBB) into the rectangle (vx, vy, vw, vh) of the drawable (pixels, top-left
   origin), black around it. filter: 0 sharp, 1 smooth, 2 sharp-bilinear. Returns 1 if a frame was presented. */
int mtl_present(const uint32_t *fb, int src_w, int src_h, int filter, double vx, double vy, double vw, double vh);
#endif
