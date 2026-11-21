/* SPDX-License-Identifier: MIT */
/* See abs.h. The API is the one the owner's audiobookshelf-app fork speaks
 * (~/projects/git/audiobookshelf-app): /status, /login with x-return-tokens,
 * /auth/refresh with x-refresh-token, /api/libraries/<id>/items,
 * /api/items/batch/get, /api/items/<id>/file/<ino>, /api/me and
 * /api/me/progress/<id>. Measured against server 2.37.2, 2026-11-20. */
#include "abs.h"

#include <ctype.h>
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
#include "rajson.h"

#define BATCH      20          /* books per batch/get: ~3 MB of JSON each */
#define COVER_PX   500
#define PUSH_MS    15000       /* a place sent every 15 s while playing */
#define PULL_MS    5000        /* the server's places asked at most this often */
#define SEEN_MS    (5 * 60 * 1000)
#define LANDED     32

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_refresh_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_mu)
#define UNLOCK() pthread_mutex_unlock(&g_mu)

static char g_card[512], g_dir[600];     /* <shared>/abs */
static char g_rel_base[256];             /* card-relative <shared>/abs */
static char g_rel_root[400];             /* card-relative root of this library's books */

/* ---- the config (P_SHARED/abs/config, 0600) ---------------------------------------- */

static struct {
	char server[256], user[128];
	char access[1024], refresh[1024], legacy[1024];
	char lib_id[64], lib_name[128];
} C;

static unsigned now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void mkdirs(const char *path)
{
	char p[1024];
	char *s;

	snprintf(p, sizeof p, "%s", path);
	for (s = p + 1; *s; s++)
		if (*s == '/') { *s = '\0'; mkdir(p, 0755); *s = '/'; }
	mkdir(p, 0755);
}

static bool exists(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0;
}

static void rel_root_set(void)
{
	if (C.lib_id[0])
		snprintf(g_rel_root, sizeof g_rel_root, "%s/%.8s", g_rel_base, C.lib_id);
	else
		g_rel_root[0] = '\0';
}

/* Under g_mu. */
static void cfg_save_locked(void)
{
	char p[700], tmp[720];
	FILE *f;
	int fd;

	snprintf(p, sizeof p, "%s/config", g_dir);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	mkdirs(g_dir);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) return;
	if (!(f = fdopen(fd, "w"))) { close(fd); return; }
	fprintf(f, "server=%s\nuser=%s\naccess=%s\nrefresh=%s\nlegacy=%s\nlib_id=%s\nlib_name=%s\n",
	        C.server, C.user, C.access, C.refresh, C.legacy, C.lib_id, C.lib_name);
	fflush(f);
	fsync(fd);
	fclose(f);
	rename(tmp, p);
}

static void cfg_load(void)
{
	char p[700], line[1200];
	FILE *f;

	memset(&C, 0, sizeof C);
	snprintf(p, sizeof p, "%s/config", g_dir);
	if ((f = fopen(p, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char *v = strchr(line, '=');

			line[strcspn(line, "\r\n")] = '\0';
			if (!v) continue;
			*v++ = '\0';
#define F(k) else if (!strcmp(line, #k)) snprintf(C.k, sizeof C.k, "%s", v)
			if (0) {}
			F(server); F(user); F(access); F(refresh); F(legacy); F(lib_id); F(lib_name);
#undef F
		}
		fclose(f);
	}
	rel_root_set();
}

/* ---- small helpers ----------------------------------------------------------------- */

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

static void jstr(jsv o, const char *k, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	if (js_member(o, k, &v)) js_str(v, out, n);
}

static double jnum(jsv o, const char *k)
{
	jsv v;

	return js_member(o, k, &v) ? js_num(v) : 0;
}

/* Into a JSON string's body: quotes, backslashes and control characters. */
static void jesc(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 7 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
		else if (c < 0x20) o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
		else out[o++] = (char)c;
	}
	out[o] = '\0';
}

/* Tabs and newlines out of a field the index keeps. */
static void clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static void tmpfile_for(const char *tag, char *out, size_t n)
{
	static unsigned seq;
	unsigned k;

	LOCK(); k = ++seq; UNLOCK();
	snprintf(out, n, "/tmp/tortos-abs-%s-%ld-%u", tag, (long)getpid(), k);
}

/* ---- requests ------------------------------------------------------------------------ */

static volatile int g_lost;              /* the refresh token was refused: sign in again */

static bool parse_tokens(const char *body, size_t len)
{
	jsv root = js_root(body, len), user, v;
	char acc[sizeof C.access], ref[sizeof C.refresh];

	if (!js_member(root, "user", &user)) return false;
	jstr(user, "accessToken", acc, sizeof acc);
	jstr(user, "refreshToken", ref, sizeof ref);
	if (!acc[0]) return false;
	LOCK();
	snprintf(C.access, sizeof C.access, "%s", acc);
	if (ref[0]) snprintf(C.refresh, sizeof C.refresh, "%s", ref);
	if (js_member(user, "token", &v)) js_str(v, C.legacy, sizeof C.legacy);
	cfg_save_locked();
	UNLOCK();
	return true;
}

/* A new access token for the one `used`, unless another thread got one
 * already. False when the refresh token is refused (then signed out) or the
 * server did not answer. */
static bool refresh_tokens(const char *used)
{
	char url[400], hdr[1100], tmp[128], ref[sizeof C.refresh];
	const char *h[2];
	char *body;
	size_t len;
	int code;
	bool ok = false;

	pthread_mutex_lock(&g_refresh_mu);
	LOCK();
	if (strcmp(used, C.access)) { UNLOCK(); pthread_mutex_unlock(&g_refresh_mu); return true; }
	snprintf(url, sizeof url, "%s/auth/refresh", C.server);
	snprintf(ref, sizeof ref, "%s", C.refresh);
	UNLOCK();
	if (!ref[0]) { pthread_mutex_unlock(&g_refresh_mu); return false; }
	snprintf(hdr, sizeof hdr, "x-refresh-token: %s", ref);
	h[0] = hdr;
	h[1] = "Content-Type: application/json";
	tmpfile_for("ref", tmp, sizeof tmp);
	code = net_fetch_body(url, "POST", h, 2, "{}", tmp, 5, 15, NULL);
	if (code == 200 && (body = slurp(tmp, &len))) {
		ok = parse_tokens(body, len);
		free(body);
	} else if (code == 401) {
		LOCK();
		C.access[0] = C.refresh[0] = '\0';
		cfg_save_locked();
		UNLOCK();
		g_lost = 1;
		fprintf(stderr, "abs: the server refused the refresh token; sign in again\n");
	}
	unlink(tmp);
	pthread_mutex_unlock(&g_refresh_mu);
	return ok;
}

/* `path` on the server, with the account's access token, the reply into memory
 * (NUL-terminated). The HTTP status, 0 for no answer, -1 when it could not run. */
static int req(const char *method, const char *path, const char *json,
               char **body, size_t *len, int timeout_s)
{
	char url[1400], auth[1100], tmp[128], used[sizeof C.access];
	const char *h[3];
	int code = -1, nh, attempt;

	if (body) *body = NULL;
	for (attempt = 0; attempt < 2; attempt++) {
		LOCK();
		snprintf(url, sizeof url, "%s%s", C.server, path);
		snprintf(used, sizeof used, "%s", C.access);
		UNLOCK();
		if (!used[0]) return 401;
		snprintf(auth, sizeof auth, "Authorization: Bearer %s", used);
		nh = 0;
		h[nh++] = auth;
		h[nh++] = "Accept: application/json";
		if (json) h[nh++] = "Content-Type: application/json";
		tmpfile_for("req", tmp, sizeof tmp);
		code = net_fetch_body(url, method, h, nh, json, tmp, 5, timeout_s, NULL);
		if (code == 401 && attempt == 0 && refresh_tokens(used)) { unlink(tmp); continue; }
		if (body && code >= 200 && code < 300) {
			*body = slurp(tmp, len);
			if (!*body) code = -1;
		}
		unlink(tmp);
		break;
	}
	return code;
}

/* ---- jobs ---------------------------------------------------------------------------- */

enum { J_CHECK, J_LOGIN, J_COVER, J_PUSH, J_PULL, J_LOAD, J_PLACE };
typedef struct job {
	int kind;
	char a[700], b[700];
	double cur, dur;
	bool fin;
	struct job *next;
} job;

static job *g_jobs;
static int g_njobs;

static job *job_new(int kind)
{
	job *j = calloc(1, sizeof *j);

	if (j) j->kind = kind;
	return j;
}

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
	g_njobs++;
	pthread_cond_signal(&g_cv);
	UNLOCK();
}

/* ---- sign-in ------------------------------------------------------------------------- */

static volatile int g_auth = 0;          /* 0 idle/waiting, 1 done, -1 failed */
static char g_auth_why[96], g_checked[256];

static void auth_fail(const char *why)
{
	LOCK(); snprintf(g_auth_why, sizeof g_auth_why, "%s", why); UNLOCK();
	g_auth = -1;
}

static void do_check(job *j)
{
	char url[800], tmp[128], app[64];
	const char *h[1] = { "Accept: application/json" };
	char *body;
	size_t len;
	int code;

	snprintf(url, sizeof url, "%s/status", j->a);
	tmpfile_for("chk", tmp, sizeof tmp);
	code = net_fetch(url, "GET", h, 1, tmp, 5, 10, NULL);
	body = code == 200 ? slurp(tmp, &len) : NULL;
	unlink(tmp);
	if (!body) { auth_fail(code == 0 ? "No answer at that address" : "Not an Audiobookshelf server"); return; }
	jstr(js_root(body, len), "app", app, sizeof app);
	free(body);
	if (strcasecmp(app, "audiobookshelf")) { auth_fail("Not an Audiobookshelf server"); return; }
	LOCK(); snprintf(g_checked, sizeof g_checked, "%.255s", j->a); UNLOCK();
	g_auth = 1;
}

/* The account's book library: its default when that is one, else the first. */
static bool pick_library(const char *dflt)
{
	char *body;
	size_t len;
	jsv libs, it = {0}, e;
	char id[64], name[128], type[16], first_id[64] = "", first_name[128] = "";
	bool found = false;

	if (req("GET", "/api/libraries", NULL, &body, &len, 15) != 200) return false;
	if (js_member(js_root(body, len), "libraries", &libs))
		while (js_next(libs, &it, &e)) {
			jstr(e, "id", id, sizeof id);
			jstr(e, "name", name, sizeof name);
			jstr(e, "mediaType", type, sizeof type);
			if (strcmp(type, "book")) continue;
			if (!first_id[0]) { snprintf(first_id, sizeof first_id, "%s", id); snprintf(first_name, sizeof first_name, "%s", name); }
			if (!strcmp(id, dflt)) { found = true; break; }
		}
	free(body);
	if (!found) { snprintf(id, sizeof id, "%s", first_id); snprintf(name, sizeof name, "%s", first_name); }
	if (!id[0]) return false;
	LOCK();
	snprintf(C.lib_id, sizeof C.lib_id, "%s", id);
	snprintf(C.lib_name, sizeof C.lib_name, "%s", name);
	rel_root_set();
	cfg_save_locked();
	UNLOCK();
	return true;
}

static void do_login(job *j)
{
	char url[400], tmp[128], body_s[800], ue[300], pe[400], dflt[64], name[128];
	const char *h[3] = { "Content-Type: application/json", "Accept: application/json",
	                     "x-return-tokens: true" };
	char *body;
	size_t len;
	int code;

	LOCK(); snprintf(url, sizeof url, "%s/login", g_checked); UNLOCK();
	jesc(j->a, ue, sizeof ue);
	jesc(j->b, pe, sizeof pe);
	memset(j->b, 0, sizeof j->b);
	snprintf(body_s, sizeof body_s, "{\"username\":\"%s\",\"password\":\"%s\"}", ue, pe);
	memset(pe, 0, sizeof pe);
	tmpfile_for("login", tmp, sizeof tmp);
	code = net_fetch_body(url, "POST", h, 3, body_s, tmp, 5, 15, NULL);
	memset(body_s, 0, sizeof body_s);
	body = code == 200 ? slurp(tmp, &len) : NULL;
	unlink(tmp);
	if (!body) {
		auth_fail(code == 401 ? "Wrong username or password" : code == 0 ? "The server did not answer"
		                      : "The server refused the sign-in");
		return;
	}
	LOCK();
	snprintf(C.server, sizeof C.server, "%s", g_checked);
	UNLOCK();
	if (!parse_tokens(body, len)) { free(body); auth_fail("The server sent no token"); return; }
	jstr(js_root(body, len), "userDefaultLibraryId", dflt, sizeof dflt);
	{
		jsv user;
		if (js_member(js_root(body, len), "user", &user)) jstr(user, "username", name, sizeof name);
		else snprintf(name, sizeof name, "%.127s", j->a);
	}
	free(body);
	LOCK(); snprintf(C.user, sizeof C.user, "%s", name); cfg_save_locked(); UNLOCK();
	g_lost = 0;
	if (!pick_library(dflt)) { auth_fail("No audiobook library on that server"); return; }
	fprintf(stderr, "abs: signed in to %s, library %s\n", C.server, C.lib_name);
	g_auth = 1;
}

void abs_check_start(const char *address)
{
	job *j = job_new(J_CHECK);
	char a[600];
	size_t n;

	/* "192.168.0.2:13378" is enough: the scheme is added, a trailing / goes. */
	snprintf(a, sizeof a, "%s%s", strstr(address, "://") ? "" : "http://", address);
	for (n = strlen(a); n && (a[n - 1] == '/' || a[n - 1] == ' '); n--) a[n - 1] = '\0';
	g_auth = 0;
	if (!j) { auth_fail("Out of memory"); return; }
	snprintf(j->a, sizeof j->a, "%s", a);
	push(j, true);
}

void abs_login_start(const char *user, const char *password)
{
	job *j = job_new(J_LOGIN);

	g_auth = 0;
	if (!j) { auth_fail("Out of memory"); return; }
	snprintf(j->a, sizeof j->a, "%s", user);
	snprintf(j->b, sizeof j->b, "%s", password);
	push(j, true);
}

int abs_auth_poll(char *why, size_t n)
{
	LOCK(); snprintf(why, n, "%s", g_auth_why); UNLOCK();
	return g_auth;
}

const char *abs_checked_address(void) { return g_checked; }

/* ---- the book table ------------------------------------------------------------------ */

typedef struct { char *ino, *ext, *title; double dur; } ab_file;
typedef struct {
	char *id, *author, *title;
	long long upd;
	int year, first, n;
	bool cover;
	double dur;
} ab_book;
typedef struct { ab_book *b; int nb, cb; ab_file *f; int nf, cf; } ab_table;

static ab_table g_tab;                   /* under g_mu, sorted by author then title */
static bool g_tab_read;

static void tab_free(ab_table *t)
{
	int i;

	for (i = 0; i < t->nb; i++) { free(t->b[i].id); free(t->b[i].author); free(t->b[i].title); }
	for (i = 0; i < t->nf; i++) { free(t->f[i].ino); free(t->f[i].ext); free(t->f[i].title); }
	free(t->b); free(t->f);
	memset(t, 0, sizeof *t);
}

static ab_book *tab_add_book(ab_table *t)
{
	if (t->nb == t->cb) {
		int c = t->cb ? t->cb * 2 : 256;
		ab_book *n = realloc(t->b, sizeof *n * (size_t)c);
		if (!n) return NULL;
		t->b = n; t->cb = c;
	}
	memset(&t->b[t->nb], 0, sizeof t->b[0]);
	t->b[t->nb].first = t->nf;
	return &t->b[t->nb++];
}

static ab_file *tab_add_file(ab_table *t)
{
	if (t->nf == t->cf) {
		int c = t->cf ? t->cf * 2 : 1024;
		ab_file *n = realloc(t->f, sizeof *n * (size_t)c);
		if (!n) return NULL;
		t->f = n; t->cf = c;
	}
	memset(&t->f[t->nf], 0, sizeof t->f[0]);
	t->b[t->nb - 1].n++;
	return &t->f[t->nf++];
}

static void index_path(char *out, size_t n)
{
	snprintf(out, n, "%s/%.8s/index.tsv", g_dir, C.lib_id);
}

static int split(char *s, char **f, int max)
{
	int n = 0;

	f[n++] = s;
	while (n < max && (s = strchr(s, '\t'))) { *s++ = '\0'; f[n++] = s; }
	return n;
}

static const ab_table *g_sort_tab;
static int cmp_book(const void *pa, const void *pb)
{
	const ab_book *a = &g_sort_tab->b[*(const int *)pa], *b = &g_sort_tab->b[*(const int *)pb];
	int c = strcasecmp(a->author, b->author);

	return c ? c : strcasecmp(a->title, b->title);
}

/* The index as a table, its books sorted by author then title with their
 * files after each, so a book's number is its album's in the ml_lib. */
static bool tab_read(ab_table *out)
{
	char p[700], *buf, *line, *next, *f[8];
	ab_table t = {0};
	int *ord, i, k;

	memset(out, 0, sizeof *out);
	index_path(p, sizeof p);
	if (!(buf = slurp(p, NULL))) return false;
	if (strncmp(buf, "ABS1\n", 5)) { free(buf); return false; }
	for (line = buf + 5; *line; line = next) {
		next = strchr(line, '\n');
		if (next) *next++ = '\0'; else next = line + strlen(line);
		if (line[0] == 'B' && line[1] == '\t' && split(line + 2, f, 7) == 7) {
			ab_book *b = tab_add_book(&t);
			if (!b) break;
			b->id = strdup(f[0]); b->upd = atoll(f[1]); b->cover = f[2][0] == '1';
			b->year = atoi(f[3]); b->dur = atof(f[4]);
			b->author = strdup(f[5]); b->title = strdup(f[6]);
		} else if (line[0] == 'F' && line[1] == '\t' && t.nb && split(line + 2, f, 4) == 4) {
			ab_file *x = tab_add_file(&t);
			if (!x) break;
			x->ino = strdup(f[0]); x->ext = strdup(f[1]); x->dur = atof(f[2]); x->title = strdup(f[3]);
		}
	}
	free(buf);
	/* Sorted: books by author and title, files moved along with them. */
	if (!(ord = malloc(sizeof *ord * (size_t)(t.nb ? t.nb : 1)))) { tab_free(&t); return false; }
	for (i = 0; i < t.nb; i++) ord[i] = i;
	g_sort_tab = &t;
	qsort(ord, (size_t)t.nb, sizeof *ord, cmp_book);
	{
		ab_table s = {0};

		s.b = malloc(sizeof *s.b * (size_t)(t.nb ? t.nb : 1));
		s.f = malloc(sizeof *s.f * (size_t)(t.nf ? t.nf : 1));
		if (!s.b || !s.f) { free(s.b); free(s.f); free(ord); tab_free(&t); return false; }
		s.cb = t.nb; s.cf = t.nf;
		for (i = 0; i < t.nb; i++) {
			ab_book *b = &t.b[ord[i]];

			s.b[s.nb] = *b;
			s.b[s.nb].first = s.nf;
			for (k = 0; k < b->n; k++) s.f[s.nf++] = t.f[b->first + k];
			s.nb++;
		}
		free(t.b); free(t.f);
		t = s;
	}
	free(ord);
	*out = t;
	return true;
}

/* Under g_mu. */
static int tab_find(const ab_table *t, const char *id)
{
	for (int i = 0; i < t->nb; i++) if (!strcmp(t->b[i].id, id)) return i;
	return -1;
}

static void tab_ensure(void)
{
	ab_table t;

	if (g_tab_read || !C.lib_id[0]) return;
	if (tab_read(&t)) { LOCK(); tab_free(&g_tab); g_tab = t; UNLOCK(); }
	g_tab_read = true;
}

/* ---- the library --------------------------------------------------------------------- */

bool abs_signed_in(void) { return C.access[0] && C.server[0] && C.lib_id[0]; }
const char *abs_server(void) { return C.server; }
const char *abs_user(void) { return C.user; }
const char *abs_library_name(void) { return C.lib_name; }

bool abs_lib_cached(void)
{
	char p[700];

	if (!C.lib_id[0]) return false;
	index_path(p, sizeof p);
	return exists(p);
}

bool abs_lib_load(ml_lib *out)
{
	ab_table t;
	int i, j, na = 0;

	memset(out, 0, sizeof *out);
	if (!C.lib_id[0] || !tab_read(&t)) return false;
	out->artists = calloc((size_t)(t.nb ? t.nb : 1), sizeof *out->artists);
	out->albums  = calloc((size_t)(t.nb ? t.nb : 1), sizeof *out->albums);
	out->tracks  = calloc((size_t)(t.nf ? t.nf : 1), sizeof *out->tracks);
	if (!out->artists || !out->albums || !out->tracks) { ml_free(out); tab_free(&t); return false; }
	for (i = 0; i < t.nb; i++) {
		ab_book *b = &t.b[i];
		ml_album *al = &out->albums[i];

		if (!na || strcasecmp(out->artists[na - 1].name, b->author)) {
			ml_artist *ar = &out->artists[na++];
			snprintf(ar->name, sizeof ar->name, "%s", b->author[0] ? b->author : "Unknown Author");
			ar->first = i;
			ar->book = true;
		}
		out->artists[na - 1].n++;
		snprintf(al->name, sizeof al->name, "%s", b->title);
		al->first = b->first;
		al->n = b->n;
		al->book = true;
		for (j = 0; j < b->n; j++) {
			ab_file *f = &t.f[b->first + j];

			snprintf(out->tracks[b->first + j].name, sizeof out->tracks[0].name, "%s", f->title);
			snprintf(out->tracks[b->first + j].path, sizeof out->tracks[0].path, "%s/%s/%03d%s",
			         g_rel_root, b->id, j + 1, f->ext);
		}
	}
	out->nartists = na;
	out->nalbums = t.nb;
	out->ntracks = t.nf;
	LOCK();
	tab_free(&g_tab);
	g_tab = t;
	g_tab_read = true;
	UNLOCK();
	fprintf(stderr, "abs: %d authors, %d books, %d files from the index\n", na, t.nb, t.nf);
	return true;
}

int abs_album_year(int al)
{
	int y;

	LOCK();
	y = al >= 0 && al < g_tab.nb ? g_tab.b[al].year : 0;
	UNLOCK();
	return y;
}

bool abs_is_path(const char *rel)
{
	size_t n = strlen(g_rel_base);

	return rel && n && !strncmp(rel, g_rel_base, n) && rel[n] == '/';
}

void abs_pl_dir(char *out, size_t n) { snprintf(out, n, "%s/%s/Playlists", g_card, g_rel_root); }

/* ---- sync ------------------------------------------------------------------------------ */

static volatile int g_sync_run, g_sync_failed, g_sync_done, g_sync_total, g_sync_new;
static volatile bool g_sync_force;

typedef struct { char id[64]; long long upd; } ab_item;

/* One book of a batch/get answer, written as the index's lines. */
static void write_item(FILE *o, jsv it)
{
	jsv media, meta, files, e, fit = {0}, v;
	char id[64], title[512], author[512], year[16], cover[8];
	long long upd = (long long)jnum(it, "updatedAt");

	jstr(it, "id", id, sizeof id);
	if (!js_member(it, "media", &media) || !js_member(media, "metadata", &meta)) return;
	jstr(meta, "title", title, sizeof title);
	jstr(meta, "authorName", author, sizeof author);
	jstr(meta, "publishedYear", year, sizeof year);
	cover[0] = js_member(media, "coverPath", &v) && js_str(v, cover + 1, sizeof cover - 1) ? '1' : '0';
	clean(title); clean(author);
	fprintf(o, "B\t%s\t%lld\t%c\t%d\t%.3f\t%s\t%s\n", id, upd, cover[0], atoi(year),
	        jnum(media, "duration"), author, title);
	if (!js_member(media, "audioFiles", &files)) return;
	while (js_next(files, &fit, &e)) {
		jsv fm, tags;
		char ino[48], ext[16], name[256];

		if (js_member(e, "exclude", &v) && js_is_true(v)) continue;
		jstr(e, "ino", ino, sizeof ino);
		if (!js_member(e, "metadata", &fm)) continue;
		jstr(fm, "ext", ext, sizeof ext);
		name[0] = '\0';
		if (js_member(e, "metaTags", &tags)) jstr(tags, "tagTitle", name, sizeof name);
		if (!name[0]) {
			char fn[256], *dot;
			jstr(fm, "filename", fn, sizeof fn);
			if ((dot = strrchr(fn, '.'))) *dot = '\0';
			snprintf(name, sizeof name, "%s", fn);
		}
		clean(name); clean(ext);
		fprintf(o, "F\t%s\t%s\t%.3f\t%s\n", ino, ext, jnum(e, "duration"), name);
	}
}

static void write_book(FILE *o, const ab_table *t, int i)
{
	const ab_book *b = &t->b[i];

	fprintf(o, "B\t%s\t%lld\t%c\t%d\t%.3f\t%s\t%s\n", b->id, b->upd, b->cover ? '1' : '0',
	        b->year, b->dur, b->author, b->title);
	for (int k = 0; k < b->n; k++) {
		const ab_file *f = &t->f[b->first + k];
		fprintf(o, "F\t%s\t%s\t%.3f\t%s\n", f->ino, f->ext, f->dur, f->title);
	}
}

static void pull_progress(void);

static void *sync_main(void *arg)
{
	char path[200], p[700], tmp[720];
	char *body = NULL, *bodyb;
	size_t len, lenb;
	ab_item *items = NULL;
	int nitems = 0, cap = 0, i, k, need = 0, done = 0;
	ab_table old = {0};
	bool *fresh = NULL, ok = false;
	FILE *o = NULL;

	(void)arg;
	snprintf(path, sizeof path, "/api/libraries/%s/items?limit=0&minified=1", C.lib_id);
	if (req("GET", path, NULL, &body, &len, 60) != 200) goto out;
	{
		jsv res, it = {0}, e, media;

		if (!js_member(js_root(body, len), "results", &res)) goto out;
		while (js_next(res, &it, &e)) {
			if (js_member(e, "media", &media) && jnum(media, "numAudioFiles") < 1) continue;
			if (nitems == cap) {
				ab_item *n = realloc(items, sizeof *n * (size_t)(cap = cap ? cap * 2 : 512));
				if (!n) goto out;
				items = n;
			}
			jstr(e, "id", items[nitems].id, sizeof items[0].id);
			items[nitems].upd = (long long)jnum(e, "updatedAt");
			nitems++;
		}
	}
	free(body); body = NULL;
	if (!g_sync_force) tab_read(&old);
	if (!(fresh = calloc((size_t)(nitems ? nitems : 1), 1))) goto out;
	for (i = 0; i < nitems; i++) {
		int o_i = tab_find(&old, items[i].id);
		fresh[i] = o_i < 0 || old.b[o_i].upd != items[i].upd;
		need += fresh[i];
	}
	g_sync_total = need;
	g_sync_done = 0;
	index_path(p, sizeof p);
	{
		char d[700], *s;
		snprintf(d, sizeof d, "%s", p);
		if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	}
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(o = fopen(tmp, "w"))) goto out;
	fputs("ABS1\n", o);
	/* Unchanged books from the old index, as they were. */
	for (i = 0; i < nitems; i++)
		if (!fresh[i]) write_book(o, &old, tab_find(&old, items[i].id));
	/* The changed and the new, BATCH at a time. */
	for (i = 0; i < nitems; ) {
		char json[BATCH * 70 + 64];
		size_t jl;
		int n = 0;

		jl = (size_t)snprintf(json, sizeof json, "{\"libraryItemIds\":[");
		for (; i < nitems && n < BATCH; i++) {
			if (!fresh[i]) continue;
			jl += (size_t)snprintf(json + jl, sizeof json - jl, "%s\"%s\"", n ? "," : "", items[i].id);
			n++;
		}
		if (!n) break;
		snprintf(json + jl, sizeof json - jl, "]}");
		if (req("POST", "/api/items/batch/get", json, &bodyb, &lenb, 120) != 200) goto out;
		{
			jsv res, it = {0}, e;

			if (js_member(js_root(bodyb, lenb), "libraryItems", &res))
				while (js_next(res, &it, &e)) write_item(o, e);
		}
		free(bodyb);
		done += n;
		g_sync_done = done;
	}
	if (fflush(o) != 0 || fsync(fileno(o)) != 0) goto out;
	fclose(o); o = NULL;
	if (rename(tmp, p) != 0) goto out;
	ok = true;
	k = need || old.nb != nitems || g_sync_force;
	if (k) {
		ab_table t;
		if (tab_read(&t)) { LOCK(); tab_free(&g_tab); g_tab = t; g_tab_read = true; UNLOCK(); }
		g_sync_new = 1;
	}
	fprintf(stderr, "abs: synced %d books, %d read again\n", nitems, need);
	pull_progress();
out:
	if (o) { fclose(o); unlink(tmp); }
	free(body); free(items); free(fresh);
	tab_free(&old);
	if (!ok) { g_sync_failed = 1; fprintf(stderr, "abs: sync failed\n"); }
	g_sync_run = 0;
	return NULL;
}

void abs_sync_start(bool force)
{
	pthread_t th;

	if (!abs_signed_in() || g_sync_run) return;
	g_sync_run = 1;
	g_sync_failed = 0;
	g_sync_force = force || !abs_lib_cached();
	g_sync_done = g_sync_total = 0;
	if (pthread_create(&th, NULL, sync_main, NULL) != 0) { g_sync_run = 0; g_sync_failed = 1; return; }
	pthread_detach(th);
}

int abs_sync_state(int *done, int *total)
{
	if (done) *done = g_sync_done;
	if (total) *total = g_sync_total;
	return g_sync_run ? 1 : g_sync_failed ? -1 : 0;
}

bool abs_sync_take(void)
{
	if (!g_sync_new) return false;
	g_sync_new = 0;
	return true;
}

/* ---- covers ---------------------------------------------------------------------------- */

static char g_landed[LANDED][700];
static int g_nlanded;

static void do_cover(job *j)
{
	char path[200], dest[720], part[740], *s, d[720];
	char *body;
	size_t len;
	FILE *f;

	snprintf(path, sizeof path, "/api/items/%.100s/cover?width=%d&format=jpeg", j->a, COVER_PX);
	if (req("GET", path, NULL, &body, &len, 30) != 200) return;
	snprintf(dest, sizeof dest, "%s.jpg", j->b);
	snprintf(part, sizeof part, "%s.part", dest);
	snprintf(d, sizeof d, "%s", dest);
	if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	if ((f = fopen(part, "wb"))) {
		bool w = fwrite(body, 1, len, f) == len;
		if (fclose(f) == 0 && w && rename(part, dest) == 0) {
			LOCK();
			if (g_nlanded < LANDED) snprintf(g_landed[g_nlanded++], sizeof g_landed[0], "%s", j->b);
			UNLOCK();
		} else {
			unlink(part);
		}
	}
	free(body);
}

bool abs_cover_want(int al, const char *base)
{
	job *j;
	int covers = 0;

	if (!(j = job_new(J_COVER))) return false;
	LOCK();
	if (al < 0 || al >= g_tab.nb || !g_tab.b[al].cover || !C.access[0]) { UNLOCK(); free(j); return false; }
	snprintf(j->a, sizeof j->a, "%s", g_tab.b[al].id);
	UNLOCK();
	snprintf(j->b, sizeof j->b, "%s", base);
	push(j, true);
	/* The newest 48 covers wished for; one scrolled past is asked again. */
	LOCK();
	for (job **pp = &g_jobs; *pp; ) {
		if ((*pp)->kind == J_COVER && ++covers > 48) {
			job *gone = *pp;
			*pp = gone->next;
			g_njobs--;
			free(gone);
			continue;
		}
		pp = &(*pp)->next;
	}
	UNLOCK();
	return true;
}

bool abs_cover_take(char *base, size_t n)
{
	bool got = false;

	LOCK();
	if (g_nlanded) { snprintf(base, n, "%s", g_landed[--g_nlanded]); got = true; }
	UNLOCK();
	return got;
}

/* ---- which book a track is ------------------------------------------------------------- */

/* The item a track belongs to: the source's own path names it; a downloaded
 * copy's folder has a .abs file that does. The last folder asked is kept,
 * since this is asked on every poll. */
static bool item_of(const char *track, char *id, size_t n)
{
	static char last_dir[600], last_id[64];
	char dir[600], p[1200], *s;
	size_t rl = strlen(g_rel_root);
	FILE *f;

	if (!track || !track[0]) return false;
	if (rl && !strncmp(track, g_rel_root, rl) && track[rl] == '/') {
		const char *a = track + rl + 1, *b = strchr(a, '/');
		if (!b || (size_t)(b - a) >= n) return false;
		snprintf(id, n, "%.*s", (int)(b - a), a);
		return true;
	}
	if (abs_is_path(track)) return false;           /* another library's */
	snprintf(dir, sizeof dir, "%s", track);
	if (!(s = strrchr(dir, '/'))) return false;
	*s = '\0';
	if (strcmp(dir, last_dir)) {
		snprintf(last_dir, sizeof last_dir, "%s", dir);
		last_id[0] = '\0';
		snprintf(p, sizeof p, "%s/%s/.abs", g_card, dir);
		if ((f = fopen(p, "r"))) {
			if (fgets(last_id, sizeof last_id, f)) last_id[strcspn(last_id, "\r\n")] = '\0';
			fclose(f);
		}
	}
	if (!last_id[0]) return false;
	snprintf(id, n, "%s", last_id);
	return true;
}

/* Seconds into the whole book from file `index` and `at` into it. Under g_mu. */
static bool book_time(const char *id, int index, double at, double *cur, double *dur)
{
	int b = tab_find(&g_tab, id), k;

	if (b < 0 || index < 0 || index >= g_tab.b[b].n) return false;
	*cur = at;
	for (k = 0; k < index; k++) *cur += g_tab.f[g_tab.b[b].first + k].dur;
	*dur = g_tab.b[b].dur;
	return true;
}

/* ---- playing ----------------------------------------------------------------------------- */

static bool local_copy(int b, int k, char *out, size_t n);

/* Under g_mu: book and file of a source path, or -1. */
static int path_book(const char *rel, int *file)
{
	char id[64];
	const char *num;
	int b;

	if (!item_of(rel, id, sizeof id) || !abs_is_path(rel)) return -1;
	if ((b = tab_find(&g_tab, id)) < 0) return -1;
	num = strrchr(rel, '/');
	*file = num ? atoi(num + 1) - 1 : -1;
	return *file >= 0 && *file < g_tab.b[b].n ? b : -1;
}

bool abs_resolve(const char *rel, musec_remote *out)
{
	int b, k;
	const char *tok;

	if (!abs_is_path(rel)) return false;
	memset(out, 0, sizeof *out);
	tab_ensure();
	LOCK();
	if ((b = path_book(rel, &k)) < 0) {
		UNLOCK();
		snprintf(out->url, sizeof out->url, "abs:missing");
		return true;
	}
	snprintf(out->title, sizeof out->title, "%s", g_tab.f[g_tab.b[b].first + k].title);
	snprintf(out->artist, sizeof out->artist, "%s", g_tab.b[b].author);
	out->len = g_tab.f[g_tab.b[b].first + k].dur;
	if (local_copy(b, k, out->url, sizeof out->url)) { UNLOCK(); return true; }
	/* The account's long-lived token where the server still gives one: an
	 * access token lasts an hour, and a seek in a long file asks again. */
	tok = C.legacy[0] ? C.legacy : C.access;
	snprintf(out->url, sizeof out->url, "%s/api/items/%s/file/%s?token=%s",
	         C.server, g_tab.b[b].id, g_tab.f[g_tab.b[b].first + k].ino, tok);
	UNLOCK();
	return true;
}

bool abs_rel_of(const char *daemon_path, char *out, size_t n)
{
	const char *p = strstr(daemon_path, "/api/items/"), *f;
	char id[64], ino[48];
	int b, k;
	bool yes = false;

	if (!p || !(f = strstr(p, "/file/"))) return false;
	p += 11;
	if (f - p <= 0 || (size_t)(f - p) >= sizeof id) return false;
	snprintf(id, sizeof id, "%.*s", (int)(f - p), p);
	snprintf(ino, sizeof ino, "%s", f + 6);
	ino[strcspn(ino, "?")] = '\0';
	tab_ensure();
	LOCK();
	if ((b = tab_find(&g_tab, id)) >= 0)
		for (k = 0; k < g_tab.b[b].n; k++)
			if (!strcmp(g_tab.f[g_tab.b[b].first + k].ino, ino)) {
				snprintf(out, n, "%s/%s/%03d%s", g_rel_root, id, k + 1, g_tab.f[g_tab.b[b].first + k].ext);
				yes = true;
				break;
			}
	UNLOCK();
	return yes;
}

bool abs_title(const char *rel, char *out, size_t n)
{
	int b, k;

	if (!abs_is_path(rel)) return false;
	LOCK();
	if ((b = path_book(rel, &k)) >= 0) snprintf(out, n, "%s", g_tab.f[g_tab.b[b].first + k].title);
	UNLOCK();
	return b >= 0;
}

bool abs_owns(const char *daemon_path, const char *rel)
{
	char want[200], copy[1200];
	int b, k;
	bool yes = false;

	if (!abs_is_path(rel)) return false;
	LOCK();
	if ((b = path_book(rel, &k)) >= 0) {
		snprintf(want, sizeof want, "/api/items/%s/file/%s?", g_tab.b[b].id, g_tab.f[g_tab.b[b].first + k].ino);
		yes = strstr(daemon_path, want) != NULL;
		if (!yes && local_copy(b, k, copy, sizeof copy)) yes = !strcmp(copy, daemon_path);
	}
	UNLOCK();
	return yes;
}

/* ---- places ------------------------------------------------------------------------------ */

typedef struct { char id[64]; double cur; bool fin; long long last; } ab_prog;
static ab_prog *g_prog; static int g_nprog;          /* the server's, under g_mu */
typedef struct { char id[64]; long long last; } ab_seen;
static ab_seen *g_seen; static int g_nseen, g_cseen; /* the server's lastUpdate as this device last knew it */
static bool g_seen_dirty;
static unsigned g_seen_saved;

static void seen_path(char *out, size_t n) { snprintf(out, n, "%s/seen.tsv", g_dir); }

static void seen_load(void)
{
	char p[700], line[160], *t;
	FILE *f;

	seen_path(p, sizeof p);
	if (!(f = fopen(p, "r"))) return;
	while (fgets(line, sizeof line, f)) {
		if (!(t = strchr(line, '\t'))) continue;
		*t++ = '\0';
		if (g_nseen == g_cseen) {
			ab_seen *n = realloc(g_seen, sizeof *n * (size_t)(g_cseen = g_cseen ? g_cseen * 2 : 64));
			if (!n) break;
			g_seen = n;
		}
		snprintf(g_seen[g_nseen].id, sizeof g_seen[0].id, "%.63s", line);
		g_seen[g_nseen++].last = atoll(t);
	}
	fclose(f);
}

/* Under g_mu. Written when it changed, at most every 5 minutes unless `now`:
 * one lost to a power cut only makes the server's place win, and that is the
 * place this device sent. */
static void seen_save(bool now)
{
	char p[700], tmp[720];
	FILE *f;

	if (!g_seen_dirty || (!now && now_ms() - g_seen_saved < SEEN_MS)) return;
	seen_path(p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) return;
	for (int i = 0; i < g_nseen; i++) fprintf(f, "%s\t%lld\n", g_seen[i].id, g_seen[i].last);
	if (fclose(f) == 0) rename(tmp, p);
	g_seen_dirty = false;
	g_seen_saved = now_ms();
}

static long long seen_get(const char *id)
{
	for (int i = 0; i < g_nseen; i++) if (!strcmp(g_seen[i].id, id)) return g_seen[i].last;
	return 0;
}

static void seen_set(const char *id, long long last)
{
	int i;

	for (i = 0; i < g_nseen && strcmp(g_seen[i].id, id); i++) {}
	if (i == g_nseen) {
		if (g_nseen == g_cseen) {
			ab_seen *n = realloc(g_seen, sizeof *n * (size_t)(g_cseen = g_cseen ? g_cseen * 2 : 64));
			if (!n) return;
			g_seen = n;
		}
		snprintf(g_seen[g_nseen++].id, sizeof g_seen[0].id, "%s", id);
	}
	if (g_seen[i].last != last) { g_seen[i].last = last; g_seen_dirty = true; }
}

static ab_prog *prog_find(const char *id)
{
	for (int i = 0; i < g_nprog; i++) if (!strcmp(g_prog[i].id, id)) return &g_prog[i];
	return NULL;
}

static void pull_progress(void)
{
	char *body;
	size_t len;
	jsv mp, it = {0}, e, v;
	ab_prog *list = NULL;
	int n = 0, cap = 0;

	if (req("GET", "/api/me", NULL, &body, &len, 15) != 200) return;
	if (js_member(js_root(body, len), "mediaProgress", &mp))
		while (js_next(mp, &it, &e)) {
			char ep[8];
			if (js_member(e, "episodeId", &v) && js_str(v, ep, sizeof ep)) continue;   /* a podcast's */
			if (n == cap) {
				ab_prog *nl = realloc(list, sizeof *nl * (size_t)(cap = cap ? cap * 2 : 128));
				if (!nl) break;
				list = nl;
			}
			jstr(e, "libraryItemId", list[n].id, sizeof list[0].id);
			list[n].cur = jnum(e, "currentTime");
			list[n].fin = js_member(e, "isFinished", &v) && js_is_true(v);
			list[n].last = (long long)jnum(e, "lastUpdate");
			n++;
		}
	free(body);
	LOCK();
	free(g_prog);
	g_prog = list;
	g_nprog = n;
	UNLOCK();
}

static void do_push(job *j)
{
	char path[160], json[200], *body;
	size_t len;
	double p = j->dur > 0 ? j->cur / j->dur : 0;

	snprintf(path, sizeof path, "/api/me/progress/%.100s", j->a);
	snprintf(json, sizeof json, "{\"currentTime\":%.2f,\"duration\":%.2f,\"progress\":%.5f,\"isFinished\":%s}",
	         j->cur, j->dur, p > 1 ? 1 : p, j->fin ? "true" : "false");
	if (req("PATCH", path, json, NULL, NULL, 10) != 200) return;
	/* The PATCH answers "OK": its lastUpdate is read back, to know it as ours. */
	if (req("GET", path, NULL, &body, &len, 10) != 200) return;
	{
		jsv root = js_root(body, len);
		long long last = (long long)jnum(root, "lastUpdate");
		ab_prog *pr;

		LOCK();
		seen_set(j->a, last);
		if ((pr = prog_find(j->a))) { pr->cur = j->cur; pr->fin = j->fin; pr->last = last; }
		seen_save(j->b[0] == 'n');
		UNLOCK();
	}
	free(body);
}

/* A place to send, replacing one for the same book still waiting. `now`
 * also writes what this device knows to the card. */
static void queue_push(const char *id, double cur, double dur, bool fin, bool now)
{
	job *j;

	LOCK();
	for (j = g_jobs; j; j = j->next)
		if (j->kind == J_PUSH && !strcmp(j->a, id)) {
			j->cur = cur; j->dur = dur; j->fin = j->fin || fin;
			if (now) j->b[0] = 'n';
			UNLOCK();
			return;
		}
	UNLOCK();
	if (!(j = job_new(J_PUSH))) return;
	snprintf(j->a, sizeof j->a, "%s", id);
	j->cur = cur; j->dur = dur; j->fin = fin;
	if (now) j->b[0] = 'n';
	push(j, false);
}

void abs_progress_refresh(void)
{
	static unsigned last;
	unsigned now = now_ms();
	job *j;

	if (!abs_signed_in() || (last && now - last < PULL_MS)) return;
	last = now;
	if ((j = job_new(J_PULL))) push(j, true);
}

/* Under g_mu: the server's place for `id` when it moved since this device
 * last sent or took one - 1 with file and seconds, 2 finished - else 0. */
static int place_locked(const char *id, int *index, double *at)
{
	ab_prog *pr;
	int b, k;
	double left;

	if (!(pr = prog_find(id)) || pr->last == seen_get(id)) return 0;
	seen_set(id, pr->last);
	seen_save(true);
	if (pr->fin) return 2;
	if ((b = tab_find(&g_tab, id)) < 0) return 0;
	left = pr->cur;
	for (k = 0; k < g_tab.b[b].n - 1 && left >= g_tab.f[g_tab.b[b].first + k].dur; k++)
		left -= g_tab.f[g_tab.b[b].first + k].dur;
	*index = k;
	*at = left < 0 ? 0 : left;
	fprintf(stderr, "abs: the server's place wins: file %d at %.0f s\n", k + 1, *at);
	return 1;
}

int abs_book_place(const char *track, int *index, double *at)
{
	char id[64];
	int r;

	if (!item_of(track, id, sizeof id)) return 0;
	tab_ensure();
	LOCK();
	r = place_locked(id, index, at);
	UNLOCK();
	return r;
}

bool abs_item(const char *track, char *id, size_t n) { return item_of(track, id, n); }

bool abs_book_secs(const char *id, int index, double at, double *secs)
{
	double dur;
	bool ok;

	tab_ensure();
	LOCK();
	ok = book_time(id, index, at, secs, &dur);
	UNLOCK();
	return ok;
}

bool abs_book_file(const char *id, double secs, int *index, double *at)
{
	int b, k;

	tab_ensure();
	LOCK();
	if ((b = tab_find(&g_tab, id)) < 0) { UNLOCK(); return false; }
	for (k = 0; k < g_tab.b[b].n - 1 && secs >= g_tab.f[g_tab.b[b].first + k].dur; k++)
		secs -= g_tab.f[g_tab.b[b].first + k].dur;
	UNLOCK();
	*index = k;
	*at = secs < 0 ? 0 : secs;
	return true;
}

bool abs_server_place(const char *id, long long *last_ms, bool *fin, double *cur)
{
	ab_prog *pr;
	bool ok = false;

	LOCK();
	if ((pr = prog_find(id))) { *last_ms = pr->last; *fin = pr->fin; *cur = pr->cur; ok = true; }
	UNLOCK();
	return ok;
}

/* ---- play on a paused book (plorpos-9rf.9) -------------------------------------------- */

#define RESUME_WAIT_MS 1500
static char g_rc_id[64];                 /* under g_mu */
static volatile int g_rc_state;          /* 0 none, 1 asking, 2 answered */
static unsigned g_rc_t0;

/* One book's place, read fresh: into g_prog, as /api/me's list would have it. */
static void do_check_place(job *j)
{
	char path[160], *body;
	size_t len;
	int code;

	snprintf(path, sizeof path, "/api/me/progress/%.100s", j->a);
	code = req("GET", path, NULL, &body, &len, 5);
	LOCK();
	if (code == 200) {
		jsv root = js_root(body, len), v;
		ab_prog *pr = prog_find(j->a);

		if (!pr) {
			ab_prog *nl = realloc(g_prog, sizeof *nl * (size_t)(g_nprog + 1));
			if (nl) { g_prog = nl; pr = &g_prog[g_nprog++]; snprintf(pr->id, sizeof pr->id, "%.63s", j->a); }
		}
		if (pr) {
			pr->cur = jnum(root, "currentTime");
			pr->fin = js_member(root, "isFinished", &v) && js_is_true(v);
			pr->last = (long long)jnum(root, "lastUpdate");
		}
	}
	if (!strcmp(g_rc_id, j->a) && g_rc_state == 1) g_rc_state = 2;
	UNLOCK();
	free(body);
}

bool abs_resume_hold(const char *track)
{
	char id[64];
	job *j;

	if (!abs_signed_in() || !item_of(track, id, sizeof id) || !(j = job_new(J_PLACE))) return false;
	snprintf(j->a, sizeof j->a, "%s", id);
	LOCK();
	snprintf(g_rc_id, sizeof g_rc_id, "%s", id);
	g_rc_state = 1;
	g_rc_t0 = now_ms();
	UNLOCK();
	push(j, true);
	return true;
}

int abs_resume_poll(int *index, double *at)
{
	unsigned ms;

	if (!g_rc_state) return -1;
	ms = now_ms() - g_rc_t0;
	if (g_rc_state == 1 && ms < RESUME_WAIT_MS) return 0;
	*index = -1;
	*at = 0;
	LOCK();
	if (g_rc_state == 2 && place_locked(g_rc_id, index, at) != 1) *index = -1;
	fprintf(stderr, "abs: play checked the server's place in %u ms%s\n", ms,
	        g_rc_state == 1 ? " (no answer: the device's place)" : *index >= 0 ? "" : " (the device's place)");
	g_rc_state = 0;
	g_rc_id[0] = '\0';
	UNLOCK();
	return 1;
}

void abs_tick(const char *track, int index, double at, int state)
{
	static struct { char id[64]; int state; unsigned ms; double cur, dur; } T = { "", -1, 0, 0, 0 };
	unsigned now = now_ms();
	char id[64];
	double cur, dur;
	bool have = abs_signed_in() && item_of(track, id, sizeof id);

	if (have) {
		tab_ensure();
		LOCK();
		have = book_time(id, index, at, &cur, &dur);
		UNLOCK();
	}
	/* Off the book (another track, or nothing): its last place, if it was
	 * playing. A stop is not sent itself - a stopped daemon says 0 s. */
	if (T.id[0] && (!have || strcmp(id, T.id))) {
		if (T.state == 1) queue_push(T.id, T.cur, T.dur, false, true);
		T.id[0] = '\0';
		T.state = -1;
	}
	if (!have || state == 0) { if (have) T.state = 0; return; }
	if (!T.id[0]) { snprintf(T.id, sizeof T.id, "%s", id); T.state = -1; }
	T.cur = cur; T.dur = dur;
	if (state == T.state && (state != 1 || now - T.ms < PUSH_MS)) return;
	queue_push(id, cur, dur, false, state == 2);
	T.state = state;
	T.ms = now;
}

void abs_book_finished(const char *track)
{
	char id[64];
	int b;
	double dur = 0;

	if (!abs_signed_in() || !item_of(track, id, sizeof id)) return;
	tab_ensure();
	LOCK();
	if ((b = tab_find(&g_tab, id)) >= 0) dur = g_tab.b[b].dur;
	UNLOCK();
	queue_push(id, dur, dur, true, true);
}

/* ---- the worker --------------------------------------------------------------------------- */

static void *api_main(void *arg)
{
	(void)arg;
	for (;;) {
		job *j;

		LOCK();
		while (!g_jobs) pthread_cond_wait(&g_cv, &g_mu);
		j = g_jobs;
		g_jobs = j->next;
		g_njobs--;
		UNLOCK();
		switch (j->kind) {
		case J_CHECK: do_check(j); break;
		case J_LOGIN: do_login(j); break;
		case J_COVER: do_cover(j); break;
		case J_PUSH:  do_push(j); break;
		case J_PULL:  pull_progress(); break;
		case J_LOAD:  tab_ensure(); break;
		case J_PLACE: do_check_place(j); break;
		}
		memset(j, 0, sizeof *j);
		free(j);
	}
	return NULL;
}

void abs_init(const char *card, const char *shared)
{
	size_t cl = strlen(card);
	pthread_t th;
	job *j;

	snprintf(g_card, sizeof g_card, "%s", card);
	snprintf(g_dir, sizeof g_dir, "%s/abs", shared);
	if (!strncmp(shared, card, cl) && shared[cl] == '/')
		snprintf(g_rel_base, sizeof g_rel_base, "%s/abs", shared + cl + 1);
	else
		snprintf(g_rel_base, sizeof g_rel_base, ".userdata/shared/abs");
	cfg_load();
	seen_load();
	if (pthread_create(&th, NULL, api_main, NULL) == 0) pthread_detach(th);
	/* The table off the boot path, for places of downloaded books too. */
	if (C.lib_id[0] && (j = job_new(J_LOAD))) push(j, false);
}

void abs_sign_out(void)
{
	LOCK();
	C.access[0] = C.refresh[0] = C.legacy[0] = C.user[0] = '\0';
	C.lib_id[0] = C.lib_name[0] = '\0';
	rel_root_set();
	cfg_save_locked();
	tab_free(&g_tab);
	g_tab_read = false;
	UNLOCK();
	fprintf(stderr, "abs: signed out\n");
}

/* ---- downloads (plorpos-9rf.6) ------------------------------------------------------------ */

/* A book goes to <card>/Audiobooks/<author>/<title>/, its files named
 * "<NNN> <title><ext>" so they sort as the server orders them, beside a .abs
 * file naming its item. It is fetched into Audiobooks/.abs-dl/<id>/ and
 * moved into place whole, so the Local Audiobooks source never sees half a
 * book. One at a time, the rest waiting. */
#define DL_Q 16
static char g_dl_q[DL_Q][64];
static int g_dl_n;
static char g_dl_id[64];                 /* the one being fetched, under g_mu */
static volatile int g_dl_cancel, g_dl_changed, g_dl_file, g_dl_files, g_dl_up;
static pthread_cond_t g_dl_cv = PTHREAD_COND_INITIALIZER;

static void safe_name(const char *in, char *out, size_t max)
{
	size_t o = 0;

	for (; *in && o + 1 < max && o < 100; in++) {
		unsigned char c = (unsigned char)*in;
		out[o++] = c < 0x20 || strchr("/\\:*?\"<>|", c) ? '_' : (char)c;
	}
	while (o && (out[o - 1] == ' ' || out[o - 1] == '.')) o--;
	out[o] = '\0';
	if (!o) snprintf(out, max, "Unknown");
}

/* Under g_mu: book b's folder on the card (absolute), and file k's name in it. */
static void book_dir(int b, char *out, size_t n)
{
	char au[128], ti[128];

	safe_name(g_tab.b[b].author[0] ? g_tab.b[b].author : "Unknown Author", au, sizeof au);
	safe_name(g_tab.b[b].title, ti, sizeof ti);
	snprintf(out, n, "%.300s/Audiobooks/%s/%s", g_card, au, ti);
}

static void file_name(int b, int k, char *out, size_t n)
{
	char ti[128];

	safe_name(g_tab.f[g_tab.b[b].first + k].title, ti, sizeof ti);
	snprintf(out, n, "%03d %s%s", k + 1, ti, g_tab.f[g_tab.b[b].first + k].ext);
}

/* Under g_mu. */
static bool local_copy(int b, int k, char *out, size_t n)
{
	char dir[600], f[300];

	book_dir(b, dir, sizeof dir);
	file_name(b, k, f, sizeof f);
	snprintf(out, n, "%s/%s", dir, f);
	return exists(out);
}

static void rm_tree(const char *dir)
{
	char cmd[1300];

	/* Only ever one of ours: a staging folder or a book's folder. */
	if (!strstr(dir, "/Audiobooks/")) return;
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
	if (!strchr(dir, '\'')) { int r = system(cmd); (void)r; }
}

static bool dl_one(const char *id)
{
	char stage[700], dir[600], path[1000], url[1400], auth[1100], f[300], au[1010], *s;
	const char *h[1] = { auth };
	int b, k, n, code;

	LOCK();
	if ((b = tab_find(&g_tab, id)) < 0) { UNLOCK(); return false; }
	n = g_tab.b[b].n;
	book_dir(b, dir, sizeof dir);
	UNLOCK();
	snprintf(stage, sizeof stage, "%s/Audiobooks/.abs-dl/%.63s", g_card, id);
	mkdirs(stage);
	g_dl_files = n;
	for (k = 0; k < n && !g_dl_cancel; k++) {
		char ino[48];

		g_dl_file = k + 1;
		LOCK();
		if ((b = tab_find(&g_tab, id)) < 0 || k >= g_tab.b[b].n) { UNLOCK(); return false; }
		file_name(b, k, f, sizeof f);
		snprintf(ino, sizeof ino, "%s", g_tab.f[g_tab.b[b].first + k].ino);
		snprintf(url, sizeof url, "%s/api/items/%s/file/%s", C.server, id, ino);
		snprintf(auth, sizeof auth, "Authorization: Bearer %s", C.legacy[0] ? C.legacy : C.access);
		UNLOCK();
		snprintf(path, sizeof path, "%s/%s", stage, f);
		if (exists(path)) continue;                     /* from an earlier try */
		snprintf(au, sizeof au, "%s.part", path);
		code = net_fetch(url, "GET", h, 1, au, 10, 0, &g_dl_cancel);
		if (code == 401 && !C.legacy[0]) {               /* an access token that ran out */
			char used[sizeof C.access];
			LOCK(); snprintf(used, sizeof used, "%s", C.access); UNLOCK();
			if (refresh_tokens(used)) { k--; unlink(au); continue; }
		}
		if (code != 200 || rename(au, path) != 0) { unlink(au); return false; }
	}
	if (g_dl_cancel) return false;
	snprintf(path, sizeof path, "%s/.abs", stage);
	{
		FILE *o = fopen(path, "w");
		if (!o) return false;
		fprintf(o, "%s\n", id);
		if (fclose(o) != 0) return false;
	}
	snprintf(path, sizeof path, "%.599s", dir);
	if ((s = strrchr(path, '/'))) { *s = '\0'; mkdirs(path); }
	rm_tree(dir);                                      /* a stale partial copy */
	if (rename(stage, dir) != 0) return false;
	sync();
	return true;
}

static void *dl_main(void *arg)
{
	(void)arg;
	for (;;) {
		char id[64];
		bool ok;

		LOCK();
		while (!g_dl_n) pthread_cond_wait(&g_dl_cv, &g_mu);
		snprintf(id, sizeof id, "%s", g_dl_q[0]);
		memmove(g_dl_q, g_dl_q + 1, sizeof g_dl_q[0] * (size_t)--g_dl_n);
		snprintf(g_dl_id, sizeof g_dl_id, "%s", id);
		g_dl_cancel = 0;
		UNLOCK();
		ok = dl_one(id);
		fprintf(stderr, "abs: download %s %s\n", id, ok ? "done" : g_dl_cancel ? "cancelled" : "failed");
		if (ok) g_dl_changed = 1;
		else if (g_dl_cancel) {
			char stage[700];
			snprintf(stage, sizeof stage, "%s/Audiobooks/.abs-dl/%.63s", g_card, id);
			rm_tree(stage);
		}
		LOCK(); g_dl_id[0] = '\0'; g_dl_file = g_dl_files = 0; UNLOCK();
	}
	return NULL;
}

static const char *book_id(int al)
{
	return al >= 0 && al < g_tab.nb ? g_tab.b[al].id : NULL;
}

int abs_book_downloaded(int al)
{
	char dir[600], p[700];
	int r = 0;

	LOCK();
	if (book_id(al)) {
		book_dir(al, dir, sizeof dir);
		snprintf(p, sizeof p, "%s/.abs", dir);
		r = exists(p);
	}
	UNLOCK();
	return r;
}

bool abs_book_downloading(int al)
{
	const char *id;
	bool yes = false;

	LOCK();
	if ((id = book_id(al))) {
		yes = !strcmp(g_dl_id, id);
		for (int i = 0; !yes && i < g_dl_n; i++) yes = !strcmp(g_dl_q[i], id);
	}
	UNLOCK();
	return yes;
}

void abs_download_book(int al)
{
	const char *id;
	pthread_t th;

	if (abs_book_downloading(al)) return;
	LOCK();
	if (!g_dl_up) g_dl_up = pthread_create(&th, NULL, dl_main, NULL) == 0 ? (pthread_detach(th), 1) : 0;
	if ((id = book_id(al)) && g_dl_n < DL_Q) {
		snprintf(g_dl_q[g_dl_n++], sizeof g_dl_q[0], "%s", id);
		pthread_cond_signal(&g_dl_cv);
	}
	UNLOCK();
}

void abs_download_cancel(void)
{
	LOCK();
	g_dl_n = 0;
	if (g_dl_id[0]) g_dl_cancel = 1;
	UNLOCK();
}

void abs_delete_book(int al)
{
	char dir[600];

	LOCK();
	if (!book_id(al)) { UNLOCK(); return; }
	book_dir(al, dir, sizeof dir);
	UNLOCK();
	rm_tree(dir);
	g_dl_changed = 1;
}

bool abs_download_status(char *out, size_t n)
{
	bool on;

	LOCK();
	on = g_dl_id[0] != '\0';
	if (on) {
		int b = tab_find(&g_tab, g_dl_id);
		snprintf(out, n, "%s: file %d of %d%s", b >= 0 ? g_tab.b[b].title : "a book", g_dl_file, g_dl_files,
		         g_dl_n ? " (more waiting)" : "");
	}
	UNLOCK();
	return on;
}

bool abs_download_take_changed(void)
{
	if (!g_dl_changed) return false;
	g_dl_changed = 0;
	return true;
}
