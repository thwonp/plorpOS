/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_PLEX_H
#define TORTOS_PLEX_H

#include <stdbool.h>
#include <stddef.h>

#include "muselib.h"
#include "musec.h"

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

/* ---- account, server, library ----------------------------------------------- */

bool plex_signed_in(void);
/* Signed in with a server and a library chosen: the source can be shown. */
bool plex_ready(void);
const char *plex_server_name(void);
const char *plex_library_name(void);
const char *plex_account_name(void);
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
void plex_libraries_ask(void);
int  plex_libraries_poll(void);
const char *plex_library_at(int i);
void plex_library_pick(int i);

/* Quality, as the pak has it: 0 = Original, else an Opus transcode at that
 * many kbps (Plex Pass). Streaming and downloads each have one. */
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
/* Sync in the background: `force` always, else only when Plex says the
 * library changed since the index was written. */
void plex_sync_start(bool force);
/* 1 running (done/total so far), 0 idle, -1 the last one failed. */
int  plex_sync_state(int *done, int *total);
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
/* The playlists of the Plex source; Favorite Tracks is one of them. */
void plex_pl_dir(char *out, size_t n);
#define PLEX_FAVS "Favorite Tracks"

/* ---- covers ------------------------------------------------------------------- */

/* Fetch album `al`'s art to `<base>.jpg`. False when it has none. */
bool plex_cover_want(int al, const char *base);
/* A cover that landed: its base. False when none has. */
bool plex_cover_take(char *base, size_t n);

/* ---- playing ------------------------------------------------------------------- */

bool plex_resolve(const char *rel, musec_remote *out);
bool plex_owns(const char *daemon_path, const char *rel);
/* The track after the one playing: fetched ahead, on a device that has to
 * fetch a track before it can play it. Cheap to call every frame. */
void plex_prefetch(const char *rel);
/* Every frame Muse is polled: what plays, for Plex's timeline. */
void plex_tick(const char *rel, int state, double at, double len);

/* ---- downloads ---------------------------------------------------------------- */

/* How many of album `al`'s tracks have a downloaded copy (of its n). */
int  plex_album_downloaded(int al);
void plex_download_album(int al);
void plex_delete_album(int al);
/* Download every Favorite Track; delete the ones that left the list. */
void plex_sync_favorites(void);
void plex_download_cancel(void);
/* "3 of 12 - Title" while downloads run; false when none do. */
bool plex_download_status(char *out, size_t n);
/* True once after a download finished or files were deleted: the Local
 * source's library is out of date. */
bool plex_download_take_changed(void);

#endif
