/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_SETMAP_H
#define TORTOS_SETMAP_H

#include <stdbool.h>
#include <stddef.h>

/* An arcade set's title from the core's own romset map: res/maps/<core>.txt,
 * `set.zip<TAB>Title` a line (the Nano port's maps, plorpos-ggv.46).
 *
 * setmap_path fills `buf` with <res_dir>/maps/<core>.txt for the two cores that
 * have a map, mame2000 and fbalpha2012 (a "fbalpha2012_*" core too), and returns
 * it; any other core gets NULL, and its shelf is named some other way (FBNeo's
 * table, or the file).
 *
 * setmap_title is the title for `set` (a stem or a file, any case), or NULL when
 * the map has no such set or cannot be read. A title starting with "." marks a
 * BIOS set to leave off the shelf. Each map is read once and kept. */
const char *setmap_path(const char *res_dir, const char *core, char *buf, size_t n);
const char *setmap_title(const char *path, const char *set);

#endif
