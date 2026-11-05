#ifndef WIN32_D3D11_H
#define WIN32_D3D11_H
#include <windows.h>
#include <stdint.h>
int d3d_init(HWND hwnd, int max_w, int max_h);       /* 1 = ok, 0 = unavailable (use GDI) */
int d3d_tearing_supported(void);
/* filter: 0 sharp, 1 smooth, 2 sharp-bilinear. returns 1 ok, 2 occluded, 0 failed */
int d3d_present(const uint32_t *fb, int src_w, int src_h, double aspect, int filter, int sync_interval, int allow_tearing);
void d3d_resize(void);
void d3d_shutdown(void);
#endif
