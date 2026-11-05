#ifndef WIN32_LAUNCHER_H
#define WIN32_LAUNCHER_H
#include <windows.h>
/* fills a one-line status and a detail line; returns 1 if the ROM is present and correct */
typedef int (*launcher_rom_check_fn)(char *msg, int msglen, char *detail, int detaillen);
typedef struct {
    int demo_sounds, difficulty, time_adj, widescreen;
    int arrange_music;          /* 0 = Regular Soundtrack, 1 = Arrange Soundtrack */
    int md_enhancements;        /* Mega Drive Enhancements: Original Mode */
    int md_controls;            /* Apply Mega Drive Controls */
    int md_music;               /* Mega Drive Music: the Mega Drive's songs throughout */
    char music_note[128];          /* shown under the Music option (e.g. how many MP3s were found) */
    launcher_rom_check_fn md_check;   /* the Mega Drive ROM: status lines under the arcade ROM's; without it the Mega Drive options are greyed out */
} launcher_settings;
/* shows the launcher; returns 1 if PLAY was pressed (settings updated), 0 if the window was closed */
int launcher_run(HINSTANCE inst, launcher_settings *s, launcher_rom_check_fn check);
#endif
