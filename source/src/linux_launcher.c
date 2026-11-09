/* Super Hang-On launcher for Linux: the shared launcher (launcher_core.c) in the SDL game window. The picture is
 * drawn smoothly scaled and the settings canvas over it with sharp pixels, letterboxed to 16:9.
 * Keyboard, mouse or gamepad: Up/Down choose a line, Left/Right (or a click) change it, Enter / A / Start on PLAY
 * plays, Esc quits; a click on the FOLDER line opens the game folder. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linux_launcher.h"

/* the launcher picture, built in from res/ (the build runs from the source folder) */
__asm__(".section .rodata\n.balign 16\n.globl sho_launcher_png\nsho_launcher_png:\n.incbin \"res/launcher_bg.png\"\n"
        ".globl sho_launcher_png_end\nsho_launcher_png_end:\n.byte 0\n.previous\n");
extern const unsigned char sho_launcher_png[], sho_launcher_png_end[];

static SDL_Renderer *ren;
static SDL_Texture *t_bg, *t_canvas;
static double now_seconds(void) { return SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency(); }
static void save_shot(const char *file) {   /* development: the window to a .bmp */
    int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (sf) { SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch); SDL_SaveBMP(sf, file); SDL_FreeSurface(sf); }
}
static void draw_frame(void) {
    SDL_RenderSetLogicalSize(ren, LC_CW, LC_CH);   /* 16:9, with bars on other window shapes; mouse positions arrive in canvas pixels */
    SDL_UpdateTexture(t_canvas, NULL, lc_canvas(), LC_CW * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (t_bg) SDL_RenderCopy(ren, t_bg, NULL, NULL);
    SDL_RenderCopy(ren, t_canvas, NULL, NULL);
}
static void present_loading(void) {   /* lc_load_step: the launcher with the loading bar */
    if (!t_canvas) return;
    SDL_PumpEvents();
    draw_frame();
    const char *shot = getenv("LAUNCHER_LOAD_SHOT");   /* development: name_N.bmp each time the step changes */
    if (shot) { static char last[64]; static int n; extern const char *lc_debug_load_msg(void);
        const char *m = lc_debug_load_msg();
        if (strcmp(last, m)) { char f[512]; snprintf(f, sizeof f, "%s_%d.bmp", shot, n++); save_shot(f); snprintf(last, sizeof last, "%s", m); } }
    SDL_RenderPresent(ren);
}

int launcher_run(SDL_Window *win, SDL_Renderer *r, launcher_settings *s, launcher_rom_check_fn check) {
    (void)win; ren = r;
    lc_begin(s, check, sho_launcher_png, (size_t)(sho_launcher_png_end - sho_launcher_png));
    int w = 0, h = 0; const uint32_t *px = lc_background(&w, &h);
    if (px) {   /* the picture, smoothly scaled */
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        t_bg = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (t_bg) SDL_UpdateTexture(t_bg, NULL, px, w * 4);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");   /* the letters stay sharp */
    t_canvas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, LC_CW, LC_CH);
    if (!t_canvas) { if (t_bg) SDL_DestroyTexture(t_bg); t_bg = NULL; lc_end(); return 1; }   /* no way to show it: play with the saved settings */
    SDL_SetTextureBlendMode(t_canvas, SDL_BLENDMODE_BLEND);
    lc_present_hook = present_loading; lc_clock = now_seconds;
    int frame = 0;
    while (lc_result() == LC_RUNNING) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) lc_key(LK_ESC);
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: lc_key(LK_UP); break;
                case SDLK_DOWN: lc_key(LK_DOWN); break;
                case SDLK_LEFT: lc_key(LK_LEFT); break;
                case SDLK_RIGHT: lc_key(LK_RIGHT); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: lc_key(LK_ENTER); break;
                case SDLK_SPACE: lc_key(LK_SPACE); break;
                case SDLK_ESCAPE: lc_key(LK_ESC); break;
                }
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: lc_key(LK_UP); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: lc_key(LK_DOWN); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: lc_key(LK_LEFT); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: lc_key(LK_RIGHT); break;
                case SDL_CONTROLLER_BUTTON_A: lc_key(LK_PAD_A); break;
                case SDL_CONTROLLER_BUTTON_START: lc_key(LK_PAD_START); break;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED) {
                SDL_GameControllerOpen(e.cdevice.which);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                lc_click(e.button.x, e.button.y);
            }
        }
        if (lc_folder_clicked() && s->game_dir) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
            char url[4200]; snprintf(url, sizeof url, "file://%s", s->game_dir); SDL_OpenURL(url);
#endif
        }
        lc_tick(now_seconds());
        draw_frame();
        frame++;
        {   /* development: LAUNCHER_SHOT=file.bmp saves the first picture and quits; LAUNCHER_PLAY=1 presses PLAY */
            const char *shot = getenv("LAUNCHER_SHOT");
            if (shot && frame > 20) { save_shot(shot); lc_key(LK_ESC); }
            if (getenv("LAUNCHER_PLAY") && frame > 20) lc_key(LK_ENTER);
        }
        SDL_RenderPresent(r);
        SDL_Delay(1);
    }
    if (lc_result() != LC_PLAY) { launcher_close(r); return 0; }
    lc_finish(s);
    return 1;
}

void launcher_close(SDL_Renderer *r) {
    if (t_canvas) { SDL_DestroyTexture(t_canvas); t_canvas = NULL; }
    if (t_bg) { SDL_DestroyTexture(t_bg); t_bg = NULL; }
    if (r) SDL_RenderSetLogicalSize(r, 0, 0);
    lc_present_hook = NULL;
    lc_end();
}
