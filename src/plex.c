/* SPDX-License-Identifier: MIT */
/* See plex.h. The Plex API as PlexMusicPak uses it (the owner's NextUI
 * client): plex.tv's PIN sign-in and resources, a server's sections, tracks
 * and photo transcoder, the universal audio transcoder and the timeline.
 * Everything over net_fetch, which runs the device's curl. */
#include "plex.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "net.h"
#include "playlist.h"
#include "rajson.h"

#define PLEX_TV        "https://plex.tv/api/v2"
#define PRODUCT        "plorpOS"
/* The transcoder serves the Opus profile below to a client that says it is a
 * browser, and 400 to one that says Linux (measured 2026-11-16, carol). */
#define PLATFORM       "Chrome"
#define MAX_CONNS      8
#define MAX_SERVERS    16
#define MAX_LIBS       16
#define PAGE           1000
#define COVER_PX       500
#define TIMELINE_MS    5000
#define PLAYED_AT      0.90   /* where Plex counts a play */

/* What the device's FFmpeg - the daemon's - can open (measured in the
 * sysroots, plorpos-1lw.8): http and https on the RG DS, GKD and Rotate; http
 * alone on the Brick; neither on the H700 (BaseOS) or the Miyoo Flip. A URL
 * the daemon cannot open is fetched to the card first (the cache below) and
 * played from there. The Rotate defines PLATFORM_H700 too. */
#if (defined(PLATFORM_H700) && !defined(PLATFORM_ROTATE)) || defined(PLATFORM_MIYOO) || \
    defined(PLEX_FETCH_FIRST)
#define STREAM_HTTP 0
#define STREAM_TLS  0
#elif defined(PLATFORM_GKD) || defined(PLATFORM_ROTATE) || defined(PLATFORM_RGDS)
#define STREAM_HTTP 1
#define STREAM_TLS  1
#else
#define STREAM_HTTP 1
#define STREAM_TLS  0
#endif
#define CACHE_KEEP  4          /* tracks fetched first that stay on the card */

/* Plexamp's Opus profile, from the pak (a tshark capture of Plexamp). */
#define OPUS_PROFILE \
	"add-transcode-target(replace%3Dtrue%26type%3DmusicProfile%26context%3Dstreaming" \
	"%26protocol%3Dhttp%26container%3Dogg%26audioCodec%3Dopus)" \
	"%2Badd-limitation(scope%3DmusicCodec%26scopeName%3DOpus%26type%3DupperBound" \
	"%26name%3Daudio.channels%26value%3D2%26onlyTranscodes%3Dtrue%26replace%3Dtrue)"

static const int QUALITY[PLEX_QUALITIES] = { 0, 96, 128, 192, 256, 320 };

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_mu)
#define UNLOCK() pthread_mutex_unlock(&g_mu)

static char g_card[512], g_dir[600];     /* <shared>/plex */
static char g_rel_base[256];             /* card-relative <shared>/plex */

/* ---- the config (P_SHARED/plex/config, 0600) ---------------------------------- */

typedef struct { char uri[256]; bool local, relay; } conn;
static struct {
	char client[48], token[128], account[128];
	char server[128], server_id[64];
	conn conns[MAX_CONNS]; int nconns;
	int  section; char library[128];
	int  stream_kbps, download_kbps;
} C;
static char g_base[256];                  /* the connection that answered */
static char g_rel_root[400];              /* card-relative cache root for this library */

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

static void cfg_path(char *out, size_t n) { snprintf(out, n, "%s/config", g_dir); }

static void rel_root_set(void)
{
	if (C.server_id[0] && C.section > 0)
		snprintf(g_rel_root, sizeof g_rel_root, "%s/%.24s-%d", g_rel_base, C.server_id, C.section);
	else
		g_rel_root[0] = '\0';
}

static void cfg_save(void)
{
	char p[700], tmp[720];
	FILE *f;
	int fd, i;

	cfg_path(p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	mkdirs(g_dir);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) return;
	if (!(f = fdopen(fd, "w"))) { close(fd); return; }
	fprintf(f, "client=%s\ntoken=%s\naccount=%s\nserver=%s\nserver_id=%s\n",
	        C.client, C.token, C.account, C.server, C.server_id);
	for (i = 0; i < C.nconns; i++)
		fprintf(f, "conn=%d %d %s\n", C.conns[i].local, C.conns[i].relay, C.conns[i].uri);
	fprintf(f, "section=%d\nlibrary=%s\nstream_kbps=%d\ndownload_kbps=%d\n",
	        C.section, C.library, C.stream_kbps, C.download_kbps);
	fflush(f);
	fsync(fd);
	fclose(f);
	rename(tmp, p);
}

static void cfg_load(void)
{
	char p[700], line[600];
	FILE *f;

	memset(&C, 0, sizeof C);
	C.stream_kbps = C.download_kbps = 128;   /* the pak's defaults */
	cfg_path(p, sizeof p);
	if ((f = fopen(p, "r"))) {
		while (fgets(line, sizeof line, f)) {
			char *v = strchr(line, '=');

			line[strcspn(line, "\r\n")] = '\0';
			if (!v) continue;
			*v++ = '\0';
			if (!strcmp(line, "client"))         snprintf(C.client, sizeof C.client, "%s", v);
			else if (!strcmp(line, "token"))     snprintf(C.token, sizeof C.token, "%s", v);
			else if (!strcmp(line, "account"))   snprintf(C.account, sizeof C.account, "%s", v);
			else if (!strcmp(line, "server"))    snprintf(C.server, sizeof C.server, "%s", v);
			else if (!strcmp(line, "server_id")) snprintf(C.server_id, sizeof C.server_id, "%s", v);
			else if (!strcmp(line, "section"))   C.section = atoi(v);
			else if (!strcmp(line, "library"))   snprintf(C.library, sizeof C.library, "%s", v);
			else if (!strcmp(line, "stream_kbps"))   C.stream_kbps = atoi(v);
			else if (!strcmp(line, "download_kbps")) C.download_kbps = atoi(v);
			else if (!strcmp(line, "conn") && C.nconns < MAX_CONNS) {
				int lo, re, k = 0;
				if (sscanf(v, "%d %d %n", &lo, &re, &k) >= 2 && k > 0) {
					conn *c = &C.conns[C.nconns++];
					c->local = lo; c->relay = re;
					snprintf(c->uri, sizeof c->uri, "%s", v + k);
				}
			}
		}
		fclose(f);
	}
	if (!C.client[0]) {
		unsigned char r[16];
		int fd = open("/dev/urandom", O_RDONLY), i;

		if (fd < 0 || read(fd, r, sizeof r) != (ssize_t)sizeof r)
			for (i = 0; i < 16; i++) r[i] = (unsigned char)(rand() ^ (int)now_ms());
		if (fd >= 0) close(fd);
		strcpy(C.client, "plorpos-");
		for (i = 0; i < 12; i++) sprintf(C.client + strlen(C.client), "%02x", r[i]);
		cfg_save();
	}
	rel_root_set();
}

/* ---- small helpers ------------------------------------------------------------- */

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

/* A field that Plex sends as a string ("ratingKey": "123") or a number. */
static long jnum(jsv o, const char *k)
{
	jsv v;
	char s[32];

	if (!js_member(o, k, &v)) return 0;
	if (js_str(v, s, sizeof s)) return atol(s);
	return js_int(v);
}

static void jstr(jsv o, const char *k, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	if (js_member(o, k, &v)) js_str(v, out, n);
}

static void urlenc(const char *s, char *out, size_t n)
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
static void tmpfile_for(const char *tag, char *out, size_t n)
{
	static unsigned seq;
	unsigned k;

	LOCK(); k = ++seq; UNLOCK();
	snprintf(out, n, "/tmp/tortos-plex-%s-%ld-%u", tag, (long)getpid(), k);
}

/* GET/POST with Plex's headers, the reply into memory (NUL-terminated).
 * Returns the HTTP status, 0 for no answer, -1 when it could not run. */
static int plex_req_c(const char *method, const char *url, bool with_token,
                      char **body, size_t *len, int connect_s, int timeout_s)
{
	char hdr[8][200], tmp[128];
	const char *h[8];
	int nh = 0, code;
	unsigned t0;

	snprintf(hdr[nh], sizeof hdr[nh], "Accept: application/json"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Product: " PRODUCT); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Version: 1.0"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Platform: " PLATFORM); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Device-Name: plorpOS"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Client-Identifier: %s", C.client); nh++;
	if (with_token) { snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Token: %s", C.token); nh++; }
	for (int i = 0; i < nh; i++) h[i] = hdr[i];
	tmpfile_for("req", tmp, sizeof tmp);
	/* Plex's relay drops about one TLS handshake in three at once (curl 35,
	 * measured 2026-11-17): an answer that failed that fast is asked again. */
	t0 = now_ms();
	code = net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
	if (code == 0 && now_ms() - t0 < 1500)
		code = net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
	if (body) {
		*body = code >= 200 && code < 300 ? slurp(tmp, len) : NULL;
		if (code >= 200 && code < 300 && !*body) code = -1;
	}
	unlink(tmp);
	return code;
}

static int plex_req(const char *method, const char *url, bool with_token,
                    char **body, size_t *len, int timeout_s)
{
	return plex_req_c(method, url, with_token, body, len, 5, timeout_s);
}

/* ---- connections ---------------------------------------------------------------- */

/* The server's address: the first of its connections to answer, local ones
 * first - plain http on the LAN, its https names, then remote, then Plex's
 * relay. Kept until a request through it gets no answer (base_failed).
 *
 * Away from home (plorpos-1lw.19): the LAN addresses are probed briefly, as
 * a server there answers in milliseconds, and the https names - the same
 * addresses - are not tried when none of them answered at all. When no
 * connection answers, the list is asked of plex.tv again (a relay's address
 * changes; the one saved at sign-in had, a day later) and tried once more.
 * One thread probes at a time; the others wait for its answer. */
static void cfg_save(void);
static pthread_mutex_t g_pick_mu = PTHREAD_MUTEX_INITIALIZER;

static bool try_conns(char *out, size_t n)
{
	char url[400];
	conn cs[MAX_CONNS];
	int pass, i, nc, code;
	bool lan = false;

	LOCK();
	memcpy(cs, C.conns, sizeof cs);
	nc = C.nconns;
	UNLOCK();
	for (pass = 0; pass < 4; pass++)
		for (i = 0; i < nc; i++) {
			conn *c = &cs[i];
			bool http = !strncmp(c->uri, "http:", 5), local = c->local && !c->relay;

			if (pass == 0 && !(local && http)) continue;
			if (pass == 1 && !(local && !http && lan)) continue;
			if (pass == 2 && (c->local || c->relay)) continue;
			if (pass == 3 && !c->relay) continue;
			snprintf(url, sizeof url, "%s/identity", c->uri);
			code = local ? plex_req_c("GET", url, true, NULL, NULL, 2, 3)
			             : plex_req_c("GET", url, true, NULL, NULL, 5, 6);
			if (code != 0) lan |= local;
			if (code == 200) {
				LOCK();
				snprintf(g_base, sizeof g_base, "%s", c->uri);
				UNLOCK();
				fprintf(stderr, "plex: server %s via %s connection %d\n", C.server,
				        c->relay ? "relay" : c->local ? "local" : "remote", i);
				snprintf(out, n, "%s", c->uri);
				return true;
			}
		}
	return false;
}

static bool refresh_conns(void);

static bool pick_base(char *out, size_t n)
{
	bool ok;

	pthread_mutex_lock(&g_pick_mu);
	LOCK();
	snprintf(out, n, "%s", g_base);
	UNLOCK();
	ok = out[0] || try_conns(out, n) || (refresh_conns() && try_conns(out, n));
	if (!ok) fprintf(stderr, "plex: no connection to %s answered\n", C.server);
	pthread_mutex_unlock(&g_pick_mu);
	return ok;
}

static void base_failed(void)
{
	LOCK(); g_base[0] = '\0'; UNLOCK();
}

/* plex_resolve's connection, found on the worker. */
static volatile bool g_picking;
static unsigned g_pick_failed;              /* when the last one found none; 0 never */

static void do_pick(void)
{
	char b[256];
	bool ok = pick_base(b, sizeof b);

	LOCK();
	g_picking = false;
	g_pick_failed = ok ? 0 : (now_ms() | 1);
	UNLOCK();
}

bool plex_stream_failed(const char *rel)
{
	if (!plex_is_path(rel)) return false;
	fprintf(stderr, "plex: a stream failed; finding the server again\n");
	base_failed();
	return true;
}

/* A server request by path, the reply into memory. */
static int server_get(const char *path, char **body, size_t *len, int timeout_s)
{
	char base[256], url[1200];
	int code;

	if (!pick_base(base, sizeof base)) return 0;
	snprintf(url, sizeof url, "%s%s", base, path);
	code = plex_req("GET", url, true, body, len, timeout_s);
	if (code == 0) base_failed();
	return code;
}

/* ---- the worker queue ----------------------------------------------------------- */

enum { J_PIN, J_SERVERS, J_LIBS, J_COVER, J_TIMELINE, J_FAVS, J_PICK };
static void do_favs(void);
static void do_pick(void);
typedef struct job {
	int kind;
	char a[320], b[600];
	long n1, n2, n3;
	struct job *next;
} job;

static pthread_t g_api;
static bool g_api_up;
static pthread_cond_t g_api_cv = PTHREAD_COND_INITIALIZER;
static job *g_jobs;                       /* a stack: the newest cover first */
static int  g_njobs;

static void push(job *j, bool front)
{
	LOCK();
	if (front || !g_jobs) { j->next = g_jobs; g_jobs = j; }
	else { job *t = g_jobs; while (t->next) t = t->next; t->next = j; j->next = NULL; }
	g_njobs++;
	pthread_cond_signal(&g_api_cv);
	UNLOCK();
}

/* Sign-in state. */
static volatile int g_pin_state;          /* 0 idle, 1 waiting, 2 linked, -1 failed */
static long g_pin_id;
static char g_pin_code[16];
static unsigned g_pin_until;

/* Choices for the pickers. */
typedef struct { char name[128], id[64]; bool owned; conn conns[MAX_CONNS]; int nconns; } server_t;
static server_t g_servers[MAX_SERVERS];
static volatile int g_nservers;            /* 0 waiting, -1 failed */
typedef struct { char name[128]; int key; } lib_t;
static lib_t g_libs[MAX_LIBS];
static volatile int g_nlibs;

/* Covers that landed. */
#define LANDED 32
static char g_landed[LANDED][600];
static int  g_nlanded;

static void do_pin(job *j)
{
	char url[300], *b = NULL;
	size_t len;
	jsv root, v;

	if (j->n1 == 0) {   /* create */
		if (plex_req("POST", PLEX_TV "/pins?strong=false", false, &b, &len, 15) != 201 &&
		    !b) { g_pin_state = -1; return; }
		if (!b) { g_pin_state = -1; return; }
		root = js_root(b, len);
		LOCK();
		g_pin_id = jnum(root, "id");
		jstr(root, "code", g_pin_code, sizeof g_pin_code);
		g_pin_until = now_ms() + 14 * 60 * 1000;
		UNLOCK();
		free(b);
		if (!g_pin_id || !g_pin_code[0]) g_pin_state = -1;
		return;
	}
	snprintf(url, sizeof url, PLEX_TV "/pins/%ld", g_pin_id);
	if (plex_req("GET", url, false, &b, &len, 10) == 200 && b) {
		char tok[128] = "";

		root = js_root(b, len);
		if (js_member(root, "authToken", &v)) js_str(v, tok, sizeof tok);
		free(b);
		if (tok[0]) {
			LOCK();
			snprintf(C.token, sizeof C.token, "%s", tok);
			UNLOCK();
			/* The account's name, for Plex Settings to say who is signed in. */
			if (plex_req("GET", PLEX_TV "/user", true, &b, &len, 10) == 200 && b) {
				root = js_root(b, len);
				LOCK();
				jstr(root, "username", C.account, sizeof C.account);
				if (!C.account[0]) jstr(root, "title", C.account, sizeof C.account);
				UNLOCK();
				free(b);
			}
			cfg_save();
			fprintf(stderr, "plex: signed in\n");
			g_pin_state = 2;
		}
	}
}

static void do_servers(void)
{
	char *b = NULL;
	size_t len;
	jsv root, it = { 0 }, r;
	int n = 0;

	if (plex_req("GET", PLEX_TV "/resources?includeHttps=1&includeRelay=1", true, &b, &len, 15) != 200 || !b) {
		g_nservers = -1;
		return;
	}
	root = js_root(b, len);
	while (js_next(root, &it, &r) && n < MAX_SERVERS) {
		char provides[128];
		server_t *s = &g_servers[n];
		jsv conns, ci = { 0 }, c;
		int pass;

		jstr(r, "provides", provides, sizeof provides);
		if (!strstr(provides, "server")) continue;
		memset(s, 0, sizeof *s);
		jstr(r, "name", s->name, sizeof s->name);
		jstr(r, "clientIdentifier", s->id, sizeof s->id);
		{ jsv o; s->owned = js_member(r, "owned", &o) && js_is_true(o); }
		if (!js_member(r, "connections", &conns)) continue;
		/* Local plain http first (address:port), as its own entry, then the
		 * connections as plex.tv lists them. */
		for (pass = 0; pass < 2; pass++) {
			memset(&ci, 0, sizeof ci);
			while (js_next(conns, &ci, &c) && s->nconns < MAX_CONNS) {
				jsv o;
				bool local = js_member(c, "local", &o) && js_is_true(o);
				bool relay = js_member(c, "relay", &o) && js_is_true(o);
				conn *k = &s->conns[s->nconns];

				if (pass == 0) {
					char addr[64];
					if (!local || relay) continue;
					jstr(c, "address", addr, sizeof addr);
					if (!addr[0]) continue;
					snprintf(k->uri, sizeof k->uri, "http://%s:%ld", addr, jnum(c, "port"));
				} else {
					jstr(c, "uri", k->uri, sizeof k->uri);
				}
				k->local = local; k->relay = relay;
				if (k->uri[0]) s->nconns++;
			}
		}
		n++;
	}
	free(b);
	g_nservers = n > 0 ? n : -1;
}

/* The server's connections as plex.tv lists them now. True when they were
 * read; at most once a minute. */
static bool refresh_conns(void)
{
	static unsigned last;
	unsigned now = now_ms();
	int i;

	if (last && now - last < 60000) return false;
	last = now;
	do_servers();
	for (i = 0; i < g_nservers; i++) {
		if (strcmp(g_servers[i].id, C.server_id)) continue;
		LOCK();
		memcpy(C.conns, g_servers[i].conns, sizeof C.conns);
		C.nconns = g_servers[i].nconns;
		UNLOCK();
		cfg_save();
		fprintf(stderr, "plex: %d connections from plex.tv\n", C.nconns);
		return true;
	}
	return false;
}

static void do_libs(void)
{
	char *b = NULL;
	size_t len;
	jsv root, mc, dirs, it = { 0 }, d;
	int n = 0;

	if (server_get("/library/sections", &b, &len, 15) != 200 || !b) { g_nlibs = -1; return; }
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Directory", &dirs))
		while (js_next(dirs, &it, &d) && n < MAX_LIBS) {
			char type[32];

			jstr(d, "type", type, sizeof type);
			if (strcmp(type, "artist")) continue;
			jstr(d, "title", g_libs[n].name, sizeof g_libs[n].name);
			g_libs[n].key = (int)jnum(d, "key");
			n++;
		}
	free(b);
	g_nlibs = n > 0 ? n : -1;
}

static void do_cover(job *j)
{
	char base[256], url[1400], enc[700], dest[620], part[640];
	char hdr[160];
	const char *h[1] = { hdr };
	int code;

	if (!pick_base(base, sizeof base)) return;
	urlenc(j->a, enc, sizeof enc);
	snprintf(url, sizeof url, "%s/photo/:/transcode?width=%d&height=%d&minSize=1&upscale=1&url=%s",
	         base, COVER_PX, COVER_PX, enc);
	snprintf(hdr, sizeof hdr, "X-Plex-Token: %s", C.token);
	snprintf(dest, sizeof dest, "%s.jpg", j->b);
	snprintf(part, sizeof part, "%s.part", dest);
	{
		char d[620], *s;
		snprintf(d, sizeof d, "%s", dest);
		if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	}
	code = net_fetch(url, "GET", h, 1, part, 5, 30, NULL);
	if (code == 200 && rename(part, dest) == 0) {
		LOCK();
		if (g_nlanded < LANDED) snprintf(g_landed[g_nlanded++], sizeof g_landed[0], "%s", j->b);
		UNLOCK();
	} else {
		unlink(part);
		if (code == 0) base_failed();
	}
}

static void do_report(job *j)
{
	char path[600];

	snprintf(path, sizeof path,
	         "/:/timeline?ratingKey=%ld&key=%%2Flibrary%%2Fmetadata%%2F%ld&state=%s"
	         "&time=%ld&duration=%ld&identifier=com.plexapp.plugins.library",
	         j->n1, j->n1, j->a, j->n2, j->n3);
	server_get(path, NULL, NULL, 5);
}

static void *api_main(void *arg)
{
	(void)arg;
	for (;;) {
		job *j;

		LOCK();
		while (!g_jobs) pthread_cond_wait(&g_api_cv, &g_mu);
		j = g_jobs;
		g_jobs = j->next;
		g_njobs--;
		UNLOCK();
		switch (j->kind) {
		case J_PIN:      do_pin(j); break;
		case J_SERVERS:  do_servers(); break;
		case J_LIBS:     do_libs(); break;
		case J_COVER:    do_cover(j); break;
		case J_TIMELINE: do_report(j); break;
		case J_FAVS:     do_favs(); break;
		case J_PICK:     do_pick(); break;
		}
		free(j);
	}
	return NULL;
}

static job *job_new(int kind)
{
	job *j = calloc(1, sizeof *j);

	if (j) j->kind = kind;
	return j;
}

/* ---- the track table -------------------------------------------------------------- */

typedef struct { int key; char *name; } px_artist;
typedef struct { int key, artist, year, first, n; char *title, *thumb; } px_album;
typedef struct { int key, album, dur_ms; short disc, idx; char *title, *artist, *part; } px_track;
typedef struct {
	px_artist *ar; int nar;
	px_album  *al; int nal;
	px_track  *tr; int ntr;
	int *hash; int hcap;           /* track key -> index, open addressing */
	char *arena;
} px_table;

static px_table g_tab;             /* under g_mu */

static void tab_free(px_table *t)
{
	free(t->ar); free(t->al); free(t->tr); free(t->hash); free(t->arena);
	memset(t, 0, sizeof *t);
}

static int tab_find(const px_table *t, int key)
{
	unsigned h;

	if (!t->hcap) return -1;
	for (h = (unsigned)key * 2654435761u % (unsigned)t->hcap; t->hash[h] >= 0;
	     h = (h + 1) % (unsigned)t->hcap)
		if (t->tr[t->hash[h]].key == key) return t->hash[h];
	return -1;
}

static void index_path(char *out, size_t n)
{
	snprintf(out, n, "%s/%s/index.tsv", g_card, g_rel_root);
}

/* Split a line on tabs in place. */
static int split(char *s, char **f, int max)
{
	int n = 0;

	f[n++] = s;
	while (n < max && (s = strchr(s, '\t'))) { *s++ = '\0'; f[n++] = s; }
	return n;
}

static bool tab_read(px_table *t)
{
	char p[700], *buf, *line, *next;
	size_t len;
	int car = 0, cal = 0, ctr = 0;

	memset(t, 0, sizeof *t);
	index_path(p, sizeof p);
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
			t->ar[t->nar++] = (px_artist){ atoi(f[1]), f[2] };
		} else if (f[0][0] == 'L' && nf >= 5 && t->nar) {
			if (t->nal == cal) { cal = cal ? cal * 2 : 1024; t->al = realloc(t->al, sizeof *t->al * (size_t)cal); }
			t->al[t->nal++] = (px_album){ atoi(f[1]), t->nar - 1, atoi(f[2]), t->ntr, 0, f[4], f[3] };
		} else if (f[0][0] == 'T' && nf >= 8 && t->nal) {
			if (t->ntr == ctr) { ctr = ctr ? ctr * 2 : 8192; t->tr = realloc(t->tr, sizeof *t->tr * (size_t)ctr); }
			t->tr[t->ntr++] = (px_track){ atoi(f[1]), t->nal - 1, atoi(f[2]),
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

/* ---- the library sync ------------------------------------------------------------ */

typedef struct {
	int key, album, artist, year, disc, idx, dur;
	char *title, *tartist, *album_t, *artist_t, *thumb, *part;
} srec;

static pthread_t g_sync_th;
static volatile int g_sync_run, g_sync_done, g_sync_total, g_sync_failed, g_sync_new;
static int g_sync_force;

static int cmp_srec(const void *pa, const void *pb)
{
	const srec *a = pa, *b = pb;
	int c;

	if ((c = strcasecmp(a->artist_t, b->artist_t))) return c;
	if (a->artist != b->artist) return a->artist < b->artist ? -1 : 1;
	if ((c = strcasecmp(a->album_t, b->album_t))) return c;
	if (a->album != b->album) return a->album < b->album ? -1 : 1;
	if (a->disc != b->disc) return a->disc < b->disc ? -1 : 1;
	if (a->idx != b->idx) return a->idx < b->idx ? -1 : 1;
	return a->key < b->key ? -1 : a->key > b->key;
}

/* Tabs and newlines are the index's structure. */
static void clean(char *s)
{
	for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static char *dupc(const char *s)
{
	char *d = strdup(s ? s : "");

	if (d) clean(d);
	return d;
}

static long library_stamp(void)
{
	char *b = NULL;
	size_t len;
	jsv root, mc, dirs, it = { 0 }, d;
	long stamp = -1;

	if (server_get("/library/sections", &b, &len, 15) != 200 || !b) return -1;
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Directory", &dirs))
		while (js_next(dirs, &it, &d))
			if (jnum(d, "key") == C.section) {
				stamp = jnum(d, "contentChangedAt");
				if (!stamp) stamp = jnum(d, "updatedAt");
			}
	free(b);
	return stamp;
}

static long index_stamp(void)
{
	char p[700], line[200];
	FILE *f;
	long s = -1;

	index_path(p, sizeof p);
	if (!(f = fopen(p, "r"))) return -1;
	if (fgets(line, sizeof line, f) && line[0] == 'P') {
		char *x[6];
		if (split(line, x, 6) >= 3) s = atol(x[2]);
	}
	fclose(f);
	return s;
}

static void *sync_main(void *arg)
{
	srec *r = NULL;
	int nr = 0, cap = 0, start = 0, total = -1, i;
	long stamp;
	char path[400], p[700], tmp[720];
	unsigned t0 = now_ms();
	FILE *f;

	(void)arg;
	g_sync_done = 0; g_sync_total = 0;
	stamp = library_stamp();
	if (stamp < 0) { g_sync_failed = 1; goto out; }
	if (!g_sync_force && stamp == index_stamp()) goto out;   /* unchanged */
	fprintf(stderr, "plex: syncing %s (%s)\n", C.library, g_sync_force ? "asked" : "changed");
	for (;;) {
		char *b = NULL;
		size_t len;
		jsv root, mc, md, it = { 0 }, t;
		int code, got = 0;

		snprintf(path, sizeof path,
		         "/library/sections/%d/all?type=10&X-Plex-Container-Start=%d&X-Plex-Container-Size=%d",
		         C.section, start, PAGE);
		code = server_get(path, &b, &len, 120);
		if (code != 200 || !b) { free(b); g_sync_failed = 1; goto out; }
		root = js_root(b, len);
		if (!js_member(root, "MediaContainer", &mc)) { free(b); g_sync_failed = 1; goto out; }
		if (total < 0) { total = (int)jnum(mc, "totalSize"); g_sync_total = total; }
		if (js_member(mc, "Metadata", &md))
			while (js_next(md, &it, &t)) {
				char s[512], ta[256], at[256], alt[256], th[256], pk[320];
				jsv media, mi = { 0 }, m0, parts, pi = { 0 }, p0;

				got++;
				pk[0] = '\0';
				if (js_member(t, "Media", &media) && js_next(media, &mi, &m0) &&
				    js_member(m0, "Part", &parts) && js_next(parts, &pi, &p0))
					jstr(p0, "key", pk, sizeof pk);
				if (!pk[0]) continue;
				if (nr == cap) {
					cap = cap ? cap * 2 : 4096;
					srec *nr_ = realloc(r, sizeof *r * (size_t)cap);
					if (!nr_) { free(b); g_sync_failed = 1; goto out; }
					r = nr_;
				}
				jstr(t, "title", s, sizeof s);
				jstr(t, "originalTitle", ta, sizeof ta);
				jstr(t, "grandparentTitle", at, sizeof at);
				jstr(t, "parentTitle", alt, sizeof alt);
				jstr(t, "parentThumb", th, sizeof th);
				r[nr] = (srec){
					.key = (int)jnum(t, "ratingKey"),
					.album = (int)jnum(t, "parentRatingKey"),
					.artist = (int)jnum(t, "grandparentRatingKey"),
					.year = (int)jnum(t, "parentYear"),
					.disc = (int)jnum(t, "parentIndex"),
					.idx = (int)jnum(t, "index"),
					.dur = (int)jnum(t, "duration"),
					.title = dupc(s), .tartist = dupc(strcmp(ta, at) ? ta : ""),
					.album_t = dupc(alt[0] ? alt : "Unknown Album"),
					.artist_t = dupc(at[0] ? at : "Unknown Artist"),
					.thumb = dupc(th), .part = dupc(pk),
				};
				if (r[nr].key && r[nr].album && r[nr].artist) nr++;
			}
		free(b);
		start += got;
		g_sync_done = start;
		if (got == 0 || start >= total) break;
	}
	qsort(r, (size_t)nr, sizeof *r, cmp_srec);

	index_path(p, sizeof p);
	{
		char d[700], *s;
		snprintf(d, sizeof d, "%s", p);
		if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	}
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) { g_sync_failed = 1; goto out; }
	fprintf(f, "P\t1\t%ld\t%d\n", stamp, nr);
	for (i = 0; i < nr; i++) {
		if (i == 0 || r[i].artist != r[i - 1].artist)
			fprintf(f, "A\t%d\t%s\n", r[i].artist, r[i].artist_t);
		if (i == 0 || r[i].album != r[i - 1].album || r[i].artist != r[i - 1].artist)
			fprintf(f, "L\t%d\t%d\t%s\t%s\n", r[i].album, r[i].year, r[i].thumb, r[i].album_t);
		fprintf(f, "T\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", r[i].key, r[i].dur, r[i].disc,
		        r[i].idx, r[i].part, r[i].title, r[i].tartist);
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) { fclose(f); unlink(tmp); g_sync_failed = 1; goto out; }
	fclose(f);
	if (rename(tmp, p) != 0) { unlink(tmp); g_sync_failed = 1; goto out; }
	fprintf(stderr, "plex: %d tracks synced in %u ms\n", nr, now_ms() - t0);
	g_sync_new = 1;
out:
	if (g_sync_failed) fprintf(stderr, "plex: sync failed after %d tracks\n", g_sync_done);
	for (i = 0; i < nr; i++) {
		free(r[i].title); free(r[i].tartist); free(r[i].album_t);
		free(r[i].artist_t); free(r[i].thumb); free(r[i].part);
	}
	free(r);
	g_sync_run = 0;
	return NULL;
}

/* ---- downloads -------------------------------------------------------------------- */

typedef struct { int key; char reason; } dl_item;
static pthread_t g_dl_th;
static bool g_dl_up;
static pthread_cond_t g_dl_cv = PTHREAD_COND_INITIALIZER;
static dl_item *g_dlq; static int g_dlq_n, g_dlq_cap, g_dl_pos, g_dl_total;
static volatile int g_dl_cancel;
/* Cancel Download on an album (1lw.18): skip only the track in flight, not the
 * queue behind it, and once it has stopped, clear away album g_dl_purge. */
static bool g_dl_skip;
/* Plex's favorites (plorpos-1lw.16): the loved tracks' keys, in the library's
 * order, and whether the playlist wants writing once the downloads drain. */
static int *g_fav, g_nfav;
static bool g_fav_m3u;
static void favs_m3u_write(void);
static int g_dl_purge = -1;
static void purge_album(int al);
static char g_dl_now[160];
static volatile int g_dl_changed;

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

/* The downloaded copy's path, with `ext`. Under g_mu, with t from g_tab. */
static void local_path(const px_track *t, const char *ext, char *out, size_t n, bool card_rel)
{
	const px_album *al = &g_tab.al[t->album];
	char ar[100], alb[100], ti[100];

	safe_name(g_tab.ar[al->artist].name, ar, sizeof ar);
	safe_name(al->title, alb, sizeof alb);
	safe_name(t->title, ti, sizeof ti);
	if (t->disc > 1) snprintf(out, n, "%s%sMusic/" ML_PLEX "/%s/%s/%d%02d %s.%s",
	                          card_rel ? "" : g_card, card_rel ? "" : "/", ar, alb, t->disc, t->idx, ti, ext);
	else snprintf(out, n, "%s%sMusic/" ML_PLEX "/%s/%s/%02d %s.%s",
	              card_rel ? "" : g_card, card_rel ? "" : "/", ar, alb, t->idx, ti, ext);
}

static bool exists(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* The copy that is there, original or Opus, whichever quality it came at. */
static bool local_copy(const px_track *t, char *out, size_t n)
{
	local_path(t, ext_of(t->part), out, n, false);
	if (exists(out)) return true;
	local_path(t, "opus", out, n, false);
	return exists(out);
}

static void manifest_path(char *out, size_t n) { snprintf(out, n, "%s/downloads.tsv", g_dir); }

/* The manifest: why each copy is there ('a' an album, 'f' a favorite), so a
 * favorites sync deletes only what it put there. */
/* Its own lock: the shelf, the download worker and the favorites job all
 * rewrite it. Never held with LOCK taken inside it. */
static pthread_mutex_t g_man_mu = PTHREAD_MUTEX_INITIALIZER;

static void manifest_set(int key, char reason, bool on)
{
	char p[700], tmp[720], line[200];
	FILE *in, *out;
	bool had = false;

	manifest_path(p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	pthread_mutex_lock(&g_man_mu);
	if (!(out = fopen(tmp, "w"))) { pthread_mutex_unlock(&g_man_mu); return; }
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
	pthread_mutex_unlock(&g_man_mu);
}

static bool manifest_has(int key, char reason)
{
	char p[700], line[200];
	FILE *in;
	bool has = false;

	manifest_path(p, sizeof p);
	pthread_mutex_lock(&g_man_mu);
	if ((in = fopen(p, "r"))) {
		while (!has && fgets(line, sizeof line, in)) {
			char *tab = strchr(line, '\t');
			has = atoi(line) == key && tab && tab[1] == reason;
		}
		fclose(in);
	}
	pthread_mutex_unlock(&g_man_mu);
	return has;
}

static void stream_url(const char *base, const px_track *t, int kbps, bool download,
                       char *out, size_t n, const char *verb)
{
	/* One session per play, the same one through a download's three
	 * requests (decision, start, stop) - so the session is the caller's. */
	static unsigned seq;
	unsigned sess = download ? (unsigned)t->key : now_ms() + ++seq;

	if (!kbps) {
		snprintf(out, n, "%s%s?X-Plex-Token=%s", base, t->part, C.token);
		return;
	}
	snprintf(out, n,
	         "%s/audio/:/transcode/universal/%s?path=%%2Flibrary%%2Fmetadata%%2F%d"
	         "&musicBitrate=%d&directPlay=0&session=px%d-%u&X-Plex-Session-Identifier=px%d-%u"
	         "%s&X-Plex-Client-Identifier=%s&X-Plex-Product=" PRODUCT
	         "&X-Plex-Platform=" PLATFORM "&X-Plex-Client-Profile-Extra=%s"
	         "&X-Plex-Token=%s",
	         base, verb, t->key, kbps, t->key, sess, t->key, sess,
	         download ? "&X-Plex-Chunked=1" : "", C.client, OPUS_PROFILE, C.token);
}

/* Track `t` (its strings the caller's own) at `kbps` into `dest`, through
 * `<dest>.part`: the original file, or Plex's Opus transcode the way the pak
 * downloads one (decision, start, stop). Returns the HTTP status of the
 * start, 0 for no answer; the file is there only on 200. */
static int fetch_track(const char *base, const px_track *t, int kbps, const char *dest,
                       const volatile int *cancel)
{
	char url[2048], part[1100], tmp[128];
	int code;

	snprintf(part, sizeof part, "%s.part", dest);
	if (kbps) {
		stream_url(base, t, kbps, true, url, sizeof url, "decision");
		tmpfile_for("dec", tmp, sizeof tmp);
		net_fetch(url, "GET", NULL, 0, tmp, 5, 30, cancel);
		unlink(tmp);
	}
	stream_url(base, t, kbps, true, url, sizeof url, "start");
	code = net_fetch(url, "GET", NULL, 0, part, 5, 0, cancel);
	if (code != 200 || !exists(part) || rename(part, dest) != 0) {
		unlink(part);
		if (code == 200) code = -1;
	}
	if (kbps) {
		stream_url(base, t, kbps, true, url, sizeof url, "stop");
		tmpfile_for("stop", tmp, sizeof tmp);
		net_fetch(url, "GET", NULL, 0, tmp, 5, 10, NULL);
		unlink(tmp);
	}
	if (code == 0) base_failed();
	return code;
}

/* A copy of track `key` safe to use with the lock released: the table can
 * be replaced under us (a sync landing). False when it is not in it. */
static bool track_copy(int key, px_track *t, char *partk, size_t pn, char *thumb, size_t tn)
{
	int i;

	LOCK();
	if ((i = tab_find(&g_tab, key)) < 0) { UNLOCK(); return false; }
	*t = g_tab.tr[i];
	snprintf(partk, pn, "%s", t->part);
	if (thumb) snprintf(thumb, tn, "%s", g_tab.al[t->album].thumb);
	t->part = partk;
	t->title = t->artist = NULL;
	UNLOCK();
	return true;
}

static bool download_one(int key, char reason)
{
	char base[256], url[2048], dest[1024], part[1100], dir[1024], partk[320], thumb[320], *s;
	px_track t;
	int kbps = C.download_kbps, code;

	LOCK();
	{
		int i = tab_find(&g_tab, key);
		if (i < 0) { UNLOCK(); return false; }
		if (local_copy(&g_tab.tr[i], dest, sizeof dest)) {
			UNLOCK();
			manifest_set(key, reason, true);
			return true;
		}
		local_path(&g_tab.tr[i], kbps ? "opus" : ext_of(g_tab.tr[i].part), dest, sizeof dest, false);
		snprintf(g_dl_now, sizeof g_dl_now, "%s", g_tab.tr[i].title);
	}
	UNLOCK();
	if (!track_copy(key, &t, partk, sizeof partk, thumb, sizeof thumb)) return false;
	if (!pick_base(base, sizeof base)) return false;
	snprintf(dir, sizeof dir, "%s", dest);
	if ((s = strrchr(dir, '/'))) { *s = '\0'; mkdirs(dir); }
	/* The album's art beside it as cover.jpg, which the Local source reads
	 * (ml_folder_image): a transcode carries no picture of its own. */
	if (thumb[0]) {
		char cov[1100], enc[700], hdr[160];
		const char *h[1] = { hdr };

		snprintf(cov, sizeof cov, "%s/cover.jpg", dir);
		if (!exists(cov)) {
			urlenc(thumb, enc, sizeof enc);
			snprintf(url, sizeof url, "%s/photo/:/transcode?width=%d&height=%d&minSize=1&upscale=1&url=%s",
			         base, COVER_PX, COVER_PX, enc);
			snprintf(hdr, sizeof hdr, "X-Plex-Token: %s", C.token);
			snprintf(part, sizeof part, "%s.part", cov);
			if (net_fetch(url, "GET", h, 1, part, 5, 30, &g_dl_cancel) != 200 || rename(part, cov) != 0)
				unlink(part);
		}
	}
	code = fetch_track(base, &t, kbps, dest, &g_dl_cancel);
	if (code == 200) {
		manifest_set(key, reason, true);
		g_dl_changed = 1;
		return true;
	}
	fprintf(stderr, "plex: download of track %d failed (http %d)\n", key, code);
	if (code == 0) base_failed();
	return false;
}

/* ---- fetched first: a device whose FFmpeg cannot open the stream ---------------- */

static pthread_t g_cache_th;
static bool g_cache_up;
static pthread_cond_t g_cache_cv = PTHREAD_COND_INITIALIZER;
static int g_cache_q[4], g_cache_n;       /* keys, the one wanted now first */

static bool need_fetch(const char *base)
{
	if (!STREAM_HTTP) return true;
	return !STREAM_TLS && !strncmp(base, "https:", 6);
}

/* Where track t is cached (either quality), or where it would go. */
static bool cache_file(const px_track *t, char *out, size_t n)
{
	snprintf(out, n, "%s/cache/%d.opus", g_dir, t->key);
	if (exists(out)) return true;
	snprintf(out, n, "%s/cache/%d.%s", g_dir, t->key, ext_of(t->part));
	if (exists(out)) return true;
	if (C.stream_kbps) snprintf(out, n, "%s/cache/%d.opus", g_dir, t->key);
	return false;
}

/* The newest CACHE_KEEP stay; a fetch half done is never one of them. */
static void cache_trim(void)
{
	char d[700], p[1100];
	DIR *dd;
	struct dirent *e;
	struct { char name[64]; time_t m; } f[64];
	int n = 0, i, j;

	snprintf(d, sizeof d, "%s/cache", g_dir);
	if (!(dd = opendir(d))) return;
	while ((e = readdir(dd)) && n < 64) {
		struct stat st;

		if (e->d_name[0] == '.' || strstr(e->d_name, ".part")) continue;
		snprintf(p, sizeof p, "%s/%s", d, e->d_name);
		if (stat(p, &st) != 0) continue;
		snprintf(f[n].name, sizeof f[n].name, "%s", e->d_name);
		f[n++].m = st.st_mtime;
	}
	closedir(dd);
	for (i = 0; i < n; i++) {
		int newer = 0;

		for (j = 0; j < n; j++) if (f[j].m > f[i].m) newer++;
		if (newer >= CACHE_KEEP) { snprintf(p, sizeof p, "%s/%s", d, f[i].name); unlink(p); }
	}
}

static void *cache_main(void *arg)
{
	(void)arg;
	for (;;) {
		char base[256], dest[1100], partk[320];
		px_track t;
		int key, code;
		unsigned t0;

		LOCK();
		while (!g_cache_n) pthread_cond_wait(&g_cache_cv, &g_mu);
		key = g_cache_q[0];
		UNLOCK();
		if (track_copy(key, &t, partk, sizeof partk, NULL, 0) && !cache_file(&t, dest, sizeof dest) &&
		    pick_base(base, sizeof base)) {
			char d[700];

			snprintf(d, sizeof d, "%s/cache", g_dir);
			mkdirs(d);
			t0 = now_ms();
			code = fetch_track(base, &t, C.stream_kbps, dest, NULL);
			fprintf(stderr, "plex: track %d fetched first in %u ms (http %d)\n", key, now_ms() - t0, code);
			if (code == 0) base_failed();
			if (code == 200) cache_trim();
		}
		LOCK();
		if (g_cache_n && g_cache_q[0] == key) {
			memmove(g_cache_q, g_cache_q + 1, sizeof g_cache_q[0] * (size_t)--g_cache_n);
		}
		UNLOCK();
	}
	return NULL;
}

/* Fetch `key` - `now`: ahead of anything queued. Under g_mu. */
static void cache_want(int key, bool now)
{
	int i;

	for (i = 0; i < g_cache_n; i++) if (g_cache_q[i] == key) break;
	if (i < g_cache_n) {
		if (now && i > 1) { memmove(g_cache_q + 2, g_cache_q + 1, sizeof g_cache_q[0] * (size_t)(i - 1)); g_cache_q[1] = key; }
		return;
	}
	if (g_cache_n == 4) g_cache_n = 3;           /* the oldest wish goes */
	if (now && g_cache_n) {                      /* behind the one in flight */
		memmove(g_cache_q + 2, g_cache_q + 1, sizeof g_cache_q[0] * (size_t)(g_cache_n - 1));
		g_cache_q[1] = key;
	} else if (now) {
		g_cache_q[0] = key;
	} else {
		g_cache_q[g_cache_n] = key;
	}
	g_cache_n++;
	if (!g_cache_up) g_cache_up = pthread_create(&g_cache_th, NULL, cache_main, NULL) == 0;
	pthread_cond_signal(&g_cache_cv);
}

static void *dl_main(void *arg)
{
	(void)arg;
	for (;;) {
		dl_item it;

		LOCK();
		while (g_dl_pos >= g_dlq_n) {
			g_dlq_n = g_dl_pos = g_dl_total = 0;
			g_dl_now[0] = '\0';
			if (g_fav_m3u) {        /* a favorites sync's downloads are in */
				g_fav_m3u = false;
				UNLOCK();
				favs_m3u_write();
				LOCK();
				continue;
			}
			pthread_cond_wait(&g_dl_cv, &g_mu);
		}
		it = g_dlq[g_dl_pos];
		g_dl_cancel = 0;
		UNLOCK();
		download_one(it.key, it.reason);
		LOCK();
		if (g_dl_cancel && !g_dl_skip) { g_dlq_n = g_dl_pos = 0; g_dl_cancel = 0; }
		else g_dl_pos++;
		g_dl_skip = false;
		{ int al = g_dl_purge; g_dl_purge = -1; UNLOCK(); if (al >= 0) purge_album(al); }
	}
	return NULL;
}

static void dl_queue(int key, char reason)
{
	int i;

	for (i = g_dl_pos; i < g_dlq_n; i++) if (g_dlq[i].key == key) return;
	if (g_dlq_n == g_dlq_cap) {
		int cap = g_dlq_cap ? g_dlq_cap * 2 : 256;
		dl_item *q = realloc(g_dlq, sizeof *q * (size_t)cap);
		if (!q) return;
		g_dlq = q; g_dlq_cap = cap;
	}
	g_dlq[g_dlq_n++] = (dl_item){ key, reason };
	g_dl_total++;
}

static void dl_start(void)
{
	if (!g_dl_up) g_dl_up = pthread_create(&g_dl_th, NULL, dl_main, NULL) == 0;
	pthread_cond_signal(&g_dl_cv);
}

/* ---- the API ---------------------------------------------------------------------- */

void plex_init(const char *card, const char *shared)
{
	size_t cl = strlen(card);

	snprintf(g_card, sizeof g_card, "%s", card);
	snprintf(g_dir, sizeof g_dir, "%s/plex", shared);
	if (!strncmp(shared, card, cl) && shared[cl] == '/')
		snprintf(g_rel_base, sizeof g_rel_base, "%s/plex", shared + cl + 1);
	else
		snprintf(g_rel_base, sizeof g_rel_base, ".userdata/shared/plex");
	cfg_load();
	g_api_up = pthread_create(&g_api, NULL, api_main, NULL) == 0;
	if (g_api_up) pthread_detach(g_api);
}

bool plex_signed_in(void) { return C.token[0] != '\0'; }
bool plex_ready(void) { return C.token[0] && C.server_id[0] && C.section > 0; }
const char *plex_server_name(void) { return C.server; }
const char *plex_library_name(void) { return C.library; }
const char *plex_account_name(void) { return C.account; }

void plex_sign_out(void)
{
	plex_download_cancel();
	LOCK();
	C.token[0] = C.account[0] = C.server[0] = C.server_id[0] = C.library[0] = '\0';
	C.nconns = 0; C.section = 0; g_base[0] = '\0';
	rel_root_set();
	UNLOCK();
	cfg_save();
	fprintf(stderr, "plex: signed out\n");
}

void plex_pin_start(void)
{
	job *j = job_new(J_PIN);

	g_pin_state = 1;
	LOCK(); g_pin_code[0] = '\0'; g_pin_id = 0; UNLOCK();
	if (j) push(j, true); else g_pin_state = -1;
}

int plex_pin_poll(char *code, size_t n)
{
	static unsigned last;
	unsigned now = now_ms();

	LOCK();
	snprintf(code, n, "%s", g_pin_code);
	UNLOCK();
	if (g_pin_state == 2) return 1;
	if (g_pin_state < 0) return -1;
	if (g_pin_id && (int)(now - g_pin_until) > 0) return -1;
	/* One check every 2 s, never two queued. */
	if (g_pin_id && now - last > 2000) {
		job *j = job_new(J_PIN);
		last = now;
		if (j) { j->n1 = 1; push(j, false); }
	}
	return 0;
}

void plex_pin_cancel(void) { g_pin_state = 0; LOCK(); g_pin_id = 0; UNLOCK(); }

void plex_servers_ask(void)
{
	job *j = job_new(J_SERVERS);
	g_nservers = 0;
	if (j) push(j, true); else g_nservers = -1;
}
int plex_servers_poll(void) { return g_nservers; }
const char *plex_server_at(int i, bool *owned)
{
	if (i < 0 || i >= g_nservers) return "";
	if (owned) *owned = g_servers[i].owned;
	return g_servers[i].name;
}
void plex_server_pick(int i)
{
	if (i < 0 || i >= g_nservers) return;
	LOCK();
	snprintf(C.server, sizeof C.server, "%s", g_servers[i].name);
	snprintf(C.server_id, sizeof C.server_id, "%s", g_servers[i].id);
	memcpy(C.conns, g_servers[i].conns, sizeof C.conns);
	C.nconns = g_servers[i].nconns;
	C.section = 0; C.library[0] = '\0';
	g_base[0] = '\0';
	rel_root_set();
	UNLOCK();
	cfg_save();
}

void plex_libraries_ask(void)
{
	job *j = job_new(J_LIBS);
	g_nlibs = 0;
	if (j) push(j, true); else g_nlibs = -1;
}
int plex_libraries_poll(void) { return g_nlibs; }
const char *plex_library_at(int i) { return i >= 0 && i < g_nlibs ? g_libs[i].name : ""; }
void plex_library_pick(int i)
{
	if (i < 0 || i >= g_nlibs) return;
	LOCK();
	C.section = g_libs[i].key;
	snprintf(C.library, sizeof C.library, "%s", g_libs[i].name);
	rel_root_set();
	UNLOCK();
	cfg_save();
}

int plex_quality_at(int i) { return i >= 0 && i < PLEX_QUALITIES ? QUALITY[i] : 0; }
const char *plex_quality_label(int kbps)
{
	static char b[4][16];
	static int k;

	if (!kbps) return "Original";
	k = (k + 1) % 4;
	snprintf(b[k], sizeof b[k], "%d kbps", kbps);
	return b[k];
}
int  plex_stream_kbps(void)   { return C.stream_kbps; }
int  plex_download_kbps(void) { return C.download_kbps; }
void plex_set_stream_kbps(int kbps)   { C.stream_kbps = kbps; cfg_save(); }
void plex_set_download_kbps(int kbps) { C.download_kbps = kbps; cfg_save(); }

bool plex_lib_cached(void)
{
	char p[700];

	if (!plex_ready()) return false;
	index_path(p, sizeof p);
	return exists(p);
}

bool plex_lib_load(ml_lib *out)
{
	px_table t;
	int i, j;

	memset(out, 0, sizeof *out);
	if (!plex_ready() || !tab_read(&t)) return false;
	out->artists = calloc((size_t)(t.nar ? t.nar : 1), sizeof *out->artists);
	out->albums  = calloc((size_t)(t.nal ? t.nal : 1), sizeof *out->albums);
	out->tracks  = calloc((size_t)(t.ntr ? t.ntr : 1), sizeof *out->tracks);
	if (!out->artists || !out->albums || !out->tracks) { ml_free(out); tab_free(&t); return false; }
	for (i = 0; i < t.nar; i++) {
		snprintf(out->artists[i].name, sizeof out->artists[i].name, "%s", t.ar[i].name);
		out->artists[i].first = -1;
	}
	for (i = 0; i < t.nal; i++) {
		px_album *a = &t.al[i];
		ml_artist *ar = &out->artists[a->artist];

		snprintf(out->albums[i].name, sizeof out->albums[i].name, "%s", a->title);
		out->albums[i].first = a->first;
		out->albums[i].n = a->n;
		if (ar->first < 0) ar->first = i;
		ar->n++;
		for (j = a->first; j < a->first + a->n; j++) {
			px_track *tr = &t.tr[j];

			snprintf(out->tracks[j].name, sizeof out->tracks[j].name, "%s", tr->title);
			snprintf(out->tracks[j].path, sizeof out->tracks[j].path, "%s/%d/%d/%d.%s",
			         g_rel_root, t.ar[a->artist].key, a->key, tr->key, ext_of(tr->part));
		}
	}
	out->nartists = t.nar;
	out->nalbums = t.nal;
	out->ntracks = t.ntr;
	LOCK();
	tab_free(&g_tab);
	g_tab = t;
	UNLOCK();
	fprintf(stderr, "plex: %d artists, %d albums, %d tracks from the index\n", t.nar, t.nal, t.ntr);
	return true;
}

int plex_album_year(int al)
{
	int y;

	LOCK();
	y = al >= 0 && al < g_tab.nal ? g_tab.al[al].year : 0;
	UNLOCK();
	return y;
}

void plex_sync_start(bool force)
{
	if (!plex_ready() || g_sync_run) return;
	g_sync_run = 1;
	g_sync_failed = 0;
	g_sync_force = force || !plex_lib_cached();
	if (pthread_create(&g_sync_th, NULL, sync_main, NULL) != 0) { g_sync_run = 0; g_sync_failed = 1; return; }
	pthread_detach(g_sync_th);
}

int plex_sync_state(int *done, int *total)
{
	if (done) *done = g_sync_done;
	if (total) *total = g_sync_total;
	return g_sync_run ? 1 : g_sync_failed ? -1 : 0;
}

bool plex_sync_take(void)
{
	if (!g_sync_new) return false;
	g_sync_new = 0;
	return true;
}

bool plex_is_path(const char *rel)
{
	size_t n = strlen(g_rel_base);

	return rel && n && !strncmp(rel, g_rel_base, n) && rel[n] == '/';
}

const char *plex_rel_root(void) { return g_rel_root; }

int plex_track_index(const char *rel)
{
	int i;

	if (!plex_is_path(rel)) return -1;
	LOCK();
	i = tab_find(&g_tab, key_of(rel));
	UNLOCK();
	return i;
}

bool plex_title(const char *rel, char *out, size_t n)
{
	int i;

	if (!plex_is_path(rel)) return false;
	LOCK();
	i = tab_find(&g_tab, key_of(rel));
	if (i >= 0) snprintf(out, n, "%s", g_tab.tr[i].title);
	UNLOCK();
	return i >= 0;
}

void plex_pl_dir(char *out, size_t n) { snprintf(out, n, "%s/%s/Playlists", g_card, g_rel_root); }

bool plex_cover_want(int al, const char *base)
{
	job *j;

	LOCK();
	if (al < 0 || al >= g_tab.nal || !g_tab.al[al].thumb[0] || !C.token[0]) { UNLOCK(); return false; }
	j = job_new(J_COVER);
	if (j) {
		snprintf(j->a, sizeof j->a, "%s", g_tab.al[al].thumb);
		snprintf(j->b, sizeof j->b, "%s", base);
	}
	UNLOCK();
	if (!j) return false;
	push(j, true);   /* the newest first: what is on screen now */
	/* A fast scroll asks for hundreds: keep the newest 48, and an album
	 * scrolled past is asked for again if it comes back (COV_ASKED expires). */
	LOCK();
	{
		job **pp = &g_jobs;
		int covers = 0;

		while (*pp) {
			if ((*pp)->kind == J_COVER && ++covers > 48) {
				job *gone = *pp;
				*pp = gone->next;
				g_njobs--;
				free(gone);
				continue;
			}
			pp = &(*pp)->next;
		}
	}
	UNLOCK();
	return true;
}

bool plex_cover_take(char *base, size_t n)
{
	bool got = false;

	LOCK();
	if (g_nlanded) {
		snprintf(base, n, "%s", g_landed[--g_nlanded]);
		got = true;
	}
	UNLOCK();
	return got;
}

bool plex_resolve(const char *rel, musec_remote *out)
{
	char base[256], partk[320];
	int i;
	px_track t;

	if (!plex_is_path(rel)) return false;
	memset(out, 0, sizeof *out);
	LOCK();
	i = tab_find(&g_tab, key_of(rel));
	if (i < 0) {
		UNLOCK();
		/* Not in the table (a queue outliving its library): the daemon
		 * reports it unplayable and the queue moves on. */
		snprintf(out->url, sizeof out->url, "plex:missing");
		return true;
	}
	t = g_tab.tr[i];
	snprintf(out->title, sizeof out->title, "%s", t.title);
	/* The track's own artist is there only when it is not the album's
	 * (Plex's originalTitle): the album's otherwise, or a playlist showed
	 * its first track's artist on every track (plorpos-1lw.20). */
	snprintf(out->artist, sizeof out->artist, "%s",
	         t.artist && t.artist[0] ? t.artist : g_tab.ar[g_tab.al[t.album].artist].name);
	out->len = t.dur_ms / 1000.0;
	if (local_copy(&t, out->url, sizeof out->url)) { UNLOCK(); return true; }
	if (!g_base[0]) {
		/* No connection known (a fresh start, or the last one stopped
		 * answering): one is found off this thread, the queue's, and the
		 * player asks again on each poll (plorpos-1lw.19). A track fetched
		 * first plays meanwhile. */
		bool ask = !g_picking && (!g_pick_failed || now_ms() - g_pick_failed > 10000);

		if (cache_file(&t, out->url, sizeof out->url)) { UNLOCK(); return true; }
		if (ask) g_picking = true;
		UNLOCK();
		if (ask) { job *j = job_new(J_PICK); if (j) push(j, true); else g_picking = false; }
		out->wait = true;
		return true;
	}
	snprintf(base, sizeof base, "%s", g_base);
	if (need_fetch(base)) {
		if (!cache_file(&t, out->url, sizeof out->url)) {
			cache_want(t.key, true);
			out->wait = true;
		}
		UNLOCK();
		return true;
	}
	snprintf(partk, sizeof partk, "%s", t.part);
	t.part = partk; t.title = t.artist = NULL;
	UNLOCK();
	stream_url(base, &t, C.stream_kbps, false, out->url, sizeof out->url, "start");
	if (C.stream_kbps) snprintf(out->codec, sizeof out->codec, "OPUS %d", C.stream_kbps);
	return true;
}

bool plex_owns(const char *daemon_path, const char *rel)
{
	char want[64], copy[1024];
	int i, key = key_of(rel);
	bool yes = false;

	if (!plex_is_path(rel)) return false;
	snprintf(want, sizeof want, "%%2Flibrary%%2Fmetadata%%2F%d&", key);
	if (strstr(daemon_path, want)) return true;
	LOCK();
	if ((i = tab_find(&g_tab, key)) >= 0) {
		const char *pk = g_tab.tr[i].part;
		const char *at = strstr(daemon_path, pk);

		yes = at && (at[strlen(pk)] == '?' || !at[strlen(pk)]);
		if (!yes && local_copy(&g_tab.tr[i], copy, sizeof copy)) yes = !strcmp(copy, daemon_path);
		if (!yes && cache_file(&g_tab.tr[i], copy, sizeof copy)) yes = !strcmp(copy, daemon_path);
	}
	UNLOCK();
	return yes;
}

void plex_tick(const char *rel, int state, double at, double len)
{
	static int key, last_state = -1;
	static unsigned last_ms;
	static bool counted;
	static const char *const NAMES[] = { "stopped", "playing", "paused" };
	unsigned now = now_ms();
	int k = plex_is_path(rel) ? key_of(rel) : 0;
	job *j;

	if (state < 0 || state > 2) state = 0;
	/* A "stopped" timeline ends the client's transcode on the server - the
	 * one just started for the next track too, which then opens as nothing,
	 * fails, and the queue ran through the album at a track a frame
	 * (plorpos-1lw.13; measured on carol). So, as the pak: none when the
	 * next track is Plex's too (reporting it is enough), and none for a
	 * track that has not played yet (stopped while its stream opens). */
	if (k != key) {
		if (key && !k && last_state > 0 && (j = job_new(J_TIMELINE))) {
			j->n1 = key; snprintf(j->a, sizeof j->a, "stopped");
			push(j, false);
		}
		key = k; counted = false; last_state = -1;
	}
	if (!key || !C.token[0]) return;
	if (state == 0 && last_state <= 0) goto mark;
	if (state == last_state && (state != 1 || now - last_ms < TIMELINE_MS)) goto mark;
	last_state = state;
	last_ms = now;
	if ((j = job_new(J_TIMELINE))) {
		j->n1 = key; j->n2 = (long)(at * 1000); j->n3 = (long)(len * 1000);
		snprintf(j->a, sizeof j->a, "%s", NAMES[state]);
		push(j, false);
	}
mark:
	/* Plex counts the play itself from a timeline past 90%; a scrobble on
	 * top counted it twice (measured: viewCount 0 -> 2, carol, 2026-11-16).
	 * So no scrobble - a timeline the moment it crosses, in case the next
	 * 5 s one would come after the end. */
	if (!counted && len > 0 && at >= len * PLAYED_AT && (j = job_new(J_TIMELINE))) {
		counted = true;
		last_ms = now;
		j->n1 = key; j->n2 = (long)(at * 1000); j->n3 = (long)(len * 1000);
		snprintf(j->a, sizeof j->a, "%s", NAMES[state]);
		push(j, false);
	}
}

void plex_prefetch(const char *rel)
{
	static int last;
	char base[256], f[1100];
	int i, key = plex_is_path(rel) ? key_of(rel) : 0;

	if (!key || key == last) return;
	last = key;
	LOCK();
	snprintf(base, sizeof base, "%s", g_base);
	if (base[0] && need_fetch(base) && (i = tab_find(&g_tab, key)) >= 0 &&
	    !local_copy(&g_tab.tr[i], f, sizeof f) && !cache_file(&g_tab.tr[i], f, sizeof f))
		cache_want(key, false);
	UNLOCK();
}

int plex_album_downloaded(int al)
{
	char p[1024];
	int i, n = 0;

	LOCK();
	if (al >= 0 && al < g_tab.nal)
		for (i = g_tab.al[al].first; i < g_tab.al[al].first + g_tab.al[al].n; i++)
			if (local_copy(&g_tab.tr[i], p, sizeof p)) n++;
	UNLOCK();
	return n;
}

void plex_download_album(int al)
{
	int i;

	LOCK();
	if (al >= 0 && al < g_tab.nal)
		for (i = g_tab.al[al].first; i < g_tab.al[al].first + g_tab.al[al].n; i++)
			dl_queue(g_tab.tr[i].key, 'a');
	UNLOCK();
	dl_start();
}

/* Remove a copy and the folders it leaves empty, up to Music/Plex. */
static void remove_copy(const char *path)
{
	char d[1024], stop[600], *s;

	if (unlink(path) != 0) return;
	g_dl_changed = 1;
	snprintf(stop, sizeof stop, "%s/Music/" ML_PLEX, g_card);
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
	while ((s = strrchr(d, '/')) && (*s = '\0', strcmp(d, stop) && strlen(d) > strlen(stop)))
		if (rmdir(d) != 0) break;
}

void plex_delete_album(int al)
{
	char p[1024];
	int i, *keys = NULL, n = 0;

	LOCK();
	if (al >= 0 && al < g_tab.nal && (keys = malloc(sizeof *keys * (size_t)(g_tab.al[al].n + 1))))
		for (i = g_tab.al[al].first; i < g_tab.al[al].first + g_tab.al[al].n; i++) {
			if (local_copy(&g_tab.tr[i], p, sizeof p)) remove_copy(p);
			keys[n++] = g_tab.tr[i].key;
		}
	UNLOCK();
	for (i = 0; i < n; i++) { manifest_set(keys[i], 'a', false); manifest_set(keys[i], 'f', false); }
	free(keys);
}

/* Album al's copies that only its download wanted: a track that is also a
 * favourite keeps its copy. */
static void purge_album(int al)
{
	char p[1024];
	int i, *keys = NULL, n = 0;

	LOCK();
	if (al >= 0 && al < g_tab.nal && (keys = malloc(sizeof *keys * (size_t)(g_tab.al[al].n + 1))))
		for (i = g_tab.al[al].first; i < g_tab.al[al].first + g_tab.al[al].n; i++)
			keys[n++] = g_tab.tr[i].key;
	UNLOCK();
	for (i = 0; i < n; i++) {
		int t;

		manifest_set(keys[i], 'a', false);
		if (manifest_has(keys[i], 'f')) continue;
		LOCK();
		if ((t = tab_find(&g_tab, keys[i])) >= 0 && local_copy(&g_tab.tr[t], p, sizeof p))
			remove_copy(p);
		UNLOCK();
	}
	free(keys);
}

static bool in_album(int al, int key)
{
	int t = tab_find(&g_tab, key);

	return t >= g_tab.al[al].first && t < g_tab.al[al].first + g_tab.al[al].n;
}

bool plex_album_downloading(int al)
{
	int i;
	bool yes = false;

	LOCK();
	if (al >= 0 && al < g_tab.nal)
		for (i = g_dl_pos; i < g_dlq_n && !yes; i++) yes = in_album(al, g_dlq[i].key);
	UNLOCK();
	return yes;
}

void plex_download_cancel_album(int al)
{
	int i, k;
	bool later = false;

	LOCK();
	if (al < 0 || al >= g_tab.nal) { UNLOCK(); return; }
	/* The queued ones out; the one in flight, if it is this album's, stops
	 * itself and the worker clears up after it. */
	for (i = k = g_dl_pos + 1; i < g_dlq_n; i++)
		if (in_album(al, g_dlq[i].key)) g_dl_total--;
		else g_dlq[k++] = g_dlq[i];
	if (g_dl_pos < g_dlq_n) {
		g_dlq_n = k;
		if (in_album(al, g_dlq[g_dl_pos].key)) {
			g_dl_skip = true; g_dl_cancel = 1; g_dl_purge = al; later = true;
		}
	}
	UNLOCK();
	if (!later) purge_album(al);
}

static volatile int g_pl_gen;

/* PLEX_FAVS_M3U twice, each rewritten whole through a temporary file: in
 * <card>/Music/Playlists the copies there are, for the Local source; in the
 * Plex source's own playlists every loved track by its Plex path, which plays
 * the copy when there is one (plex_resolve) and streams it when not. */
static void favs_m3u_write(void)
{
	char dir[700], p[800], tmp[820], f[1100], pre[700];
	size_t pl;
	FILE *o, *x;
	int i, t;

	snprintf(dir, sizeof dir, "%s/" PL_DIR_REL, g_card);
	mkdirs(dir);
	snprintf(p, sizeof p, "%s/" PLEX_FAVS_M3U ".m3u", dir);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	snprintf(pre, sizeof pre, "%s/Music/", g_card);
	pl = strlen(pre);
	plex_pl_dir(dir, sizeof dir);
	mkdirs(dir);
	snprintf(f, sizeof f, "%s/" PLEX_FAVS_M3U ".m3u.new", dir);
	if (!(o = fopen(tmp, "w"))) return;
	if (!(x = fopen(f, "w"))) { fclose(o); unlink(tmp); return; }
	fputs("#EXTM3U\n", o);
	fputs("#EXTM3U\n", x);
	LOCK();
	for (i = 0; i < g_nfav; i++) {
		const px_track *tr;

		if ((t = tab_find(&g_tab, g_fav[i])) < 0) continue;
		tr = &g_tab.tr[t];
		/* Card-relative as pl_add writes a path outside Music: "../../". */
		fprintf(x, "../../%s/%d/%d/%d.%s\n", g_rel_root, g_tab.ar[g_tab.al[tr->album].artist].key,
		        g_tab.al[tr->album].key, tr->key, ext_of(tr->part));
		if (local_copy(tr, f, sizeof f) && !strncmp(f, pre, pl)) fprintf(o, "../%s\n", f + pl);
	}
	UNLOCK();
	if (fclose(o) == 0) rename(tmp, p);
	else unlink(tmp);
	snprintf(p, sizeof p, "%s/" PLEX_FAVS_M3U ".m3u", dir);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (fclose(x) == 0) rename(tmp, p);
	else unlink(tmp);
	g_dl_changed = 1;
	g_pl_gen++;
}

int plex_pl_gen(void) { return g_pl_gen; }

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* The job: the server's loved tracks (rated 10, Plexamp's love), downloaded;
 * the copies of ones loved no more gone; the playlist written now and again
 * when the downloads are in. */
static void do_favs(void)
{
	char path[200], line[200], mp[700], p[1024];
	char *b = NULL;
	size_t len;
	jsv root, mc, md, it = { 0 }, d;
	int *keys = NULL, n = 0, cap = 0, i, t;
	FILE *in;

	snprintf(path, sizeof path, "/library/sections/%d/all?type=10&userRating=10"
	         "&X-Plex-Container-Start=0&X-Plex-Container-Size=20000", C.section);
	if (server_get(path, &b, &len, 30) != 200 || !b) {
		free(b);
		fprintf(stderr, "plex: favorites: the server did not answer\n");
		return;
	}
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Metadata", &md))
		while (js_next(md, &it, &d)) {
			if (n == cap) {
				int *k = realloc(keys, sizeof *k * (size_t)(cap = cap ? cap * 2 : 64));
				if (!k) break;
				keys = k;
			}
			keys[n++] = (int)jnum(d, "ratingKey");
		}
	free(b);
	/* In the library's order: artist, album, track - as the shelf lists them.
	 * A key the index does not have yet (added since the last sync) waits
	 * for the next one. */
	LOCK();
	{
		int *ord = malloc(sizeof *ord * (size_t)(n ? n : 1)), m = 0;

		if (ord) {
			for (i = 0; i < n; i++) if ((t = tab_find(&g_tab, keys[i])) >= 0) ord[m++] = t;
			qsort(ord, (size_t)m, sizeof *ord, cmp_int);
			for (i = 0; i < m; i++) ord[i] = g_tab.tr[ord[i]].key;
			free(g_fav);
			g_fav = ord; g_nfav = m;
			for (i = 0; i < m; i++) dl_queue(g_fav[i], 'f');
		}
	}
	UNLOCK();
	fprintf(stderr, "plex: favorites: %d loved\n", n);
	/* Copies that came as favorites and are favorites no more, unless an
	 * album download wants them too. */
	manifest_path(mp, sizeof mp);
	pthread_mutex_lock(&g_man_mu);
	in = fopen(mp, "r");
	pthread_mutex_unlock(&g_man_mu);
	if (in) {
		int gone[512], ng = 0;

		while (fgets(line, sizeof line, in) && ng < 512) {
			int k = atoi(line), j;
			char *tab = strchr(line, '\t');

			if (!tab || tab[1] != 'f') continue;
			for (j = 0; j < n && keys[j] != k; j++) {}
			if (j == n) gone[ng++] = k;
		}
		fclose(in);
		for (i = 0; i < ng; i++) {
			manifest_set(gone[i], 'f', false);
			if (manifest_has(gone[i], 'a')) continue;
			LOCK();
			if ((t = tab_find(&g_tab, gone[i])) >= 0 && local_copy(&g_tab.tr[t], p, sizeof p))
				remove_copy(p);
			UNLOCK();
		}
	}
	free(keys);
	favs_m3u_write();
	LOCK();
	g_fav_m3u = true;
	UNLOCK();
	dl_start();
}

void plex_sync_favorites(void)
{
	job *j = job_new(J_FAVS);

	if (j) push(j, false);
}

void plex_download_cancel(void)
{
	LOCK();
	if (g_dl_pos < g_dlq_n) g_dl_cancel = 1;
	g_dl_skip = false;                    /* all of it, not one album's */
	g_dlq_n = g_dl_pos + (g_dl_pos < g_dlq_n ? 1 : 0);   /* the one in flight stops itself */
	UNLOCK();
}

bool plex_download_status(char *out, size_t n)
{
	bool on;

	LOCK();
	on = g_dl_pos < g_dlq_n;
	if (on) snprintf(out, n, "%d of %d - %s", g_dl_pos + 1, g_dl_total, g_dl_now);
	UNLOCK();
	return on;
}

bool plex_download_take_changed(void)
{
	if (!g_dl_changed) return false;
	g_dl_changed = 0;
	return true;
}
