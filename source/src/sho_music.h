/* External soundtrack. Audio is decoded once; mixing performs no file I/O. */
#ifndef SHO_MUSIC_H
#define SHO_MUSIC_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *title;
    uint8_t command;
    unsigned rom_header;
} sho_music_info;
extern const sho_music_info sho_music_tracks[4];

int sho_music_load_files(const char *directory, unsigned sample_rate, char *err, size_t errlen);
void sho_music_unload(void);
unsigned sho_music_mask(void);
void sho_music_set_volume(int percent);
void sho_music_on_command(uint8_t command);
void sho_music_stop(void);
void sho_music_mix(int16_t *samples, int frames);
int sho_music_current_track(void); /* -1 = original soundtrack / silence */
#endif
