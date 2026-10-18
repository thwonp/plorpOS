/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_RECENT_H
#define TORTOS_RECENT_H

#include <stdbool.h>

/* The game switcher's list (plorpos-gl5): the last RECENT_MAX games
 * launched, newest first, keyed like favorites - the system's tag plus the
 * launch path under Roms/<folder>.
 *
 * One row in the LIBRARY database ("recent", a line per game), since the
 * list is about the games on this card. THE LAUNCH PATH DOES NO I/O (see
 * stats.h): recent_note only moves the game to the front in memory, and the
 * game tick calls recent_flush, which writes the row when it changed. */

#define RECENT_MAX       30
#define RECENT_TAG_MAX   16
#define RECENT_FILE_MAX 544     /* LIB_PATH */

void recent_load(void);
void recent_note(const char *tag, const char *file);
/* Off the list, written at once (the switcher's X: not on a launch path). */
void recent_remove(const char *tag, const char *file);
/* Writes the row if the list changed since the last write. */
bool recent_flush(void);

int  recent_count(void);
bool recent_at(int i, const char **tag, const char **file);

#endif
