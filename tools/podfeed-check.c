/* SPDX-License-Identifier: MIT */
/* src/podfeed.c on the host (plorpos-7ni.2): its edge cases, then any feeds or
 * OPML files named on the command line, summarised.
 *
 *   cc -O1 -Isrc tools/podfeed-check.c src/podfeed.c -o build/podfeed-check
 *   build/podfeed-check [feed.xml | list.opml] ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "podfeed.h"

static int fails;
#define ck(c, what) do { if (!(c)) { fails++; printf("FAIL %s\n", what); } } while (0)

static char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb");
	char *b;
	long l;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
	b = malloc((size_t)l + 1);
	if (b && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); b = NULL; }
	fclose(f);
	if (b) { b[l] = '\0'; *n = (size_t)l; }
	return b;
}

static void edges(void)
{
	static const char RSS[] =
		"<?xml version=\"1.0\"?><!-- c --><rss xmlns:itunes=\"x\"><channel>"
		"<title>Show &amp; Tell</title><image><url>http://i/x.png</url><title>img</title></image>"
		"<itunes:image href=\"http://i/big.jpg\"/><itunes:author>Me</itunes:author>"
		"<item><title><![CDATA[Ep <1>]]></title><itunes:title>no</itunes:title>"
		"<guid isPermaLink=\"false\">g-1</guid><pubDate>Tue, 18 Nov 2025 08:00:00 -0500</pubDate>"
		"<enclosure url=\"https://a/b.mp3?x=1&amp;y=2\" length=\"123\" type=\"audio/mpeg\"/>"
		"<itunes:duration>1:01:08</itunes:duration><description><p>notes</p></description></item>"
		"<item><title>Video</title><enclosure url=\"https://v/c.mp4\" type=\"video/mp4\"/></item>"
		"<item><title>No audio</title></item>"
		"<item><title>Two&#8217;s</title><pubDate>18 Nov 25 13:00 GMT</pubDate>"
		"<enclosure url='https://a/d.m4a' type='audio/x-m4a'/><itunes:duration>3668</itunes:duration></item>"
		"</channel></rss>";
	static const char ATOM[] =
		"<feed xmlns=\"http://www.w3.org/2005/Atom\"><title>A</title>"
		"<entry><title>E</title><id>urn:1</id><published>2025-11-18T13:00:00Z</published>"
		"<link rel=\"alternate\" href=\"https://page\"/>"
		"<link rel=\"enclosure\" href=\"https://a/e.ogg\" type=\"audio/ogg\" length=\"9\"/></entry></feed>";
	static const char OPML[] =
		"<opml version=\"2.0\"><body><outline text=\"Feeds\"><outline text=\"One\" type=\"rss\" "
		"xmlUrl=\"https://f/1?a=1&amp;b=2\"/><outline title=\"Two\" text=\"t\" xmlUrl=\"https://f/2\"/>"
		"</outline></body></opml>";
	pf_feed f;
	pf_outline o[4];

	ck(pf_parse(RSS, sizeof RSS - 1, &f), "rss is a feed");
	ck(!strcmp(f.title, "Show & Tell"), "channel title, not the image's");
	ck(!strcmp(f.image, "http://i/big.jpg"), "itunes:image wins over <image><url>");
	ck(!strcmp(f.author, "Me"), "itunes:author");
	ck(f.n == 2, "two audio items: video and no-enclosure left out");
	if (f.n == 2) {
		ck(!strcmp(f.items[0].title, "Ep <1>"), "CDATA title, not itunes:title");
		ck(!strcmp(f.items[0].guid, "g-1"), "guid");
		ck(!strcmp(f.items[0].url, "https://a/b.mp3?x=1&y=2"), "enclosure url decoded");
		ck(f.items[0].len == 123 && f.items[0].dur == 3668, "length and h:mm:ss");
		ck(f.items[0].stamp == 1763470800LL, "RFC 822 with -0500");
		ck(!strcmp(f.items[1].title, "Two\xE2\x80\x99s"), "numeric entity to UTF-8");
		ck(f.items[1].stamp == 1763470800LL, "2-digit year, GMT");
		ck(!strcmp(pf_ext(f.items[1].type, f.items[1].url), ".m4a"), "m4a by type");
	}
	pf_free(&f);
	ck(pf_parse(ATOM, sizeof ATOM - 1, &f) && f.n == 1, "atom entry with an enclosure link");
	if (f.n == 1) {
		ck(!strcmp(f.items[0].url, "https://a/e.ogg") && f.items[0].stamp == 1763470800LL &&
		   !strcmp(f.items[0].guid, "urn:1"), "atom fields");
	}
	pf_free(&f);
	ck(!pf_parse("<html><body>no</body></html>", 28, &f), "html is not a feed");
	pf_free(&f);
	ck(pf_opml(OPML, sizeof OPML - 1, o, 4) == 2, "two outlines with xmlUrl");
	ck(!strcmp(o[0].url, "https://f/1?a=1&b=2") && !strcmp(o[0].title, "One") &&
	   !strcmp(o[1].title, "Two"), "opml url decoded, title over text");
	ck(pf_opml("<rss/>", 6, o, 4) == -1, "not opml");
	ck(pf_duration("61:08") == 3668 && pf_duration("3668.5") == 3668 && pf_duration("") == 0, "durations");
	ck(pf_date("2025-11-18T09:00:00+01:00") == 1763452800LL, "iso with offset");
	ck(!strcmp(pf_ext("", "https://x/y.MP3?z=.m4a"), ".MP3") || !strcmp(pf_ext("", "https://x/y.MP3?z=.m4a"), ".mp3"),
	   "ext from path before the query");
	ck(!strcmp(pf_ext(NULL, "https://x/y"), ".mp3"), "mp3 when nothing says");
}

int main(int argc, char **argv)
{
	int i;

	edges();
	for (i = 1; i < argc; i++) {
		size_t n;
		char *b = slurp(argv[i], &n);
		pf_feed f;
		pf_outline o[256];
		int k, nodate = 0, nodur = 0, noguid = 0;

		if (!b) { printf("%s: cannot read\n", argv[i]); fails++; continue; }
		if ((k = pf_opml(b, n, o, 256)) >= 0) {
			printf("%s: OPML, %d feeds; first %s <%s>\n", argv[i], k, k ? o[0].title : "", k ? o[0].url : "");
			free(b);
			continue;
		}
		if (!pf_parse(b, n, &f)) { printf("%s: not a feed\n", argv[i]); fails++; free(b); continue; }
		for (k = 0; k < f.n; k++) {
			nodate += !f.items[k].stamp;
			nodur += !f.items[k].dur;
			noguid += !f.items[k].guid[0];
		}
		printf("%-10s %5zu KB %4d eps  no date %d, no dur %d, no guid %d  \"%.40s\" img=%s\n",
		       strrchr(argv[i], '/') ? strrchr(argv[i], '/') + 1 : argv[i], n / 1024, f.n,
		       nodate, nodur, noguid, f.title, f.image[0] ? "yes" : "NO");
		if (f.n) printf("           newest: %lld %.50s (%d s) %s\n", f.items[0].stamp, f.items[0].title,
		                f.items[0].dur, pf_ext(f.items[0].type, f.items[0].url));
		ck(f.n > 0 && f.title[0], argv[i]);
		pf_free(&f);
		free(b);
	}
	printf(fails ? "%d FAILED\n" : "ok\n", fails);
	return fails != 0;
}
