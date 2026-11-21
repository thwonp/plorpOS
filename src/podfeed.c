/* SPDX-License-Identifier: MIT */
/* See podfeed.h. */
#include "podfeed.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* A copy that cuts at `n` - the fields are fixed, the feeds are not. */
static void cpy(char *dst, size_t n, const char *src)
{
	size_t l = strlen(src);

	if (l >= n) {
		l = n - 1;
		/* Not halfway through a character. */
		while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) l--;
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

/* ---- the reader --------------------------------------------------------------------- */

typedef struct {
	const char *p, *end;
} xr;

/* One code point into UTF-8 at `o` (room for 4). Returns bytes written. */
static int utf8(unsigned long c, char *o)
{
	if (c < 0x80)    { o[0] = (char)c; return 1; }
	if (c < 0x800)   { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63));
	                   o[2] = (char)(0x80 | (c & 63)); return 3; }
	if (c < 0x110000) { o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63));
	                    o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63)); return 4; }
	o[0] = '?';
	return 1;
}

/* Append s[0..n) to out (size cap, length *ol), entities decoded unless `raw`. */
static void put(char *out, size_t cap, size_t *ol, const char *s, size_t n, bool raw)
{
	size_t i = 0;

	while (i < n && *ol + 5 < cap) {
		if (!raw && s[i] == '&') {
			const char *semi = memchr(s + i, ';', n - i < 12 ? n - i : 12);
			if (semi) {
				const char *e = s + i + 1;
				size_t el = (size_t)(semi - e);
				char ch = 0;

				if (el && e[0] == '#') {
					unsigned long c = e[1] == 'x' || e[1] == 'X' ? strtoul(e + 2, NULL, 16)
					                                             : strtoul(e + 1, NULL, 10);
					if (c) { *ol += (size_t)utf8(c, out + *ol); i = (size_t)(semi - s) + 1; continue; }
				}
				else if (el == 3 && !memcmp(e, "amp", 3))  ch = '&';
				else if (el == 2 && !memcmp(e, "lt", 2))   ch = '<';
				else if (el == 2 && !memcmp(e, "gt", 2))   ch = '>';
				else if (el == 4 && !memcmp(e, "quot", 4)) ch = '"';
				else if (el == 4 && !memcmp(e, "apos", 4)) ch = '\'';
				if (ch) { out[(*ol)++] = ch; i = (size_t)(semi - s) + 1; continue; }
			}
		}
		out[(*ol)++] = s[i++];
	}
	out[*ol] = '\0';
}

/* Whitespace (tabs and newlines too) folded to single spaces, trimmed. */
static void tidy(char *s)
{
	char *r = s, *w = s;
	bool sp = true;

	for (; *r; r++) {
		unsigned char c = (unsigned char)*r;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (!sp) *w++ = ' ';
			sp = true;
		} else {
			*w++ = (char)c;
			sp = false;
		}
	}
	if (w > s && w[-1] == ' ') w--;
	*w = '\0';
}

static const char *find(const char *p, const char *end, const char *needle)
{
	size_t n = strlen(needle);

	for (; p + n <= end; p++)
		if (*p == needle[0] && !memcmp(p, needle, n)) return p;
	return NULL;
}

/* Attribute `name` of a start tag's attribute text [a, b): its value, decoded. */
static bool attr(const char *a, const char *b, const char *name, char *out, size_t cap)
{
	size_t nl = strlen(name);

	out[0] = '\0';
	while (a < b) {
		const char *k, *v;
		char q;
		size_t kl;

		while (a < b && isspace((unsigned char)*a)) a++;
		k = a;
		while (a < b && *a != '=' && !isspace((unsigned char)*a) && *a != '/') a++;
		kl = (size_t)(a - k);
		while (a < b && isspace((unsigned char)*a)) a++;
		if (a >= b || *a != '=') { if (a < b) a++; continue; }
		a++;
		while (a < b && isspace((unsigned char)*a)) a++;
		if (a >= b || (*a != '"' && *a != '\'')) continue;
		q = *a++;
		v = a;
		while (a < b && *a != q) a++;
		if (kl == nl && !strncasecmp(k, name, nl)) {
			size_t ol = 0;
			put(out, cap, &ol, v, (size_t)(a - v), false);
			tidy(out);
			return true;
		}
		if (a < b) a++;
	}
	return false;
}

/* The next piece: a start tag (kind 1, `self` when it closes itself), an end
 * tag (2) or text (3). Name in `name`, the attribute text or the text in
 * [*a, *b). CDATA arrives as text with `raw` set. 0 at the end. */
static int next(xr *x, char *name, size_t nn, const char **a, const char **b, bool *self, bool *raw)
{
	for (;;) {
		const char *p = x->p, *e;

		if (p >= x->end) return 0;
		*raw = false;
		if (*p != '<') {
			e = memchr(p, '<', (size_t)(x->end - p));
			if (!e) e = x->end;
			*a = p; *b = e;
			x->p = e;
			return 3;
		}
		if (x->end - p >= 4 && !memcmp(p, "<!--", 4)) {
			e = find(p + 4, x->end, "-->");
			x->p = e ? e + 3 : x->end;
			continue;
		}
		if (x->end - p >= 9 && !memcmp(p, "<![CDATA[", 9)) {
			e = find(p + 9, x->end, "]]>");
			*a = p + 9; *b = e ? e : x->end;
			*raw = true;
			x->p = e ? e + 3 : x->end;
			return 3;
		}
		if (x->end - p >= 2 && (p[1] == '?' || p[1] == '!')) {
			e = memchr(p, '>', (size_t)(x->end - p));
			x->p = e ? e + 1 : x->end;
			continue;
		}
		{
			bool end_tag = x->end - p >= 2 && p[1] == '/';
			const char *s = p + (end_tag ? 2 : 1), *t = s;
			size_t l;
			char q = 0;

			while (t < x->end && !isspace((unsigned char)*t) && *t != '>' && *t != '/') t++;
			l = (size_t)(t - s);
			if (l >= nn) l = nn - 1;
			memcpy(name, s, l);
			name[l] = '\0';
			/* The tag's end, minding quoted attribute values. */
			e = t;
			while (e < x->end && (q || *e != '>')) {
				if (q) { if (*e == q) q = 0; }
				else if (*e == '"' || *e == '\'') q = *e;
				e++;
			}
			if (e >= x->end) { x->p = x->end; return 0; }
			*a = t;
			*b = e;
			*self = e > t && e[-1] == '/';
			if (*self) (*b)--;
			x->p = e + 1;
			return end_tag ? 2 : 1;
		}
	}
}

/* ---- dates and durations ------------------------------------------------------------------ */

static long long days_from_civil(long long y, unsigned m, unsigned d)
{
	long long era;
	unsigned yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = (unsigned)(y - era * 400);
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long long)doe - 719468;
}

static long long civil(int y, int mo, int d, int h, int mi, int s)
{
	if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	return days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s;
}

static int zone(const char *z)
{
	static const struct { const char *n; int h; } Z[] = {
		{ "GMT", 0 }, { "UT", 0 }, { "UTC", 0 }, { "Z", 0 },
		{ "EST", -5 }, { "EDT", -4 }, { "CST", -6 }, { "CDT", -5 },
		{ "MST", -7 }, { "MDT", -6 }, { "PST", -8 }, { "PDT", -7 },
	};
	size_t i;

	while (*z == ' ') z++;
	if (*z == '+' || *z == '-') {
		int sign = *z == '-' ? -1 : 1, hh = 0, mm = 0;

		z++;
		if (isdigit((unsigned char)z[0]) && isdigit((unsigned char)z[1])) {
			hh = (z[0] - '0') * 10 + (z[1] - '0');
			z += 2;
			if (*z == ':') z++;
			if (isdigit((unsigned char)z[0]) && isdigit((unsigned char)z[1]))
				mm = (z[0] - '0') * 10 + (z[1] - '0');
		}
		return sign * (hh * 3600 + mm * 60);
	}
	for (i = 0; i < sizeof Z / sizeof Z[0]; i++)
		if (!strncasecmp(z, Z[i].n, strlen(Z[i].n))) return Z[i].h * 3600;
	return 0;
}

long long pf_date(const char *s)
{
	static const char M[] = "janfebmaraprmayjunjulaugsepoctnovdec";
	int y, mo = 0, d, h = 0, mi = 0, sec = 0, n = 0;
	long long t;

	if (!s) return 0;
	while (isspace((unsigned char)*s)) s++;
	/* ISO 8601: 2025-11-18[T08:00[:00][.123]][Z|+hh:mm] */
	if (isdigit((unsigned char)s[0]) && s[4] == '-') {
		if (sscanf(s, "%d-%d-%d%n", &y, &mo, &d, &n) < 3) return 0;
		s += n;
		if (*s == 'T' || *s == ' ') {
			n = 0;
			if (sscanf(s + 1, "%d:%d%n", &h, &mi, &n) >= 2) {
				s += 1 + n;
				if (*s == ':') { n = 0; sscanf(s + 1, "%d%n", &sec, &n); s += 1 + n; }
				while (*s == '.' || isdigit((unsigned char)*s)) s++;
			}
		}
		t = civil(y, mo, d, h, mi, sec);
		return t ? t - zone(s) : 0;
	}
	/* RFC 822: [Day,] 18 Nov 2025 08:00[:00] zone */
	if (isalpha((unsigned char)*s)) {
		while (*s && *s != ',' && !isdigit((unsigned char)*s)) s++;
		if (*s == ',') s++;
	}
	while (isspace((unsigned char)*s)) s++;
	{
		char mon[4] = "";
		const char *f;

		if (sscanf(s, "%d %3s %d%n", &d, mon, &y, &n) < 3) return 0;
		for (int k = 0; mon[k]; k++) mon[k] = (char)tolower((unsigned char)mon[k]);
		if (strlen(mon) != 3 || !(f = strstr(M, mon)) || (f - M) % 3) return 0;
		mo = (int)(f - M) / 3 + 1;
		if (y < 100) y += y < 70 ? 2000 : 1900;
		s += n;
		n = 0;
		if (sscanf(s, " %d:%d%n", &h, &mi, &n) >= 2) {
			s += n;
			if (*s == ':') { n = 0; sscanf(s + 1, "%d%n", &sec, &n); s += 1 + n; }
		}
	}
	t = civil(y, mo, d, h, mi, sec);
	return t ? t - zone(s) : 0;
}

int pf_duration(const char *s)
{
	long v[3] = { 0, 0, 0 };
	int n = 0;

	if (!s) return 0;
	while (isspace((unsigned char)*s)) s++;
	while (*s && n < 3) {
		char *e;

		if (!isdigit((unsigned char)*s)) break;
		v[n++] = strtol(s, &e, 10);
		s = e;
		if (*s == '.') { s++; while (isdigit((unsigned char)*s)) s++; }
		if (*s != ':') break;
		s++;
	}
	if (n == 1) return (int)v[0];
	if (n == 2) return (int)(v[0] * 60 + v[1]);
	if (n == 3) return (int)(v[0] * 3600 + v[1] * 60 + v[2]);
	return 0;
}

const char *pf_ext(const char *type, const char *url)
{
	static const struct { const char *t, *x; } T[] = {
		{ "audio/mpeg", ".mp3" }, { "audio/mp3", ".mp3" }, { "audio/x-mp3", ".mp3" },
		{ "audio/x-m4a", ".m4a" }, { "audio/mp4", ".m4a" }, { "audio/m4a", ".m4a" },
		{ "audio/aac", ".aac" }, { "audio/x-aac", ".aac" },
		{ "audio/ogg", ".ogg" }, { "audio/vorbis", ".ogg" }, { "audio/opus", ".opus" },
		{ "audio/flac", ".flac" }, { "audio/x-flac", ".flac" },
		{ "audio/wav", ".wav" }, { "audio/x-wav", ".wav" },
	};
	static const char *const X[] = { ".mp3", ".m4a", ".aac", ".ogg", ".oga", ".opus", ".flac", ".wav", ".m4b" };
	size_t i;

	for (i = 0; type && i < sizeof T / sizeof T[0]; i++)
		if (!strncasecmp(type, T[i].t, strlen(T[i].t))) return T[i].x;
	if (url) {
		const char *q = strpbrk(url, "?#"), *e = q ? q : url + strlen(url), *dot = e;

		while (dot > url && *dot != '.' && *dot != '/') dot--;
		if (*dot == '.')
			for (i = 0; i < sizeof X / sizeof X[0]; i++)
				if ((size_t)(e - dot) == strlen(X[i]) && !strncasecmp(dot, X[i], strlen(X[i])))
					return !strcmp(X[i], ".oga") ? ".ogg" : X[i];
	}
	return ".mp3";
}

/* ---- feeds ------------------------------------------------------------------------------ */

/* An element's local name, and whether its prefix is one feeds use for what
 * is read here ("" or itunes or atom). */
static const char *local(const char *name, bool *itunes, bool *other)
{
	const char *c = strchr(name, ':');

	*itunes = c && (size_t)(c - name) == 6 && !strncasecmp(name, "itunes", 6);
	*other = c && !*itunes && !((size_t)(c - name) == 4 && !strncasecmp(name, "atom", 4));
	return c ? c + 1 : name;
}

enum { F_NONE, F_TITLE, F_GUID, F_DATE, F_DUR, F_AUTHOR, F_IMGURL };

static pf_item *item_add(pf_feed *f)
{
	if (f->n == f->cap) {
		int cap = f->cap ? f->cap * 2 : 64;
		pf_item *n = realloc(f->items, sizeof *n * (size_t)cap);

		if (!n) return NULL;
		f->items = n;
		f->cap = cap;
	}
	memset(&f->items[f->n], 0, sizeof f->items[0]);
	return &f->items[f->n];
}

bool pf_parse(const char *xml, size_t len, pf_feed *out)
{
	xr x = { xml, xml + len };
	char name[64], text[1024], av[1024];
	const char *a, *b;
	bool self, raw, feed = false, in_item = false, in_image = false;
	int field = F_NONE, depth_item = 0, depth = 0;
	size_t tl = 0;
	pf_item cur;
	int k;

	memset(out, 0, sizeof *out);
	memset(&cur, 0, sizeof cur);
	text[0] = '\0';
	while ((k = next(&x, name, sizeof name, &a, &b, &self, &raw))) {
		bool it, other;
		const char *ln = local(name, &it, &other);

		if (k == 3) {
			if (field != F_NONE) put(text, sizeof text, &tl, a, (size_t)(b - a), raw);
			continue;
		}
		if (k == 1) {
			if (!self) depth++;
			if (!strcasecmp(ln, "rss") || !strcasecmp(ln, "channel") || (!strcasecmp(ln, "feed") && !other)) feed = true;
			if (other) continue;
			if (!strcasecmp(ln, "item") || (!strcasecmp(ln, "entry") && !it)) {
				in_item = true;
				depth_item = depth;
				memset(&cur, 0, sizeof cur);
				continue;
			}
			if (!in_item && !it && !strcasecmp(ln, "image")) { if (!self) in_image = true; continue; }
			if (it && !strcasecmp(ln, "image")) {
				if (!in_item && attr(a, b, "href", av, sizeof av) && av[0])
					cpy(out->image, sizeof out->image, av);
				continue;
			}
			if (!it && !strcasecmp(ln, "enclosure") && in_item) {
				attr(a, b, "type", av, sizeof av);
				if (!strncasecmp(av, "video/", 6)) continue;
				cpy(cur.type, sizeof cur.type, av);
				attr(a, b, "url", cur.url, sizeof cur.url);
				if (attr(a, b, "length", av, sizeof av)) cur.len = atoll(av);
				continue;
			}
			/* Atom: <link rel="enclosure" href=... type=... length=...> */
			if (!it && !strcasecmp(ln, "link") && in_item && attr(a, b, "rel", av, sizeof av) &&
			    !strcasecmp(av, "enclosure")) {
				attr(a, b, "type", av, sizeof av);
				if (!strncasecmp(av, "video/", 6) || cur.url[0]) continue;
				cpy(cur.type, sizeof cur.type, av);
				attr(a, b, "href", cur.url, sizeof cur.url);
				if (attr(a, b, "length", av, sizeof av)) cur.len = atoll(av);
				continue;
			}
			if (self) continue;
			field = F_NONE;
			if (!strcasecmp(ln, "title") && !it) field = F_TITLE;
			else if (in_item && !it && (!strcasecmp(ln, "guid") || !strcasecmp(ln, "id"))) field = F_GUID;
			else if (in_item && !it && (!strcasecmp(ln, "pubDate") || !strcasecmp(ln, "published") ||
			                            (!strcasecmp(ln, "updated") && !cur.stamp))) field = F_DATE;
			else if (in_item && it && !strcasecmp(ln, "duration")) field = F_DUR;
			else if (!in_item && it && !strcasecmp(ln, "author")) field = F_AUTHOR;
			else if (in_image && !strcasecmp(ln, "url")) field = F_IMGURL;
			tl = 0;
			text[0] = '\0';
			continue;
		}
		/* An end tag. */
		if (field != F_NONE) {
			tidy(text);
			switch (field) {
			case F_TITLE:
				if (in_item) { if (!cur.title[0]) cpy(cur.title, sizeof cur.title, text); }
				else if (!in_image && !out->title[0]) cpy(out->title, sizeof out->title, text);
				break;
			case F_GUID:   if (!cur.guid[0]) cpy(cur.guid, sizeof cur.guid, text); break;
			case F_DATE:   if (!cur.stamp) cur.stamp = pf_date(text); break;
			case F_DUR:    cur.dur = pf_duration(text); break;
			case F_AUTHOR: cpy(out->author, sizeof out->author, text); break;
			case F_IMGURL: if (!out->image[0]) cpy(out->image, sizeof out->image, text); break;
			}
			field = F_NONE;
		}
		if (!other && in_item && depth == depth_item &&
		    (!strcasecmp(ln, "item") || (!strcasecmp(ln, "entry") && !it))) {
			pf_item *n;

			in_item = false;
			if (cur.url[0] && (n = item_add(out))) { *n = cur; out->n++; }
		}
		if (!other && !it && !in_item && !strcasecmp(ln, "image")) in_image = false;
		if (depth > 0) depth--;
	}
	return feed;
}

void pf_free(pf_feed *f)
{
	free(f->items);
	memset(f, 0, sizeof *f);
}

int pf_opml(const char *xml, size_t len, pf_outline *out, int max)
{
	xr x = { xml, xml + len };
	char name[64];
	const char *a, *b;
	bool self, raw, opml = false;
	int k, n = 0;

	while ((k = next(&x, name, sizeof name, &a, &b, &self, &raw))) {
		if (k != 1) continue;
		if (!strcasecmp(name, "opml")) opml = true;
		if (strcasecmp(name, "outline") || n >= max) continue;
		if (!attr(a, b, "xmlUrl", out[n].url, sizeof out[n].url) || !out[n].url[0]) continue;
		if (!attr(a, b, "title", out[n].title, sizeof out[n].title) || !out[n].title[0])
			attr(a, b, "text", out[n].title, sizeof out[n].title);
		n++;
	}
	return opml ? n : -1;
}
