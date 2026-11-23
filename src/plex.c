/* SPDX-License-Identifier: MIT */
/* See plex.h. The Plex API as PlexMusicPak uses it (the owner's NextUI
 * client): plex.tv's PIN sign-in and resources, a server's sections, tracks
 * and photo transcoder, the universal audio transcoder and the timeline.
 * Everything over net_fetch, which runs the device's curl. */
#include "plex.h"
#include "coverq.h"

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

/* A download failed (not cancelled): once, for a toast (plorpos-5eq.13). */
static volatile int g_dl_fail_note;
bool plex_download_take_failed(void)
{
	bool r = g_dl_fail_note != 0;

	g_dl_fail_note = 0;
	return r;
}

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
#define AHEAD_MAX   10         /* the most tracks fetched ahead (the setting's top) */
#define CACHE_Q     16         /* wishes waiting: the one now and those ahead */

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
	int  stream_kbps, download_kbps, ahead;
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
	fprintf(f, "section=%d\nlibrary=%s\nstream_kbps=%d\ndownload_kbps=%d\nahead=%d\n",
	        C.section, C.library, C.stream_kbps, C.download_kbps, C.ahead);
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
	C.ahead = 1;
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
			else if (!strcmp(line, "ahead"))         C.ahead = atoi(v);
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
/* Offline Mode (plorpos-5tm.16): nothing goes to a server. Every fetch comes
 * back as if no network answered; a track plays from the card or not at all. */
static volatile bool g_offline;

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
	code = g_offline ? 0 : net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
	if (code == 0 && now_ms() - t0 < 1500)
		code = g_offline ? 0 : net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
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

/* What the server says of itself at "/" (plorpos-5eq.9): whether it is tied
 * to a Plex Pass account - a shared user rides on the owner's - and whether
 * this account may download from it. -1 until it has been asked; asked each
 * time a connection is picked, so a pass bought or lapsed is seen. */
static volatile int g_srv_pass = -1, g_srv_sync = -1;

static void server_caps(const char *uri)
{
	char url[400], *b = NULL;
	size_t len;
	jsv root, mc, v;

	snprintf(url, sizeof url, "%.380s/", uri);
	if (plex_req_c("GET", url, true, &b, &len, 5, 6) != 200 || !b) { free(b); return; }
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc)) {
		int pass = js_member(mc, "myPlexSubscription", &v) ? js_is_true(v) : -1;
		int sync = js_member(mc, "allowSync", &v) ? js_is_true(v) : -1;

		if (pass != g_srv_pass || sync != g_srv_sync)
			fprintf(stderr, "plex: server Plex Pass %s, downloads %s\n",
			        pass < 0 ? "unknown" : pass ? "yes" : "no", sync < 0 ? "unknown" : sync ? "allowed" : "not allowed");
		g_srv_pass = pass;
		g_srv_sync = sync;
	}
	free(b);
}

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
				server_caps(c->uri);
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

int plex_pass(void) { return g_srv_pass; }

/* The quality asked of the server: Plex transcodes music only for a Plex
 * Pass server, so one known to have none is asked for the file as it is
 * (0, Original) whatever the settings say (plorpos-5eq.9). */
static int kbps_stream(void)   { return g_srv_pass == 0 ? 0 : C.stream_kbps; }
static int kbps_download(void) { return g_srv_pass == 0 ? 0 : C.download_kbps; }

int plex_allow_sync(void) { return g_srv_sync; }

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

enum { J_PIN, J_SERVERS, J_LIBS, J_TIMELINE, J_PLS, J_PLDL, J_PLEDIT, J_PICK, J_PLAYS };
static void do_pls(void);
static void do_pl_downloads(void);
static void do_pick(void);
struct job;
static void do_pl_edit(struct job *j);
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

static job *job_new(int kind);

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

/* Covers, fetched on coverq's threads (plorpos-ps8.9). */
static coverq *g_cq;

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

/* coverq's fetch: Plex's resize of the album's thumb, to <b>.jpg. */
static bool cover_fetch(const char *thumb, const char *b)
{
	char base[256], url[1400], enc[700], dest[620], part[640];
	char hdr[160];
	const char *h[1] = { hdr };
	int code;

	if (!pick_base(base, sizeof base)) return false;
	urlenc(thumb, enc, sizeof enc);
	snprintf(url, sizeof url, "%s/photo/:/transcode?width=%d&height=%d&minSize=1&upscale=1&url=%s",
	         base, COVER_PX, COVER_PX, enc);
	snprintf(hdr, sizeof hdr, "X-Plex-Token: %s", C.token);
	snprintf(dest, sizeof dest, "%s.jpg", b);
	snprintf(part, sizeof part, "%s.part", dest);
	{
		char d[620], *s;
		snprintf(d, sizeof d, "%s", dest);
		if ((s = strrchr(d, '/'))) { *s = '\0'; mkdirs(d); }
	}
	code = g_offline ? 0 : net_fetch(url, "GET", h, 1, part, 5, 30, NULL);
	if (code == 200 && rename(part, dest) == 0) return true;
	unlink(part);
	if (code == 0) base_failed();
	return false;
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

/* Plays counted with no server to tell - Offline Mode, or no network
 * (plorpos-5tm.16.4): kept in plays.tsv, a track key a line, and each sent
 * as one scrobble when the server answers again. No timeline went for
 * them, so Plex counts each once (with a timeline a scrobble counted
 * twice). The file being sent is set aside, so a play made meanwhile is
 * not lost; one the server does not take goes back. */
static pthread_mutex_t g_plays_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile bool g_plays_busy;
static unsigned g_plays_last;

static void plays_path(char *out, size_t n, bool sending)
{
	snprintf(out, n, "%s/plays%s", g_dir, sending ? ".sending" : ".tsv");
}

static void plays_add(int key)
{
	char p[700];
	FILE *f;

	plays_path(p, sizeof p, false);
	pthread_mutex_lock(&g_plays_mu);
	if ((f = fopen(p, "a"))) { fprintf(f, "%d\n", key); fclose(f); }
	pthread_mutex_unlock(&g_plays_mu);
}

static void do_plays(void)
{
	char p[700], s[700], path[200], line[32];
	int sent = 0, kept = 0;
	FILE *f;

	plays_path(p, sizeof p, false);
	plays_path(s, sizeof s, true);
	pthread_mutex_lock(&g_plays_mu);
	if (access(s, F_OK) != 0) rename(p, s);   /* one left by a restart goes first */
	pthread_mutex_unlock(&g_plays_mu);
	if ((f = fopen(s, "r"))) {
		while (fgets(line, sizeof line, f)) {
			int key = atoi(line);

			if (key <= 0) continue;
			snprintf(path, sizeof path, "/:/scrobble?identifier=com.plexapp.plugins.library&key=%d", key);
			if (!g_offline && server_get(path, NULL, NULL, 10) == 200) sent++;
			else { plays_add(key); kept++; }
		}
		fclose(f);
		unlink(s);
	}
	if (sent || kept) fprintf(stderr, "plex: %d plays made offline sent%s\n", sent, kept ? ", some kept" : "");
	g_plays_busy = false;
}

void plex_plays_retry(void)
{
	char p[700], s[700];
	unsigned now = now_ms();
	job *j;

	if (g_plays_busy || g_offline || !C.token[0] || (g_plays_last && now - g_plays_last < 30000u)) return;
	g_plays_last = now;
	plays_path(p, sizeof p, false);
	plays_path(s, sizeof s, true);
	if ((access(p, F_OK) != 0 && access(s, F_OK) != 0) || !net_online()) return;
	if ((j = job_new(J_PLAYS))) { g_plays_busy = true; push(j, false); }
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
		case J_TIMELINE: do_report(j); break;
		case J_PLS:      do_pls(); break;
		case J_PLDL:     do_pl_downloads(); break;
		case J_PLEDIT:   do_pl_edit(j); break;
		case J_PICK:     do_pick(); break;
		case J_PLAYS:    do_plays(); break;
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
static volatile int g_sync_last;        /* the last run: 1 up to date, 2 updated, -1 failed */
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

/* The index's header: P, version, the library stamp it was written at, its
 * tracks, the newest updatedAt among them and the server's track count then
 * (the last two since plorpos-1lw.24; -1 in an older index). The stamp, or -1
 * without an index. */
static long index_head(long *since, long *total)
{
	char p[700], line[200];
	FILE *f;
	long s = -1;

	*since = *total = -1;
	index_path(p, sizeof p);
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

/* ---- a listing, a page at a time ---- */

typedef bool (*each_fn)(jsv item, void *ctx);

/* Every item of `query`, PAGE at a time, through `each` (false: give up).
 * How many the listing has, or -1 when the server failed. `progress` counts
 * into g_sync_done / g_sync_total. */
static long fetch_all(const char *query, each_fn each, void *ctx, bool progress)
{
	char path[600];
	long start = 0, total = -1;

	for (;;) {
		char *b = NULL;
		size_t len;
		jsv root, mc, md, it = { 0 }, t;
		int got = 0;

		snprintf(path, sizeof path, "%s%cX-Plex-Container-Start=%ld&X-Plex-Container-Size=%d",
		         query, strchr(query, '?') ? '&' : '?', start, PAGE);
		if (server_get(path, &b, &len, 120) != 200 || !b) { free(b); return -1; }
		root = js_root(b, len);
		if (!js_member(root, "MediaContainer", &mc)) { free(b); return -1; }
		if (total < 0) {
			total = jnum(mc, "totalSize");          /* 0: a listing without one */
			if (progress) g_sync_total = (int)total;
		}
		if (js_member(mc, "Metadata", &md))
			while (js_next(md, &it, &t)) {
				got++;
				if (!each(t, ctx)) { free(b); return -1; }
			}
		free(b);
		start += got;
		if (progress) g_sync_done = (int)start;
		if (got == 0 || (total > 0 ? start >= total : got < PAGE)) break;
	}
	return total > 0 ? total : start;
}

/* How many items `query` lists, without any of them. */
static long list_total(const char *query)
{
	char path[600], *b = NULL;
	size_t len;
	jsv mc;
	long n = -1;

	snprintf(path, sizeof path, "%s%cX-Plex-Container-Start=0&X-Plex-Container-Size=0",
	         query, strchr(query, '?') ? '&' : '?');
	if (server_get(path, &b, &len, 30) == 200 && b && js_member(js_root(b, len), "MediaContainer", &mc))
		n = jnum(mc, "totalSize");
	free(b);
	return n;
}

/* ---- the records an index is written from ---- */

typedef struct { srec *r; int n, cap; long since; } sset;   /* since: newest updatedAt */

static void srec_free(srec *x)
{
	free(x->title); free(x->tartist); free(x->album_t);
	free(x->artist_t); free(x->thumb); free(x->part);
	memset(x, 0, sizeof *x);
}

static void sset_free(sset *s)
{
	for (int i = 0; i < s->n; i++) srec_free(&s->r[i]);
	free(s->r);
	memset(s, 0, sizeof *s);
}

/* Takes x's strings - but not on failure, when they are still the caller's. */
static bool sset_push(sset *s, srec x)
{
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 4096;
		srec *r = realloc(s->r, sizeof *r * (size_t)cap);

		if (!r) return false;
		s->r = r; s->cap = cap;
	}
	s->r[s->n++] = x;
	return true;
}

/* A track from a listing (a fetch_all `each`). */
static bool sset_track(jsv t, void *ctx)
{
	sset *s = ctx;
	char ti[512], ta[256], at[256], alt[256], th[256], pk[320];
	jsv media, mi = { 0 }, m0, parts, pi = { 0 }, p0;
	long u = jnum(t, "updatedAt");
	srec x;

	if (u > s->since) s->since = u;
	pk[0] = '\0';
	if (js_member(t, "Media", &media) && js_next(media, &mi, &m0) &&
	    js_member(m0, "Part", &parts) && js_next(parts, &pi, &p0))
		jstr(p0, "key", pk, sizeof pk);
	if (!pk[0]) return true;
	jstr(t, "title", ti, sizeof ti);
	jstr(t, "originalTitle", ta, sizeof ta);
	jstr(t, "grandparentTitle", at, sizeof at);
	jstr(t, "parentTitle", alt, sizeof alt);
	jstr(t, "parentThumb", th, sizeof th);
	x = (srec){
		.key = (int)jnum(t, "ratingKey"),
		.album = (int)jnum(t, "parentRatingKey"),
		.artist = (int)jnum(t, "grandparentRatingKey"),
		.year = (int)jnum(t, "parentYear"),
		.disc = (int)jnum(t, "parentIndex"),
		.idx = (int)jnum(t, "index"),
		.dur = (int)jnum(t, "duration"),
		.title = dupc(ti), .tartist = dupc(strcmp(ta, at) ? ta : ""),
		.album_t = dupc(alt[0] ? alt : "Unknown Album"),
		.artist_t = dupc(at[0] ? at : "Unknown Artist"),
		.thumb = dupc(th), .part = dupc(pk),
	};
	if (!x.key || !x.album || !x.artist) { srec_free(&x); return true; }
	if (!sset_push(s, x)) { srec_free(&x); return false; }
	return true;
}

/* Track i of an index read back as a record. */
static srec srec_of(const px_table *t, int i)
{
	const px_track *tr = &t->tr[i];
	const px_album *al = &t->al[tr->album];
	const px_artist *ar = &t->ar[al->artist];

	return (srec){
		.key = tr->key, .album = al->key, .artist = ar->key, .year = al->year,
		.disc = tr->disc, .idx = tr->idx, .dur = tr->dur_ms,
		.title = dupc(tr->title), .tartist = dupc(tr->artist), .album_t = dupc(al->title),
		.artist_t = dupc(ar->name), .thumb = dupc(al->thumb), .part = dupc(tr->part),
	};
}

static bool srec_same(const srec *a, const srec *b)
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
	return cmp_int(&((const srec *)a)->key, &((const srec *)b)->key);
}

/* ---- updating the index in place (plorpos-1lw.24) ---- */

/* Albums and artists Plex changed whose tracks are read again: those whose
 * title, year or cover (an album) or name (an artist) is no longer what the
 * index keeps - a track carries those, and its own updatedAt need not move
 * when they do. One not in the index is new: its tracks are new too, so
 * they came with the changed tracks already. */
typedef struct { const px_table *t; int *key; int n, cap; bool artists; } stale;

static bool stale_each(jsv o, void *ctx)
{
	stale *s = ctx;
	char title[256], thumb[256];
	int key = (int)jnum(o, "ratingKey"), i;
	bool differs = false;

	jstr(o, "title", title, sizeof title);
	clean(title);
	if (s->artists) {
		if (!title[0]) snprintf(title, sizeof title, "Unknown Artist");
		for (i = 0; i < s->t->nar && s->t->ar[i].key != key; i++) {}
		if (i == s->t->nar) return true;
		differs = strcmp(s->t->ar[i].name, title) != 0;
	} else {
		if (!title[0]) snprintf(title, sizeof title, "Unknown Album");
		jstr(o, "thumb", thumb, sizeof thumb);
		clean(thumb);
		for (i = 0; i < s->t->nal && s->t->al[i].key != key; i++) {}
		if (i == s->t->nal) return true;
		differs = strcmp(s->t->al[i].title, title) || strcmp(s->t->al[i].thumb, thumb) ||
		          s->t->al[i].year != (int)jnum(o, "year");
	}
	if (!differs) return true;
	if (s->n == s->cap) {
		int cap = s->cap ? s->cap * 2 : 64, *k = realloc(s->key, sizeof *k * (size_t)cap);

		if (!k) return false;
		s->key = k; s->cap = cap;
	}
	s->key[s->n++] = key;
	return true;
}

/* What changed since `since` (the newest updatedAt the index has), merged
 * into the index on the card, into `out`. A deleted track leaves no trace in
 * any listing, so the merge must come out at the server's count less the
 * tracks a sync skips (no file), as last time: false when it does not, or on
 * any failure - then the caller reads the library whole. `*changed`: how many
 * tracks really differ (Plex touches tracks without changing them). */
static bool sync_incremental(long since, long was_total, sset *out, long *total, int *changed)
{
	px_table t;
	sset ch = { .since = since };
	stale st = { 0 };
	char q[300];
	int *keys = NULL, i, nal;
	bool ok = false;

	*changed = 0;
	if (!tab_read(&t)) return false;
	st.t = &t;
	fprintf(stderr, "plex: updating %s since %ld\n", C.library, since);
	snprintf(q, sizeof q, "/library/sections/%d/all?type=10&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, sset_track, &ch, true) < 0) goto done;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=9&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, stale_each, &st, false) < 0) goto done;
	nal = st.n;
	st.artists = true;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=8&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, stale_each, &st, false) < 0) goto done;
	for (i = 0; i < st.n; i++) {
		snprintf(q, sizeof q, i < nal ? "/library/metadata/%d/children" : "/library/metadata/%d/allLeaves",
		         st.key[i]);
		if (fetch_all(q, sset_track, &ch, false) < 0) goto done;
	}

	/* The index less what came again, then what came, once each. */
	qsort(ch.r, (size_t)ch.n, sizeof *ch.r, cmp_srec_key);
	if (ch.n && !(keys = malloc(sizeof *keys * (size_t)ch.n))) goto done;
	for (i = 0; i < ch.n; i++) keys[i] = ch.r[i].key;
	out->since = ch.since;
	for (i = 0; i < t.ntr; i++) {
		srec x;

		if (ch.n && bsearch(&t.tr[i].key, keys, (size_t)ch.n, sizeof *keys, cmp_int)) continue;
		x = srec_of(&t, i);
		if (!sset_push(out, x)) { srec_free(&x); goto done; }
	}
	for (i = 0; i < ch.n; i++) {
		int o;

		if (i && keys[i] == keys[i - 1]) continue;              /* freed with ch */
		if ((o = tab_find(&t, ch.r[i].key)) < 0) (*changed)++;
		else {
			srec was = srec_of(&t, o);

			if (!srec_same(&was, &ch.r[i])) (*changed)++;
			srec_free(&was);
		}
		if (!sset_push(out, ch.r[i])) goto done;
		memset(&ch.r[i], 0, sizeof ch.r[i]);                    /* out's now */
	}
	snprintf(q, sizeof q, "/library/sections/%d/all?type=10", C.section);
	if ((*total = list_total(q)) < 0) goto done;
	if (*total - out->n != was_total - t.ntr) {
		fprintf(stderr, "plex: %ld tracks on the server, %d here (%ld then, %d): reading it all\n",
		        *total, out->n, was_total, t.ntr);
		goto done;
	}
	ok = true;
done:
	free(keys);
	free(st.key);
	sset_free(&ch);                  /* the duplicates left behind, and on failure the rest */
	tab_free(&t);
	return ok;
}

static void *sync_main(void *arg)
{
	sset s = { 0 };
	long stamp, since, was_total, total = 0;
	char q[300], p[700], tmp[720];
	unsigned t0 = now_ms();
	int i, changed = -1;
	bool inc = false;
	FILE *f;

	(void)arg;
	g_sync_done = 0; g_sync_total = 0;
	stamp = library_stamp();
	if (stamp < 0) { g_sync_failed = 1; goto out; }
	if (!g_sync_force && stamp == index_head(&since, &was_total)) { g_sync_last = 1; goto out; }
	if (!g_sync_force && since > 0 && was_total >= 0) {
		inc = sync_incremental(since, was_total, &s, &total, &changed);
		if (!inc) { sset_free(&s); changed = -1; }
	}
	if (!inc) {
		fprintf(stderr, "plex: syncing %s (%s)\n", C.library, g_sync_force ? "asked" : "changed");
		g_sync_done = g_sync_total = 0;
		snprintf(q, sizeof q, "/library/sections/%d/all?type=10", C.section);
		if ((total = fetch_all(q, sset_track, &s, true)) < 0) { g_sync_failed = 1; goto out; }
	}
	qsort(s.r, (size_t)s.n, sizeof *s.r, cmp_srec);

	index_path(p, sizeof p);
	{
		char d[700], *sl;
		snprintf(d, sizeof d, "%s", p);
		if ((sl = strrchr(d, '/'))) { *sl = '\0'; mkdirs(d); }
	}
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) { g_sync_failed = 1; goto out; }
	fprintf(f, "P\t1\t%ld\t%d\t%ld\t%ld\n", stamp, s.n, s.since, total);
	for (i = 0; i < s.n; i++) {
		const srec *r = s.r;

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
	if (inc) fprintf(stderr, "plex: %d tracks, %d changed, updated in %u ms\n", s.n, changed, now_ms() - t0);
	else fprintf(stderr, "plex: %d tracks synced in %u ms\n", s.n, now_ms() - t0);
	if (changed) g_sync_new = 1;            /* -1 (a full sync) included */
	g_sync_last = changed ? 2 : 1;
out:
	/* The playlists, by the index as it now is - library changed or not
	 * (plorpos-5eq.7). */
	if (!g_sync_failed) plex_playlists_sync();
	if (g_sync_failed) { g_sync_last = -1; fprintf(stderr, "plex: sync failed after %d tracks\n", g_sync_done); }
	sset_free(&s);
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
/* Downloaded playlists (plorpos-5eq.7): whether their Local copies' lists
 * want writing once the downloads drain. */
static bool g_pl_m3u;
static void pl_local_write_all(void);
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
		if (!g_offline) net_fetch(url, "GET", NULL, 0, tmp, 5, 30, cancel);
		unlink(tmp);
	}
	stream_url(base, t, kbps, true, url, sizeof url, "start");
	code = g_offline ? 0 : net_fetch(url, "GET", NULL, 0, part, 5, 0, cancel);
	if (code != 200 || !exists(part) || rename(part, dest) != 0) {
		unlink(part);
		if (code == 200) code = -1;
	}
	if (kbps) {
		stream_url(base, t, kbps, true, url, sizeof url, "stop");
		tmpfile_for("stop", tmp, sizeof tmp);
		if (!g_offline) net_fetch(url, "GET", NULL, 0, tmp, 5, 10, NULL);
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
	int kbps = kbps_download(), code;

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
			if (g_offline || net_fetch(url, "GET", h, 1, part, 5, 30, &g_dl_cancel) != 200 || rename(part, cov) != 0)
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
	g_dl_fail_note = 1;
	if (code == 0) base_failed();
	return false;
}

/* ---- fetched first: a device whose FFmpeg cannot open the stream ---------------- */

static pthread_t g_cache_th;
static bool g_cache_up;
static pthread_cond_t g_cache_cv = PTHREAD_COND_INITIALIZER;
static int g_cache_q[CACHE_Q], g_cache_n; /* keys, the one wanted now first */
/* What plex_prefetch last asked for: the playing track's key and those ahead.
 * The trim never deletes them; a change takes back the wishes no longer
 * wanted. Under g_mu. */
static int g_keep[AHEAD_MAX + 1], g_nkeep;

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
	if (kbps_stream()) snprintf(out, n, "%s/cache/%d.opus", g_dir, t->key);
	return false;
}

/* The cache within CACHE_MB, the oldest out first - never a fetch half done,
 * the track playing or one wanted ahead. */
static void cache_trim(void)
{
	char d[700], p[1100];
	DIR *dd;
	struct dirent *e;
	struct { char name[64]; time_t m; off_t sz; } f[96];
	int n = 0, i, j, keep[AHEAD_MAX + 1], nkeep;
	long long total = 0;

	LOCK();
	nkeep = g_nkeep;
	memcpy(keep, g_keep, sizeof keep[0] * (size_t)nkeep);
	UNLOCK();
	snprintf(d, sizeof d, "%s/cache", g_dir);
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
	(void)arg;
	{
		char c[700];
		int n;

		snprintf(c, sizeof c, "%s/cache", g_dir);
		if ((n = sweep_parts(c, 0))) fprintf(stderr, "plex: removed %d unfinished pre-fetch files\n", n);
	}
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
			code = fetch_track(base, &t, kbps_stream(), dest, NULL);
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
	if (g_cache_n == CACHE_Q) g_cache_n = CACHE_Q - 1;   /* the newest wish goes */
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
	{
		char m[700];
		int n;

		snprintf(m, sizeof m, "%s/Music/" ML_PLEX, g_card);
		if ((n = sweep_parts(m, 2))) fprintf(stderr, "plex: removed %d unfinished downloads\n", n);
	}
	for (;;) {
		dl_item it;

		LOCK();
		while (g_dl_pos >= g_dlq_n) {
			g_dlq_n = g_dl_pos = g_dl_total = 0;
			g_dl_now[0] = '\0';
			if (g_pl_m3u) {         /* a playlist's downloads are in */
				g_pl_m3u = false;
				UNLOCK();
				pl_local_write_all();
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
	g_sync_last = 0;
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

int plex_sync_outcome(void) { return g_sync_last; }

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

/* Not inlined: each caller's buffer would get its own truncation warning. */
__attribute__((noinline))
void plex_pl_dir(char *out, size_t n) { snprintf(out, n, "%s/%s/Playlists", g_card, g_rel_root); }

bool plex_cover_want(int al, const char *base)
{
	char thumb[600];

	LOCK();
	if (al < 0 || al >= g_tab.nal || !g_tab.al[al].thumb[0] || !C.token[0]) { UNLOCK(); return false; }
	snprintf(thumb, sizeof thumb, "%s", g_tab.al[al].thumb);
	UNLOCK();
	if (!g_cq && !(g_cq = coverq_new(cover_fetch))) return false;
	coverq_want(g_cq, thumb, base);
	return true;
}

void plex_cover_bump(const char *base) { coverq_bump(g_cq, base); }

bool plex_covers_idle(void) { return coverq_idle(g_cq); }

bool plex_cover_take(char *base, size_t n) { return coverq_take(g_cq, base, n); }

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
	if (g_offline) {                             /* a pre-fetched copy plays too */
		if (!cache_file(&t, out->url, sizeof out->url)) out->offline = true;
		UNLOCK();
		return true;
	}
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
	stream_url(base, &t, kbps_stream(), false, out->url, sizeof out->url, "start");
	if (kbps_stream()) snprintf(out->codec, sizeof out->codec, "OPUS %d", kbps_stream());
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
	if (g_offline) goto mark;                     /* no timelines; the play counted below */
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
	if (!counted && len > 0 && at >= len * PLAYED_AT && (g_offline || !net_online())) {
		/* Once a play: Muse's connection lost for a moment reads as a
		 * track change and clears `counted`, so the same track again
		 * within its own length is that, not a second play. */
		static int added;
		static unsigned added_ms;

		counted = true;
		if (key != added || now - added_ms > (unsigned)(len * 1000)) {
			plays_add(key);                       /* sent when the server answers */
			added = key;
			added_ms = now;
		}
		return;
	}
	if (!counted && len > 0 && at >= len * PLAYED_AT && (j = job_new(J_TIMELINE))) {
		counted = true;
		last_ms = now;
		j->n1 = key; j->n2 = (long)(at * 1000); j->n3 = (long)(len * 1000);
		snprintf(j->a, sizeof j->a, "%s", NAMES[state]);
		push(j, false);
	}
}

/* The size a fetch of t will be, about: Opus at its rate, the original
 * at a CD's FLAC. */
static long long fetch_bytes(const px_track *t)
{
	return (long long)t->dur_ms * (kbps_stream() ? kbps_stream() : 1000) / 8;
}

void plex_prefetch(const char *playing, const char *const *next, int n)
{
	static int last[AHEAD_MAX + 1], nlast = -1;
	int keys[AHEAD_MAX + 1], nk = 0, i, j, t;
	long long sum = 0;
	char base[256], f[1100];

	keys[nk++] = plex_is_path(playing) ? key_of(playing) : 0;
	for (i = 0; i < n && i < AHEAD_MAX; i++)
		keys[nk++] = plex_is_path(next[i]) ? key_of(next[i]) : 0;
	if (nk == nlast && !memcmp(keys, last, sizeof keys[0] * (size_t)nk)) return;
	memcpy(last, keys, sizeof keys[0] * (size_t)nk);
	nlast = nk;
	LOCK();
	/* Wishes ahead that are no longer ahead go (the one in flight finishes;
	 * one wanted now, by the player, is the playing track and stays). */
	for (i = j = 1; i < g_cache_n; i++) {
		int k = g_cache_q[i], a, b;

		for (a = 0; a < g_nkeep && g_keep[a] != k; a++) {}
		for (b = 0; b < nk && keys[b] != k; b++) {}
		if (a < g_nkeep && b == nk) continue;
		g_cache_q[j++] = k;
	}
	if (g_cache_n > 1) g_cache_n = j;
	memcpy(g_keep, keys, sizeof keys[0] * (size_t)nk);
	g_nkeep = nk;
	snprintf(base, sizeof base, "%s", g_base);
	/* In order, as long as the guess at their size fits the budget. */
	if (base[0] && need_fetch(base))
		for (i = 1; i < nk; i++) {
			if (!keys[i] || (t = tab_find(&g_tab, keys[i])) < 0) continue;
			if (local_copy(&g_tab.tr[t], f, sizeof f)) continue;
			sum += fetch_bytes(&g_tab.tr[t]);
			if (i > 1 && sum > (long long)CACHE_MB << 20) break;
			if (!cache_file(&g_tab.tr[t], f, sizeof f)) cache_want(keys[i], false);
		}
	if (!base[0]) nlast = -1;            /* no connection yet: again next poll */
	UNLOCK();
}

int plex_ahead(void) { return C.ahead >= 1 && C.ahead <= AHEAD_MAX ? C.ahead : 1; }
void plex_set_ahead(int n) { C.ahead = n; cfg_save(); }
bool plex_fetches_first(void) { return !STREAM_HTTP || !STREAM_TLS; }

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

/* Offline Mode's lists (plorpos-5tm.16): which albums have any track on the
 * card, from the manifest - each copy it names looked at once, so the cost
 * is the downloads, not the library. out: n flags, one per album. */
void plex_albums_local(unsigned char *out, int n)
{
	char p[700], line[200], copy[1024];
	FILE *in;

	memset(out, 0, (size_t)n);
	manifest_path(p, sizeof p);
	pthread_mutex_lock(&g_man_mu);           /* rewritten by rename: this one stays whole */
	in = fopen(p, "r");
	pthread_mutex_unlock(&g_man_mu);
	if (!in) return;
	LOCK();
	while (fgets(line, sizeof line, in)) {
		int i = tab_find(&g_tab, atoi(line)), al = i >= 0 ? g_tab.tr[i].album : -1;

		if (al >= 0 && al < n && !out[al] && local_copy(&g_tab.tr[i], copy, sizeof copy)) out[al] = 1;
	}
	UNLOCK();
	fclose(in);
}

bool plex_track_local(int t)
{
	char p[1024];
	bool yes;

	LOCK();
	yes = t >= 0 && t < g_tab.ntr && local_copy(&g_tab.tr[t], p, sizeof p);
	UNLOCK();
	return yes;
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

/* Remove a copy and the folders it leaves empty, up to Music/Plex, with the
 * covers Local Music cached for them (plorpos-5tm.14). */
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
	if ((s = strrchr(d, '/')) && (*s = '\0', strcmp(d, stop) && strlen(d) > strlen(stop)) &&
	    rmdir(d) == 0)
		ml_prune_up(d, stop);
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

int plex_download_queue(plex_queued *out, int max)
{
	int i, j, n = 0;

	LOCK();
	for (i = g_dl_pos; i < g_dlq_n; i++) {
		int t = tab_find(&g_tab, g_dlq[i].key), al;

		if (t < 0 || (i == g_dl_pos && g_dl_cancel)) continue;   /* stopping */
		al = g_tab.tr[t].album;
		for (j = 0; j < n && out[j].al != al; j++) {}
		if (j < n) { out[j].tracks++; continue; }
		if (n == max) continue;
		out[n] = (plex_queued){ .al = al, .tracks = 1, .now = i == g_dl_pos };
		snprintf(out[n].title, sizeof out[n].title, "%s", g_tab.al[al].title);
		snprintf(out[n].artist, sizeof out[n].artist, "%s", g_tab.ar[g_tab.al[al].artist].name);
		n++;
	}
	UNLOCK();
	return n;
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

/* ---- playlists (plorpos-5eq.7) ---------------------------------------------------- *
 *
 * The Plex source's playlists are .m3u files of Plex paths in plex_pl_dir, as
 * they always were; this keeps them the same as the server's. Pulled after
 * every library sync: each audio playlist on the server is written over its
 * file (the server wins - an edit made here was sent when it was made), one
 * the server no longer has goes, and one made here that the server has not
 * got is created there. A smart playlist is read only, and one longer than
 * PLS_TRACKS_MAX (Plex's own "All Music" is the whole library) is left out.
 * Favorite Tracks stays this device's own list. What is known of each is in
 * .plex.tsv beside them: the server's id, smart or not, downloaded or not.
 *
 * A downloaded playlist's tracks are copied to Music/Plex as an album's are
 * (manifest reason 'p'; 'f', the old favorites, counts as 'p'), and listed
 * in <card>/Music/Playlists/Plex <name>.m3u for the Local source. With Keep
 * Playlists Synced on, every pull brings the copies in line as well. */
#define PLS_MAX        64
#define PLS_TRACKS_MAX 2000
#define PL_LOCAL_PRE   "Plex "
typedef struct { char name[96]; int id; bool smart, dl; } px_pl;
static px_pl g_pls[PLS_MAX];              /* under g_mu */
static int   g_npls;
static bool  g_pls_in;
static volatile int g_pl_mirror = 1;
static volatile int g_pl_gen;
enum { PLE_ADD, PLE_DEL, PLE_MOVE, PLE_RENAME, PLE_DELETE };

int plex_pl_gen(void) { return g_pl_gen; }
void plex_set_pl_mirror(bool on) { g_pl_mirror = on; }

void plex_set_offline(bool on)
{
	if (on == g_offline) return;
	g_offline = on;
	fprintf(stderr, "plex: Offline Mode %s\n", on ? "on" : "off");
	if (on) plex_download_cancel();
	else g_plays_last = 0;                        /* plays made offline go at once */
}

bool plex_offline(void) { return g_offline; }

static void pls_file(char *out, size_t n)
{
	char d[700];

	plex_pl_dir(d, sizeof d);
	snprintf(out, n, "%s/.plex.tsv", d);
}

static void pls_load_locked(void)
{
	char p[800], line[200];
	FILE *f;

	if (g_pls_in) return;
	g_pls_in = true;
	g_npls = 0;
	pls_file(p, sizeof p);
	if (!(f = fopen(p, "r"))) return;
	while (g_npls < PLS_MAX && fgets(line, sizeof line, f)) {
		px_pl *e = &g_pls[g_npls];
		int id, smart, dl, at = 0;

		line[strcspn(line, "\r\n")] = '\0';
		if (sscanf(line, "%d\t%d\t%d\t%n", &id, &smart, &dl, &at) < 3 || !at || !line[at]) continue;
		*e = (px_pl){ .id = id, .smart = smart != 0, .dl = dl != 0 };
		snprintf(e->name, sizeof e->name, "%s", line + at);
		g_npls++;
	}
	fclose(f);
}

static void pls_save_locked(void)
{
	char p[800], tmp[820], d[700];
	FILE *f;
	int i;

	plex_pl_dir(d, sizeof d);
	mkdirs(d);
	pls_file(p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) return;
	for (i = 0; i < g_npls; i++)
		fprintf(f, "%d\t%d\t%d\t%s\n", g_pls[i].id, g_pls[i].smart, g_pls[i].dl, g_pls[i].name);
	if (fclose(f) == 0) rename(tmp, p);
	else unlink(tmp);
}

static px_pl *pls_find_locked(const char *name)
{
	int i;

	pls_load_locked();
	for (i = 0; i < g_npls; i++) if (!strcmp(g_pls[i].name, name)) return &g_pls[i];
	return NULL;
}

static px_pl *pls_get_locked(const char *name)
{
	px_pl *e = pls_find_locked(name);

	if (e || g_npls == PLS_MAX) return e;
	e = &g_pls[g_npls++];
	*e = (px_pl){ 0 };
	snprintf(e->name, sizeof e->name, "%s", name);
	return e;
}

static void pls_drop_locked(px_pl *e)
{
	*e = g_pls[--g_npls];
}

bool plex_pl_smart(const char *name)
{
	px_pl *e;
	bool r;

	LOCK(); e = pls_find_locked(name); r = e && e->smart; UNLOCK();
	return r;
}

bool plex_pl_downloaded(const char *name)
{
	px_pl *e;
	bool r;

	LOCK(); e = pls_find_locked(name); r = e && e->dl; UNLOCK();
	return r;
}

/* The track keys of playlist `name`, in order, from its file: a Plex path's
 * last part is "<key>.<ext>". The caller frees *out. */
static int pl_file_keys(const char *name, int **out)
{
	char d[700], **v = NULL;
	size_t rl = strlen(g_rel_root);
	int n, i, m = 0, *k;

	*out = NULL;
	plex_pl_dir(d, sizeof d);
	if ((n = pl_read(d, name, g_card, &v)) <= 0) { if (n == 0) pl_free(v, n); return 0; }
	if ((k = malloc(sizeof *k * (size_t)n))) {
		for (i = 0; i < n; i++) {
			const char *sl = strrchr(v[i], '/');

			if (strncmp(v[i], g_rel_root, rl) || !sl || atoi(sl + 1) <= 0) continue;
			k[m++] = atoi(sl + 1);
		}
	}
	pl_free(v, n);
	*out = k;
	return k ? m : 0;
}

/* Playlist `name`'s file, written whole from track keys; a key the index
 * does not have (another library's, or added since the last sync) is left
 * out. Card-relative as pl_add writes a path outside Music: "../../". */
static void pl_file_write(const char *name, const int *keys, int n)
{
	char d[700], p[900], tmp[920];
	FILE *f;
	int i, t;

	plex_pl_dir(d, sizeof d);
	mkdirs(d);
	snprintf(p, sizeof p, "%s/%s.m3u", d, name);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	if (!(f = fopen(tmp, "w"))) return;
	fputs("#EXTM3U\n", f);
	LOCK();
	for (i = 0; i < n; i++) {
		const px_track *tr;

		if ((t = tab_find(&g_tab, keys[i])) < 0) continue;
		tr = &g_tab.tr[t];
		fprintf(f, "../../%s/%d/%d/%d.%s\n", g_rel_root, g_tab.ar[g_tab.al[tr->album].artist].key,
		        g_tab.al[tr->album].key, tr->key, ext_of(tr->part));
	}
	UNLOCK();
	if (fclose(f) == 0) rename(tmp, p);
	else unlink(tmp);
}

static void pl_local_path(const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/" PL_DIR_REL "/" PL_LOCAL_PRE "%.95s.m3u", g_card, name);
}

/* The Local source's list of a downloaded playlist: the copies there are. */
static void pl_local_write(const char *name)
{
	char dir[700], p[900], tmp[920], f[1100], pre[700];
	size_t pl;
	FILE *o;
	int *keys, n = pl_file_keys(name, &keys), i, t;

	snprintf(dir, sizeof dir, "%s/" PL_DIR_REL, g_card);
	mkdirs(dir);
	pl_local_path(name, p, sizeof p);
	snprintf(tmp, sizeof tmp, "%s.new", p);
	snprintf(pre, sizeof pre, "%s/Music/", g_card);
	pl = strlen(pre);
	if (!(o = fopen(tmp, "w"))) { free(keys); return; }
	fputs("#EXTM3U\n", o);
	LOCK();
	for (i = 0; i < n; i++)
		if ((t = tab_find(&g_tab, keys[i])) >= 0 && local_copy(&g_tab.tr[t], f, sizeof f) &&
		    !strncmp(f, pre, pl))
			fprintf(o, "../%s\n", f + pl);
	UNLOCK();
	free(keys);
	if (fclose(o) == 0) rename(tmp, p);
	else unlink(tmp);
}

static void pl_local_write_all(void)
{
	char names[PLS_MAX][96];
	int n = 0, i;

	LOCK();
	pls_load_locked();
	for (i = 0; i < g_npls; i++) if (g_pls[i].dl) memcpy(names[n++], g_pls[i].name, sizeof names[0]);
	UNLOCK();
	for (i = 0; i < n; i++) pl_local_write(names[i]);
	g_dl_changed = 1;
	g_pl_gen++;
}

static int cmp_keys(const void *x, const void *y) { return *(const int *)x - *(const int *)y; }

/* The job: the copies a downloaded playlist wants, queued; the copies no
 * downloaded playlist wants any more gone, unless an album download wants
 * them too; the Local lists written now and again when the downloads are in. */
static void do_pl_downloads(void)
{
	char names[PLS_MAX][96], mp[700], line[200], p[1024];
	int *want = NULL, nw = 0, cap = 0, n = 0, i, j, t;
	FILE *in;

	LOCK();
	pls_load_locked();
	for (i = 0; i < g_npls; i++) if (g_pls[i].dl) memcpy(names[n++], g_pls[i].name, sizeof names[0]);
	UNLOCK();
	for (i = 0; i < n; i++) {
		int *k, m = pl_file_keys(names[i], &k);

		if (nw + m > cap) {
			int *w = realloc(want, sizeof *w * (size_t)(cap = nw + m + 64));
			if (!w) { free(k); break; }
			want = w;
		}
		for (j = 0; j < m; j++) want[nw++] = k[j];
		free(k);
	}
	if (nw) qsort(want, (size_t)nw, sizeof *want, cmp_keys);
	LOCK();
	for (i = 0; i < nw; i++)
		if (i == 0 || want[i] != want[i - 1]) dl_queue(want[i], 'p');
	UNLOCK();
	fprintf(stderr, "plex: playlists: %d downloaded, %d tracks wanted\n", n, nw);
	manifest_path(mp, sizeof mp);
	pthread_mutex_lock(&g_man_mu);
	in = fopen(mp, "r");
	pthread_mutex_unlock(&g_man_mu);
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
			manifest_set(gone[i], why[i], false);
			if (manifest_has(gone[i], 'a')) continue;
			LOCK();
			if ((t = tab_find(&g_tab, gone[i])) >= 0 && local_copy(&g_tab.tr[t], p, sizeof p))
				remove_copy(p);
			UNLOCK();
		}
		free(gone);
		free(why);
	}
	free(want);
	/* The list the old Download Favorite Tracks wrote, on both sources. */
	snprintf(p, sizeof p, "%s/" PL_DIR_REL "/Plex Favorites.m3u", g_card);
	unlink(p);
	plex_pl_dir(mp, sizeof mp);
	snprintf(p, sizeof p, "%s/Plex Favorites.m3u", mp);
	unlink(p);
	pl_local_write_all();
	LOCK();
	g_pl_m3u = true;
	UNLOCK();
	dl_start();
}

void plex_pl_download(const char *name, bool on)
{
	px_pl *e;
	job *j;

	LOCK();
	if ((e = pls_get_locked(name))) { e->dl = on; pls_save_locked(); }
	UNLOCK();
	if (!on) {
		char p[900];

		pl_local_path(name, p, sizeof p);
		unlink(p);
	}
	g_pl_gen++;
	if ((j = job_new(J_PLDL))) push(j, false);
}

/* "server://<machine>/com.plexapp.plugins.library/library/metadata/<k,k,...>",
 * urlencoded, for a playlist's uri= parameter. */
static void pl_uri(const int *keys, int n, char *out, size_t cap)
{
	char raw[6000];
	size_t o;
	int i;

	o = (size_t)snprintf(raw, sizeof raw, "server://%s/com.plexapp.plugins.library/library/metadata/", C.server_id);
	for (i = 0; i < n && o + 16 < sizeof raw; i++)
		o += (size_t)snprintf(raw + o, sizeof raw - o, "%s%d", i ? "," : "", keys[i]);
	urlenc(raw, out, cap);
}

static int server_send(const char *method, const char *path, char **body, size_t *len)
{
	char base[256], url[20000];
	int code;

	if (!pick_base(base, sizeof base)) return 0;
	snprintf(url, sizeof url, "%s%s", base, path);
	code = plex_req(method, url, true, body, len, 20);
	if (code == 0) base_failed();
	return code;
}

/* Playlist `name` made on the server from its file here; its id, or 0. Sent
 * in parts: a uri of a few hundred keys is as long as a request line gets. */
static int pl_create(const char *name)
{
	char title[400], uri[18000], path[19000], *b = NULL, v[32] = "";
	size_t len;
	jsv root, mc, md, it = { 0 }, d;
	int *keys, n = pl_file_keys(name, &keys), id = 0, at;

	if (n <= 0) { free(keys); return 0; }           /* Plex makes none empty */
	urlenc(name, title, sizeof title);
	pl_uri(keys, n < 300 ? n : 300, uri, sizeof uri);
	snprintf(path, sizeof path, "/playlists?type=audio&smart=0&title=%s&uri=%s", title, uri);
	if (server_send("POST", path, &b, &len) == 200 && b) {
		root = js_root(b, len);
		if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Metadata", &md) &&
		    js_next(md, &it, &d)) {
			jstr(d, "ratingKey", v, sizeof v);
			id = atoi(v);
		}
	}
	free(b);
	for (at = 300; id && at < n; at += 300) {
		pl_uri(keys + at, n - at < 300 ? n - at : 300, uri, sizeof uri);
		snprintf(path, sizeof path, "/playlists/%d/items?uri=%s", id, uri);
		if (server_send("PUT", path, NULL, NULL) != 200) break;
	}
	free(keys);
	if (id) fprintf(stderr, "plex: playlist %s made on the server (%d tracks)\n", name, n);
	return id;
}

/* The server's item ids of playlist `id`, with each one's track key. */
static int pl_items(int id, int **keys, int **items)
{
	char path[160], *b = NULL;
	size_t len;
	jsv root, mc, md, it = { 0 }, d;
	int n = 0, cap = 0, *k = NULL, *im = NULL;

	*keys = *items = NULL;
	snprintf(path, sizeof path, "/playlists/%d/items?X-Plex-Container-Start=0&X-Plex-Container-Size=%d",
	         id, PLS_TRACKS_MAX);
	if (server_get(path, &b, &len, 30) != 200 || !b) { free(b); return -1; }
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Metadata", &md))
		while (js_next(md, &it, &d)) {
			if (n == cap) {
				int *k2 = realloc(k, sizeof *k2 * (size_t)(cap = cap ? cap * 2 : 128));
				int *i2 = k2 ? realloc(im, sizeof *i2 * (size_t)cap) : NULL;
				if (k2) k = k2;
				if (i2) im = i2;
				if (!k2 || !i2) break;
			}
			k[n] = (int)jnum(d, "ratingKey");
			im[n++] = (int)jnum(d, "playlistItemID");
		}
	free(b);
	*keys = k;
	*items = im;
	return n;
}

/* The pull, and the push of what was made here. */
static void do_pls(void)
{
	typedef struct { int id, leaf; bool smart; char name[96]; } spl;
	char *b = NULL, d[700], p[900];
	size_t len;
	jsv root, mc, md, it = { 0 }, e;
	spl *sv;
	pl_info *have;
	int ns = 0, nh, i, j;

	if (!g_tab.ntr) return;                     /* no index yet: its sync comes first */
	if (server_get("/playlists?playlistType=audio", &b, &len, 30) != 200 || !b) {
		free(b);
		fprintf(stderr, "plex: playlists: the server did not answer\n");
		return;
	}
	if (!(sv = calloc(PLS_MAX, sizeof *sv)) || !(have = calloc(PLS_MAX, sizeof *have))) { free(sv); free(b); return; }
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Metadata", &md))
		while (ns < PLS_MAX && js_next(md, &it, &e)) {
			char title[200] = "";
			jsv v;

			jstr(e, "title", title, sizeof title);
			sv[ns].id = (int)jnum(e, "ratingKey");
			sv[ns].leaf = (int)jnum(e, "leafCount");
			sv[ns].smart = js_member(e, "smart", &v) && js_is_true(v);
			safe_name(title, sv[ns].name, 90);
			if (!sv[ns].id || !sv[ns].name[0] || !strcmp(sv[ns].name, PLEX_FAVS)) continue;
			if (sv[ns].leaf > PLS_TRACKS_MAX) {
				fprintf(stderr, "plex: playlist %s left out (%d tracks)\n", sv[ns].name, sv[ns].leaf);
				continue;
			}
			ns++;
		}
	free(b);
	plex_pl_dir(d, sizeof d);
	/* The server's, each over its file. */
	for (i = 0; i < ns; i++) {
		int *keys, *items, n = pl_items(sv[i].id, &keys, &items);
		px_pl *pe;
		char was[96] = "";

		if (n < 0) { free(keys); free(items); continue; }
		LOCK();
		pls_load_locked();
		for (j = 0; j < g_npls; j++)
			if (g_pls[j].id == sv[i].id && strcmp(g_pls[j].name, sv[i].name)) {
				memcpy(was, g_pls[j].name, sizeof was);               /* renamed there */
				memcpy(g_pls[j].name, sv[i].name, sizeof g_pls[j].name);
			}
		if ((pe = pls_get_locked(sv[i].name))) { pe->id = sv[i].id; pe->smart = sv[i].smart; }
		UNLOCK();
		if (was[0]) {
			snprintf(p, sizeof p, "%s/%s.m3u", d, was);
			unlink(p);
			pl_local_path(was, p, sizeof p);
			unlink(p);
		}
		pl_file_write(sv[i].name, keys, n);
		free(keys);
		free(items);
	}
	/* Gone from the server: gone here. */
	LOCK();
	pls_load_locked();
	for (j = 0; j < g_npls; ) {
		if (g_pls[j].id > 0) {
			for (i = 0; i < ns && sv[i].id != g_pls[j].id; i++) {}
			if (i == ns) {
				char gone[96];

				memcpy(gone, g_pls[j].name, sizeof gone);
				pls_drop_locked(&g_pls[j]);
				UNLOCK();
				snprintf(p, sizeof p, "%s/%s.m3u", d, gone);
				unlink(p);
				pl_local_path(gone, p, sizeof p);
				unlink(p);
				fprintf(stderr, "plex: playlist %s is gone from the server\n", gone);
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
		px_pl *pe;
		int id;
		bool known;

		if (!strcmp(have[i].name, PLEX_FAVS)) continue;
		LOCK(); pe = pls_find_locked(have[i].name); known = pe && pe->id > 0; UNLOCK();
		if (known || !(id = pl_create(have[i].name))) continue;
		LOCK();
		if ((pe = pls_get_locked(have[i].name))) pe->id = id;
		UNLOCK();
	}
	LOCK();
	pls_save_locked();
	UNLOCK();
	free(sv);
	free(have);
	g_pl_gen++;
	fprintf(stderr, "plex: playlists: %d on the server\n", ns);
	if (g_pl_mirror) do_pl_downloads();
}

void plex_playlists_sync(void)
{
	job *j = job_new(J_PLS);

	if (j) push(j, false);
}

/* An edit made here, sent: by track key, not by place - the server's list
 * may hold tracks this library's index has not. */
static void do_pl_edit(job *j)
{
	char path[19000], uri[400], title[400];
	int id = (int)j->n2, key = (int)j->n3, code = 200;

	switch ((int)j->n1) {
	case PLE_ADD:
		if (!id) {
			px_pl *pe;

			if (!(id = pl_create(j->a))) { code = 0; break; }
			LOCK();
			if ((pe = pls_get_locked(j->a))) { pe->id = id; pls_save_locked(); }
			UNLOCK();
			break;
		}
		pl_uri(&key, 1, uri, sizeof uri);
		snprintf(path, sizeof path, "/playlists/%d/items?uri=%s", id, uri);
		code = server_send("PUT", path, NULL, NULL);
		break;
	case PLE_DEL:
	case PLE_MOVE: {
		int *keys, *items, n, i, item = 0, after = 0, akey = atoi(j->b);

		if (!id) break;
		if ((n = pl_items(id, &keys, &items)) < 0) { code = 0; break; }
		for (i = 0; i < n; i++) {
			if (!item && keys[i] == key) item = items[i];
			if (!after && akey && keys[i] == akey) after = items[i];
		}
		free(keys);
		free(items);
		if (!item || (akey && !after)) break;       /* not there: the next pull settles it */
		if (j->n1 == PLE_DEL) snprintf(path, sizeof path, "/playlists/%d/items/%d", id, item);
		else if (after) snprintf(path, sizeof path, "/playlists/%d/items/%d/move?after=%d", id, item, after);
		else snprintf(path, sizeof path, "/playlists/%d/items/%d/move", id, item);
		code = server_send(j->n1 == PLE_DEL ? "DELETE" : "PUT", path, NULL, NULL);
		break;
	}
	case PLE_RENAME:
		if (!id) break;
		urlenc(j->b, title, sizeof title);
		snprintf(path, sizeof path, "/playlists/%d?title=%s", id, title);
		code = server_send("PUT", path, NULL, NULL);
		break;
	case PLE_DELETE:
		if (!id) break;
		snprintf(path, sizeof path, "/playlists/%d", id);
		code = server_send("DELETE", path, NULL, NULL);
		if (code == 204) code = 200;
		break;
	}
	if (code != 200) {
		fprintf(stderr, "plex: a playlist change did not reach the server (http %d)\n", code);
		g_dl_fail_note = 1;
	}
}

static void pl_edit(int op, const char *name, const char *b, int key)
{
	px_pl *e;
	job *j = job_new(J_PLEDIT);

	if (!j) return;
	LOCK();
	e = pls_find_locked(name);
	j->n1 = op;
	j->n2 = e ? e->id : 0;
	j->n3 = key;
	snprintf(j->a, sizeof j->a, "%s", name);
	snprintf(j->b, sizeof j->b, "%s", b ? b : "");
	if (e && op == PLE_RENAME) { snprintf(e->name, sizeof e->name, "%.95s", b); pls_save_locked(); }
	if (e && op == PLE_DELETE) { pls_drop_locked(e); pls_save_locked(); }
	UNLOCK();
	if (op == PLE_RENAME || op == PLE_DELETE) {
		char p[900], q[900];

		pl_local_path(name, p, sizeof p);
		if (op == PLE_RENAME) { pl_local_path(b, q, sizeof q); rename(p, q); }
		else unlink(p);
	}
	push(j, false);
}

static int rel_key(const char *rel)
{
	const char *sl = rel ? strrchr(rel, '/') : NULL;

	return sl && plex_is_path(rel) ? atoi(sl + 1) : 0;
}

void plex_pl_added(const char *name, const char *track)
{
	if (strcmp(name, PLEX_FAVS) && rel_key(track)) pl_edit(PLE_ADD, name, NULL, rel_key(track));
}

void plex_pl_removed(const char *name, const char *track)
{
	if (strcmp(name, PLEX_FAVS) && rel_key(track)) pl_edit(PLE_DEL, name, NULL, rel_key(track));
}

void plex_pl_moved(const char *name, const char *track, const char *after)
{
	char ak[16];

	if (!strcmp(name, PLEX_FAVS) || !rel_key(track)) return;
	snprintf(ak, sizeof ak, "%d", rel_key(after));
	pl_edit(PLE_MOVE, name, ak, rel_key(track));
}

void plex_pl_renamed(const char *from, const char *to)
{
	if (strcmp(from, PLEX_FAVS)) pl_edit(PLE_RENAME, from, to, 0);
}

void plex_pl_deleted(const char *name)
{
	if (strcmp(name, PLEX_FAVS)) pl_edit(PLE_DELETE, name, NULL, 0);
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
