/* Super Hang-On launcher for macOS (the Mac twin of win32_launcher.c). */
#ifndef MAC_LAUNCHER_H
#define MAC_LAUNCHER_H
/* fills a one-line status and a detail line; returns 1 if the ROM is present and correct */
typedef int (*launcher_rom_check_fn)(char *msg, int msglen, char *detail, int detaillen);
enum { VIEW_FIT, VIEW_WIDE, VIEW_43 };   /* fit the screen's shape, 16:9, 4:3 */
typedef struct {
    int demo_sounds, difficulty, time_adj;
    int view;                   /* VIEW_FIT / VIEW_WIDE / VIEW_43 */
    int arrange_music;          /* 0 = Regular Soundtrack, 1 = Arrange Soundtrack */
    int md_enhancements;        /* Mega Drive Enhancements: Original Mode */
    int md_controls;            /* Apply Mega Drive Controls */
    int md_music;               /* Mega Drive Music: the Mega Drive's songs throughout */
    char music_note[128];       /* shown under the Music option (e.g. how many MP3s were found) */
    launcher_rom_check_fn md_check;   /* the Mega Drive ROM: status lines under the arcade ROM's; without it the Mega Drive options are greyed out */
    const char *game_dir;       /* the folder with the ROMs, music and settings (Open Game Folder) */
} launcher_settings;
/* shows the launcher; returns 1 if PLAY was pressed (settings updated), 0 if the window was closed or the app quit */
int launcher_run(launcher_settings *s, launcher_rom_check_fn check);
void launcher_cancel(void);   /* close it as if the window was closed (the app is quitting) */
#endif
