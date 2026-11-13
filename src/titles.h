/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_TITLES_H
#define TORTOS_TITLES_H

#include "db.h"
#include "library.h"

/* Give a scanned shelf the titles it shows, then sort it by them.
 *
 * First choice is the title a gamelist import put in the games table, as the
 * list wrote it. Then, for a shelf whose core is fbneo, `dat` - the set table
 * tools/gen-fbneo-titles.py writes - with the brackets cut (lib_title): an
 * arcade zip is named for its set, and "mk3" is not a title. Pass NULL for any
 * other shelf. Otherwise the title lib_scan already gave it from the filename.
 *
 * A shelf on mame2000 or fbalpha2012 is named from `map` first (setmap.h: the
 * core's own romset, not FBNeo's), `dat` for a set the map does not list or
 * when the card has no map, and a set the map titles "." is left off the shelf
 * (a BIOS set). `map` is NULL for any other shelf.
 *
 * A missing table is not an error: those shelves show set names, as they did
 * before it existed. */
void titles_apply(game_list *l, db *d, const char *folder, const char *dat, const char *map);

#endif
