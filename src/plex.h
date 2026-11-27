/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_PLEX_H
#define TORTOS_PLEX_H

#include <stdbool.h>
#include <stddef.h>

#include "muselib.h"
#include "musec.h"
#include "msrv.h"

/* Plex as Muse's second source (plorpos-1lw), ported from the owner's
 * PlexMusicPak for NextUI.
 *
 * NOT A SECOND PLAYER. Muse's daemon plays a Plex track the way it plays a
 * file - FFmpeg opens the stream URL - and Muse's own screens browse it, so
 * Now Playing, the queue, the second screen, Bluetooth and sleep are the ones
 * Muse already has. What this file adds is everything Plex-shaped:
 *
 *   - signing in (a PIN shown on the screen, entered at plex.tv/link), the
 *     server and the music library;
 *   - the library, synced once into an index on the card and read from there,
 *     as an ml_lib whose tracks are paths in a cache folder with no files
 *     behind them: <P_SHARED>/plex/<server>-<section>/<artist>/<album>/<track>.<ext>.
 *     Real folders hold the album art (<artist>.media/<album>.jpg, where
 *     ml_cover_base looks) and the source's playlists;
 *   - turning one of those paths into what the daemon is sent (musec's
 *     remote hook): the stream - the original file, or Plex's Opus transcode
 *     at the quality chosen - or the downloaded copy when there is one;
 *   - telling Plex what plays (a timeline every 5 s; Plex counts the play at 90%);
 *   - downloads: an album, or the Favorite Tracks playlist, into
 *     <card>/Music/Plex/<artist>/<album>/, where they are ordinary music for
 *     the Local source too.
 *
 * The network runs on three threads of its own - requests, the library sync
 * and downloads - and nothing here waits for it on the caller's thread: every
 * call below returns at once. The account token lives in <P_SHARED>/plex/config
 * (0600) and in the URLs the daemon is handed, and is written nowhere else. */

void plex_init(const char *card, const char *shared);
msrv *plex_src(void);   /* the shared core's instance: Muse asks it through msrv_* */

/* ---- account, server, library ----------------------------------------------- */

bool plex_signed_in(void);
/* Signed in with a server and a library chosen: the source can be shown. */
bool plex_ready(void);
const char *plex_server_name(void);
const char *plex_library_name(void);
const char *plex_account_name(void);
/* Whether this account may download from the server, as it last said
 * (plorpos-5eq.9): 1 yes, 0 no, -1 not known (not asked yet, or it did not say). */
int plex_allow_sync(void);
void plex_sign_out(void);

/* Sign-in: start, then poll each frame. 0 waiting (`code` holds the PIN once
 * plex.tv has given one, else ""), 1 linked, -1 failed or expired. */
void plex_pin_start(void);
int  plex_pin_poll(char *code, size_t n);
void plex_pin_cancel(void);

/* The account's servers, and a server's music libraries: ask, then poll.
 * 0 waiting, n > 0 that many to choose from, -1 failed (or none). */
void plex_servers_ask(void);
int  plex_servers_poll(void);
const char *plex_server_at(int i, bool *owned);
void plex_server_pick(int i);
/* Tailnet Server (plorpos-vgr.7): the chosen server's address on the tailnet,
 * used first while Tailscale is connected. Set ("" clears), then poll: 0
 * waiting, 1 saved, -1 refused - plex_tailnet_why() says why, or what to
 * know of a saved one. */
const char *plex_tailnet(void);
int  plex_home_port(void);
void plex_tailnet_set(const char *address);
int  plex_tailnet_poll(void);
const char *plex_tailnet_why(void);
void plex_libraries_ask(void);
int  plex_libraries_poll(void);
const char *plex_library_at(int i);
void plex_library_pick(int i);

/* Quality, as the pak has it: 0 = Original, else an Opus transcode at that
 * many kbps. Streaming and downloads each have one. */
#define PLEX_QUALITIES 6
int  plex_quality_at(int i);
const char *plex_quality_label(int kbps);
int  plex_stream_kbps(void);
int  plex_download_kbps(void);
void plex_set_stream_kbps(int kbps);
void plex_set_download_kbps(int kbps);

/* ---- the library -------------------------------------------------------------- */

/* An index for the chosen library is on the card. */
bool plex_lib_cached(void);
/* The index as Muse's library, on any thread (muse_walk's). False without
 * one. The track table the resolver reads is replaced with it. */
bool plex_lib_load(ml_lib *out);
/* An album's year, by its index in the ml_lib plex_lib_load made last. */
int  plex_album_year(int al);
/* Sync in the background: `force` reads the whole library again; else only
 * when Plex says it changed since the index was written, and then only what
 * changed (the whole of it when tracks were deleted, or for an older index). */
void plex_sync_start(bool force);
/* 1 running (done/total so far), 0 idle, -1 the last one failed. */
int  plex_sync_state(int *done, int *total);
/* How the last sync ended: 1 nothing had changed, 2 the index was updated,
 * -1 it failed, 0 none yet (or one running). */
int  plex_sync_outcome(void);
/* True once after a sync wrote a new index: time to load it. */
bool plex_sync_take(void);

/* `rel` (card-relative) is a track of the Plex cache. */
bool plex_is_path(const char *rel);
/* The cache folder's card-relative root, for the Local source's tests. */
const char *plex_rel_root(void);
/* Its index in the ml_lib plex_lib_load made last, or -1. */
int  plex_track_index(const char *rel);
/* A Plex track's title, for lists drawn from queue paths. */
bool plex_title(const char *rel, char *out, size_t n);
/* The playlists of the Plex source; Favorite Plex Tracks is one of them. */
void plex_pl_dir(char *out, size_t n);

/* ---- covers ------------------------------------------------------------------- */

/* Fetch album `al`'s art to `<base>.jpg`. False when it has none. */
bool plex_cover_want(int al, const char *base);
/* Still drawn without it: fetched sooner if it is waiting (plorpos-ps8.9). */
void plex_cover_bump(const char *base);
/* No cover waiting to be fetched (plorpos-ps8.12). */
bool plex_covers_idle(void);
/* A cover that landed: its base. False when none has. */
bool plex_cover_take(char *base, size_t n);

/* ---- playing ------------------------------------------------------------------- */

bool plex_resolve(const char *rel, musec_remote *out);
bool plex_owns(const char *daemon_path, const char *rel);
/* musec's word that the daemon could not open a track (ERROR): true when it
 * is Plex's, and the connection is found again before it is tried again. */
bool plex_stream_failed(const char *rel);
/* The track playing and the next `n` (the queue's order): fetched ahead, in
 * order and within the cache's budget, on a device that has to fetch a track
 * before it can play it. Cheap to call every frame. */
void plex_prefetch(const char *playing, const char *const *next, int n);
/* How many tracks plex_prefetch is handed (1, 3, 5 or 10; plorpos-1lw.22),
 * on every port: a streaming one caches them too (plorpos-vgr.12). */
int  plex_ahead(void);
void plex_set_ahead(int n);
/* Every frame Muse is polled: what plays, for Plex's timeline. */
void plex_tick(const char *rel, int state, double at, double len);

/* ---- downloads ---------------------------------------------------------------- */

/* How many of album `al`'s tracks have a downloaded copy (of its n). */
int  plex_album_downloaded(int al);
void plex_download_album(int al);
void plex_delete_album(int al);
/* Album al has tracks waiting or in flight; Cancel Download stops them and
 * removes the copies the download made (a favourite's stays). */
bool plex_album_downloading(int al);
/* The albums waiting in the download queue, in queue order, at most `max`:
 * the album (index as plex_album_downloading), how many of its tracks are
 * still queued, and whether one of them is downloading now. */
typedef struct { int al, tracks; bool now; char title[96], artist[64]; } plex_queued;
int  plex_download_queue(plex_queued *out, int max);
void plex_download_cancel_album(int al);
/* Playlists (plorpos-5eq.7). The Plex source's are files in plex_pl_dir, kept
 * the same as the server's: plex_playlists_sync pulls them (and runs after
 * every library sync), and an edit made here is told to the server with the
 * calls below, after the file has changed - a track by its library path, a
 * move by the track it now comes after (NULL: first). Favorite Tracks is
 * this device's own and is never sent. */
void plex_playlists_sync(void);
bool plex_pl_smart(const char *name);       /* the server's own rule: read only */
void plex_pl_added(const char *name, const char *track);
void plex_pl_removed(const char *name, const char *track);
void plex_pl_moved(const char *name, const char *track, const char *after);
void plex_pl_renamed(const char *from, const char *to);
void plex_pl_deleted(const char *name);
/* A downloaded playlist: its tracks copied to Music/Plex, and listed for the
 * Local source as "Plex <name>" in <card>/Music/Playlists. With the mirror
 * on (the default) every pull brings the copies in line. */
bool plex_pl_downloaded(const char *name);
void plex_pl_download(const char *name, bool on);
void plex_set_pl_mirror(bool on);
/* Offline Mode (plorpos-5tm.16): no request leaves; downloads under way stop. */
void plex_set_offline(bool on);
/* Plays counted offline (plays.tsv), sent as scrobbles once the server
 * answers; from the poll, every 30 s at most. */
void plex_plays_retry(void);
/* For Offline Mode's lists: n flags, an album with any track on the card
 * (from the downloads manifest), and one track's copy. */
void plex_albums_local(unsigned char *out, int n);
bool plex_track_local(int t);
bool plex_offline(void);
/* Goes up each time the playlists change under the shelf: a cached list of
 * them is stale. */
int  plex_pl_gen(void);
void plex_download_cancel(void);
/* "3 of 12 - Title" while downloads run; false when none do. */
bool plex_download_status(char *out, size_t n);
/* True once after a download finished or files were deleted: the Local
 * source's library is out of date. */
bool plex_download_take_changed(void);

/* True once after a download failed (not cancelled). */
bool plex_download_take_failed(void);

#endif
