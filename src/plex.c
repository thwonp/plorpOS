/* SPDX-License-Identifier: MIT */
/* See plex.h. The Plex API as PlexMusicPak uses it (the owner's NextUI
 * client): plex.tv's PIN sign-in and resources, a server's sections, tracks
 * and photo transcoder, the universal audio transcoder and the timeline.
 * Everything over net_fetch, which runs the device's curl.
 *
 * Plex's half of the source (plorpos-vgr.9): what is shared with Jellyfin -
 * the index, downloads, the pre-fetch cache, covers, plays made offline and
 * the playlists' files - is msrv.c's; this file is the server's side of it. */
#include "plex.h"
#include "msrv.h"

#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "net.h"
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
#define PLS_TRACKS_MAX 2000

/* Plexamp's Opus profile, from the pak (a tshark capture of Plexamp). */
#define OPUS_PROFILE \
	"add-transcode-target(replace%3Dtrue%26type%3DmusicProfile%26context%3Dstreaming" \
	"%26protocol%3Dhttp%26container%3Dogg%26audioCodec%3Dopus)" \
	"%2Badd-limitation(scope%3DmusicCodec%26scopeName%3DOpus%26type%3DupperBound" \
	"%26name%3Daudio.channels%26value%3D2%26onlyTranscodes%3Dtrue%26replace%3Dtrue)"

static const int QUALITY[PLEX_QUALITIES] = { 0, 96, 128, 192, 256, 320 };

static msrv *P;                           /* the source */
#define LOCK()   msrv_lock(P)
#define UNLOCK() msrv_unlock(P)
#define now_ms   msrv_now_ms
#define slurp    msrv_slurp
#define urlenc   msrv_urlenc

/* ---- the config (P_SHARED/plex/config, 0600) ---------------------------------- */

typedef struct { char uri[256]; bool local, relay; } conn;
static struct {
	char client[48], token[128], account[128];
	char server[128], server_id[64];
	conn conns[MAX_CONNS]; int nconns;
	int  section; char library[128];
} C;
static char g_base[256];                  /* the connection that answered; under the lock */

static void cfg_reset(void) { memset(&C, 0, sizeof C); }

static void cfg_key(const char *k, const char *v)
{
	if (!strcmp(k, "client"))         snprintf(C.client, sizeof C.client, "%s", v);
	else if (!strcmp(k, "token"))     snprintf(C.token, sizeof C.token, "%s", v);
	else if (!strcmp(k, "account"))   snprintf(C.account, sizeof C.account, "%s", v);
	else if (!strcmp(k, "server"))    snprintf(C.server, sizeof C.server, "%s", v);
	else if (!strcmp(k, "server_id")) snprintf(C.server_id, sizeof C.server_id, "%s", v);
	else if (!strcmp(k, "section"))   C.section = atoi(v);
	else if (!strcmp(k, "library"))   snprintf(C.library, sizeof C.library, "%s", v);
	else if (!strcmp(k, "conn") && C.nconns < MAX_CONNS) {
		int lo, re, n = 0;
		if (sscanf(v, "%d %d %n", &lo, &re, &n) >= 2 && n > 0) {
			conn *c = &C.conns[C.nconns++];
			c->local = lo; c->relay = re;
			snprintf(c->uri, sizeof c->uri, "%s", v + n);
		}
	}
}

/* A client id of our own, made once. */
static void cfg_loaded(msrv *m)
{
	unsigned char r[16];
	int fd, i;

	if (C.client[0]) return;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, r, sizeof r) != (ssize_t)sizeof r)
		for (i = 0; i < 16; i++) r[i] = (unsigned char)(rand() ^ (int)now_ms());
	if (fd >= 0) close(fd);
	strcpy(C.client, "plorpos-");
	for (i = 0; i < 12; i++) sprintf(C.client + strlen(C.client), "%02x", r[i]);
	msrv_cfg_save(m);
}

static void cfg_write(FILE *f)
{
	int i;

	fprintf(f, "client=%s\ntoken=%s\naccount=%s\nserver=%s\nserver_id=%s\n",
	        C.client, C.token, C.account, C.server, C.server_id);
	for (i = 0; i < C.nconns; i++)
		fprintf(f, "conn=%d %d %s\n", C.conns[i].local, C.conns[i].relay, C.conns[i].uri);
	fprintf(f, "section=%d\nlibrary=%s\n", C.section, C.library);
}

#define cfg_save() msrv_cfg_save(P)

static bool signed_in(void) { return C.token[0] != '\0'; }
static bool ready(void) { return C.token[0] && C.server_id[0] && C.section > 0; }
static const char *library(void) { return C.library; }

static bool rel_id(char *out, size_t n)
{
	if (!C.server_id[0] || C.section <= 0) return false;
	snprintf(out, n, "%.24s-%d", C.server_id, C.section);
	return true;
}

/* ---- small helpers ------------------------------------------------------------- */

static bool exists(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
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

/* GET/POST with Plex's headers, the reply into memory (NUL-terminated).
 * Returns the HTTP status, 0 for no answer, -1 when it could not run.
 * Offline Mode (plorpos-5tm.16): nothing goes to a server. Every fetch comes
 * back as if no network answered; a track plays from the card or not at all. */
static int plex_req_c(const char *method, const char *url, bool with_token,
                      char **body, size_t *len, int connect_s, int timeout_s)
{
	char hdr[8][200], tmp[128];
	const char *h[8];
	int nh = 0, code;
	unsigned t0;
	bool off = msrv_offline(P);

	snprintf(hdr[nh], sizeof hdr[nh], "Accept: application/json"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Product: " PRODUCT); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Version: 1.0"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Platform: " PLATFORM); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Device-Name: plorpOS"); nh++;
	snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Client-Identifier: %s", C.client); nh++;
	if (with_token) { snprintf(hdr[nh], sizeof hdr[nh], "X-Plex-Token: %s", C.token); nh++; }
	for (int i = 0; i < nh; i++) h[i] = hdr[i];
	msrv_tmpfile(P, "req", tmp, sizeof tmp);
	/* Plex's relay drops about one TLS handshake in three at once (curl 35,
	 * measured 2026-11-17): an answer that failed that fast is asked again. */
	t0 = now_ms();
	code = off ? 0 : net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
	if (code == 0 && now_ms() - t0 < 1500)
		code = msrv_offline(P) ? 0 : net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
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
static int kbps_stream(void)   { return g_srv_pass == 0 ? 0 : msrv_stream_kbps(P); }
static int kbps_download(void) { return g_srv_pass == 0 ? 0 : msrv_download_kbps(P); }

int plex_allow_sync(void) { return g_srv_sync; }

static bool pick_base(msrv *m, char *out, size_t n)
{
	bool ok;

	(void)m;
	pthread_mutex_lock(&g_pick_mu);
	LOCK();
	snprintf(out, n, "%s", g_base);
	UNLOCK();
	ok = out[0] || try_conns(out, n) || (refresh_conns() && try_conns(out, n));
	if (!ok) fprintf(stderr, "plex: no connection to %s answered\n", C.server);
	pthread_mutex_unlock(&g_pick_mu);
	return ok;
}

static void base_failed(msrv *m)
{
	(void)m;
	LOCK(); g_base[0] = '\0'; UNLOCK();
}

static void base_now(char *out, size_t n) { snprintf(out, n, "%s", g_base); }

/* A server request by path, the reply into memory. */
static int server_get(const char *path, char **body, size_t *len, int timeout_s)
{
	char base[256], url[1200];
	int code;

	if (!pick_base(P, base, sizeof base)) return 0;
	snprintf(url, sizeof url, "%s%s", base, path);
	code = plex_req("GET", url, true, body, len, timeout_s);
	if (code == 0) base_failed(P);
	return code;
}

static int server_send(const char *method, const char *path, char **body, size_t *len)
{
	char base[256], url[20000];
	int code;

	if (!pick_base(P, base, sizeof base)) return 0;
	snprintf(url, sizeof url, "%s%s", base, path);
	code = plex_req(method, url, true, body, len, 20);
	if (code == 0) base_failed(P);
	return code;
}

/* ---- sign-in, servers, libraries --------------------------------------------------- */

enum { J_PIN = MSRV_J_BACKEND, J_SERVERS, J_LIBS };

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

static void do_pin(msrv_job *j)
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

static void job(msrv *m, msrv_job *j)
{
	(void)m;
	switch (j->kind) {
	case J_PIN:     do_pin(j); break;
	case J_SERVERS: do_servers(); break;
	case J_LIBS:    do_libs(); break;
	}
}

/* ---- covers, reports ------------------------------------------------------------- */

/* Plex's resize of the album's thumb, into dest. */
static int cover_get(msrv *m, const char *thumb, const char *dest, const volatile int *cancel)
{
	char base[256], url[1400], enc[700], hdr[160];
	const char *h[1] = { hdr };

	if (!pick_base(m, base, sizeof base)) return -1;
	urlenc(thumb, enc, sizeof enc);
	snprintf(url, sizeof url, "%s/photo/:/transcode?width=%d&height=%d&minSize=1&upscale=1&url=%s",
	         base, COVER_PX, COVER_PX, enc);
	snprintf(hdr, sizeof hdr, "X-Plex-Token: %s", C.token);
	return msrv_offline(m) ? 0 : net_fetch(url, "GET", h, 1, dest, 5, 30, cancel);
}

static bool cover_cb(const char *thumb, const char *base) { return msrv_cover_fetch(P, thumb, base); }

static void timeline(msrv *m, int key, const char *state, long at_ms, long len_ms)
{
	char path[600];

	(void)m;
	snprintf(path, sizeof path,
	         "/:/timeline?ratingKey=%d&key=%%2Flibrary%%2Fmetadata%%2F%d&state=%s"
	         "&time=%ld&duration=%ld&identifier=com.plexapp.plugins.library",
	         key, key, state, at_ms, len_ms);
	server_get(path, NULL, NULL, 5);
}

static int scrobble(msrv *m, int key)
{
	char path[200];

	(void)m;
	snprintf(path, sizeof path, "/:/scrobble?identifier=com.plexapp.plugins.library&key=%d", key);
	return server_get(path, NULL, NULL, 10);
}

/* ---- favorites (plorpos-vgr.14) -------------------------------------------------- */

/* A favorite on Plex is a love: rated 10, as Plexamp does it. */
static int loved(int type, int **out, int *n)
{
	char path[200], *b = NULL;
	size_t len;
	jsv mc, md, it = { 0 }, e;
	int cap = 0;

	*out = NULL;
	*n = 0;
	snprintf(path, sizeof path, "/library/sections/%d/all?type=%d&userRating=10", C.section, type);
	if (server_get(path, &b, &len, 30) != 200 || !b) { free(b); return -1; }
	if (js_member(js_root(b, len), "MediaContainer", &mc) && js_member(mc, "Metadata", &md))
		while (js_next(md, &it, &e)) {
			int k = (int)jnum(e, "ratingKey");

			if (k <= 0) continue;
			if (*n == cap) {
				int c = cap ? cap * 2 : 64, *v = realloc(*out, sizeof *v * (size_t)c);

				if (!v) { free(b); free(*out); *out = NULL; return -1; }
				*out = v;
				cap = c;
			}
			(*out)[(*n)++] = k;
		}
	free(b);
	return 0;
}

static int fav_list(msrv *m, int **tracks, int *nt, int **albums, int *na)
{
	(void)m;
	*albums = NULL;
	*na = 0;
	if (loved(10, tracks, nt) < 0) return -1;
	if (loved(9, albums, na) < 0) { free(*tracks); *tracks = NULL; return -1; }
	return 0;
}

/* Loved, or the rating cleared (-1). */
static int fav_set(msrv *m, int key, bool album, bool on)
{
	char path[200];

	(void)m; (void)album;
	snprintf(path, sizeof path, "/:/rate?key=%d&identifier=com.plexapp.plugins.library&rating=%d", key, on ? 10 : -1);
	return server_send("PUT", path, NULL, NULL);
}

/* ---- the library sync ------------------------------------------------------------ */

static long stamp(msrv *m, long old, long since, long total_then)
{
	char *b = NULL;
	size_t len;
	jsv root, mc, dirs, it = { 0 }, d;
	long s = -1;

	(void)m; (void)old; (void)since; (void)total_then;
	if (server_get("/library/sections", &b, &len, 15) != 200 || !b) return -1;
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Directory", &dirs))
		while (js_next(dirs, &it, &d))
			if (jnum(d, "key") == C.section) {
				s = jnum(d, "contentChangedAt");
				if (!s) s = jnum(d, "updatedAt");
			}
	free(b);
	return s;
}

typedef bool (*each_fn)(jsv item, void *ctx);

/* Every item of `query`, PAGE at a time, through `each` (false: give up).
 * How many the listing has, or -1 when the server failed. `progress` counts
 * into the sync's done / total. */
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
			if (progress) msrv_sync_progress(P, -1, (int)total);
		}
		if (js_member(mc, "Metadata", &md))
			while (js_next(md, &it, &t)) {
				got++;
				if (!each(t, ctx)) { free(b); return -1; }
			}
		free(b);
		start += got;
		if (progress) msrv_sync_progress(P, (int)start, -1);
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

/* A track from a listing (a fetch_all `each`). */
static bool sset_track(jsv t, void *ctx)
{
	msrv_set *s = ctx;
	char ti[512], ta[256], at[256], alt[256], th[256], pk[320];
	jsv media, mi = { 0 }, m0, parts, pi = { 0 }, p0;
	long u = jnum(t, "updatedAt");
	msrv_rec x;

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
	x = (msrv_rec){
		.key = (int)jnum(t, "ratingKey"),
		.album = (int)jnum(t, "parentRatingKey"),
		.artist = (int)jnum(t, "grandparentRatingKey"),
		.year = (int)jnum(t, "parentYear"),
		.disc = (int)jnum(t, "parentIndex"),
		.idx = (int)jnum(t, "index"),
		.dur = (int)jnum(t, "duration"),
		.title = msrv_dupc(ti), .tartist = msrv_dupc(strcmp(ta, at) ? ta : ""),
		.album_t = msrv_dupc(alt[0] ? alt : "Unknown Album"),
		.artist_t = msrv_dupc(at[0] ? at : "Unknown Artist"),
		.thumb = msrv_dupc(th), .part = msrv_dupc(pk),
	};
	if (!x.key || !x.album || !x.artist) { msrv_rec_free(&x); return true; }
	if (!msrv_set_push(s, x)) { msrv_rec_free(&x); return false; }
	return true;
}

static long sync_full(msrv *m, msrv_set *out)
{
	char q[300];

	(void)m;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=10", C.section);
	return fetch_all(q, sset_track, out, true);
}

/* Albums and artists Plex changed whose tracks are read again: those whose
 * title, year or cover (an album) or name (an artist) is no longer what the
 * index keeps - a track carries those, and its own updatedAt need not move
 * when they do. One not in the index is new: its tracks are new too, so
 * they came with the changed tracks already. */
typedef struct { const msrv_table *t; int *key; int n, cap; bool artists; } stale;

static bool stale_each(jsv o, void *ctx)
{
	stale *s = ctx;
	char title[256], thumb[256];
	int key = (int)jnum(o, "ratingKey"), i;
	bool differs = false;

	jstr(o, "title", title, sizeof title);
	msrv_clean(title);
	if (s->artists) {
		if (!title[0]) snprintf(title, sizeof title, "Unknown Artist");
		for (i = 0; i < s->t->nar && s->t->ar[i].key != key; i++) {}
		if (i == s->t->nar) return true;
		differs = strcmp(s->t->ar[i].name, title) != 0;
	} else {
		if (!title[0]) snprintf(title, sizeof title, "Unknown Album");
		jstr(o, "thumb", thumb, sizeof thumb);
		msrv_clean(thumb);
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

/* The tracks changed since `since` (the newest updatedAt the index has), with
 * the tracks of albums and artists that changed under them, and the server's
 * track count now (plorpos-1lw.24). */
static bool changed(msrv *m, long since, const msrv_table *t, msrv_set *ch, long *total)
{
	stale st = { .t = t };
	char q[300];
	int i, nal;
	bool ok = false;

	(void)m;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=10&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, sset_track, ch, true) < 0) goto done;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=9&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, stale_each, &st, false) < 0) goto done;
	nal = st.n;
	st.artists = true;
	snprintf(q, sizeof q, "/library/sections/%d/all?type=8&updatedAt>>=%ld", C.section, since - 1);
	if (fetch_all(q, stale_each, &st, false) < 0) goto done;
	for (i = 0; i < st.n; i++) {
		snprintf(q, sizeof q, i < nal ? "/library/metadata/%d/children" : "/library/metadata/%d/allLeaves",
		         st.key[i]);
		if (fetch_all(q, sset_track, ch, false) < 0) goto done;
	}
	snprintf(q, sizeof q, "/library/sections/%d/all?type=10", C.section);
	if ((*total = list_total(q)) < 0) goto done;
	ok = true;
done:
	free(st.key);
	return ok;
}

/* ---- streams and fetches ---------------------------------------------------------- */

static void plex_stream_url(const char *base, const msrv_track *t, int kbps, bool download,
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

static void stream_url(msrv *m, const char *base, const msrv_track *t, char *out, size_t n,
                       char *codec, size_t cn)
{
	int kbps = kbps_stream();

	(void)m;
	plex_stream_url(base, t, kbps, false, out, n, "start");
	if (kbps) snprintf(codec, cn, "OPUS %d", kbps);
}

/* Track `t` (its strings the caller's own) at `kbps` into `dest`, through
 * `<dest>.part`: the original file, or Plex's Opus transcode the way the pak
 * downloads one (decision, start, stop). Returns the HTTP status of the
 * start, 0 for no answer; the file is there only on 200. */
static int fetch_track(msrv *m, const char *base, const msrv_track *t, int kbps, const char *dest,
                       const volatile int *cancel)
{
	char url[2048], part[1100], tmp[128];
	int code;

	snprintf(part, sizeof part, "%s.part", dest);
	if (kbps) {
		plex_stream_url(base, t, kbps, true, url, sizeof url, "decision");
		msrv_tmpfile(m, "dec", tmp, sizeof tmp);
		if (!msrv_offline(m)) net_fetch(url, "GET", NULL, 0, tmp, 5, 30, cancel);
		unlink(tmp);
	}
	plex_stream_url(base, t, kbps, true, url, sizeof url, "start");
	code = msrv_offline(m) ? 0 : net_fetch(url, "GET", NULL, 0, part, 5, 0, cancel);
	if (code != 200 || !exists(part) || rename(part, dest) != 0) {
		unlink(part);
		if (code == 200) code = -1;
	}
	if (kbps) {
		plex_stream_url(base, t, kbps, true, url, sizeof url, "stop");
		msrv_tmpfile(m, "stop", tmp, sizeof tmp);
		if (!msrv_offline(m)) net_fetch(url, "GET", NULL, 0, tmp, 5, 10, NULL);
		unlink(tmp);
	}
	if (code == 0) base_failed(m);
	return code;
}

/* The daemon's path is this track's: Plex's transcode for it, or its file. */
static bool owns_url(const char *daemon_path, int key, const char *part)
{
	if (!part) {
		char want[64];

		snprintf(want, sizeof want, "%%2Flibrary%%2Fmetadata%%2F%d&", key);
		return strstr(daemon_path, want) != NULL;
	}
	{
		const char *at = strstr(daemon_path, part);

		return at && (at[strlen(part)] == '?' || !at[strlen(part)]);
	}
}

/* ---- playlists -------------------------------------------------------------------- */

static int pl_list(msrv *m, msrv_spl *out, int max)
{
	char *b = NULL;
	size_t len;
	jsv root, mc, md, it = { 0 }, e;
	int n = 0;

	(void)m;
	if (server_get("/playlists?playlistType=audio", &b, &len, 30) != 200 || !b) { free(b); return -1; }
	root = js_root(b, len);
	if (js_member(root, "MediaContainer", &mc) && js_member(mc, "Metadata", &md))
		while (n < max && js_next(md, &it, &e)) {
			jsv v;
			long id = jnum(e, "ratingKey");

			if (id) snprintf(out[n].id, sizeof out[n].id, "%ld", id);
			else out[n].id[0] = '\0';
			jstr(e, "title", out[n].title, sizeof out[n].title);
			/* Plex's own list of rated tracks: Favorite Plex Tracks stands
			 * in for it here (owner, plorpos-vgr.18). */
			if (!strcmp(out[n].title, "\xe2\x9d\xa4\xef\xb8\x8f Tracks")) continue;
			out[n].leaf = (int)jnum(e, "leafCount");
			out[n].smart = js_member(e, "smart", &v) && js_is_true(v);
			n++;
		}
	free(b);
	return n;
}

/* The server's item ids of playlist `id`, with each one's track key. */
static int items_of(int id, int **keys, int **items)
{
	char path[160], *b = NULL;
	size_t len;
	jsv root, mc, md, it = { 0 }, d;
	int n = 0, cap = 0, *k = NULL, *im = NULL;

	*keys = NULL;
	if (items) *items = NULL;
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
	if (items) *items = im;
	else free(im);
	return n;
}

static int pl_items(msrv *m, const char *id, int **keys)
{
	(void)m;
	return items_of(atoi(id), keys, NULL);
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

/* Sent in parts: a uri of a few hundred keys is as long as a request line gets. */
static bool pl_create(msrv *m, const char *name, const int *keys, int n, char *idout, size_t idn)
{
	char title[400], uri[18000], path[19000], *b = NULL, v[32] = "";
	size_t len;
	jsv root, mc, md, it = { 0 }, d;
	int id = 0, at;

	(void)m;
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
	if (id) snprintf(idout, idn, "%d", id);
	return id != 0;
}

static int pl_edit(msrv *m, int op, const char *ids, const char *b, int key, int akey)
{
	char path[19000], uri[400], title[400];
	int id = atoi(ids), code = 200;

	(void)m;
	switch (op) {
	case MSRV_PLE_ADD:
		pl_uri(&key, 1, uri, sizeof uri);
		snprintf(path, sizeof path, "/playlists/%d/items?uri=%s", id, uri);
		code = server_send("PUT", path, NULL, NULL);
		break;
	case MSRV_PLE_DEL:
	case MSRV_PLE_MOVE: {
		int *keys, *items, n, i, item = 0, after = 0;

		if ((n = items_of(id, &keys, &items)) < 0) { code = 0; break; }
		for (i = 0; i < n; i++) {
			if (!item && keys[i] == key) item = items[i];
			if (!after && akey && keys[i] == akey) after = items[i];
		}
		free(keys);
		free(items);
		if (!item || (akey && !after)) break;       /* not there: the next pull settles it */
		if (op == MSRV_PLE_DEL) snprintf(path, sizeof path, "/playlists/%d/items/%d", id, item);
		else if (after) snprintf(path, sizeof path, "/playlists/%d/items/%d/move?after=%d", id, item, after);
		else snprintf(path, sizeof path, "/playlists/%d/items/%d/move", id, item);
		code = server_send(op == MSRV_PLE_DEL ? "DELETE" : "PUT", path, NULL, NULL);
		break;
	}
	case MSRV_PLE_RENAME:
		urlenc(b, title, sizeof title);
		snprintf(path, sizeof path, "/playlists/%d?title=%s", id, title);
		code = server_send("PUT", path, NULL, NULL);
		break;
	case MSRV_PLE_DELETE:
		snprintf(path, sizeof path, "/playlists/%d", id);
		code = server_send("DELETE", path, NULL, NULL);
		if (code == 204) code = 200;
		break;
	}
	return code;
}

/* ---- the source -------------------------------------------------------------------- */

static const msrv_ops OPS = {
	.tag = "plex", .dir = "plex", .music = ML_PLEX, .pl_pre = "Plex ", .pl_tsv = ".plex.tsv", .favs = "Favorite Plex Tracks",
	.cover_cb = cover_cb,
	.cfg_reset = cfg_reset, .cfg_key = cfg_key, .cfg_loaded = cfg_loaded, .cfg_write = cfg_write,
	.signed_in = signed_in, .ready = ready, .rel_id = rel_id, .library = library,
	.pick_base = pick_base, .base_failed = base_failed, .base_now = base_now,
	.kbps_stream = kbps_stream, .kbps_download = kbps_download,
	.stamp = stamp, .sync_full = sync_full, .changed = changed,
	.stream_url = stream_url, .fetch_track = fetch_track, .cover_get = cover_get, .owns_url = owns_url,
	.timeline = timeline, .scrobble = scrobble,
	.pl_list = pl_list, .pl_items = pl_items, .pl_create = pl_create, .pl_edit = pl_edit,
	.fav_list = fav_list, .fav_set = fav_set,
	.job = job,
};

void plex_init(const char *card, const char *shared) { P = msrv_new(&OPS, card, shared); }
msrv *plex_src(void) { return P; }

bool plex_signed_in(void) { return signed_in(); }
bool plex_ready(void) { return ready(); }
const char *plex_server_name(void) { return C.server; }
const char *plex_library_name(void) { return C.library; }
const char *plex_account_name(void) { return C.account; }

void plex_sign_out(void)
{
	msrv_download_cancel(P);
	LOCK();
	C.token[0] = C.account[0] = C.server[0] = C.server_id[0] = C.library[0] = '\0';
	C.nconns = 0; C.section = 0; g_base[0] = '\0';
	msrv_rel_root_set(P);
	UNLOCK();
	cfg_save();
	fprintf(stderr, "plex: signed out\n");
}

void plex_pin_start(void)
{
	msrv_job *j = msrv_job_new(J_PIN);

	g_pin_state = 1;
	LOCK(); g_pin_code[0] = '\0'; g_pin_id = 0; UNLOCK();
	if (j) msrv_push(P, j, true); else g_pin_state = -1;
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
		msrv_job *j = msrv_job_new(J_PIN);
		last = now;
		if (j) { j->n1 = 1; msrv_push(P, j, false); }
	}
	return 0;
}

void plex_pin_cancel(void) { g_pin_state = 0; LOCK(); g_pin_id = 0; UNLOCK(); }

void plex_servers_ask(void)
{
	msrv_job *j = msrv_job_new(J_SERVERS);
	g_nservers = 0;
	if (j) msrv_push(P, j, true); else g_nservers = -1;
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
	msrv_rel_root_set(P);
	UNLOCK();
	cfg_save();
}

void plex_libraries_ask(void)
{
	msrv_job *j = msrv_job_new(J_LIBS);
	g_nlibs = 0;
	if (j) msrv_push(P, j, true); else g_nlibs = -1;
}
int plex_libraries_poll(void) { return g_nlibs; }
const char *plex_library_at(int i) { return i >= 0 && i < g_nlibs ? g_libs[i].name : ""; }
void plex_library_pick(int i)
{
	if (i < 0 || i >= g_nlibs) return;
	LOCK();
	C.section = g_libs[i].key;
	snprintf(C.library, sizeof C.library, "%s", g_libs[i].name);
	msrv_rel_root_set(P);
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

/* The rest is the shared source's. */
int  plex_stream_kbps(void)              { return msrv_stream_kbps(P); }
int  plex_download_kbps(void)            { return msrv_download_kbps(P); }
void plex_set_stream_kbps(int kbps)      { msrv_set_stream_kbps(P, kbps); }
void plex_set_download_kbps(int kbps)    { msrv_set_download_kbps(P, kbps); }
bool plex_lib_cached(void)               { return msrv_lib_cached(P); }
bool plex_lib_load(ml_lib *out)          { return msrv_lib_load(P, out); }
int  plex_album_year(int al)             { return msrv_album_year(P, al); }
void plex_sync_start(bool force)         { msrv_sync_start(P, force); }
int  plex_sync_state(int *done, int *total) { return msrv_sync_state(P, done, total); }
int  plex_sync_outcome(void)             { return msrv_sync_outcome(P); }
bool plex_sync_take(void)                { return msrv_sync_take(P); }
bool plex_is_path(const char *rel)       { return msrv_is_path(P, rel); }
const char *plex_rel_root(void)          { return msrv_rel_root(P); }
int  plex_track_index(const char *rel)   { return msrv_track_index(P, rel); }
bool plex_title(const char *rel, char *out, size_t n) { return msrv_title(P, rel, out, n); }
void plex_pl_dir(char *out, size_t n)    { msrv_pl_dir(P, out, n); }
bool plex_cover_want(int al, const char *base) { return msrv_cover_want(P, al, base); }
void plex_cover_bump(const char *base)   { msrv_cover_bump(P, base); }
bool plex_covers_idle(void)              { return msrv_covers_idle(P); }
bool plex_cover_take(char *base, size_t n) { return msrv_cover_take(P, base, n); }
bool plex_resolve(const char *rel, musec_remote *out) { return msrv_resolve(P, rel, out); }
bool plex_owns(const char *daemon_path, const char *rel) { return msrv_owns(P, daemon_path, rel); }
bool plex_stream_failed(const char *rel) { return msrv_stream_failed(P, rel); }
void plex_prefetch(const char *playing, const char *const *next, int n) { msrv_prefetch(P, playing, next, n); }
int  plex_ahead(void)                    { return msrv_ahead(P); }
void plex_set_ahead(int n)               { msrv_set_ahead(P, n); }
void plex_tick(const char *rel, int state, double at, double len) { msrv_tick(P, rel, state, at, len); }
int  plex_album_downloaded(int al)       { return msrv_album_downloaded(P, al); }
void plex_albums_local(unsigned char *out, int n) { msrv_albums_local(P, out, n); }
bool plex_track_local(int t)             { return msrv_track_local(P, t); }
void plex_download_album(int al)         { msrv_download_album(P, al); }
void plex_delete_album(int al)           { msrv_delete_album(P, al); }
bool plex_album_downloading(int al)      { return msrv_album_downloading(P, al); }
_Static_assert(sizeof(plex_queued) == sizeof(msrv_queued), "plex_queued is msrv_queued");
int  plex_download_queue(plex_queued *out, int max) { return msrv_download_queue(P, (msrv_queued *)out, max); }
void plex_download_cancel_album(int al)  { msrv_download_cancel_album(P, al); }
void plex_download_cancel(void)          { msrv_download_cancel(P); }
bool plex_download_status(char *out, size_t n) { return msrv_download_status(P, out, n); }
bool plex_download_take_changed(void)    { return msrv_download_take_changed(P); }
bool plex_download_take_failed(void)     { return msrv_download_take_failed(P); }
void plex_playlists_sync(void)           { msrv_playlists_sync(P); }
bool plex_pl_smart(const char *name)     { return msrv_pl_smart(P, name); }
bool plex_pl_downloaded(const char *name) { return msrv_pl_downloaded(P, name); }
void plex_pl_download(const char *name, bool on) { msrv_pl_download(P, name, on); }
void plex_pl_added(const char *name, const char *track)   { msrv_pl_added(P, name, track); }
void plex_pl_removed(const char *name, const char *track) { msrv_pl_removed(P, name, track); }
void plex_pl_moved(const char *name, const char *track, const char *after) { msrv_pl_moved(P, name, track, after); }
void plex_pl_renamed(const char *from, const char *to)    { msrv_pl_renamed(P, from, to); }
void plex_pl_deleted(const char *name)   { msrv_pl_deleted(P, name); }
int  plex_pl_gen(void)                   { return msrv_pl_gen(P); }
void plex_set_pl_mirror(bool on)         { msrv_set_pl_mirror(P, on); }
void plex_set_offline(bool on)           { msrv_set_offline(P, on); }
bool plex_offline(void)                  { return msrv_get_offline(P); }
void plex_plays_retry(void)              { msrv_plays_retry(P); }
