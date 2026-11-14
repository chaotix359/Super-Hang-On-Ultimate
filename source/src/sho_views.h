/* The display shapes: F9 steps through them (Linux and Mac have "fit the window/screen" in front of them),
 * the launcher's Display line and the pause menu's DISPLAY line show them. Width = the game picture's width
 * at the arcade monitor's pixel shape (320 pixels are 4:3; 16:9 keeps its long-standing 428). "21:9" is the
 * shape of 21:9 monitors (2560x1080, 3440x1440: 64:27), so they are filled edge to edge. */
#ifndef SHO_VIEWS_H
#define SHO_VIEWS_H
typedef struct { const char *name, *long_name; double aspect; int width; } sho_shape;
static const sho_shape sho_shapes[] = {      /* in F9 order */
    { "16:9", "16:9 WIDESCREEN", 16.0 / 9.0, 428 },
    { "18:9", "18:9 ULTRAWIDE",  18.0 / 9.0, 480 },
    { "19:9", "19:9 ULTRAWIDE",  19.0 / 9.0, 506 },
    { "20:9", "20:9 ULTRAWIDE",  20.0 / 9.0, 534 },
    { "21:9", "21:9 ULTRAWIDE",  64.0 / 27.0, 568 },
    { "4:3",  "4:3 ORIGINAL",    4.0 / 3.0, 320 },
};
#define SHO_NSHAPES 6
#define SHO_SHAPE_43 5
/* a shape by name ("16:9", "21:9", "4:3", ...), or -1 */
static inline int sho_shape_find(const char *s) {
    for (int i = 0; i < SHO_NSHAPES; i++) { const char *a = sho_shapes[i].name, *b = s; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return i; }
    return -1;
}
#endif
