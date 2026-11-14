/* Frame pacing helpers shared by the frontends.
 *
 * The frontends show one picture per display refresh (the present waits for it) and work out how many game
 * frames (60.05 a second) go with it. That only keeps real time if they know how many refreshes really went by:
 *  - the rate the system REPORTS isn't always the rate presents come at (a 144 Hz desktop whose presents are held
 *    to 60 by the compositor, a driver's "half refresh" vsync, the window on another monitor, a remote session):
 *    trusting it made the game - and its sound - run in slow motion (60 presents counted as 60/144 of a second);
 *  - a present that misses a refresh (a slow PC, a hiccup) is two refreshes, not one: counted as one, the game
 *    drops behind real time and the sound queue runs dry (choppy music).
 * So: sho_pace_step() is given the measured time since the last present, keeps a running average of it, and
 * returns how many refreshes went by, at the rate presents really come at (p->used). That rate starts as the
 * reported one and is replaced when the measured one has differed from it steadily for about a second. (It may
 * settle on a whole multiple of the true rate with that many refreshes per present, e.g. 120 x 2 for presents at
 * 60 on a "144 Hz" desktop: the time counted is the same.)
 *
 * sho_audio_target_step() widens the sound queue when it runs dry (a PC with hiccups gets more cushion, up to
 * 200 ms; everyone else keeps the short 67 ms).
 */
#ifndef SHO_PACE_H
#define SHO_PACE_H
#include <math.h>

typedef struct {
    double reported;    /* what the system says the refresh rate is */
    double used;        /* the rate presents really come at (what the frontends pace by) */
    double avg;         /* running average of the time between presents */
    int off, seen;      /* presents in a row whose average didn't match `used`; presents since the last reset */
    int adopted;        /* times `used` was changed (for the log) */
} sho_pacer;

static inline void sho_pace_reset(sho_pacer *p, double reported) {
    p->reported = p->used = reported > 1 ? reported : 60.0;
    p->avg = 1.0 / p->used; p->off = p->seen = 0;
}
/* the nearest sensible rate to a measured one: a whole fraction of the reported rate, or a common display rate */
static inline double sho_pace_snap(double measured, double reported) {
    static const double common[] = { 24, 25, 30, 36, 48, 50, 59.94, 60, 72, 75, 85, 90, 100, 120, 144, 165, 170, 180, 200, 240, 300, 360 };
    double best = measured, err = 0.025;   /* within 2.5%, otherwise the measured rate itself */
    for (int d = 1; d <= 6; d++) { double c = reported / d, e = fabs(measured - c) / c; if (e < err) { err = e; best = c; } }
    for (unsigned i = 0; i < sizeof common / sizeof common[0]; i++) { double c = common[i], e = fabs(measured - c) / c; if (e < err - 0.004) { err = e; best = c; } }
    return best;
}
/* One call per presented picture. iv = seconds since the previous one. Returns the refreshes that went by (1..8). */
static inline int sho_pace_step(sho_pacer *p, double reported, double iv) {
    if (reported != p->reported || p->used < 1) sho_pace_reset(p, reported);
    int ticks = (int)(iv * p->used + 0.5);
    ticks = ticks < 1 ? 1 : ticks > 8 ? 8 : ticks;
    if (iv > 0.0002 && iv < 0.25) {            /* (not a pause, a dragged window, a debugger) */
        /* the time ONE refresh takes: a present that missed refreshes counts as that many (otherwise the misses
           would be counted twice: once in a slower rate, once as extra refreshes) */
        p->avg += (iv / ticks - p->avg) * 0.04; p->seen++;
        double measured = 1.0 / p->avg;
        if (fabs(measured - p->used) > 0.03 * p->used) p->off++; else p->off = 0;
        if (p->off > 45 && p->seen > 60) {     /* steadily different: this is the real rate */
            p->used = sho_pace_snap(measured, p->reported); p->off = 0; p->adopted++;
            ticks = (int)(iv * p->used + 0.5); ticks = ticks < 1 ? 1 : ticks > 8 ? 8 : ticks;
        }
    }
    return ticks;
}

/* The sound queue's target (samples ahead). Called once per game frame with what is queued now: when the queue has
 * all but run dry, the target grows by a frame's worth (800), up to 9600 (200 ms); returns 1 when it grew. */
static inline int sho_audio_target_step(long queued, int *target) {
    static int cooldown;
    if (cooldown > 0) { cooldown--; return 0; }
    if (queued < 300 && *target < 9600) { *target += 800; cooldown = 120; return 1; }
    return 0;
}
#endif
