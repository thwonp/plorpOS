/* SPDX-License-Identifier: MIT */
/* Button Mapping. See btnmap.h. */
#include <stdio.h>
#include <string.h>

#include "btnmap.h"

const char *const BM_SRC[BM_SRC_N] = { "a", "b", "x", "y", "l1", "r1", "l2", "r2" };
const char *const BM_SRC_LABEL[BM_SRC_N] = { "A", "B", "X", "Y", "L1", "R1", "L2", "R2" };

/* Labels and targets read from each core's input descriptors (2026-10-30):
 * PicoDrive Y=A B=B A=C L=X X=Y R=Z and Button 1 = B on the Master System;
 * beetle-pce-fast A=I B=II; mednafen_ngp B=A A=B; fake08 B=O A=X;
 * pcsx_rearmed the usual RetroPad-as-DualShock. */
static const bm_row GENERIC[] = { { "A", "a" }, { "B", "b" }, { "X", "x" }, { "Y", "y" },
	{ "L1", "l1" }, { "R1", "r1" }, { "L2", "l2" }, { "R2", "r2" } };
static const bm_row SNES[]  = { { "A", "a" }, { "B", "b" }, { "X", "x" }, { "Y", "y" },
	{ "L", "l1" }, { "R", "r1" } };
static const bm_row TWO[]   = { { "A", "a" }, { "B", "b" } };
static const bm_row GBA[]   = { { "A", "a" }, { "B", "b" }, { "L", "l1" }, { "R", "r1" } };
static const bm_row SMS[]   = { { "1", "b" }, { "2", "a" } };
static const bm_row MD[]    = { { "A", "y" }, { "B", "b" }, { "C", "a" },
	{ "X", "l1" }, { "Y", "x" }, { "Z", "r1" } };
static const bm_row PCE[]   = { { "I", "a" }, { "II", "b" } };
static const bm_row NGP[]   = { { "A", "b" }, { "B", "a" } };
static const bm_row PICO8[] = { { "O", "b" }, { "X", "a" } };
static const bm_row PS[]    = { { "Cross", "b" }, { "Circle", "a" }, { "Square", "y" },
	{ "Triangle", "x" }, { "L1", "l1" }, { "R1", "r1" }, { "L2", "l2" }, { "R2", "r2" } };

#define ROWS(t) t, (int)(sizeof t / sizeof t[0])
static const struct { const char *tag; const bm_row *rows; int n; } SYSTEMS[] = {
	{ "NES", ROWS(TWO) }, { "GB", ROWS(TWO) }, { "GBC", ROWS(TWO) },
	{ "GBA", ROWS(GBA) }, { "SFC", ROWS(SNES) }, { "NDS", ROWS(SNES) },
	{ "SMS", ROWS(SMS) }, { "GG", ROWS(SMS) },
	{ "MD", ROWS(MD) }, { "SEGACD", ROWS(MD) }, { "32X", ROWS(MD) },
	{ "PCE", ROWS(PCE) }, { "NGP", ROWS(NGP) }, { "NGPC", ROWS(NGP) },
	{ "PICO8", ROWS(PICO8) }, { "PS", ROWS(PS) },
};

int bm_rows(const char *tag, const bm_row **rows)
{
	size_t i;

	for (i = 0; tag && i < sizeof SYSTEMS / sizeof SYSTEMS[0]; i++)
		if (!strcmp(SYSTEMS[i].tag, tag)) {
			*rows = SYSTEMS[i].rows;
			return SYSTEMS[i].n;
		}
	*rows = GENERIC;
	return (int)(sizeof GENERIC / sizeof GENERIC[0]);
}

int bm_src_index(const char *name)
{
	int i;

	for (i = 0; i < BM_SRC_N; i++)
		if (!strcmp(BM_SRC[i], name)) return i;
	return -1;
}

void bm_parse(const char *tag, const char *spec, int src_for_row[BM_ROWS_MAX])
{
	const bm_row *rows;
	int n = bm_rows(tag, &rows), r;
	char buf[160], *save = NULL, *tok;

	for (r = 0; r < n; r++) src_for_row[r] = bm_src_index(rows[r].target);
	if (!spec || !*spec) return;

	/* A spec names every row; a row it leaves out (a table that grew) keeps
	 * its default, and an unknown pair is skipped rather than failing the
	 * rest. */
	snprintf(buf, sizeof buf, "%s", spec);
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
		char *c = strchr(tok, ':');
		if (!c) continue;
		*c = '\0';
		for (r = 0; r < n; r++)
			if (!strcmp(rows[r].target, tok))
				src_for_row[r] = strcmp(c + 1, "-") ? bm_src_index(c + 1) : -1;
	}
}

void bm_serialize(const char *tag, const int src_for_row[BM_ROWS_MAX],
                  char *out, size_t n)
{
	const bm_row *rows;
	int nr = bm_rows(tag, &rows), r;
	size_t k = 0;
	int dflt = 1;

	for (r = 0; r < nr; r++)
		if (src_for_row[r] != bm_src_index(rows[r].target)) dflt = 0;
	out[0] = '\0';
	if (dflt) return;
	for (r = 0; r < nr && k < n; r++)
		k += (size_t)snprintf(out + k, n - k, "%s%s:%s", r ? "," : "", rows[r].target,
		                      src_for_row[r] < 0 ? "-" : BM_SRC[src_for_row[r]]);
}

const char *bm_target(const char *tag, const char *spec, int src)
{
	const bm_row *rows;
	int n = bm_rows(tag, &rows), r, held[BM_ROWS_MAX];

	bm_parse(tag, spec, held);
	for (r = 0; r < n; r++)
		if (held[r] == src) return rows[r].target;
	for (r = 0; r < n; r++)
		if (!strcmp(rows[r].target, BM_SRC[src])) return "none";
	return BM_SRC[src];
}
