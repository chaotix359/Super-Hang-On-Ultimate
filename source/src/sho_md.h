/* Mega Drive Enhancements (Original Mode) and Mega Drive controls for the arcade core.
 *
 * Both are off by default; when off, every hook below is a no-op and the arcade game runs
 * exactly as the original (verified bit-exact by the lockstep tests).
 *
 *  - Mega Drive Controls: the stick/d-pad drives a virtual handlebar the way the Mega Drive
 *    version does (steer-in, counter-steer and centring rates, speed sensitivity). In arcade
 *    play it uses the Mega Drive Arcade Mode handling.
 *  - Mega Drive Enhancements: adds Original Mode (selected on Start): races against rivals on
 *    the arcade courses, prize money, the Mega Drive parts shop (frame/engine/brake/muffler/
 *    oil/tyre, with the Mega Drive's names and prices), wear and the mechanic, saved to a file.
 *    In Original Mode races the frame sets the steering inertia, and the other parts change
 *    acceleration, top speed, braking and tyre grip on top of the arcade physics.
 */
#ifndef SHO_MD_H
#define SHO_MD_H
#include <stdint.h>
#include "sho_core.h"

/* public */
void sho_md_configure(int enhancements, int controls);
void sho_md_set_music(int on);        /* the Mega Drive Music option (needs the Mega Drive ROM already read) */
int  sho_md_music_active(void);
int  sho_md_audio_mode(void);         /* 0 arcade audio, 1 Mega Drive songs replace the arcade's, 2 live Mega Drive screens */
void sho_md_music_command(uint8_t cmd);
void sho_md_set_save_path(const char *path);       /* Original Mode save file */
int sho_md_overlay_active(void);                   /* an Original Mode screen is showing (menus take keyboard nav) */

/* core hooks (sho_core.c) */
extern int md_enabled;                             /* enhancements or controls on */
void md_filter_input(sho_input *in);               /* may block, inject or reshape the player's input */
int md_steer_adc(float s, int *adc);               /* 1 = steering handled (adc written) */
void md_pre_frame(void);
void md_post_frame(void);
int md_extra_frames(void);                         /* hidden fast-forward frames wanted after this one */
void md_compose(uint32_t *fb, int width);
int sho_md_reset_requested(void);
int sho_md_exit_requested(void);                   /* EXIT GAME chosen on the mode choice */                  /* 1 = reset to the title screen, 2 = reset the arcade machine only (entering Original Mode) */
void sho_md_before_reset(void);                    /* call before any machine reset */
int md_frozen(void);                               /* the arcade is held still (the goal sequence) */
int sho_md_pause_allowed(void);
void sho_md_dev_race(int n);                       /* development: 1-7 = Original Mode always starts that race, 0 = normal */
int sho_md_dev_race_get(void);
void sho_md_cheat_upgrade(void);
void sho_md_cheat_money(void);                     /* development: Ctrl+9 on the Original Mode menu = the most money the game holds */                   /* development: Ctrl+8 on the Original Mode menu = fully upgraded bike */                    /* Mega Drive mode, during a race: Start pauses */
void md_pause_reset(void);
/* pause menu: draws and handles input; returns 0 = stay, 1 = continue, 2 = exit to title; levels 0-10 */
int md_pause_menu(uint32_t *fb, int width, int tick, const sho_input *in, int *music, int *sfx);          /* draw Original Mode screens / race HUD */
int md_mute(void);

#endif
