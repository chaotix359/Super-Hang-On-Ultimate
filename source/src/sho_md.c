/* Mega Drive Enhancements (Original Mode) and Mega Drive controls - see sho_md.h.
 *
 * The arcade machine keeps running underneath everything: Original Mode screens are drawn over
 * a dimmed arcade picture, a race is started by driving the arcade's own coin/start/class-select
 * inputs, and the race itself is the arcade game with its time limit held, a lap timer and rival
 * shown on top, and the parts' effects applied to the arcade's speed and cornering values.
 *
 * Arcade RAM used (found by tracing the original program):
 *   sub  $260000  game state: 0 attract/menus, 1 start countdown, 2 racing, 3 goal
 *   sub  $260012  class (course): 0 Africa/Beginner .. 3 Europe/Expert
 *   sub  $260078  speed, 16.16 km/h (integrated by the main CPU at 0x6382)
 *   sub  $260094  crash/slow-down in progress
 *   work $060408  credit game in progress (0 during the attract demo)
 *   work $060438  stage number (1..)
 *   work $060500  time remaining, BCD seconds
 *   work $066400..$0665BF  high score tables (kept untouched by Original Mode races)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sho_md.h"
#include "sho_music.h"
#include "sho_internal.h"
#include "sho_mdgfx.h"
#include "sho_mdcourse.h"
#include "sho_mdsnd.h"

extern long sho_frame_no, sho_entry_last_frame, sho_menu_last_frame;
extern int sho_menu_step;

int md_enabled;
int md_grip_q8 = 256;                       /* read by the M_0065EC override */
static int cfg_enh, cfg_ctl;
static int arc_t;   /* Arcade chosen on the mode screen: a credit, then Start if needed, up to the class select */
static int arcade_pass, arcade_ok, select_prev;   /* arcade-mode choice in force / class-select edge */
static float hb;   /* virtual handlebar position, ADC units (+ = right) */
static int md_pot = 0x80, md_pot_tick;   /* the Mega Drive's virtual steering potentiometer (see md_steer_adc) */
static char save_path[1024] = "shangon_original.sav";
/* live Mega Drive Original Mode (with the Mega Drive ROM): its own screens and rules run in the embedded
   Mega Drive; only the races are run by the arcade engine */
static int md_live;                 /* this Original Mode session runs the Mega Drive live */
static int reset_request;           /* ask the host: 1 = reset to the title screen, 2 = reset the arcade machine only */
/* finish: the Mega Drive's goal - the picture stops, the bike rides off into the distance, then a fade */
static int finish_t;
static int lap_start_f, lap_last_f, lap_last_d, prev_d;
static uint32_t bar_acc, bar_pos;   /* the Mega Drive's bar ($FF0530): the course position's steps added up; $800 a lap */   /* race frame the lap began; length of the lap before (frames, and in $260090 distance units) */   /* race frame the lap began; length of the lap before */
static uint32_t *fin_bg, *fin_bike; static int fin_x0, fin_y0, fin_x1, fin_y1, fin_w;   /* road without the bike / bike pixels */
/* the Mega Drive's damage model in the race (Original Mode): see md_damage_frame */
static int broken_part = -1, broken_t, wear_t[6];
int sho_md_reset_requested(void) { int r = reset_request; reset_request = 0; return r; }
static int exit_request;
int sho_md_exit_requested(void) { return exit_request; }
extern const mdscene_t *sho_md_scene;
extern int sho_video_hide_player;
int sho_video_horizon(void);
void sho_video_render(uint32_t *fb);
int sho_video_width(void);
/* before the host resets the machine: put back what an Original Mode race changed in the arcade */
static uint8_t hiscore_backup[0x1c0]; static int hiscore_saved;
/* the Mega Drive sound driver is only kept running while something needs it: the Mega Drive Music option, or the
   live Mega Drive screens. Leaving Original Mode stops the music; without the option the driver is shut down. */
static int cfg_music;
static void md_sound_release(void) {
    if (!mdsnd_ready()) return;
    if (cfg_music) mdsnd_command(0xb0); else mdsnd_shutdown();
}
static int live_screens_up(void);
void sho_md_before_reset(void) {
    if (hiscore_saved) { memcpy(M->workram + 0x6400, hiscore_backup, sizeof hiscore_backup); hiscore_saved = 0; }
    mdcourse_uninstall(); sho_md_scene = NULL;
    if (!live_screens_up()) md_sound_release();   /* Mega Drive music off with the machine reset (not under the live screens) */
}
static int live_resolve;            /* after an arcade race: hand the result to the Mega Drive */
static unsigned live_buttons, live_prev_state;
static long live_time_frames; static int live_retired, live_music;
static uint32_t live_img[320 * 224];
static int dev_race;   /* development: 1-7 = always this Original Mode race (Ctrl+1..7, Ctrl+0 off) */
static int cheat_req, cheat_msg;   /* development: Ctrl+8 on the Original Mode menu = every part at its best, as new */
void sho_md_cheat_upgrade(void) { cheat_req |= 1; }
void sho_md_cheat_money(void) { cheat_req |= 2; }   /* Ctrl+9: the most money the game holds ($99999999 BCD at $B638) */
static const char *cheat_text = "";
void sho_md_dev_race(int n) { dev_race = n >= 1 && n <= 7 ? n : 0; }
int sho_md_dev_race_get(void) { return dev_race; }
static void md_save_path(char *o, size_t n) {   /* shangon_original.sav -> shangon_original_md.sav */
    snprintf(o, n, "%s", save_path); char *d = strrchr(o, '.'); if (d && strlen(o) + 4 < n) memmove(d + 3, d, strlen(d) + 1), memcpy(d, "_md", 3);
}

/* ------------------------------------------------------------------ */
/* Mega Drive data                                                     */
/* ------------------------------------------------------------------ */
enum { P_FRAME, P_ENGINE, P_BRAKE, P_MUFFLER, P_OIL, P_TIRE, NPART };
static const char *part_label[NPART] = { "FRAME", "ENGINE", "BRAKE", "MUFFLER", "OIL", "TIRE" };
typedef struct { const char *name; long price; } part_grade;
/* names and prices exactly as in the Mega Drive parts shop */
static const part_grade grades[NPART][6] = {
    { { "IRON CRADLE", 1200 }, { "DOUBLE CRADLE", 2000 }, { "ALUMINIUM DOUBLE CRADLE", 8000 },
      { "ALUMINIUM SPACE", 128000 }, { "TITANIUM SPACE", 500000 }, { "TITANIUM MONOCOQUE", 8000000 } },
    { { "OHC NORMAL", 600 }, { "DOHC NORMAL", 2000 }, { "DOHC V4", 8000 },
      { "TURBO V4", 32000 }, { "CERAMIC TURBO", 512000 }, { "TWIN TURBO", 8192000 } },
    { { "DRUM", 400 }, { "DISC", 1500 }, { "VENTILATED DISC", 6000 }, { "DOUBLE DISC", 24000 }, { "ANTI-SKID", 90000 } },
    { { "NORMAL", 300 }, { "COL", 1500 }, { "WORKS ORIGINAL", 6000 }, { "PRIVATE ORIGINAL", 90000 } },
    { { "REGULAR", 100 }, { "MANUAL", 200 }, { "TOP", 3200 }, { "PRIVATE ORIGINAL", 14000 } },
    { { "NORMAL", 300 }, { "RADIAL", 400 }, { "HIGH PERFORMANCE", 1500 }, { "SLICK", 6000 }, { "ORIGINAL SLICK", 24000 } },
};
static const int ngrades[NPART] = { 6, 6, 5, 4, 4, 5 };
/* Mega Drive handlebar rates per frame grade (ROM table $A038): centre-return, steer-in, counter-steer.
 * Grade 6 is what the Mega Drive's Arcade Mode uses. */
static const int frame_rates[7][3] = { { 2, 3, 6 }, { 2, 4, 8 }, { 2, 4, 9 }, { 3, 5, 11 }, { 4, 7, 16 }, { 5, 8, 18 }, { 3, 6, 16 } };
/* tuning of the other parts on the arcade physics (prototype values) */
static const float eng_accel[6] = { 0.70f, 0.80f, 0.90f, 1.00f, 1.12f, 1.25f };
static const int eng_top[6] = { 222, 238, 256, 999, 999, 999 };      /* speed cap (arcade units: 279 = 324 km/h on the meter); turbo engines keep the arcade top speed */
static const float muf_accel[4] = { 0.0f, 0.03f, 0.06f, 0.10f };
static const float oil_accel[4] = { 0.0f, 0.02f, 0.04f, 0.07f };
static const float brk_power[5] = { 0.70f, 0.85f, 1.00f, 1.15f, 1.30f };
static const float tire_push[5] = { 1.30f, 1.20f, 1.05f, 0.92f, 0.80f };   /* outward push on curves (1 = arcade) */

static const char *course_name[4] = { "AFRICA", "ASIA", "AMERICA", "EUROPE" };
static const char *class_name[4] = { "BEGINNER", "JUNIOR", "SENIOR", "EXPERT" };
static const int course_stages[4] = { 6, 10, 14, 18 };
static const int course_allow[4] = { 185, 308, 431, 554 };   /* arcade time allowance (normal), seconds */
typedef struct { const char *name, *quote; } rival_t;
static const rival_t rivals[4][4] = {
    { { "MIA FERRARU", "I'M REALLY FAST!" }, { "DINO ROSSANO", "TRY TO KEEP UP." }, { "KAI NAKAMURA", "THE DESERT IS MINE." }, { "LEON VARGAS", "NOBODY PASSES ME HERE." } },
    { { "SUN HARADA", "WELCOME TO THE BIG LEAGUE." }, { "OTTO BRANDT", "PRECISION WINS RACES." }, { "RIO SANTOS", "I LIVE FOR THE CORNERS." }, { "IVAN PETROV", "YOU WILL NOT SEE MY TAIL." } },
    { { "JACK MURDOCK", "LONG ROADS, FAST BIKES." }, { "ELLA STONE", "DON'T BLINK." }, { "MARCO VELLI", "MY MECHANIC IS A GENIUS." }, { "KEN TAKEDA", "I NEVER LOSE ON HOME SOIL." } },
    { { "LUCA BIANCHI", "THE ALPS WILL BREAK YOU." }, { "NIKO LAINE", "ICE IN MY VEINS." }, { "PIERRE DUVAL", "ELEGANCE AT 300 KM/H." }, { "THE CHAMPION", "SHOW ME WHAT YOU'VE GOT." } },
};
static const float rival_pace[4] = { 1.08f, 1.00f, 0.94f, 0.89f };   /* x course allowance */
static const long prize[4][4] = { { 1500, 2500, 4000, 6000 }, { 10000, 15000, 25000, 40000 },
                                  { 60000, 100000, 160000, 250000 }, { 400000, 700000, 1200000, 2000000 } };

/* ------------------------------------------------------------------ */
/* save data                                                           */
/* ------------------------------------------------------------------ */
typedef struct {
    long money;
    int part[NPART], cond[NPART];          /* grade owned, condition 0..15 */
    int rival[4];                          /* current rival per course (4 = course cleared) */
    int wins, losses, unlocked;
    long best[4];                          /* best time per course, frames (0 = none) */
} profile_t;
static profile_t pf;
static int have_save;

static void profile_new(void) {
    memset(&pf, 0, sizeof pf);
    for (int i = 0; i < NPART; i++) pf.cond[i] = 15;
    pf.unlocked = 1;
}
static void profile_save(void) {
    FILE *f = fopen(save_path, "w"); if (!f) return;
    fprintf(f, "; Super Hang-On Ultimate Original Mode save\nversion=1\nmoney=%ld\nwins=%d\nlosses=%d\nunlocked=%d\n", pf.money, pf.wins, pf.losses, pf.unlocked);
    for (int i = 0; i < NPART; i++) fprintf(f, "part%d=%d\ncond%d=%d\n", i, pf.part[i], i, pf.cond[i]);
    for (int c = 0; c < 4; c++) fprintf(f, "rival%d=%d\nbest%d=%ld\n", c, pf.rival[c], c, pf.best[c]);
    fclose(f); have_save = 1;
}
static int profile_load(void) {
    FILE *f = fopen(save_path, "r"); if (!f) return 0;
    profile_new(); char line[128]; int ok = 0;
    while (fgets(line, sizeof line, f)) {
        char key[32]; long v;
        if (sscanf(line, "%31[a-z0-9]=%ld", key, &v) != 2) continue;
        int idx = key[strlen(key) - 1] - '0';
        if (!strcmp(key, "version")) ok = 1;
        else if (!strcmp(key, "money")) pf.money = v < 0 ? 0 : v;
        else if (!strcmp(key, "wins")) pf.wins = (int)v;
        else if (!strcmp(key, "losses")) pf.losses = (int)v;
        else if (!strcmp(key, "unlocked")) pf.unlocked = v < 1 ? 1 : v > 4 ? 4 : (int)v;
        else if (!strncmp(key, "part", 4) && idx >= 0 && idx < NPART) pf.part[idx] = v < 0 ? 0 : v >= ngrades[idx] ? ngrades[idx] - 1 : (int)v;
        else if (!strncmp(key, "cond", 4) && idx >= 0 && idx < NPART) pf.cond[idx] = v < 0 ? 0 : v > 15 ? 15 : (int)v;
        else if (!strncmp(key, "rival", 5) && idx >= 0 && idx < 4) pf.rival[idx] = v < 0 ? 0 : v > 4 ? 4 : (int)v;
        else if (!strncmp(key, "best", 4) && idx >= 0 && idx < 4) pf.best[idx] = v < 0 ? 0 : v;
    }
    fclose(f);
    return ok;
}

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */
enum { PH_ARCADE, PH_MODESEL, PH_TITLE, PH_CONFIRM_NEW, PH_MENU, PH_PARTS, PH_MECH, PH_COURSE, PH_LAUNCH, PH_RACE, PH_RESULT, PH_MDLIVE, PH_FINISH };
static int phase = PH_ARCADE;
void sho_md_configure(int enhancements, int controls) {
    cfg_enh = enhancements != 0; cfg_ctl = controls != 0;
    md_enabled = cfg_enh || cfg_ctl;
    sho_md_before_reset(); md_live = 0; live_resolve = 0; mdlive_stop(); finish_t = 0; broken_part = -1;
    phase = PH_ARCADE; md_grip_q8 = 256; hb = 0; md_pot = 0x80; md_pot_tick = 0; arcade_ok = 0; arcade_pass = 0; select_prev = 0; arc_t = 0;
}
/* ---- Mega Drive music ----
   mode 2: the live Mega Drive's screens (menus, mechanic...) play their own music through its own sound driver.
   mode 1: the "Mega Drive Music" option: the arcade's songs are replaced by the Mega Drive's. Mode 0: arcade audio. */
void sho_md_set_music(int on) { cfg_music = on && mdgfx && mdgfx->rom && mdsnd_boot(mdgfx->rom, mdgfx->romsz); }
int sho_md_music_active(void) { return cfg_music; }
int sho_md_audio_mode(void) { if (cfg_enh && md_live && phase == PH_MDLIVE && mdsnd_ready()) return 2; return cfg_music ? 1 : 0; }
static int overlay_phase(int p);
void sho_md_music_command(uint8_t cmd) {   /* an arcade sound command: songs and stops are passed to the Mega Drive's driver */
    if (!cfg_music) return;
    if (overlay_phase(phase) || phase == PH_FINISH) return;   /* the Mega Drive's own screens and the goal run their own sound */
    switch (cmd) {
    case 0x9d: mdsnd_command(0x82); break;   /* Outride a Crisis (Mega Drive song $82) */
    case 0x82: mdsnd_command(0x84); break;   /* Sprinter ($84) */
    case 0x96: mdsnd_command(0x83); break;   /* Winning Run ($83) */
    case 0x81: mdsnd_command(0x85); break;   /* Hard Road ($85) */
    case 0x00: case 0x83: case 0x8e: case 0x92: case 0x9e: case 0x9f: mdsnd_command(0xb0); break;
    }
}
void sho_md_set_save_path(const char *path) { snprintf(save_path, sizeof save_path, "%s", path); }
static int overlay_phase(int p) { return p != PH_ARCADE && p != PH_LAUNCH && p != PH_RACE && p != PH_FINISH; }
static int live_screens_up(void) { return md_live && phase == PH_MDLIVE; }
int sho_md_overlay_active(void) { return md_enabled && overlay_phase(phase); }

/* ------------------------------------------------------------------ */
/* machine state                                                       */
/* ------------------------------------------------------------------ */
static int game_state(void) { return be16r(M->subram, 0x00); }
static int playing(void) { return be16r(M->workram, 0x408) != 0; }
static int entry_active(void) { return sho_frame_no - sho_entry_last_frame <= 2; }
static int select_active(void) { return sho_frame_no - sho_menu_last_frame <= 2; }
static int machine_idle(void) { return !playing() && !entry_active() && !select_active() && game_state() != 1 && game_state() != 3; }
/* idle, or waiting on the class select with no game started (free play goes straight there after boot) */
static int machine_ready(void) { return !playing() && !entry_active() && game_state() != 1 && game_state() != 3; }
static uint32_t speed_get(void) { return ((uint32_t)be16r(M->subram, 0x78) << 16) | be16r(M->subram, 0x7a); }
static void speed_set(uint32_t v) { be16w(M->subram, 0x78, (uint16_t)(v >> 16)); be16w(M->subram, 0x7a, (uint16_t)v); }

/* ------------------------------------------------------------------ */
/* menu input (edges with auto-repeat)                                 */
/* ------------------------------------------------------------------ */
static int k_up, k_down, k_left, k_right, k_ok, k_back;
static int held[6];
static int edge(int down, int *h, int repeat) {
    if (!down) { *h = 0; return 0; }
    int n = ++*h; return n == 1 || (repeat && n > 18 && (n - 18) % 5 == 0);
}
static void read_menu_keys(const sho_input *in) {
    k_up = edge(in->nav_up, &held[0], 1); k_down = edge(in->nav_down, &held[1], 1);
    k_left = edge(in->nav_left, &held[2], 1); k_right = edge(in->nav_right, &held[3], 1);
    k_ok = edge(in->pad_a || in->start, &held[4], 0); k_back = edge(in->pad_b, &held[5], 0);
}

/* ------------------------------------------------------------------ */
/* Original Mode state                                                 */
/* ------------------------------------------------------------------ */
static int cur_menu, cur_title, cur_modesel, cur_part, cur_mech, sel_course, browse[NPART];
static int launch_step, launch_timer, launch_course;
static int racing, race_frames, race_done, race_retired, crashes, brake_frames, crash_prev, back_held;
static int stage_prev, gap_timer; float gap_secs;
/* Mega Drive Original Mode lap message ($E5F6 -> object $A98A): "YOU m'ss"hh / RIVAL m'ss"hh" for 120 frames after each lap.
   The rival's lap time is the ROM's base time for the race ($8D84, BCD) with random hundredths ($8DF0: tens 3-6, units 0/3/6) */
static int fin_cross_f;   /* race_frames when the last lap line was crossed */
static int msg_t, msg_you_cs, msg_rv_cs, rv_lap_cs[8], live_rv_cs[8]; static unsigned rv_rng = 1;
static int bcd2(int b) { return (b >> 4) * 10 + (b & 15); }
static void live_rival_table(void) {   /* the Mega Drive's rival lap table ($C864 + 8 per lap, BCD 00 mm ss hh) */
    for (int i = 0; i < 8; i++) {
        uint32_t v = mdlive_r32(0xc864 + 8 * i); int ok = v && !(v >> 24) && ((v >> 16) & 15) < 10 && ((v >> 12) & 15) < 6;
        live_rv_cs[i] = ok ? (int)(((v >> 16) & 15) * 6000 + ((v >> 12) & 15) * 1000 + ((v >> 8) & 15) * 100 + ((v >> 4) & 15) * 10 + (v & 15)) : 0;
    }
}
static void rival_laps_init(int race) {
    static const int unit[8] = { 0, 3, 6, 0, 3, 6, 0, 3 };
    rv_rng = 0x2a6d365bu ^ (unsigned)sho_frame_no * 2654435761u;
    int base = 0;
    if (mdgfx && mdgfx->rom && mdgfx->romsz > 0x8da0 && race >= 0 && race < 7) {
        const uint8_t *p = mdgfx->rom + 0x8d84 + 4 * race;
        base = (bcd2(p[1]) * 60 + bcd2(p[2])) * 100;
    }
    for (int i = 0; i < 8; i++) {
        rv_rng = rv_rng * 1103515245u + 12345u; int t = 3 + (int)((rv_rng >> 16) & 3);
        rv_rng = rv_rng * 1103515245u + 12345u; int u = unit[(rv_rng >> 16) & 7];
        rv_lap_cs[i] = base ? base + t * 10 + u : 0;
    }
    if (md_live) { live_rival_table(); if (getenv("LIVEDBG")) fprintf(stderr, "live table %d %d %d  fallback %d\n", live_rv_cs[0], live_rv_cs[1], live_rv_cs[2], rv_lap_cs[0]); }
    for (int i = 0; i < 8; i++) if (live_rv_cs[i] > 0) rv_lap_cs[i] = live_rv_cs[i];   /* the live Mega Drive's own table */
    msg_t = 0;
}
static uint32_t spd_last;
static int result_win, result_ready, inject_start; long result_prize, result_time, result_rival; int wear_d[NPART];
static char msg[64]; static int msg_timer;

static void say(const char *m) { snprintf(msg, sizeof msg, "%s", m); msg_timer = 150; }
static long repair_cost(int p) { long c = grades[p][pf.part[p]].price * (15 - pf.cond[p]) / 40; return pf.cond[p] < 15 && c < 10 ? 10 : c; }
static int cur_rival(int c) { return pf.rival[c] > 3 ? 3 : pf.rival[c]; }
/* with the Mega Drive ROM present, races are the Mega Drive's Original Mode courses (laps of one stage) */
static int md_race = -1, md_laps, launch_class;
static int race_for_course(int c) { int r = c * 2 + cur_rival(c) / 2; return r > 6 ? 6 : r; }
static int laps_for_course(int c) { mdcourse_t ci; return mdcourse_available() && mdcourse_info(race_for_course(c), &ci) ? ci.laps : 0; }
static long live_rival;   /* live Mega Drive: the rival's time for this race, frames */
static long rival_frames(int c) {
    if (md_live && live_rival > 0) return live_rival;
    int laps = laps_for_course(c);
    double secs = laps ? laps * 31.0 : course_allow[c];
    return (long)(secs * rival_pace[cur_rival(c)] * SHO_FPS);
}
static long race_prize(int c) { long p = prize[c][cur_rival(c)]; return pf.rival[c] > 3 ? p / 2 : p; }

static int md_ok(void);
static void start_original(void) {
    if (getenv("LIVEDBG")) fprintf(stderr, "start_original at %ld held4 %d\n", (long)sho_frame_no, held[4]);
    reset_request = 2;   /* a clean arcade machine for Original Mode (the host resets it, keeping this phase) */
    md_live = md_ok() && mdcourse_available();
    if (md_live) { char p[1100]; md_save_path(p, sizeof p); have_save = mdlive_saved(p); }
    else have_save = profile_load();
    cur_title = 0; phase = PH_TITLE;
}
/* live: go to the Mega Drive (continue from the save, or a new game) */
static void live_open(int cont) {
    char p[1100]; md_save_path(p, sizeof p);
    int ok = cont ? mdlive_load(p) : mdlive_boot();
    if (!ok) { md_live = 0; have_save = profile_load(); phase = PH_TITLE; return; }
    live_resolve = 0; live_prev_state = mdlive_state(); live_buttons = 0;
    mdlive_frame(0, live_img);
    phase = PH_MDLIVE;
}

static void begin_race_r(int course, int r);
static void begin_race(int course) { begin_race_r(course, race_for_course(course)); }
static void begin_race_r(int course, int r) {
    launch_course = course; launch_step = 0; launch_timer = 0;
    md_race = -1; md_laps = 0; launch_class = course;
    if (mdcourse_available()) {
        mdcourse_t ci;
        if (mdcourse_info(r, &ci) && mdcourse_install(r)) { md_race = r; md_laps = ci.laps; launch_class = 2; }   /* Senior: enough stages for any lap count */
    }
    if (md_live && mdsnd_ready()) mdsnd_command(0xb0);   /* the Mega Drive screens' music ends with the race set-up */
    racing = 0; race_done = 0; race_retired = 0; race_frames = 0; crashes = 0; brake_frames = 0; back_held = 0;
    gap_timer = 0; stage_prev = 1;
    memcpy(hiscore_backup, M->workram + 0x6400, sizeof hiscore_backup); hiscore_saved = 1;
    phase = PH_LAUNCH;
}

static void finish_race(void) {
    if (md_live) {   /* the Mega Drive works out the result (prize, wins, rival, wear) from the time */
        live_time_frames = (fin_cross_f > 0 && !race_retired) ? fin_cross_f : race_frames; live_retired = race_retired;
        live_resolve = 1; phase = PH_MDLIVE; md_grip_q8 = 256;
        if (mdsnd_ready()) mdsnd_command(0xb0);   /* the race song ends; the Mega Drive's results take over */
        return;
    }
    long t = race_frames;
    result_time = race_retired ? 0 : t;
    result_rival = rival_frames(launch_course);
    result_win = !race_retired && t < rival_frames(launch_course);
    long p = race_prize(launch_course);
    result_prize = result_win ? p : (race_retired ? 0 : p / 10);   /* sponsor pays something for finishing */
    pf.money += result_prize;
    if (result_win) {
        pf.wins++;
        if (pf.rival[launch_course] < 4) pf.rival[launch_course]++;
        if (pf.rival[launch_course] == 4 && pf.unlocked < 4 && launch_course + 1 >= pf.unlocked) pf.unlocked = launch_course + 2 > 4 ? 4 : launch_course + 2;
    } else pf.losses++;
    if (!race_retired && (!pf.best[launch_course] || t < pf.best[launch_course])) pf.best[launch_course] = t;
    /* wear */
    int secs = race_frames / 60, len = secs / 90;
    memset(wear_d, 0, sizeof wear_d);
    wear_d[P_ENGINE] = 1 + len / 2;
    wear_d[P_OIL] = 2 + len / 2;
    wear_d[P_MUFFLER] = 1;
    wear_d[P_TIRE] = 1 + len / 2 + crashes / 2;
    wear_d[P_BRAKE] = 1 + brake_frames / 900;
    wear_d[P_FRAME] = crashes / 2;
    for (int i = 0; i < NPART; i++) { if (pf.cond[i] - wear_d[i] < 0) wear_d[i] = pf.cond[i]; pf.cond[i] -= wear_d[i]; }
    profile_save();
    result_ready = 0; phase = PH_RESULT;
    md_grip_q8 = 256;
}

/* effective part values (grade + condition) */
static float cq(int p) { return pf.cond[p] < 0 ? 0.0f : pf.cond[p] / 15.0f; }
static float accel_factor(void) {
    float a = eng_accel[pf.part[P_ENGINE]] + muf_accel[pf.part[P_MUFFLER]] * cq(P_MUFFLER) + oil_accel[pf.part[P_OIL]] * cq(P_OIL);
    return a * (0.85f + 0.15f * cq(P_ENGINE)) * (0.95f + 0.05f * cq(P_OIL));
}
static float brake_factor(void) { return brk_power[pf.part[P_BRAKE]] * (0.80f + 0.20f * cq(P_BRAKE)); }
static float push_factor(void) { return tire_push[pf.part[P_TIRE]] * (1.0f + 0.35f * (1.0f - cq(P_TIRE))); }

/* ------------------------------------------------------------------ */
/* steering: the Mega Drive virtual handlebar                          */
/* ------------------------------------------------------------------ */
static int in_original_race(void) { return cfg_enh && (phase == PH_RACE || phase == PH_LAUNCH); }
int md_test_direct_steer;   /* test harness only: bypass the handlebar (the scripted autopilot can't drive with inertia) */
/* The Mega Drive's own steering ($9842-$990E): it keeps a virtual steering potentiometer ($C73C, $80 = centre,
   $3A-$C6) and moves it from the d-pad with three rates from its table at $A038, picked by the frame part
   ($B618; row 6 outside Original Mode): [0] back to centre when released, [1] steering further in,
   [2] steering from the other side; from frame 2 up each rate gains speed/128. Released, it steps towards
   $80 without stopping exactly there (it settles inside the dead zone $6C-$94). The game runs this once
   every 3 frames, which is what makes the bike straighten up so gradually. Its value is then read like
   the arcade's: dead zone, (v-$80)*256/$8C; the arcade divides by $90, so the potentiometer is scaled. */
int md_steer_adc(float s, int *adc) {
    int orig = in_original_race();
    if (md_test_direct_steer || (!cfg_ctl && !orig)) return 0;
    int row = orig ? pf.part[P_FRAME] : 6;
    if (row < 0 || row > 6) row = 6;
    if (++md_pot_tick >= 3) {
        md_pot_tick = 0;
        int r0 = frame_rates[row][0], r1 = frame_rates[row][1], r2 = frame_rates[row][2];
        if (row >= 2) { int sp = be16r(M->subram, 0x78) >> 7; r0 += sp; r1 += sp; r2 += sp; }
        int left = s < -0.3f, right = s > 0.3f, v = md_pot;
        if (left) { if (v >= 0x80) { v += r1; if (v >= 0xc7) v = 0xc6; } else v += r2; }            /* left raises it */
        else if (right) { if (v < 0x80) { v -= r1; if (v < 0x3a) v = 0x3a; } else v -= r2; }
        else if (v != 0x80) v += v > 0x80 ? -r0 : r0;
        if (v > 0xc6) v = 0xc6; if (v < 0x3a) v = 0x3a;
        md_pot = v;
    }
    int a = 0x80 + (md_pot - 0x80) * 0x90 / 0x8c;
    if (md_pot >= 0x6c && md_pot <= 0x94) a = 0x80;                         /* the Mega Drive's dead zone */
    *adc = a;
    hb = (float)(0x80 - a);
    return 1;
}

/* ------------------------------------------------------------------ */
/* input                                                               */
/* ------------------------------------------------------------------ */
static void neutral(sho_input *in) {
    in->steer = 0; in->throttle = 0; in->brake = 0; in->coin = 0; in->start = 0; in->turbo = 0;
    in->pad_a = 0; in->pad_b = 0; in->nav_up = in->nav_down = in->nav_left = in->nav_right = 0;
}

void sho_video_arcade_text(uint32_t *fb, int fbw, int x, int y, const char *s, int color);
int sho_video_title_recent(void);
extern int sho_video_hide_title_text;
static int modesel_on_title;   /* the choice sits under the title logo, where INSERT COINS flashes */
static void menu_logic(void);
void md_filter_input(sho_input *in) {
    if (!md_enabled) return;
    if (msg_timer > 0) msg_timer--;
    if (!cfg_enh) return;
    int start_edge = 0; { static int prev; start_edge = in->start && !prev; prev = in->start; }
    read_menu_keys(in);
    sho_video_hide_title_text = phase == PH_MODESEL && modesel_on_title;
    switch (phase) {
    case PH_ARCADE:
        if (arcade_pass > 0) arcade_pass--;
        if (arc_t > 0) {   /* Arcade chosen: one credit, then the arcade's own class (map) select */
            if (select_active() || playing()) arc_t = 0;
            else { arc_t++; if (arc_t <= 6) in->coin = 1; else if (arc_t >= 50 && arc_t < 53) in->start = 1; else if (arc_t > 140) arc_t = 7; return; }
        }
        if (inject_start > 0) { inject_start--; in->start = 1; return; }
        /* Start on the attract screens opens the mode selection; everywhere else it goes to the game */
        if (arcade_ok && game_state() == 2 && playing()) arcade_ok = 0;      /* that arcade game is under way */
        { int sel = select_active(), appeared = sel && !select_prev; select_prev = sel;
          /* free play: the class select comes up by itself - offer the mode choice there */
          if (appeared && !arcade_ok && !playing()) { phase = PH_MODESEL; modesel_on_title = 0; cur_modesel = 0; held[4] = in->pad_a || in->start; in->start = 0; return; } }
        if (start_edge && arcade_pass == 0 && machine_idle()) { phase = PH_MODESEL; modesel_on_title = sho_video_title_recent(); cur_modesel = 0; in->start = 0; held[4] = 1; }
        else if (in->start && phase == PH_ARCADE && !arcade_pass && machine_idle()) in->start = 0;
        return;
    case PH_LAUNCH: {
        sho_input keep = *in; neutral(in);
        if (keep.pad_b && launch_step < 4 && !md_live) { phase = PH_MENU; return; }
        launch_timer++;
        switch (launch_step) {
        case 0: if (select_active() && !playing() && launch_timer > 10) { launch_step = 3; launch_timer = 0; }   /* already on the class select */
                else if (machine_idle() && launch_timer > 10) { launch_step = 1; launch_timer = 0; } break;
        case 1: in->coin = launch_timer < 5;   /* with a credit the arcade goes to the class select by itself */
                if (launch_timer > 8 && select_active()) { launch_step = 3; launch_timer = 0; }
                else if (launch_timer > 40) { launch_step = 2; launch_timer = 0; } break;
        case 2: in->start = launch_timer < 2;   /* short press: a held Start would also confirm the class */
                if (select_active()) { launch_step = 3; launch_timer = 0; }
                else if (launch_timer > 120) { launch_step = 1; launch_timer = 0; }
                break;
        case 3: {   /* class select: step to the course, then Start */
            int cls = be16r(M->subram, 0x12) & 3;     /* class select cursor ($260012) */
            if (cls != launch_class) { if (launch_timer % 10 == 0 && !sho_menu_step) sho_menu_step = ((launch_class - cls) & 3) <= 2 ? 1 : -1; }
            else if (launch_timer > 20) { launch_step = 4; launch_timer = 0; }
            break; }
        case 4: in->start = launch_timer < 2; if (launch_timer > 40) { if (md_live) { launch_step = 5; launch_timer = 0; } else phase = PH_RACE; } break;
        case 5: {   /* live Mega Drive: its music choice ($FF0516) on the arcade's music select */
            if (!select_active()) { if (launch_timer > 300) phase = PH_RACE; break; }
            int m = be16r(M->workram, 0x41a) & 3;
            if (m != live_music) { if (launch_timer % 10 == 0 && !sho_menu_step) sho_menu_step = ((live_music - m) & 3) <= 2 ? 1 : -1; }
            else if (launch_timer > 20) { launch_step = 6; launch_timer = 0; }
            break; }
        case 6: in->start = launch_timer < 2; if (launch_timer > 10) phase = PH_RACE; break;
        }
        return; }
    case PH_FINISH:   /* the bike is no longer ours to drive */
        in->throttle = 0; in->brake = 0; in->turbo = 0; in->steer = 0; in->coin = 0; in->start = 0; in->pad_a = 0; in->pad_b = 0;
        return;
    case PH_RACE:
        if (!racing) return;   /* music select and countdown: player in control */
        { int c = in->coin; in->coin = 0; in->pad_b = c; }
        if (pf.part[P_ENGINE] < 3) in->turbo = 0;                  /* turbo needs a turbo engine */
        back_held = in->pad_b ? back_held + 1 : 0;   /* hold Back (coin button) to retire */
        if (back_held == 90) { race_retired = 1; M->workram[0x500] = 0; M->workram[0x501] = 1; finish_race(); }
        return;
    default:
        break;
    }
    if (phase == PH_MDLIVE) {   /* the pad goes to the Mega Drive: A = its C (select), B = its A, X/turbo = its B */
        unsigned b = 0;
        if (in->nav_up) b |= 1; if (in->nav_down) b |= 2; if (in->nav_left) b |= 4; if (in->nav_right) b |= 8;
        if (in->pad_b) b |= 16; if (in->turbo) b |= 32; if (in->pad_a) b |= 64; if (in->start) b |= 128;
        live_buttons = b;
        neutral(in);
        return;
    }
    /* Original Mode screens: the machine gets nothing */
    neutral(in);
    menu_logic();
}

/* ------------------------------------------------------------------ */
/* menus                                                               */
/* ------------------------------------------------------------------ */
static void menu_logic(void) {
    static int last_phase;
    if (phase != last_phase) { msg_timer = 0; last_phase = phase; }
    switch (phase) {
    case PH_MODESEL:
        if (getenv("LIVEDBG")) fprintf(stderr, "modesel %ld ok %d held %d\n", (long)sho_frame_no, k_ok, held[4]);
        if (k_up) cur_modesel = (cur_modesel + 2) % 3;
        if (k_down) cur_modesel = (cur_modesel + 1) % 3;
        if (k_back) phase = PH_ARCADE;
        if (k_ok && cur_modesel == 2) { exit_request = 1; break; }
        if (k_ok) { if (cur_modesel == 0) { phase = PH_ARCADE; arcade_ok = 1; if (!select_active()) { arcade_pass = 600; arc_t = 1; } } else start_original(); }
        break;
    case PH_TITLE: {
        int n = have_save ? 3 : 2;
        if (k_up) cur_title = (cur_title + n - 1) % n;
        if (k_down) cur_title = (cur_title + 1) % n;
        if (k_back) { phase = PH_ARCADE; reset_request = 1; }
        if (k_ok) {
            int item = have_save ? cur_title : cur_title + 1;   /* 0 continue, 1 new game, 2 back */
            if (item == 0) { if (md_live) live_open(1); else { phase = PH_MENU; cur_menu = 0; } }
            else if (item == 1) { if (have_save) { phase = PH_CONFIRM_NEW; cur_title = 0; } else if (md_live) live_open(0); else { profile_new(); profile_save(); phase = PH_MENU; cur_menu = 0; } }
            else { phase = PH_ARCADE; reset_request = 1; }
        }
        break; }
    case PH_CONFIRM_NEW:
        if (k_left || k_right || k_up || k_down) cur_title ^= 1;
        if (k_back) { phase = PH_TITLE; cur_title = 0; }
        if (k_ok) { if (cur_title) { if (md_live) live_open(0); else { profile_new(); profile_save(); phase = PH_MENU; cur_menu = 0; } } else { phase = PH_TITLE; cur_title = 0; } }
        break;
    case PH_MENU:
        if (k_up) cur_menu = (cur_menu + 3) % 4;
        if (k_down) cur_menu = (cur_menu + 1) % 4;
        if (k_left && sel_course > 0) sel_course--;
        if (k_right && sel_course + 1 < pf.unlocked) sel_course++;
        if (k_ok) switch (cur_menu) {
            case 0: phase = PH_PARTS; cur_part = 0; for (int i = 0; i < NPART; i++) browse[i] = pf.part[i]; break;
            case 1: phase = PH_MECH; cur_mech = 0; break;
            case 2: phase = PH_COURSE; if (sel_course >= pf.unlocked) sel_course = pf.unlocked - 1; break;
            case 3: profile_save(); phase = PH_ARCADE; reset_request = 1; break;
        }
        break;
    case PH_PARTS: {
        int p = cur_part;
        if (k_up) cur_part = (cur_part + NPART - 1) % NPART;
        if (k_down) cur_part = (cur_part + 1) % NPART;
        if (k_left && browse[p] > 0) browse[p]--;
        if (k_right && browse[p] + 1 < ngrades[p]) browse[p]++;
        if (k_back) phase = PH_MENU;
        if (k_ok) {
            int g = browse[p];
            if (g == pf.part[p]) say("ALREADY FITTED");
            else if (pf.money < grades[p][g].price) say("NOT ENOUGH MONEY");
            else { pf.money -= grades[p][g].price; pf.part[p] = g; pf.cond[p] = 15; profile_save(); say("FITTED. TOP CONDITION!"); }
        }
        break; }
    case PH_MECH:
        if (k_up) cur_mech = (cur_mech + NPART) % (NPART + 1);
        if (k_down) cur_mech = (cur_mech + 1) % (NPART + 1);
        if (k_back) phase = PH_MENU;
        if (k_ok) {
            if (cur_mech < NPART) {
                long c = repair_cost(cur_mech);
                if (pf.cond[cur_mech] == 15) say("THAT PART IS FINE");
                else if (pf.money < c) say("NOT ENOUGH MONEY");
                else { pf.money -= c; pf.cond[cur_mech] = 15; profile_save(); say("ALL FIXED!"); }
            } else {
                long tot = 0; for (int i = 0; i < NPART; i++) tot += pf.cond[i] < 15 ? repair_cost(i) : 0;
                if (!tot) say("THE MACHINE IS IN TOP CONDITION");
                else if (pf.money < tot) say("NOT ENOUGH MONEY");
                else { pf.money -= tot; for (int i = 0; i < NPART; i++) pf.cond[i] = 15; profile_save(); say("ALL FIXED!"); }
            }
        }
        break;
    case PH_COURSE:
        if (k_left && sel_course > 0) sel_course--;
        if (k_right && sel_course + 1 < pf.unlocked) sel_course++;
        if (k_back) phase = PH_MENU;
        if (k_ok) begin_race(sel_course);
        break;
    case PH_RESULT:
        if (result_ready && k_ok) { phase = PH_MENU; cur_menu = 2; }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* per frame                                                           */
/* ------------------------------------------------------------------ */
static uint32_t *FB; static int FW, XO;
static void px(int x, int y, uint32_t c);
static void md_race_hud(void);
static void mt(int col, int row, const char *s, int pal);
/* ---- the Mega Drive's damage model, from its program (Original Mode only) ----
   a fall ($AC3A, once per fall): frame -3, engine -1, brake -2, muffler -2; frame, engine or tire below 0 =
   broken: retire ($AC60). At 180 km/h and over ($A0BA) an engine at 0 breaks, and an empty oil breaks it too.
   A brake at 0 breaks when used ($A6D2); a muffler or tire at 0 wears through after a while ($A5D8, $A7EE). */
static const char *const broken_msg[NPART] = { "YOUR FRAME IS BROKEN!", "YOUR ENGINE IS BROKEN!", "YOUR BRAKE IS BROKEN!",
                                               "YOUR MUFFLER IS BROKEN!", "YOUR OIL IS EMPTY!", "YOUR TIRE IS BROKEN!" };
static void md_crash_damage(void) {
    pf.cond[P_FRAME] -= 3; pf.cond[P_ENGINE] -= 1; pf.cond[P_BRAKE] -= 2; pf.cond[P_MUFFLER] -= 2;
    if (getenv("LIVEDBG")) fprintf(stderr, "crash: frame %d engine %d brake %d muffler %d\n", pf.cond[0], pf.cond[1], pf.cond[2], pf.cond[3]);
}
/* a bump against another bike ($D53A, Original Mode only): muffler -1 and frame -1. The arcade engine has no
   separate bump flag, so a bump is a sudden speed loss (8 or more in a frame) without a fall or braking. */
static int bump_spd, bump_cool, bumps;
static void md_bump_check(int fell) {
    int sp = be16r(M->subram, 0x78);
    if (bump_cool > 0) bump_cool--;
    else if (!fell && bump_spd - sp >= 8 && be16r(M->subram, 0x70) <= 0x20) {
        pf.cond[P_MUFFLER] -= 1; pf.cond[P_FRAME] -= 1; bump_cool = 60; bumps++;
        if (getenv("LIVEDBG")) fprintf(stderr, "bump: frame %d muffler %d\n", pf.cond[P_FRAME], pf.cond[P_MUFFLER]);
    }
    bump_spd = sp;
}
static int md_damage_frame(void) {   /* 1 = the race is over */
    if (broken_part >= 0) {
        if (broken_part == P_BRAKE || broken_part == P_MUFFLER) { if (++broken_t > 180) broken_part = -1; return 0; }   /* a warning only */
        speed_set(speed_get() > 0x30000 ? speed_get() - 0x30000 : 0);          /* the bike rolls to a stop */
        if (++broken_t > 240) { race_retired = 1; finish_race(); M->workram[0x500] = 0; M->workram[0x501] = 1; return 1; }
        return 0;
    }
    int kmh = be16r(M->subram, 0x78) * 324 / 279;
    if (kmh >= 180) {
        if (pf.cond[P_ENGINE] == 0) pf.cond[P_ENGINE] = -1;
        if (pf.cond[P_OIL] == 0) { pf.cond[P_ENGINE] = -1; pf.cond[P_OIL] = -1; }
    }
    if (pf.cond[P_BRAKE] == 0 && be16r(M->subram, 0x70) > 0x20) { pf.cond[P_BRAKE] = -1; broken_part = P_BRAKE; broken_t = 0; }
    if (pf.cond[P_MUFFLER] == 0 && ++wear_t[P_MUFFLER] > 600) { pf.cond[P_MUFFLER] = -1; broken_part = P_MUFFLER; broken_t = 0; }
    if (pf.cond[P_TIRE] == 0 && ++wear_t[P_TIRE] > 600) pf.cond[P_TIRE] = -1;
    for (int p = 0; p < NPART; p++) if ((p == P_FRAME || p == P_ENGINE || p == P_TIRE) && pf.cond[p] < 0) { broken_part = p; broken_t = 0; break; }
    return 0;
}
static void live_write_parts(void) { for (int i = 0; i < NPART; i++) mdlive_w16(0xb648 + i * 2, (unsigned)(pf.cond[i] < -1 ? -1 : pf.cond[i]) & 0xffff); }

/* ---- the goal ----
   the picture stops on the crossing, the bike rides away and shrinks into the distance, the lap time stays
   up (SPEED goes), then the screen fades to black and the Mega Drive's results follow */
#define FIN_RIDE 170
#define FIN_FADE 40
#define FIN_HOLD 20
static int fin_hz;
static int fin_valid; static float fin_a, fin_yv; static float fin_c[SHO_H];   /* road: half width = a * (y - yv), centre per line */
/* The road's perspective, measured from the picture itself (the road colours of the course): per scanline the
   outer edges of the road; half width is linear in the line below the vanishing row, which gives the exact horizon. */
int sho_video_road_extent(int y, int *l, int *r);
static void finish_road_profile(const uint32_t *img, int w) {
    (void)img;
    fin_valid = 0;
    static int L[SHO_H], R[SHO_H];
    int n = 0; double sy = 0, sw = 0, syy = 0, syw = 0;
    for (int y = 0; y < SHO_H; y++) {
        L[y] = R[y] = -1;
        if (y < fin_hz) continue;
        int l, r; if (!sho_video_road_extent(y, &l, &r)) continue;
        L[y] = l; R[y] = r;
        if (L[y] > 0 && R[y] < w - 1 && R[y] - L[y] > 6) {   /* a whole road line, not cut by the screen sides */
            double hw = (R[y] - L[y]) / 2.0; n++; sy += y; sw += hw; syy += (double)y * y; syw += hw * y;
        }
    }
    if (n < 12) return;
    double den = n * syy - sy * sy; if (den < 1) return;
    double a = (n * syw - sy * sw) / den, b = (sw - a * sy) / n;   /* hw = a*y + b */
    if (a < 0.2) return;
    fin_a = (float)a; fin_yv = (float)(-b / a);
    /* centre per line: measured where valid, extended (nearest valid line) elsewhere, lightly smoothed */
    int last = -1; for (int y = 0; y < SHO_H; y++) { if (L[y] >= 0 && R[y] > L[y]) { fin_c[y] = (L[y] + R[y]) / 2.0f; last = y; } else fin_c[y] = -1; }
    if (last < 0) return;
    int first = -1; for (int y = 0; y < SHO_H; y++) if (fin_c[y] >= 0) { first = y; break; }
    for (int y = 0; y < first; y++) fin_c[y] = fin_c[first];
    for (int y = first; y < SHO_H; y++) if (fin_c[y] < 0) fin_c[y] = fin_c[y - 1];
    static float t[SHO_H]; for (int y = 0; y < SHO_H; y++) { float s = 0; int c = 0; for (int k = -3; k <= 3; k++) { int yy = y + k; if (yy >= 0 && yy < SHO_H) { s += fin_c[yy]; c++; } } t[y] = s / c; }
    for (int y = 0; y < SHO_H; y++) fin_c[y] = t[y];
    fin_valid = 1;
}
static int finish_begin(void) {
    int w = sho_video_width();
    if (!fin_bg) { fin_bg = malloc(SHO_MAX_W * SHO_H * 4); fin_bike = malloc(SHO_MAX_W * SHO_H * 4); }
    if (!fin_bg || !fin_bike) return 0;
    sho_video_render(fin_bike);                                 /* this frame as it is */
    sho_video_hide_player = 1; sho_video_render(fin_bg); sho_video_hide_player = 0;   /* and without the bike */
    fin_w = w; fin_x0 = w; fin_y0 = SHO_H; fin_x1 = -1; fin_y1 = -1;
    for (int y = 0; y < SHO_H; y++) for (int x = 0; x < w; x++) {
        int i = y * w + x;
        if (fin_bike[i] != fin_bg[i]) { if (x < fin_x0) fin_x0 = x; if (x > fin_x1) fin_x1 = x; if (y < fin_y0) fin_y0 = y; if (y > fin_y1) fin_y1 = y; }
        else fin_bike[i] = 0xff000000u;                                       /* not the bike: transparent */
    }
    fin_hz = sho_video_horizon();
    finish_road_profile(fin_bg, w);
    if (getenv("FINDBG")) fprintf(stderr, "finish: horizon %d valid %d a %.3f yv %.1f bike x %d-%d y %d-%d\n", fin_hz, fin_valid, fin_a, fin_yv, fin_x0, fin_x1, fin_y0, fin_y1);
    finish_t = 1; phase = PH_FINISH;
    return 1;
}
int md_frozen(void) { return phase == PH_MODESEL && modesel_on_title; }   /* the mode choice holds the title screen still (the goal sequence runs the arcade on) */
extern uint16_t sho_video_extra_sprites[4][6]; extern int sho_video_nextra;
/* The other bikes on the road are single hardware sprites: pre-drawn art in four sizes (71, 35, 17 and 8 lines tall,
   each half the one before) and the sprite chip's zoom (up to 2x) between them. Our bike goes through the same chip with
   the same art (the player's pitch-9 art is the biggest size), so it shrinks the way theirs do. */
/* the rival bikes' own art set and zoom rule (logged from the arcade's sprite list as they pull away
   from the start): flipped art, h = Ha*32/(32+z) with the same z both ways, art changes at 36/18/10 lines,
   wheel palette toggles every 4 frames, height per road line ~0.75 x (bottom - vanishing line) */
static const struct { int h, d2, addr; } bike_tier[4] = { { 71, 0x109, 0xffff }, { 35, 0x105, 0x0287 }, { 17, 0x103, 0x033b }, { 8, 0x102, 0x0371 } };
static float fin_inv;   /* distance, in units of our bike's distance when it crossed the line */
/* (bottom, height) of the rival bikes over the last few seconds: their size rule h = k * (bottom - y0) */
static short rv_bot[512], rv_h[512]; static int rv_n, rv_w;
static void rival_sample(void) {
    for (int i = 0; i < 0x800; i += 8) {
        unsigned bse = (unsigned)i * 2;
        uint16_t d0 = be16r(M->sprram, bse), d2 = be16r(M->sprram, bse + 4), d3 = be16r(M->sprram, bse + 6), d4 = be16r(M->sprram, bse + 8);
        if (d2 & 0x8000) break; if (d2 & 0x4000) continue;
        if (!(d2 & 0x100) || !(d3 == 0xffff || d3 == 0x0287 || d3 == 0x033b)) continue;
        if ((d4 & 0xfe) == 0xc0 || (d4 & 0x0f00)) continue;    /* our own bike; other banks are scenery */
        int top = d0 & 0xff, bot = d0 >> 8;
        if (bot - top < 9 || bot >= 200) continue;
        rv_bot[rv_w] = (short)bot; rv_h[rv_w] = (short)(bot - top); rv_w = (rv_w + 1) & 511; if (rv_n < 512) rv_n++;
    }
}
static int rival_fit(float *k, float *y0) {   /* least squares, then again without the outliers */
    float kk = 0, yy = 0; int ok = 0;
    for (int pass = 0; pass < 2; pass++) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0, lo = 999, hi = -999;
        for (int i = 0; i < rv_n; i++) {
            float x = rv_bot[i], y = rv_h[i];
            if (pass && (y - kk * (x - yy) > 3 || y - kk * (x - yy) < -3)) continue;
            sx += x; sy += y; sxx += x * x; sxy += x * y; n++; if (x < lo) lo = (int)x; if (x > hi) hi = (int)x;
        }
        double d = n * sxx - sx * sx;
        if (n < 8 || hi - lo < 12 || d <= 0) return ok;
        double m = (n * sxy - sx * sy) / d, c = (sy - m * sx) / n;
        if (m < 0.5 || m > 1.1) return ok;
        kk = (float)m; yy = (float)(-c / m); ok = 1;
    }
    *k = kk; *y0 = yy; return ok;
}
static float fin_rk, fin_ry0; static int fin_rok;
static void finish_bike_sprite(void) {
    sho_video_nextra = 0;
    if (finish_t > FIN_RIDE || fin_x1 < fin_x0) return;
    if (finish_t <= 1) { fin_inv = 1.0f; fin_rok = rival_fit(&fin_rk, &fin_ry0);
        if (getenv("FINDBG")) fprintf(stderr, "rival fit ok %d k %.3f y0 %.1f n %d\n", fin_rok, fin_rk, fin_ry0, rv_n); }
    else { float v = 0.03f + 0.0005f * finish_t; if (v > 0.06f) v = 0.06f; fin_inv += v; }   /* pulls away like the rivals off the start */
    float sc = 1.0f / fin_inv;
    float cx0 = (fin_x0 + fin_x1) / 2.0f, by0 = (float)fin_y1, cx, by, yv;
    if (fin_valid) {
        yv = fin_yv;
        int yb = (int)by0; if (yb < 0) yb = 0; if (yb >= SHO_H) yb = SHO_H - 1;
        float hw0 = fin_a * (by0 - fin_yv); if (hw0 < 1.0f) hw0 = 1.0f;
        float k = (cx0 - fin_c[yb]) / hw0;
        by = fin_yv + sc * (by0 - fin_yv);
        int yy = (int)by; if (yy < 0) yy = 0; if (yy >= SHO_H) yy = SHO_H - 1;
        cx = fin_c[yy] + k * hw0 * sc;
    } else { float vx = fin_w / 2.0f; yv = (float)fin_hz; cx = vx + (cx0 - vx) * sc; by = yv + (by0 - yv) * sc; }
    /* the rivals' own rule: they close on y0 (a little above the road's vanishing line), h = kr * (bottom - y0) */
    float y0 = fin_rok ? fin_ry0 : yv - 0.06f * (by0 - yv), kr = fin_rok ? fin_rk : 0.75f;
    if (y0 > by0 - 30) y0 = by0 - 30;
    by = y0 + sc * (by0 - y0);
    if (fin_valid) { int yy = (int)by; if (yy < 0) yy = 0; if (yy >= SHO_H) yy = SHO_H - 1;
        int yb = (int)by0; if (yb < 0) yb = 0; if (yb >= SHO_H) yb = SHO_H - 1;
        float hw0 = fin_a * (by0 - fin_yv); if (hw0 < 1.0f) hw0 = 1.0f;
        float hw = fin_a * (by - fin_yv); if (hw < 0) hw = 0;
        cx = fin_c[yy] + (cx0 - fin_c[yb]) / hw0 * hw; }
    int bottom = (int)(by + 0.5f);
    /* starts from our own bike's size and blends into the rivals' size for the line */
    float k0 = 71.0f / (by0 - y0), u = (1.0f - sc) / 0.3f; if (u > 1) u = 1;
    float ht = (k0 + (kr - k0) * u) * ((float)bottom - y0);
    if (ht < 6.5f) return;
    int t = ht >= 35.5f ? 0 : ht >= 17.5f ? 1 : ht >= 9.5f ? 2 : 3;
    int Ha = bike_tier[t].h, z = (int)(32.0f * Ha / ht - 32.0f + 0.5f);
    if (z < 0) z = 0; if (z > 31) z = 31;
    int h = Ha * 32 / (32 + z), pitch = bike_tier[t].d2 & 0xff;
    int w = pitch * 4 * (64 - z) / 64; if (w < 1) w = 1;
    int xo = (sho_video_width() - SHO_W) / 2, left = (int)(cx - w / 2.0f + 0.5f), top = bottom - h;
    if (top < 0) top = 0;
    uint16_t *e = sho_video_extra_sprites[0];
    e[0] = (uint16_t)((bottom & 0xff) << 8 | (top & 0xff)); e[1] = (uint16_t)(left + 184 - xo);
    e[2] = (uint16_t)bike_tier[t].d2; e[3] = (uint16_t)bike_tier[t].addr;
    e[4] = (uint16_t)(0x00c0 | ((finish_t >> 2) & 1)); e[5] = (uint16_t)(z << 5 | z);
    if (getenv("FINDBG")) fprintf(stderr, "FB t%d bot %d h %d tier %d z %d kr %.3f k0 %.3f y0 %.1f yv %.1f ok %d\n", finish_t, bottom, h, t, z, kr, k0, y0, yv, fin_rok);
    sho_video_nextra = 1;
}
static void finish_step(void) {   /* one frame of the goal sequence: the camera stays, the other bikes keep their own speed */
    speed_set(0); M->workram[0x500] = 0x60;
    sho_video_hide_player = 1;
    finish_bike_sprite(); if (msg_t > 0) msg_t--;
    if (++finish_t > FIN_RIDE + FIN_FADE + FIN_HOLD) {
        sho_video_hide_player = 0; sho_video_nextra = 0;
        finish_t = 0; phase = PH_RACE;
        finish_race(); M->workram[0x500] = 0; M->workram[0x501] = 1;
    }
}
static void finish_draw(void) {
    /* (the live frame stays: the other bikes go on moving; our bike is a hardware sprite added in finish_bike_sprite) */
    if (mdhud && mdhud->ok) md_race_hud();
    if (finish_t > FIN_RIDE) {                                  /* fade to black */
        int k = finish_t - FIN_RIDE; float f = k >= FIN_FADE ? 0.0f : 1.0f - k / (float)FIN_FADE;
        for (int i = 0; i < FW * SHO_H; i++) { uint32_t c = FB[i];
            FB[i] = (uint32_t)(((c >> 16 & 255) * f)) << 16 | (uint32_t)(((c >> 8 & 255) * f)) << 8 | (uint32_t)((c & 255) * f); }
    }
}

/* one frame of the live Mega Drive, and what its state means for us */
static void live_step(void) {
    char sp[1100]; md_save_path(sp, sizeof sp);
    if (live_resolve) {
        /* the Mega Drive goes on into its own race; once it is running, it is told the race is over with
           our time ($C832, BCD m'ss"hh; $FFFFFFFF = retired) and finishes it its own way ($B8B0) */
        int n = 0;
        while (mdlive_state() != 0x18 && n < 3000) { mdlive_frames(1); n++; }
        if (getenv("LIVEDBG")) fprintf(stderr, "resolve: state %x after %d frames\n", mdlive_state(), n);
        mdlive_frames(60);
        uint32_t t = 0xffffffffu;
        if (!live_retired) {
            long cs = live_time_frames * 100 / SHO_FPS; int m = (int)(cs / 6000); if (m > 9) m = 9;
            int sec = (int)(cs / 100 % 60), h = (int)(cs % 100);
            t = (uint32_t)(m << 16 | (sec / 10) << 12 | (sec % 10) << 8 | (h / 10) << 4 | (h % 10));
        }
        if (getenv("LIVEDBG")) fprintf(stderr, "c82c %x c8b6 %x c70e %x 06c0 %x\n", mdlive_r16(0xc82c), mdlive_r16(0xc8b6), mdlive_r16(0xc70e), mdlive_r16(0x06c0));
        /* race-over state ($1C, handler $B8A2): with Original Mode on it copies the time and goes to the results */
        live_write_parts();
        mdlive_w32(0xc832, t); mdlive_w16(0x06c0, 1); mdlive_w16(0xc70e, 1); mdlive_w16(0xc704, 0x1c);
        n = 0; while (mdlive_state() != 0x24 && mdlive_state() != 0x25 && n < 3000) { mdlive_frames(1); n++; }
        live_resolve = 0; live_prev_state = 0;   /* so the command screen saves */
        if (getenv("LIVEDBG")) fprintf(stderr, "resolve: done state %x after %d frames, time %08x\n", mdlive_state(), n, t);
        mdsnd_resync();   /* the Mega Drive ran ahead of its sound: restart the driver on the results screen's music */
        reset_request = 2;   /* a clean arcade machine behind the Mega Drive's screens (instead of hurrying it through its own
                                goal and name entry at 6 frames a frame, which starved the Mega Drive's music of time) */
    }
    if (cheat_req) {   /* only on the command screen (the Mega Drive's grades $B618 and condition $B648, money $B638, then saved) */
        if (!live_resolve && mdlive_state() == 0x25) {
            if (cheat_req & 1) { for (int i = 0; i < NPART; i++) { mdlive_w16(0xb618 + i * 2, (unsigned)(ngrades[i] - 1)); mdlive_w16(0xb648 + i * 2, 15); } cheat_text = "BIKE UPGRADED!"; }
            if (cheat_req & 2) { mdlive_w32(0xb638, 0x99999999u); cheat_text = "MAX MONEY!"; }
            mdlive_save(sp); cheat_msg = 150;
            mdlive_w16(0xc704, 0x24);   /* the command screen sets itself up again, showing the new money and bike */
        }
        cheat_req = 0;
    }
    if (dev_race && !live_resolve) { unsigned s0 = mdlive_state(); if (s0 != 0x11 && s0 != 0x18 && s0 != 0x14) mdlive_w16(0xb606, (unsigned)(dev_race - 1)); }   /* the race the Mega Drive starts ($B606 -> $C85A) */
    mdlive_frame(live_buttons, live_img);
    unsigned st = mdlive_state();
    if (st == 0x25 && live_prev_state != 0x25) mdlive_save(sp);              /* back on the command screen: save */
    if (st == 0x11) {                                                        /* race chosen (music picked) */
        int r = (int)mdlive_r16(0xc85a); if (r < 0 || r > 6) r = 0;
        for (int i = 0; i < NPART; i++) {                                     /* parts and condition for the physics */
            int g = (int)mdlive_r16(0xb618 + i * 2), c = (int16_t)mdlive_r16(0xb648 + i * 2);
            pf.part[i] = g < 0 ? 0 : g >= ngrades[i] ? ngrades[i] - 1 : g; pf.cond[i] = c < 0 ? 0 : c > 15 ? 15 : c;
        }
        live_music = (int)mdlive_r16(0x0516) & 3;
        for (int w = 0; w < 120 && !mdlive_r32(0xc864); w++) mdlive_frames(1);   /* the Mega Drive builds its rival table a moment after choosing the race */
        {   uint32_t rt = mdlive_r32(0xc83a);   /* the rival's time, BCD m'ss"hh */
            long m = (rt >> 16) & 15, sec = ((rt >> 12) & 15) * 10 + ((rt >> 8) & 15), h = ((rt >> 4) & 15) * 10 + (rt & 15);
            live_rival = (m * 6000 + sec * 100 + h) * SHO_FPS / 100; }
        if (getenv("LIVEDBG")) { fprintf(stderr, "race %d music %d rival %ld parts", r, live_music, live_rival); for (int i = 0; i < NPART; i++) fprintf(stderr, " %d/%d", (int)mdlive_r16(0xb618 + i * 2), (int16_t)mdlive_r16(0xb648 + i * 2)); fprintf(stderr, "\n"); }
        begin_race_r(r / 2 > 3 ? 3 : r / 2, r);
    } else if (st < 0x08 && live_prev_state >= 0x08) { mdlive_stop(); md_live = 0; phase = PH_ARCADE; reset_request = 1; md_sound_release(); }   /* END: back to the title screen */
    live_prev_state = st;
}

void md_pre_frame(void) {
    if (!cfg_enh) return;
    if (phase == PH_MDLIVE) live_step();
    if (phase == PH_FINISH) finish_step();
    if (overlay_phase(phase) && select_active() && !playing()) { M->workram[0x500] = 0x09; return; }   /* hold the class-select countdown behind our screens */
    if (phase != PH_RACE || !racing) return;
    M->workram[0x500] = 0x60;            /* hold the arcade time limit: the race is against the rival */
}

extern const mdscene_t *sho_md_scene;
int sho_md_hold_music(void) { return cfg_enh && (phase == PH_RACE || phase == PH_LAUNCH); }
/* Original Mode: the song starts with the race, not on the music select's preview */
static int snd_pending = -1; static long snd_pending_f;
static int is_track_cmd(uint8_t c) { return c == 0x9d || c == 0x82 || c == 0x96 || c == 0x81; }
static int is_stop_cmd(uint8_t c) { return c == 0x00 || c == 0x83 || c == 0x8e || c == 0x92 || c == 0x9e || c == 0x9f; }
void sho_md_route_sound(uint8_t cmd) {
    if (sho_md_hold_music()) {
        if (is_stop_cmd(cmd)) return;                                  /* the song plays through the race */
        if (is_track_cmd(cmd) && !racing) { snd_pending = cmd; snd_pending_f = sho_frame_no; return; }   /* preview: held */
        if (is_track_cmd(cmd)) snd_pending = -1;
    }
    sho_md_music_command(cmd); sho_music_on_command(cmd);
}
static void route_race_start(void) {   /* the race is running: its own song command, or the held one */
    if (snd_pending < 0) return;
    if (sho_frame_no - snd_pending_f <= 10 || race_frames > 45) {
        uint8_t c = (uint8_t)snd_pending; snd_pending = -1;
        sho_md_music_command(c); sho_music_on_command(c);
    }
}
void md_post_frame(void) {
    static int was_race;
    if (!cfg_enh) { was_race = 0; return; }
    { int r = sho_md_hold_music(); if (was_race && !r) sho_music_stop(); was_race = r; }   /* the arrangement ends with the race */
    if (!sho_md_hold_music()) snd_pending = -1; else if (racing) route_race_start();
    int gs = game_state();
    /* Mega Drive course look while the converted course is on screen (countdown and race) */
    sho_md_scene = (md_race >= 0 && md_race < MD_NRACES && (phase == PH_RACE || phase == PH_LAUNCH || phase == PH_FINISH) && playing() && (gs == 1 || gs == 2)) ? mdscene[md_race] : NULL;
    if (phase == PH_RACE) {
        if (!racing) {
            if (gs == 2 && playing()) { racing = 1; race_frames = 0; spd_last = speed_get(); crash_prev = 0; bump_spd = be16r(M->subram, 0x78); bump_cool = 60; bumps = 0; stage_prev = 1; lap_start_f = 0; lap_last_f = 0; fin_cross_f = 0; rival_laps_init(md_race); rv_n = rv_w = 0; lap_last_d = 0; prev_d = 0; bar_acc = 0; bar_pos = 0; broken_part = -1; broken_t = 0; memset(wear_t, 0, sizeof wear_t); md_grip_q8 = (int)(256 * push_factor()); }
            else if (!playing() && game_state() == 0 && !select_active() && sho_frame_no % 600 == 0) { /* gave up waiting */ }
            return;
        }
        if (gs == 3 || !playing()) { if (gs != 3) race_retired = 1; finish_race(); return; }
        if (gs != 2) return;
        race_frames++;
        int cr = be16r(M->subram, 0x94) != 0; if (cr && !crash_prev) { crashes++; if (md_live) md_crash_damage(); } if (md_live) md_bump_check(cr || crash_prev); crash_prev = cr;
        if (md_live && md_damage_frame()) return;
        if (M->subram[0x70] | M->subram[0x71]) { if (be16r(M->subram, 0x70) > 0x20) brake_frames++; }
        /* parts: acceleration, top speed, braking on top of the arcade's speed integration */
        uint32_t s = speed_get(); int64_t d = (int64_t)s - (int64_t)spd_last; int64_t ns = s;
        if (d > 0) {
            ns = (int64_t)spd_last + (int64_t)(d * accel_factor());
            int64_t cap = (int64_t)eng_top[pf.part[P_ENGINE]] << 16;
            if (ns > cap) ns = spd_last > (uint32_t)cap ? (int64_t)spd_last - 0x4000 : cap;
        } else if (d < 0 && be16r(M->subram, 0x70) > 0x20) {
            ns = (int64_t)spd_last + (int64_t)(d * brake_factor());
        }
        if (ns < 0) ns = 0;
        if (ns != (int64_t)s) speed_set((uint32_t)ns);
        spd_last = (uint32_t)ns;
        /* checkpoints: time gap to the rival's pace */
        int st = be16r(M->workram, 0x438);
        /* Mega Drive course: the last lap is done. The Mega Drive does not stop at the line: the bike runs on to course
           position $20 (the goal gate), speed goes to 0 there and the bike rides away ($9B62: $FF0616 > $1F) */
        if (md_race >= 0 && st > md_laps && stage_prev > md_laps && be16r(M->subram, 0x30) >= 0x20) {
            if (md_live && finish_begin()) return;
            finish_race(); M->workram[0x500] = 0; M->workram[0x501] = 1; return;
        }
        if (st != stage_prev && st > stage_prev && !(md_race >= 0 && st > md_laps)) {   /* (past the last lap the clock just stops at the goal) */
            lap_last_f = race_frames - lap_start_f; lap_start_f = race_frames; lap_last_d = prev_d;
            float rival_at = rival_frames(launch_course) * (float)(st - 1) / (md_race >= 0 ? md_laps : course_stages[launch_course]);
            gap_secs = (race_frames - rival_at) / (float)SHO_FPS; gap_timer = md_race >= 0 ? 0 : 240;
            if (md_race >= 0 && st <= md_laps && st - 2 < 8) {   /* (the last lap has no message: the race is over, $E6BC) */ msg_t = 240; msg_you_cs = (int)(lap_last_f * 100.0 / SHO_FPS + 0.5); msg_rv_cs = rv_lap_cs[st - 2]; }
        }
        prev_d = be16r(M->subram, 0x90);
        {   /* bar: the same step the course position ($260030, the Mega Drive's $FF0616) takes each frame; the
               position goes back to 0 past $800 (a lap), the bar does not ($9E8E) */
            uint32_t p32 = (uint32_t)be16r(M->subram, 0x30) << 16 | be16r(M->subram, 0x32);
            if (p32 >= bar_pos) bar_acc += p32 - bar_pos;
            else if (bar_pos >= 0x07000000u && p32 < 0x01000000u) bar_acc += p32;   /* a lap: the position starts again from 0 */
            uint32_t cap = md_laps ? (uint32_t)md_laps * 0x08000000u - 1 : 0; if (bar_acc > cap) bar_acc = cap;
            bar_pos = p32;
        }
        rival_sample();
        stage_prev = st;
        if (gap_timer > 0) gap_timer--;
        if (msg_t > 0) msg_t--;
        return;
    }
    if (phase == PH_RESULT || overlay_phase(phase)) {
        if (hiscore_saved && machine_ready() && game_state() == 0) {
            mdcourse_uninstall();   /* the arcade's own courses back once its game has ended */
            md_race = -1; sho_md_scene = NULL;
            memcpy(M->workram + 0x6400, hiscore_backup, sizeof hiscore_backup); hiscore_saved = 0;
        }
        if (phase == PH_RESULT && !hiscore_saved && machine_ready()) result_ready = 1;
    }
}

static int launch_hidden(void) {   /* live: the arcade's class and music screens are not shown */
    return md_live && (phase == PH_LAUNCH || (phase == PH_RACE && !racing && (select_active() || (game_state() != 1 && game_state() != 2))));
}
int md_extra_frames(void) {
    if (!cfg_enh) return 0;
    if (launch_hidden()) return 3;
    if (getenv("LIVEDBG") && sho_frame_no % 25 == 0) fprintf(stderr, "fr %ld phase %d step %d gs %d racing %d sel %d hurry %d (hs %d ready %d)\n", (long)sho_frame_no, phase, launch_step, game_state(), racing, select_active(), overlay_phase(phase) && (hiscore_saved || !machine_ready()), hiscore_saved, machine_ready());
    /* behind the results/menu screens: hurry the arcade through its goal and name-entry sequences */
    if (md_live && phase == PH_MDLIVE) return 0;                 /* live: the arcade is reset after each race instead */
    if (overlay_phase(phase) && (hiscore_saved || !machine_ready())) return 5;
    return 0;
}
int md_mute(void) { return cfg_enh && (overlay_phase(phase) || phase == PH_FINISH || launch_hidden()); }

/* ------------------------------------------------------------------ */
/* drawing (arcade text font from the tile ROM)                        */
/* ------------------------------------------------------------------ */
static uint32_t *FB; static int FW, XO;
#define C_YEL 0xf8f800
#define C_WHT 0xf8f8f8
#define C_RED 0xf83820
#define C_GRN 0x38f838
#define C_CYN 0x38e0f8
#define C_ORG 0xf8a000
#define C_GRY 0x8890a0
#define C_PNL 0x081038
#define C_PNL2 0x10205c
static const uint8_t glyph_dollar[8] = { 0x10, 0x7c, 0xd0, 0x7c, 0x16, 0xfc, 0x10, 0x00 };
static const uint8_t glyph_colon[8] = { 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00 };
static const uint8_t glyph_slash[8] = { 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0x00 };
static const uint8_t glyph_comma[8] = { 0, 0, 0, 0, 0, 0x30, 0x30, 0x60 };
static void px(int x, int y, uint32_t c) { if (x >= 0 && x < FW && y >= 0 && y < SHO_H) FB[y * FW + x] = c; }
static void rect(int x, int y, int w, int h, uint32_t c) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) px(i, j, c); }
static void box(int x, int y, int w, int h, uint32_t border) {
    rect(x, y, w, h, C_PNL); rect(x, y, w, 1, border); rect(x, y + h - 1, w, 1, border); rect(x, y, 1, h, border); rect(x + w - 1, y, 1, h, border);
}
static int tile_for(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '!' || ch == '?' || ch == '-' || ch == '.' || ch == '+' || ch == '*' || ch == '&' || ch == '%' || ch == '#') return ch;
    switch (ch) { case '\'': return 0x19; case '"': return 0x1a; case '=': return 0x1b; case '>': return 0x3e; case '<': return 0x3c; case '^': return 0x3a; }
    return -1;
}
static void chr(int x, int y, char ch, uint32_t col, int sc) {
    const uint8_t *g = NULL;
    if (ch == '$') g = glyph_dollar; else if (ch == ':') g = glyph_colon; else if (ch == '/') g = glyph_slash; else if (ch == ',') g = glyph_comma;
    if (g) { for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) if (g[j] >> (7 - i) & 1) rect(x + i * sc, y + j * sc, sc, sc, col); return; }
    int t = tile_for(ch); if (t < 0) return;
    for (int j = 0; j < 8; j++) {
        uint8_t b0 = M->tiles[t * 8 + j], b1 = M->tiles[0x8000 + t * 8 + j], b2 = M->tiles[0x10000 + t * 8 + j];
        for (int i = 0; i < 8; i++) {
            int s = 7 - i, v = ((b2 >> s) & 1) << 2 | ((b1 >> s) & 1) << 1 | ((b0 >> s) & 1);
            if (v == 1) rect(x + i * sc, y + j * sc, sc, sc, 0);
            else if (v >= 2) rect(x + i * sc, y + j * sc, sc, sc, col);
        }
    }
}
static void text(int x, int y, const char *s, uint32_t col) { for (; *s; s++, x += 8) chr(XO + x, y, *s, col, 1); }
static void textc(int y, const char *s, uint32_t col) { text(160 - (int)strlen(s) * 4, y, s, col); }
static void textr(int xr, int y, const char *s, uint32_t col) { text(xr - (int)strlen(s) * 8, y, s, col); }
static void text2(int x, int y, const char *s, uint32_t col) { for (; *s; s++, x += 16) chr(XO + x, y, *s, col, 2); }
static void text2c(int y, const char *s, uint32_t col) { text2(160 - (int)strlen(s) * 8, y, s, col); }
static void bx(int x, int y, int w, int h, uint32_t border) { box(XO + x, y, w, h, border); }
static void money_str(char *o, size_t n, long v) {
    char d[32]; snprintf(d, sizeof d, "%ld", v); int l = (int)strlen(d), k = 0; char t[48];
    for (int i = 0; i < l; i++) { t[k++] = d[i]; if ((l - i - 1) % 3 == 0 && i < l - 1) t[k++] = ','; }
    t[k] = 0; snprintf(o, n, "$%s", t);
}
static void time_str(char *o, size_t n, long frames) {
    if (frames <= 0) { snprintf(o, n, "-'--\"--"); return; }
    long cs = (long)(frames * 100.0 / SHO_FPS + 0.5);
    snprintf(o, n, "%ld'%02ld\"%02ld", cs / 6000, (cs / 100) % 60, cs % 100);
}
static void dim(float k) {
    for (int i = 0; i < FW * SHO_H; i++) {
        uint32_t c = FB[i]; int r = (int)(((c >> 16) & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)((c & 255) * k);
        FB[i] = (uint32_t)(r << 16 | g << 8 | b);
    }
}
static void cursor(int x, int y, int on) { if (on && (sho_frame_no / 16) % 4 != 3) text(x, y, ">", C_YEL); }
static void cond_bar(int x, int y, int c) {
    for (int i = 0; i < 15; i++) {
        uint32_t col = i < c ? (c > 9 ? C_GRN : c > 4 ? C_YEL : C_RED) : 0x202838;
        rect(XO + x + i * 4, y, 3, 7, col);
    }
}
static void footer(const char *s) { textc(212, s, C_GRY); }
static void money_line(int y) { char m[40]; money_str(m, sizeof m, pf.money); text(8, y, "MONEY", C_CYN); textr(152, y, m, C_YEL); }
static void message_line(void) { if (msg_timer > 0) { int w = (int)strlen(msg) * 8 + 16; bx(160 - w / 2, 96, w, 20, C_YEL); textc(102, msg, C_WHT); } }
static void wrap_text(int x, int y, int maxc, const char *s, uint32_t col) {
    char line[64]; int n = 0; const char *w = s;
    while (*w) {
        const char *e = w; while (*e && *e != ' ') e++;
        int wl = (int)(e - w);
        if (n && n + 1 + wl > maxc) { line[n] = 0; text(x, y, line, col); y += 10; n = 0; }
        if (n) line[n++] = ' ';
        memcpy(line + n, w, (size_t)wl); n += wl; w = *e ? e + 1 : e;
    }
    if (n) { line[n] = 0; text(x, y, line, col); }
}

static void draw_status_panel(int c) {
    char b[48], t[24]; int r = cur_rival(c);
    bx(128, 32, 184, 108, C_CYN);
    text(136, 38, "RIVAL", C_CYN);
    snprintf(b, sizeof b, "%s", pf.rival[c] > 3 ? "CHAMPION REMATCH" : "");
    textr(306, 38, b, C_ORG);
    text(136, 50, rivals[c][r].name, C_YEL);
    snprintf(b, sizeof b, "\"%s\"", rivals[c][r].quote); wrap_text(136, 62, 21, b, C_WHT);
    snprintf(b, sizeof b, "%s  %s", course_name[c], class_name[c]); text(136, 86, b, C_GRN);
    text(136, 98, "RIVAL LAP", C_CYN); time_str(t, sizeof t, rival_frames(c)); textr(306, 98, t, C_WHT);
    text(136, 110, "YOUR BEST", C_CYN); time_str(t, sizeof t, pf.best[c]); textr(306, 110, t, C_WHT);
    text(136, 122, "PRIZE", C_CYN); money_str(t, sizeof t, race_prize(c)); textr(306, 122, t, C_YEL);
}

static void draw_menu(void) {
    char b[64];
    text2c(6, "ORIGINAL MODE", C_YEL);
    bx(8, 32, 112, 66, C_CYN);
    text(16, 38, "COMMAND", C_CYN);
    static const char *items[4] = { "PARTS", "MECHANIC", "RACE", "END" };
    for (int i = 0; i < 4; i++) { text(32, 52 + i * 11, items[i], cur_menu == i ? C_YEL : C_WHT); cursor(20, 52 + i * 11, cur_menu == i); }
    bx(8, 104, 112, 36, C_CYN);
    snprintf(b, sizeof b, "WIN %d", pf.wins); text(16, 110, b, C_GRN);
    snprintf(b, sizeof b, "LOSS %d", pf.losses); text(16, 124, b, C_RED);
    draw_status_panel(sel_course);
    if (pf.unlocked > 1) { text(136, 130, "<", sel_course > 0 ? C_YEL : C_GRY); textr(306, 130, ">", sel_course + 1 < pf.unlocked ? C_YEL : C_GRY); textc(130, "COURSE", C_GRY); }
    /* machine summary */
    bx(8, 146, 304, 58, C_CYN);
    money_line(152);
    text(168, 152, "MECHANIC", C_CYN); text(240, 152, "R.MONTOYA", C_WHT);
    int worst = 15, wp = 0; for (int i = 0; i < NPART; i++) if (pf.cond[i] < worst) { worst = pf.cond[i]; wp = i; }
    if (worst >= 13) snprintf(b, sizeof b, "\"TOP CONDITION!\"");
    else if (worst >= 7) snprintf(b, sizeof b, "\"THE %s COULD USE SOME WORK.\"", part_label[wp]);
    else snprintf(b, sizeof b, "\"YOUR %s IS WORN OUT!\"", part_label[wp]);
    text(16, 166, b, worst >= 13 ? C_GRN : worst >= 7 ? C_YEL : C_RED);
    snprintf(b, sizeof b, "%s / %s", grades[P_ENGINE][pf.part[P_ENGINE]].name, grades[P_FRAME][pf.part[P_FRAME]].name); text(16, 180, b, C_GRY);
    snprintf(b, sizeof b, "%s BRAKE / %s TIRE", grades[P_BRAKE][pf.part[P_BRAKE]].name, grades[P_TIRE][pf.part[P_TIRE]].name); text(16, 192, b, C_GRY);
    footer("A SELECT   < > COURSE   B BACK");
}

static void part_effect(int p, int g, char *o, size_t n) {
    switch (p) {
    case P_FRAME: snprintf(o, n, "STEER %d  COUNTER %d", frame_rates[g][1], frame_rates[g][2]); break;
    case P_ENGINE: if (eng_top[g] < 999) snprintf(o, n, "POWER %d%% TOP %dKM", (int)(eng_accel[g] * 100 + .5f), eng_top[g] * 324 / 279);
                   else snprintf(o, n, "POWER %d%% TURBO", (int)(eng_accel[g] * 100 + .5f)); break;
    case P_BRAKE: snprintf(o, n, "BRAKES %d%%", (int)(brk_power[g] * 100 + .5f)); break;
    case P_MUFFLER: snprintf(o, n, "POWER +%d%%", (int)(muf_accel[g] * 100 + .5f)); break;
    case P_OIL: snprintf(o, n, "POWER +%d%%", (int)(oil_accel[g] * 100 + .5f)); break;
    case P_TIRE: snprintf(o, n, "GRIP %d%%", (int)(100.0f / tire_push[g] + .5f)); break;
    }
}
static void draw_parts(void) {
    char b[64], m[32];
    text2c(6, "PARTS", C_YEL);
    for (int p = 0; p < NPART; p++) {
        int y = 28 + p * 26, g = browse[p], sel = p == cur_part, fitted = g == pf.part[p];
        bx(8, y, 304, 25, sel ? C_YEL : C_CYN);
        text(16, y + 4, part_label[p], sel ? C_RED : C_CYN);
        text(96, y + 4, grades[p][g].name, fitted ? C_GRN : C_WHT);
        if (sel && g > 0) text(86, y + 4, "<", C_YEL);
        if (sel && g + 1 < ngrades[p]) text(98 + 8 * (int)strlen(grades[p][g].name), y + 4, ">", C_YEL);
        snprintf(b, sizeof b, "%d/%d", g + 1, ngrades[p]); text(16, y + 14, b, C_GRY);
        part_effect(p, g, b, sizeof b); text(56, y + 14, b, C_GRY);
        if (fitted) textr(306, y + 14, "FITTED", C_GRN);
        else { money_str(m, sizeof m, grades[p][g].price); textr(306, y + 14, m, pf.money >= grades[p][g].price ? C_YEL : C_RED); }
    }
    money_line(190);
    footer("< > BROWSE   A BUY   B BACK");
    message_line();
}
static void draw_mech(void) {
    char b[64], m[32];
    text2c(6, "MECHANIC", C_YEL);
    bx(8, 26, 304, 14, C_CYN); text(16, 29, "RICARDO MONTOYA:", C_CYN);
    int worst = 15; for (int i = 0; i < NPART; i++) if (pf.cond[i] < worst) worst = pf.cond[i];
    text(152, 29, worst >= 13 ? "TOP CONDITION!" : worst >= 7 ? "NEEDS SOME WORK." : "THIS IS DANGEROUS!", C_WHT);
    long tot = 0;
    for (int p = 0; p < NPART; p++) {
        int y = 46 + p * 18, sel = p == cur_mech;
        bx(8, y, 304, 16, sel ? C_YEL : C_CYN);
        cursor(12, y + 4, sel);
        text(24, y + 4, part_label[p], sel ? C_YEL : C_CYN);
        cond_bar(96, y + 4, pf.cond[p]);
        if (pf.cond[p] < 15) { long c = repair_cost(p); tot += c; money_str(m, sizeof m, c); textr(306, y + 4, m, pf.money >= c ? C_YEL : C_RED); }
        else textr(306, y + 4, "OK", C_GRN);
    }
    int y = 46 + NPART * 18; bx(8, y, 304, 16, cur_mech == NPART ? C_YEL : C_CYN); cursor(12, y + 4, cur_mech == NPART);
    text(24, y + 4, "REPAIR ALL", cur_mech == NPART ? C_YEL : C_WHT); money_str(m, sizeof m, tot); textr(306, y + 4, tot ? m : "-", C_YEL);
    (void)b;
    money_line(190);
    footer("A REPAIR   B BACK");
    message_line();
}
static void draw_course(void) {
    char b[64];
    text2c(6, "RACE", C_YEL);
    bx(8, 32, 112, 108, C_CYN);
    text(16, 38, "COURSE", C_CYN);
    for (int c = 0; c < 4; c++) {
        int y = 54 + c * 20, ok = c < pf.unlocked;
        cursor(16, y, c == sel_course);
        text(28, y, course_name[c], !ok ? C_GRY : c == sel_course ? C_YEL : C_WHT);
        snprintf(b, sizeof b, ok ? (pf.rival[c] > 3 ? "CLEARED" : "%d STAGES") : "LOCKED", course_stages[c]); text(28, y + 9, b, ok ? C_GRY : C_RED);
    }
    draw_status_panel(sel_course);
    bx(8, 146, 304, 58, C_CYN);
    text(16, 152, "BEAT THE RIVAL'S LAP TIME TO WIN.", C_WHT);
    text(16, 164, "BEAT ALL 4 RIVALS TO OPEN THE NEXT", C_WHT);
    text(16, 174, "COURSE. HOLD BACK IN A RACE TO RETIRE.", C_WHT);
    money_line(190);
    footer("< > COURSE   A START   B BACK");
}
static void draw_result(void) {
    char b[64], t[24];
    text2c(14, race_retired ? "RETIRED" : "GOAL!", race_retired ? C_RED : C_YEL);
    bx(40, 40, 240, 64, C_CYN);
    text(52, 48, "YOUR LAP", C_CYN); time_str(t, sizeof t, result_time); textr(268, 48, race_retired ? "-" : t, C_WHT);
    text(52, 60, "RIVAL LAP", C_CYN);
    time_str(t, sizeof t, result_rival); textr(268, 60, t, C_WHT);
    text(52, 76, "PRIZE", C_CYN); money_str(t, sizeof t, result_prize); textr(268, 76, t, C_YEL);
    text(52, 88, "MONEY", C_CYN); money_str(t, sizeof t, pf.money); textr(268, 88, t, C_YEL);
    text2c(112, result_win ? "YOU WIN!" : "YOU LOSE", result_win ? C_GRN : C_RED);
    bx(40, 136, 240, 56, C_CYN); text(52, 142, "WEAR", C_CYN);
    for (int p = 0; p < NPART; p++) {
        int x = 52 + (p % 2) * 116, y = 154 + (p / 2) * 11;
        snprintf(b, sizeof b, "%-7s", part_label[p]); text(x, y, b, C_GRY);
        snprintf(b, sizeof b, wear_d[p] ? "-%d" : "OK", wear_d[p]); textr(x + 100, y, b, wear_d[p] > 2 ? C_RED : wear_d[p] ? C_YEL : C_GRN);
    }
    footer(result_ready ? "PRESS A" : "PLEASE WAIT...");
}
static void draw_modesel_title(void) {
    static const char *const items[3] = { "ARCADE MODE", "ORIGINAL MODE", "EXIT GAME" };
    for (int i = 0; i < 3; i++) {
        int y = 112 + i * 12, w = (int)strlen(items[i]) * 8, x = XO + 167 - w / 2, sel = cur_modesel == i;
        sho_video_arcade_text(FB, FW, x, y, items[i], sel ? 1 : 2);   /* yellow when chosen, else white (the title's own colours) */
        if (sel && (sho_frame_no / 16) % 2 == 0) sho_video_arcade_text(FB, FW, x - 14, y, ">", 1);
    }
}
static void draw_modesel(void) {
    bx(56, 72, 208, 88, C_YEL);
    textc(80, "SELECT MODE", C_CYN);
    text(108, 100, "ARCADE MODE", cur_modesel == 0 ? C_YEL : C_WHT); cursor(96, 100, cur_modesel == 0);
    text(108, 118, "ORIGINAL MODE", cur_modesel == 1 ? C_YEL : C_WHT); cursor(96, 118, cur_modesel == 1);
    text(108, 136, "EXIT GAME", cur_modesel == 2 ? C_YEL : C_WHT); cursor(96, 136, cur_modesel == 2);
}
static void draw_title(void) {
    text2c(40, "ORIGINAL MODE", C_YEL);
    bx(90, 80, 140, 64, C_CYN);
    const char *items[3] = { "CONTINUE", "NEW GAME", "BACK" };
    int first = have_save ? 0 : 1, row = 0;
    for (int i = first; i < 3; i++, row++) { int sel = cur_title == row; text(120, 92 + row * 14, items[i], sel ? C_YEL : C_WHT); cursor(106, 92 + row * 14, sel); }
    if (have_save) { char m[32]; money_str(m, sizeof m, pf.money); char b[64]; snprintf(b, sizeof b, "SAVED: %s  WINS %d", m, pf.wins); textc(156, b, C_GRY); }
}
static void draw_confirm(void) {
    bx(50, 80, 220, 60, C_RED);
    textc(90, "START A NEW GAME?", C_WHT);
    textc(102, "YOUR SAVED GAME WILL BE LOST.", C_GRY);
    text(110, 120, "NO", cur_title == 0 ? C_YEL : C_WHT); text(178, 120, "YES", cur_title == 1 ? C_YEL : C_WHT);
    cursor(98, 120, cur_title == 0); cursor(166, 120, cur_title == 1);
}
/* the Mega Drive race HUD: LAP TIME, SPEED and its S-G progress bar (laps marked by ticks) */
static void hud_glyph(int x, int y, char ch, const uint32_t *pal) {
    if (ch < 0 || ch >= 127 || !mdgfx->has_glyph[(int)ch]) return;
    const uint8_t *g = mdgfx->glyph[(int)ch];
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) if (g[j * 8 + i]) px(XO + x + i, y + j, pal[g[j * 8 + i]]);
}
static void hud_tile(int x, int y, const uint8_t *t, const uint32_t *pal, const uint8_t *only_if_differs) {
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
        int c = t[j * 8 + i]; if (!c || (only_if_differs && only_if_differs[j * 8 + i] == c)) continue;
        px(XO + x + i, y + j, pal[c]);
    }
}
/* one converted lap = 2308 road steps, and the course position ($260030) advances two per step */
static void md_race_hud(void) {
    char t[24];
    /* 16:9: LAP TIME, its time and the bar go to the left edge (the time keeps its 4:3 distance from the label), SPEED goes to the right edge */
    int xl = FW > SHO_W ? -XO : 0, xr = FW > SHO_W ? XO : 0;
    {   /* the LAP TIME plate's last two lines (white edge, black outline) come out of the capture 6 lines too low and
           in reverse order, just above the bar: put them back under the plate (lines 24-25) */
        static mdhud_t *fixed;
        if (fixed != mdhud) {
            fixed = mdhud;
            for (int x = 16; x < 104; x++)
                if (!(mdhud->img[24][x] >> 24) && !(mdhud->img[25][x] >> 24) && ((mdhud->img[30][x] | mdhud->img[31][x]) >> 24)) {
                    mdhud->img[24][x] = mdhud->img[31][x]; mdhud->img[25][x] = mdhud->img[30][x];
                    mdhud->img[30][x] = mdhud->img[31][x] = 0;
                }
        }
    }
    for (int y = 0; y < 40; y++) for (int x = 0; x < 320; x++) {
        uint32_t c = mdhud->img[y][x]; if (!(c >> 24)) continue;
        int col = x / 8, dx = col <= 12 ? xl : col >= 22 ? xr : 0;
        px(XO + x + dx, y, c & 0xffffff);
    }
    time_str(t, sizeof t, race_frames - lap_start_f); if (t[0] == '-') strcpy(t, "0'00\"00");
    int n = (int)strlen(t), col = 20 - n + 1;
    for (int i = 0; i < n; i++) hud_glyph((col + i) * 8 + xl, 16, t[i], (t[i] == '\'' || t[i] == '"') ? mdhud->pal[2] : mdhud->pal[1]);   /* stays beside LAP TIME in 16:9 */
    if (phase != PH_FINISH) {
        snprintf(t, sizeof t, "%d", be16r(M->subram, 0x78) * 324 / 279);
        n = (int)strlen(t); col = 33 - n + 1;
        for (int i = 0; i < n; i++) hud_glyph((col + i) * 8 + xr, 16, t[i], mdhud->pal[3]);
    } else for (int y = 8; y < 24; y++) for (int x = 22 * 8; x < 320; x++) px(XO + x + xr, y, fin_bg[y * fin_w + XO + x + xr]);   /* SPEED goes (the road behind it as it was) */
    if (broken_part >= 0 && mdgfx && mdgfx->ok) {                    /* the Mega Drive's warning lines under the HUD */
        const char *m = broken_msg[broken_part];
        if (broken_part != P_BRAKE && broken_part != P_MUFFLER && (sho_frame_no / 16) % 2 == 0) mt(16, 5, "RETIRE?", MDP_GREEN);
        mt(20 - (int)strlen(m) / 2, 6, m, MDP_YELLOW);
    }
    if (msg_t > 0 && (msg_t / 12) % 2 == 0) {   /* the flashing LAP TIME banner (sprites 24/25 of the message): green when ahead of the rival, red when behind */
        const uint32_t *bp = mdgfx->pal[msg_you_cs <= msg_rv_cs ? MDP_GREEN : MDP_RED];
        for (int i = 0; i < 3; i++) hud_glyph(128 + i * 8, 64, "LAP"[i], bp);
        for (int i = 0; i < 4; i++) hud_glyph(160 + i * 8, 64, "TIME"[i], bp);
    }
    if (msg_t > 0) {   /* the lap message: YOU / RIVAL, 8-cell times with a blank for a missing tens digit */
        static const char *lab[2] = { "YOU", "RIVAL" };
        for (int r = 0; r < 2; r++) {
            int cs = r ? msg_rv_cs : msg_you_cs, y = 80 + r * 16;
            for (int i = 0; lab[r][i]; i++) hud_glyph(104 + i * 8, y, lab[r][i], mdgfx->pal[MDP_WHITE]);
            char b[16]; snprintf(b, sizeof b, "%d'%02d\"%02d", cs / 6000, (cs / 100) % 60, cs % 100);
            int n = (int)strlen(b), c0 = 19 + 8 - n;
            if (cs >= 60000) c0 = 19;
            for (int i = 0; i < n; i++) hud_glyph((c0 + i) * 8, y, b[i], (b[i] == '\'' || b[i] == '"') ? mdhud->pal[2] : mdhud->pal[1]);
        }
    }
    /* progress bar: two rails, a yellow band filling between them from the start cap, green ticks between laps */
    int st = be16r(M->workram, 0x438), pos = be16r(M->subram, 0x30);
    /* as the Mega Drive draws it ($BBBA): the summed course position, $800 to a lap */
    (void)pos;
    float lapf = md_laps ? (float)bar_acc / 134217728.0f - (st - 1) : 0;   /* 0x08000000 = $800 in the high word */
    float p = md_laps ? ((st - 1) + lapf) / md_laps : 0; if (p < 0) p = 0; if (p > 1) p = 1;
    int bx0 = 24 + xl;
    for (int i = 0; i < 8; i++) hud_tile(bx0 + i * 8, 32, mdhud->bar_empty, mdhud->pal[1], NULL);
    int r0 = -1, r1 = -1;   /* rail rows: where the empty tile's middle column is drawn */
    for (int j = 0; j < 8; j++) if (mdhud->bar_empty[j * 8 + 4]) { if (r0 < 0) r0 = j; r1 = j; }
    uint32_t yel = 0;
    for (int i = 0; i < 64 && !yel; i++) if (mdhud->bar_start[i] && mdhud->bar_start[i] != mdhud->bar_empty[i]) yel = mdhud->pal[1][mdhud->bar_start[i]];
    int fx = (int)(p * 64);
    if (r0 >= 0 && r1 > r0 + 1 && fx > 0) rect(XO + bx0, 32 + r0 + 1, fx, r1 - r0 - 1, yel);
    hud_tile(bx0, 32, mdhud->bar_start, mdhud->pal[1], mdhud->bar_empty);   /* start cap */
    for (int k = 1; k < md_laps; k++) {
        int tx = bx0 + k * 64 / md_laps, tcol = 0;
        for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) if (mdhud->bar_tick[j * 8 + i] != mdhud->bar_empty[j * 8 + i]) { tcol = i; break; }
        hud_tile(tx - tcol, 32, mdhud->bar_tick, mdhud->pal[2], mdhud->bar_empty);
    }
    if (back_held > 10) { bx(96, 100, 128, 14, C_RED); textc(103, "RETIRING...", C_RED); }
}
static void draw_race_hud(void) {
    char t[32], b[64];
    if (!racing) return;   /* nothing over the arcade's start line */
    if (md_race >= 0 && mdhud && mdhud->ok && mdgfx && mdgfx->ok) { md_race_hud(); return; }
    /* lap time and rival's progress, Mega Drive Original Mode style */
    bx(132, 30, 62, 26, C_CYN);
    time_str(t, sizeof t, race_frames); textc(34, t, C_WHT);
    float rp = race_frames / (float)rival_frames(launch_course); if (rp > 1) rp = 1;
    int stages = md_race >= 0 ? md_laps : course_stages[launch_course], st = be16r(M->workram, 0x438);
    if (md_race >= 0) { int lap = st > md_laps ? md_laps : st; snprintf(b, sizeof b, "LAP %d/%d", lap, md_laps); bx(132, 57, 62, 13, C_CYN); textc(60, b, C_YEL); }
    float pp = (st - 1) / (float)stages; if (pp < 0) pp = 0; if (pp > 1) pp = 1;
    text(136, 45, "R", C_ORG);
    rect(XO + 146, 48, 44, 3, 0x405070);
    rect(XO + 146, 48, (int)(44 * rp), 3, C_ORG);
    rect(XO + 146 + (int)(41 * pp), 46, 3, 7, C_YEL);
    if (gap_timer > 0) {
        snprintf(b, sizeof b, "RIVAL %s%.2f", gap_secs <= 0 ? "-" : "+", fabsf(gap_secs));
        bx(104, 72, 112, 14, gap_secs <= 0 ? C_GRN : C_RED);
        textc(75, b, gap_secs <= 0 ? C_GRN : C_RED);
    }
    if (back_held > 10) { bx(96, 100, 128, 14, C_RED); textc(103, "RETIRING...", C_RED); }
}


/* ------------------------------------------------------------------ */
/* Mega Drive look: screens captured from the player's Mega Drive ROM  */
/* ------------------------------------------------------------------ */
/* part names broken over lines the way the Mega Drive shop shows them */
static const char *const md_lines[NPART][6][3] = {
    { { "IRON", "CRADLE" }, { "DOUBLE", "CRADLE" }, { "ALUMINIUM", "DOUBLE", "CRADLE" }, { "ALUMINIUM", "SPACE" }, { "TITANIUM", "SPACE" }, { "TITANIUM", "MONO-", "-COQUE" } },
    { { "OHC", "NORMAL" }, { "DOHC", "NORMAL" }, { "DOHC V4" }, { "TURBO V4" }, { "CERAMIC", "TURBO" }, { "TWIN", "TURBO" } },
    { { "DRUM" }, { "DISC" }, { "VENTI-", "-LATED", "DISC" }, { "DOUBLE", "DISC" }, { "ANTI-", "-SKID" } },
    { { "NORMAL" }, { "COL" }, { "WORKS", "ORIGINAL" }, { "PRIVATE", "ORIGINAL" } },
    { { "REGULAR" }, { "MANUAL" }, { "TOP" }, { "PRIVATE", "ORIGINAL" } },
    { { "NORMAL" }, { "RADIAL" }, { "HIGH", "PERFORM-", "-ANCE" }, { "SLICK" }, { "ORIGINAL", "SLICK" } },
};
/* box positions on the parts screen (text cell grid): label row, price row, left column, right column */
static const int md_box_label_row[NPART] = { 8, 15, 22, 8, 15, 22 };
static const int md_box_price_row[NPART] = { 12, 19, 26, 12, 19, 26 };
static const int md_box_col[NPART] = { 3, 3, 3, 29, 29, 29 };
static int md_ok(void) { return mdgfx && mdgfx->ok; }
static void md_blit(int img) {
    const uint32_t *src = mdgfx->img[img];
    for (int y = 0; y < SHO_H; y++) {   /* 16:9 sides: continue each line's edge colour */
        uint32_t *d = FB + y * FW, l = src[y * 320], r = src[y * 320 + 319];
        for (int x = 0; x < XO; x++) d[x] = l;
        memcpy(d + XO, src + y * 320, 320 * 4);
        for (int x = XO + 320; x < FW; x++) d[x] = r;
    }
}
static void md_chr(int px_, int py_, char ch, int pal) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 0 || ch >= 127 || !mdgfx->has_glyph[(int)ch]) return;
    const uint8_t *g = mdgfx->glyph[(int)ch];
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) if (g[j * 8 + i]) px(XO + px_ + i, py_ + j, mdgfx->pal[pal][g[j * 8 + i]]);
}
static void mt(int col, int row, const char *s, int pal) { for (int x = col * 8; *s; s++, x += 8) md_chr(x, row * 8, *s, pal); }
static void mtr(int col_end, int row, const char *s, int pal) { mt(col_end - (int)strlen(s) + 1, row, s, pal); }
static void md_wrap(int col, int row, int maxc, int maxl, const char *s, int pal) {
    char line[48]; int n = 0, l = 0; const char *w = s;
    while (*w && l < maxl) {
        const char *e = w; while (*e && *e != ' ') e++;
        int wl = (int)(e - w); if (wl > 40) wl = 40;
        if (n && n + 1 + wl > maxc) { line[n] = 0; mt(col, row + l, line, pal); l++; n = 0; if (l >= maxl) return; }
        if (n) line[n++] = ' ';
        memcpy(line + n, w, (size_t)wl); n += wl; w = *e ? e + 1 : e;
    }
    if (n && l < maxl) { line[n] = 0; mt(col, row + l, line, pal); }
}
static void md_money(char *o, size_t n, long v) { snprintf(o, n, "%ld", v); }
static void md_cursor(int col, int row, int on) { if (on && (sho_frame_no / 16) % 4 != 3) mt(col, row, ">", MDP_WHITE); }

/* command screen frame shared by the menu, race and result views */
static void md_command_base(int cursor_item, int course, const char *rival_line, long your_lap, long rival_lap) {
    char b[48];
    md_blit(MDG_COMMAND);
    mt(3, 1, "COMMAND", MDP_WHITE); mt(13, 1, "RIVAL", MDP_WHITE);
    static const char *items[4] = { "PARTS", "MECHANIC", "RACE", "END" };
    for (int i = 0; i < 4; i++) { mt(3, 3 + i * 2, items[i], MDP_WHITE); md_cursor(2, 3 + i * 2, cursor_item == i); }
    int r = cur_rival(course);
    mt(20, 2, rivals[course][r].name, MDP_YELLOW);
    md_wrap(21, 4, 17, 3, rival_line ? rival_line : rivals[course][r].quote, MDP_WHITE);
    mt(14, 11, "$", MDP_WHITE); md_money(b, sizeof b, pf.money); mtr(25, 11, b, MDP_WHITE);
    mt(30, 10, "YOUR LAP", MDP_WHITE); time_str(b, sizeof b, your_lap); if (your_lap > 0) mtr(37, 11, b, MDP_WHITE);
    mt(29, 12, "RIVAL LAP", MDP_WHITE); time_str(b, sizeof b, rival_lap); mtr(37, 13, b, MDP_WHITE);
    mt(29, 17, "WIN", MDP_GREEN); mt(34, 17, "LOSS", MDP_RED);
    snprintf(b, sizeof b, "%d", pf.wins); mtr(30, 18, b, MDP_GREEN);
    snprintf(b, sizeof b, "%d", pf.losses); mtr(36, 18, b, MDP_RED);
    mt(1, 13, "MECHANIC", MDP_RED); mt(9, 14, "RICARDO MONTOYA", MDP_YELLOW);
    mt(1, 20, "SPONSOR", MDP_WHITE); mt(9, 21, "PHARMACY", MDP_YELLOW);
}
static void md_mech_says(const char *a, const char *b2) { if (a) mt(10, 16, a, MDP_WHITE); if (b2) mt(10, 17, b2, MDP_WHITE); }
static void md_sponsor_says(const char *a, const char *b2) { if (a) mt(10, 23, a, MDP_WHITE); if (b2) mt(10, 24, b2, MDP_WHITE); }
static void md_condition_comment(char *o, size_t n) {
    int worst = 15, wp = 0; for (int i = 0; i < NPART; i++) if (pf.cond[i] < worst) { worst = pf.cond[i]; wp = i; }
    if (worst >= 13) snprintf(o, n, "TOP CONDITION!");
    else if (worst >= 7) snprintf(o, n, "%s NEEDS WORK.", part_label[wp]);
    else snprintf(o, n, "%s IS WORN OUT!", part_label[wp]);
}
static void md_draw_menu(void) {
    char b[48];
    md_command_base(cur_menu, sel_course, NULL, pf.best[sel_course], rival_frames(sel_course));
    md_condition_comment(b, sizeof b); md_mech_says(b, NULL);
    md_sponsor_says(NULL, "IF IT'S MONEY, LET ME KNOW.");
}
static void md_draw_course(void) {
    char b[48], m[24];
    int c = sel_course;
    md_command_base(2, c, NULL, pf.best[c], rival_frames(c));
    snprintf(b, sizeof b, "%s%s %s%s", c > 0 ? "<" : " ", course_name[c], class_name[c], c + 1 < pf.unlocked ? " >" : "");
    md_mech_says(b, NULL);
    snprintf(b, sizeof b, "%d STAGES%s", course_stages[c], pf.rival[c] > 3 ? "  REMATCH" : ""); mt(10, 17, b, MDP_WHITE);
    md_money(m, sizeof m, race_prize(c));
    snprintf(b, sizeof b, "PRIZE $%s  A:START", m); md_sponsor_says(b, "BEAT THE RIVAL'S LAP TIME!");
}
static void md_draw_result(void) {
    char b[64], m[24];
    int c = launch_course;
    const char *line = race_retired ? "GIVING UP ALREADY?" : result_win ? "YOU'RE FASTER THAN I THOUGHT." : "TOO SLOW! TRY AGAIN.";
    /* the rival shown is the one just raced */
    int keep = pf.rival[c]; if (result_win && keep > 0 && keep <= 4) pf.rival[c] = keep - 1 > 3 ? 3 : keep - 1;
    md_command_base(2, c, line, result_time, result_rival);
    pf.rival[c] = keep;
    mt(27 - 8, 8, result_win ? "YOU WIN!" : race_retired ? "RETIRED" : "YOU LOSE", result_win ? MDP_GREEN : MDP_RED);
    int n = 0; b[0] = 0;
    for (int p = 0; p < NPART && n < 3; p++) if (wear_d[p]) { char t[20]; snprintf(t, sizeof t, "%s%s -%d", n ? " " : "", part_label[p], wear_d[p]); if (strlen(b) + strlen(t) < 18) { strcat(b, t); n++; } }
    md_mech_says("WEAR:", n ? b : "NONE");
    md_money(m, sizeof m, result_prize);
    snprintf(b, sizeof b, result_win ? "HERE'S YOUR PRIZE: $%s" : "FOR TAKING PART: $%s", m);
    md_sponsor_says(b, result_ready ? "PRESS A" : "PLEASE WAIT...");
}
static void md_part_box(int p, int g, int pal_name, int show_price, int sel) {
    int col = md_box_col[p], lr = md_box_label_row[p], pr = md_box_price_row[p];
    mt(col - 1, lr, part_label[p], sel ? MDP_RED : MDP_WHITE);
    int nl = 0; while (nl < 3 && md_lines[p][g][nl]) nl++;
    for (int i = 0; i < nl; i++) {
        const char *t = md_lines[p][g][i]; int row = pr - nl + i;
        if (i == 0) mt((int)strlen(t) > 8 ? col - 1 : col, row, t, pal_name);
        else mtr(col + 7, row, t, pal_name);
    }
    if (show_price) {
        char m[24]; mt(col, pr, "$", MDP_WHITE); md_money(m, sizeof m, grades[p][g].price);
        mtr(col + 7, pr, m, g == pf.part[p] ? MDP_GREEN : pf.money >= grades[p][g].price ? MDP_WHITE : MDP_RED);
    }
    if (sel && show_price) { if (g > 0) mt(col + 6, lr, "<", MDP_WHITE); if (g + 1 < ngrades[p]) mt(col + 7, lr, ">", MDP_WHITE); }
}
static void md_draw_parts(void) {
    char b[48], m[24];
    md_blit(MDG_PARTS0 + cur_part);
    mt(10, 2, "RICARDO MONTOYA", MDP_YELLOW);
    for (int p = 0; p < NPART; p++) md_part_box(p, browse[p], browse[p] == pf.part[p] ? MDP_GREEN : MDP_WHITE, 1, p == cur_part);
    if (msg_timer > 0) mt(10, 5, msg, MDP_WHITE);
    else {
        int g = browse[cur_part];
        if (g == pf.part[cur_part]) md_condition_comment(b, sizeof b); else part_effect(cur_part, g, b, sizeof b);
        for (char *q = b; *q; q++) if (*q == '%') *q = ' ';
        mt(10, 4, b, MDP_WHITE);
        mt(10, 6, g == pf.part[cur_part] ? "< > BROWSE   B: BACK" : "A: BUY   B: BACK", MDP_WHITE);
    }
    mt(14, 25, "$", MDP_WHITE); md_money(m, sizeof m, pf.money); mtr(25, 25, m, MDP_WHITE);
}
static void md_draw_mech(void) {
    char b[48], m[24];
    md_blit(MDG_PARTS0 + (cur_mech < NPART ? cur_mech : 0));
    mt(10, 2, "RICARDO MONTOYA", MDP_YELLOW);
    long tot = 0;
    for (int p = 0; p < NPART; p++) {
        md_part_box(p, pf.part[p], MDP_WHITE, 0, p == cur_mech);
        int col = md_box_col[p], pr = md_box_price_row[p];
        uint32_t bc = pf.cond[p] > 9 ? mdgfx->pal[MDP_GREEN][2] : pf.cond[p] > 4 ? mdgfx->pal[MDP_YELLOW][2] : mdgfx->pal[MDP_RED][2];
        for (int i = 0; i < 15; i++) rect(XO + col * 8 + i * 4, pr * 8 + 1, 3, 6, i < pf.cond[p] ? bc : 0x203030);
        if (pf.cond[p] < 15) tot += repair_cost(p);
    }
    if (msg_timer > 0) mt(10, 5, msg, MDP_WHITE);
    else if (cur_mech < NPART) {
        if (pf.cond[cur_mech] == 15) snprintf(b, sizeof b, "THE %s IS FINE.", part_label[cur_mech]);
        else { md_money(m, sizeof m, repair_cost(cur_mech)); snprintf(b, sizeof b, "FIX THE %s? $%s", part_label[cur_mech], m); }
        mt(10, 4, b, MDP_WHITE);
        md_money(m, sizeof m, tot); snprintf(b, sizeof b, tot ? "DOWN: ALL FOR $%s" : "A: REPAIR   B: BACK", m); mt(10, 6, b, MDP_WHITE);
    } else {
        md_money(m, sizeof m, tot); snprintf(b, sizeof b, tot ? "FIX EVERYTHING? $%s" : "EVERYTHING IS FINE.", m);
        mt(10, 4, b, MDP_YELLOW); mt(10, 6, "A: REPAIR   B: BACK", MDP_WHITE);
    }
    mt(14, 25, "$", MDP_WHITE); md_money(m, sizeof m, pf.money); mtr(25, 25, m, MDP_WHITE);
}
static void md_draw_title(void) {
    md_blit(MDG_TITLE);
    mt(14, 15, "ORIGINAL MODE", MDP_YELLOW);
    const char *items[3] = { "CONTINUE", "NEW GAME", "BACK" };
    int first = have_save ? 0 : 1, row = 0;
    for (int i = first; i < 3; i++, row++) { mt(16, 18 + row * 2, items[i], MDP_WHITE); md_cursor(14, 18 + row * 2, cur_title == row); }
    if (have_save && !md_live) { char b[48], m[24]; md_money(m, sizeof m, pf.money); snprintf(b, sizeof b, "SAVED: $%s  WINS %d", m, pf.wins); mt(20 - (int)strlen(b) / 2, 25, b, MDP_WHITE); }
}
static void md_draw_confirm(void) {
    md_blit(MDG_TITLE);
    mt(11, 16, "START A NEW GAME?", MDP_YELLOW);
    mt(8, 18, "YOUR SAVED GAME WILL BE LOST.", MDP_WHITE);
    mt(16, 21, "NO", MDP_WHITE); mt(22, 21, "YES", MDP_WHITE);
    md_cursor(14, 21, cur_title == 0); md_cursor(20, 21, cur_title == 1);
}

/* Mega Drive mode pause: the Mega Drive's own "P A U S E" (its yellow font, letters 16 pixels apart, at
   (120,112) on its screen), blinking 48 frames on, 48 off as on the Mega Drive */
int sho_md_pause_allowed(void) { return cfg_enh && md_ok() && game_state() == 2 && playing() && !overlay_phase(phase); }
/* pause menu under the blinking PAUSE: CONTINUE / SOUND / EXIT, and a sound page with music and sound
   effect levels (0-10), in the Mega Drive font: highlighted line yellow with the ">" cursor, others white */
static int pm_page, pm_cur, pm_prev_nav[6];
void md_pause_reset(void) { pm_page = 0; pm_cur = 0; for (int i = 0; i < 6; i++) pm_prev_nav[i] = 1; }
static int pm_tick;
/* the menu is centred on the screen: PAUSE at y 80, options at y 104/120/136 (block centred on line 112),
   each line centred on x 160 to the pixel; the cursor sits left of the text without moving it */
static void pm_text(int x, int y, const char *s, int pal) { for (; *s; s++, x += 8) md_chr(x, y, *s, pal); }
static void pm_line(int y, const char *s, int sel) {
    int x = 160 - (int)strlen(s) * 4;
    pm_text(x, y, s, sel ? MDP_YELLOW : MDP_WHITE);
    if (sel && (pm_tick / 16) % 4 != 3) md_chr(x - 16, y, '>', MDP_WHITE);
}
static void md_chr_mirror(int px_, int py_, char ch, int pal) {   /* a glyph flipped left-right (the left arrow) */
    if (!mdgfx->has_glyph[(int)ch]) return;
    const uint8_t *g = mdgfx->glyph[(int)ch];
    for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) if (g[j * 8 + 7 - i]) px(XO + px_ + i, py_ + j, mdgfx->pal[pal][g[j * 8 + 7 - i]]);
}
/* a level line, NAME  < nn >, centred as a whole */
static void pm_level(int y, const char *name, int v, int sel) {
    char b[32]; snprintf(b, sizeof b, "%-7s   %2d  ", name, v);   /* name, gap, left arrow slot, value, right arrow slot */
    int x = 160 - (int)strlen(b) * 4, pal = sel ? MDP_YELLOW : MDP_WHITE;
    pm_text(x, y, b, pal);
    if (sel && (pm_tick / 16) % 4 != 3) md_chr(x - 16, y, '>', MDP_WHITE);
    if (v > 0) md_chr_mirror(x + 8 * 8, y, '>', pal);
    if (v < 10) md_chr(x + 13 * 8, y, '>', pal);
}
int md_pause_menu(uint32_t *fb, int width, int tick, const sho_input *in, int *music, int *sfx) {
    FB = fb; FW = width; XO = (width - SHO_W) / 2;
    int now[6] = { in->nav_up, in->nav_down, in->nav_left, in->nav_right, in->pad_a || in->start, in->pad_b }, e[6];
    for (int i = 0; i < 6; i++) { e[i] = now[i] && !pm_prev_nav[i]; pm_prev_nav[i] = now[i]; }
    int act = 0;
    if (pm_page == 0) {
        if (e[0]) pm_cur = (pm_cur + 2) % 3;
        if (e[1]) pm_cur = (pm_cur + 1) % 3;
        if (e[5]) act = 1;                                    /* back = continue */
        if (e[4]) { if (pm_cur == 0) act = 1; else if (pm_cur == 1) { pm_page = 1; pm_cur = 0; } else act = 2; }
    } else {
        if (e[0]) pm_cur = (pm_cur + 2) % 3;
        if (e[1]) pm_cur = (pm_cur + 1) % 3;
        int *lv = pm_cur == 0 ? music : pm_cur == 1 ? sfx : NULL;
        if (lv && e[2] && *lv > 0) (*lv)--;
        if (lv && e[3] && *lv < 10) (*lv)++;
        if (e[5] || (e[4] && pm_cur == 2)) { pm_page = 0; pm_cur = 1; }
    }
    if (!((tick / 48) & 1)) { static const char word[] = "PAUSE"; for (int i = 0; word[i]; i++) md_chr(124 + i * 16, 80, word[i], MDP_YELLOW); }   /* 72 px wide, centred */
    pm_tick = tick;
    if (pm_page == 0) {
        pm_line(104, "CONTINUE", pm_cur == 0); pm_line(120, "SOUND", pm_cur == 1); pm_line(136, "EXIT", pm_cur == 2);
    } else {
        pm_level(104, "MUSIC", *music, pm_cur == 0); pm_level(120, "EFFECTS", *sfx, pm_cur == 1); pm_line(136, "BACK", pm_cur == 2);
    }
    return act;
}

/* test harness: show a results screen without racing */
void md_test_rival_sample(void) { rival_sample(); }
void md_test_finish(void) { cfg_enh = 1; md_enabled = 1; finish_begin(); }
int md_finish_t(void) { return phase == PH_FINISH ? finish_t : 0; }
void md_test_result(int win) {
    launch_course = 0; race_frames = win ? 11000 : 13000; race_retired = 0; crashes = 3; brake_frames = 1200;
    finish_race(); result_ready = 1;
}
void md_compose(uint32_t *fb, int width) {
    if (!cfg_enh) return;
    FB = fb; FW = width; XO = (width - SHO_W) / 2;
    if (phase == PH_ARCADE) return;
    if (launch_hidden()) { memset(FB, 0, (size_t)FW * SHO_H * 4); return; }   /* live: no arcade class/music screens */
    if (phase == PH_RACE || phase == PH_LAUNCH) { draw_race_hud(); return; }
    if (phase == PH_FINISH) { finish_draw(); return; }
    if (phase == PH_MDLIVE) {
        for (int y = 0; y < SHO_H; y++) {
            uint32_t *d = FB + y * FW, l = live_img[y * 320], r = live_img[y * 320 + 319];
            for (int x = 0; x < XO; x++) d[x] = l;
            memcpy(d + XO, live_img + y * 320, 320 * 4);
            for (int x = XO + 320; x < FW; x++) d[x] = r;
        }
        if (cheat_msg > 0 && mdgfx && mdgfx->ok) { cheat_msg--; for (int y = 216; y < 224; y++) for (int x = 176; x < 320; x++) FB[y * FW + XO + x] = 0; mtr(39, 27, cheat_text, MDP_GREEN); }
        if (dev_race && mdgfx && mdgfx->ok) {   /* development course select */
            char b[40]; mdcourse_t ci; int laps = mdcourse_info(dev_race - 1, &ci) ? ci.laps : 0;
            snprintf(b, sizeof b, "DEV RACE %d  %d LAPS", dev_race, laps);
            for (int y = 216; y < 224; y++) for (int x = 0; x < 160; x++) FB[y * FW + XO + x] = 0;
            mt(0, 27, b, MDP_YELLOW);
        }
        return;
    }
    if (md_ok() && phase != PH_MODESEL) {
        switch (phase) {
        case PH_TITLE: md_draw_title(); break;
        case PH_CONFIRM_NEW: md_draw_confirm(); break;
        case PH_MENU: md_draw_menu(); break;
        case PH_PARTS: md_draw_parts(); break;
        case PH_MECH: md_draw_mech(); break;
        case PH_COURSE: md_draw_course(); break;
        case PH_RESULT: md_draw_result(); break;
        }
        return;
    }
    if (phase == PH_MODESEL && modesel_on_title) { draw_modesel_title(); return; }
    dim(phase == PH_MODESEL ? 0.45f : 0.22f);
    switch (phase) {
    case PH_MODESEL: draw_modesel(); break;
    case PH_TITLE: draw_title(); break;
    case PH_CONFIRM_NEW: draw_title(); draw_confirm(); break;
    case PH_MENU: draw_menu(); break;
    case PH_PARTS: draw_parts(); break;
    case PH_MECH: draw_mech(); break;
    case PH_COURSE: draw_course(); break;
    case PH_RESULT: draw_result(); break;
    }
}
int md_test_phase(void) { return phase; }
