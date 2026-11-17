/* Windows VR (OpenXR + Direct3D 11): the game on a large screen floating in front of the player, each eye
 * seeing its own picture of the 3D mode, and the two hand controllers held like a motorbike's handlebars.
 * Works with any OpenXR runtime for PC headsets (Virtual Desktop, Quest Link, SteamVR / Steam Link, WMR...). */
#ifndef WIN32_VR_H
#define WIN32_VR_H
#include <stdint.h>
#include "sho_core.h"

typedef struct {
    float screen_distance;   /* metres from the player to the screen */
    float screen_width;      /* metres */
    float screen_height_off; /* metres: the screen's centre above (+) or below (-) eye level */
    float steer_angle;       /* degrees of handlebar tilt for full lock */
    int steer_stick;         /* 1 = steer with the left thumbstick instead of tilting the controllers */
    int swap_triggers;       /* 1 = left trigger throttle, right trigger brake */
    int twist_throttle;      /* 1 = twist grip: hold the right grip button and roll that wrist back; both triggers brake */
    float twist_angle;       /* degrees of twist for full throttle */
} vr_settings;

int  vr_init(const vr_settings *s, char *err, int errlen);   /* 1 = a headset session was made */
void vr_shutdown(void);
int  vr_active(void);
void vr_configure(const vr_settings *s);
/* The headset has its own thread: it shows the newest picture given to vr_present at the headset's own rate and
 * reads the controllers, so the game keeps its own steady 60 frames a second whatever the headset does (its
 * refresh rate, a slow frame, the headset taken off...).
 * vr_poll: 1 when the headset session has ended (the game should close).
 * vr_present: fb = sho_present_frame()'s picture, fw x 224 (copied); eye_w = sho_width() (3D: fw = 2 x SHO_3D_SUB
 * x eye_w, left | right); aspect = the display shape of one eye's picture (width / height);
 * depth = the 3D depth setting (game pixels between the two eyes' pictures of the backdrop): if that is wider on
 * the screen than the eyes are apart, the two eyes' screens are slid together until the backdrop is exactly
 * infinitely far - so any depth setting is safe, and more depth brings the road and the bike closer to the player */
int  vr_poll(void);
void vr_stats(double seconds, double *fps, double *ms);   /* for the log: headset frames a second, and the time each took to make */
void vr_present(const uint32_t *fb, int fw, int eye_w, double aspect, int depth);
/* adds the hand controllers to the input made from keyboard and pads. menu = an Original Mode / pause menu is up */
void vr_input(sho_input *in, int menu);
/* 1 once after the player moved the screen with the right stick (up / down): *s = the settings now, to be saved */
int  vr_settings_changed(vr_settings *s);
void vr_recenter(void);                  /* put the screen in front of where the player looks now */
int  vr_y_pressed(void);                 /* 1 once per press of Y (left controller): the settings menu */
int  vr_menu_pressed(void);              /* 1 once per press of the left controller's menu button */
#endif
