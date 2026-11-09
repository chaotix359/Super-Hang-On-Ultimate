/* Super Hang-On launcher for Linux: the shared launcher (launcher_core.c) shown in the SDL game window. */
#ifndef LINUX_LAUNCHER_H
#define LINUX_LAUNCHER_H
#include <SDL.h>
#include "launcher_core.h"
/* shows the launcher in the window; returns 1 if PLAY was chosen (settings updated), 0 if the window was closed / Esc.
   After PLAY it stays on screen: lc_load_begin / lc_load_step draw the loading bar; launcher_close() when the game
   is ready to take the window. */
int launcher_run(SDL_Window *win, SDL_Renderer *r, launcher_settings *s, launcher_rom_check_fn check);
void launcher_close(SDL_Renderer *r);
#endif
