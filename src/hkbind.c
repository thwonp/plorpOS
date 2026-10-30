/* SPDX-License-Identifier: MIT */
/* See hkbind.h. */
#include <stdio.h>
#include <string.h>

#include "hkbind.h"

/* Cycling order: the modifier layer first (what every binding was before
 * plorpos-gkd.43.2, so indexes 1-8 kept their meaning), then the same buttons
 * direct, then the directions, which only exist with the modifier - except
 * the Brick Pro's right stick, last, on both layers (diatom's ADR-0044). */
#define HK_BUTTONS(p) p "l1", p "r1", p "l2", p "r2", p "a", p "b", p "x", p "y"
const char *const HK_TRIG_NAME[] = {
	"None",
	"L1", "R1", "L2", "R2", "A", "B", "X", "Y",
	"L1", "R1", "L2", "R2", "A", "B", "X", "Y",
	"Up", "Down", "Left", "Right",
	"Stick Up", "Stick Down", "Stick Left", "Stick Right",
	"R Stick Up", "R Stick Down", "R Stick Left", "R Stick Right",
	"R Stick Up", "R Stick Down", "R Stick Left", "R Stick Right",
	"Fn1", "Fn2", "Fn1", "Fn2",   /* the Bricks' function keys (plorpos-reo.22) */
	"R Stick Click", "R Stick Click",  /* R3, no longer a modifier (plorpos-6ei.6) */
};
static const char *const HK_TRIG_WIRE[] = {
	NULL,
	HK_BUTTONS(""),
	HK_BUTTONS("d."),
	"up", "down", "left", "right",
	"sup", "sdown", "sleft", "sright",
	"rsup", "rsdown", "rsleft", "rsright",
	"d.rsup", "d.rsdown", "d.rsleft", "d.rsright",
	"fn1", "fn2", "d.fn1", "d.fn2",
	"r3", "d.r3",
};
enum { HK_FIRST_DIRECT = 9, HK_FIRST_DPAD = 17, HK_FIRST_STICK = 21,
       HK_FIRST_RSTICK = 25, HK_FIRST_RSTICK_DIRECT = 29,
       HK_FIRST_FN = 33, HK_FIRST_FN_DIRECT = 35, HK_R3 = 37, HK_R3_DIRECT = 38 };

int hk_trig_mod(int t)
{
	return t > 0 && (t < HK_FIRST_DIRECT
	                 || (t >= HK_FIRST_DPAD && t < HK_FIRST_RSTICK_DIRECT)
	                 || (t >= HK_FIRST_FN && t < HK_FIRST_FN_DIRECT) || t == HK_R3);
}
int hk_trig_stick(int t) { return t >= HK_FIRST_STICK && t < HK_FIRST_RSTICK; }
int hk_trig_stickdir(int t) { return t >= HK_FIRST_STICK && t < HK_FIRST_FN; }

/* A binding's own modifier (plorpos-6ei.6): the wire's prefix, "l3+a". */
const char *const HK_MOD_WIRE[HK_MOD_COUNT] = { "menu", "select", "l3", "home" };
int hk_mod_from_wire(const char *w)
{
	int m;

	for (m = 0; w && m < HK_MOD_COUNT; m++)
		if (!strcmp(w, HK_MOD_WIRE[m])) return m;
	return -1;
}

int hk_trig_from(int input, int mod)
{
	if (input < 0 || input >= HK_IN_COUNT) return 0;
	if (input < HK_IN_UP) return (mod ? 1 : HK_FIRST_DIRECT) + input;
	if (input == HK_IN_R3) return mod ? HK_R3 : HK_R3_DIRECT;
	if (input >= HK_IN_FN1)
		return (mod ? HK_FIRST_FN : HK_FIRST_FN_DIRECT) + (input - HK_IN_FN1);
	if (input >= HK_IN_RSUP)
		return (mod ? HK_FIRST_RSTICK : HK_FIRST_RSTICK_DIRECT) + (input - HK_IN_RSUP);
	return mod ? HK_FIRST_DPAD + (input - HK_IN_UP) : 0;
}
const char *const HK_ACTION_LABEL[] = {
	"Fast-Forward", "Rewind", "Quick Save", "Quick Load", "Screenshot",
	"Turbo Assign",  /* plorpos-tkh, Diatom's ADR-0045 */
	"Screen Layout ->", /* the DS's Display Mode, last so other shelves drop it (plorpos-3yv.47, .62) */
	"Screen Layout <-", /* the same round the other way, where there are four layouts (plorpos-reo.21) */
	"Stylus Tap",    /* the DS's on a port with no panel (the GKD's cursor, plorpos-46o.4) */
	"Stylus Mode",   /* the DS's on a port with neither panel nor stick: d-pad as cursor (plorpos-reo.21) */
	"Blow Into Mic"  /* the DS's everywhere: held, noise into the mic (plorpos-6ei.3) */
};
static const char *const HK_ACTION_WIRE[] = {
	"ff", "rewind", "savestate", "loadstate", "screenshot", "turbo", "swap", "swapback", "stylus", "stylusmode", "blow"
};

/* hkbind.h's HK_TRIG_COUNT/HK_ROW_COUNT are plain numbers, not derived from
 * these arrays' own sizes - an extern array is an incomplete type in the
 * header that declares it, so sizeof cannot cross that boundary the way it
 * could when everything lived in one file (see BRIGHT_MAX in platform.c for
 * that in-file version of the same idiom). Checked here instead, at the one
 * place both the header's numbers and these arrays are in scope together, so
 * a mismatch fails the build rather than corrupting a caller's stack array. */
_Static_assert(sizeof HK_TRIG_NAME / sizeof *HK_TRIG_NAME == HK_TRIG_COUNT,
               "HK_TRIG_COUNT must match HK_TRIG_NAME's length");
_Static_assert(sizeof HK_TRIG_WIRE / sizeof *HK_TRIG_WIRE == HK_TRIG_COUNT,
               "HK_TRIG_COUNT must match HK_TRIG_WIRE's length");
_Static_assert(sizeof HK_ACTION_LABEL / sizeof *HK_ACTION_LABEL == HK_ROW_COUNT,
               "HK_ROW_COUNT must match HK_ACTION_LABEL's length");
_Static_assert(sizeof HK_ACTION_WIRE / sizeof *HK_ACTION_WIRE == HK_ROW_COUNT,
               "HK_ROW_COUNT must match HK_ACTION_WIRE's length");

void hk_parse(const char *spec, int trig_for_row[HK_ROW_COUNT], int mod_for_row[HK_ROW_COUNT])
{
	char buf[320], *save = NULL, *tok;
	int i;

	for (i = 0; i < HK_ROW_COUNT; i++) {
		trig_for_row[i] = 0;
		if (mod_for_row) mod_for_row[i] = -1;
	}
	if (!spec || !*spec) return;
	snprintf(buf, sizeof buf, "%s", spec);
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(tok, ':'), *plus = strchr(tok, '+'), *in = tok;
		int b, r, m = -1;

		if (!colon) continue;
		*colon = '\0';
		if (plus && plus < colon) {
			*plus = '\0';
			if ((m = hk_mod_from_wire(tok)) < 0) continue;
			in = plus + 1;
		}
		for (b = 1; b < HK_TRIG_COUNT; b++)
			if (!strcmp(in, HK_TRIG_WIRE[b])) break;
		if (b == HK_TRIG_COUNT || (m >= 0 && !hk_trig_mod(b))) continue;
		for (r = 0; r < HK_ROW_COUNT; r++)
			if (!strcmp(colon + 1, HK_ACTION_WIRE[r])) break;
		if (r == HK_ROW_COUNT) continue;
		trig_for_row[r] = b;
		if (mod_for_row) mod_for_row[r] = m;
	}
}

void hk_serialize(const int trig_for_row[HK_ROW_COUNT], const int mod_for_row[HK_ROW_COUNT],
                  char *out, size_t n)
{
	char *p = out;
	size_t left = n;
	int r;

	*p = '\0';
	for (r = 0; r < HK_ROW_COUNT; r++) {
		int written;

		if (!trig_for_row[r]) continue;
		{
			int t = trig_for_row[r], m = mod_for_row && hk_trig_mod(t) ? mod_for_row[r] : -1;

			written = snprintf(p, left, "%s%s%s%s:%s", p == out ? "" : ",",
			                   m >= 0 && m < HK_MOD_COUNT ? HK_MOD_WIRE[m] : "",
			                   m >= 0 && m < HK_MOD_COUNT ? "+" : "",
			                   HK_TRIG_WIRE[t], HK_ACTION_WIRE[r]);
		}
		if (written < 0 || (size_t)written >= left) break;
		p += written;
		left -= (size_t)written;
	}
}
