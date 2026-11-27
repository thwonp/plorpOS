/* SPDX-License-Identifier: MIT */
/* Muse+'s empty Local Source lines (src/muse_empty.h, plorpos-5tm.23) wrap
 * cleanly on every port and face: each '\n' phrase whole on a line of its own
 * (never cut further), three lines at most, each within the list's room. Measured with SDL_ttf and the
 * shipped fonts at the sizes ui.c opens them, in layout units as main.c's
 * empty-list drawer sees them, through the same text_wrap. */
#include "../src/muse_empty.h"
#include "../src/textwrap.h"

#include <SDL_ttf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ui.c: FONT_BASE * font_mul[UI_F_LIST] (the list rows) */
#define LIST_PT (32.0f * 1.15f * 1.34f * 40.0f / 48.0f)

/* size: the port's default Body Size (main.c font_size_default, plorpos-bn9.6) */
static const struct { const char *name; int w, h; float size; int flip; } PORTS[] = {
	{ "brick",        1024, 768, 1.00f, 0 },
	{ "brick pro",    1024, 768, 0.80f, 0 },
	{ "gkd",           640, 480, 1.00f, 0 },
	{ "rg ds",         640, 480, 0.90f, 0 },
	{ "rotate",        720, 720, 0.90f, 0 },   /* the H700 macros: 4:3 or narrower */
	{ "rg sp/34xx sp", 720, 480, 1.00f, 0 },
	{ "rg35xx h/sp, zero 28", 640, 480, 0.90f, 0 },
	{ "h700 720x720",  720, 720, 0.90f, 0 },
	{ "zero 40",       480, 800, 1.00f, 0 },   /* portrait: 768 units across */
	{ "miyoo flip",    752, 560, 1.00f, 1 },
};
static const struct { const char *file; float scale; } FACES[] = {   /* main.c FONTS[] */
	{ "head.ttf", 1.00f }, { "josefin.ttf", 1.00f }, { "menu.ttf", 0.98f }, { "lato.ttf", 1.03f },
	{ "jp.ttf", 0.96f },
};

/* UI Settings > Fonts > Body Size over the default (plorpos-bn9.6): at the
 * default each phrase is whole on its line, three lines at most; at the ends
 * of the range a phrase may wrap again, into four lines, but no word is lost. */
static const float SIZES[] = { 1.0f, 0.5f, 1.5f };
static float g_ts;
static int width(void *font, const char *s)
{
	int w = 0;

	TTF_SizeUTF8((TTF_Font *)font, s, &w, NULL);
	return g_ts == 1.0f ? w : (int)ceilf(w / g_ts);
}

int main(void)
{
	int fail = 0, runs = 0;

	if (TTF_Init() != 0) { fprintf(stderr, "TTF_Init: %s\n", TTF_GetError()); return 1; }
	for (size_t p = 0; p < sizeof PORTS / sizeof *PORTS; p++) {
		/* platform.c plat_geometry_init */
		float ts = (float)PORTS[p].w / 1024 < (float)PORTS[p].h / 768 ? (float)PORTS[p].w / 1024
		                                                              : (float)PORTS[p].h / 768;
		if (PORTS[p].h > PORTS[p].w) ts = (float)PORTS[p].w / 768;
		int W = (int)(PORTS[p].w / ts), room = W - 2 * (W / 40);
		const char *msgs[2] = { MUSE_NO_BOOKS, PORTS[p].flip ? MUSE_NO_MUSIC_CARD : MUSE_NO_MUSIC_SRV };

		g_ts = ts;
		for (size_t sz = 0; sz < sizeof SIZES / sizeof *SIZES; sz++)
		for (size_t f = 0; f < sizeof FACES / sizeof *FACES; f++) {
			char path[64];
			int pt = (int)(LIST_PT * FACES[f].scale * PORTS[p].size * SIZES[sz] * ts + 0.5f);
			TTF_Font *font;

			snprintf(path, sizeof path, "res/fonts/%s", FACES[f].file);
			if (!(font = TTF_OpenFont(path, pt))) { fprintf(stderr, "%s: %s\n", path, TTF_GetError()); return 1; }
			for (int m = 0; m < 2; m++) {
				char lines[4][TEXTWRAP_LINE];
				int n = text_wrap(msgs[m], room, width, font, lines, 4), ok = n <= 3;
				const char *t = msgs[m];

				if (sz == 0) {
					/* Line l is phrase l, whole. */
					for (int l = 0; l < n && ok; l++) {
						size_t k = strcspn(t, "\n");

						ok = strlen(lines[l]) == k && !strncmp(lines[l], t, k);
						t += k + (t[k] == '\n');
					}
					ok = ok && !*t;
				} else {
					/* Every word kept: the lines, joined, are the message. */
					char all[4 * TEXTWRAP_LINE] = "", want[4 * TEXTWRAP_LINE];

					for (int l = 0; l < n; l++) {
						if (l) strcat(all, " ");
						strcat(all, lines[l]);
					}
					snprintf(want, sizeof want, "%s", msgs[m]);
					for (char *c = want; *c; c++) if (*c == '\n') *c = ' ';
					ok = n <= 4 && !strcmp(all, want);
				}
				for (int l = 0; l < n; l++) ok = ok && width(font, lines[l]) <= room;
				runs++;
				if (!ok) {
					printf("%s %-13s %3d%% %-11s room %4d:", ok ? "  " : "!!", PORTS[p].name,
					       (int)(SIZES[sz] * 100), FACES[f].file, room);
					for (int l = 0; l < n; l++) printf(" [%s %d]", lines[l], width(font, lines[l]));
					printf("\n");
				}
				fail += !ok;
			}
			TTF_CloseFont(font);
		}
	}
	TTF_Quit();
	if (fail) { printf("emptywrap-check: %d of %d FAILED\n", fail, runs); return 1; }
	printf("emptywrap-check: %d message/port/face cases, every phrase whole on its own line\n", runs);
	return 0;
}
