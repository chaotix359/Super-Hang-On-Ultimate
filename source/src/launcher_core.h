/* Super Hang-On Ultimate launcher: the settings screen shown in the game window before the game starts, the same
 * on Windows, macOS and Linux. This part has no platform code: it keeps the settings, draws them in the arcade's own
 * text font over the launcher picture, takes keys and clicks, and shows a loading bar after PLAY. Each frontend
 * (win32_main.c, mac_main.m, linux_launcher.c) puts the picture on screen and passes its input in. */
#ifndef LAUNCHER_CORE_H
#define LAUNCHER_CORE_H
#include <stddef.h>
#include <stdint.h>

/* fills a one-line status and a detail line; returns 1 if the ROM is present and correct */
typedef int (*launcher_rom_check_fn)(char *msg, int msglen, char *detail, int detaillen);
enum { VIEW_FIT, VIEW_WIDE, VIEW_43 };   /* Linux and Mac display choices (Windows: 0 = 16:9, 1 = 4:3) */
typedef struct {
    int demo_sounds, difficulty, time_adj;
    int view;                   /* index into view_names */
    int arrange_music;          /* 0 = Regular Soundtrack, 1 = Arrange Soundtrack */
    int md_enhancements;        /* Mega Drive Enhancements: Original Mode */
    int md_controls;            /* Apply Mega Drive Controls */
    int md_music;               /* Mega Drive Music: the Mega Drive's songs throughout */
    char music_note[128];       /* shown under the Music option (e.g. how many MP3s were found) */
    launcher_rom_check_fn md_check;   /* the Mega Drive ROM: without it the Mega Drive options are greyed out */
    const char *game_dir;       /* the folder with the ROMs, music and settings (shown on the screen) */
    const char *const *view_names; int view_count;   /* the Display choices (capitals) */
    const char *view_desc;      /* the line under Display */
} launcher_settings;

#define LC_CW 640                  /* the settings canvas */
#define LC_CH 360
#define LC_IW 1920                 /* the launcher picture, and the finished image */
#define LC_IH 1080
enum { LK_UP, LK_DOWN, LK_LEFT, LK_RIGHT, LK_ENTER, LK_SPACE, LK_ESC, LK_PAD_A, LK_PAD_START };
enum { LC_RUNNING = -1, LC_QUIT = 0, LC_PLAY = 1 };

/* png: the launcher picture (res/launcher_bg.png, 1920x1080), built into each program */
void lc_begin(launcher_settings *s, launcher_rom_check_fn check, const unsigned char *png, size_t png_len);
void lc_key(int key);
void lc_click(int cx, int cy);              /* canvas pixels (0..639, 0..359) */
int  lc_folder_clicked(void);               /* 1 once after a click on the FOLDER line: open the game folder */
void lc_tick(double now_seconds);           /* ROM re-checks, the blinking cursor; redraws the canvas */
int  lc_result(void);                       /* LC_RUNNING until PLAY or quit */
void lc_finish(launcher_settings *s);       /* after PLAY: the choices into s */
void lc_end(void);                          /* frees everything (after the loading bar) */

const uint32_t *lc_canvas(void);            /* 640x360 ARGB, transparent where the picture shows */
const uint32_t *lc_background(int *w, int *h);   /* the decoded picture (ARGB), or NULL */
const uint32_t *lc_image(void);             /* 1920x1080 xRGB: picture + canvas (3x), for GPU presenters */
/* where a 16:9 picture sits in an out_w x out_h area (black bars around it), and a point there in canvas pixels */
void lc_view_rect(int out_w, int out_h, int *x, int *y, int *w, int *h);
void lc_to_canvas(int out_w, int out_h, int px, int py, int *cx, int *cy);

/* loading after PLAY: steps 0 the arcade ROM, 1 the arranged MP3s, 2 the Mega Drive capture (often finished
 * already; it runs at the same time as the MP3s), 3 starting. Each step's share of the bar is its expected time. */
void lc_load_begin(const int mp3_present[4], int md_pending);
void lc_load_step(int step, float within, const char *what);
void lc_music_progress(int track, float fraction);   /* for sho_music_load_progress */
extern void (*lc_present_hook)(void);       /* the frontend's "show the launcher now" (lc_load_step calls it) */
extern double (*lc_clock)(void);            /* seconds; limits the loading bar to about 30 pictures a second */
#endif
