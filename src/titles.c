/* SPDX-License-Identifier: MIT */
#include "titles.h"

#include <stdio.h>
#include <string.h>

#include "fbneodat.h"
#include "setmap.h"

typedef struct {
	game_list *l;
	int hits;
} from_db;

/* Linear over the shelf for each row: a shelf is hundreds of games and a
 * folder's rows about as many, at scan time, not per frame. */
static void take(void *ctx, const char *file, const char *title)
{
	from_db *c = ctx;
	int i;

	for (i = 0; i < c->l->count; i++) {
		game_entry *g = &c->l->items[i];

		if (strcmp(g->file, file)) continue;
		snprintf(g->title, sizeof g->title, "%s", title);
		c->hits++;
		return;
	}
}

void titles_apply(game_list *l, db *d, const char *folder, const char *dat, const char *map)
{
	from_db c = { l, 0 };
	int i, sets = 0, mapped = 0, hidden = 0;

	if (!l || l->count <= 0) return;
	for (i = 0; (map || dat) && i < l->count; ) {
		game_entry *g = &l->items[i];
		const char *t = map ? setmap_title(map, g->name) : NULL;

		if (t && t[0] == '.') {   /* a BIOS set: not a game */
			memmove(g, g + 1, (size_t)(l->count - i - 1) * sizeof *g);
			l->count--;
			hidden++;
			continue;
		}
		if (t) {
			snprintf(g->title, sizeof g->title, "%s", t);
			mapped++;
		} else if (dat && (t = fbneo_desc(dat, g->name))) {
			lib_title(t, g->title, sizeof g->title);
			sets++;
		}
		i++;
	}
	/* After the table, so a gamelist's name wins over it. */
	db_game_titles(d, folder, take, &c);
	if (sets || mapped || c.hits)
		fprintf(stderr, "titles: %-16s %d from a gamelist, %d from the set map, %d from FBNeo's list, %d hidden\n",
		        folder, c.hits, mapped, sets, hidden);
	lib_sort(l);
}
