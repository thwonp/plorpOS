/* SPDX-License-Identifier: MIT */
/* See pcm.h. */
#include "pcm.h"

#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef MUSE_PCM_DIATOM
/* libasound's own values, which are ABI and have not moved in two decades. */
#define SND_PCM_STREAM_PLAYBACK       0
#define SND_PCM_FORMAT_S16_LE         2
#define SND_PCM_ACCESS_RW_INTERLEAVED 3

typedef struct snd_pcm snd_pcm_t;

static int         (*a_open)(snd_pcm_t **, const char *, int, int);
static int         (*a_set_params)(snd_pcm_t *, int, int, unsigned, unsigned,
                                   int, unsigned);
static long        (*a_writei)(snd_pcm_t *, const void *, unsigned long);
static int         (*a_recover)(snd_pcm_t *, int, int);
static int         (*a_drop)(snd_pcm_t *);
static int         (*a_prepare)(snd_pcm_t *);
static int         (*a_delay)(snd_pcm_t *, long *);
static int         (*a_close)(snd_pcm_t *);
static const char *(*a_strerror)(int);

static snd_pcm_t *g_pcm;
#else
static int        g_sock = -1;   /* the resident diatom's music socket */
#endif
static char       g_err[128];
static int        g_last;        /* the last open's error, 0 when it worked */

/* The last things done to the output, kept for when it goes wrong - #42.
 * Twice on 2026-09-25 Muse stalled inside bluealsa's plugin, and the second
 * time the Muse before it vanished with no line at all, so there was nothing
 * to say which call it had been making. Each entry is formatted when it is
 * made, wall clock included to line up with the syslog, so that dumping them
 * is only write() and is safe from a signal handler. */
#define TRAIL 32
static char     g_trail[TRAIL][128];
static unsigned g_trail_n;
static volatile long g_write_since;   /* when the write in progress began, or 0 */
static long     g_written;           /* frames written since the last open or prepare */

void pcm_note(const char *fmt, ...)
{
	char *slot = g_trail[g_trail_n % TRAIL];
	struct timespec t;
	struct tm tm;
	va_list ap;
	int n;

	clock_gettime(CLOCK_REALTIME, &t);
	localtime_r(&t.tv_sec, &tm);
	n = snprintf(slot, sizeof g_trail[0], "muse:   %02d:%02d:%02d.%03ld ",
	             tm.tm_hour, tm.tm_min, tm.tm_sec, t.tv_nsec / 1000000);
	va_start(ap, fmt);
	vsnprintf(slot + n, sizeof g_trail[0] - n - 1, fmt, ap);
	va_end(ap);
	n = (int)strlen(slot);
	/* Into the syslog as well, where it lands between bluetoothd's transport
	 * lines in the order things happened. Not by time: the syslog's own stamps
	 * come in batches - two lines three seconds apart read the same to the
	 * millisecond, measured 2026-09-25 - hence the wall clock in the text. */
	syslog(LOG_INFO, "%s", slot + 8);
	slot[n] = '\n';
	slot[n + 1] = '\0';
	g_trail_n++;
}

void pcm_trail_dump(int fd)
{
	unsigned i = g_trail_n > TRAIL ? g_trail_n - TRAIL : 0;

	for (; i < g_trail_n; i++) {
		const char *s = g_trail[i % TRAIL];
		if (write(fd, s, strlen(s)) < 0) return;
	}
}

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

long pcm_in_write_ms(void)
{
	long since = g_write_since;
	return since ? now_ms() - since : 0;
}

#ifndef MUSE_PCM_DIATOM
static bool alsa_bind(void)
{
	static void *h;

	if (h) return true;
	h = dlopen("libasound.so.2", RTLD_NOW);
	if (!h) { snprintf(g_err, sizeof g_err, "no libasound: %s", dlerror()); return false; }
	a_open       = dlsym(h, "snd_pcm_open");
	a_set_params = dlsym(h, "snd_pcm_set_params");
	a_writei     = dlsym(h, "snd_pcm_writei");
	a_recover    = dlsym(h, "snd_pcm_recover");
	a_drop       = dlsym(h, "snd_pcm_drop");
	a_prepare    = dlsym(h, "snd_pcm_prepare");
	a_delay      = dlsym(h, "snd_pcm_delay");
	a_close      = dlsym(h, "snd_pcm_close");
	a_strerror   = dlsym(h, "snd_strerror");
	if (!a_open || !a_set_params || !a_writei || !a_recover || !a_drop ||
	    !a_prepare || !a_delay || !a_close || !a_strerror) {
		snprintf(g_err, sizeof g_err, "libasound is missing a function");
		dlclose(h);
		h = NULL;
		return false;
	}
	return true;
}

/* How much audio ALSA holds ahead of the ear. 200 ms - see pcm_open - except
 * a USB DAC (the launcher names one plughw:CARD=<id>), which gets 500 ms. On
 * the codec Muse writes into dmix, whose own ring holds another ~170 ms, so a
 * stall in the writer is covered twice; straight into a DAC only this buffer
 * covers it, and on the Brick 200 ms ran dry several times an album
 * (plorpos-8wc.9). Pause, seek and skip drop the buffer, so its size is not
 * felt there, and the heard position already subtracts what is queued. */
static unsigned pcm_latency_us(const char *device)
{
	return strncmp(device, "plughw:CARD=", 12) ? 200000 : 500000;
}

bool pcm_open(const char *device)
{
	int r;

	if (!alsa_bind()) return false;
	pcm_close();
	r = a_open(&g_pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
	g_last = r < 0 ? r : 0;
	if (r < 0) {
		snprintf(g_err, sizeof g_err, "open %s: %s", device, a_strerror(r));
		pcm_note("%s", g_err);
		g_pcm = NULL;
		return false;
	}
	/* soft_resample 0: the decoder already delivers 48 kHz, and asking ALSA
	 * to resample again would be the awrate path this exists to avoid.
	 * 200 ms of latency: long enough that a busy frame in a running game does
	 * not starve it, short enough that pause and seek feel immediate. */
	r = a_set_params(g_pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
	                 2, 48000, 0, pcm_latency_us(device));
	/* Except a headset that is not running at 48 kHz, where ALSA has to
	 * resample or nothing plays. A headset picks the rate when IT starts the
	 * connection: the OpenFit came back at 44.1 kHz on reconnecting by itself
	 * and every open failed on it, measured 2026-09-25. The wired path's
	 * reason against awrate does not carry over - the SBC encode that follows
	 * costs more than the resample does.
	 *
	 * Through `plug:`, because a bare `type bluealsa` PCM has no rate
	 * converter to allow: soft_resample 1 on the PCM itself failed the same
	 * way, measured the same day. Only on a rate mismatch, which is -EINVAL
	 * here; busy stays busy, for the caller to retry. */
	if (r == -EINVAL && strcmp(device, "default")) {
		char plug[160];

		pcm_close();
		snprintf(plug, sizeof plug, "plug:%s", device);
		r = a_open(&g_pcm, plug, SND_PCM_STREAM_PLAYBACK, 0);
		if (r >= 0)
			r = a_set_params(g_pcm, SND_PCM_FORMAT_S16_LE,
			                 SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
			                 pcm_latency_us(device));
		else
			g_pcm = NULL;
		if (r >= 0)
			fprintf(stderr, "muse: %s is not at 48 kHz; ALSA resamples it\n", device);
	}
	if (r < 0) {
		snprintf(g_err, sizeof g_err, "set params on %s: %s", device, a_strerror(r));
		g_last = r;
		pcm_note("%s", g_err);
		pcm_close();
		return false;
	}
	pcm_note("open %s: ok", device);
	g_written = 0;
	return true;
}

void pcm_close(void)
{
	if (g_pcm) {
		pcm_note("close");
		a_close(g_pcm);
	}
	g_pcm = NULL;
}

bool pcm_write(const int16_t *frames, int n)
{
	while (g_pcm && n > 0) {
		long t0 = now_ms(), took;
		long w;

		g_write_since = t0;
		w = a_writei(g_pcm, frames, (unsigned long)n);
		g_write_since = 0;

		/* A write is ~21 ms of audio into a 200 ms buffer, so a quarter of a
		 * second inside one is the output not taking it - noted, because a
		 * stall starts as exactly that. */
		if ((took = now_ms() - t0) > 250)
			pcm_note("write of %d frames took %ld ms (%s)", n, took,
			          w < 0 ? a_strerror((int)w) : "ok");
		if (w < 0) {
			/* An underrun is ordinary - a pause, a slow card read - and
			 * recover re-prepares the stream. Anything else is not. */
			int r = a_recover(g_pcm, (int)w, 1);

			pcm_note("write: %s, recover %s", a_strerror((int)w), r < 0 ? a_strerror(r) : "ok");
			if (r < 0) {
				snprintf(g_err, sizeof g_err, "write: %s", a_strerror((int)w));
				return false;
			}
			continue;
		}
		frames += w * 2;
		n -= (int)w;
		g_written += w;
	}
	return g_pcm != NULL;
}

void pcm_drop(void)
{
	if (!g_pcm) return;
	/* Nothing to drop on a device that has not been written to since it was
	 * opened or last prepared - and on a bluealsa headset a drop there is
	 * what stalled Muse (#42). The drop makes bluealsa release the A2DP
	 * stream it acquired a millisecond earlier for the open, and the writes
	 * that follow wait for a stream it never takes again. All three stalls on
	 * 2026-09-25/26 were SINK then PLAY, the drop right behind the open; the
	 * third was caught in order in the syslog: Acquire, ACTIVE, the open,
	 * PLAY, drop, Release, IDLE, then a write that never returned. */
	if (!g_written) {
		pcm_note("drop skipped: nothing written since the open or prepare");
		return;
	}
	pcm_note("drop+prepare");
	a_drop(g_pcm);
	a_prepare(g_pcm);
	g_written = 0;
}

long pcm_queued(void)
{
	long d = 0;

	if (!g_pcm || a_delay(g_pcm, &d) < 0 || d < 0) return 0;
	return d;
}

const char *pcm_error(void) { return g_err; }

bool pcm_is_open(void) { return g_pcm != NULL; }

bool pcm_busy(void) { return g_last == -EBUSY; }
#else   /* MUSE_PCM_DIATOM */

/* The Flip's sound is MI_AO, which has one owner: the resident diatom, which
 * keeps it open for its whole life (plorpos-f1p.9). Muse hands its PCM to it
 * over a unix socket - 48 kHz S16 stereo, raw - and diatom plays it in place
 * of the game's audio, so a game is silent while music plays, as on every
 * other port. The socket's backpressure paces this process: a write returns
 * as diatom drains its ring, as a blocking ALSA write would.
 *
 * Disconnecting is how Muse drops what is queued: diatom empties its ring when
 * a client goes. diatom re-execs between games, which also ends the
 * connection; a write that finds it closed reconnects for a few seconds
 * rather than failing a track that was fine. */
#define MUSIC_SOCK   "/tmp/diatom-music.sock"
#define MUSIC_RING   4096        /* diatom's ring, frames: its fill is ~all of it */
#define RECONNECT_MS 3000

static bool dconnect(void)
{
	struct sockaddr_un a;
	int fd, sz = 16384;          /* ~85 ms in the socket, so "heard" stays close */

	g_last = 0;
	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
		g_last = -errno;
		snprintf(g_err, sizeof g_err, "socket: %s", strerror(errno));
		return false;
	}
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", MUSIC_SOCK);
	if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
		g_last = -errno;
		snprintf(g_err, sizeof g_err, "connect %s: %s", MUSIC_SOCK, strerror(errno));
		close(fd);
		return false;
	}
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
	g_sock = fd;
	return true;
}

bool pcm_open(const char *device)
{
	(void)device;                /* one sink: the speaker */
	pcm_close();
	if (!dconnect()) { pcm_note("%s", g_err); return false; }
	pcm_note("open diatom: ok");
	g_written = 0;
	return true;
}

void pcm_close(void)
{
	if (g_sock >= 0) {
		pcm_note("close");
		close(g_sock);
	}
	g_sock = -1;
}

bool pcm_write(const int16_t *frames, int n)
{
	const char *p = (const char *)frames;
	size_t left = (size_t)n * 4;

	while (g_sock >= 0 && left > 0) {
		long t0 = now_ms();
		ssize_t w;

		g_write_since = t0;
		w = send(g_sock, p, left, MSG_NOSIGNAL);
		g_write_since = 0;
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) {
			/* diatom went away, as it does between games: wait for the next
			 * one to listen, then go on with what is left. */
			long until = now_ms() + RECONNECT_MS;

			pcm_note("write: %s, reconnecting", w < 0 ? strerror(errno) : "closed");
			pcm_close();
			while (now_ms() < until && !dconnect()) usleep(100000);
			if (g_sock < 0) return false;
			continue;
		}
		p += w;
		left -= (size_t)w;
		g_written += w / 4;
	}
	return g_sock >= 0;
}

void pcm_drop(void)
{
	/* A disconnect is the drop: diatom empties its ring when a client goes. */
	if (g_sock < 0 || !g_written) return;
	pcm_note("drop: reconnect");
	pcm_close();
	dconnect();
	g_written = 0;
}

long pcm_queued(void)
{
	int out = 0;

	if (g_sock < 0 || ioctl(g_sock, TIOCOUTQ, &out) < 0) return 0;
	/* What is still in the socket, and diatom's ring, which holds about all of
	 * it while music is flowing - an estimate within one 10 ms chunk or so. */
	return out / 4 + MUSIC_RING - 512;
}

const char *pcm_error(void) { return g_err; }

bool pcm_is_open(void) { return g_sock >= 0; }

/* Diatom not up yet, or between execs: worth the second Muse waits for it. */
bool pcm_busy(void) { return g_last == -ECONNREFUSED || g_last == -ENOENT; }

#endif
