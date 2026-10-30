/* SPDX-License-Identifier: MIT */
/* GKD PortMaster: fixed-size SDL2 games need fullscreen mode emulation,
 * not just a compositor-sized window (plorpos-46o.14). Keep the game's
 * requested drawing resolution; SDL/Wayland scales its buffer to the panel.
 * Loaded only by pm-run.sh, never by the launcher or Diatom. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags)
{
	SDL_Window *(*create)(const char *, int, int, int, int, Uint32) =
		dlsym(RTLD_NEXT, "SDL_CreateWindow");
	int (*mode)(SDL_Window *, const SDL_DisplayMode *) =
		dlsym(RTLD_NEXT, "SDL_SetWindowDisplayMode");
	int (*fullscreen)(SDL_Window *, Uint32) = dlsym(RTLD_NEXT, "SDL_SetWindowFullscreen");
	void (*show)(SDL_Window *) = dlsym(RTLD_NEXT, "SDL_ShowWindow");
	SDL_DisplayMode dm = { .w = w, .h = h };
	SDL_Window *window;

	if (!create || !mode || !fullscreen || !show) return NULL;
	window = create(title, x, y, w, h,
	                (flags & ~(SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN)) |
	                SDL_WINDOW_HIDDEN);
	if (window) {
		mode(window, &dm);
		fullscreen(window, SDL_WINDOW_FULLSCREEN);
		if (!(flags & SDL_WINDOW_HIDDEN)) show(window);
	}
	return window;
}

int SDL_SetWindowFullscreen(SDL_Window *window, Uint32 flags)
{
	int (*fullscreen)(SDL_Window *, Uint32) = dlsym(RTLD_NEXT, "SDL_SetWindowFullscreen");
	(void)flags;
	return fullscreen ? fullscreen(window, SDL_WINDOW_FULLSCREEN) : -1;
}
