/* SPDX-License-Identifier: MIT */
/* See msrv.h. Split out of plex.c (plorpos-vgr.9) as it was, behaviour and
 * files unchanged; the comments that explain a choice came along with it. */
#include "msrv.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "net.h"
#include "playlist.h"

#define TIMELINE_MS    5000
#define PLAYED_AT      0.90   /* where Plex counts a play */

/* What the device's FFmpeg - the daemon's - can open (measured in the
 * sysroots, plorpos-1lw.8): http and https on the RG DS, GKD and Rotate, and
 * on the H700 since its FFmpeg was rebuilt with Mbed TLS for podcasts
 * (plorpos-7ni.9), and on the Brick since it runs that same FFmpeg
 * (plorpos-5eq.2); still nothing streamed on the Miyoo Flip or the Zero 40. A URL
 * the daemon cannot open is fetched to the card first (the cache below) and
 * played from there. The Rotate defines PLATFORM_H700 too. */
#if defined(PLATFORM_MIYOO) || defined(PLATFORM_ZERO40) || defined(PLEX_FETCH_FIRST)
#define STREAM_HTTP 0
#define STREAM_TLS  0
#elif defined(PLATFORM_GKD) || defined(PLATFORM_ROTATE) || defined(PLATFORM_RGDS) || defined(PLATFORM_H700)
#define STREAM_HTTP 1
#define STREAM_TLS  1
#else
#define STREAM_HTTP 1
#define STREAM_TLS  1
#endif
/* Fetched first: what stays on the card, and how far ahead (plorpos-1lw.22). */
#define CACHE_MB    100        /* the cache's budget; the oldest go first */
#define AHEAD_MAX   MSRV_AHEAD_MAX
#define CACHE_Q     16         /* wishes waiting: the one now and those ahead */

#define PLS_MAX        64
#define PLS_TRACKS_MAX 2000

typedef struct { int key; char reason; } dl_item;
typedef struct { char name[96]; char id[48]; bool smart, dl; } ms_pl;

struct msrv {
	const msrv_ops *o;
	pthread_mutex_t mu;
	char card[512], dir[600];          /* <shared>/<dir> */
	char rel_base[256];                /* card-relative <shared>/<dir> */
	char rel_root[400];                /* card-relative cache root for this library */
	int  stream_kbps, download_kbps, ahead;
	volatile bool offline;
	volatile int dl_fail_note;

	/* the worker */
	pthread_t api; bool api_up;
	pthread_cond_t api_cv;
	msrv_job *jobs; int njobs;         /* a stack: the newest first */

	/* resolve's connection, found on the worker */
	volatile bool picking;
	unsigned pick_failed;              /* when the last one found none; 0 never */

	coverq *cq;

	/* plays made offline */
	pthread_mutex_t plays_mu;
	volatile bool plays_busy;
	unsigned plays_last;

	msrv_table tab;                    /* under mu */

	/* the sync */
	pthread_t sync_th;
	volatile int sync_run, sync_done, sync_total, sync_failed, sync_new;
	volatile int sync_last;            /* the last run: 1 up to date, 2 updated, -1 failed */
	int sync_force;

	/* downloads */
	pthread_t dl_th; bool dl_up;
	pthread_cond_t dl_cv;
	dl_item *dlq; int dlq_n, dlq_cap, dl_pos, dl_total;
	volatile int dl_cancel;
	bool dl_skip;                      /* Cancel Download on one album: the track in flight only */
	bool pl_m3u;                       /* downloaded playlists' Local lists, once downloads drain */
	int dl_purge;
	char dl_now[160];
	volatile int dl_changed;
	pthread_mutex_t man_mu;            /* the manifest; never held with mu taken inside it */

	/* fetched first */
	pthread_t cache_th; bool cache_up;
	pthread_cond_t cache_cv;
	int cache_q[CACHE_Q], cache_n;     /* keys, the one wanted now first */
	int keep[AHEAD_MAX + 1], nkeep;    /* what prefetch last asked for; under mu */

	/* tick and prefetch, between calls */
	int tk_key, tk_state; unsigned tk_ms; bool tk_counted;
	int tk_added; unsigned tk_added_ms;
	int pf_last[AHEAD_MAX + 1], pf_nlast;

	/* playlists */
	ms_pl pls[PLS_MAX];                /* under mu */
	int npls;
	bool pls_in;
	volatile int pl_mirror;
	volatile int pl_gen;
};

#define LOCK()   pthread_mutex_lock(&m->mu)
#define UNLOCK() pthread_mutex_unlock(&m->mu)

void msrv_lock(msrv *m)   { LOCK(); }
void msrv_unlock(msrv *m) { UNLOCK(); }
bool msrv_offline(msrv *m) { return m->offline; }
void msrv_fail_note(msrv *m) { m->dl_fail_note = 1; }

bool msrv_download_take_failed(msrv *m)
{
	bool r = m->dl_fail_note != 0;

	m->dl_fail_note = 0;
	return r;
}

/* ---- small helpers ------------------------------------------------------------- */

unsigned msrv_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}
#define now_ms msrv_now_ms

void msrv_mkdirs(const char *path)
{
	char p[1024];
	char *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}
#define mkdirs msrv_mkdirs

char *msrv_slurp(const char *path, size_t *len)
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
#define slurp msrv_slurp

void msrv_urlenc(const char *s, char *out, size_t n)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;

	for (; *s && o + 4 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
		else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
	}
	out[o] = '\0';
}

/* A request's scratch file, per thread and call. */
void msrv_tmpfile(msrv *m, const char *tag, char *out, size_t n)
{
	static unsigned seq;
	unsigned k;

	LOCK(); k = ++seq; UNLOCK();
	snprintf(out, n, "/tmp/tortos-%s-%s-%ld-%u", m->o->tag, tag, (long)getpid(), k);
}

/* Tabs and newlines are the index's structure. */
void msrv_clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

char *msrv_dupc(const char *s)
{
	char *d = strdup(s ? s : "");

	if (d) msrv_clean(d);
	return d;
}

static bool exists(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* Split a line on tabs in place. */
static int split(char *s, char **f, int max)
{
	int n = 0;

	f[n++] = s;
	while (n < max && (s = strchr(s, '\t'))) { *s++ = '\0'; f[n++] = s; }
	return n;
}

static const char *ext_of(const char *part)
{
	const char *d = strrchr(part, '.');

	return d && !strchr(d, '/') ? d + 1 : "flac";
}

/* The track key in a cache path: ".../<artist>/<album>/<key>.<ext>". */
static int key_of(const char *rel)
{
	const char *s = strrchr(rel, '/');

	return s ? atoi(s + 1) : 0;
}

/* exFAT's forbidden characters and control codes out, no leading dot, no
 * trailing dot or space, at most `max` bytes without splitting a character. */
static void safe_name(const char *in, char *out, size_t max)
{
	size_t o = 0;

	while (*in == '.' || *in == ' ') in++;
	for (; *in && o + 1 < max; in++) {
		unsigned char c = (unsigned char)*in;
		out[o++] = (c < 32 || strchr("/\\:*?\"<>|", c)) ? '_' : (char)c;
	}
	while (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0x80) o--;     /* cut mid-character */
	if (o > 0 && ((unsigned char)out[o - 1] & 0xC0) == 0xC0) o--;
	while (o > 0 && (out[o - 1] == '.' || out[o - 1] == ' ')) o--;
	out[o] = '\0';
	if (!o) snprintf(out, max, "_");
}

/* ---- the config ------------------------------------------------------------------ */

static void cfg_path(msrv *m, char *out, size_t n) { snprintf(out, n, "%s/config", m->dir); }

void msrv_rel_root_set(msrv *m)
{
	char id[96];

	if (m->o->rel_id(id, sizeof id))
		snprintf(m->rel_root, sizeof m->rel_root, "%s/%s", m->rel_base, id);
	else
		m->rel_root[0] = '\0';
}

void msrv_cfg_save(msrv *m)
{
	char p[700], tmp[720];
	FILE *f;
	int fd;

	cfg_path(m, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	mkdirs(m->dir);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) return;
	if (!(f = fdopen(fd, "w"))) { close(fd); return; }
	m->o->cfg_write(f);
	fprintf(f, "stream_kbps=%d\ndownload_kbps=%d\nahead=%d\n",
	        m->stream_kbps, m->download_kbps, m->ahead);
	fflush(f);
	fsync(fd);
	fclose(f);
	rename(tmp, p);
}

static void cfg_load(msrv *m)
{
	char p[700], line[600];
	FILE *f;

	m->o->cfg_reset();
	m->stream_kbps = m->download_kbps = 128;   /* the pak's defaults */
	m->ahead = 1;
	cfg_path(m, p, sizeof p);
	if ((f = fopen(p, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char *v = strchr(line, '=');

			line[strcspn(line, "\r\n")] = '\0';
			if (!v) continue;
			*v++ = '\0';
			if (!strcmp(line, "stream_kbps"))        m->stream_kbps = atoi(v);
			else if (!strcmp(line, "download_kbps")) m->download_kbps = atoi(v);
			else if (!strcmp(line, "ahead"))         m->ahead = atoi(v);
			else m->o->cfg_key(line, v);
		}
		fclose(f);
	}
	m->o->cfg_loaded(m);
	msrv_rel_root_set(m);
}

int  msrv_stream_kbps(msrv *m)   { return m->stream_kbps; }
int  msrv_download_kbps(msrv *m) { return m->download_kbps; }
void msrv_set_stream_kbps(msrv *m, int kbps)   { m->stream_kbps = kbps; msrv_cfg_save(m); }
void msrv_set_download_kbps(msrv *m, int kbps) { m->download_kbps = kbps; msrv_cfg_save(m); }

/* ---- the worker queue ----------------------------------------------------------- */

enum { J_TIMELINE = 1, J_PLS, J_PLDL, J_PLEDIT, J_PICK, J_PLAYS };
static void do_pls(msrv *m);
static void do_pl_downloads(msrv *m);
static void do_pl_edit(msrv *m, msrv_job *j);

msrv_job *msrv_job_new(int kind)
{
	msrv_job *j = calloc(1, sizeof *j);

	if (j) j->kind = kind;
	return j;
}

void msrv_push(msrv *m, msrv_job *j, bool front)
{
	LOCK();
	if (front || !m->jobs) { j->next = m->jobs; m->jobs = j; }
	else { msrv_job *t = m->jobs; while (t->next) t = t->next; t->next = j; j->next = NULL; }
	m->njobs++;
	pthread_cond_signal(&m->api_cv);
	UNLOCK();
}

static void do_pick(msrv *m)
{
	char b[256];
	bool ok = m->o->pick_base(m, b, sizeof b);

	LOCK();
	m->picking = false;
	m->pick_failed = ok ? 0 : (now_ms() | 1);
	UNLOCK();
}

bool msrv_stream_failed(msrv *m, const char *rel)
{
	if (!msrv_is_path(m, rel)) return false;
	fprintf(stderr, "%s: a stream failed; finding the server again\n", m->o->tag);
	m->o->base_failed(m);
	return true;
}

/* Plays counted with no server to tell - Offline Mode, or no network
 * (plorpos-5tm.16.4): kept in plays.tsv, a track key a line, and each sent
 * as one scrobble when the server answers again. No timeline went for
 * them, so Plex counts each once (with a timeline a scrobble counted
 * twice). The file being sent is set aside, so a play made meanwhile is
 * not lost; one the server does not take goes back. */
static void plays_path(msrv *m, char *out, size_t n, bool sending)
{
	snprintf(out, n, "%s/plays%s", m->dir, sending ? ".sending" : ".tsv");
}

static void plays_add(msrv *m, int key)
{
	char p[700];
	FILE *f;

	plays_path(m, p, sizeof p, false);
	pthread_mutex_lock(&m->plays_mu);
	if ((f = fopen(p, "a"))) { fprintf(f, "%d\n", key); fclose(f); }
	pthread_mutex_unlock(&m->plays_mu);
}

static void do_plays(msrv *m)
{
	char p[700], s[700], line[32];
	int sent = 0, kept = 0;
	FILE *f;

	plays_path(m, p, sizeof p, false);
	plays_path(m, s, sizeof s, true);
	pthread_mutex_lock(&m->plays_mu);
	if (access(s, F_OK) != 0) rename(p, s);   /* one left by a restart goes first */
	pthread_mutex_unlock(&m->plays_mu);
	if ((f = fopen(s, "r"))) {
		while (fgets(line, sizeof line, f)) {
			int key = atoi(line);

			if (key <= 0) continue;
			if (!m->offline && m->o->scrobble(m, key) == 200) sent++;
			else { plays_add(m, key); kept++; }
		}
		fclose(f);
		unlink(s);
	}
	if (sent || kept) fprintf(stderr, "%s: %d plays made offline sent%s\n", m->o->tag, sent, kept ? ", some kept" : "");
	m->plays_busy = false;
}

void msrv_plays_retry(msrv *m)
{
	char p[700], s[700];
	unsigned now = now_ms();
	msrv_job *j;

	if (m->plays_busy || m->offline || !m->o->signed_in() || (m->plays_last && now - m->plays_last < 30000u)) return;
	m->plays_last = now;
	plays_path(m, p, sizeof p, false);
	plays_path(m, s, sizeof s, true);
	if ((access(p, F_OK) != 0 && access(s, F_OK) != 0) || !net_online()) return;
	if ((j = msrv_job_new(J_PLAYS))) { m->plays_busy = true; msrv_push(m, j, false); }
}

static void *api_main(void *arg)
{
	msrv *m = arg;

	for (;;) {
		msrv_job *j;

		LOCK();
		while (!m->jobs) pthread_cond_wait(&m->api_cv, &m->mu);
		j = m->jobs;
		m->jobs = j->next;
		m->njobs--;
		UNLOCK();
		switch (j->kind) {
		case J_TIMELINE: m->o->timeline(m, (int)j->n1, j->a, j->n2, j->n3); break;
		case J_PLS:      do_pls(m); break;
		case J_PLDL:     do_pl_downloads(m); break;
		case J_PLEDIT:   do_pl_edit(m, j); break;
		case J_PICK:     do_pick(m); break;
		case J_PLAYS:    do_plays(m); break;
		default:         if (j->kind >= MSRV_J_BACKEND) m->o->job(m, j); break;
		}
		free(j);
	}
	return NULL;
}

/* ---- the track table -------------------------------------------------------------- */

static void tab_free(msrv_table *t)
{
	free(t->ar); free(t->al); free(t->tr); free(t->hash); free(t->arena);
	memset(t, 0, sizeof *t);
}

int msrv_tab_find(const msrv_table *t, int key)
{
	unsigned h;

	if (!t->hcap) return -1;
	for (h = (unsigned)key * 2654435761u % (unsigned)t->hcap; t->hash[h] >= 0;
	     h = (h + 1) % (unsigned)t->hcap)
		if (t->tr[t->hash[h]].key == key) return t->hash[h];
	return -1;
}
#define tab_find msrv_tab_find

static void index_path(msrv *m, char *out, size_t n)
{
	snprintf(out, n, "%s/%s/index.tsv", m->card, m->rel_root);
}

static bool tab_read(msrv *m, msrv_table *t)
{
	char p[700], *buf, *line, *next;
	size_t len;
	int car = 0, cal = 0, ctr = 0;

	memset(t, 0, sizeof *t);
	index_path(m, p, sizeof p);
	if (!(buf = slurp(p, &len))) return false;
	t->arena = buf;
	for (line = buf; line && *line; line = next) {
		char *f[10];
		int nf;

		next = strchr(line, '\n');
		if (next) *next++ = '\0';
		nf = split(line, f, 10);
		if (f[0][0] == 'A' && nf >= 3) {
			if (t->nar == car) { car = car ? car * 2 : 256; t->ar = realloc(t->ar, sizeof *t->ar * (size_t)car); }
			t->ar[t->nar++] = (msrv_artist){ atoi(f[1]), f[2] };
		} else if (f[0][0] == 'L' && nf >= 5 && t->nar) {
			if (t->nal == cal) { cal = cal ? cal * 2 : 1024; t->al = realloc(t->al, sizeof *t->al * (size_t)cal); }
			t->al[t->nal++] = (msrv_album){ atoi(f[1]), t->nar - 1, atoi(f[2]), t->ntr, 0, f[4], f[3] };
		} else if (f[0][0] == 'T' && nf >= 8 && t->nal) {
			if (t->ntr == ctr) { ctr = ctr ? ctr * 2 : 8192; t->tr = realloc(t->tr, sizeof *t->tr * (size_t)ctr); }
			t->tr[t->ntr++] = (msrv_track){ atoi(f[1]), t->nal - 1, atoi(f[2]),
			                                (short)atoi(f[3]), (short)atoi(f[4]), f[6], f[7], f[5] };
			t->al[t->nal - 1].n++;
		}
		if ((t->nar && !t->ar) || (t->nal && !t->al) || (t->ntr && !t->tr)) { tab_free(t); return false; }
	}
	t->hcap = t->ntr * 2 + 1;
	if (!(t->hash = malloc(sizeof *t->hash * (size_t)t->hcap))) { tab_free(t); return false; }
	for (int i = 0; i < t->hcap; i++) t->hash[i] = -1;
	for (int i = 0; i < t->ntr; i++) {
		unsigned h = (unsigned)t->tr[i].key * 2654435761u % (unsigned)t->hcap;
		while (t->hash[h] >= 0) h = (h + 1) % (unsigned)t->hcap;
		t->hash[h] = i;
	}
	return true;
}

/* ---- the library sync ------------------------------------------------------------ */

static int cmp_srec(const void *pa, const void *pb)
{
	const msrv_rec *a = pa, *b = pb;
	int c;

	if ((c = strcasecmp(a->artist_t, b->artist_t))) return c;
	if (a->artist != b->artist) return a->artist < b->artist ? -1 : 1;
	if ((c = strcasecmp(a->album_t, b->album_t))) return c;
	if (a->album != b->album) return a->album < b->album ? -1 : 1;
	if (a->disc != b->disc) return a->disc < b->disc ? -1 : 1;
	if (a->idx != b->idx) return a->idx < b->idx ? -1 : 1;
	return a->key < b->key ? -1 : a->key > b->key;
}

/* The index's header: P, version, the library stamp it was written at, its
 * tracks, the newest change stamp among them and the server's track count then
 * (the last two since plorpos-1lw.24; -1 in an older index). The stamp, or -1
 * without an index. */
static long index_head(msrv *m, long *since, long *total)
{
	char p[700], line[200];
	FILE *f;
	long s = -1;

	*since = *total = -1;
	index_path(m, p, sizeof p);
	if (!(f = fopen(p, "r"))) return -1;
	if (fgets(line, sizeof line, f) && line[0] == 'P') {
		char *x[7];
		int n = split(line, x, 7);

		if (n >= 3) s = atol(x[2]);
		if (n >= 6) { *since = atol(x[4]); *total = atol(x[5]); }
	}
	fclose(f);
	return s;
}

void msrv_sync_progress(msrv *m, int done, int total)
{
	if (done >= 0) m->sync_done = done;
	if (total >= 0) m->sync_total = total;
}

void msrv_rec_free(msrv_rec *x)
{
	free(x->title); free(x->tartist); free(x->album_t);
	free(x->artist_t); free(x->thumb); free(x->part);
	memset(x, 0, sizeof *x);
}

static void sset_free(msrv_set *s)
{
	for (int i = 0; i < s->n; i++) msrv_rec_free(&s->r[i]);
	free(s->r);
	memset(s, 0, sizeof *s);
}

bool msrv_set_push(msrv_set *s, msrv_rec x)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 4096;
		msrv_rec *r = realloc(s->r, sizeof *r * (size_t)cap);

		if (!r) return false;
		s->r = r; s->cap = cap;
	}
	s->r[s->n++] = x;
	return true;
}

/* Track i of an index read back as a record. */
static msrv_rec srec_of(const msrv_table *t, int i)
{
	const msrv_track *tr = &t->tr[i];
	const msrv_album *al = &t->al[tr->album];
	const msrv_artist *ar = &t->ar[al->artist];

	return (msrv_rec){
		.key = tr->key, .album = al->key, .artist = ar->key, .year = al->year,
		.disc = tr->disc, .idx = tr->idx, .dur = tr->dur_ms,
		.title = msrv_dupc(tr->title), .tartist = msrv_dupc(tr->artist), .album_t = msrv_dupc(al->title),
		.artist_t = msrv_dupc(ar->name), .thumb = msrv_dupc(al->thumb), .part = msrv_dupc(tr->part),
	};
}

static bool srec_same(const msrv_rec *a, const msrv_rec *b)
{
	return a->key == b->key && a->album == b->album && a->artist == b->artist &&
	       a->year == b->year && a->disc == b->disc && a->idx == b->idx && a->dur == b->dur &&
	       !strcmp(a->title, b->title) && !strcmp(a->tartist, b->tartist) &&
	       !strcmp(a->album_t, b->album_t) && !strcmp(a->artist_t, b->artist_t) &&
	       !strcmp(a->thumb, b->thumb) && !strcmp(a->part, b->part);
}

static int cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;

	return x < y ? -1 : x > y;
}

static int cmp_srec_key(const void *a, const void *b)
{
	return cmp_int(&((const msrv_rec *)a)->key, &((const msrv_rec *)b)->key);
}

/* What changed since `since` (the backend's listing), merged into the index
 * on the card, into `out` (plorpos-1lw.24). A deleted track leaves no trace in
 * any listing, so the merge must come out at the server's count less the
 * tracks a sync skips (no file), as last time: false when it does not, or on
 * any failure - then the caller reads the library whole. `*changed`: how many
 * tracks really differ (Plex touches tracks without changing them). */
static bool sync_incremental(msrv *m, long since, long was_total, msrv_set *out, long *total, int *changed)
{
	msrv_table t;
	msrv_set ch = { .since = since };
	int *keys = NULL, i;
	bool ok = false;

	*changed = 0;
	if (!tab_read(m, &t)) return false;
	fprintf(stderr, "%s: updating %s since %ld\n", m->o->tag, m->o->library(), since);
	if (!m->o->changed(m, since, &t, &ch, total)) goto done;

	/* The index less what came again, then what came, once each. */
	qsort(ch.r, (size_t)ch.n, sizeof *ch.r, cmp_srec_key);
	if (ch.n && !(keys = malloc(sizeof *keys * (size_t)ch.n))) goto done;
	for (i = 0; i < ch.n; i++) keys[i] = ch.r[i].key;
	out->since = ch.since;
	for (i = 0; i < t.ntr; i++) {
		msrv_rec x;

		if (ch.n && bsearch(&t.tr[i].key, keys, (size_t)ch.n, sizeof *keys, cmp_int)) continue;
		x = srec_of(&t, i);
		if (!msrv_set_push(out, x)) { msrv_rec_free(&x); goto done; }
	}
	for (i = 0; i < ch.n; i++) {
		int o;

		if (i && keys[i] == keys[i - 1]) continue;              /* freed with ch */
		if ((o = tab_find(&t, ch.r[i].key)) < 0) (*changed)++;
		else {
			msrv_rec was = srec_of(&t, o);

			if (!srec_same(&was, &ch.r[i])) (*changed)++;
			msrv_rec_free(&was);
		}
		if (!msrv_set_push(out, ch.r[i])) goto done;
		memset(&ch.r[i], 0, sizeof ch.r[i]);                    /* out's now */
	}
	if (*total - out->n != was_total - t.ntr) {
		fprintf(stderr, "%s: %ld tracks on the server, %d here (%ld then, %d): reading it all\n",
		        m->o->tag, *total, out->n, was_total, t.ntr);
		goto done;
	}
	ok = true;
done:
	free(keys);
	sset_free(&ch);                  /* the duplicates left behind, and on failure the rest */
	tab_free(&t);
	return ok;
}

static void *sync_main(void *arg)
{
	msrv *m = arg;
	msrv_set s = { 0 };
	long stamp, old, since, was_total, total = 0;
	char p[700], tmp[720];
	unsigned t0 = now_ms();
	int i, changed = -1;
	bool inc = false;
	FILE *f;

	m->sync_done = 0; m->sync_total = 0;
	old = index_head(m, &since, &was_total);
	stamp = m->o->stamp(m, old, since, was_total);
	if (stamp < 0) { m->sync_failed = 1; goto out; }
	if (!m->sync_force && stamp == old) { m->sync_last = 1; goto out; }
	if (!m->sync_force && since > 0 && was_total >= 0) {
		inc = sync_incremental(m, since, was_total, &s, &total, &changed);
		if (!inc) { sset_free(&s); changed = -1; }
	}
	if (!inc) {
		fprintf(stderr, "%s: syncing %s (%s)\n", m->o->tag, m->o->library(), m->sync_force ? "asked" : "changed");
		m->sync_done = m->sync_total = 0;
		if ((total = m->o->sync_full(m, &s)) < 0) { m->sync_failed = 1; goto out; }
	}
	qsort(s.r, (size_t)s.n, sizeof *s.r, cmp_srec);

	index_path(m, p, sizeof p);
	{
		char d[700], *sl;
		snprintf(d, sizeof d, "%s", p);
		if ((sl = strrchr(d, '/'))) { *sl = '\0'; mkdirs(d); }
	}
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) { m->sync_failed = 1; goto out; }
	fprintf(f, "P\t1\t%ld\t%d\t%ld\t%ld\n", stamp, s.n, s.since, total);
	for (i = 0; i < s.n; i++) {
		const msrv_rec *r = s.r;

		if (i == 0 || r[i].artist != r[i - 1].artist)
			fprintf(f, "A\t%d\t%s\n", r[i].artist, r[i].artist_t);
		if (i == 0 || r[i].album != r[i - 1].album || r[i].artist != r[i - 1].artist)
			fprintf(f, "L\t%d\t%d\t%s\t%s\n", r[i].album, r[i].year, r[i].thumb, r[i].album_t);
		fprintf(f, "T\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", r[i].key, r[i].dur, r[i].disc,
		        r[i].idx, r[i].part, r[i].title, r[i].tartist);
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) { fclose(f); unlink(tmp); m->sync_failed = 1; goto out; }
	fclose(f);
	if (rename(tmp, p) != 0) { unlink(tmp); m->sync_failed = 1; goto out; }
	if (inc) fprintf(stderr, "%s: %d tracks, %d changed, updated in %u ms\n", m->o->tag, s.n, changed, now_ms() - t0);
	else fprintf(stderr, "%s: %d tracks synced in %u ms\n", m->o->tag, s.n, now_ms() - t0);
	if (changed) m->sync_new = 1;            /* -1 (a full sync) included */
	m->sync_last = changed ? 2 : 1;
out:
	/* The playlists, by the index as it now is - library changed or not
	 * (plorpos-5eq.7). */
	if (!m->sync_failed) msrv_playlists_sync(m);
	if (m->sync_failed) { m->sync_last = -1; fprintf(stderr, "%s: sync failed after %d tracks\n", m->o->tag, m->sync_done); }
	sset_free(&s);
	m->sync_run = 0;
	return NULL;
}

/* ---- downloads -------------------------------------------------------------------- */

static void pl_local_write_all(msrv *m);
static void purge_album(msrv *m, int al);

/* The downloaded copy's path, with `ext`. Under the lock, with t from the table. */
static void local_path(msrv *m, const msrv_track *t, const char *ext, char *out, size_t n, bool card_rel)
{
	const msrv_album *al = &m->tab.al[t->album];
	char ar[100], alb[100], ti[100];

	safe_name(m->tab.ar[al->artist].name, ar, sizeof ar);
	safe_name(al->title, alb, sizeof alb);
	safe_name(t->title, ti, sizeof ti);
	if (t->disc > 1) snprintf(out, n, "%s%sMusic/%s/%s/%s/%d%02d %s.%s",
	                          card_rel ? "" : m->card, card_rel ? "" : "/", m->o->music, ar, alb, t->disc, t->idx, ti, ext);
	else snprintf(out, n, "%s%sMusic/%s/%s/%s/%02d %s.%s",
	              card_rel ? "" : m->card, card_rel ? "" : "/", m->o->music, ar, alb, t->idx, ti, ext);
}

/* The copy that is there, original or Opus, whichever quality it came at. */
static bool local_copy(msrv *m, const msrv_track *t, char *out, size_t n)
{
	local_path(m, t, ext_of(t->part), out, n, false);
	if (exists(out)) return true;
	local_path(m, t, "opus", out, n, false);
	return exists(out);
}

static void manifest_path(msrv *m, char *out, size_t n) { snprintf(out, n, "%s/downloads.tsv", m->dir); }

/* The manifest: why each copy is there ('a' an album, 'f' a favorite, 'p' a
 * playlist), so a favorites sync deletes only what it put there. Its own
 * lock: the shelf, the download worker and the favorites job all rewrite it. */
static void manifest_set(msrv *m, int key, char reason, bool on)
{
	char p[700], tmp[720], line[200];
	FILE *in, *out;
	bool had = false;

	manifest_path(m, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	pthread_mutex_lock(&m->man_mu);
	if (!(out = fopen(tmp, "w"))) { pthread_mutex_unlock(&m->man_mu); return; }
	if ((in = fopen(p, "r"))) {
		while (fgets(line, sizeof line, in)) {
			int k = atoi(line);
			char *tab = strchr(line, '\t');
			char r = tab ? tab[1] : 'a';

			if (k == key && r == reason) { had = true; if (!on) continue; }
			fputs(line, out);
		}
		fclose(in);
	}
	if (on && !had) fprintf(out, "%d\t%c\n", key, reason);
	fclose(out);
	rename(tmp, p);
	pthread_mutex_unlock(&m->man_mu);
}

static bool manifest_has(msrv *m, int key, char reason)
{
	char p[700], line[200];
	FILE *in;
	bool has = false;

	manifest_path(m, p, sizeof p);
	pthread_mutex_lock(&m->man_mu);
	if ((in = fopen(p, "r"))) {
		while (!has && fgets(line, sizeof line, in)) {
			char *tab = strchr(line, '\t');
			has = atoi(line) == key && tab && tab[1] == reason;
		}
		fclose(in);
	}
	pthread_mutex_unlock(&m->man_mu);
	return has;
}

/* A copy of track `key` safe to use with the lock released: the table can
 * be replaced under us (a sync landing). False when it is not in it. */
static bool track_copy(msrv *m, int key, msrv_track *t, char *partk, size_t pn, char *thumb, size_t tn)
{
	int i;

	LOCK();
	if ((i = tab_find(&m->tab, key)) < 0) { UNLOCK(); return false; }
	*t = m->tab.tr[i];
	snprintf(partk, pn, "%s", t->part);
	if (thumb) snprintf(thumb, tn, "%s", m->tab.al[t->album].thumb);
	t->part = partk;
	t->title = t->artist = NULL;
	UNLOCK();
	return true;
}

static bool download_one(msrv *m, int key, char reason)
{
	char base[256], dest[1024], part[1100], dir[1024], partk[320], thumb[320], *s;
	msrv_track t;
	int kbps = m->o->kbps_download(), code;

	LOCK();
	{
		int i = tab_find(&m->tab, key);
		if (i < 0) { UNLOCK(); return false; }
		if (local_copy(m, &m->tab.tr[i], dest, sizeof dest)) {
			UNLOCK();
			manifest_set(m, key, reason, true);
			return true;
		}
		local_path(m, &m->tab.tr[i], kbps ? "opus" : ext_of(m->tab.tr[i].part), dest, sizeof dest, false);
		snprintf(m->dl_now, sizeof m->dl_now, "%s", m->tab.tr[i].title);
	}
	UNLOCK();
	if (!track_copy(m, key, &t, partk, sizeof partk, thumb, sizeof thumb)) return false;
	if (!m->o->pick_base(m, base, sizeof base)) return false;
	snprintf(dir, sizeof dir, "%s", dest);
	if ((s = strrchr(dir, '/'))) { *s = '\0'; mkdirs(dir); }
	/* The album's art beside it as cover.jpg, which the Local source reads
	 * (ml_folder_image): a transcode carries no picture of its own. */
	if (thumb[0]) {
		char cov[1100];

		snprintf(cov, sizeof cov, "%s/cover.jpg", dir);
		if (!exists(cov)) {
			snprintf(part, sizeof part, "%s.part", cov);
			if (m->o->cover_get(m, thumb, part, &m->dl_cancel) != 200 || rename(part, cov) != 0)
				unlink(part);
		}
	}
	code = m->o->fetch_track(m, base, &t, kbps, dest, &m->dl_cancel);
	if (code == 200) {
		manifest_set(m, key, reason, true);
		m->dl_changed = 1;
		return true;
	}
	fprintf(stderr, "%s: download of track %d failed (http %d)\n", m->o->tag, key, code);
	m->dl_fail_note = 1;
	if (code == 0) m->o->base_failed(m);
	return false;
}

/* ---- fetched first: a device whose FFmpeg cannot open the stream ---------------- */

static bool need_fetch(const char *base)
{
	if (!STREAM_HTTP) return true;
	return !STREAM_TLS && !strncmp(base, "https:", 6);
}

/* Where track t is cached (either quality), or where it would go. */
static bool cache_file(msrv *m, const msrv_track *t, char *out, size_t n)
{
	snprintf(out, n, "%s/cache/%d.opus", m->dir, t->key);
	if (exists(out)) return true;
	snprintf(out, n, "%s/cache/%d.%s", m->dir, t->key, ext_of(t->part));
	if (exists(out)) return true;
	if (m->o->kbps_stream()) snprintf(out, n, "%s/cache/%d.opus", m->dir, t->key);
	return false;
}

/* The cache within CACHE_MB, the oldest out first - never a fetch half done,
 * the track playing or one wanted ahead. */
static void cache_trim(msrv *m)
{
	char d[700], p[1100];
	DIR *dd;
	struct dirent *e;
	struct { char name[64]; time_t m; off_t sz; } f[96];
	int n = 0, i, j, keep[AHEAD_MAX + 1], nkeep;
	long long total = 0;

	LOCK();
	nkeep = m->nkeep;
	memcpy(keep, m->keep, sizeof keep[0] * (size_t)nkeep);
	UNLOCK();
	snprintf(d, sizeof d, "%s/cache", m->dir);
	if (!(dd = opendir(d))) return;
	while ((e = readdir(dd)) && n < 96) {
		struct stat st;

		if (e->d_name[0] == '.' || strstr(e->d_name, ".part")) continue;
		snprintf(p, sizeof p, "%s/%s", d, e->d_name);
		if (stat(p, &st) != 0) continue;
		snprintf(f[n].name, sizeof f[n].name, "%s", e->d_name);
		f[n].m = st.st_mtime;
		f[n].sz = st.st_size;
		total += f[n++].sz;
	}
	closedir(dd);
	while (total > (long long)CACHE_MB << 20) {
		int old = -1, k;

		for (i = 0; i < n; i++) {
			if (f[i].sz < 0) continue;                       /* gone already */
			for (k = atoi(f[i].name), j = 0; j < nkeep && keep[j] != k; j++) {}
			if (j < nkeep) continue;
			if (old < 0 || f[i].m < f[old].m) old = i;
		}
		if (old < 0) break;                                  /* all of it wanted */
		snprintf(p, sizeof p, "%s/%s", d, f[old].name);
		unlink(p);
		total -= f[old].sz;
		f[old].sz = -1;
	}
}

/* `.part` files under dir, `depth` folders down: what a download cut off by
 * a power-off or a kill left (plorpos-ps8.14). Called only by the thread
 * that writes there, before its first job, so none is being written. */
static int sweep_parts(const char *dir, int depth)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char p[1200];
	int n = 0;
	size_t l;

	if (!d) return 0;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		l = strlen(e->d_name);
		if (l > 5 && !strcmp(e->d_name + l - 5, ".part")) n += unlink(p) == 0;
		else if (depth > 0 && (e->d_type == DT_DIR || e->d_type == DT_UNKNOWN)) n += sweep_parts(p, depth - 1);
	}
	closedir(d);
	return n;
}

static void *cache_main(void *arg)
{
	msrv *m = arg;

	{
		char c[700];
		int n;

		snprintf(c, sizeof c, "%s/cache", m->dir);
		if ((n = sweep_parts(c, 0))) fprintf(stderr, "%s: removed %d unfinished pre-fetch files\n", m->o->tag, n);
	}
	for (;;) {
		char base[256], dest[1100], partk[320];
		msrv_track t;
		int key, code;
		unsigned t0;

		LOCK();
		while (!m->cache_n) pthread_cond_wait(&m->cache_cv, &m->mu);
		key = m->cache_q[0];
		UNLOCK();
		if (track_copy(m, key, &t, partk, sizeof partk, NULL, 0) && !cache_file(m, &t, dest, sizeof dest) &&
		    m->o->pick_base(m, base, sizeof base)) {
			char d[700];

			snprintf(d, sizeof d, "%s/cache", m->dir);
			mkdirs(d);
			t0 = now_ms();
			code = m->o->fetch_track(m, base, &t, m->o->kbps_stream(), dest, NULL);
			fprintf(stderr, "%s: track %d fetched first in %u ms (http %d)\n", m->o->tag, key, now_ms() - t0, code);
			if (code == 0) m->o->base_failed(m);
			if (code == 200) cache_trim(m);
		}
		LOCK();
		if (m->cache_n && m->cache_q[0] == key) {
			memmove(m->cache_q, m->cache_q + 1, sizeof m->cache_q[0] * (size_t)--m->cache_n);
		}
		UNLOCK();
	}
	return NULL;
}

/* Fetch `key` - `now`: ahead of anything queued. Under the lock. */
static void cache_want(msrv *m, int key, bool now)
{
	int i;

	for (i = 0; i < m->cache_n; i++) if (m->cache_q[i] == key) break;
	if (i < m->cache_n) {
		if (now && i > 1) { memmove(m->cache_q + 2, m->cache_q + 1, sizeof m->cache_q[0] * (size_t)(i - 1)); m->cache_q[1] = key; }
		return;
	}
	if (m->cache_n == CACHE_Q) m->cache_n = CACHE_Q - 1;   /* the newest wish goes */
	if (now && m->cache_n) {                      /* behind the one in flight */
		memmove(m->cache_q + 2, m->cache_q + 1, sizeof m->cache_q[0] * (size_t)(m->cache_n - 1));
		m->cache_q[1] = key;
	} else if (now) {
		m->cache_q[0] = key;
	} else {
		m->cache_q[m->cache_n] = key;
	}
	m->cache_n++;
	if (!m->cache_up) m->cache_up = pthread_create(&m->cache_th, NULL, cache_main, m) == 0;
	pthread_cond_signal(&m->cache_cv);
}

static void *dl_main(void *arg)
{
	msrv *m = arg;

	{
		char d[700];
		int n;

		snprintf(d, sizeof d, "%s/Music/%s", m->card, m->o->music);
		if ((n = sweep_parts(d, 2))) fprintf(stderr, "%s: removed %d unfinished downloads\n", m->o->tag, n);
	}
	for (;;) {
		dl_item it;

		LOCK();
		while (m->dl_pos >= m->dlq_n) {
			m->dlq_n = m->dl_pos = m->dl_total = 0;
			m->dl_now[0] = '\0';
			if (m->pl_m3u) {         /* a playlist's downloads are in */
				m->pl_m3u = false;
				UNLOCK();
				pl_local_write_all(m);
				LOCK();
				continue;
			}
			pthread_cond_wait(&m->dl_cv, &m->mu);
		}
		it = m->dlq[m->dl_pos];
		m->dl_cancel = 0;
		UNLOCK();
		download_one(m, it.key, it.reason);
		LOCK();
		if (m->dl_cancel && !m->dl_skip) { m->dlq_n = m->dl_pos = 0; m->dl_cancel = 0; }
		else m->dl_pos++;
		m->dl_skip = false;
		{ int al = m->dl_purge; m->dl_purge = -1; UNLOCK(); if (al >= 0) purge_album(m, al); }
	}
	return NULL;
}

static void dl_queue(msrv *m, int key, char reason)
{
	int i;

	for (i = m->dl_pos; i < m->dlq_n; i++) if (m->dlq[i].key == key) return;
	if (m->dlq_n == m->dlq_cap) {
		int cap = m->dlq_cap ? m->dlq_cap * 2 : 256;
		dl_item *q = realloc(m->dlq, sizeof *q * (size_t)cap);
		if (!q) return;
		m->dlq = q; m->dlq_cap = cap;
	}
	m->dlq[m->dlq_n++] = (dl_item){ key, reason };
	m->dl_total++;
}

static void dl_start(msrv *m)
{
	if (!m->dl_up) m->dl_up = pthread_create(&m->dl_th, NULL, dl_main, m) == 0;
	pthread_cond_signal(&m->dl_cv);
}

/* ---- the source ------------------------------------------------------------------- */

msrv *msrv_new(const msrv_ops *ops, const char *card, const char *shared)
{
	msrv *m = calloc(1, sizeof *m);
	size_t cl = strlen(card);

	if (!m) return NULL;
	m->o = ops;
	pthread_mutex_init(&m->mu, NULL);
	pthread_mutex_init(&m->plays_mu, NULL);
	pthread_mutex_init(&m->man_mu, NULL);
	pthread_cond_init(&m->api_cv, NULL);
	pthread_cond_init(&m->dl_cv, NULL);
	pthread_cond_init(&m->cache_cv, NULL);
	m->dl_purge = -1;
	m->pl_mirror = 1;
	m->tk_state = -1;
	m->pf_nlast = -1;
	snprintf(m->card, sizeof m->card, "%s", card);
	snprintf(m->dir, sizeof m->dir, "%s/%s", shared, ops->dir);
	if (!strncmp(shared, card, cl) && shared[cl] == '/')
		snprintf(m->rel_base, sizeof m->rel_base, "%s/%s", shared + cl + 1, ops->dir);
	else
		snprintf(m->rel_base, sizeof m->rel_base, ".userdata/shared/%s", ops->dir);
	cfg_load(m);
	m->api_up = pthread_create(&m->api, NULL, api_main, m) == 0;
	if (m->api_up) pthread_detach(m->api);
	return m;
}

bool msrv_lib_cached(msrv *m)
{
	char p[700];

	if (!m->o->ready()) return false;
	index_path(m, p, sizeof p);
	return exists(p);
}

bool msrv_lib_load(msrv *m, ml_lib *out)
{
	msrv_table t;
	int i, j;

	memset(out, 0, sizeof *out);
	if (!m->o->ready() || !tab_read(m, &t)) return false;
	out->artists = calloc((size_t)(t.nar ? t.nar : 1), sizeof *out->artists);
	out->albums  = calloc((size_t)(t.nal ? t.nal : 1), sizeof *out->albums);
	out->tracks  = calloc((size_t)(t.ntr ? t.ntr : 1), sizeof *out->tracks);
	if (!out->artists || !out->albums || !out->tracks) { ml_free(out); tab_free(&t); return false; }
	for (i = 0; i < t.nar; i++) {
		snprintf(out->artists[i].name, sizeof out->artists[i].name, "%s", t.ar[i].name);
		out->artists[i].first = -1;
	}
	for (i = 0; i < t.nal; i++) {
		msrv_album *a = &t.al[i];
		ml_artist *ar = &out->artists[a->artist];

		snprintf(out->albums[i].name, sizeof out->albums[i].name, "%s", a->title);
		out->albums[i].first = a->first;
		out->albums[i].n = a->n;
		if (ar->first < 0) ar->first = i;
		ar->n++;
		for (j = a->first; j < a->first + a->n; j++) {
			msrv_track *tr = &t.tr[j];

			snprintf(out->tracks[j].name, sizeof out->tracks[j].name, "%s", tr->title);
			snprintf(out->tracks[j].path, sizeof out->tracks[j].path, "%s/%d/%d/%d.%s",
			         m->rel_root, t.ar[a->artist].key, a->key, tr->key, ext_of(tr->part));
		}
	}
	out->nartists = t.nar;
	out->nalbums = t.nal;
	out->ntracks = t.ntr;
	LOCK();
	tab_free(&m->tab);
	m->tab = t;
	UNLOCK();
	fprintf(stderr, "%s: %d artists, %d albums, %d tracks from the index\n", m->o->tag, t.nar, t.nal, t.ntr);
	return true;
}

int msrv_album_year(msrv *m, int al)
{
	int y;

	LOCK();
	y = al >= 0 && al < m->tab.nal ? m->tab.al[al].year : 0;
	UNLOCK();
	return y;
}

void msrv_sync_start(msrv *m, bool force)
{
	if (!m->o->ready() || m->sync_run) return;
	m->sync_run = 1;
	m->sync_failed = 0;
	m->sync_last = 0;
	m->sync_force = force || !msrv_lib_cached(m);
	if (pthread_create(&m->sync_th, NULL, sync_main, m) != 0) { m->sync_run = 0; m->sync_failed = 1; return; }
	pthread_detach(m->sync_th);
}

int msrv_sync_state(msrv *m, int *done, int *total)
{
	if (done) *done = m->sync_done;
	if (total) *total = m->sync_total;
	return m->sync_run ? 1 : m->sync_failed ? -1 : 0;
}

int msrv_sync_outcome(msrv *m) { return m->sync_last; }

bool msrv_sync_take(msrv *m)
{
	if (!m->sync_new) return false;
	m->sync_new = 0;
	return true;
}

bool msrv_is_path(msrv *m, const char *rel)
{
	size_t n = strlen(m->rel_base);

	return rel && n && !strncmp(rel, m->rel_base, n) && rel[n] == '/';
}

const char *msrv_rel_root(msrv *m) { return m->rel_root; }

int msrv_track_index(msrv *m, const char *rel)
{
	int i;

	if (!msrv_is_path(m, rel)) return -1;
	LOCK();
	i = tab_find(&m->tab, key_of(rel));
	UNLOCK();
	return i;
}

bool msrv_title(msrv *m, const char *rel, char *out, size_t n)
{
	int i;

	if (!msrv_is_path(m, rel)) return false;
	LOCK();
	i = tab_find(&m->tab, key_of(rel));
	if (i >= 0) snprintf(out, n, "%s", m->tab.tr[i].title);
	UNLOCK();
	return i >= 0;
}

/* Not inlined: each caller's buffer would get its own truncation warning. */
__attribute__((noinline))
void msrv_pl_dir(msrv *m, char *out, size_t n) { snprintf(out, n, "%s/%s/Playlists", m->card, m->rel_root); }

/* coverq's fetch, through the backend's own trampoline: the album's art to <b>.jpg. */
bool msrv_cover_fetch(msrv *m, const char *thumb, const char *b)
{
	char dest[620], part[640];
	int code;

	snprintf(dest, sizeof dest, "%s.jpg", b);
	snprintf(part, sizeof part, "%s.part", dest);
	{
		char d[620], *s;
		snprintf(d, sizeof d, "%s", dest);
		if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	}
	code = m->o->cover_get(m, thumb, part, NULL);
	if (code == 200 && rename(part, dest) == 0) return true;
	unlink(part);
	if (code == 0) m->o->base_failed(m);
	return false;
}

bool msrv_cover_want(msrv *m, int al, const char *base)
{
	char thumb[600];

	LOCK();
	if (al < 0 || al >= m->tab.nal || !m->tab.al[al].thumb[0] || !m->o->signed_in()) { UNLOCK(); return false; }
	snprintf(thumb, sizeof thumb, "%s", m->tab.al[al].thumb);
	UNLOCK();
	if (!m->cq && !(m->cq = coverq_new(m->o->cover_cb))) return false;
	coverq_want(m->cq, thumb, base);
	return true;
}

void msrv_cover_bump(msrv *m, const char *base) { coverq_bump(m->cq, base); }

bool msrv_covers_idle(msrv *m) { return coverq_idle(m->cq); }

bool msrv_cover_take(msrv *m, char *base, size_t n) { return coverq_take(m->cq, base, n); }

bool msrv_resolve(msrv *m, const char *rel, musec_remote *out)
{
	char base[256], partk[320];
	int i;
	msrv_track t;

	if (!msrv_is_path(m, rel)) return false;
	memset(out, 0, sizeof *out);
	LOCK();
	i = tab_find(&m->tab, key_of(rel));
	if (i < 0) {
		UNLOCK();
		/* Not in the table (a queue outliving its library): the daemon
		 * reports it unplayable and the queue moves on. */
		snprintf(out->url, sizeof out->url, "%s:missing", m->o->tag);
		return true;
	}
	t = m->tab.tr[i];
	snprintf(out->title, sizeof out->title, "%s", t.title);
	/* The track's own artist is there only when it is not the album's
	 * (Plex's originalTitle): the album's otherwise, or a playlist showed
	 * its first track's artist on every track (plorpos-1lw.20). */
	snprintf(out->artist, sizeof out->artist, "%s",
	         t.artist && t.artist[0] ? t.artist : m->tab.ar[m->tab.al[t.album].artist].name);
	out->len = t.dur_ms / 1000.0;
	if (local_copy(m, &t, out->url, sizeof out->url)) { UNLOCK(); return true; }
	if (m->offline) {                             /* a pre-fetched copy plays too */
		if (!cache_file(m, &t, out->url, sizeof out->url)) out->offline = true;
		UNLOCK();
		return true;
	}
	m->o->base_now(base, sizeof base);
	if (!base[0]) {
		/* No connection known (a fresh start, or the last one stopped
		 * answering): one is found off this thread, the queue's, and the
		 * player asks again on each poll (plorpos-1lw.19). A track fetched
		 * first plays meanwhile. */
		bool ask = !m->picking && (!m->pick_failed || now_ms() - m->pick_failed > 10000);

		if (cache_file(m, &t, out->url, sizeof out->url)) { UNLOCK(); return true; }
		if (ask) m->picking = true;
		UNLOCK();
		if (ask) { msrv_job *j = msrv_job_new(J_PICK); if (j) msrv_push(m, j, true); else m->picking = false; }
		out->wait = true;
		return true;
	}
	if (need_fetch(base)) {
		if (!cache_file(m, &t, out->url, sizeof out->url)) {
			cache_want(m, t.key, true);
			out->wait = true;
		}
		UNLOCK();
		return true;
	}
	snprintf(partk, sizeof partk, "%s", t.part);
	t.part = partk; t.title = t.artist = NULL;
	UNLOCK();
	m->o->stream_url(m, base, &t, out->url, sizeof out->url, out->codec, sizeof out->codec);
	return true;
}

bool msrv_owns(msrv *m, const char *daemon_path, const char *rel)
{
	char copy[1024];
	int i, key = key_of(rel);
	bool yes = false;

	if (!msrv_is_path(m, rel)) return false;
	if (m->o->owns_url(daemon_path, key, NULL)) return true;
	LOCK();
	if ((i = tab_find(&m->tab, key)) >= 0) {
		yes = m->o->owns_url(daemon_path, key, m->tab.tr[i].part);
		if (!yes && local_copy(m, &m->tab.tr[i], copy, sizeof copy)) yes = !strcmp(copy, daemon_path);
		if (!yes && cache_file(m, &m->tab.tr[i], copy, sizeof copy)) yes = !strcmp(copy, daemon_path);
	}
	UNLOCK();
	return yes;
}

void msrv_tick(msrv *m, const char *rel, int state, double at, double len)
{
	static const char *const NAMES[] = { "stopped", "playing", "paused" };
	unsigned now = now_ms();
	int k = msrv_is_path(m, rel) ? key_of(rel) : 0;
	msrv_job *j;

	if (state < 0 || state > 2) state = 0;
	/* A "stopped" timeline ends the client's transcode on the server - the
	 * one just started for the next track too, which then opens as nothing,
	 * fails, and the queue ran through the album at a track a frame
	 * (plorpos-1lw.13; measured on carol). So, as the pak: none when the
	 * next track is this source's too (reporting it is enough), and none for
	 * a track that has not played yet (stopped while its stream opens). */
	if (k != m->tk_key) {
		if (m->tk_key && !k && m->tk_state > 0 && (j = msrv_job_new(J_TIMELINE))) {
			j->n1 = m->tk_key; snprintf(j->a, sizeof j->a, "stopped");
			msrv_push(m, j, false);
		}
		m->tk_key = k; m->tk_counted = false; m->tk_state = -1;
	}
	if (!m->tk_key || !m->o->signed_in()) return;
	if (m->offline) goto mark;                     /* no timelines; the play counted below */
	if (state == 0 && m->tk_state <= 0) goto mark;
	if (state == m->tk_state && (state != 1 || now - m->tk_ms < TIMELINE_MS)) goto mark;
	m->tk_state = state;
	m->tk_ms = now;
	if ((j = msrv_job_new(J_TIMELINE))) {
		j->n1 = m->tk_key; j->n2 = (long)(at * 1000); j->n3 = (long)(len * 1000);
		snprintf(j->a, sizeof j->a, "%s", NAMES[state]);
		msrv_push(m, j, false);
	}
mark:
	/* Plex counts the play itself from a timeline past 90%; a scrobble on
	 * top counted it twice (measured: viewCount 0 -> 2, carol, 2026-11-16).
	 * So no scrobble - a timeline the moment it crosses, in case the next
	 * 5 s one would come after the end. */
	if (!m->tk_counted && len > 0 && at >= len * PLAYED_AT && (m->offline || !net_online())) {
		/* Once a play: Muse's connection lost for a moment reads as a
		 * track change and clears `counted`, so the same track again
		 * within its own length is that, not a second play. */
		m->tk_counted = true;
		if (m->tk_key != m->tk_added || now - m->tk_added_ms > (unsigned)(len * 1000)) {
			plays_add(m, m->tk_key);              /* sent when the server answers */
			m->tk_added = m->tk_key;
			m->tk_added_ms = now;
		}
		return;
	}
	if (!m->tk_counted && len > 0 && at >= len * PLAYED_AT && (j = msrv_job_new(J_TIMELINE))) {
		m->tk_counted = true;
		m->tk_ms = now;
		j->n1 = m->tk_key; j->n2 = (long)(at * 1000); j->n3 = (long)(len * 1000);
		snprintf(j->a, sizeof j->a, "%s", NAMES[state]);
		msrv_push(m, j, false);
	}
}

/* The size a fetch of t will be, about: Opus at its rate, the original
 * at a CD's FLAC. */
static long long fetch_bytes(msrv *m, const msrv_track *t)
{
	int kbps = m->o->kbps_stream();

	return (long long)t->dur_ms * (kbps ? kbps : 1000) / 8;
}

void msrv_prefetch(msrv *m, const char *playing, const char *const *next, int n)
{
	int keys[AHEAD_MAX + 1], nk = 0, i, j, t;
	long long sum = 0;
	char base[256], f[1100];

	keys[nk++] = msrv_is_path(m, playing) ? key_of(playing) : 0;
	for (i = 0; i < n && i < AHEAD_MAX; i++)
		keys[nk++] = msrv_is_path(m, next[i]) ? key_of(next[i]) : 0;
	if (nk == m->pf_nlast && !memcmp(keys, m->pf_last, sizeof keys[0] * (size_t)nk)) return;
	memcpy(m->pf_last, keys, sizeof keys[0] * (size_t)nk);
	m->pf_nlast = nk;
	LOCK();
	/* Wishes ahead that are no longer ahead go (the one in flight finishes;
	 * one wanted now, by the player, is the playing track and stays). */
	for (i = j = 1; i < m->cache_n; i++) {
		int k = m->cache_q[i], a, b;

		for (a = 0; a < m->nkeep && m->keep[a] != k; a++) {}
		for (b = 0; b < nk && keys[b] != k; b++) {}
		if (a < m->nkeep && b == nk) continue;
		m->cache_q[j++] = k;
	}
	if (m->cache_n > 1) m->cache_n = j;
	memcpy(m->keep, keys, sizeof keys[0] * (size_t)nk);
	m->nkeep = nk;
	m->o->base_now(base, sizeof base);
	/* In order, as long as the guess at their size fits the budget. */
	if (base[0] && need_fetch(base))
		for (i = 1; i < nk; i++) {
			if (!keys[i] || (t = tab_find(&m->tab, keys[i])) < 0) continue;
			if (local_copy(m, &m->tab.tr[t], f, sizeof f)) continue;
			sum += fetch_bytes(m, &m->tab.tr[t]);
			if (i > 1 && sum > (long long)CACHE_MB << 20) break;
			if (!cache_file(m, &m->tab.tr[t], f, sizeof f)) cache_want(m, keys[i], false);
		}
	if (!base[0]) m->pf_nlast = -1;            /* no connection yet: again next poll */
	UNLOCK();
}

int  msrv_ahead(msrv *m) { return m->ahead >= 1 && m->ahead <= AHEAD_MAX ? m->ahead : 1; }
void msrv_set_ahead(msrv *m, int n) { m->ahead = n; msrv_cfg_save(m); }
bool msrv_fetches_first(void) { return !STREAM_HTTP || !STREAM_TLS; }

int msrv_album_downloaded(msrv *m, int al)
{
	char p[1024];
	int i, n = 0;

	LOCK();
	if (al >= 0 && al < m->tab.nal)
		for (i = m->tab.al[al].first; i < m->tab.al[al].first + m->tab.al[al].n; i++)
			if (local_copy(m, &m->tab.tr[i], p, sizeof p)) n++;
	UNLOCK();
	return n;
}

/* Offline Mode's lists (plorpos-5tm.16): which albums have any track on the
 * card, from the manifest - each copy it names looked at once, so the cost
 * is the downloads, not the library. out: n flags, one per album. */
void msrv_albums_local(msrv *m, unsigned char *out, int n)
{
	char p[700], line[200], copy[1024];
	FILE *in;

	memset(out, 0, (size_t)n);
	manifest_path(m, p, sizeof p);
	pthread_mutex_lock(&m->man_mu);           /* rewritten by rename: this one stays whole */
	in = fopen(p, "r");
	pthread_mutex_unlock(&m->man_mu);
	if (!in) return;
	LOCK();
	while (fgets(line, sizeof line, in)) {
		int i = tab_find(&m->tab, atoi(line)), al = i >= 0 ? m->tab.tr[i].album : -1;

		if (al >= 0 && al < n && !out[al] && local_copy(m, &m->tab.tr[i], copy, sizeof copy)) out[al] = 1;
	}
	UNLOCK();
	fclose(in);
}

bool msrv_track_local(msrv *m, int t)
{
	char p[1024];
	bool yes;

	LOCK();
	yes = t >= 0 && t < m->tab.ntr && local_copy(m, &m->tab.tr[t], p, sizeof p);
	UNLOCK();
	return yes;
}

void msrv_download_album(msrv *m, int al)
{
	int i;

	LOCK();
	if (al >= 0 && al < m->tab.nal)
		for (i = m->tab.al[al].first; i < m->tab.al[al].first + m->tab.al[al].n; i++)
			dl_queue(m, m->tab.tr[i].key, 'a');
	UNLOCK();
	dl_start(m);
}

/* Remove a copy and the folders it leaves empty, up to Music/<music>, with
 * the covers Local Music cached for them (plorpos-5tm.14). */
static void remove_copy(msrv *m, const char *path)
{
	char d[1024], stop[600], *s;

	if (unlink(path) != 0) return;
	m->dl_changed = 1;
	snprintf(stop, sizeof stop, "%s/Music/%s", m->card, m->o->music);
	snprintf(d, sizeof d, "%s", path);
	/* The last track out takes the cover it brought. */
	if ((s = strrchr(d, '/'))) {
		DIR *dd;
		struct dirent *e;
		bool music = false;

		*s = '\0';
		if ((dd = opendir(d))) {
			while (!music && (e = readdir(dd))) music = ml_is_audio(e->d_name);
			closedir(dd);
		}
		if (!music) { char cov[1100]; snprintf(cov, sizeof cov, "%s/cover.jpg", d); unlink(cov); }
		snprintf(d, sizeof d, "%s", path);
	}
	if ((s = strrchr(d, '/')) && (*s = '\0', strcmp(d, stop) && strlen(d) > strlen(stop)) &&
	    rmdir(d) == 0)
		ml_prune_up(d, stop);
}

void msrv_delete_album(msrv *m, int al)
{
	char p[1024];
	int i, *keys = NULL, n = 0;

	LOCK();
	if (al >= 0 && al < m->tab.nal && (keys = malloc(sizeof *keys * (size_t)(m->tab.al[al].n + 1))))
		for (i = m->tab.al[al].first; i < m->tab.al[al].first + m->tab.al[al].n; i++) {
			if (local_copy(m, &m->tab.tr[i], p, sizeof p)) remove_copy(m, p);
			keys[n++] = m->tab.tr[i].key;
		}
	UNLOCK();
	for (i = 0; i < n; i++) { manifest_set(m, keys[i], 'a', false); manifest_set(m, keys[i], 'f', false); }
	free(keys);
}

/* Album al's copies that only its download wanted: a track that is also a
 * favourite keeps its copy. */
static void purge_album(msrv *m, int al)
{
	char p[1024];
	int i, *keys = NULL, n = 0;

	LOCK();
	if (al >= 0 && al < m->tab.nal && (keys = malloc(sizeof *keys * (size_t)(m->tab.al[al].n + 1))))
		for (i = m->tab.al[al].first; i < m->tab.al[al].first + m->tab.al[al].n; i++)
			keys[n++] = m->tab.tr[i].key;
	UNLOCK();
	for (i = 0; i < n; i++) {
		int t;

		manifest_set(m, keys[i], 'a', false);
		if (manifest_has(m, keys[i], 'f')) continue;
		LOCK();
		if ((t = tab_find(&m->tab, keys[i])) >= 0 && local_copy(m, &m->tab.tr[t], p, sizeof p))
			remove_copy(m, p);
		UNLOCK();
	}
	free(keys);
}

static bool in_album(msrv *m, int al, int key)
{
	int t = tab_find(&m->tab, key);

	return t >= m->tab.al[al].first && t < m->tab.al[al].first + m->tab.al[al].n;
}

bool msrv_album_downloading(msrv *m, int al)
{
	int i;
	bool yes = false;

	LOCK();
	if (al >= 0 && al < m->tab.nal)
		for (i = m->dl_pos; i < m->dlq_n && !yes; i++) yes = in_album(m, al, m->dlq[i].key);
	UNLOCK();
	return yes;
}

int msrv_download_queue(msrv *m, msrv_queued *out, int max)
{
	int i, j, n = 0;

	LOCK();
	for (i = m->dl_pos; i < m->dlq_n; i++) {
		int t = tab_find(&m->tab, m->dlq[i].key), al;

		if (t < 0 || (i == m->dl_pos && m->dl_cancel)) continue;   /* stopping */
		al = m->tab.tr[t].album;
		for (j = 0; j < n && out[j].al != al; j++) {}
		if (j < n) { out[j].tracks++; continue; }
		if (n == max) continue;
		out[n] = (msrv_queued){ .al = al, .tracks = 1, .now = i == m->dl_pos };
		snprintf(out[n].title, sizeof out[n].title, "%s", m->tab.al[al].title);
		snprintf(out[n].artist, sizeof out[n].artist, "%s", m->tab.ar[m->tab.al[al].artist].name);
		n++;
	}
	UNLOCK();
	return n;
}

void msrv_download_cancel_album(msrv *m, int al)
{
	int i, k;
	bool later = false;

	LOCK();
	if (al < 0 || al >= m->tab.nal) { UNLOCK(); return; }
	/* The queued ones out; the one in flight, if it is this album's, stops
	 * itself and the worker clears up after it. */
	for (i = k = m->dl_pos + 1; i < m->dlq_n; i++)
		if (in_album(m, al, m->dlq[i].key)) m->dl_total--;
		else m->dlq[k++] = m->dlq[i];
	if (m->dl_pos < m->dlq_n) {
		m->dlq_n = k;
		if (in_album(m, al, m->dlq[m->dl_pos].key)) {
			m->dl_skip = true; m->dl_cancel = 1; m->dl_purge = al; later = true;
		}
	}
	UNLOCK();
	if (!later) purge_album(m, al);
}

void msrv_download_cancel(msrv *m)
{
	LOCK();
	if (m->dl_pos < m->dlq_n) m->dl_cancel = 1;
	m->dl_skip = false;                    /* all of it, not one album's */
	m->dlq_n = m->dl_pos + (m->dl_pos < m->dlq_n ? 1 : 0);   /* the one in flight stops itself */
	UNLOCK();
}

bool msrv_download_status(msrv *m, char *out, size_t n)
{
	bool on;

	LOCK();
	on = m->dl_pos < m->dlq_n;
	if (on) snprintf(out, n, "%d of %d - %s", m->dl_pos + 1, m->dl_total, m->dl_now);
	UNLOCK();
	return on;
}

bool msrv_download_take_changed(msrv *m)
{
	if (!m->dl_changed) return false;
	m->dl_changed = 0;
	return true;
}

/* ---- playlists (plorpos-5eq.7) ---------------------------------------------------- *
 *
 * The source's playlists are .m3u files of its paths in msrv_pl_dir; this
 * keeps them the same as the server's. Pulled after every library sync: each
 * audio playlist on the server is written over its file (the server wins - an
 * edit made here was sent when it was made), one the server no longer has
 * goes, and one made here that the server has not got is created there. A
 * smart playlist is read only, and one longer than PLS_TRACKS_MAX (Plex's own
 * "All Music" is the whole library) is left out. Favorite Tracks stays this
 * device's own list. What is known of each is in the pl_tsv file beside them:
 * the server's id ("0" for none yet), smart or not, downloaded or not.
 *
 * A downloaded playlist's tracks are copied to Music/<music> as an album's
 * are (manifest reason 'p'; 'f', the old favorites, counts as 'p'), and listed
 * in <card>/Music/Playlists/<pl_pre><name>.m3u for the Local source. With
 * Keep Playlists Synced on, every pull brings the copies in line as well. */

int  msrv_pl_gen(msrv *m) { return m->pl_gen; }
void msrv_set_pl_mirror(msrv *m, bool on) { m->pl_mirror = on; }

void msrv_set_offline(msrv *m, bool on)
{
	if (on == m->offline) return;
	m->offline = on;
	fprintf(stderr, "%s: Offline Mode %s\n", m->o->tag, on ? "on" : "off");
	if (on) msrv_download_cancel(m);
	else m->plays_last = 0;                        /* plays made offline go at once */
}

bool msrv_get_offline(msrv *m) { return m->offline; }
const char *msrv_name(msrv *m) { return m->o->music; }
bool msrv_signed_in(msrv *m) { return m->o->signed_in(); }
bool msrv_ready(msrv *m) { return m->o->ready(); }
const char *msrv_library(msrv *m) { return m->o->library(); }

static void pls_file(msrv *m, char *out, size_t n)
{
	char d[700];

	msrv_pl_dir(m, d, sizeof d);
	snprintf(out, n, "%s/%s", d, m->o->pl_tsv);
}

static void pls_load_locked(msrv *m)
{
	char p[800], line[200];
	FILE *f;

	if (m->pls_in) return;
	m->pls_in = true;
	m->npls = 0;
	pls_file(m, p, sizeof p);
	if (!(f = fopen(p, "r"))) return;
	while (m->npls < PLS_MAX && fgets(line, sizeof line, f)) {
		ms_pl *e = &m->pls[m->npls];
		char id[48];
		int smart, dl, at = 0;

		line[strcspn(line, "\r\n")] = '\0';
		if (sscanf(line, "%47[^\t]\t%d\t%d\t%n", id, &smart, &dl, &at) < 3 || !at || !line[at]) continue;
		*e = (ms_pl){ .smart = smart != 0, .dl = dl != 0 };
		if (strcmp(id, "0")) snprintf(e->id, sizeof e->id, "%s", id);
		snprintf(e->name, sizeof e->name, "%s", line + at);
		m->npls++;
	}
	fclose(f);
}

static void pls_save_locked(msrv *m)
{
	char p[800], tmp[820], d[700];
	FILE *f;
	int i;

	msrv_pl_dir(m, d, sizeof d);
	mkdirs(d);
	pls_file(m, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) return;
	for (i = 0; i < m->npls; i++)
		fprintf(f, "%s\t%d\t%d\t%s\n", m->pls[i].id[0] ? m->pls[i].id : "0", m->pls[i].smart,
		        m->pls[i].dl, m->pls[i].name);
	if (fclose(f) == 0) rename(tmp, p);
	else unlink(tmp);
}

static ms_pl *pls_find_locked(msrv *m, const char *name)
{
	int i;

	pls_load_locked(m);
	for (i = 0; i < m->npls; i++) if (!strcmp(m->pls[i].name, name)) return &m->pls[i];
	return NULL;
}

static ms_pl *pls_get_locked(msrv *m, const char *name)
{
	ms_pl *e = pls_find_locked(m, name);

	if (e || m->npls == PLS_MAX) return e;
	e = &m->pls[m->npls++];
	*e = (ms_pl){ 0 };
	snprintf(e->name, sizeof e->name, "%s", name);
	return e;
}

static void pls_drop_locked(msrv *m, ms_pl *e)
{
	*e = m->pls[--m->npls];
}

bool msrv_pl_smart(msrv *m, const char *name)
{
	ms_pl *e;
	bool r;

	LOCK(); e = pls_find_locked(m, name); r = e && e->smart; UNLOCK();
	return r;
}

bool msrv_pl_downloaded(msrv *m, const char *name)
{
	ms_pl *e;
	bool r;

	LOCK(); e = pls_find_locked(m, name); r = e && e->dl; UNLOCK();
	return r;
}

/* The track keys of playlist `name`, in order, from its file: a path's last
 * part is "<key>.<ext>". The caller frees *out. */
static int pl_file_keys(msrv *m, const char *name, int **out)
{
	char d[700], **v = NULL;
	size_t rl = strlen(m->rel_root);
	int n, i, k2 = 0, *k;

	*out = NULL;
	msrv_pl_dir(m, d, sizeof d);
	if ((n = pl_read(d, name, m->card, &v)) <= 0) { if (n == 0) pl_free(v, n); return 0; }
	if ((k = malloc(sizeof *k * (size_t)n))) {
		for (i = 0; i < n; i++) {
			const char *sl = strrchr(v[i], '/');

			if (strncmp(v[i], m->rel_root, rl) || !sl || atoi(sl + 1) <= 0) continue;
			k[k2++] = atoi(sl + 1);
		}
	}
	pl_free(v, n);
	*out = k;
	return k ? k2 : 0;
}

/* Playlist `name`'s file, written whole from track keys; a key the index
 * does not have (another library's, or added since the last sync) is left
 * out. Card-relative as pl_add writes a path outside Music: "../../". */
static void pl_file_write(msrv *m, const char *name, const int *keys, int n)
{
	char d[700], p[900], tmp[920];
	FILE *f;
	int i, t;

	msrv_pl_dir(m, d, sizeof d);
	mkdirs(d);
	snprintf(p, sizeof p, "%s/%s.m3u", d, name);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) return;
	fputs("#EXTM3U\n", f);
	LOCK();
	for (i = 0; i < n; i++) {
		const msrv_track *tr;

		if ((t = tab_find(&m->tab, keys[i])) < 0) continue;
		tr = &m->tab.tr[t];
		fprintf(f, "../../%s/%d/%d/%d.%s\n", m->rel_root, m->tab.ar[m->tab.al[tr->album].artist].key,
		        m->tab.al[tr->album].key, tr->key, ext_of(tr->part));
	}
	UNLOCK();
	if (fclose(f) == 0) rename(tmp, p);
	else unlink(tmp);
}

static void pl_local_path(msrv *m, const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/" PL_DIR_REL "/%s%.95s.m3u", m->card, m->o->pl_pre, name);
}

/* The Local source's list of a downloaded playlist: the copies there are. */
static void pl_local_write(msrv *m, const char *name)
{
	char dir[700], p[900], tmp[920], f[1100], pre[700];
	size_t pl;
	FILE *o;
	int *keys, n = pl_file_keys(m, name, &keys), i, t;

	snprintf(dir, sizeof dir, "%s/" PL_DIR_REL, m->card);
	mkdirs(dir);
	pl_local_path(m, name, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	snprintf(pre, sizeof pre, "%s/Music/", m->card);
	pl = strlen(pre);
	if (!(o = fopen(tmp, "w"))) { free(keys); return; }
	fputs("#EXTM3U\n", o);
	LOCK();
	for (i = 0; i < n; i++)
		if ((t = tab_find(&m->tab, keys[i])) >= 0 && local_copy(m, &m->tab.tr[t], f, sizeof f) &&
		    !strncmp(f, pre, pl))
			fprintf(o, "../%s\n", f + pl);
	UNLOCK();
	free(keys);
	if (fclose(o) == 0) rename(tmp, p);
	else unlink(tmp);
}

static void pl_local_write_all(msrv *m)
{
	char names[PLS_MAX][96];
	int n = 0, i;

	LOCK();
	pls_load_locked(m);
	for (i = 0; i < m->npls; i++) if (m->pls[i].dl) memcpy(names[n++], m->pls[i].name, sizeof names[0]);
	UNLOCK();
	for (i = 0; i < n; i++) pl_local_write(m, names[i]);
	m->dl_changed = 1;
	m->pl_gen++;
}

static int cmp_keys(const void *x, const void *y) { return *(const int *)x - *(const int *)y; }

/* The job: the copies a downloaded playlist wants, queued; the copies no
 * downloaded playlist wants any more gone, unless an album download wants
 * them too; the Local lists written now and again when the downloads are in. */
static void do_pl_downloads(msrv *m)
{
	char names[PLS_MAX][96], mp[700], line[200], p[1024];
	int *want = NULL, nw = 0, cap = 0, n = 0, i, j, t;
	FILE *in;

	LOCK();
	pls_load_locked(m);
	for (i = 0; i < m->npls; i++) if (m->pls[i].dl) memcpy(names[n++], m->pls[i].name, sizeof names[0]);
	UNLOCK();
	for (i = 0; i < n; i++) {
		int *k, c = pl_file_keys(m, names[i], &k);

		if (nw + c > cap) {
			int *w = realloc(want, sizeof *w * (size_t)(cap = nw + c + 64));
			if (!w) { free(k); break; }
			want = w;
		}
		for (j = 0; j < c; j++) want[nw++] = k[j];
		free(k);
	}
	if (nw) qsort(want, (size_t)nw, sizeof *want, cmp_keys);
	LOCK();
	for (i = 0; i < nw; i++)
		if (i == 0 || want[i] != want[i - 1]) dl_queue(m, want[i], 'p');
	UNLOCK();
	fprintf(stderr, "%s: playlists: %d downloaded, %d tracks wanted\n", m->o->tag, n, nw);
	manifest_path(m, mp, sizeof mp);
	pthread_mutex_lock(&m->man_mu);
	in = fopen(mp, "r");
	pthread_mutex_unlock(&m->man_mu);
	if (in) {
		int *gone = NULL, ng = 0, gcap = 0;
		char *why = NULL;

		while (fgets(line, sizeof line, in)) {
			int k = atoi(line);
			char *tab = strchr(line, '\t');

			if (!tab || (tab[1] != 'p' && tab[1] != 'f')) continue;
			if (nw && bsearch(&k, want, (size_t)nw, sizeof *want, cmp_keys)) continue;
			if (ng == gcap) {
				int *g = realloc(gone, sizeof *g * (size_t)(gcap = gcap ? gcap * 2 : 64));
				char *w = realloc(why, (size_t)gcap);
				if (g) gone = g;
				if (w) why = w;
				if (!g || !w) break;
			}
			why[ng] = tab[1];
			gone[ng++] = k;
		}
		fclose(in);
		for (i = 0; i < ng; i++) {
			manifest_set(m, gone[i], why[i], false);
			if (manifest_has(m, gone[i], 'a')) continue;
			LOCK();
			if ((t = tab_find(&m->tab, gone[i])) >= 0 && local_copy(m, &m->tab.tr[t], p, sizeof p))
				remove_copy(m, p);
			UNLOCK();
		}
		free(gone);
		free(why);
	}
	free(want);
	/* The list the old Download Favorite Tracks wrote, on both sources. */
	snprintf(p, sizeof p, "%s/" PL_DIR_REL "/%sFavorites.m3u", m->card, m->o->pl_pre);
	unlink(p);
	msrv_pl_dir(m, mp, sizeof mp);
	snprintf(p, sizeof p, "%s/%sFavorites.m3u", mp, m->o->pl_pre);
	unlink(p);
	pl_local_write_all(m);
	LOCK();
	m->pl_m3u = true;
	UNLOCK();
	dl_start(m);
}

void msrv_pl_download(msrv *m, const char *name, bool on)
{
	ms_pl *e;
	msrv_job *j;

	LOCK();
	if ((e = pls_get_locked(m, name))) { e->dl = on; pls_save_locked(m); }
	UNLOCK();
	if (!on) {
		char p[900];

		pl_local_path(m, name, p, sizeof p);
		unlink(p);
	}
	m->pl_gen++;
	if ((j = msrv_job_new(J_PLDL))) msrv_push(m, j, false);
}

/* Playlist `name` made on the server from its file here; false when not. */
static bool pl_create(msrv *m, const char *name, char *id, size_t idn)
{
	int *keys, n = pl_file_keys(m, name, &keys);
	bool ok;

	id[0] = '\0';
	if (n <= 0) { free(keys); return false; }       /* Plex makes none empty */
	ok = m->o->pl_create(m, name, keys, n, id, idn) && id[0];
	free(keys);
	if (ok) fprintf(stderr, "%s: playlist %s made on the server (%d tracks)\n", m->o->tag, name, n);
	return ok;
}

/* The pull, and the push of what was made here. */
static void do_pls(msrv *m)
{
	typedef struct { char id[48]; int leaf; bool smart; char name[96]; } spl;
	char d[700], p[900];
	msrv_spl *raw;
	spl *sv;
	pl_info *have;
	int nraw, ns = 0, nh, i, j;

	if (!m->tab.ntr) return;                    /* no index yet: its sync comes first */
	if (!(raw = calloc(PLS_MAX * 4, sizeof *raw))) return;
	if ((nraw = m->o->pl_list(m, raw, PLS_MAX * 4)) < 0) {
		free(raw);
		fprintf(stderr, "%s: playlists: the server did not answer\n", m->o->tag);
		return;
	}
	if (!(sv = calloc(PLS_MAX, sizeof *sv)) || !(have = calloc(PLS_MAX, sizeof *have))) { free(sv); free(raw); return; }
	for (i = 0; i < nraw && ns < PLS_MAX; i++) {
		snprintf(sv[ns].id, sizeof sv[ns].id, "%s", raw[i].id);
		sv[ns].leaf = raw[i].leaf;
		sv[ns].smart = raw[i].smart;
		safe_name(raw[i].title, sv[ns].name, 90);
		if (!sv[ns].id[0] || !sv[ns].name[0] || !strcmp(sv[ns].name, MSRV_FAVS)) continue;
		if (sv[ns].leaf > PLS_TRACKS_MAX) {
			fprintf(stderr, "%s: playlist %s left out (%d tracks)\n", m->o->tag, sv[ns].name, sv[ns].leaf);
			continue;
		}
		ns++;
	}
	free(raw);
	msrv_pl_dir(m, d, sizeof d);
	/* The server's, each over its file. */
	for (i = 0; i < ns; i++) {
		int *keys, n = m->o->pl_items(m, sv[i].id, &keys);
		ms_pl *pe;
		char was[96] = "";

		if (n < 0) { free(keys); continue; }
		LOCK();
		pls_load_locked(m);
		for (j = 0; j < m->npls; j++)
			if (!strcmp(m->pls[j].id, sv[i].id) && strcmp(m->pls[j].name, sv[i].name)) {
				memcpy(was, m->pls[j].name, sizeof was);               /* renamed there */
				memcpy(m->pls[j].name, sv[i].name, sizeof m->pls[j].name);
			}
		if ((pe = pls_get_locked(m, sv[i].name))) { memcpy(pe->id, sv[i].id, sizeof pe->id); pe->smart = sv[i].smart; }
		UNLOCK();
		if (was[0]) {
			snprintf(p, sizeof p, "%s/%s.m3u", d, was);
			unlink(p);
			pl_local_path(m, was, p, sizeof p);
			unlink(p);
		}
		pl_file_write(m, sv[i].name, keys, n);
		free(keys);
	}
	/* Gone from the server: gone here. */
	LOCK();
	pls_load_locked(m);
	for (j = 0; j < m->npls; ) {
		if (m->pls[j].id[0]) {
			for (i = 0; i < ns && strcmp(sv[i].id, m->pls[j].id); i++) {}
			if (i == ns) {
				char gone[96];

				memcpy(gone, m->pls[j].name, sizeof gone);
				pls_drop_locked(m, &m->pls[j]);
				UNLOCK();
				snprintf(p, sizeof p, "%s/%s.m3u", d, gone);
				unlink(p);
				pl_local_path(m, gone, p, sizeof p);
				unlink(p);
				fprintf(stderr, "%s: playlist %s is gone from the server\n", m->o->tag, gone);
				LOCK();
				continue;
			}
		}
		j++;
	}
	UNLOCK();
	/* Made here, not there yet. */
	nh = pl_list(d, have, PLS_MAX);
	for (i = 0; i < nh; i++) {
		ms_pl *pe;
		char id[48];
		bool known;

		if (!strcmp(have[i].name, MSRV_FAVS)) continue;
		LOCK(); pe = pls_find_locked(m, have[i].name); known = pe && pe->id[0]; UNLOCK();
		if (known || !pl_create(m, have[i].name, id, sizeof id)) continue;
		LOCK();
		if ((pe = pls_get_locked(m, have[i].name))) snprintf(pe->id, sizeof pe->id, "%s", id);
		UNLOCK();
	}
	LOCK();
	pls_save_locked(m);
	UNLOCK();
	free(sv);
	free(have);
	m->pl_gen++;
	fprintf(stderr, "%s: playlists: %d on the server\n", m->o->tag, ns);
	if (m->pl_mirror) do_pl_downloads(m);
}

void msrv_playlists_sync(msrv *m)
{
	msrv_job *j = msrv_job_new(J_PLS);

	if (j) msrv_push(m, j, false);
}

/* An edit made here, sent: by track key, not by place - the server's list
 * may hold tracks this library's index has not. */
static void do_pl_edit(msrv *m, msrv_job *j)
{
	int op = (int)j->n1, key = (int)j->n3, code = 200;

	if (op == MSRV_PLE_ADD && !j->id[0]) {
		ms_pl *pe;
		char id[48];

		if (!pl_create(m, j->a, id, sizeof id)) code = 0;
		else {
			LOCK();
			if ((pe = pls_get_locked(m, j->a))) { snprintf(pe->id, sizeof pe->id, "%s", id); pls_save_locked(m); }
			UNLOCK();
		}
	} else if (j->id[0]) {
		code = m->o->pl_edit(m, op, j->id, j->b, key, op == MSRV_PLE_MOVE ? atoi(j->b) : 0);
	}
	if (code != 200) {
		fprintf(stderr, "%s: a playlist change did not reach the server (http %d)\n", m->o->tag, code);
		m->dl_fail_note = 1;
	}
}

static void pl_edit(msrv *m, int op, const char *name, const char *b, int key)
{
	ms_pl *e;
	msrv_job *j = msrv_job_new(J_PLEDIT);

	if (!j) return;
	LOCK();
	e = pls_find_locked(m, name);
	j->n1 = op;
	snprintf(j->id, sizeof j->id, "%s", e ? e->id : "");
	j->n3 = key;
	snprintf(j->a, sizeof j->a, "%s", name);
	snprintf(j->b, sizeof j->b, "%s", b ? b : "");
	if (e && op == MSRV_PLE_RENAME) { snprintf(e->name, sizeof e->name, "%.95s", b); pls_save_locked(m); }
	if (e && op == MSRV_PLE_DELETE) { pls_drop_locked(m, e); pls_save_locked(m); }
	UNLOCK();
	if (op == MSRV_PLE_RENAME || op == MSRV_PLE_DELETE) {
		char p[900], q[900];

		pl_local_path(m, name, p, sizeof p);
		if (op == MSRV_PLE_RENAME) { pl_local_path(m, b, q, sizeof q); rename(p, q); }
		else unlink(p);
	}
	msrv_push(m, j, false);
}

static int rel_key(msrv *m, const char *rel)
{
	const char *sl = rel ? strrchr(rel, '/') : NULL;

	return sl && msrv_is_path(m, rel) ? atoi(sl + 1) : 0;
}

void msrv_pl_added(msrv *m, const char *name, const char *track)
{
	if (strcmp(name, MSRV_FAVS) && rel_key(m, track)) pl_edit(m, MSRV_PLE_ADD, name, NULL, rel_key(m, track));
}

void msrv_pl_removed(msrv *m, const char *name, const char *track)
{
	if (strcmp(name, MSRV_FAVS) && rel_key(m, track)) pl_edit(m, MSRV_PLE_DEL, name, NULL, rel_key(m, track));
}

void msrv_pl_moved(msrv *m, const char *name, const char *track, const char *after)
{
	char ak[16];

	if (!strcmp(name, MSRV_FAVS) || !rel_key(m, track)) return;
	snprintf(ak, sizeof ak, "%d", rel_key(m, after));
	pl_edit(m, MSRV_PLE_MOVE, name, ak, rel_key(m, track));
}

void msrv_pl_renamed(msrv *m, const char *from, const char *to)
{
	if (strcmp(from, MSRV_FAVS)) pl_edit(m, MSRV_PLE_RENAME, from, to, 0);
}

void msrv_pl_deleted(msrv *m, const char *name)
{
	if (strcmp(name, MSRV_FAVS)) pl_edit(m, MSRV_PLE_DELETE, name, NULL, 0);
}
