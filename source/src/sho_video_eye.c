/* A second copy of the renderer (sho_video.c with every name of its own changed), so that the two eyes' pictures of
 * the stereo 3D mode can be drawn at the same time on two processor cores: the first copy draws the left eye on
 * the game's thread while this one draws the right eye on a worker thread (sho_core.c, render_eyes). It reads the
 * video state from its own machine, sho_e_M - a copy made for the frame - and shares only the frame's backdrop. */
#define SHO_VIDEO_EYE 1
#include "sho_video.c"
