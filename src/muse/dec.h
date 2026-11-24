/* SPDX-License-Identifier: MIT */
#ifndef MUSE_DEC_H
#define MUSE_DEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One file, decoded and brought to what the Brick's mixer takes: 48 kHz,
 * signed 16-bit, stereo, interleaved.
 *
 * Everything goes through the device's own FFmpeg 6.1 - the libraries in
 * /usr/lib, which is why there is no decoder of our own here. Measured on the
 * Brick 2026-09-17 (BACKLOG, the Muse spike): MP3, AAC, M4B, FLAC and Opus at
 * 88-116x realtime and about 1% of a core, 12-14 MB resident, with the M4B's
 * chapters and every file's tags read by the same library. */

#define DEC_RATE     48000
#define DEC_CHANNELS 2

typedef struct dec dec;

/* Open `path` and position it at `at` seconds, playing at `speed` (1.0 is
 * normal; 0.5-2.0 is what anyone uses, and atempo changes it without changing
 * pitch). `proxy`, when not NULL or "", is the HTTP proxy a stream goes
 * through (tailscaled's, plorpos-8ns.5). NULL on failure, with the reason in
 * `err`. */
dec *dec_open(const char *path, const char *proxy, double at, double speed, char *err, size_t errn);

/* Up to `max` frames into `buf` (a frame is one sample per channel). Returns
 * how many, 0 at the end of the file, <0 on an error the file cannot recover
 * from. */
int dec_read(dec *d, int16_t *buf, int max);

/* Where the output is, in the FILE's seconds - which is not the number of
 * seconds played once the speed is not 1. */
double dec_pos(const dec *d);
/* Move the open file to `at` seconds: no new connection to a stream's server
 * beyond the one ranged request, where a dec_open follows every redirect and
 * reads the stream's header again - 2-5 s over Wi-Fi (owner, plorpos-7ni.10).
 * 0 when it could not, and `d` is then only fit for dec_close. */
int dec_seek(dec *d, double at);
/* Play on at `speed` from where the decoder is, reading nothing again. 0 when
 * it could not, and `d` is then only fit for dec_close. */
int dec_speed(dec *d, double speed);
/* Whether the end dec_read reached was a read that failed - a stream whose
 * server or Wi-Fi went, after rw_timeout - rather than the file's own end. */
int dec_cut(const dec *d);
double dec_len(const dec *d);

/* A tag, or "" when the file has none: "title", "artist", "album". */
const char *dec_tag(const dec *d, const char *key);

/* What the file is, for a badge: "FLAC 16/44.1" for lossless, "MP3 320"
 * for lossy (kbps, snapped to a standard rate within 8%), or just the codec
 * when no rate is known - a stream often says none. Once per open. */
void dec_codec(const dec *d, char *out, size_t n);

/* Chapters: M4B carries them; everything else answers 0. */
int         dec_chapters(const dec *d);
double      dec_chapter_at(const dec *d, int i);
const char *dec_chapter_title(const dec *d, int i);

void dec_close(dec *d);

#endif
