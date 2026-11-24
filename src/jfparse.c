/* SPDX-License-Identifier: MIT */
/* See jfparse.h. */
#include "jfparse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void jstr(jsv o, const char *k, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	if (js_member(o, k, &v)) js_str(v, out, n);
}

static long jint(jsv o, const char *k)
{
	jsv v;

	return js_member(o, k, &v) ? js_int(v) : 0;
}

/* Tabs and newlines are the index's structure. */
static void clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

void jf_ext(const char *container, char *out, size_t n)
{
	static const char *const KNOWN[] = {
		"flac", "mp3", "m4a", "opus", "ogg", "oga", "wav", "aac", "wma", "ape", "wv", "alac", "aiff", "aif", NULL
	};
	const char *s = container ? container : "";

	while (*s) {
		char tok[16];
		size_t l = strcspn(s, ",");
		int i;

		if (l < sizeof tok) {
			for (i = 0; i < (int)l; i++) tok[i] = (char)tolower((unsigned char)s[i]);
			tok[l] = '\0';
			for (i = 0; KNOWN[i]; i++)
				if (!strcmp(tok, KNOWN[i])) { snprintf(out, n, "%s", tok); return; }
		}
		s += l;
		if (*s == ',') s++;
	}
	snprintf(out, n, "flac");
}

void jf_iso(long t, char *out, size_t n)
{
	time_t tt = (time_t)t;
	struct tm tm;

	gmtime_r(&tt, &tm);
	strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

int jf_fav_parse(jsv item, char *id, size_t n)
{
	char type[32];

	jstr(item, "Id", id, n);
	jstr(item, "Type", type, sizeof type);
	if (!id[0]) return 0;
	return !strcmp(type, "Audio") ? 1 : !strcmp(type, "MusicAlbum") ? 2 : 0;
}

bool jf_track_parse(jsv item, jf_track *t)
{
	jsv v, it = { 0 }, e;
	char tag[64], cont[64];
	long ticks;

	memset(t, 0, sizeof *t);
	jstr(item, "Id", t->id, sizeof t->id);
	jstr(item, "AlbumId", t->album_id, sizeof t->album_id);
	if (!t->id[0] || !t->album_id[0]) return false;
	jstr(item, "Name", t->title, sizeof t->title);
	jstr(item, "Album", t->album, sizeof t->album);
	jstr(item, "AlbumArtist", t->artist, sizeof t->artist);
	if (js_member(item, "AlbumArtists", &v) && js_next(v, &it, &e)) {
		char id[40];

		jstr(e, "Id", id, sizeof id);
		if (!t->artist[0]) jstr(e, "Name", t->artist, sizeof t->artist);
		if (id[0]) snprintf(t->artist_id, sizeof t->artist_id, "%s", id);
	}
	/* No album artist at all: the track's first artist, as the shelf shows it. */
	if (!t->artist[0] && js_member(item, "Artists", &v)) {
		memset(&it, 0, sizeof it);
		if (js_next(v, &it, &e)) js_str(e, t->artist, sizeof t->artist);
	}
	if (!t->artist[0]) snprintf(t->artist, sizeof t->artist, "Unknown Artist");
	if (!t->album[0]) snprintf(t->album, sizeof t->album, "Unknown Album");
	if (!t->title[0]) snprintf(t->title, sizeof t->title, "Unknown");
	if (!t->artist_id[0]) snprintf(t->artist_id, sizeof t->artist_id, "n:%.190s", t->artist);
	/* The track's own artists, when they are not just the album's. */
	if (js_member(item, "Artists", &v)) {
		size_t o = 0;

		memset(&it, 0, sizeof it);
		while (js_next(v, &it, &e) && o + 4 < sizeof t->tartist) {
			char a[200];

			js_str(e, a, sizeof a);
			if (!a[0]) continue;
			o += (size_t)snprintf(t->tartist + o, sizeof t->tartist - o, "%s%s", o ? ", " : "", a);
		}
		if (!strcmp(t->tartist, t->artist)) t->tartist[0] = '\0';
	}
	t->year = (int)jint(item, "ProductionYear");
	t->disc = (int)jint(item, "ParentIndexNumber");
	t->idx = (int)jint(item, "IndexNumber");
	ticks = js_member(item, "RunTimeTicks", &v) ? (long)js_num(v) : 0;
	t->dur_ms = (int)(ticks / 10000);
	jstr(item, "AlbumPrimaryImageTag", tag, sizeof tag);
	if (tag[0]) snprintf(t->thumb, sizeof t->thumb, "%s/%s", t->album_id, tag);
	jstr(item, "Container", cont, sizeof cont);
	jf_ext(cont, t->ext, sizeof t->ext);
	clean(t->title); clean(t->album); clean(t->artist); clean(t->tartist); clean(t->artist_id);
	return true;
}

/* ---- ids to keys ---- */

struct jf_keys {
	char path[700];
	char **ids;       /* by key; ids[0] unused */
	int n, cap;       /* the highest key, and room */
	int *hash; int hcap;
};

static unsigned hstr(const char *s)
{
	unsigned h = 2166136261u;

	for (; *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

static bool rehash(jf_keys *k, int hcap)
{
	int *h = malloc(sizeof *h * (size_t)hcap), i;

	if (!h) return false;
	for (i = 0; i < hcap; i++) h[i] = 0;
	for (i = 1; i <= k->n; i++) {
		unsigned x;

		if (!k->ids[i]) continue;
		for (x = hstr(k->ids[i]) % (unsigned)hcap; h[x]; x = (x + 1) % (unsigned)hcap) {}
		h[x] = i;
	}
	free(k->hash);
	k->hash = h;
	k->hcap = hcap;
	return true;
}

static bool put(jf_keys *k, int key, const char *id)
{
	unsigned x;

	if (key >= k->cap) {
		int cap = k->cap ? k->cap : 1024;
		char **ids;

		while (cap <= key) cap *= 2;
		if (!(ids = realloc(k->ids, sizeof *ids * (size_t)cap))) return false;
		memset(ids + k->cap, 0, sizeof *ids * (size_t)(cap - k->cap));
		k->ids = ids;
		k->cap = cap;
	}
	if (k->ids[key] || !(k->ids[key] = strdup(id))) return false;
	if (key > k->n) k->n = key;
	if ((k->n + 1) * 2 > k->hcap) return rehash(k, (k->n + 1) * 4 + 1);   /* places it too */
	for (x = hstr(id) % (unsigned)k->hcap; k->hash[x]; x = (x + 1) % (unsigned)k->hcap) {}
	k->hash[x] = key;
	return true;
}

jf_keys *jf_keys_open(const char *path)
{
	jf_keys *k = calloc(1, sizeof *k);
	char line[300];
	FILE *f;

	if (!k) return NULL;
	snprintf(k->path, sizeof k->path, "%s", path);
	if (!rehash(k, 2049)) { free(k); return NULL; }
	if ((f = fopen(path, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char *tab = strchr(line, '\t');
			int key = atoi(line);

			line[strcspn(line, "\r\n")] = '\0';
			if (!tab || key <= 0 || !tab[1]) continue;
			put(k, key, tab + 1);              /* a repeat (a torn write) is skipped */
		}
		fclose(f);
	}
	return k;
}

void jf_keys_free(jf_keys *k)
{
	int i;

	if (!k) return;
	for (i = 0; i < k->cap; i++) free(k->ids[i]);
	free(k->ids);
	free(k->hash);
	free(k);
}

int jf_key_find(jf_keys *k, const char *id)
{
	unsigned x;

	if (!k || !id || !id[0]) return 0;
	for (x = hstr(id) % (unsigned)k->hcap; k->hash[x]; x = (x + 1) % (unsigned)k->hcap)
		if (!strcmp(k->ids[k->hash[x]], id)) return k->hash[x];
	return 0;
}

int jf_key(jf_keys *k, const char *id)
{
	int key = jf_key_find(k, id);
	FILE *f;

	if (key || !k || !id || !id[0] || strchr(id, '\n') || strchr(id, '\t')) return key;
	key = k->n + 1;
	if (!(f = fopen(k->path, "a"))) return 0;
	fprintf(f, "%d\t%s\n", key, id);
	if (fclose(f) != 0 || !put(k, key, id)) return 0;
	return key;
}

const char *jf_key_id(jf_keys *k, int key)
{
	return k && key > 0 && key <= k->n ? k->ids[key] : NULL;
}
