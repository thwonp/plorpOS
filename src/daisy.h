/* SPDX-License-Identifier: MIT */
/* PokeDaisy's options in the in-game menu (plorpos-wjw): the rows, and
 * PokeDaisy's own DATA/settings.ini they read and write - one file, so these
 * and the companion's SETTINGS can never disagree. PokeDaisy reads it again
 * when it comes back from a pause (mk/patches/pokedaisy-plorpos.patch), which
 * the menu is. No SDL, so tools/menu-check.c drives the real thing. */
#ifndef TORTOS_DAISY_H
#define TORTOS_DAISY_H

#include <stdbool.h>

#include "menu.h"

typedef struct {
	int  aspect;     /* settings.ini aspect: 0 Original, 1 Expand, 2 Zoom Out */
	int  scaling;    /* scaling: Original's 0 Integer, 1 Fit, 2 Stretch */
	int  ff;         /* ff: 2-10 times, 0 = Max */
	bool autozoom, ffaudio, rewind, runindoors, wildoff, hud;
} daisy_opts;

typedef enum {
	DO_ASPECT, DO_AUTOZOOM, DO_FF, DO_FFSOUND, DO_REWIND, DO_RUNINDOORS,
	DO_WILD, DO_HUD, DO_ROWS
} daisy_row;

/* PokeDaisy's defaults, then what the file says. */
void daisy_load(const char *path, daisy_opts *o);
/* Every other line of the file kept (the companion's own: tab bar, HUD
 * parts ...); through a temporary file and a rename. */
bool daisy_save(const char *path, const daisy_opts *o);

/* DO_ROWS rows, then a rule and a note; returns how many. */
int  daisy_rows(const daisy_opts *o, menu_row *out);
/* Left (-1) or right / A (+1) on a row: its next value, round. */
void daisy_step(daisy_opts *o, int row, int dir);

#endif
