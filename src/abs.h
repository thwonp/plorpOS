/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_ABS_H
#define TORTOS_ABS_H

#include <stdbool.h>
#include <stddef.h>

#include "muselib.h"
#include "musec.h"

/* Audiobookshelf as one of Muse's sources (plorpos-9rf): the books of a
 * server on the LAN, streamed over plain http.
 *
 * NOT A SECOND PLAYER, as with Plex (plex.h): Muse's daemon plays a book's
 * file from its URL, and Muse's own screens browse the books. This file adds:
 *   - signing in as the Android app does: the server's address, checked
 *     against /status, then a username and a password. The tokens (a
 *     short-lived access token with its refresh token, and the account's
 *     long-lived token for stream URLs) live in <P_SHARED>/abs/config (0600);
 *   - the library, synced into an index on the card and read from there as an
 *     ml_lib of books: <P_SHARED>/abs/<library>/<item id>/<NNN>.<ext>, paths
 *     with no files behind them. Covers go where ml_cover_base looks;
 *   - turning one of those paths into what the daemon is sent: the file's
 *     stream URL, or the downloaded copy when there is one;
 *   - a book's place on the server, both ways: the newer of the server's and
 *     the device's wins when a book opens, and the place is sent back while it
 *     plays. "Newer" needs no clock: the server's lastUpdate is remembered each
 *     time this device sends or takes a place, and a different one means the
 *     place was moved somewhere else since;
 *   - Download Book, into <card>/Audiobooks/<author>/<title>/, where it is an
 *     ordinary book of the Local Audiobooks source, with a .abs file naming
 *     its item, so its place still goes to the server.
 * Network calls run on threads of their own; nothing below waits for them. */

void abs_init(const char *card, const char *shared);

/* ---- account ---------------------------------------------------------------------- */

bool abs_signed_in(void);
const char *abs_server(void);
const char *abs_user(void);
const char *abs_library_name(void);
void abs_sign_out(void);

/* Sign-in, in two steps, each started and then polled every frame:
 * 0 waiting, 1 done, -1 failed (`why` says what to show). */
void abs_check_start(const char *address);   /* is it an Audiobookshelf? */
void abs_login_start(const char *user, const char *password);
int  abs_auth_poll(char *why, size_t n);
/* The address the last check settled on ("http://host:port"). */
const char *abs_checked_address(void);

/* ---- the library ------------------------------------------------------------------- */

bool abs_lib_cached(void);
bool abs_lib_load(ml_lib *out);
int  abs_album_year(int al);
/* Sync in the background: `force` reads every book again; else only the
 * books the server says changed. */
void abs_sync_start(bool force);
/* 1 running (done/total), 0 idle, -1 the last one failed. */
int  abs_sync_state(int *done, int *total);
/* True once after a sync wrote a new index. */
bool abs_sync_take(void);

bool abs_is_path(const char *rel);
/* A file's title, for lists drawn from queue paths. */
bool abs_title(const char *rel, char *out, size_t n);
void abs_pl_dir(char *out, size_t n);
bool abs_cover_want(int al, const char *base);
void abs_cover_bump(const char *base);   /* see plex_cover_bump */
bool abs_covers_idle(void);              /* see plex_covers_idle */
bool abs_cover_take(char *base, size_t n);

/* ---- playing ------------------------------------------------------------------------ */

bool abs_resolve(const char *rel, musec_remote *out);
bool abs_owns(const char *daemon_path, const char *rel);
/* The source's path for what the daemon plays (a stream URL), for taking up
 * a book Muse played on through a launcher restart. False when not one. */
bool abs_rel_of(const char *daemon_path, char *out, size_t n);

/* The server's places, asked again (at most every few seconds): on the way
 * into Muse, so a place moved elsewhere is there before a book opens. */
void abs_progress_refresh(void);
/* Places the server could not take, sent again while online, every 30 s at
 * most; cheap when there are none - from muse_poll (plorpos-ps8.15). */
void abs_unsent_retry(void);
/* Book `track` (any of its files, card-relative: the source's path or a
 * downloaded copy): when the server's place is newer than the one this
 * device knows, 1 with the file's index and the seconds into it, 2 when the
 * server says it is finished; 0 to keep the device's own. */
int  abs_book_place(const char *track, int *index, double *at);
/* One place per server book, whichever copy plays (plorpos-9rf.10): the item
 * `track` belongs to, and seconds into the whole book <-> file + seconds. */
bool abs_item(const char *track, char *id, size_t n);
bool abs_book_secs(const char *id, int index, double at, double *secs);
bool abs_book_file(const char *id, double secs, int *index, double *at);
/* What the server last said of item `id` (/api/me, pulled when Muse opens):
 * false when it has nothing; else its lastUpdate in ms, whether it is
 * finished, and the seconds in (plorpos-i8d.4's lists). */
bool abs_server_place(const char *id, long long *last_ms, bool *fin, double *cur);
/* What plays, each poll: file `index` of the book `track` is in, `at` into it.
 * state 1 playing, 2 paused, 0 stopped. Sent every 15 s and on every change. */
void abs_tick(const char *track, int index, double at, int state);
/* Play on a paused book (plorpos-9rf.9): its place asked of the server, and
 * the resume held meanwhile. True when asked; then abs_resume_poll each poll:
 * -1 nothing asked, 0 waiting, 1 done - `index` -1 to resume where it is,
 * else the file and seconds the server's newer place is at. 1.5 s at most. */
bool abs_resume_hold(const char *track);
int  abs_resume_poll(int *index, double *at);
/* The book ran out: finished on the server too. */
void abs_book_finished(const char *track);
/* Marked unread: its place and its finish taken off the server too. */
void abs_book_unread(const char *track);
/* Goes up when the server's places were read again (abs_server_place). */
unsigned abs_progress_gen(void);

/* ---- downloads ---------------------------------------------------------------------- */

/* Album `al` of the library abs_lib_load made: 1 on the card, 0 not. */
int  abs_book_downloaded(int al);
bool abs_book_downloading(int al);
void abs_download_book(int al);
void abs_download_cancel(void);
/* As pod_download_queue (plorpos-ps8.8), and one book out of it. */
int  abs_download_queue(int *al, bool *now, int max);
void abs_download_cancel_book(int al);
void abs_delete_book(int al);
/* A line for the screen while downloads run ("Downloading 3 of 12"). */
bool abs_download_status(char *out, size_t n);
/* True once after a download finished or a copy went: Audiobooks changed. */
bool abs_download_take_changed(void);

/* True once after a download failed (not cancelled). */
bool abs_download_take_failed(void);

#endif
