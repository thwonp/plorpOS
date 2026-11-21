/* SPDX-License-Identifier: MIT */
/* See podcast.h. */
#include "podcast.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "net.h"
#include "podfeed.h"

#define LANDED 32

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_mu)
#define UNLOCK() pthread_mutex_unlock(&g_mu)

static char g_card[512], g_dir[600];     /* <shared>/podcasts */
static char g_rel_root[256];             /* the same, card-relative */
static void (*g_shrink)(const char *path);

/* ---- small helpers ---------------------------------------------------------------- */

static void mkdirs(const char *path)
{
	char p[1024];
	char *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *b;
	long n;

	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0 || !(b = malloc((size_t)n + 1))) { fclose(f); return NULL; }
	if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
	b[n] = '\0';
	fclose(f);
	if (len) *len = (size_t)n;
	return b;
}

/* Into a field of a .tsv: no tabs or line breaks. */
static void clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static void cpy(char *dst, size_t n, const char *src)
{
	size_t l = strlen(src);

	if (l >= n) {
		l = n - 1;
		while (l > 0 && ((unsigned char)src[l] & 0xC0) == 0x80) l--;
	}
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static unsigned long long fnv(const char *a, const char *b)
{
	unsigned long long h = 1469598103934665603ULL;

	for (; a && *a; a++) { h ^= (unsigned char)*a; h *= 1099511628211ULL; }
	if (b) { h ^= '|'; h *= 1099511628211ULL; }
	for (; b && *b; b++) { h ^= (unsigned char)*b; h *= 1099511628211ULL; }
	return h;
}

static int split(char *s, char **f, int max)
{
	int n = 0;

	f[n++] = s;
	for (; *s && n < max; s++)
		if (*s == '\t') { *s = '\0'; f[n++] = s + 1; }
	return n;
}

/* Written whole or not at all: <path>.new, then renamed over. */
static bool write_file(const char *path, const char *data, size_t len)
{
	char tmp[1100];
	FILE *f;
	bool ok;

	snprintf(tmp, sizeof tmp, "%s.new", path);
	if (!(f = fopen(tmp, "wb"))) return false;
	ok = fwrite(data, 1, len, f) == len;
	ok = fclose(f) == 0 && ok;
	if (ok && rename(tmp, path) == 0) return true;
	unlink(tmp);
	return false;
}

/* ---- the shows (subs.tsv) ---------------------------------------------------------- */

typedef struct {
	char sid[12];
	char url[1024], title[256], image[1024];
	long long added;
} pd_sub;

static pd_sub *g_subs;          /* under g_mu */
static int g_nsubs, g_csubs;

static void subs_path(char *out, size_t n) { snprintf(out, n, "%s/subs.tsv", g_dir); }

static void subs_load(void)
{
	char p[700], line[2600], *f[5];
	FILE *fp;

	g_nsubs = 0;
	subs_path(p, sizeof p);
	if (!(fp = fopen(p, "r"))) return;
	while (fgets(line, sizeof line, fp)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (split(line, f, 5) < 5 || !f[0][0] || !f[1][0]) continue;
		if (g_nsubs == g_csubs) {
			int c = g_csubs ? g_csubs * 2 : 32;
			pd_sub *n = realloc(g_subs, sizeof *n * (size_t)c);
			if (!n) break;
			g_subs = n;
			g_csubs = c;
		}
		pd_sub *s = &g_subs[g_nsubs++];
		cpy(s->sid, sizeof s->sid, f[0]);
		cpy(s->url, sizeof s->url, f[1]);
		cpy(s->title, sizeof s->title, f[2]);
		cpy(s->image, sizeof s->image, f[3]);
		s->added = atoll(f[4]);
	}
	fclose(fp);
}

/* Under g_mu. */
static bool subs_save_locked(void)
{
	char p[700], *buf;
	size_t cap = 256 + (size_t)g_nsubs * 2400, len = 0;
	bool ok;
	int i;

	if (!(buf = malloc(cap))) return false;
	for (i = 0; i < g_nsubs; i++)
		len += (size_t)snprintf(buf + len, cap - len, "%s\t%s\t%s\t%s\t%lld\n", g_subs[i].sid,
		                        g_subs[i].url, g_subs[i].title, g_subs[i].image, g_subs[i].added);
	mkdirs(g_dir);
	subs_path(p, sizeof p);
	ok = write_file(p, buf, len);
	free(buf);
	return ok;
}

static int sub_find(const char *sid)
{
	for (int i = 0; i < g_nsubs; i++) if (!strcmp(g_subs[i].sid, sid)) return i;
	return -1;
}

int pod_show_count(void)
{
	int n;

	LOCK(); n = g_nsubs; UNLOCK();
	return n;
}

int pod_subscribe(const char *feed_url, const char *title, const char *image)
{
	char sid[12];
	int r = 1;

	if (!feed_url || !feed_url[0]) return -1;
	snprintf(sid, sizeof sid, "%08llx", fnv(feed_url, NULL) & 0xffffffffULL);
	LOCK();
	if (sub_find(sid) >= 0) { UNLOCK(); return 0; }
	if (g_nsubs == g_csubs) {
		int c = g_csubs ? g_csubs * 2 : 32;
		pd_sub *n = realloc(g_subs, sizeof *n * (size_t)c);
		if (!n) { UNLOCK(); return -1; }
		g_subs = n;
		g_csubs = c;
	}
	pd_sub *s = &g_subs[g_nsubs++];
	memset(s, 0, sizeof *s);
	cpy(s->sid, sizeof s->sid, sid);
	cpy(s->url, sizeof s->url, feed_url);
	cpy(s->title, sizeof s->title, title ? title : "");
	cpy(s->image, sizeof s->image, image ? image : "");
	clean(s->url); clean(s->title); clean(s->image);
	s->added = (long long)time(NULL);
	if (!subs_save_locked()) { g_nsubs--; r = -1; }
	UNLOCK();
	if (r > 0) fprintf(stderr, "pod: subscribed to %s\n", title && title[0] ? title : feed_url);
	return r;
}

int pod_import_opml(const char *path, int *had)
{
	enum { MAX = 512 };
	pf_outline *o;
	size_t len;
	char *b = slurp(path, &len);
	int n, i, added = 0;

	*had = 0;
	if (!b) return -1;
	if (!(o = malloc(sizeof *o * MAX))) { free(b); return -1; }
	n = pf_opml(b, len, o, MAX);
	free(b);
	for (i = 0; i < n; i++) {
		int r = pod_subscribe(o[i].url, o[i].title, "");

		if (r > 0) added++;
		else if (r == 0) (*had)++;
	}
	free(o);
	if (n >= 0) fprintf(stderr, "pod: imported %s: %d added, %d already\n", path, added, *had);
	return n < 0 ? -1 : added;
}

/* ---- jobs ------------------------------------------------------------------------------ */

enum { J_SYNC, J_COVER };
typedef struct job {
	int kind;
	char a[1100], b[700];
	struct job *next;
} job;

static job *g_jobs;

static void push(job *j, bool front)
{
	LOCK();
	if (front || !g_jobs) {
		j->next = g_jobs;
		g_jobs = j;
	} else {
		job *t = g_jobs;
		while (t->next) t = t->next;
		t->next = j;
	}
	pthread_cond_signal(&g_cv);
	UNLOCK();
}

static void tmp_for(const char *tag, char *out, size_t n)
{
	static unsigned seq;
	unsigned k;

	LOCK(); k = ++seq; UNLOCK();
	snprintf(out, n, "/tmp/tortos-pod-%s-%ld-%u", tag, (long)getpid(), k);
}

/* ---- the index (<show>/eps.tsv) ---------------------------------------------------------- *
 *
 * "<episode id>\t<published>\t<seconds>\t<ext>\t<enclosure url>\t<title>", newest first. */

static void eps_path(const char *sid, char *out, size_t n) { snprintf(out, n, "%s/%s/eps.tsv", g_dir, sid); }

/* The ids an index holds, for "what is new". Returns how many (in *ids, a
 * block of 16-byte strings the caller frees). */
static int eps_ids(const char *sid, char (**ids)[16])
{
	char p[700], *b, *s;
	int n = 0, cap = 0;

	*ids = NULL;
	eps_path(sid, p, sizeof p);
	if (!(b = slurp(p, NULL))) return 0;
	for (s = b; *s; ) {
		char *e = strchr(s, '\n'), *t = strchr(s, '\t');

		if (t && (!e || t < e) && t - s < 16) {
			if (n == cap) {
				char (*m)[16] = realloc(*ids, 16 * (size_t)(cap = cap ? cap * 2 : 256));
				if (!m) break;
				*ids = m;
			}
			memcpy((*ids)[n], s, (size_t)(t - s));
			(*ids)[n++][t - s] = '\0';
		}
		if (!e) break;
		s = e + 1;
	}
	free(b);
	return n;
}

static bool has_id(char (*ids)[16], int n, const char *id)
{
	for (int i = 0; i < n; i++) if (!strcmp(ids[i], id)) return true;
	return false;
}

/* ---- syncing ------------------------------------------------------------------------------ */

static volatile int g_sync_run, g_sync_failed, g_sync_done, g_sync_total, g_sync_new, g_sync_added;

static int cmp_item(const void *pa, const void *pb)
{
	const pf_item *a = pa, *b = pb;

	return a->stamp < b->stamp ? 1 : a->stamp > b->stamp ? -1 : 0;
}

/* One show: its feed fetched and its index written. -1 failed, else how many
 * episodes are new to it (0 on a first fetch: nothing is "new" yet). */
static int sync_one(int k)
{
	char url[1024], sid[12], tmp[128], p[700], *body, (*old)[16];
	size_t len, cap, out = 0;
	pf_feed f;
	int code, i, nold, added = 0;
	char *buf;

	LOCK();
	cpy(url, sizeof url, g_subs[k].url);
	cpy(sid, sizeof sid, g_subs[k].sid);
	UNLOCK();
	tmp_for("feed", tmp, sizeof tmp);
	code = net_fetch(url, "GET", NULL, 0, tmp, 10, 60, NULL);
	if (code != 200 || !(body = slurp(tmp, &len))) {
		unlink(tmp);
		fprintf(stderr, "pod: feed %s: HTTP %d\n", sid, code);
		return -1;
	}
	unlink(tmp);                        /* /tmp is RAM on the devices */
	if (!pf_parse(body, len, &f)) {
		free(body);
		fprintf(stderr, "pod: feed %s: not a feed\n", sid);
		return -1;
	}
	free(body);
	qsort(f.items, (size_t)f.n, sizeof *f.items, cmp_item);
	nold = eps_ids(sid, &old);
	cap = (size_t)f.n * 1600 + 64;
	if (!(buf = malloc(cap))) { pf_free(&f); free(old); return -1; }
	for (i = 0; i < f.n; i++) {
		pf_item *it = &f.items[i];
		char eid[16];

		snprintf(eid, sizeof eid, "%012llx", fnv(sid, it->guid[0] ? it->guid : it->url) & 0xffffffffffffULL);
		/* Two items with one guid (it happens): the first, the newest. */
		{
			bool dup = false;
			for (int j = 0; j < i && !dup; j++) {
				const pf_item *o = &f.items[j];
				dup = !strcmp(o->guid[0] ? o->guid : o->url, it->guid[0] ? it->guid : it->url);
			}
			if (dup) continue;
		}
		if (nold && !has_id(old, nold, eid)) added++;
		clean(it->title);
		clean(it->url);
		out += (size_t)snprintf(buf + out, cap - out, "%s\t%lld\t%d\t%s\t%s\t%s\n", eid, it->stamp,
		                        it->dur, pf_ext(it->type, it->url), it->url, it->title);
	}
	free(old);
	snprintf(p, sizeof p, "%s/%s", g_dir, sid);
	mkdirs(p);
	eps_path(sid, p, sizeof p);
	{
		size_t ol;
		char *was = slurp(p, &ol);
		bool same = was && ol == out && !memcmp(was, buf, out);

		free(was);
		if (!same) {
			if (!write_file(p, buf, out)) added = -1;
			else g_sync_new = 1;
		}
	}
	free(buf);
	/* The feed's own name and art over what the OPML or search said. */
	LOCK();
	if ((k = sub_find(sid)) >= 0) {
		bool ch = false;

		clean(f.title);
		clean(f.image);
		if (f.title[0] && strcmp(f.title, g_subs[k].title)) { cpy(g_subs[k].title, sizeof g_subs[k].title, f.title); ch = true; }
		if (f.image[0] && strcmp(f.image, g_subs[k].image)) { cpy(g_subs[k].image, sizeof g_subs[k].image, f.image); ch = true; }
		if (ch) { subs_save_locked(); g_sync_new = 1; }
	}
	UNLOCK();
	pf_free(&f);
	return added;
}

static void do_sync(void)
{
	int k, n, ok = 0, added = 0;
	unsigned long t0 = (unsigned long)time(NULL);

	LOCK(); n = g_nsubs; UNLOCK();
	g_sync_total = n;
	for (k = 0; k < n; k++) {
		int r;

		g_sync_done = k;
		r = sync_one(k);
		if (r >= 0) { ok++; added += r; }
	}
	g_sync_done = n;
	g_sync_failed = n > 0 && ok == 0;
	g_sync_added += added;
	/* Lean on the card: one line per sync, not per show. */
	fprintf(stderr, "pod: synced %d of %d shows, %d new episodes in %lu s\n", ok, n, added,
	        (unsigned long)time(NULL) - t0);
}

void pod_sync_start(void)
{
	job *j;

	if (g_sync_run || !(j = calloc(1, sizeof *j))) return;
	g_sync_run = 1;
	g_sync_failed = 0;
	g_sync_done = 0;
	j->kind = J_SYNC;
	push(j, false);
}

int pod_sync_state(int *done, int *total)
{
	if (done) *done = g_sync_done;
	if (total) *total = g_sync_total;
	return g_sync_run ? 1 : g_sync_failed ? -1 : 0;
}

bool pod_sync_take(int *added)
{
	bool t;

	LOCK();
	t = g_sync_new && !g_sync_run;
	if (t) {
		g_sync_new = 0;
		if (added) *added = g_sync_added;
		g_sync_added = 0;
	}
	UNLOCK();
	return t;
}

/* ---- the library ---------------------------------------------------------------------------- */

typedef struct {
	char sid[12], eid[16], ext[8];
	long long stamp;
	int dur, show;
	char *url, *title;                   /* in the table's text block */
} pd_ep;

typedef struct {
	pd_ep *e; int n;
	char **text; int ntext;              /* the index files, kept for the strings */
	int *show_of_sub;                    /* artist index per g_subs entry, -1 none */
	int nshows;
	char (*show_sid)[12];                /* per artist */
} pd_table;

static pd_table g_tab;                   /* the installed library's, under g_mu */

static void tab_free(pd_table *t)
{
	for (int i = 0; i < t->ntext; i++) free(t->text[i]);
	free(t->text);
	free(t->e);
	free(t->show_of_sub);
	free(t->show_sid);
	memset(t, 0, sizeof *t);
}

bool pod_lib_cached(void) { return pod_show_count() > 0; }

bool pod_is_path(const char *rel)
{
	size_t n = strlen(g_rel_root);

	return rel && n && !strncmp(rel, g_rel_root, n) && rel[n] == '/';
}

static pd_sub *g_sorted_tmp;
static int cmp_sub_title(const void *pa, const void *pb)
{
	const int *a = pa, *b = pb;
	const char *ta = g_sorted_tmp[*a].title[0] ? g_sorted_tmp[*a].title : g_sorted_tmp[*a].url;
	const char *tb = g_sorted_tmp[*b].title[0] ? g_sorted_tmp[*b].title : g_sorted_tmp[*b].url;

	return strcasecmp(ta, tb);
}

bool pod_lib_load(ml_lib *out)
{
	pd_table t = { 0 };
	pd_sub *subs;
	int ns, i, *ord, cap = 0;

	memset(out, 0, sizeof *out);
	LOCK();
	ns = g_nsubs;
	subs = ns ? malloc(sizeof *subs * (size_t)ns) : NULL;
	if (subs) memcpy(subs, g_subs, sizeof *subs * (size_t)ns);
	UNLOCK();
	if (!ns || !subs) { free(subs); return false; }
	ord = malloc(sizeof *ord * (size_t)ns);
	t.text = calloc((size_t)ns, sizeof *t.text);
	t.show_of_sub = malloc(sizeof *t.show_of_sub * (size_t)ns);
	t.show_sid = calloc((size_t)ns, sizeof *t.show_sid);
	out->artists = calloc((size_t)ns, sizeof *out->artists);
	if (!ord || !t.text || !t.show_of_sub || !t.show_sid || !out->artists) goto fail;
	for (i = 0; i < ns; i++) { ord[i] = i; t.show_of_sub[i] = -1; }
	g_sorted_tmp = subs;
	qsort(ord, (size_t)ns, sizeof *ord, cmp_sub_title);
	for (i = 0; i < ns; i++) {
		pd_sub *s = &subs[ord[i]];
		char p[700], *b, *line;
		ml_artist *ar = &out->artists[t.nshows];

		eps_path(s->sid, p, sizeof p);
		b = slurp(p, NULL);
		cpy(ar->name, sizeof ar->name, s->title[0] ? s->title : s->url);
		ar->first = t.n;
		ar->book = true;
		cpy(t.show_sid[t.nshows], sizeof t.show_sid[0], s->sid);
		t.show_of_sub[ord[i]] = t.nshows;
		if (b) t.text[t.ntext++] = b;
		for (line = b; line && *line; ) {
			char *e = strchr(line, '\n'), *f[6];
			pd_ep *ep;

			if (e) *e = '\0';
			if (split(line, f, 6) == 6 && f[0][0] && f[4][0]) {
				if (t.n == cap) {
					pd_ep *m = realloc(t.e, sizeof *m * (size_t)(cap = cap ? cap * 2 : 1024));
					if (!m) { free(ord); goto fail; }
					t.e = m;
				}
				ep = &t.e[t.n++];
				memset(ep, 0, sizeof *ep);
				cpy(ep->sid, sizeof ep->sid, s->sid);
				cpy(ep->eid, sizeof ep->eid, f[0]);
				ep->stamp = atoll(f[1]);
				ep->dur = atoi(f[2]);
				cpy(ep->ext, sizeof ep->ext, f[3]);
				ep->url = f[4];
				ep->title = f[5];
				ep->show = t.nshows;
			}
			line = e ? e + 1 : NULL;
		}
		ar->n = t.n - ar->first;
		t.nshows++;
	}
	free(ord);
	free(subs);
	subs = NULL;
	out->nartists = t.nshows;
	out->albums = calloc((size_t)(t.n ? t.n : 1), sizeof *out->albums);
	out->tracks = calloc((size_t)(t.n ? t.n : 1), sizeof *out->tracks);
	if (!out->albums || !out->tracks) goto fail;
	for (i = 0; i < t.n; i++) {
		pd_ep *ep = &t.e[i];

		cpy(out->albums[i].name, sizeof out->albums[i].name, ep->title[0] ? ep->title : "Untitled");
		out->albums[i].first = i;
		out->albums[i].n = 1;
		out->albums[i].book = true;
		cpy(out->tracks[i].name, sizeof out->tracks[i].name, out->albums[i].name);
		snprintf(out->tracks[i].path, sizeof out->tracks[i].path, "%s/%s/%s/ep%s",
		         g_rel_root, ep->sid, ep->eid, ep->ext);
	}
	out->nalbums = out->ntracks = t.n;
	LOCK();
	tab_free(&g_tab);
	g_tab = t;
	UNLOCK();
	fprintf(stderr, "pod: %d shows, %d episodes from the index\n", t.nshows, t.n);
	return true;
fail:
	free(subs);
	tab_free(&t);
	ml_free(out);
	return false;
}

int pod_album_stamp(int al)
{
	long long s;

	LOCK();
	s = al >= 0 && al < g_tab.n ? g_tab.e[al].stamp : 0;
	UNLOCK();
	return s > 0 ? (int)(s / 60) : 0;
}

bool pod_episode(int al, pod_ep *out)
{
	bool ok;

	LOCK();
	if ((ok = al >= 0 && al < g_tab.n)) {
		out->stamp = g_tab.e[al].stamp;
		out->dur = g_tab.e[al].dur;
		out->show = g_tab.e[al].show;
	}
	UNLOCK();
	return ok;
}

/* ---- covers ---------------------------------------------------------------------------------- */

static char g_landed[LANDED][700];
static int g_nlanded;
/* Asked for or given up on this session: one fetch per show's art. */
static char (*g_asked)[700];
static int g_nasked, g_casked;

void pod_cover_base(int al, char *out, size_t n)
{
	LOCK();
	if (al >= 0 && al < g_tab.n) snprintf(out, n, "%s/%s/%s/.media/show", g_card, g_rel_root, g_tab.e[al].sid);
	else if (n) out[0] = '\0';
	UNLOCK();
}

static void do_cover(job *j)
{
	char part[720], dest[720], d[720], *s;
	unsigned char m[4] = { 0 };
	FILE *f;
	int code;

	snprintf(part, sizeof part, "%s.part", j->b);
	snprintf(d, sizeof d, "%s", j->b);
	if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	code = net_fetch(j->a, "GET", NULL, 0, part, 10, 60, NULL);
	if (code != 200) { unlink(part); fprintf(stderr, "pod: art HTTP %d\n", code); return; }
	if (g_shrink) g_shrink(part);        /* in place; PNG when it shrank */
	if ((f = fopen(part, "rb"))) { if (fread(m, 1, 4, f) != 4) m[0] = 0; fclose(f); }
	snprintf(dest, sizeof dest, "%s.%s", j->b, m[0] == 0x89 && m[1] == 'P' ? "png" : "jpg");
	if (rename(part, dest) != 0) { unlink(part); return; }
	LOCK();
	if (g_nlanded < LANDED) snprintf(g_landed[g_nlanded++], sizeof g_landed[0], "%s", j->b);
	UNLOCK();
}

bool pod_cover_want(int al, const char *base)
{
	char url[1024];
	job *j;
	int i;

	LOCK();
	url[0] = '\0';
	if (al >= 0 && al < g_tab.n) {
		for (i = 0; i < g_nsubs; i++)
			if (!strcmp(g_subs[i].sid, g_tab.e[al].sid)) { cpy(url, sizeof url, g_subs[i].image); break; }
	}
	for (i = 0; i < g_nasked && url[0]; i++) if (!strcmp(g_asked[i], base)) url[0] = '\0';
	if (url[0] && g_nasked == g_casked) {
		char (*m)[700] = realloc(g_asked, sizeof *m * (size_t)(g_casked = g_casked ? g_casked * 2 : 32));
		if (m) g_asked = m; else url[0] = '\0';
	}
	if (url[0]) cpy(g_asked[g_nasked++], sizeof g_asked[0], base);
	UNLOCK();
	if (!url[0] || !(j = calloc(1, sizeof *j))) return false;
	j->kind = J_COVER;
	cpy(j->a, sizeof j->a, url);
	cpy(j->b, sizeof j->b, base);
	push(j, true);
	return true;
}

bool pod_cover_take(char *base, size_t n)
{
	bool got = false;

	LOCK();
	if (g_nlanded) { snprintf(base, n, "%s", g_landed[--g_nlanded]); got = true; }
	UNLOCK();
	return got;
}

/* ---- the worker ------------------------------------------------------------------------------- */

static void *api_main(void *arg)
{
	(void)arg;
	for (;;) {
		job *j;

		LOCK();
		while (!g_jobs) pthread_cond_wait(&g_cv, &g_mu);
		j = g_jobs;
		g_jobs = j->next;
		UNLOCK();
		switch (j->kind) {
		case J_SYNC:  do_sync(); g_sync_run = 0; break;
		case J_COVER: do_cover(j); break;
		}
		free(j);
	}
	return NULL;
}

void pod_init(const char *card, const char *shared, void (*shrink)(const char *path))
{
	size_t cl = strlen(card);
	pthread_t th;

	snprintf(g_card, sizeof g_card, "%s", card);
	snprintf(g_dir, sizeof g_dir, "%s/podcasts", shared);
	if (!strncmp(shared, card, cl) && shared[cl] == '/')
		snprintf(g_rel_root, sizeof g_rel_root, "%s/podcasts", shared + cl + 1);
	else
		snprintf(g_rel_root, sizeof g_rel_root, ".userdata/shared/podcasts");
	g_shrink = shrink;
	LOCK();
	subs_load();
	UNLOCK();
	if (pthread_create(&th, NULL, api_main, NULL) == 0) pthread_detach(th);
}
