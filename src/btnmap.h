/* SPDX-License-Identifier: MIT */
/* Button Mapping (plorpos-c4l): which physical button sends each of a
 * system's game buttons, per system. Split out of main.c and platform.c
 * under ADR-0001 so tools/btnmap-check.c drives it with no SDL.
 *
 * A row is a game button as the system's own pad names it ("C" on the
 * Genesis, "II" on the TurboGrafx-16), and the canonical button Diatom sends
 * the core for it - so a row's default physical button is that same
 * canonical one. Two-button systems have two rows: the cores put their own
 * turbo or nothing on X and Y, and those stay as they are. START and SELECT
 * are never rows.
 *
 * The spec is what the db keeps: "" for the defaults, else every row as
 * `<row target>:<physical>` with `-` for none ("y:x,b:b,a:-"). The map
 * Diatom gets is derived from it (bm_target), never stored. */
#ifndef TORTOS_BTNMAP_H
#define TORTOS_BTNMAP_H

#include <stddef.h>

/* The physical buttons, by Diatom's canonical name: what a row can hold and
 * what a SETMAP source is. Index = position here. */
#define BM_SRC_N 8
extern const char *const BM_SRC[BM_SRC_N];       /* "a" .. "r2" */
extern const char *const BM_SRC_LABEL[BM_SRC_N]; /* "A" .. "R2" */

#define BM_ROWS_MAX 8
typedef struct { const char *label, *target; } bm_row;

/* The system's rows; every tag has some (a generic eight for the rest). */
int bm_rows(const char *tag, const bm_row **rows);

/* The physical button (BM_SRC index, -1 none) on each row. */
void bm_parse(const char *tag, const char *spec, int src_for_row[BM_ROWS_MAX]);
/* "" when every row holds its default. */
void bm_serialize(const char *tag, const int src_for_row[BM_ROWS_MAX],
                  char *out, size_t n);

/* What the game gets from physical button `src` under `spec`: the target of
 * the row holding it; "none" when it was a row's default and was moved off
 * it; else its own name, unchanged. */
const char *bm_target(const char *tag, const char *spec, int src);

int bm_src_index(const char *name);   /* -1 if not a BM_SRC name */

#endif
