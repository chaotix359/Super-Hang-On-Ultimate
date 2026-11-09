/* Mega Drive sound: the Mega Drive's own Z80 sound driver (taken at run time from the player's ROM), run on a
   Z80 core with a YM2612 (Nuked OPN2) and an SN76489 (PSG). Nothing is stored on disk. */
#ifndef SHO_MDSND_H
#define SHO_MDSND_H
#include <stdint.h>
int  mdsnd_boot(const uint8_t *rom, long romsz);   /* load the driver from the ROM and start it; 1 = ok */
int  mdsnd_ready(void);
void mdsnd_shutdown(void);
void mdsnd_command(uint8_t cmd);                   /* send a sound command to the driver (the $A01C03 mailbox) */
void mdsnd_resync(void);                           /* restart the driver on the current screen's music (after a fast-forward) */
void mdsnd_zwrite(unsigned a, uint8_t v);          /* the 68000 writing Z80 memory (live Mega Drive) */
uint8_t mdsnd_zread(unsigned a);
void mdsnd_zreset(int running);                    /* Z80 reset line: 0 = held, 1 = released */
void mdsnd_set_follow(int on);                     /* 1 = a live Mega Drive is driving the Z80 */
void mdsnd_add(int16_t *out, int frames, unsigned rate, int gain_percent);   /* add stereo samples to out */
extern unsigned (*mdsnd_command_hook)(unsigned cmd);   /* if set: sees each sound command the Mega Drive's program sends and returns the one to deliver */
extern int mdsnd_log;                              /* debug: log mailbox writes */
unsigned mdsnd_last_command(void);
unsigned mdsnd_last_music(void);                   /* the last song command ($81-$85), 0 after a stop */
#endif
