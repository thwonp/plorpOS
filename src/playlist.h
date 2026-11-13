/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_PLAYLIST_H
#define TORTOS_PLAYLIST_H

#include <stdbool.h>

/* Muse's playlists (plorpos-era.5): .m3u files in <card>/Music/Playlists,
 * the owner's choice, so they are plain files other players and a PC can
 * read and edit.
 *
 * Tracks are named the way the library names them, relative to the card
 * ("Music/Artist/Album/01 Track.flac"), and written relative to the playlist
 * ("../Artist/Album/01 Track.flac"), which is what an .m3u beside the music
 * means. Reading also takes "../../x" (card-relative), an absolute path under
 * the card, and a bare name (beside the playlist). Comment lines (#EXTM3U,
 * #EXTINF, ...) are kept on a rewrite; blank lines are not.
 *
 * Rewrites go to a temporary file and are renamed over the old one, so a
 * power cut leaves the old playlist or the new one, never half of either. */

#define PL_DIR_REL  "Music/Playlists"   /* under the card */
#define PL_NAME_MAX 96
#define PL_PATH_MAX 1024

typedef struct {
	char name[PL_NAME_MAX];   /* the file's name without .m3u */
	int  n;                   /* its entries */
} pl_info;

/* The playlists in `dir`, by name ignoring case. Returns how many (at most
 * `max`), 0 when the folder is missing. */
int  pl_list(const char *dir, pl_info *out, int max);

/* A playlist's entries as card-relative paths, in order: *out is an array of
 * strings for pl_free. `card` is the card's absolute root, for absolute
 * entries. Returns the count, or -1 when the file cannot be read. */
int  pl_read(const char *dir, const char *name, const char *card, char ***out);
void pl_free(char **v, int n);

/* Append a card-relative track, creating the folder and the file. */
bool pl_add(const char *dir, const char *name, const char *track);
/* Drop the `index`-th entry (as pl_read numbers them). */
bool pl_remove_at(const char *dir, const char *name, int index);
/* False when `to` exists or either name is not a good one. */
bool pl_rename(const char *dir, const char *from, const char *to);
bool pl_delete(const char *dir, const char *name);
bool pl_exists(const char *dir, const char *name);

/* A name that makes a good file name on exFAT: not empty, not all spaces, no
 * / \ : * ? " < > | or control characters, not starting with a dot, short
 * enough for PL_NAME_MAX. */
bool pl_name_ok(const char *name);

#endif
