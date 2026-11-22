/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_COVERQ_H
#define TORTOS_COVERQ_H

#include <stdbool.h>
#include <stddef.h>

/* Album covers fetched from a server (Plex, Audiobookshelf), a few at a time
 * on threads of their own, the one on screen first (plorpos-ps8.9).
 *
 * A stack: the newest wish is fetched next, and only the newest COVERQ_KEEP
 * are kept - a fast scroll wishes for hundreds, and one scrolled past is asked
 * again if it comes back. `src` is the source's own handle on the picture (a
 * Plex thumb, an ABS item id), `base` where it goes, less ".jpg". */
typedef struct coverq coverq;

/* Fetches src to <base>.jpg, true once it is there. On a worker thread. */
typedef bool (*coverq_fetch_fn)(const char *src, const char *base);

coverq *coverq_new(coverq_fetch_fn fetch);
/* Wished for: fetched next. Already waiting: moved up to be fetched next. */
void coverq_want(coverq *q, const char *src, const char *base);
/* Still wished for (drawn and not here yet): moved up if it is waiting. Never
 * adds one, so a cover that cannot be fetched is not asked for every frame. */
void coverq_bump(coverq *q, const char *base);
/* One that landed, by its base: true while there are any. */
bool coverq_take(coverq *q, char *base, size_t n);
/* Nothing waiting (one may still be on its way): room to fetch ahead. */
bool coverq_idle(coverq *q);

#endif
