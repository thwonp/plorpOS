/* SPDX-License-Identifier: MIT */
/* GKD on the BasedOS fork, PortMaster ports only (sd/gkdb/portmaster/pm-run.sh,
 * plorpos-gkd.94.15): a fixed-size SDL2 game on KMSDRM draws into a corner.
 * KMSDRM has the panel's one mode and does not scale a smaller window - nor
 * an exclusive fullscreen at the game's size - up to it: Cave Story Evo's
 * frame sat in the top-left 640x480 of the 1600x1440 panel.
 *
 * Such a window is made a desktop-fullscreen one. Some games then fill it
 * themselves - Hydra Castle Labyrinth sizes its frame by the renderer's output
 * size - and a logical size on top zoomed it twice over; others draw at their
 * window's size and need SDL to scale. So the renderer is given the game's
 * size as its logical size at its first present, and only if the game has
 * neither asked for the output size nor set a logical size or scale of its
 * own. At most that first frame is drawn unscaled.
 *
 * A window that already fills the panel, or asks for desktop fullscreen, is
 * left as it is. Everything through dlsym: this is preloaded into every
 * process of a port's script (bash, tee, gptokeyb), most of them without SDL. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>

static SDL_Window *scaled;
static SDL_Renderer *scaled_ren;
static int scaled_w, scaled_h;
static bool knows_output, decided;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	SDL_Window *(*create)(const char *, int, int, int, int, Uint32) = dlsym(RTLD_NEXT, "SDL_CreateWindow");
	int (*desktop)(int, SDL_DisplayMode *) = dlsym(RTLD_NEXT, "SDL_GetDesktopDisplayMode");
	SDL_DisplayMode dm;
	SDL_Window *win;

	if (!create) return NULL;
	if (!desktop || (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP ||
	    w <= 0 || h <= 0 || desktop(0, &dm) != 0 || (w >= dm.w && h >= dm.h))
		return create(title, x, y, w, h, flags);
	win = create(title, x, y, w, h, flags | SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (win) {
		scaled = win;
		scaled_w = w;
		scaled_h = h;
		fprintf(stderr, "portscale: %dx%d window drawn at %dx%d\n", w, h, dm.w, dm.h);
	}
	return win;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	SDL_Renderer *(*create)(SDL_Window *, int, Uint32) = dlsym(RTLD_NEXT, "SDL_CreateRenderer");
	SDL_Renderer *r = create ? create(window, index, flags) : NULL;

	if (r && window && window == scaled) {
		scaled_ren = r;
		knows_output = decided = false;
	}
	return r;
}

/* A game that asks this draws to the whole output itself. */
int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h)
{
	int (*size)(SDL_Renderer *, int *, int *) = dlsym(RTLD_NEXT, "SDL_GetRendererOutputSize");

	if (r && r == scaled_ren) knows_output = true;
	return size ? size(r, w, h) : -1;
}

void SDL_RenderPresent(SDL_Renderer *r)
{
	void (*present)(SDL_Renderer *) = dlsym(RTLD_NEXT, "SDL_RenderPresent");

	if (r && r == scaled_ren && !decided) {
		void (*get_logical)(SDL_Renderer *, int *, int *) = dlsym(RTLD_NEXT, "SDL_RenderGetLogicalSize");
		void (*get_scale)(SDL_Renderer *, float *, float *) = dlsym(RTLD_NEXT, "SDL_RenderGetScale");
		int (*logical)(SDL_Renderer *, int, int) = dlsym(RTLD_NEXT, "SDL_RenderSetLogicalSize");
		int lw = 0, lh = 0;
		float sx = 1.0f, sy = 1.0f;

		decided = true;
		if (get_logical) get_logical(r, &lw, &lh);
		if (get_scale) get_scale(r, &sx, &sy);
		if (!knows_output && !lw && !lh && sx == 1.0f && sy == 1.0f && logical) {
			logical(r, scaled_w, scaled_h);
			fprintf(stderr, "portscale: scaled by SDL from %dx%d\n", scaled_w, scaled_h);
		} else
			fprintf(stderr, "portscale: the game fills the panel itself\n");
	}
	if (present) present(r);
}

/* A game that goes fullscreen itself later would leave the panel's mode for
 * its own and land in the corner again. */
int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
	int (*fullscreen)(SDL_Window *, Uint32) = dlsym(RTLD_NEXT, "SDL_SetWindowFullscreen");

	if (!fullscreen) return -1;
	if (window && window == scaled && (flags & SDL_WINDOW_FULLSCREEN))
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	return fullscreen(window, flags);
}
