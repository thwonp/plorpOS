/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_DETAILS_H
#define TORTOS_DETAILS_H

#include <stdbool.h>
#include <SDL.h>

/* The RG DS's second screen (plorpos-rgds.18): the game under the shelf's
 * cursor, three pages to swipe between - Card, Story, Stats (the owner's
 * order, A - C - B of the mockups). Drawing only: the caller gathers the
 * facts and owns the window. Its own fonts and textures, because the ui.c
 * text cache belongs to the shelf's renderer and this is a second one. */

#define DETAILS_PAGES 3

typedef struct {
	char     title[256];
	char     sys[24];          /* the pill: the system's short name */
	unsigned accent;           /* its colour, 0xRRGGBB */
	char     year[8], publisher[96], genre[128], players[16];
	char     synopsis[1024];
	char     played[32];       /* "12 h 40 min", empty = never */
	char     last[32];         /* "Yesterday", empty = never */
	int      launches;
	bool     cheevos;          /* a set exists */
	int      ch_earned, ch_total, pts_earned, pts_total;
	char     art[1024];        /* box art path, empty = none */
} details_info;

/* `font_dir` holds head.ttf and lato.ttf. Fonts open on first use. */
void details_init(const char *font_dir);
void details_draw(SDL_Renderer *r, int w, int h, const details_info *d, int page);
/* The system under the carousel's cursor, while no game is (plorpos-rgds.18.1). */
void details_draw_system(SDL_Renderer *r, int w, int h, const char *name, unsigned accent, const char *line);
/* Fonts and the cached cover texture; before the renderer goes. */
void details_quit(void);

#endif
