/* SPDX-License-Identifier: MIT */
/* Do Muse's playlists read and write the .m3u files they should?
 *
 *     make check-playlist
 *
 * In a temporary folder: names that make good files and names that do not,
 * adding to a new playlist and an existing one, every way an entry can be
 * written being read back card-relative, removing the right entry while the
 * comments stay, rename refusing to overwrite, delete, and the listing's
 * order and counts.
 *
 * No SDL, no daemon, no device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/playlist.h"

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

static void slurp(const char *path, char *out, size_t n)
{
	FILE *fp = fopen(path, "r");
	size_t k = fp ? fread(out, 1, n - 1, fp) : 0;

	out[k] = '\0';
	if (fp) fclose(fp);
}

int main(void)
{
	char root[] = "/tmp/playlist-check-XXXXXX", dir[256], f[300], body[2048];
	char **v;
	pl_info pl[8];
	int n;

	if (!mkdtemp(root)) { perror("mkdtemp"); return 1; }
	snprintf(dir, sizeof dir, "%s/Music/Playlists", root);
	snprintf(f, sizeof f, "%s/Music", root);
	mkdir(f, 0755);

	printf("names:\n");
	CHECK(pl_name_ok("Road Trip"), "a plain name");
	CHECK(pl_name_ok("90's Mix #2"), "punctuation a file can carry");
	CHECK(!pl_name_ok(""), "empty");
	CHECK(!pl_name_ok("   "), "all spaces");
	CHECK(!pl_name_ok(".hidden"), "a leading dot");
	CHECK(!pl_name_ok("a/b"), "a slash");
	CHECK(!pl_name_ok("what?"), "a character exFAT refuses");

	printf("adding:\n");
	CHECK(pl_list(dir, pl, 8) == 0, "no folder yet, no playlists");
	CHECK(pl_add(dir, "Road Trip", "Music/Baths/2010 Cerulean/01 Apologetic.flac"), "the first track makes the folder and file");
	CHECK(pl_add(dir, "Road Trip", "Music/Beck/1996 Odelay/01 Devils Haircut.mp3"), "a second track appends");
	CHECK(pl_add(dir, "Books", "Audiobooks/Someone/Book/01.mp3"), "a track outside Music");
	snprintf(f, sizeof f, "%s/Road Trip.m3u", dir);
	slurp(f, body, sizeof body);
	CHECK(!strcmp(body, "#EXTM3U\n../Baths/2010 Cerulean/01 Apologetic.flac\n"
	                   "../Beck/1996 Odelay/01 Devils Haircut.mp3\n"),
	      "written relative to the playlist, one header: [%s]", body);

	printf("reading:\n");
	n = pl_read(dir, "Road Trip", root, &v);
	CHECK(n == 2, "two entries, got %d", n);
	if (n == 2) {
		CHECK(!strcmp(v[0], "Music/Baths/2010 Cerulean/01 Apologetic.flac"), "back to card-relative: %s", v[0]);
		CHECK(!strcmp(v[1], "Music/Beck/1996 Odelay/01 Devils Haircut.mp3"), "the second: %s", v[1]);
	}
	pl_free(v, n);
	n = pl_read(dir, "Books", root, &v);
	CHECK(n == 1 && !strcmp(v[0], "Audiobooks/Someone/Book/01.mp3"), "../../ is card-relative");
	pl_free(v, n);
	{
		FILE *fp;

		snprintf(f, sizeof f, "%s/Mixed.m3u", dir);
		fp = fopen(f, "w");
		fprintf(fp, "#EXTM3U\r\n#EXTINF:123,Someone - Song\r\n..\\Win\\Path.mp3\r\n\r\n"
		            "%s/Music/Abs/One.mp3\nbeside.mp3\n   \n", root);
		fclose(fp);
	}
	n = pl_read(dir, "Mixed", root, &v);
	CHECK(n == 3, "three entries among comments and blanks, got %d", n);
	if (n == 3) {
		CHECK(!strcmp(v[0], "Music/Win/Path.mp3"), "backslashes and CRLF: %s", v[0]);
		CHECK(!strcmp(v[1], "Music/Abs/One.mp3"), "an absolute path under the card: %s", v[1]);
		CHECK(!strcmp(v[2], "Music/Playlists/beside.mp3"), "a bare name sits beside the playlist: %s", v[2]);
	}
	pl_free(v, n);
	CHECK(pl_read(dir, "Nope", root, &v) == -1, "a missing playlist cannot be read");

	printf("removing:\n");
	CHECK(pl_remove_at(dir, "Mixed", 1), "the middle entry");
	slurp(f, body, sizeof body);
	CHECK(strstr(body, "#EXTINF:123") && strstr(body, "Path.mp3") && !strstr(body, "Abs/One") &&
	      strstr(body, "beside.mp3"), "only that entry goes, comments stay: [%s]", body);
	CHECK(!pl_remove_at(dir, "Mixed", 5), "an entry past the end is refused");
	snprintf(f, sizeof f, "%s/Mixed.m3u.tmp", dir);
	CHECK(access(f, F_OK) != 0, "and leaves no temporary file");

	printf("moving:\n");
	{
		FILE *fp;

		snprintf(f, sizeof f, "%s/Order.m3u", dir);
		fp = fopen(f, "w");
		fprintf(fp, "#EXTM3U\n../A/1.mp3\n#EXTINF:5,Two\n../A/2.mp3\n../A/3.mp3\n");
		fclose(fp);
	}
	CHECK(pl_swap(dir, "Order", 0, 1), "the first two trade places");
	slurp(f, body, sizeof body);
	CHECK(!strcmp(body, "#EXTM3U\n#EXTINF:5,Two\n../A/2.mp3\n../A/1.mp3\n../A/3.mp3\n"),
	      "the EXTINF goes with its track: [%s]", body);
	CHECK(pl_swap(dir, "Order", 1, 2), "a later pair");
	slurp(f, body, sizeof body);
	CHECK(!strcmp(body, "#EXTM3U\n#EXTINF:5,Two\n../A/2.mp3\n../A/3.mp3\n../A/1.mp3\n"),
	      "and the rest stays put: [%s]", body);
	CHECK(!pl_swap(dir, "Order", 2, 3), "past the end is refused");
	CHECK(pl_remove_at(dir, "Order", 0), "removing an entry with an EXTINF");
	slurp(f, body, sizeof body);
	CHECK(!strcmp(body, "#EXTM3U\n../A/3.mp3\n../A/1.mp3\n"),
	      "takes the EXTINF too: [%s]", body);
	CHECK(pl_delete(dir, "Order"), "tidy up");

	printf("rename and delete:\n");
	CHECK(!pl_rename(dir, "Mixed", "Road Trip"), "not over another playlist");
	CHECK(pl_rename(dir, "Mixed", "mixed"), "a change of case only");
	CHECK(pl_rename(dir, "mixed", "Late Night"), "a new name");
	CHECK(pl_exists(dir, "Late Night") && !pl_exists(dir, "mixed"), "the file moved");
	CHECK(!pl_rename(dir, "Late Night", "bad/name"), "not to a bad name");

	printf("listing:\n");
	n = pl_list(dir, pl, 8);
	CHECK(n == 3, "three playlists, got %d", n);
	if (n == 3) {
		CHECK(!strcmp(pl[0].name, "Books") && !strcmp(pl[1].name, "Late Night") &&
		      !strcmp(pl[2].name, "Road Trip"), "by name: %s, %s, %s", pl[0].name, pl[1].name, pl[2].name);
		CHECK(pl[2].n == 2 && pl[1].n == 2, "with their entry counts");
	}
	CHECK(pl_delete(dir, "Books") && !pl_exists(dir, "Books"), "delete");
	CHECK(pl_list(dir, pl, 8) == 2, "two left");

	{
		char cmd[400];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
		if (system(cmd) != 0) printf("  (could not remove %s)\n", root);
	}
	if (failures) { printf("%d failure(s)\n", failures); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
