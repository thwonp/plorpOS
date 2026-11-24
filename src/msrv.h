/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_MSRV_H
#define TORTOS_MSRV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "coverq.h"
#include "muselib.h"
#include "musec.h"

/* A music server as a Muse source (plorpos-vgr.9): what Plex and Jellyfin
 * share, split out of plex.c so the second is a backend, not a copy.
 *
 * The core keeps the library in an index on the card and reads it as an
 * ml_lib whose tracks are paths in a cache folder with no files behind them:
 * <P_SHARED>/<dir>/<rel id>/<artist>/<album>/<track>.<ext>. It turns one of
 * those into what the daemon is sent (the downloaded copy, a pre-fetched one,
 * or the backend's stream), paces what is told to the server about it, and
 * runs downloads, the pre-fetch cache, covers, plays made offline and the
 * playlists' files. Everything server-shaped - the account, the address, the
 * listings, the URLs, the reports - is the backend's (msrv_ops).
 *
 * One msrv per source, side by side: a queue can hold both. Every key here is
 * the backend's own integer for a track, album or artist (Plex's ratingKey;
 * Jellyfin maps its ids to local ones). */

typedef struct msrv msrv;

#define MSRV_FAVS      "Favorite Tracks"
#define MSRV_AHEAD_MAX 10        /* the most tracks fetched ahead (the setting's top) */

/* ---- the track table (the index as read back) ---- */

typedef struct { int key; char *name; } msrv_artist;
typedef struct { int key, artist, year, first, n; char *title, *thumb; } msrv_album;
typedef struct { int key, album, dur_ms; short disc, idx; char *title, *artist, *part; } msrv_track;
typedef struct {
	msrv_artist *ar; int nar;
	msrv_album  *al; int nal;
	msrv_track  *tr; int ntr;
	int *hash; int hcap;           /* track key -> index, open addressing */
	char *arena;
} msrv_table;

int msrv_tab_find(const msrv_table *t, int key);

/* ---- the records an index is written from ---- */

typedef struct {
	int key, album, artist, year, disc, idx, dur;
	char *title, *tartist, *album_t, *artist_t, *thumb, *part;
} msrv_rec;
typedef struct { msrv_rec *r; int n, cap; long since; } msrv_set;   /* since: the newest change stamp */

void msrv_rec_free(msrv_rec *x);
/* Takes x's strings - but not on failure, when they are still the caller's. */
bool msrv_set_push(msrv_set *s, msrv_rec x);

/* ---- jobs on the source's worker thread ---- */

typedef struct msrv_job {
	int kind;
	char a[320], b[600], id[48];
	long n1, n2, n3;
	struct msrv_job *next;
} msrv_job;
#define MSRV_J_BACKEND 100        /* kinds from here on are the backend's (ops->job) */

msrv_job *msrv_job_new(int kind);
void msrv_push(msrv *m, msrv_job *j, bool front);

/* ---- playlists, as the server lists them ---- */

typedef struct { char id[48]; int leaf; bool smart; char title[200]; } msrv_spl;
enum { MSRV_PLE_ADD, MSRV_PLE_DEL, MSRV_PLE_MOVE, MSRV_PLE_RENAME, MSRV_PLE_DELETE };

/* ---- the backend ---- */

typedef struct {
	const char *tag;      /* log prefix and scratch files: "plex" */
	const char *dir;      /* <P_SHARED>/<dir> */
	const char *music;    /* downloads: <card>/Music/<music> */
	const char *pl_pre;   /* a downloaded playlist's Local list: "<pl_pre><name>.m3u" */
	const char *pl_tsv;   /* what is known of the playlists, beside them */
	coverq_fetch_fn cover_cb;   /* calls msrv_cover_fetch on this source */

	/* The config file (<dir>/config, 0600): the core reads and writes its own
	 * keys and hands the rest over; cfg_write writes the backend's first. */
	void (*cfg_reset)(void);
	void (*cfg_key)(const char *k, const char *v);
	void (*cfg_loaded)(msrv *m);
	void (*cfg_write)(FILE *f);

	bool (*signed_in)(void);
	bool (*ready)(void);                       /* signed in, library chosen */
	bool (*rel_id)(char *out, size_t n);       /* the library's folder name; false without one */
	const char *(*library)(void);

	/* The server's address: found (may block), forgotten, and the one known
	 * now (under the core lock, never blocks; "" when none). */
	bool (*pick_base)(msrv *m, char *out, size_t n);
	void (*base_failed)(msrv *m);
	void (*base_now)(char *out, size_t n);
	int  (*kbps_stream)(void);                 /* 0: the file as it is */
	int  (*kbps_download)(void);

	/* The sync: the library's change stamp (-1 failed) - given the index's
	 * own (old: its stamp, -1 without one; since: its newest change; total:
	 * the server's track count then), for a server that keeps none - every
	 * track (the count, -1 failed), and what changed since `since` with the
	 * server's track count (false: read it all). */
	long (*stamp)(msrv *m, long old, long since, long total);
	long (*sync_full)(msrv *m, msrv_set *out);
	bool (*changed)(msrv *m, long since, const msrv_table *t, msrv_set *ch, long *total);

	void (*stream_url)(msrv *m, const char *base, const msrv_track *t, char *out, size_t n,
	                   char *codec, size_t cn);
	/* The track into dest: the HTTP status, 0 for no answer. */
	int  (*fetch_track)(msrv *m, const char *base, const msrv_track *t, int kbps, const char *dest,
	                    const volatile int *cancel);
	/* An album's art into `dest` as it is (the core renames it): the status. */
	int  (*cover_get)(msrv *m, const char *thumb, const char *dest, const volatile int *cancel);
	/* The daemon's path is this track's stream: by its key alone (part NULL),
	 * or with the track's part. */
	bool (*owns_url)(const char *daemon_path, int key, const char *part);

	void (*timeline)(msrv *m, int key, const char *state, long at_ms, long len_ms);
	int  (*scrobble)(msrv *m, int key);       /* a play made offline: the status */

	int  (*pl_list)(msrv *m, msrv_spl *out, int max);   /* -1: no answer */
	int  (*pl_items)(msrv *m, const char *id, int **keys);
	bool (*pl_create)(msrv *m, const char *name, const int *keys, int n, char *id, size_t idn);
	/* An edit sent; 200 when done or nothing to do. akey: the track it now
	 * comes after (0: first). */
	int  (*pl_edit)(msrv *m, int op, const char *id, const char *b, int key, int akey);

	/* Favorites (plorpos-vgr.6): the server's favorite tracks and albums by
	 * key (-1: no answer), and one set or cleared (the status). NULL: none. */
	int  (*fav_list)(msrv *m, int **tracks, int *nt, int **albums, int *na);
	int  (*fav_set)(msrv *m, int key, bool album, bool on);

	void (*job)(msrv *m, msrv_job *j);         /* kinds >= MSRV_J_BACKEND */
} msrv_ops;

msrv *msrv_new(const msrv_ops *ops, const char *card, const char *shared);

/* ---- for the backend ---- */

void msrv_lock(msrv *m);
void msrv_unlock(msrv *m);
unsigned msrv_now_ms(void);
void msrv_mkdirs(const char *path);
char *msrv_slurp(const char *path, size_t *len);
void msrv_urlenc(const char *s, char *out, size_t n);
void msrv_tmpfile(msrv *m, const char *tag, char *out, size_t n);
char *msrv_dupc(const char *s);
void msrv_clean(char *s);
bool msrv_offline(msrv *m);
void msrv_cfg_save(msrv *m);
void msrv_rel_root_set(msrv *m);              /* under the core lock */
void msrv_sync_progress(msrv *m, int done, int total);   /* -1: leave as it is */
bool msrv_cover_fetch(msrv *m, const char *thumb, const char *base);
void msrv_fail_note(msrv *m);

/* ---- the source, as plex.h has it ---- */

int  msrv_stream_kbps(msrv *m);
int  msrv_download_kbps(msrv *m);
void msrv_set_stream_kbps(msrv *m, int kbps);
void msrv_set_download_kbps(msrv *m, int kbps);

bool msrv_lib_cached(msrv *m);
bool msrv_lib_load(msrv *m, ml_lib *out);
int  msrv_album_year(msrv *m, int al);
void msrv_sync_start(msrv *m, bool force);
int  msrv_sync_state(msrv *m, int *done, int *total);
int  msrv_sync_outcome(msrv *m);
bool msrv_sync_take(msrv *m);

bool msrv_is_path(msrv *m, const char *rel);
const char *msrv_rel_root(msrv *m);
int  msrv_track_index(msrv *m, const char *rel);
bool msrv_title(msrv *m, const char *rel, char *out, size_t n);
void msrv_pl_dir(msrv *m, char *out, size_t n);

bool msrv_cover_want(msrv *m, int al, const char *base);
void msrv_cover_bump(msrv *m, const char *base);
bool msrv_covers_idle(msrv *m);
bool msrv_cover_take(msrv *m, char *base, size_t n);

bool msrv_resolve(msrv *m, const char *rel, musec_remote *out);
bool msrv_owns(msrv *m, const char *daemon_path, const char *rel);
bool msrv_stream_failed(msrv *m, const char *rel);
void msrv_prefetch(msrv *m, const char *playing, const char *const *next, int n);
int  msrv_ahead(msrv *m);
void msrv_set_ahead(msrv *m, int n);
void msrv_tick(msrv *m, const char *rel, int state, double at, double len);

int  msrv_album_downloaded(msrv *m, int al);
void msrv_download_album(msrv *m, int al);
void msrv_delete_album(msrv *m, int al);
bool msrv_album_downloading(msrv *m, int al);
typedef struct { int al, tracks; bool now; char title[96], artist[64]; } msrv_queued;
int  msrv_download_queue(msrv *m, msrv_queued *out, int max);
void msrv_download_cancel_album(msrv *m, int al);
void msrv_download_cancel(msrv *m);
bool msrv_download_status(msrv *m, char *out, size_t n);
bool msrv_download_take_changed(msrv *m);
bool msrv_download_take_failed(msrv *m);
void msrv_albums_local(msrv *m, unsigned char *out, int n);
bool msrv_track_local(msrv *m, int t);

void msrv_playlists_sync(msrv *m);
bool msrv_pl_smart(msrv *m, const char *name);
bool msrv_pl_downloaded(msrv *m, const char *name);
void msrv_pl_download(msrv *m, const char *name, bool on);
void msrv_pl_added(msrv *m, const char *name, const char *track);
void msrv_pl_removed(msrv *m, const char *name, const char *track);
void msrv_pl_moved(msrv *m, const char *name, const char *track, const char *after);
void msrv_pl_renamed(msrv *m, const char *from, const char *to);
void msrv_pl_deleted(msrv *m, const char *name);

/* Favorites both ways (plorpos-vgr.6): Favorite Tracks (through the
 * msrv_pl_added/removed hooks) and the albums hearted are the server's
 * favorites; a change made with no server goes when it answers again. */
bool msrv_favs_on(msrv *m);                              /* the backend has favorites */
bool msrv_fav_album_is(msrv *m, const char *dir);       /* dir: an album folder of this source */
bool msrv_fav_album_set(msrv *m, const char *dir, bool on);    /* false: not an album of its library */
void msrv_favs_retry(msrv *m);                          /* each frame: a change waiting, sent */
bool msrv_fav_apply(msrv *m);                           /* UI thread: Favorite Tracks in step; true if rewritten */
int  msrv_pl_gen(msrv *m);
void msrv_set_pl_mirror(msrv *m, bool on);

void msrv_set_offline(msrv *m, bool on);
bool msrv_get_offline(msrv *m);
bool msrv_signed_in(msrv *m);
bool msrv_ready(msrv *m);          /* signed in, library chosen */
const char *msrv_library(msrv *m); /* the library's name, "" before one is chosen */
const char *msrv_name(msrv *m);   /* "Plex", "Jellyfin": its downloads' folder, and what the user is told */
void msrv_plays_retry(msrv *m);

#endif
