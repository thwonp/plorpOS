/* SPDX-License-Identifier: MIT */
/* PokeDaisy's options. See daisy.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "daisy.h"

/* The keys and ranges are PokeDaisy's (3ds/core/pd_settings.c, as patched). */
static const int FF[] = { 2, 3, 4, 5, 6, 8, 10, 0 };
#define NFF ((int)(sizeof FF / sizeof FF[0]))
static const char *const FF_LABEL[NFF] = { "2x", "3x", "4x", "5x", "6x", "8x", "10x", "Max" };
/* Aspect and scaling as one row: Original's three, then Expand and Zoom Out. */
static const char *const ASPECT[] = { "Integer", "Fit", "Stretch", "Expand", "Zoom Out" };
#define NASPECT 5

static int ff_index(int ff)
{
	int i;

	for (i = 0; i < NFF; i++)
		if (FF[i] == ff) return i;
	return 0;
}

static int aspect_index(const daisy_opts *o)
{
	return o->aspect ? 2 + o->aspect : o->scaling;
}

void daisy_load(const char *path, daisy_opts *o)
{
	FILE *f = fopen(path, "r");
	char line[128];

	*o = (daisy_opts){ .ff = 2, .autozoom = true, .hud = true };
	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		int v;

		if (!eq) continue;
		*eq = '\0';
		v = atoi(eq + 1);
		if (!strcmp(line, "aspect") && v >= 0 && v <= 2)        o->aspect = v;
		else if (!strcmp(line, "scaling") && v >= 0 && v <= 2)  o->scaling = v;
		else if (!strcmp(line, "ff") && FF[ff_index(v)] == v)   o->ff = v;
		else if (!strcmp(line, "autozoom"))   o->autozoom = v != 0;
		else if (!strcmp(line, "ffaudio"))    o->ffaudio = v != 0;
		else if (!strcmp(line, "rewind"))     o->rewind = v != 0;
		else if (!strcmp(line, "runindoors")) o->runindoors = v != 0;
		else if (!strcmp(line, "wildoff"))    o->wildoff = v != 0;
		else if (!strcmp(line, "hud"))        o->hud = v != 0;
	}
	fclose(f);
}

bool daisy_save(const char *path, const daisy_opts *o)
{
	const struct { const char *key; int v; } mine[] = {
		{ "aspect", o->aspect }, { "scaling", o->scaling }, { "ff", o->ff },
		{ "autozoom", o->autozoom }, { "ffaudio", o->ffaudio },
		{ "rewind", o->rewind }, { "runindoors", o->runindoors },
		{ "wildoff", o->wildoff }, { "hud", o->hud },
	};
	enum { N = sizeof mine / sizeof mine[0] };
	char tmp[1100], line[128];
	bool written[N] = { false };
	FILE *in = fopen(path, "r"), *out;
	int i;

	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if (!(out = fopen(tmp, "w"))) {
		if (in) fclose(in);
		return false;
	}
	while (in && fgets(line, sizeof line, in)) {
		size_t k = strcspn(line, "=");

		for (i = 0; i < N; i++)
			if (strlen(mine[i].key) == k && !strncmp(line, mine[i].key, k)) break;
		if (i == N) {
			fputs(line, out);
		} else if (!written[i]) {
			fprintf(out, "%s=%d\n", mine[i].key, mine[i].v);
			written[i] = true;
		}
	}
	if (in) fclose(in);
	for (i = 0; i < N; i++)
		if (!written[i]) fprintf(out, "%s=%d\n", mine[i].key, mine[i].v);
	if (fclose(out) != 0) return false;
	return rename(tmp, path) == 0;
}

static const char *on_off(bool on) { return on ? "On" : "Off"; }

int daisy_rows(const daisy_opts *o, menu_row *out)
{
	out[DO_ASPECT]     = (menu_row){ "Aspect", ASPECT[aspect_index(o)], true };
	/* Zoom Out's: back in to Expand off the plain overworld. */
	out[DO_AUTOZOOM]   = (menu_row){ "Auto Zoom In", on_off(o->autozoom), o->aspect == 2 };
	out[DO_FF]         = (menu_row){ "Fast-Forward Speed", FF_LABEL[ff_index(o->ff)], true };
	out[DO_FFSOUND]    = (menu_row){ "Fast-Forward Sound", o->ffaudio ? "Sped Up" : "Off", true };
	out[DO_REWIND]     = (menu_row){ "Rewind", on_off(o->rewind), true };
	out[DO_RUNINDOORS] = (menu_row){ "Run Indoors", on_off(o->runindoors), true };
	out[DO_WILD]       = (menu_row){ "Wild Encounters", on_off(!o->wildoff), true };
	out[DO_HUD]        = (menu_row){ "HUD", on_off(o->hud), true };
	out[DO_ROWS]       = MENU_RULE;
	out[DO_ROWS + 1]   = MENU_NOTE("More in PokeDaisy: X, then Settings");
	return DO_ROWS + 2;
}

void daisy_step(daisy_opts *o, int row, int dir)
{
	int i;

	switch (row) {
	case DO_ASPECT:
		i = (aspect_index(o) + NASPECT + dir) % NASPECT;
		o->aspect  = i > 2 ? i - 2 : 0;
		if (i <= 2) o->scaling = i;   /* Expand keeps Original's scaling */
		break;
	case DO_AUTOZOOM:   o->autozoom = !o->autozoom; break;
	case DO_FF:         o->ff = FF[(ff_index(o->ff) + NFF + dir) % NFF]; break;
	case DO_FFSOUND:    o->ffaudio = !o->ffaudio; break;
	case DO_REWIND:     o->rewind = !o->rewind; break;
	case DO_RUNINDOORS: o->runindoors = !o->runindoors; break;
	case DO_WILD:       o->wildoff = !o->wildoff; break;
	case DO_HUD:        o->hud = !o->hud; break;
	}
}
