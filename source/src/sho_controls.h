/* The player's controls, shared by the three frontends and the launcher's CONTROLS screen.
 *
 * A controller is read by its frontend into an Xbox-style pad state (sho_pad): the sixteen buttons of the launcher's
 * diagram, the left stick and the two triggers. Each button has one game action (sho_ctl.pad[]); the left stick
 * always steers, and the d-pad and left stick always move through menus. The keyboard has up to four keys for
 * each action (sho_ctl.key[][], in the frontend's own key codes).
 *
 * shangon.ini: [pad] actions= and device=, [keys_<system>] keys= (frontends save and load the strings below).
 */
#ifndef SHO_CONTROLS_H
#define SHO_CONTROLS_H
#include "sho_core.h"

/* the buttons of an Xbox-style pad */
enum { PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT, PB_BACK, PB_START, PB_L3, PB_R3, PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT, PB_COUNT };
typedef struct {
    float lx, ly;        /* left stick, -1..1 (ly: up is positive) */
    float lt, rt;        /* triggers, 0..1 */
    unsigned buttons;    /* bit (1 << PB_x); PB_LT / PB_RT are set by sho_pad_finish() from lt / rt */
} sho_pad;
static inline void sho_pad_finish(sho_pad *p) {   /* a trigger past halfway also counts as a button (and a button-only trigger as a full pull) */
    if (p->buttons & (1u << PB_LT)) { if (p->lt < 1) p->lt = 1; } else if (p->lt > 0.5f) p->buttons |= 1u << PB_LT;
    if (p->buttons & (1u << PB_RT)) { if (p->rt < 1) p->rt = 1; } else if (p->rt > 0.5f) p->buttons |= 1u << PB_RT;
}

/* what a pad button can do */
enum { ACT_NONE, ACT_ACCEL, ACT_BRAKE, ACT_TURBO, ACT_LEFT, ACT_RIGHT, ACT_START, ACT_COIN, ACT_COUNT };
/* what the keyboard has keys for */
enum { KA_LEFT, KA_RIGHT, KA_ACCEL, KA_BRAKE, KA_TURBO, KA_START, KA_COIN, KA_PAUSE, KA_COUNT };
#define KEY_SLOTS 4

typedef struct {
    unsigned char pad[PB_COUNT];        /* ACT_x for each button */
    char device[128];                   /* the chosen controller's name; "" = any controller */
    int key[KA_COUNT][KEY_SLOTS];       /* key codes (the frontend's own), -1 = none */
    float deadzone, curve;              /* the steering stick ([input] in shangon.ini) */
} sho_controls;
extern sho_controls sho_ctl;

const char *sho_button_name(int b);         /* "A", "LEFT TRIGGER", ... */
const char *sho_action_name(int a);         /* "ACCELERATE", ... ("-" for none) */
const char *sho_key_action_name(int ka);    /* "STEER LEFT", ... */

void sho_controls_pad_defaults(int swap_pedals);                    /* the layout the game has always had */
void sho_controls_key_defaults(const int def[KA_COUNT][KEY_SLOTS]);   /* the frontend's default keys (also kept for RESET) */
void sho_controls_key_reset(void);
/* text for shangon.ini */
void sho_controls_pad_to_str(char *out, int n);
int  sho_controls_pad_from_str(const char *s);                      /* 1 if it was a complete line */
void sho_controls_keys_to_str(char *out, int n);
int  sho_controls_keys_from_str(const char *s);

/* One pad's state into the game's input. slot: a small number that stays with that pad (stick hysteresis).
 * menu: the Original Mode screens or the pause menu are up (there A selects and B goes back, whatever they are set to).
 * steer / thr / brk: the strongest of every pad and the keyboard so far. */
void sho_controls_apply_pad(int slot, const sho_pad *p, int menu, sho_input *in, float *steer, float *thr, float *brk);
/* is one of the action's keys down? (down(code) is the frontend's "is this key held") */
int sho_key_action_down(int ka, int (*down)(int code));
int sho_key_is_action(int ka, int code);    /* is this key one of the action's? */
#endif
