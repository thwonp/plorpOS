/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_JFPARSE_H
#define TORTOS_JFPARSE_H

#include <stdbool.h>
#include <stddef.h>

#include "rajson.h"

/* The pure half of the Jellyfin source (plorpos-vgr.2): reading the server's
 * JSON and keeping its ids as the small numbers the shared core keys by. No
 * network, no threads - the host test (tools/jellyfin-check.c) runs it on
 * canned replies. */

/* A track as the sync wants it, from an /Items element (IncludeItemTypes=Audio).
 * Artists and albums are taken from the track's own fields: on 12.1-12.4 an
 * album's AlbumArtists can be empty (jellyfin#18658), a track's are not. */
typedef struct {
	char id[40], album_id[40], artist_id[200];   /* artist: its Id, else "n:<name>" */
	char title[512], album[256], artist[256];    /* artist: the album artist */
	char tartist[256];                           /* the track's own, when it differs */
	char thumb[120];                             /* "<album id>/<image tag>", "" without art */
	char ext[8];
	int  year, disc, idx, dur_ms;
} jf_track;

/* False for an element that is not a playable track (no Id or AlbumId). */
bool jf_track_parse(jsv item, jf_track *t);

/* A file extension from Jellyfin's Container ("flac", "mp3", "mov,mp4,m4a,..."):
 * the first token that is an audio extension we know, else "flac". */
void jf_ext(const char *container, char *out, size_t n);

/* An epoch second as the API's dates are written: "2026-11-24T17:00:00Z". */
void jf_iso(long t, char *out, size_t n);

/* Ids to keys: each id the server sends gets the next small number, written to
 * `path` as "<key>\t<id>" the moment it is made, so a key never changes - the
 * card's downloads, cache and playlists are named by them. */
typedef struct jf_keys jf_keys;
jf_keys *jf_keys_open(const char *path);   /* reads what the file has */
void jf_keys_free(jf_keys *k);
int  jf_key(jf_keys *k, const char *id);     /* made if new; 0 on failure */
int  jf_key_find(jf_keys *k, const char *id); /* 0 if not known */
const char *jf_key_id(jf_keys *k, int key);  /* NULL if not known */

#endif
