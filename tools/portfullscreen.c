/* SPDX-License-Identifier: MIT */
/* GKD PortMaster: fixed-size SDL2 games need fullscreen mode emulation,
 * not just a compositor-sized window (plorpos-46o.14). Keep the game's
 * requested drawing resolution; SDL/Wayland scales its buffer to the panel.
 * Loaded only by pm-run.sh, never by the launcher or Diatom. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <errno.h>     /* program_invocation_short_name */
#include <stdlib.h>
#include <string.h>

/* SDL itself, past these hooks: also when the game carries its own SDL inside
 * a library it loaded privately (Pyxel's Python extension, RTLD_LOCAL), which
 * RTLD_NEXT does not see - then the caller's copy, from its own library. As
 * tools/portscale.c (plorpos-rgds.31). */
static void *sdl_real(const char *name, const void *caller)
{
	void *f = dlsym(RTLD_NEXT, name);
	Dl_info di;

	if (!f && dladdr(caller, &di) && di.dli_fname) {
		void *h = dlopen(di.dli_fname, RTLD_LAZY | RTLD_NOLOAD);

		if (h) {
			f = dlsym(h, name);
			dlclose(h);
		}
	}
	return f;
}

/* The program pm-run.sh names in PORTFS_PASS keeps SDL's own window: Ren'Py
 * on the RG DS, which draws through a window surface that must keep its
 * first size (plorpos-rgds.31). */
static int pass(void)
{
	const char *p = getenv("PORTFS_PASS");

	return p && strcmp(p, program_invocation_short_name) == 0;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags)
{
	const void *caller = __builtin_return_address(0);
	SDL_Window *(*create)(const char *, int, int, int, int, Uint32) =
		sdl_real("SDL_CreateWindow", caller);
	int (*mode)(SDL_Window *, const SDL_DisplayMode *) =
		sdl_real("SDL_SetWindowDisplayMode", caller);
	int (*fullscreen)(SDL_Window *, Uint32) = sdl_real("SDL_SetWindowFullscreen", caller);
	void (*show)(SDL_Window *) = sdl_real("SDL_ShowWindow", caller);
	SDL_DisplayMode dm = { .w = w, .h = h };
	SDL_Window *window;

	if (create && pass()) return create(title, x, y, w, h, flags);
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
	int (*fullscreen)(SDL_Window *, Uint32) =
		sdl_real("SDL_SetWindowFullscreen", __builtin_return_address(0));
	if (!pass()) flags = SDL_WINDOW_FULLSCREEN;
	return fullscreen ? fullscreen(window, flags) : -1;
}
