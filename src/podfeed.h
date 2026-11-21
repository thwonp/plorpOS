/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_PODFEED_H
#define TORTOS_PODFEED_H

#include <stdbool.h>
#include <stddef.h>

/* Reading what a podcast publishes (plorpos-7ni.2): an RSS or Atom feed, and
 * the OPML list another player exports its subscriptions as.
 *
 * Pure: a buffer in, structs out - no files, no network, no threads - so
 * tools/podfeed-check.c can feed it real feeds and look. Written for this
 * project from the RSS 2.0, iTunes and Atom documents; nothing is taken from
 * another player's code.
 *
 * A small XML reader of its own, not a general one: elements, attributes,
 * text, CDATA and the five entities plus numeric ones. Namespaces are known by
 * the prefix feeds actually use (itunes:, atom's default). Comments,
 * processing instructions and the doctype are skipped. What a feed says in
 * HTML (descriptions, show notes) is not read at all - a non-goal. */

typedef struct {
	char guid[512];          /* the item's guid, Atom's id; "" when it has none */
	char title[256];
	char url[1024];          /* the enclosure: the audio */
	char type[48];           /* its MIME type, "" if not said */
	long long len;           /* bytes, as the feed says (often 0) */
	long long stamp;         /* published, Unix seconds; 0 unknown */
	int dur;                 /* seconds, itunes:duration; 0 unknown */
} pf_item;

typedef struct {
	char title[256], author[128];
	char image[1024];        /* itunes:image, else the RSS <image><url> */
	pf_item *items;          /* in the feed's order; only items with audio */
	int n, cap;
} pf_feed;

/* The feed in `xml` (`len` bytes, need not be NUL-terminated). False when it
 * is neither RSS nor Atom; true with n 0 for a feed with no audio. Items
 * whose enclosure is video are left out (a non-goal). */
bool pf_parse(const char *xml, size_t len, pf_feed *out);
void pf_free(pf_feed *f);

/* OPML: each <outline xmlUrl=...> (any depth), its title or text. Returns how
 * many, at most `max`; -1 when it is not OPML. */
typedef struct { char url[1024], title[256]; } pf_outline;
int pf_opml(const char *xml, size_t len, pf_outline *out, int max);

/* RFC 822 ("Tue, 18 Nov 2025 08:00:00 -0500", zone names too) or ISO 8601
 * ("2025-11-18T08:00:00Z"): Unix seconds, 0 when it cannot be read. */
long long pf_date(const char *s);
/* itunes:duration: "3668", "61:08", "1:01:08" (decimals ignored); 0 unknown. */
int pf_duration(const char *s);
/* The extension a file of this enclosure gets: ".mp3", ".m4a", ".ogg", ".opus",
 * ".aac", ".flac", ".wav" - from the type, else the URL's path, else ".mp3". */
const char *pf_ext(const char *type, const char *url);

#endif
