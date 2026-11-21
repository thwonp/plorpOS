/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_PODCAST_H
#define TORTOS_PODCAST_H

#include <stdbool.h>
#include <stddef.h>

#include "muselib.h"
#include "musec.h"

/* Podcasts as one of Muse's sources (plorpos-7ni): shows subscribed to by
 * their feeds, read into an index on the card and browsed by Muse's own
 * screens, as Audiobookshelf's books are (abs.h). AntennaPod (GPL-3.0) was
 * the behaviour reference; nothing here comes from its code.
 *
 * THE LIBRARY, as an ml_lib: a show is an artist and each of its episodes a
 * book album of one file, newest first. An episode is then a book to the rest
 * of Muse - its place kept per episode ("book.<folder>"), finished when heard
 * to the end, a favorite on its own - with nothing new in those places. Paths
 * are <shared>/podcasts/<show id>/<episode id>/ep<ext>, with no file behind
 * them: pod_resolve turns one into the enclosure's URL or the downloaded copy.
 * Ids are hashes of the feed's URL and of the episode's guid, so they are the
 * same on every sync.
 *
 * ON THE CARD, under <shared>/podcasts/: subs.tsv (the shows: id, feed URL,
 * title, image URL, when added), <show id>/eps.tsv (its episodes, newest
 * first), <show id>/.media/show.{jpg,png} (its art, shrunk).
 *
 * The network is asked only when Muse's source opens and on Check for Updates
 * (owner: no polling in the background); everything runs on a thread of its
 * own and nothing here waits for it. */

/* `shrink`: shrinks a fetched picture in place (artshrink), run on the
 * worker; NULL leaves art as it came. */
void pod_init(const char *card, const char *shared, void (*shrink)(const char *path));

/* ---- shows ----------------------------------------------------------------------------- */

int pod_show_count(void);
/* Subscribe to `feed_url` (title and image may be ""): 1 added, 0 already
 * there, -1 could not write. Its episodes come with the next sync. */
int pod_subscribe(const char *feed_url, const char *title, const char *image);

/* An OPML file (another player's export): every show in it subscribed to.
 * Returns how many were added, -1 when it is not OPML; `had` the ones
 * already subscribed. */
int pod_import_opml(const char *path, int *had);

/* ---- syncing ------------------------------------------------------------------------------ */

/* Every show's feed fetched again, in the background. */
void pod_sync_start(void);
/* 1 running (`done` of `total` shows), 0 idle, -1 the last one reached none. */
int  pod_sync_state(int *done, int *total);
/* True once after a sync changed an index; `added` the episodes that are new
 * since the last one (NULL to not ask). */
bool pod_sync_take(int *added);

/* ---- the library ---------------------------------------------------------------------------- */

bool pod_lib_cached(void);           /* any shows at all */
bool pod_lib_load(ml_lib *out);
/* Episode `al`'s publish time in minutes since 1970, for ordering; 0 unknown. */
int  pod_album_stamp(int al);
bool pod_is_path(const char *rel);

typedef struct {
	long long stamp;                 /* published, Unix seconds */
	int dur;                         /* seconds, the feed's word; 0 unknown */
	int show;                        /* the artist it is under */
} pod_ep;
bool pod_episode(int al, pod_ep *out);

/* The show's id and the episode's ("<show>/<episode>"), for keys kept
 * across syncs. False for an album not in the library. */
bool pod_ep_key(int al, char *out, size_t n);
/* The album an episode key is now, or -1. */
int  pod_album_of_key(const char *key);
/* Downloaded copy, card-relative, or false (plorpos-7ni.6). */
bool pod_downloaded(int al, char *rel, size_t n);

/* ---- Up Next (owner, plorpos-7ni.4): the Queue tab, kept as queue.tsv ---------------- */

int  pod_queue_count(void);
int  pod_queue_album(int k);         /* the k-th one's album, -1 when gone from the library */
int  pod_queue_pos(int al);          /* where album `al` is in it, -1 not there */
void pod_queue_add(int al);          /* at the end, unless there already */
void pod_queue_remove(int al);
bool pod_queue_move(int al, int dir);/* a place up (-1) or down (+1); false at an end */

/* Where album `al`'s art is kept, without extension: its show's. */
void pod_cover_base(int al, char *out, size_t n);
bool pod_cover_want(int al, const char *base);
bool pod_cover_take(char *base, size_t n);

#endif
