/* SPDX-License-Identifier: MIT */
#include "gbpal.h"

#include <stdio.h>
#include <string.h>

/* Values exactly as mgba 0.11 declares mgba_gb_colors - arrows and all, they
 * are the buttons held at a Game Boy Color's boot - and labels without the
 * arrows, which the menu font may not have.
 *
 * Auto is what a Game Boy Color does with nothing held: mgba_gb_colors_preset
 * 1 looks the game up in the GBC boot ROM's table (144 games - Pokemon Red
 * comes out red) and anything not in it gets Dark Green, the GBC's own
 * default. The preset is read at load, so switching TO Auto mid-game shows the
 * fallback until the next launch; every fixed palette applies at once. Checked
 * on the GKD 2026-10-03 with one-shots: Pokemon Red under Auto red and green,
 * a homebrew game under Auto identical to Dark Green, and a resume state made
 * under Auto loaded grey under Grayscale - a state does not carry the palette,
 * so the choice holds on a game already played. */
static const struct { const char *label, *value, *gambatte; } PAL[GBPAL_COUNT] = {
	{ "Auto",            "GBC Dark Green →A", NULL },
	{ "Grayscale",       "Grayscale", "GBC - Grayscale" },
	{ "DMG Green",       "DMG Green", "GB - DMG" },
	{ "GB Pocket",       "GB Pocket", "GB - Pocket" },
	{ "GB Light",        "GB Light", "GB - Light" },
	{ "GBC Brown",       "GBC Brown ↑", "GBC - Brown" },
	{ "GBC Red",         "GBC Red ↑A", "GBC - Red" },
	{ "GBC Dark Brown",  "GBC Dark Brown ↑B", "GBC - Dark Brown" },
	{ "GBC Pale Yellow", "GBC Pale Yellow ↓", "GBC - Pastel Mix" },
	{ "GBC Orange",      "GBC Orange ↓A", "GBC - Orange" },
	{ "GBC Yellow",      "GBC Yellow ↓B", "GBC - Yellow" },
	{ "GBC Blue",        "GBC Blue ←", "GBC - Blue" },
	{ "GBC Dark Blue",   "GBC Dark Blue ←A", "GBC - Dark Blue" },
	{ "GBC Gray",        "GBC Gray ←B", "GBC - Grayscale" },
	{ "GBC Green",       "GBC Green →", "GBC - Green" },
	{ "GBC Dark Green",  "GBC Dark Green →A", "GBC - Dark Green" },
	{ "GBC Reverse",     "GBC Reverse →B", "GBC - Inverted" },
};

const char *gbpal_label(int i)
{
	return i >= 0 && i < GBPAL_COUNT ? PAL[i].label : PAL[0].label;
}

int gbpal_find(const char *label)
{
	int i;

	if (!label) return 0;
	for (i = 0; i < GBPAL_COUNT; i++)
		if (!strcmp(PAL[i].label, label)) return i;
	return 0;
}

int gbpal_opts(int i, int gambatte, char *preset, size_t pcap, char *colors, size_t ccap)
{
	int a, b;

	if (i < 0 || i >= GBPAL_COUNT) i = 0;
	if (gambatte) {
	/* Gambatte (plorpos-f1p.77, every port's since plorpos-ssa): a colorization mode, and under Internal
	 * the palette's name. Both apply as the game runs, Auto included once the core re-reads them, but
	 * Auto's per-game table is read at load, as mgba's was. Auto sends a fixed one beside it, which
	 * the core ignores, so a switch from a fixed palette to Auto is the same pair of keys. Grayscale
	 * and GBC Gray are the one palette there (the owner's call). */
	a = snprintf(preset, pcap, "gambatte_gb_colorization=%s", i == 0 ? "auto" : "internal");
	b = snprintf(colors, ccap, "gambatte_gb_internal_palette=%s", i == 0 ? "GBC - Dark Green" : PAL[i].gambatte);
	} else {
	a = snprintf(preset, pcap, "mgba_gb_colors_preset=%d", i == 0 ? 1 : 0);
	b = snprintf(colors, ccap, "mgba_gb_colors=%s", PAL[i].value);
	}
	return a > 0 && (size_t)a < pcap && b > 0 && (size_t)b < ccap;
}
