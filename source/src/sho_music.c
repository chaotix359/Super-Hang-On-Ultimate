#include <stdlib.h>
extern long sho_frame_no;
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "sho_music.h"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_ONLY_MP3
#include "third_party/dr_mp3/dr_mp3.h"

/* Menu order and sound-driver header addresses from the verified 'shangon' ROM. */
const sho_music_info sho_music_tracks[4] = {
    { "Outride a Crisis", 0x9d, 0x1f21 },
    { "Sprinter",         0x82, 0x4fc3 },
    { "Winning Run",      0x96, 0x3c84 },
    { "Hard Road",        0x81, 0x6107 }
};
typedef struct {
    drmp3_int16 *pcm;
    drmp3_uint64 frames;
    drmp3_config format;
} music_track;
static music_track tracks[4];
static unsigned output_rate = 48000;
static int current = -1, volume = 55;
static double position;

void sho_music_stop(void) { current = -1; position = 0; }
int sho_music_current_track(void) { return current; }
void sho_music_set_volume(int percent) { volume = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
void sho_music_unload(void) {
    sho_music_stop();
    for (int i = 0; i < 4; i++) { drmp3_free(tracks[i].pcm, NULL); memset(&tracks[i], 0, sizeof tracks[i]); }
}
unsigned sho_music_mask(void) {
    unsigned mask = 0;
    for (int i = 0; i < 4; i++) if (tracks[i].pcm) mask |= 1u << i;
    return mask;
}
/* Decodes a whole MP3 in pieces (the same result as drmp3_open_file_and_read_pcm_frames_s16), telling
   sho_music_load_progress how far through the file it is. */
void (*sho_music_load_progress)(int track, float fraction);
static drmp3_int16 *decode_file(const char *path, int track, drmp3_config *fmt, drmp3_uint64 *frames) {
    long size = 0;
    { FILE *f = fopen(path, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); size = ftell(f); fclose(f); }
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path, NULL)) return NULL;
    drmp3_uint64 n = 0, cap = 0; drmp3_int16 *out = NULL;
    for (;;) {
        enum { CHUNK = 65536 };
        if (n + CHUNK > cap) {
            drmp3_uint64 nc = cap ? cap * 2 : CHUNK * 16;
            drmp3_int16 *b = (drmp3_int16 *)realloc(out, (size_t)nc * mp3.channels * sizeof *out);
            if (!b) { free(out); drmp3_uninit(&mp3); return NULL; }
            out = b; cap = nc;
        }
        drmp3_uint64 got = drmp3_read_pcm_frames_s16(&mp3, CHUNK, out + n * mp3.channels);
        n += got;
        if (sho_music_load_progress && size > 0) sho_music_load_progress(track, (float)mp3.streamCursor / (float)size);
        if (got < CHUNK) break;
    }
    fmt->channels = mp3.channels; fmt->sampleRate = mp3.sampleRate;
    drmp3_uninit(&mp3);
    if (!n) { free(out); return NULL; }
    *frames = n;
    return out;   /* (freed with drmp3_free, which uses free()) */
}

int sho_music_load_files(const char *directory, unsigned sample_rate, char *err, size_t errlen) {
    sho_music_unload();
    if (errlen) err[0] = 0;
    if (!sample_rate) { if (errlen) snprintf(err, errlen, "Invalid output sample rate"); return 0; }
    output_rate = sample_rate;
    int loaded = 0;
    for (int i = 0; i < 4; i++) {
        char path[1024];
        int n = snprintf(path, sizeof path, "%s/%s.mp3", directory, sho_music_tracks[i].title);
        if (n < 0 || (size_t)n >= sizeof path) continue;
        music_track *t = &tracks[i];
        t->pcm = decode_file(path, i, &t->format, &t->frames);
        if (!t->pcm || t->frames < 2 || !t->format.sampleRate || t->format.channels < 1 || t->format.channels > 2) {
            drmp3_free(t->pcm, NULL); memset(t, 0, sizeof *t);
            if (errlen) { size_t len = strlen(err); if (len < errlen) snprintf(err + len, errlen - len, "%s%s", len ? ", " : "Original music used for: ", sho_music_tracks[i].title); }
            continue;
        }
        loaded++;
    }
    return loaded;
}
void sho_music_on_command(uint8_t command) {
    for (int i = 0; i < 4; i++) if (command == sho_music_tracks[i].command) {
        sho_music_stop();
        if (tracks[i].pcm) current = i;
        return;
    }
    /* Commands that clear or replace the driver's music channels. 0x80 is idle;
       coin, countdown, engine and collision commands leave the arrangement playing. */
    switch (command) {
    case 0x00: case 0x83: case 0x8e: case 0x92: case 0x9e: case 0x9f:
        sho_music_stop(); break;
    }
}
void sho_music_mix(int16_t *samples, int frames) {
    if (current < 0 || frames <= 0) return;
    const music_track *t = &tracks[current];
    double step = (double)t->format.sampleRate / output_rate;
    float gain = volume / 100.0f;
    for (int j = 0; j < frames; j++) {
        drmp3_uint64 a = (drmp3_uint64)position, b = a + 1;
        if (b >= t->frames) b = 0;
        float fraction = (float)(position - a);
        for (unsigned c = 0; c < 2; c++) {
            unsigned channel = t->format.channels == 1 ? 0 : c;
            float x = t->pcm[a * t->format.channels + channel];
            float y = t->pcm[b * t->format.channels + channel];
            float mixed = samples[j * 2 + c] + gain * (x + (y - x) * fraction);
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;
            samples[j * 2 + c] = (int16_t)mixed;
        }
        position += step;
        while (position >= t->frames) position -= t->frames;
    }
}
