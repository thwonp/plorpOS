/* SPDX-License-Identifier: MIT */
/* Does Muse read the Music folder the way its header says?
 *
 *     make check-muselib
 *
 * A folder is built under /tmp in both shapes muselib understands, with the
 * litter a Mac leaves on a card copied from it, and read back. Then the
 * questions the screens ask of it: which album is this track in, where is that
 * album's cover kept, and which album is each card on the shelf in either of
 * its orders.
 *
 * No SDL, no daemon, no device.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/muselib.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

static char root[256];

static void touch(const char *rel)
{
	char p[512];
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	f = fopen(p, "w");
	if (f) fclose(f);
}

static void dir(const char *rel)
{
	char p[512];

	snprintf(p, sizeof p, "%s/%s", root, rel);
	mkdir(p, 0755);
}

static int album_named(const ml_lib *l, const char *name)
{
	int i;

	for (i = 0; i < l->nalbums; i++)
		if (!strcmp(l->albums[i].name, name)) return i;
	return -1;
}

static void names(void)
{
	char out[128];

	printf("track names\n");
	ml_track_name("03 High And Dry.mp3", out, sizeof out);
	CHECK(!strcmp(out, "High And Dry"), "number and space: got \"%s\"", out);
	ml_track_name("01 - Orphan.flac", out, sizeof out);
	CHECK(!strcmp(out, "Orphan"), "number and dash: got \"%s\"", out);
	ml_track_name("1979.mp3", out, sizeof out);
	CHECK(!strcmp(out, "1979"), "a track CALLED a number keeps it: got \"%s\"", out);
	ml_track_name("01.mp3", out, sizeof out);
	CHECK(!strcmp(out, "01"), "a number and nothing else is its name, not the extension: got \"%s\"", out);
	ml_track_name("Intro.opus", out, sizeof out);
	CHECK(!strcmp(out, "Intro"), "no number: got \"%s\"", out);
}

static void scan(void)
{
	ml_lib l;
	char base[512], want[512];
	int bends, bends2, pod, i;

	printf("the folder\n");
	dir("Radiohead");
	dir("Radiohead/The Bends");
	dir("Radiohead/The Bends 2");
	dir("Radiohead/.media");
	dir("Podcast");
	dir("Empty");
	touch("Radiohead/The Bends/02 Bones.mp3");
	touch("Radiohead/The Bends/01 Planet Telex.mp3");
	touch("Radiohead/The Bends/._01 Planet Telex.mp3");   /* AppleDouble */
	touch("Radiohead/The Bends/notes.txt");
	touch("Radiohead/The Bends 2/01 Bonus.mp3");
	touch("Radiohead/.media/The Bends.jpg");               /* a cover, not an album */
	touch("Podcast/ep1.mp3");
	touch("loose.mp3");                                   /* under the root: no album */
	dir("Singles");                                       /* where the launcher moves it */
	touch("Singles/A Single.mp3");

	CHECK(ml_scan(root, &l), "a folder that is there scans");
	CHECK(l.nartists == 3, "two artists with music and Singles, not the empty one: %d",
	      l.nartists);
	CHECK(l.nalbums == 4, "three albums and Singles, and .media is not one: %d", l.nalbums);
	CHECK(l.ntracks == 5, "five tracks, no AppleDouble, no text, nothing loose: %d",
	      l.ntracks);

	printf("Singles\n");
	{
		int s = album_named(&l, ML_SINGLES);

		CHECK(s >= 0 && l.albums[s].singles && !l.albums[s].book,
		      "Music/Singles is an album marked as Singles");
		CHECK(album_named(&l, "The Bends") >= 0 &&
		      !l.albums[album_named(&l, "The Bends")].singles,
		      "and no other album is");
	}

	bends  = album_named(&l, "The Bends");
	bends2 = album_named(&l, "The Bends 2");
	pod    = album_named(&l, "Podcast");
	CHECK(bends >= 0 && bends2 >= 0 && pod >= 0, "the albums are named by folder");
	if (bends < 0 || bends2 < 0 || pod < 0) { ml_free(&l); return; }
	CHECK(l.albums[bends].n == 2, "The Bends has two tracks: %d", l.albums[bends].n);
	CHECK(!strcmp(l.tracks[l.albums[bends].first].name, "Planet Telex"),
	      "sorted by file name, shown without the number: \"%s\"",
	      l.tracks[l.albums[bends].first].name);

	printf("which album a track is in\n");
	CHECK(ml_album_of(&l, "Radiohead/The Bends/02 Bones.mp3") == bends,
	      "a track in The Bends");
	CHECK(ml_album_of(&l, "Radiohead/The Bends 2/01 Bonus.mp3") == bends2,
	      "a folder whose name starts with another's is its own album");
	CHECK(ml_album_of(&l, "Podcast/ep1.mp3") == pod, "the flat shape");
	CHECK(ml_album_of(&l, "Radiohead/Pablo Honey/01 You.mp3") == -1,
	      "a folder the scan never saw");
	CHECK(ml_album_of(&l, "") == -1, "nothing playing");

	printf("where covers are kept\n");
	ml_cover_base(root, &l, bends, base, sizeof base);
	snprintf(want, sizeof want, "%s/Radiohead/.media/The Bends", root);
	CHECK(!strcmp(base, want), "beside the album: got %s", base);
	ml_cover_base(root, &l, pod, base, sizeof base);
	snprintf(want, sizeof want, "%s/.media/Podcast", root);
	CHECK(!strcmp(base, want), "under the root for the flat shape: got %s", base);
	ml_cover_base(root, &l, l.nalbums, base, sizeof base);
	CHECK(base[0] == '\0', "no album, no path");

	for (i = 0; i < l.ntracks; i++)
		CHECK(l.tracks[i].path[0] != '/', "paths are relative: %s", l.tracks[i].path);
	ml_free(&l);
}

/* The shelf's two orders, on a folder where they differ: two artists with a
 * "Greatest Hits" each, and an album named in lower case, which has to sort
 * where its letters say and not after every capital. */
static void orders(void)
{
	static const char *const want[] = {
		"amnesiac", "Greatest Hits", "Greatest Hits", "Parklife", "The Bends"
	};
	char sub[300];
	ml_lib l;
	int out[8], k;

	printf("the shelf's orders\n");
	CHECK(ml_order_index("album") == ML_BY_ALBUM, "album is stored as \"album\"");
	CHECK(ml_order_index("artist") == ML_BY_ARTIST, "artist as \"artist\"");
	CHECK(ml_order_index("name") == ML_BY_ARTIST && ml_order_index(NULL) == ML_BY_ARTIST,
	      "anything else is the folder's own order");
	CHECK(!strcmp(ml_order_label(ML_BY_ALBUM, false), "Album") &&
	      !strcmp(ml_order_label(ML_BY_ARTIST, false), "Artist"), "what the menu says");
	CHECK(!strcmp(ml_order_label(ML_BY_ALBUM, true), "Title") &&
	      !strcmp(ml_order_label(ML_BY_ARTIST, true), "Author"),
	      "and what it says on the books");
	CHECK(ml_order_index("year") == ML_BY_YEAR &&
	      !strcmp(ml_order_label(ML_BY_YEAR, false), "Year") &&
	      !strcmp(ml_order_label(ML_BY_YEAR, true), "Year"), "year, on music and books");
	CHECK(!strcmp(ml_order_name((ml_order)ML_ORDERS), "artist"),
	      "a number out of range reads as the first order, not past the table");

	dir("o");
	dir("o/Blur");
	dir("o/Blur/Parklife");
	dir("o/Blur/Greatest Hits");
	dir("o/Queen");
	dir("o/Queen/Greatest Hits");
	dir("o/Radiohead");
	dir("o/Radiohead/The Bends");
	dir("o/Radiohead/amnesiac");
	touch("o/Blur/Parklife/01 Girls And Boys.mp3");
	touch("o/Blur/Greatest Hits/01 Beetlebum.mp3");
	touch("o/Queen/Greatest Hits/01 Bohemian Rhapsody.mp3");
	touch("o/Radiohead/The Bends/01 Planet Telex.mp3");
	touch("o/Radiohead/amnesiac/01 Packt.mp3");
	snprintf(sub, sizeof sub, "%s/o", root);
	CHECK(ml_scan(sub, &l) && l.nalbums == 5, "five albums: %d", l.nalbums);
	if (l.nalbums != 5) { ml_free(&l); return; }

	CHECK(ml_shelf_order(&l, ML_BY_ARTIST, false, NULL, out) == 5, "every album is music");
	for (k = 0; k < 5; k++)
		CHECK(out[k] == k, "by artist is the scan's order: card %d is album %d", k, out[k]);

	ml_shelf_order(&l, ML_BY_ALBUM, false, NULL, out);
	for (k = 0; k < 5; k++)
		CHECK(!strcmp(l.albums[out[k]].name, want[k]),
		      "by album, card %d: want %s, got %s", k, want[k], l.albums[out[k]].name);
	CHECK(!strncmp(l.tracks[l.albums[out[1]].first].path, "Blur/", 5) &&
	      !strncmp(l.tracks[l.albums[out[2]].first].path, "Queen/", 6),
	      "two of one title go in their artists' order");
	CHECK(ml_shelf_order(&l, ML_BY_ALBUM, true, NULL, out) == 0, "and none is a book");

	/* Scan order: Blur/Greatest Hits, Blur/Parklife, Queen/Greatest Hits,
	 * Radiohead/amnesiac, Radiohead/The Bends - case does not count
	 * (plorpos-xav). */
	{
		int years[5] = { 1997, 1994, 0, 2001, 1995 };
		const int want_y[5] = { 3, 0, 4, 1, 2 };

		for (k = 0; k < 5; k++)
			CHECK(!strcmp(l.albums[k].name, (const char *[]){ "Greatest Hits",
			      "Parklife", "Greatest Hits", "amnesiac", "The Bends" }[k]),
			      "scan order as assumed, album %d", k);
		ml_shelf_order(&l, ML_BY_YEAR, false, years, out);
		for (k = 0; k < 5; k++)
			CHECK(out[k] == want_y[k], "by year newest first, none last: card %d is %d, want %d",
			      k, out[k], want_y[k]);
		years[4] = 1997;
		ml_shelf_order(&l, ML_BY_YEAR, false, years, out);
		CHECK(out[1] == 0 && out[2] == 4, "one year keeps artist order");
		years[1] = -1;
		ml_shelf_order(&l, ML_BY_YEAR, false, years, out);
		CHECK(out[3] == 1 && out[4] == 2, "not known yet goes with none, in artist order");
		ml_shelf_order(&l, ML_BY_YEAR, false, NULL, out);
		for (k = 0; k < 5; k++)
			CHECK(out[k] == k, "no years at all is artist order: card %d is %d", k, out[k]);
	}
	ml_free(&l);
}

/* A card's two folders read as one library, the books marked: what Muse's
 * shelf filters on, and what the launcher resumes. Then the pictures a folder
 * carries, which is where a book's cover usually is. */
static void card(void)
{
	char c[300], img[600], want[600];
	ml_lib l;
	int out[8], dcc, hs, n;

	printf("a card's Music and Audiobooks\n");
	dir("card");
	dir("card/Music");
	dir("card/Music/Blur");
	dir("card/Music/Blur/Parklife");
	touch("card/Music/Blur/Parklife/01 Girls And Boys.mp3");
	touch("card/Music/Blur/Parklife/cover.JPG");
	dir("card/Audiobooks");
	dir("card/Audiobooks/Dungeon Crawler Carl 3");
	touch("card/Audiobooks/Dungeon Crawler Carl 3/Book 3.m4b");
	touch("card/Audiobooks/Dungeon Crawler Carl 3/Book 3.jpg");
	touch("card/Audiobooks/Dungeon Crawler Carl 3/Book 3.cue");
	dir("card/Audiobooks/Banks");
	dir("card/Audiobooks/Banks/The Hydrogen Sonata");
	touch("card/Audiobooks/Banks/The Hydrogen Sonata/disc 02.mp3");
	touch("card/Audiobooks/Banks/The Hydrogen Sonata/disc 01.mp3");
	touch("card/Audiobooks/Banks/The Hydrogen Sonata/back.jpg");
	touch("card/Audiobooks/Banks/The Hydrogen Sonata/front.jpg");
	snprintf(c, sizeof c, "%s/card", root);

	CHECK(ml_scan_card(c, &l), "a card with both folders scans");
	CHECK(l.nalbums == 3 && ml_count(&l, false) == 1 && ml_count(&l, true) == 2,
	      "one album and two books: %d, %d, %d", l.nalbums, ml_count(&l, false),
	      ml_count(&l, true));
	dcc = album_named(&l, "Dungeon Crawler Carl 3");
	hs  = album_named(&l, "The Hydrogen Sonata");
	CHECK(dcc >= 0 && hs >= 0, "books named by folder, in both shapes");
	if (dcc < 0 || hs < 0) { ml_free(&l); return; }
	CHECK(l.albums[dcc].book && l.albums[hs].book && !l.albums[album_named(&l, "Parklife")].book,
	      "the books are marked, the album is not");
	CHECK(!strcmp(l.tracks[l.albums[hs].first].path,
	              "Audiobooks/Banks/The Hydrogen Sonata/disc 01.mp3"),
	      "paths are relative to the card: %s", l.tracks[l.albums[hs].first].path);
	CHECK(ml_album_of(&l, "Audiobooks/Dungeon Crawler Carl 3/Book 3.m4b") == dcc,
	      "which book a file is in");
	n = ml_shelf_order(&l, ML_BY_ARTIST, true, NULL, out);
	CHECK(n == 2 && out[0] == hs && out[1] == dcc,
	      "the book shelf is the books alone, by author: %d", n);
	CHECK(ml_shelf_order(&l, ML_BY_ARTIST, false, NULL, out) == 1, "the music shelf the album alone");

	ml_cover_base(c, &l, dcc, img, sizeof img);
	snprintf(want, sizeof want, "%s/Audiobooks/.media/Dungeon Crawler Carl 3", c);
	CHECK(!strcmp(img, want), "a book's cover is kept like an album's: %s", img);

	printf("pictures in a folder\n");
	snprintf(want, sizeof want, "%s/Audiobooks/Dungeon Crawler Carl 3/Book 3.jpg", c);
	CHECK(ml_folder_image(c, &l, dcc, img, sizeof img) && !strcmp(img, want),
	      "the only picture, whatever it is called: %s", img);
	snprintf(want, sizeof want, "%s/Music/Blur/Parklife/cover.JPG", c);
	CHECK(ml_folder_image(c, &l, album_named(&l, "Parklife"), img, sizeof img) &&
	      !strcmp(img, want), "cover, in any case: %s", img);
	CHECK(!ml_folder_image(c, &l, hs, img, sizeof img),
	      "two pictures and neither named cover: no guess");
	ml_free(&l);

	snprintf(c, sizeof c, "%s/nothing", root);
	CHECK(!ml_scan_card(c, &l), "a card with neither folder does not scan");
	ml_free(&l);
}

/* Music/Plex/<artist>/<album>: Muse's Plex downloads (plorpos-1lw). */
static void plex(void)
{
	char c[300];
	ml_lib l;
	int al;

	printf("Plex downloads under Music/Plex\n");
	dir("pcard");
	dir("pcard/Music");
	dir("pcard/Music/Blur");
	dir("pcard/Music/Blur/Parklife");
	touch("pcard/Music/Blur/Parklife/01 Girls And Boys.mp3");
	dir("pcard/Music/Plex");
	dir("pcard/Music/Plex/Pixies");
	dir("pcard/Music/Plex/Pixies/Doolittle");
	touch("pcard/Music/Plex/Pixies/Doolittle/02 Tame.opus");
	touch("pcard/Music/Plex/Pixies/Doolittle/01 Debaser.opus");
	dir("pcard/Music/Plex/Pixies/Plex");                  /* not a container again */
	touch("pcard/Music/Plex/Pixies/Plex/01 x.mp3");
	/* Sorted in with the card's own artists, and one of them when the
	 * card has it too (plorpos-1lw.14). */
	dir("pcard/Music/Plex/Arcade Fire");
	dir("pcard/Music/Plex/Arcade Fire/Funeral");
	touch("pcard/Music/Plex/Arcade Fire/Funeral/01 Tunnels.opus");
	dir("pcard/Music/Plex/blur");
	dir("pcard/Music/Plex/blur/13");
	touch("pcard/Music/Plex/blur/13/01 Tender.opus");
	snprintf(c, sizeof c, "%s/pcard", root);
	CHECK(ml_scan_card(c, &l), "a card with Music/Plex scans");
	CHECK(l.nartists == 3, "Arcade Fire, Blur, Pixies - no Plex, one Blur: %d", l.nartists);
	if (l.nartists == 3) {
		int i, k = 0;

		CHECK(!strcmp(l.artists[0].name, "Arcade Fire") && !strcmp(l.artists[1].name, "Blur") &&
		      !strcmp(l.artists[2].name, "Pixies"), "artists by name: %s, %s, %s",
		      l.artists[0].name, l.artists[1].name, l.artists[2].name);
		CHECK(l.artists[1].n == 2 && !strcmp(l.albums[l.artists[1].first].name, "13") &&
		      !strcmp(l.albums[l.artists[1].first + 1].name, "Parklife"),
		      "Blur's albums together, by name: %d", l.artists[1].n);
		for (i = 0; i < l.nartists; i++) {
			CHECK(l.artists[i].first == k, "artist %d's albums follow the last's", i);
			k += l.artists[i].n;
		}
		CHECK(k == l.nalbums, "every album is an artist's: %d of %d", k, l.nalbums);
	}
	al = album_named(&l, "Doolittle");
	CHECK(al >= 0 && l.albums[al].n == 2, "the downloaded album and its tracks");
	if (al >= 0)
		CHECK(!strcmp(l.tracks[l.albums[al].first].path,
		              "Music/Plex/Pixies/Doolittle/01 Debaser.opus"),
		      "paths keep the Plex level: %s", l.tracks[l.albums[al].first].path);
	CHECK(album_named(&l, "Plex") >= 0, "a Plex album under an artist is an album");
	ml_free(&l);
}

/* Music/Jellyfin/<artist>/<album>: Jellyfin's downloads, read as Plex's, and
 * one artist with the card's own and Plex's (plorpos-vgr.13). */
static void jellyfin(void)
{
	char c[300];
	ml_lib l;
	int al;

	printf("Jellyfin downloads under Music/Jellyfin\n");
	dir("jcard");
	dir("jcard/Music");
	dir("jcard/Music/Blur");
	dir("jcard/Music/Blur/Parklife");
	touch("jcard/Music/Blur/Parklife/01 Girls And Boys.mp3");
	dir("jcard/Music/Plex");
	dir("jcard/Music/Plex/Blur");
	dir("jcard/Music/Plex/Blur/13");
	touch("jcard/Music/Plex/Blur/13/01 Tender.opus");
	dir("jcard/Music/Jellyfin");
	dir("jcard/Music/Jellyfin/blur");
	dir("jcard/Music/Jellyfin/blur/Think Tank");
	touch("jcard/Music/Jellyfin/blur/Think Tank/01 Ambulance.opus");
	dir("jcard/Music/Jellyfin/Low");
	dir("jcard/Music/Jellyfin/Low/Things We Lost in the Fire");
	touch("jcard/Music/Jellyfin/Low/Things We Lost in the Fire/02 Sunflower.opus");
	touch("jcard/Music/Jellyfin/Low/Things We Lost in the Fire/01 Medicine Magic.opus");
	snprintf(c, sizeof c, "%s/jcard", root);
	CHECK(ml_scan_card(c, &l), "a card with Music/Jellyfin scans");
	CHECK(l.nartists == 2, "Blur, Low - no Jellyfin, no Plex, one Blur: %d", l.nartists);
	if (l.nartists == 2) {
		CHECK(!strcmp(l.artists[0].name, "Blur") && !strcmp(l.artists[1].name, "Low"),
		      "artists by name: %s, %s", l.artists[0].name, l.artists[1].name);
		CHECK(l.artists[0].n == 3, "Blur's card, Plex and Jellyfin albums together: %d",
		      l.artists[0].n);
	}
	al = album_named(&l, "Things We Lost in the Fire");
	CHECK(al >= 0 && l.albums[al].n == 2, "the downloaded album and its tracks");
	if (al >= 0)
		CHECK(!strcmp(l.tracks[l.albums[al].first].path,
		              "Music/Jellyfin/Low/Things We Lost in the Fire/01 Medicine Magic.opus"),
		      "paths keep the Jellyfin level: %s", l.tracks[l.albums[al].first].path);
	CHECK(album_named(&l, "Think Tank") >= 0, "a Jellyfin album of a card artist");
	CHECK(album_named(&l, "Jellyfin") < 0 && album_named(&l, "Low") < 0,
	      "no Jellyfin artist, no artist folder as an album");
	ml_free(&l);
}

static bool there(const char *rel)
{
	char p[512];
	struct stat st;

	snprintf(p, sizeof p, "%s/%s", root, rel);
	return stat(p, &st) == 0;
}

static void gone(const char *rel)
{
	char p[512];

	snprintf(p, sizeof p, "%s/%s", root, rel);
	rmdir(p);
}

/* A deleted book or album takes its cover and the folders it empties
 * (plorpos-5tm.14). */
static void prune(void)
{
	char d[640], top[512], plex[512];

	printf("prune: a deleted folder's cover and the folders it empties\n");
	snprintf(top, sizeof top, "%s/P/Audiobooks", root);
	snprintf(plex, sizeof plex, "%s/P/Music/Plex", root);
	dir("P"); dir("P/Audiobooks"); dir("P/Audiobooks/.media");
	dir("P/Audiobooks/King"); dir("P/Audiobooks/King/.media"); dir("P/Audiobooks/King/It");
	touch("P/Audiobooks/King/.media/Carrie.jpg"); touch("P/Audiobooks/King/.media/It.png");
	touch("P/Audiobooks/.media/Solo.jpg"); touch("P/Audiobooks/.media/Other.jpg");
	dir("P/Audiobooks/Wilde"); dir("P/Audiobooks/Wilde/.media");
	touch("P/Audiobooks/Wilde/.media/Gone.jpg"); touch("P/Audiobooks/Wilde/notes.txt");

	/* Carrie gone, It still there: only Carrie's cover goes. */
	snprintf(d, sizeof d, "%s/King/Carrie", top); ml_prune_up(d, top);
	CHECK(!there("P/Audiobooks/King/.media/Carrie.jpg"), "Carrie's cover stays");
	CHECK(there("P/Audiobooks/King/.media/It.png"), "It's cover went");
	CHECK(there("P/Audiobooks/King/It"), "the other book went");

	/* It gone too: cover, .media and King go; Audiobooks stays. */
	gone("P/Audiobooks/King/It");
	snprintf(d, sizeof d, "%s/King/It", top); ml_prune_up(d, top);
	CHECK(!there("P/Audiobooks/King"), "an emptied author stays");
	CHECK(there("P/Audiobooks"), "Audiobooks went");

	/* A book straight under Audiobooks: its cover only, Audiobooks/.media
	 * kept while another cover is in it. */
	snprintf(d, sizeof d, "%s/Solo", top); ml_prune_up(d, top);
	CHECK(!there("P/Audiobooks/.media/Solo.jpg"), "Solo's cover stays");
	CHECK(there("P/Audiobooks/.media/Other.jpg"), "another cover went");

	/* Something not ours in the author's folder keeps it. */
	snprintf(d, sizeof d, "%s/Wilde/Gone", top); ml_prune_up(d, top);
	CHECK(!there("P/Audiobooks/Wilde/.media"), "an emptied .media stays");
	CHECK(there("P/Audiobooks/Wilde/notes.txt"), "a stray file went");

	/* Outside stop, or a name stop is only a prefix of: nothing. */
	dir("P/AudiobooksX"); dir("P/AudiobooksX/.media"); touch("P/AudiobooksX/.media/A.jpg");
	snprintf(d, sizeof d, "%s/P/AudiobooksX/A", root); ml_prune_up(d, top);
	CHECK(there("P/AudiobooksX/.media/A.jpg"), "a folder outside stop was touched");
	ml_prune_up(top, top);
	CHECK(there("P/Audiobooks/.media/Other.jpg"), "stop itself was pruned");

	/* Plex: album and artist both empty out, each with its cover; Music/Plex stays. */
	dir("P/Music"); dir("P/Music/Plex"); dir("P/Music/Plex/.media");
	dir("P/Music/Plex/Art"); dir("P/Music/Plex/Art/.media");
	touch("P/Music/Plex/Art/.media/Alb.jpg"); touch("P/Music/Plex/.media/Art.jpg");
	snprintf(d, sizeof d, "%s/Art/Alb", plex); ml_prune_up(d, plex);
	CHECK(!there("P/Music/Plex/Art"), "an emptied artist stays");
	CHECK(!there("P/Music/Plex/.media"), "the artist's picture stays");
	CHECK(there("P/Music/Plex"), "Music/Plex went");
}

int main(void)
{
	char cmd[300];

	snprintf(root, sizeof root, "/tmp/muselib-check.XXXXXX");
	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }

	printf("muselib: the Music and Audiobooks folders\n");
	names();
	scan();
	orders();
	card();
	plex();
	jellyfin();
	prune();

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
	if (system(cmd) != 0) { }

	if (failures) {
		printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("ok\n");
	return 0;
}
