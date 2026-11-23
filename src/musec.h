/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_MUSEC_H
#define TORTOS_MUSEC_H

#include <stdbool.h>
#include <stddef.h>

#include "musequeue.h"

/* The launcher's side of Muse: the connection to the daemon, and the play
 * queue.
 *
 * The daemon (src/muse/) knows one file at a time. What comes next is policy,
 * and policy is the launcher's - so this holds the queue, and when the daemon
 * says END it sends the next PLAY itself.
 *
 * Polled, never waited on: musec_poll reads whatever has arrived and returns.
 * It is called from the shelf's main loop, from Muse's own screen, and from
 * the 10 Hz tick the launcher gets while a game runs - which is what lets an
 * album carry on through a game, the way EROS's Muse did. On the speaker and
 * the jack the mixing needs nothing from anybody: both processes open
 * `default`, which is dmix. A Bluetooth headset is different - see musec_sink. */

typedef enum { MU_OFF, MU_STOPPED, MU_PLAYING, MU_PAUSED } mu_state;

typedef struct {
	mu_state state;
	char     title[128], artist[128], album[128];
	char     codec[32];             /* "FLAC 16/44.1", "OPUS 128"; "" unknown */
	double   at, len;
	double   speed;                 /* 1.0 but on a book set otherwise */
	/* Where in the queue, and how long it is. The PLAY order's place, so
	 * shuffled it counts the order being heard rather than album order. */
	int      index, count;
} mu_now;

/* Where the daemon lives, and the folder paths are relative to: the card,
 * since tracks come from Music and books from Audiobooks. Starting it is lazy:
 * the first call that wants it spawns it if it is not answering. */
void musec_init(const char *muse_bin, const char *root);

void musec_poll(void);

/* Queue `n` tracks - paths relative to the root - and play from `start`,
 * `at` seconds into it. The launcher's copies; the caller's array may go away.
 *
 * A `book` queue plays in order whatever the play mode is: a book shuffled or
 * on repeat is not a way anyone listens to one. The mode is left as it was
 * set, for the music after it. */
void musec_play(const char *const *paths, int n, int start, double at, bool book,
                double speed, const char *artist, const char *album);

/* Whether the queue is a book. */
/* musec_play's queue without playing it: stopped on `start` until pause/play
 * (plorpos-ggv.25, a queue kept over a restart). */
void musec_load(const char *const *paths, int n, int start, double at, bool book,
                double speed, const char *artist, const char *album);
/* What Muse was found playing outside the restored queue (a book, or music
 * restarted inside the 5 s before a save), relative to the root; "" if
 * nothing. Cleared by any new queue. */
const char *musec_orphan(void);

/* A queue entry the daemon cannot open by its path (plorpos-1lw): a Plex
 * track, kept in the queue - and in the queue file - as a path in the Plex
 * cache with no file behind it. `resolve` turns one into what the daemon is
 * sent (a stream URL, or a downloaded copy) plus what the file's tags may not
 * say; false for a path that is not one of its own. `owns` says whether the
 * daemon's path is the one a queue entry resolved to, for taking up a Muse
 * that outlived the launcher. The URL carries an account token: it goes to
 * the daemon and nowhere else - never into a log. */
typedef struct {
	char url[2048];
	char title[128], artist[128];
	/* What the stream is, when the daemon cannot tell (a transcode has no
	 * bitrate it can read): "OPUS 128". "" to leave it to the daemon. */
	char codec[32];
	double len;
	/* Not playable yet - a device whose FFmpeg cannot stream, fetching the
	 * track first: the PLAY is asked for again on every poll until it is. */
	bool wait;
	/* title and artist are the source's own and the file's tags do not
	 * replace them, nor give an album: a podcast's MP3 can carry the
	 * network's legal notice as its artist (plorpos-7ni.19). */
	bool names;
} musec_remote;
/* `failed`: the daemon could not open a remote track; true to have it tried
 * once more (the resolver will look for its server again) - else skipped. */
void musec_set_remote(bool (*resolve)(const char *rel, musec_remote *out),
                      bool (*owns)(const char *daemon_path, const char *rel),
                      bool (*failed)(const char *rel));
/* `online`: whether the device has a network at all. With it, a remote track
 * asked for off the network is not tried. */
void musec_set_online(bool (*online)(void));
/* Auto Rewind: on, a book paused 10 s or more resumes 3 to 30 s back. The
 * steps, for a book opened again from its kept place. */
void   musec_set_auto_rewind(bool on);
double musec_rewind_for(double paused_s);
/* Once: 1 a remote track was not played for want of a network, 2 its server
 * could not be reached; 0 neither. */
int musec_take_net_fail(void);
/* The queue's track resolved again for what it shows: a queue restored at
 * start comes before the library its resolver reads, and shows no title. */
void musec_retitle(void);
/* The queue for what Muse is already playing, paths[start], without a PLAY:
 * Muse plays on and the queue follows it (plorpos-3yv.71). n 0: only forgets
 * the orphan. */
void musec_adopt(const char *const *paths, int n, int start, bool book, double speed,
                 const char *artist, const char *album);
/* The queue in its own order, and the place in it of the track now. */
int musec_queue(const char *const **paths, int *current, const char **artist,
                const char **album);
/* The queue kept on the card (plorpos-ggv.25, plorpos-2ru). keep: every tick,
 * writes once it has stood still 5 s. save: now (powering off). restore: at
 * start, stopped on its track; mode_too where the play mode is not kept
 * anywhere else. A Muse still playing from before is taken up in it. */
void musec_queue_keep(const char *file);
void musec_queue_save(const char *file);
void musec_queue_restore(const char *file, bool mode_too);
bool musec_is_book(void);

/* A book's speed, 0.5 to 2.0 with the pitch kept - the daemon's SPEED. Set on
 * the playing queue at once and carried by every PLAY after it: the daemon
 * treats a PLAY with no speed as 1x, so a speed the launcher did not repeat
 * would be lost at the next file. */
void   musec_set_speed(double x);
double musec_speed(void);

/* The chapters of the file playing, as the daemon read them from it (an
 * M4B's; a folder of MP3s has none, its files are its parts). A book with more
 * than one moves by chapter on L1 and R1 - see musec_next. */
int         musec_chapters(void);
double      musec_chapter_at(int i);
const char *musec_chapter_title(int i);
/* The chapter the playing position is in, or -1 with none. */
int         musec_chapter_now(void);

/* True once each time a queue plays out to its end - the last track ended by
 * itself, not stopped or replaced. A book that does has been finished. */
bool musec_take_ran_out(void);
/* A PLAY or RESUME sent and not answered yet: a stream opening, which takes
 * seconds (plorpos-7ni.18). */
bool musec_asked(void);
/* That, for a remote track: the old one plays on until the new one opens, so
 * "playing" alone hides it (plorpos-ps8.2). */
bool musec_asked_stream(void);
/* The next play of the current track (A from stopped) starts at `at`, not
 * at 0: a stream cut short keeps its place (plorpos-ps8.13). */
void musec_start_at(double at);
/* The last track's end was a stream cut short (Wi-Fi or its server went), not
 * the file's end: the daemon's END said cut=1 (plorpos-7ni.16). */
bool musec_end_cut(void);

void musec_toggle(void);       /* pause or resume */
/* Pause only, never resume - and if Muse is not "playing" right now (it may be
 * mid-fallback after its output went), at the first "playing" within 2 s. */
void musec_pause(void);
/* The next and previous track - or, in a book whose file has chapters, the
 * next chapter and the start of this one (or the one before, within three
 * seconds of its start), falling back to the files at either end. */
void musec_next(void);
void musec_prev(void);
void musec_seek_by(double delta);
void musec_stop(void);
/* Stopped with nothing in the player, as before anything was played: the
 * queue and the names on show go, so a toggle has nothing to start again -
 * for a book deleted from under it (plorpos-5tm.15). */
void musec_forget(void);

const mu_now *musec_now(void);

/* Play heard position i of the queue (musec_track's i, mu_now.index's
 * count) from its start - the shuffle's order kept (plorpos-rgds.18.3.4). */
void musec_jump(int i);
/* A book's file i from `at` seconds into it - a seek when that file is the one
 * playing (More > Chapters and Timeline, plorpos-i8d.1). */
void musec_jump_at(int i, double at);
/* Called when the daemon says the state changed (playing, paused, stopped) -
 * from musec_poll, so wherever Muse is polled, the dark included (the book
 * Timeline, plorpos-i8d.5). */
void musec_on_state(void (*fn)(mu_state st));

/* The queued track, relative to the music root, or "" - so a list can mark
 * the row that is playing without comparing titles, which tags can change. */
const char *musec_path(void);

/* Playing right now, for Auto Off: music is somebody using the device with
 * nobody touching it, the same as the charger. */
bool musec_playing(void);

/* Playing, or asked to and not yet answered. What decides who holds a
 * Bluetooth headset, which has to be true from the moment a PLAY is sent:
 * musec_playing only turns true when the daemon says so, and in between the
 * route would hand the headset back. */
bool musec_heard(void);

/* The output. bluealsa here gives a headset to one process at a time, so it
 * is handed between Diatom and Muse rather than shared: whoever may be heard
 * holds it (ADR-0032 already quiets a game while music plays). `device` is an
 * ALSA name, or "" for the default. Sent only when it changes; the daemon
 * tries a headset for up to a second, while the other side lets go, and falls
 * back to the default if it never does. */
void musec_sink(const char *device);
/* Make the next musec_sink send even if the device has not changed: for a
 * headset that dropped and came back under the same name, which Muse may
 * have fallen back from while it was gone. The daemon ignores a SINK for the
 * device it is already on, so this costs nothing when it never left. */
void musec_sink_again(void);
/* Whether the last musec_sink has been answered, or given up on - so the
 * launcher can hand the headset to Diatom only once Muse has let go. */
bool musec_sink_settled(void);
/* What the daemon last said it is on, or "" before it has said, or while a
 * SINK is waiting for its answer. */
const char *musec_sink_now(void);
/* Called just before a PLAY or RESUME is sent, so the launcher can move the
 * headset to Muse first. */
void musec_on_before_heard(void (*fn)(void));
/* Asked before a paused queue resumes: true holds the RESUME (it shows as
 * playing) until musec_resume_release - with a file and seconds to play from
 * instead, or index -1 to resume where it is (plorpos-9rf.9). */
void musec_on_resume(bool (*hold)(const char *rel));
void musec_resume_release(int index, double at);
/* Called when Muse's real output becomes a DAC or a headset it was not on a
 * moment ago - including coming back from a fall-back to the speaker - before
 * the next play. The launcher caps the level there (plorpos-7wu.6). */
void musec_on_arrive(void (*fn)(const char *sink));

/* The queue's track `i` in PLAY order - shuffled, the i-th to be heard -
 * relative to the music root, or NULL past either end. */
const char *musec_track(int i);

/* What plays when this track finishes, or NULL when that is nothing or not yet
 * decided - see muq_upcoming. For "Next:", which has to follow the mode. */
const char *musec_upcoming(void);
/* The one k places on (musec_upcoming is k = 1), or NULL. */
const char *musec_upcoming_n(int k);

/* The play mode, src/musequeue.h. Applied to the queue playing now without
 * changing its track, and kept for every queue after it; saving it is the
 * caller's. */
void     musec_set_mode(muq_mode m);
muq_mode musec_mode(void);

/* Ask for the picture `track` (relative to the music root) carries, written to
 * `base` plus the extension its format has. The daemon answers in a few
 * milliseconds and the answer is collected with musec_cover_take.
 *
 * False when it could not be asked - the daemon is not up yet, and asking has
 * just started it - and the caller asks again later. An answer can also be
 * lost with the daemon, so a caller that asked and heard nothing should ask
 * again after a while rather than wait for good. */
bool musec_cover_ask(const char *track, const char *base);

/* One answer: the `base` asked about, and the file written there - "" when the
 * track carries no picture or it could not be written. False when none is
 * waiting. */
bool musec_cover_take(char *base, size_t bn, char *file, size_t fn);

/* The year `track` (relative to the music root) is tagged with, asked the same
 * way and lost the same way; one at a time - an answer not taken is replaced
 * by the next. Taken as the track asked about and the year: 0 none in the
 * tags, -1 the file would not open (plorpos-xav). */
bool musec_year_ask(const char *track);
bool musec_year_take(char *track, size_t n, int *year);

/* Ask who the tracks in `dir` (relative to the music root) are by, from their
 * tags: the album artist, or the artist every track agrees on, or "" (see
 * src/muse/tags.h). False when it could not be asked, as musec_cover_ask. The
 * answer is collected with musec_artist_take, `dir` as it was asked. */
bool musec_artist_ask(const char *dir);
bool musec_artist_take(char *dir, size_t dn, char *name, size_t nn);

#endif
