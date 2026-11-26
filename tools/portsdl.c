/* SPDX-License-Identifier: MIT */
/* oakMOSS (Zero 40, Zero 28), PortMaster ports only (plorpos-08c.16): the
 * firmware SDL's "mali" video driver has the panel's one size, and a window
 * asked for at another size comes back at the panel's without the resize
 * event SDL sends elsewhere. MonoGame believes the size it asked for: Stardew
 * Valley drew its 1280x720 frame into the 640x480 panel, bottom-left corner
 * only. For Mono, this keeps a window to the panel's size and posts that
 * event, so the game sizes its frame to the window it got.
 *
 * Found by name ahead of the firmware's copy (pm-run.sh puts this folder
 * first on LD_LIBRARY_PATH): MonoGame opens SDL by name and looks symbols up
 * in that handle, past any preload. Everything else is the firmware's SDL,
 * a dependency of this file by absolute path (built against a stub of that
 * name). Every other program passes straight through. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>

static void *real(const char *name)
{
	return dlsym(RTLD_NEXT, name);
}

static int is_mono(void)
{
	static int v = -1;

	if (v < 0) v = !strcmp(program_invocation_short_name, "mono");
	return v;
}

/* The window is not the size asked for: say so, as a resize would. */
static void tell(SDL_Window *win, int want_w, int want_h)
{
	void (*size)(SDL_Window *, int *, int *) = real("SDL_GetWindowSize");
	Uint32 (*id)(SDL_Window *) = real("SDL_GetWindowID");
	int (*push)(SDL_Event *) = real("SDL_PushEvent");
	SDL_Event e;
	int w, h;

	if (!win || !size || !id || !push || !is_mono()) return;
	size(win, &w, &h);
	if (w == want_w && h == want_h) return;
	memset(&e, 0, sizeof e);
	e.type = SDL_WINDOWEVENT;
	e.window.windowID = id(win);
	e.window.data1 = w;
	e.window.data2 = h;
	e.window.event = SDL_WINDOWEVENT_RESIZED;
	push(&e);
	e.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
	push(&e);
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	SDL_Window *(*create)(const char *, int, int, int, int, Uint32) = real("SDL_CreateWindow");
	SDL_Window *win;

	if (!create) return NULL;
	win = create(title, x, y, w, h, flags);
	tell(win, w, h);
	return win;
}

/* Out of fullscreen the driver takes any size as the window's, though the
 * panel shows only its own: Stardew left fullscreen and asked for 1280x720.
 * For Mono, no larger than the panel, and the game told what it got. */
void SDL_SetWindowSize(SDL_Window *win, int w, int h)
{
	void (*set)(SDL_Window *, int, int) = real("SDL_SetWindowSize");
	int (*desktop)(int, SDL_DisplayMode *) = real("SDL_GetDesktopDisplayMode");
	SDL_DisplayMode dm;
	int cw = w, ch = h;

	if (!set) return;
	if (is_mono() && desktop && desktop(0, &dm) == 0 && (w > dm.w || h > dm.h)) {
		cw = dm.w;
		ch = dm.h;
	}
	set(win, cw, ch);
	tell(win, w, h);
}
