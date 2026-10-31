/* SPDX-License-Identifier: MIT */
/* Button Mapping's rows, spec and resolved targets (plorpos-c4l). */
#include <stdio.h>
#include <string.h>

#include "../src/btnmap.h"

static int failures;

#define CHECK(cond, what) do { \
	if (cond) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); failures++; } \
} while (0)

#define SRC(n) bm_src_index(n)

int main(void)
{
	const bm_row *rows;
	int held[BM_ROWS_MAX], i, same = 1;
	char out[160];

	CHECK(bm_rows("NES", &rows) == 2, "NES: two rows");
	CHECK(bm_rows("MD", &rows) == 6 && !strcmp(rows[2].label, "C") && !strcmp(rows[2].target, "a"),
	      "Genesis: six rows, C is retropad A");
	CHECK(bm_rows("ARCADE", &rows) == 8, "an unlisted system gets the generic eight");

	/* defaults */
	for (i = 0; i < BM_SRC_N; i++)
		if (strcmp(bm_target("SFC", "", i), BM_SRC[i])) same = 0;
	CHECK(same, "empty spec: every button is itself");
	bm_parse("MD", "", held);
	bm_serialize("MD", held, out, sizeof out);
	CHECK(out[0] == '\0', "defaults serialize to nothing");

	/* swap A and B on the NES */
	bm_parse("NES", "", held);
	held[0] = SRC("b");
	held[1] = SRC("a");
	bm_serialize("NES", held, out, sizeof out);
	CHECK(!strcmp(out, "a:b,b:a"), "swap serializes every row");
	CHECK(!strcmp(bm_target("NES", out, SRC("a")), "b") && !strcmp(bm_target("NES", out, SRC("b")), "a"),
	      "swap resolves both ways");
	CHECK(!strcmp(bm_target("NES", out, SRC("x")), "x"), "NES X is left to the core");

	/* move A onto X: A's old row default goes to none */
	CHECK(!strcmp(bm_target("NES", "a:x,b:b", SRC("x")), "a"), "X sends A");
	CHECK(!strcmp(bm_target("NES", "a:x,b:b", SRC("a")), "none"), "A, moved off, sends nothing");

	/* a cleared row */
	CHECK(!strcmp(bm_target("GBA", "a:a,b:b,l1:-,r1:r1", SRC("l1")), "none"), "a cleared row's button is none");
	bm_parse("GBA", "a:a,b:b,l1:-,r1:r1", held);
	CHECK(held[2] == -1 && held[3] == SRC("r1"), "parse reads - as none");

	/* robustness */
	bm_parse("NES", "zz:a,a:q,b", held);
	CHECK(held[0] == -1 && held[1] == SRC("b"), "junk pairs are skipped");

	if (failures) {
		printf("\n%d check(s) failed\n", failures);
		return 1;
	}
	printf("\nok: every check passed\n");
	return 0;
}
