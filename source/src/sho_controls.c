/* The player's controls (see sho_controls.h): button and key assignments, their defaults and ini text, and turning a
 * pad's state into the game's input. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sho_controls.h"

sho_controls sho_ctl = { .deadzone = 0.15f, .curve = 1.0f };

static const char *const button_names[PB_COUNT] = { "A", "B", "X", "Y", "LEFT BUMPER", "RIGHT BUMPER", "LEFT TRIGGER", "RIGHT TRIGGER",
                                                    "BACK", "START", "LEFT STICK CLICK", "RIGHT STICK CLICK", "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT" };
static const char *const action_names[ACT_COUNT] = { "-", "ACCELERATE", "BRAKE", "TURBO", "STEER LEFT", "STEER RIGHT", "START", "COIN" };
static const char *const key_action_names[KA_COUNT] = { "STEER LEFT", "STEER RIGHT", "ACCELERATE", "BRAKE", "TURBO", "START", "COIN", "PAUSE" };
const char *sho_button_name(int b) { return b >= 0 && b < PB_COUNT ? button_names[b] : "?"; }
const char *sho_action_name(int a) { return a >= 0 && a < ACT_COUNT ? action_names[a] : "?"; }
const char *sho_key_action_name(int k) { return k >= 0 && k < KA_COUNT ? key_action_names[k] : "?"; }

void sho_controls_pad_defaults(int swap_pedals) {
    static const unsigned char def[PB_COUNT] = {
        [PB_A] = ACT_ACCEL, [PB_B] = ACT_BRAKE, [PB_X] = ACT_BRAKE, [PB_Y] = ACT_TURBO, [PB_LB] = ACT_NONE, [PB_RB] = ACT_TURBO,
        [PB_LT] = ACT_BRAKE, [PB_RT] = ACT_ACCEL, [PB_BACK] = ACT_COIN, [PB_START] = ACT_START, [PB_L3] = ACT_NONE, [PB_R3] = ACT_NONE,
        [PB_UP] = ACT_NONE, [PB_DOWN] = ACT_NONE, [PB_LEFT] = ACT_LEFT, [PB_RIGHT] = ACT_RIGHT };
    memcpy(sho_ctl.pad, def, sizeof def);
    if (swap_pedals) { sho_ctl.pad[PB_LT] = ACT_ACCEL; sho_ctl.pad[PB_RT] = ACT_BRAKE; }
}
static int key_def[KA_COUNT][KEY_SLOTS];
void sho_controls_key_defaults(const int def[KA_COUNT][KEY_SLOTS]) { memcpy(key_def, def, sizeof key_def); sho_controls_key_reset(); }
void sho_controls_key_reset(void) { memcpy(sho_ctl.key, key_def, sizeof key_def); }

/* [pad] actions=1,2,2,3,0,3,2,1,7,6,0,0,0,0,4,5 (A B X Y LB RB LT RT Back Start L3 R3 Up Down Left Right) */
void sho_controls_pad_to_str(char *out, int n) {
    int k = 0; out[0] = 0;
    for (int b = 0; b < PB_COUNT && k < n - 4; b++) k += snprintf(out + k, (size_t)(n - k), b ? ",%d" : "%d", sho_ctl.pad[b]);
}
int sho_controls_pad_from_str(const char *s) {
    unsigned char v[PB_COUNT]; int b = 0;
    while (*s && b < PB_COUNT) {
        char *e; long a = strtol(s, &e, 10);
        if (e == s || a < 0 || a >= ACT_COUNT) return 0;
        v[b++] = (unsigned char)a; s = e;
        while (*s == ',' || *s == ' ') s++;
    }
    if (b != PB_COUNT) return 0;
    memcpy(sho_ctl.pad, v, sizeof v);
    return 1;
}
/* [keys_x] keys=37,65,-1,-1;39,68,-1,-1;... (steer left; steer right; accelerate; brake; turbo; start; coin; pause) */
void sho_controls_keys_to_str(char *out, int n) {
    int k = 0; out[0] = 0;
    for (int a = 0; a < KA_COUNT && k < n - 52; a++)
        k += snprintf(out + k, (size_t)(n - k), "%s%d,%d,%d,%d", a ? ";" : "", sho_ctl.key[a][0], sho_ctl.key[a][1], sho_ctl.key[a][2], sho_ctl.key[a][3]);
}
int sho_controls_keys_from_str(const char *s) {
    int v[KA_COUNT][KEY_SLOTS], a = 0;
    while (*s && a < KA_COUNT) {
        if (sscanf(s, "%d,%d,%d,%d", &v[a][0], &v[a][1], &v[a][2], &v[a][3]) != 4) return 0;
        a++;
        s = strchr(s, ';'); if (!s) break; s++;
    }
    if (a != KA_COUNT) return 0;
    memcpy(sho_ctl.key, v, sizeof v);
    return 1;
}

static float axis_dz(float v) {
    float a = fabsf(v), dz = sho_ctl.deadzone;
    if (a < dz) return 0.0f;
    a = (a - dz) / (1.0f - dz); if (a > 1) a = 1;
    a = powf(a, sho_ctl.curve);
    return v < 0 ? -a : a;
}
void sho_controls_apply_pad(int slot, const sho_pad *p, int menu, sho_input *in, float *steer, float *thr, float *brk) {
    /* steering: the left stick, and any buttons set to steer (the d-pad's left and right by default) */
    float sx = axis_dz(p->lx);
    /* directions for menus / initials: d-pad or left stick (with hysteresis), always */
    static int stick_dir[8][4];   /* [pad][up,down,left,right] */
    float v[4] = { p->ly, -p->ly, -p->lx, p->lx };
    slot &= 7;
    for (int d = 0; d < 4; d++) stick_dir[slot][d] = stick_dir[slot][d] ? v[d] > 0.35f : v[d] > 0.6f;
    if ((p->buttons & (1u << PB_UP)) || stick_dir[slot][0]) in->nav_up = 1;
    if ((p->buttons & (1u << PB_DOWN)) || stick_dir[slot][1]) in->nav_down = 1;
    if ((p->buttons & (1u << PB_LEFT)) || stick_dir[slot][2]) in->nav_left = 1;
    if ((p->buttons & (1u << PB_RIGHT)) || stick_dir[slot][3]) in->nav_right = 1;
    float t = 0, b = 0;
    for (int k = 0; k < PB_COUNT; k++) {
        int trig = k == PB_LT || k == PB_RT;
        float pull = k == PB_LT ? p->lt : k == PB_RT ? p->rt : 0;
        int down = (p->buttons >> k) & 1;
        if (trig && pull < 0.04f) pull = 0;
        switch (sho_ctl.pad[k]) {
        case ACT_ACCEL: if (trig) { if (pull > t) t = pull; } else if (down) { t = 1; in->pad_a = 1; } break;   /* a button on accelerate also confirms (initials, class and music select) */
        case ACT_BRAKE: if (trig) { if (pull > b) b = pull; } else if (down) b = 1; break;
        case ACT_TURBO: if (down) in->turbo = 1; break;
        case ACT_LEFT:  if (down) sx = -1; break;
        case ACT_RIGHT: if (down) sx = 1; break;
        case ACT_START: if (down) in->start = 1; break;
        case ACT_COIN:  if (down) in->coin = 1; break;
        }
    }
    /* the Original Mode screens and the pause menu: A selects, B goes back (the real A and B, whatever they do in a race) */
    if (menu && (p->buttons & (1u << PB_A))) in->pad_a = 1;
    if ((p->buttons & (1u << PB_B)) && (menu || sho_ctl.pad[PB_B] == ACT_BRAKE || sho_ctl.pad[PB_B] == ACT_NONE)) in->pad_b = 1;
    if (fabsf(sx) > fabsf(*steer)) *steer = sx;
    if (t > *thr) *thr = t;
    if (b > *brk) *brk = b;
}

int sho_key_action_down(int ka, int (*down)(int code)) {
    for (int i = 0; i < KEY_SLOTS; i++) if (sho_ctl.key[ka][i] >= 0 && down(sho_ctl.key[ka][i])) return 1;
    return 0;
}
int sho_key_is_action(int ka, int code) {
    for (int i = 0; i < KEY_SLOTS; i++) if (sho_ctl.key[ka][i] == code) return 1;
    return 0;
}
