/* SPDX-License-Identifier: MIT */
/* Host check for src/jfparse.c (plorpos-vgr.2): Jellyfin's /Items replies
 * read into tracks, and ids kept as stable keys across a restart. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/jfparse.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char ITEMS[] =
	"{\"Items\":["
	/* the usual: album artist, its id, art, a FLAC */
	"{\"Name\":\"Dumbest Girl Alive\",\"Id\":\"a1b2c3d4e5f60718293a4b5c6d7e8f90\",\"Container\":\"flac\","
	"\"RunTimeTicks\":1609100000,\"ProductionYear\":2023,\"IndexNumber\":1,\"ParentIndexNumber\":1,"
	"\"Artists\":[\"100 gecs\"],\"Album\":\"10,000 gecs\",\"AlbumId\":\"00000000000000000000000000000a01\","
	"\"AlbumPrimaryImageTag\":\"5f1e\",\"AlbumArtist\":\"100 gecs\","
	"\"AlbumArtists\":[{\"Name\":\"100 gecs\",\"Id\":\"0000000000000000000000000000ar01\"}],\"Type\":\"Audio\"},"
	/* a featured artist, an m4a, a tab in the title, disc 2, no art */
	"{\"Name\":\"Song\\tTwo\",\"Id\":\"b1\",\"Container\":\"mov,mp4,m4a,3gp,3g2,mj2\","
	"\"RunTimeTicks\":2000000000,\"IndexNumber\":3,\"ParentIndexNumber\":2,"
	"\"Artists\":[\"Main\",\"Guest\"],\"Album\":\"Two\",\"AlbumId\":\"al2\",\"AlbumArtist\":\"Main\","
	"\"AlbumArtists\":[{\"Name\":\"Main\",\"Id\":\"ar2\"}]},"
	/* jellyfin#18658 and untagged files: no album artist at all */
	"{\"Name\":\"Loose\",\"Id\":\"c1\",\"Container\":\"MP3\",\"Artists\":[\"Solo\"],\"AlbumId\":\"al3\"},"
	/* not a track: no AlbumId */
	"{\"Name\":\"Orphan\",\"Id\":\"d1\",\"Container\":\"flac\"}"
	"],\"TotalRecordCount\":4}";

static void items(void)
{
	jsv root = js_root(ITEMS, sizeof ITEMS - 1), arr, it = { 0 }, e;
	jf_track t[4];
	int n = 0;

	CHECK(js_member(root, "Items", &arr), "no Items");
	while (n < 4 && js_next(arr, &it, &e)) { if (jf_track_parse(e, &t[n])) n++; }
	CHECK(n == 3, "3 tracks, got %d", n);

	CHECK(!strcmp(t[0].title, "Dumbest Girl Alive"), "title %s", t[0].title);
	CHECK(!strcmp(t[0].artist, "100 gecs") && !strcmp(t[0].artist_id, "0000000000000000000000000000ar01"),
	      "artist %s / %s", t[0].artist, t[0].artist_id);
	CHECK(!t[0].tartist[0], "no own artist when it is the album's: %s", t[0].tartist);
	CHECK(t[0].dur_ms == 160910, "duration %d", t[0].dur_ms);
	CHECK(t[0].year == 2023 && t[0].disc == 1 && t[0].idx == 1, "year/disc/idx");
	CHECK(!strcmp(t[0].thumb, "00000000000000000000000000000a01/5f1e"), "thumb %s", t[0].thumb);
	CHECK(!strcmp(t[0].ext, "flac"), "ext %s", t[0].ext);

	CHECK(!strcmp(t[1].ext, "m4a"), "m4a from a container list: %s", t[1].ext);
	CHECK(!strcmp(t[1].tartist, "Main, Guest"), "own artists %s", t[1].tartist);
	CHECK(!strchr(t[1].title, '\t'), "tab cleaned: %s", t[1].title);
	CHECK(t[1].disc == 2 && t[1].idx == 3 && !t[1].thumb[0], "disc 2, no art");

	CHECK(!strcmp(t[2].artist, "Solo") && !strcmp(t[2].artist_id, "n:Solo"), "fallback artist %s / %s",
	      t[2].artist, t[2].artist_id);
	CHECK(!strcmp(t[2].album, "Unknown Album"), "album %s", t[2].album);
	CHECK(!strcmp(t[2].ext, "mp3"), "MP3 lower-cased: %s", t[2].ext);
}

static void ext_iso(void)
{
	char b[32];

	jf_ext("", b, sizeof b);         CHECK(!strcmp(b, "flac"), "empty -> flac: %s", b);
	jf_ext("webma,webm", b, sizeof b); CHECK(!strcmp(b, "flac"), "unknown -> flac: %s", b);
	jf_ext("ogg", b, sizeof b);      CHECK(!strcmp(b, "ogg"), "ogg: %s", b);
	jf_iso(1795536000L, b, sizeof b); CHECK(!strcmp(b, "2026-11-24T16:00:00Z"), "iso %s", b);
}

static void keys(void)
{
	char path[] = "/tmp/jellyfin-check-XXXXXX";
	int fd = mkstemp(path), a, b, c, i;
	jf_keys *k;

	CHECK(fd >= 0, "mkstemp");
	close(fd);
	k = jf_keys_open(path);
	a = jf_key(k, "a1b2"); b = jf_key(k, "n:Solo"); c = jf_key(k, "a1b2");
	CHECK(a == 1 && b == 2 && c == 1, "keys %d %d %d", a, b, c);
	CHECK(!jf_key(k, "bad\tid") && !jf_key_find(k, "zzz"), "a tab is refused, unknown is 0");
	for (i = 0; i < 5000; i++) { char id[16]; snprintf(id, sizeof id, "x%d", i); jf_key(k, id); }  /* grows */
	CHECK(jf_key_find(k, "x4999") == 5002 && jf_key_find(k, "a1b2") == 1, "after growth");
	jf_keys_free(k);

	k = jf_keys_open(path);                        /* a restart */
	CHECK(jf_key_find(k, "n:Solo") == 2 && !strcmp(jf_key_id(k, 1), "a1b2"), "read back");
	CHECK(jf_key(k, "new") == 5003, "the next key follows the file: %d", jf_key_find(k, "new"));
	CHECK(jf_key_id(k, 9999) == NULL, "unknown key");
	jf_keys_free(k);
	unlink(path);
}

int main(void)
{
	items();
	ext_iso();
	keys();
	if (fails) { printf("jellyfin-check: %d failed\n", fails); return 1; }
	printf("jellyfin-check: ok\n");
	return 0;
}
