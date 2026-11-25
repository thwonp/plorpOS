/* SPDX-License-Identifier: MIT */
/* See jellyfin.h. The Jellyfin 12 API as REPORT.md maps it (plorpos-vgr.1):
 * the MediaBrowser Authorization header (12.x turned the legacy ones off),
 * Quick Connect or a password, the user's music views, tracks paged from
 * /Items, the universal audio endpoint, /Sessions/Playing reports and the
 * playlists' calls. The shared half is msrv.c. */
#include "jellyfin.h"
#include "jfparse.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "net.h"
#include "rajson.h"
#include "tailscale.h"

#ifndef TORTOS_VERSION
#define TORTOS_VERSION "0.0"
#endif
#ifdef TS_HOST
#define JF_DEVICE TS_HOST
#else
#define JF_DEVICE "plorpos"
#endif

#define PAGE       2000    /* tracks per /Items page: each request costs ~2.5 s on top (plorpos-vgr.5) */
#define COVER_PX   500
#define MAX_VIEWS  16
#define PLS_TRACKS_MAX 2000
#define SINCE_SLACK 600     /* a sync's own start, less this: the next one's since */

static msrv *P;
#define LOCK()   msrv_lock(P)
#define UNLOCK() msrv_unlock(P)
#define now_ms   msrv_now_ms

/* ---- the config (P_SHARED/jellyfin/config, 0600) ------------------------------ */

static struct {
	char server[256];                     /* "http://192.168.0.2:8096" */
	char tailnet[256];                    /* the same server on the tailnet, or "" (plorpos-vgr.7) */
	char server_id[64], server_name[128];
	char device[48], token[128], user_id[64], user[128];
	char view[64], library[128];          /* view "": all music libraries */
} C;
static char g_base[256];                  /* the address that answered; under the lock */
static jf_keys *g_keys;                   /* under g_kmu */
static pthread_mutex_t g_kmu = PTHREAD_MUTEX_INITIALIZER;

static void cfg_reset(void) { memset(&C, 0, sizeof C); }

static void cfg_key(const char *k, const char *v)
{
#define K(name) else if (!strcmp(k, #name)) snprintf(C.name, sizeof C.name, "%s", v)
	if (0) {}
	K(server); K(tailnet); K(server_id); K(server_name); K(device); K(token); K(user_id); K(user); K(view); K(library);
#undef K
}

/* A device id of our own, made once (REPORT.md: one per device; two sharing
 * one sign each other out). */
static void cfg_loaded(msrv *m)
{
	unsigned char r[16];
	int fd, i;

	if (C.device[0]) return;
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0 || read(fd, r, sizeof r) != (ssize_t)sizeof r)
		for (i = 0; i < 16; i++) r[i] = (unsigned char)(rand() ^ (int)now_ms());
	if (fd >= 0) close(fd);
	strcpy(C.device, "plorpos-");
	for (i = 0; i < 16; i++) sprintf(C.device + strlen(C.device), "%02x", r[i]);
	msrv_cfg_save(m);
}

static void cfg_write(FILE *f)
{
	fprintf(f, "server=%s\ntailnet=%s\nserver_id=%s\nserver_name=%s\ndevice=%s\ntoken=%s\nuser_id=%s\nuser=%s\nview=%s\nlibrary=%s\n",
	        C.server, C.tailnet, C.server_id, C.server_name, C.device, C.token, C.user_id, C.user, C.view, C.library);
}

#define cfg_save() msrv_cfg_save(P)

static bool signed_in(void) { return C.token[0] && C.user_id[0]; }
static bool ready(void) { return signed_in() && C.server_id[0] && C.library[0]; }
static const char *library(void) { return C.library; }

static bool rel_id(char *out, size_t n)
{
	if (!C.server_id[0] || !C.library[0]) return false;
	snprintf(out, n, "%.12s-%.12s", C.server_id, C.view[0] ? C.view : "all");
	return true;
}

/* ---- requests -------------------------------------------------------------------- */

static void jstr(jsv o, const char *k, char *out, size_t n)
{
	jsv v;

	out[0] = '\0';
	if (js_member(o, k, &v)) js_str(v, out, n);
}

/* JSON's string escapes, for a name sent in a body. */
static void jesc(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 7 < n; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
		else if (c < 32) o += (size_t)snprintf(out + o, n - o, "\\u%04x", c);
		else out[o++] = (char)c;
	}
	out[o] = '\0';
}

static void auth_hdr(char *out, size_t n)
{
	int o = snprintf(out, n, "Authorization: MediaBrowser Client=\"plorpOS\", Device=\"%s\", DeviceId=\"%s\", Version=\"%s\"",
	                 JF_DEVICE, C.device, TORTOS_VERSION);

	if (C.token[0] && o > 0 && (size_t)o < n) snprintf(out + o, n - (size_t)o, ", Token=\"%s\"", C.token);
}

/* `method` `at``path` with the auth header and an optional JSON body, the
 * reply into memory. The HTTP status, 0 for no answer, -1 when it could not run. */
static int req_at(const char *at, const char *method, const char *path, const char *json,
                  char **body, size_t *len, int connect_s, int timeout_s)
{
	char url[2400], auth[512], tmp[128];
	const char *h[3];
	int nh = 0, code;

	if (body) *body = NULL;
	if (msrv_offline(P)) return 0;
	snprintf(url, sizeof url, "%s%s", at, path);
	auth_hdr(auth, sizeof auth);
	h[nh++] = "Accept: application/json";
	h[nh++] = auth;
	if (json) h[nh++] = "Content-Type: application/json";
	msrv_tmpfile(P, "req", tmp, sizeof tmp);
	code = json ? net_fetch_body(url, method, h, nh, json, tmp, connect_s, timeout_s, NULL)
	            : net_fetch(url, method, h, nh, tmp, connect_s, timeout_s, NULL);
	if (body) {
		*body = code >= 200 && code < 300 ? msrv_slurp(tmp, len) : NULL;
		if (code >= 200 && code < 300 && !*body) code = -1;
	}
	unlink(tmp);
	return code;
}

/* The server's address: the one saved, once it has answered as the same
 * server. Kept until a request through it gets no answer. (The tailnet
 * address comes with plorpos-vgr.7.) */
static pthread_mutex_t g_pick_mu = PTHREAD_MUTEX_INITIALIZER;

static bool pick_base(msrv *m, char *out, size_t n)
{
	char at[2][256], id[64];
	int i, na = 0;
	bool ok = false, ts = ts_connected();   /* asked before the locks: it may wait on the daemon */

	(void)m;
	pthread_mutex_lock(&g_pick_mu);
	LOCK();
	snprintf(out, n, "%s", g_base);
	/* Tailnet Server first while Tailscale is connected, as Audiobookshelf's
	 * (plorpos-vgr.7); off the tailnet it is asked only when it is the one
	 * address there is. */
	if (C.tailnet[0] && (ts || !C.server[0])) snprintf(at[na++], sizeof at[0], "%s", C.tailnet);
	if (C.server[0]) snprintf(at[na++], sizeof at[0], "%s", C.server);
	UNLOCK();
	if (out[0]) ok = true;
	for (i = 0; !ok && i < na; i++) {
		char *b = NULL;
		size_t len;

		if (req_at(at[i], "GET", "/System/Info/Public", NULL, &b, &len, 3, 6) == 200 && b) {
			jstr(js_root(b, len), "Id", id, sizeof id);
			if (!strcmp(id, C.server_id)) {
				LOCK(); snprintf(g_base, sizeof g_base, "%.255s", at[i]); UNLOCK();
				snprintf(out, n, "%s", at[i]);
				fprintf(stderr, "jellyfin: server %s at %s\n", C.server_name, at[i]);
				ok = true;
			} else fprintf(stderr, "jellyfin: %s is another server now\n", at[i]);
		}
		free(b);
	}
	if (!ok) fprintf(stderr, "jellyfin: %s did not answer\n", C.server_name);
	pthread_mutex_unlock(&g_pick_mu);
	return ok;
}

static void base_failed(msrv *m)
{
	(void)m;
	LOCK(); g_base[0] = '\0'; UNLOCK();
}

static void base_now(char *out, size_t n) { snprintf(out, n, "%s", g_base); }

static int server_req(const char *method, const char *path, const char *json, char **body, size_t *len,
                      int timeout_s)
{
	char base[256];
	int code;

	if (body) *body = NULL;
	if (!pick_base(P, base, sizeof base)) return 0;
	code = req_at(base, method, path, json, body, len, 5, timeout_s);
	if (code == 0) base_failed(P);
	return code;
}

static int kbps_stream(void)   { return msrv_stream_kbps(P); }
static int kbps_download(void) { return msrv_download_kbps(P); }

/* The id part of a track's index part, "<id>.<ext>". */
static void part_id(const char *part, char *out, size_t n)
{
	size_t l = strcspn(part, ".");

	snprintf(out, n, "%.*s", (int)(l < n ? l : n - 1), part);
}

static int key_of_id(const char *id)
{
	int k;

	pthread_mutex_lock(&g_kmu);
	k = jf_key_find(g_keys, id);
	pthread_mutex_unlock(&g_kmu);
	return k;
}

static bool id_of_key(int key, char *out, size_t n)
{
	const char *id;

	pthread_mutex_lock(&g_kmu);
	id = jf_key_id(g_keys, key);
	snprintf(out, n, "%s", id ? id : "");
	pthread_mutex_unlock(&g_kmu);
	return out[0] != '\0';
}

/* ---- the address, signing in, the library ---------------------------------------- */

enum { J_ADDR = MSRV_J_BACKEND, J_QC, J_QCPOLL, J_LOGIN, J_VIEWS, J_LOGOUT, J_TADDR };

static volatile int g_auth;                /* 0 idle, 1 waiting, 2 done, -1 failed */
static char g_why[96];
static char g_qc_code[16], g_qc_secret[128];
static volatile int g_qc;                  /* 0 idle, 1 waiting for approval, 2 signed in, -1 failed */
typedef struct { char id[64], name[128]; } view_t;
static view_t g_views[MAX_VIEWS + 1];
static volatile int g_nviews;

static void fail(volatile int *state, const char *why)
{
	LOCK(); snprintf(g_why, sizeof g_why, "%s", why); UNLOCK();
	*state = -1;
}

/* "192.168.0.2:8096" is enough: the scheme is added, a trailing / goes. */
static void addr_norm(const char *address, char *a, size_t cap)
{
	size_t n;

	snprintf(a, cap, "%s%s", strstr(address, "://") ? "" : "http://", address);
	for (n = strlen(a); n && (a[n - 1] == '/' || a[n - 1] == ' '); n--) a[n - 1] = '\0';
}

static void do_addr(msrv_job *j)
{
	char *b = NULL, id[64], name[128], prod[64];
	size_t len;
	int code = req_at(j->a, "GET", "/System/Info/Public", NULL, &b, &len, 5, 10);
	jsv root;

	if (code != 200 || !b) {
		free(b);
		fail(&g_auth, code == 0 ? "The server did not answer" : "Not a Jellyfin server");
		return;
	}
	root = js_root(b, len);
	jstr(root, "Id", id, sizeof id);
	jstr(root, "ServerName", name, sizeof name);
	jstr(root, "ProductName", prod, sizeof prod);
	free(b);
	if (!id[0] || (prod[0] && !strstr(prod, "Jellyfin"))) { fail(&g_auth, "Not a Jellyfin server"); return; }
	LOCK();
	if (strcmp(C.server_id, id)) {             /* another server: signed out of the old one */
		C.token[0] = C.user_id[0] = C.user[0] = C.view[0] = C.library[0] = C.tailnet[0] = '\0';
	}
	snprintf(C.server, sizeof C.server, "%.255s", j->a);
	snprintf(C.server_id, sizeof C.server_id, "%s", id);
	snprintf(C.server_name, sizeof C.server_name, "%s", name);
	snprintf(g_base, sizeof g_base, "%.255s", j->a);
	msrv_rel_root_set(P);
	UNLOCK();
	cfg_save();
	fprintf(stderr, "jellyfin: %s (%s) at %s\n", name, id, j->a);
	g_auth = 2;
}

/* A sign-in's answer: the account's token, its user and the server's id. */
static bool take_auth(const char *b, size_t len)
{
	jsv root = js_root(b, len), user;
	char tok[128], uid[64] = "", uname[128] = "";

	jstr(root, "AccessToken", tok, sizeof tok);
	if (js_member(root, "User", &user)) {
		jstr(user, "Id", uid, sizeof uid);
		jstr(user, "Name", uname, sizeof uname);
	}
	if (!tok[0] || !uid[0]) return false;
	LOCK();
	snprintf(C.token, sizeof C.token, "%s", tok);
	snprintf(C.user_id, sizeof C.user_id, "%s", uid);
	snprintf(C.user, sizeof C.user, "%s", uname);
	UNLOCK();
	cfg_save();
	fprintf(stderr, "jellyfin: signed in as %s\n", uname);
	return true;
}

static void do_login(msrv_job *j)
{
	char body[1200], u[400], p[400], *b = NULL;
	size_t len;
	int code;

	jesc(j->a, u, sizeof u);
	jesc(j->b, p, sizeof p);
	snprintf(body, sizeof body, "{\"Username\":\"%s\",\"Pw\":\"%s\"}", u, p);
	memset(j->b, 0, sizeof j->b);              /* the password is not kept */
	LOCK(); C.token[0] = '\0'; UNLOCK();
	code = server_req("POST", "/Users/AuthenticateByName", body, &b, &len, 15);
	memset(body, 0, sizeof body);
	if (code == 200 && b && take_auth(b, len)) g_auth = 2;
	else fail(&g_auth, code == 401 ? "Wrong username or password" : code == 0 ? "The server did not answer"
	                                : "The server refused the sign-in");
	free(b);
}

static void do_qc(void)
{
	char *b = NULL;
	size_t len;
	int code;
	jsv root;

	LOCK(); C.token[0] = '\0'; UNLOCK();
	code = server_req("POST", "/QuickConnect/Initiate", NULL, &b, &len, 10);
	if (code != 200 || !b) {
		free(b);
		fail(&g_qc, code == 401 || code == 403 ? "Quick Connect is off on this server"
		            : code == 0 ? "The server did not answer" : "Quick Connect failed");
		return;
	}
	root = js_root(b, len);
	LOCK();
	jstr(root, "Code", g_qc_code, sizeof g_qc_code);
	jstr(root, "Secret", g_qc_secret, sizeof g_qc_secret);
	UNLOCK();
	free(b);
	if (!g_qc_code[0] || !g_qc_secret[0]) fail(&g_qc, "Quick Connect failed");
}

static void do_qc_poll(void)
{
	char path[300], body[300], secret[128], *b = NULL;
	size_t len;
	jsv v;
	bool approved = false;
	int code;

	LOCK(); snprintf(secret, sizeof secret, "%s", g_qc_secret); UNLOCK();
	if (!secret[0] || g_qc != 1) return;
	snprintf(path, sizeof path, "/QuickConnect/Connect?secret=%s", secret);
	code = server_req("GET", path, NULL, &b, &len, 10);
	if (code == 200 && b) approved = js_member(js_root(b, len), "Authenticated", &v) && js_is_true(v);
	free(b);
	b = NULL;
	if (code == 404 || code == 401) { fail(&g_qc, "The code expired"); return; }
	if (!approved) return;
	snprintf(body, sizeof body, "{\"Secret\":\"%s\"}", secret);
	code = server_req("POST", "/Users/AuthenticateWithQuickConnect", body, &b, &len, 15);
	if (code == 200 && b && take_auth(b, len)) g_qc = 2;
	else fail(&g_qc, "The server refused the sign-in");
	free(b);
}

static void do_views(void)
{
	char path[200], *b = NULL;
	size_t len;
	jsv root, items, it = { 0 }, e;
	int n = 0;

	snprintf(path, sizeof path, "/UserViews?userId=%s", C.user_id);
	if (server_req("GET", path, NULL, &b, &len, 15) != 200 || !b) { free(b); g_nviews = -1; return; }
	root = js_root(b, len);
	if (js_member(root, "Items", &items))
		while (n < MAX_VIEWS && js_next(items, &it, &e)) {
			char ct[32];

			jstr(e, "CollectionType", ct, sizeof ct);
			if (strcmp(ct, "music")) continue;
			jstr(e, "Id", g_views[n].id, sizeof g_views[n].id);
			jstr(e, "Name", g_views[n].name, sizeof g_views[n].name);
			if (g_views[n].id[0]) n++;
		}
	free(b);
	if (n >= 2) { g_views[n].id[0] = '\0'; snprintf(g_views[n].name, sizeof g_views[n].name, "All music"); n++; }
	g_nviews = n > 0 ? n : -1;
}

static void do_logout(msrv_job *j)
{
	char auth[600];
	const char *h[1] = { auth };
	char url[400], tmp[128];

	/* The token the job was given: the config's is gone already. */
	snprintf(auth, sizeof auth, "Authorization: MediaBrowser Client=\"plorpOS\", Device=\"%s\", DeviceId=\"%s\", "
	         "Version=\"%s\", Token=\"%.127s\"", JF_DEVICE, C.device, TORTOS_VERSION, j->b);
	snprintf(url, sizeof url, "%.255s/Sessions/Logout", j->a);
	msrv_tmpfile(P, "out", tmp, sizeof tmp);
	if (!msrv_offline(P)) net_fetch(url, "POST", h, 1, tmp, 5, 10, NULL);
	unlink(tmp);
	memset(j->b, 0, sizeof j->b);
}

/* Tailnet Server (plorpos-vgr.7), as Audiobookshelf's: the same server's
 * address on the tailnet, an empty one clears it. Only this server's is
 * taken; no answer is saved anyway - typed with Tailscale off - and says so. */
static void do_taddr(msrv_job *j)
{
	char *b = NULL, id[64] = "";
	const char *why = "";
	size_t len;
	int code;

	if (j->a[0]) {
		code = req_at(j->a, "GET", "/System/Info/Public", NULL, &b, &len, 5, 10);
		if (code == 200 && b) jstr(js_root(b, len), "Id", id, sizeof id);
		free(b);
		if (code != 0 && (code != 200 || !id[0])) { fail(&g_auth, "Not a Jellyfin server"); return; }
		if (code == 200 && strcmp(id, C.server_id)) { fail(&g_auth, "Not the same server"); return; }
		if (code == 0) why = "Saved. No answer now";
	}
	LOCK();
	if (!j->a[0] && !C.server[0]) { UNLOCK(); fail(&g_auth, "Keep one address"); return; }
	snprintf(C.tailnet, sizeof C.tailnet, "%.255s", j->a);
	snprintf(g_why, sizeof g_why, "%s", why);
	g_base[0] = '\0';                        /* picked again, by the new addresses */
	UNLOCK();
	cfg_save();
	g_auth = 2;
}

static void job(msrv *m, msrv_job *j)
{
	(void)m;
	switch (j->kind) {
	case J_TADDR:  do_taddr(j); break;
	case J_ADDR:   do_addr(j); break;
	case J_QC:     do_qc(); break;
	case J_QCPOLL: do_qc_poll(); break;
	case J_LOGIN:  do_login(j); break;
	case J_VIEWS:  do_views(); break;
	case J_LOGOUT: do_logout(j); break;
	}
}

/* ---- the library sync ------------------------------------------------------------ */

static long g_sync_t0;                     /* this sync's start, epoch s */

/* "&ParentId=<view>" or nothing, for every listing of the library. */
static void lib_q(char *out, size_t n)
{
	snprintf(out, n, "userId=%s&IncludeItemTypes=Audio&Recursive=true%s%s", C.user_id,
	         C.view[0] ? "&ParentId=" : "", C.view);
}

/* How many tracks the library has (`since` > 0: saved since then), -1 failed. */
static long count(long since)
{
	char q[300], path[500], iso[32] = "", *b = NULL;
	size_t len;
	jsv v;
	long n = -1;

	lib_q(q, sizeof q);
	if (since > 0) jf_iso(since, iso, sizeof iso);
	snprintf(path, sizeof path, "/Items?%s&Limit=0%s%s", q, since > 0 ? "&minDateLastSaved=" : "", iso);
	if (server_req("GET", path, NULL, &b, &len, 30) == 200 && b && js_member(js_root(b, len), "TotalRecordCount", &v))
		n = js_int(v);
	free(b);
	return n;
}

/* Jellyfin keeps no library stamp (REPORT.md): unchanged is the same count
 * and nothing saved since the index's last sync - then the index's own stamp. */
static long stamp(msrv *m, long old, long since, long total_then)
{
	long now = (long)time(NULL), n, saved;

	(void)m;
	g_sync_t0 = now;
	if ((n = count(0)) < 0) return -1;
	if (old < 0 || since <= 0 || n != total_then) return now;
	if ((saved = count(since)) < 0) return -1;
	return saved > 0 ? now : old;
}

/* Every track of a listing into `out`, PAGE at a time; the listing's count,
 * -1 failed. Pages can overlap on 12.x (GetItems is async), so a track seen
 * twice is kept once (by key, after). Only the first page asks for the
 * count: counting the whole library costs ~2.8 s per request on carolyn's
 * 47k tracks, and later pages keep the first one's. No images: the
 * album's cover tag still comes, 16% fewer bytes (plorpos-vgr.5). */
static long fetch_tracks(const char *extra, msrv_set *out, bool progress)
{
	char q[300], path[700];
	long start = 0, total = -1;

	lib_q(q, sizeof q);
	for (;;) {
		char *b = NULL;
		size_t len;
		jsv root, items, it = { 0 }, e, v;
		int got = 0;

		snprintf(path, sizeof path, "/Items?%s%s&SortBy=SortName&SortOrder=Ascending&EnableUserData=false"
		         "&EnableImages=false&EnableTotalRecordCount=%s&StartIndex=%ld&Limit=%d",
		         q, extra, total < 0 ? "true" : "false", start, PAGE);
		if (server_req("GET", path, NULL, &b, &len, 120) != 200 || !b) { free(b); return -1; }
		root = js_root(b, len);
		if (total < 0 && js_member(root, "TotalRecordCount", &v)) {
			total = js_int(v);
			if (progress) msrv_sync_progress(P, -1, (int)total);
		}
		if (js_member(root, "Items", &items))
			while (js_next(items, &it, &e)) {
				jf_track t;
				msrv_rec x;
				char part[64];

				got++;
				if (!jf_track_parse(e, &t)) continue;
				snprintf(part, sizeof part, "%s.%s", t.id, t.ext);
				pthread_mutex_lock(&g_kmu);
				x = (msrv_rec){
					.key = jf_key(g_keys, t.id), .album = jf_key(g_keys, t.album_id),
					.artist = jf_key(g_keys, t.artist_id),
					.year = t.year, .disc = t.disc, .idx = t.idx, .dur = t.dur_ms,
				};
				pthread_mutex_unlock(&g_kmu);
				if (!x.key || !x.album || !x.artist) { free(b); return -1; }   /* keys.tsv unwritable */
				x.title = msrv_dupc(t.title); x.tartist = msrv_dupc(t.tartist);
				x.album_t = msrv_dupc(t.album); x.artist_t = msrv_dupc(t.artist);
				x.thumb = msrv_dupc(t.thumb); x.part = msrv_dupc(part);
				if (!msrv_set_push(out, x)) { msrv_rec_free(&x); free(b); return -1; }
			}
		free(b);
		start += got;
		if (progress) msrv_sync_progress(P, (int)start, -1);
		if (got == 0 || (total >= 0 && start >= total)) break;
	}
	return total >= 0 ? total : start;
}

static int cmp_key(const void *a, const void *b)
{
	int x = ((const msrv_rec *)a)->key, y = ((const msrv_rec *)b)->key;

	return x < y ? -1 : x > y;
}

/* One record per track key. */
static void dedup(msrv_set *s)
{
	int i, o = 0;

	qsort(s->r, (size_t)s->n, sizeof *s->r, cmp_key);
	for (i = 0; i < s->n; i++) {
		if (o && s->r[o - 1].key == s->r[i].key) { msrv_rec_free(&s->r[i]); continue; }
		s->r[o++] = s->r[i];
	}
	if (o != s->n) fprintf(stderr, "jellyfin: %d tracks came twice\n", s->n - o);
	s->n = o;
}

static long sync_full(msrv *m, msrv_set *out)
{
	long total;

	(void)m;
	if ((total = fetch_tracks("", out, true)) < 0) return -1;
	dedup(out);
	if (out->n < total) fprintf(stderr, "jellyfin: %d of %ld tracks read (no album, or a page moved)\n", out->n, total);
	out->since = g_sync_t0 - SINCE_SLACK;
	return total;
}

static bool changed(msrv *m, long since, const msrv_table *t, msrv_set *ch, long *total)
{
	char extra[80], iso[32];

	(void)m; (void)t;
	jf_iso(since, iso, sizeof iso);
	snprintf(extra, sizeof extra, "&minDateLastSaved=%s", iso);
	if (fetch_tracks(extra, ch, true) < 0) return false;
	dedup(ch);
	ch->since = g_sync_t0 - SINCE_SLACK;
	return (*total = count(0)) >= 0;
}

/* ---- streams, covers, reports ----------------------------------------------------- */

/* The original as it is, or Opus at the quality chosen - everything else is
 * transcoded, as Plex's is, so a copy is one format whatever the source. */
static void jf_url(const char *base, const msrv_track *t, int kbps, char *out, size_t n)
{
	char id[48];

	part_id(t->part, id, sizeof id);
	if (!kbps)
		snprintf(out, n, "%s/Audio/%s/stream?static=true&ApiKey=%s", base, id, C.token);
	else
		snprintf(out, n, "%s/Audio/%s/universal?UserId=%s&DeviceId=%s&MaxStreamingBitrate=%d"
		         "&Container=opus&AudioCodec=opus&TranscodingContainer=ogg&TranscodingProtocol=http"
		         "&MaxAudioChannels=2&ApiKey=%s", base, id, C.user_id, C.device, kbps * 1000, C.token);
}

static void stream_url(msrv *m, const char *base, const msrv_track *t, char *out, size_t n,
                       char *codec, size_t cn)
{
	int kbps = kbps_stream();

	(void)m;
	jf_url(base, t, kbps, out, n);
	if (kbps) snprintf(codec, cn, "OPUS %d", kbps);
}

static bool exists(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static int fetch_track(msrv *m, const char *base, const msrv_track *t, int kbps, const char *dest,
                       const volatile int *cancel)
{
	char url[1200], part[1100];
	int code;

	snprintf(part, sizeof part, "%s.part", dest);
	jf_url(base, t, kbps, url, sizeof url);
	code = msrv_offline(m) ? 0 : net_fetch(url, "GET", NULL, 0, part, 5, 0, cancel);
	if (code != 200 || !exists(part) || rename(part, dest) != 0) {
		unlink(part);
		if (code == 200) code = -1;
	}
	if (code == 0) base_failed(m);
	return code;
}

/* thumb: "<album id>/<tag>". */
static int cover_get(msrv *m, const char *thumb, const char *dest, const volatile int *cancel)
{
	char base[256], url[700], auth[512], id[48];
	const char *tag = strchr(thumb, '/'), *h[1] = { auth };

	if (!tag || !pick_base(m, base, sizeof base)) return -1;
	snprintf(id, sizeof id, "%.*s", (int)(tag - thumb), thumb);
	snprintf(url, sizeof url, "%s/Items/%s/Images/Primary?fillWidth=%d&fillHeight=%d&quality=90&tag=%s",
	         base, id, COVER_PX, COVER_PX, tag + 1);
	auth_hdr(auth, sizeof auth);
	return msrv_offline(m) ? 0 : net_fetch(url, "GET", h, 1, dest, 5, 30, cancel);
}

static bool cover_cb(const char *thumb, const char *base) { return msrv_cover_fetch(P, thumb, base); }

static bool owns_url(const char *daemon_path, int key, const char *part)
{
	char want[64], id[48];

	(void)key;
	if (!part) return false;
	part_id(part, id, sizeof id);
	snprintf(want, sizeof want, "/Audio/%s/", id);
	return strstr(daemon_path, want) != NULL;
}

/* What plays, as Jellyfin's sessions want it: Playing once per track, then
 * Progress, then Stopped - which also counts the play past 90%. The core
 * sends no "stopped" between two tracks of this source, so a new track
 * stops the last one here. On the worker, one at a time. */
static int g_started;                      /* the track Playing was sent for */
static long g_pos;                         /* its last position, ms */
static bool g_paused;                      /* and whether it was paused */

static void report(const char *what, int key, long at_ms, bool paused)
{
	char id[48], body[400], path[60];

	if (!id_of_key(key, id, sizeof id)) return;
	snprintf(path, sizeof path, "/Sessions/Playing%s", what);
	snprintf(body, sizeof body, "{\"ItemId\":\"%s\",\"PositionTicks\":%lld,\"IsPaused\":%s,\"CanSeek\":true,"
	         "\"PlayMethod\":\"%s\"}", id, (long long)at_ms * 10000, paused ? "true" : "false",
	         kbps_stream() ? "Transcode" : "DirectStream");
	server_req("POST", path, body, NULL, NULL, 5);
}

static void timeline(msrv *m, int key, const char *state, long at_ms, long len_ms)
{
	bool stop = !strcmp(state, "stopped"), paused = !strcmp(state, "paused");

	(void)m; (void)len_ms;
	if (g_started && (g_started != key || stop)) {
		report("/Stopped", g_started, stop && g_started == key ? at_ms : g_pos, false);
		g_started = 0;
	}
	if (stop) return;
	if (g_started != key) { report("", key, at_ms, paused); g_started = key; }
	else report("/Progress", key, at_ms, paused);
	g_pos = at_ms;
	g_paused = paused;
}

/* A play made offline. With datePlayed the server adds one to the play
 * count; without it, it only marks the track played (plorpos-vgr.5). The
 * queue keeps no time, so the play is dated when it is sent. */
static int scrobble(msrv *m, int key)
{
	char id[48], path[240], iso[32];
	int code;

	(void)m;
	if (!id_of_key(key, id, sizeof id)) return 200;      /* not ours to send: dropped */
	jf_iso((long)time(NULL), iso, sizeof iso);
	snprintf(path, sizeof path, "/UserPlayedItems/%s?userId=%s&datePlayed=%s", id, C.user_id, iso);
	code = server_req("POST", path, NULL, NULL, NULL, 10);
	return code >= 200 && code < 300 ? 200 : code;
}

/* ---- favorites (plorpos-vgr.6) ----------------------------------------------------- */

static bool push_key(int **v, int *n, int *cap, int k)
{
	if (*n == *cap) {
		int c = *cap ? *cap * 2 : 64, *nv = realloc(*v, sizeof *nv * (size_t)c);

		if (!nv) return false;
		*v = nv;
		*cap = c;
	}
	(*v)[(*n)++] = k;
	return true;
}

/* The user's favorite tracks and albums in this library, by key. An id with
 * no key is not in the index, so not one of ours. */
static int fav_list(msrv *m, int **tracks, int *nt, int **albums, int *na)
{
	char path[400], id[40], *b = NULL;
	size_t len;
	jsv items, it = { 0 }, e;
	int ct = 0, ca = 0;

	(void)m;
	*tracks = *albums = NULL;
	*nt = *na = 0;
	snprintf(path, sizeof path, "/Items?userId=%s&Recursive=true&IncludeItemTypes=Audio,MusicAlbum&Filters=IsFavorite"
	         "&EnableImages=false&EnableUserData=false%s%s", C.user_id, C.view[0] ? "&ParentId=" : "", C.view);
	if (server_req("GET", path, NULL, &b, &len, 60) != 200 || !b) { free(b); return -1; }
	if (js_member(js_root(b, len), "Items", &items))
		while (js_next(items, &it, &e)) {
			int kind = jf_fav_parse(e, id, sizeof id), k;

			if (!kind) continue;
			pthread_mutex_lock(&g_kmu);
			k = jf_key_find(g_keys, id);
			pthread_mutex_unlock(&g_kmu);
			if (k > 0 && !(kind == 1 ? push_key(tracks, nt, &ct, k) : push_key(albums, na, &ca, k))) {
				free(b); free(*tracks); free(*albums);
				*tracks = *albums = NULL;
				return -1;
			}
		}
	free(b);
	return 0;
}

/* The playing track's favorite (plorpos-vgr.20): Jellyfin 12.4 holds the
 * item's user data as it was at Playing and writes that copy back with every
 * Progress and the Stopped, so a favorite changed mid-track was undone within
 * seconds - and the next sync took the undo as the server's own. Stopped
 * first (its stale write lands before the change), then a fresh Playing that
 * reads the new flag. Stopped at 0, so a track favorited past 90% is not
 * counted as played twice. On the worker, as timeline() is. */
static int fav_set(msrv *m, int key, bool album, bool on)
{
	char id[48], path[200];
	bool playing = !album && key == g_started;
	int code;

	(void)m;
	if (!id_of_key(key, id, sizeof id)) return 404;
	if (playing) report("/Stopped", key, 0, false);
	snprintf(path, sizeof path, "/UserFavoriteItems/%s?userId=%s", id, C.user_id);
	code = server_req(on ? "POST" : "DELETE", path, NULL, NULL, NULL, 10);
	if (playing) report("", key, g_pos, g_paused);
	return code;
}

/* ---- playlists -------------------------------------------------------------------- */

static int pl_list(msrv *m, msrv_spl *out, int max)
{
	char path[300], *b = NULL;
	size_t len;
	jsv root, items, it = { 0 }, e, v;
	int n = 0;

	(void)m;
	snprintf(path, sizeof path, "/Items?userId=%s&IncludeItemTypes=Playlist&Recursive=true&Fields=ChildCount",
	         C.user_id);
	if (server_req("GET", path, NULL, &b, &len, 30) != 200 || !b) { free(b); return -1; }
	root = js_root(b, len);
	if (js_member(root, "Items", &items))
		while (n < max && js_next(items, &it, &e)) {
			char mt[32];

			jstr(e, "MediaType", mt, sizeof mt);
			if (mt[0] && strcmp(mt, "Audio")) continue;          /* a video playlist */
			jstr(e, "Id", out[n].id, sizeof out[n].id);
			jstr(e, "Name", out[n].title, sizeof out[n].title);
			out[n].leaf = js_member(e, "ChildCount", &v) ? (int)js_int(v) : 0;
			out[n].smart = false;
			n++;
		}
	free(b);
	return n;
}

/* Playlist `id`'s tracks we know, in order, with each one's entry id (its
 * PlaylistItemId, what an edit names). -1 when the server failed. */
static int items_of(const char *id, int **keys, char (**entries)[48])
{
	char path[300], *b = NULL;
	size_t len;
	jsv root, items, it = { 0 }, e;
	int n = 0, cap = 0, *k = NULL;
	char (*en)[48] = NULL;

	*keys = NULL;
	if (entries) *entries = NULL;
	snprintf(path, sizeof path, "/Playlists/%s/Items?userId=%s&Limit=%d&EnableUserData=false", id, C.user_id,
	         PLS_TRACKS_MAX);
	if (server_req("GET", path, NULL, &b, &len, 30) != 200 || !b) { free(b); return -1; }
	root = js_root(b, len);
	if (js_member(root, "Items", &items))
		while (js_next(items, &it, &e)) {
			char tid[48];
			int key;

			jstr(e, "Id", tid, sizeof tid);
			if (!(key = key_of_id(tid))) continue;               /* not in the index */
			if (n == cap) {
				int *k2 = realloc(k, sizeof *k2 * (size_t)(cap = cap ? cap * 2 : 128));
				char (*e2)[48] = k2 ? realloc(en, sizeof *e2 * (size_t)cap) : NULL;
				if (k2) k = k2;
				if (e2) en = e2;
				if (!k2 || !e2) break;
			}
			k[n] = key;
			jstr(e, "PlaylistItemId", en[n], sizeof en[n]);
			n++;
		}
	free(b);
	*keys = k;
	if (entries) *entries = en;
	else free(en);
	return n;
}

static int pl_items(msrv *m, const char *id, int **keys)
{
	(void)m;
	return items_of(id, keys, NULL);
}

static bool pl_create(msrv *m, const char *name, const int *keys, int n, char *idout, size_t idn)
{
	char nm[400], id[48], *body, *b = NULL;
	size_t cap = (size_t)n * 40 + 1024, o, len;
	int i, code;

	(void)m;
	if (!(body = malloc(cap))) return false;
	jesc(name, nm, sizeof nm);
	o = (size_t)snprintf(body, cap, "{\"Name\":\"%s\",\"UserId\":\"%s\",\"MediaType\":\"Audio\",\"Ids\":[", nm, C.user_id);
	for (i = 0; i < n; i++)
		if (id_of_key(keys[i], id, sizeof id) && o + 50 < cap)
			o += (size_t)snprintf(body + o, cap - o, "%s\"%s\"", o && body[o - 1] == '"' ? "," : "", id);
	snprintf(body + o, cap - o, "]}");
	code = server_req("POST", "/Playlists", body, &b, &len, 30);
	free(body);
	idout[0] = '\0';
	if (code == 200 && b) jstr(js_root(b, len), "Id", idout, idn);
	free(b);
	return idout[0] != '\0';
}

static int ok204(int code) { return code == 204 ? 200 : code; }

static int pl_edit(msrv *m, int op, const char *pid, const char *b, int key, int akey)
{
	char path[600], body[500], nm[400], id[48];

	(void)m;
	switch (op) {
	case MSRV_PLE_ADD:
		if (!id_of_key(key, id, sizeof id)) return 200;
		snprintf(path, sizeof path, "/Playlists/%s/Items?ids=%s&userId=%s", pid, id, C.user_id);
		return ok204(server_req("POST", path, NULL, NULL, NULL, 20));
	case MSRV_PLE_DEL:
	case MSRV_PLE_MOVE: {
		int *keys, n, i, at = -1, to = 0, code = 200;
		char (*en)[48];

		if ((n = items_of(pid, &keys, &en)) < 0) return 0;
		for (i = 0; i < n; i++) if (keys[i] == key) { at = i; break; }
		if (at >= 0 && op == MSRV_PLE_MOVE && akey) {
			/* Its place among the others: after `akey`. */
			for (i = 0, to = -1; i < n; i++) if (keys[i] == akey) { to = i < at ? i + 1 : i; break; }
		}
		if (at >= 0 && to >= 0 && en[at][0]) {          /* not there: the next pull settles it */
			if (op == MSRV_PLE_DEL) snprintf(path, sizeof path, "/Playlists/%s/Items?entryIds=%s", pid, en[at]);
			else snprintf(path, sizeof path, "/Playlists/%s/Items/%s/Move/%d", pid, en[at], to);
			code = ok204(server_req(op == MSRV_PLE_DEL ? "DELETE" : "POST", path, NULL, NULL, NULL, 20));
		}
		free(keys);
		free(en);
		return code;
	}
	case MSRV_PLE_RENAME:
		jesc(b, nm, sizeof nm);
		snprintf(path, sizeof path, "/Playlists/%s", pid);
		snprintf(body, sizeof body, "{\"Name\":\"%s\"}", nm);
		return ok204(server_req("POST", path, body, NULL, NULL, 20));
	case MSRV_PLE_DELETE:
		snprintf(path, sizeof path, "/Items/%s", pid);
		return ok204(server_req("DELETE", path, NULL, NULL, NULL, 20));
	}
	return 200;
}

/* ---- the source -------------------------------------------------------------------- */

static const msrv_ops OPS = {
	.tag = "jellyfin", .dir = "jellyfin", .music = ML_JELLYFIN, .pl_pre = "Jellyfin ", .pl_tsv = ".jellyfin.tsv", .favs = "Favorite Jellyfin Tracks",
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

void jellyfin_init(const char *card, const char *shared)
{
	char p[700];

	snprintf(p, sizeof p, "%s/jellyfin", shared);
	msrv_mkdirs(p);
	snprintf(p, sizeof p, "%s/jellyfin/keys.tsv", shared);
	g_keys = jf_keys_open(p);
	P = msrv_new(&OPS, card, shared);
}

msrv *jellyfin_src(void) { return P; }
bool jellyfin_signed_in(void) { return signed_in(); }
bool jellyfin_ready(void) { return ready(); }
const char *jellyfin_server(void) { return C.server; }
const char *jellyfin_tailnet(void) { return C.tailnet; }
const char *jellyfin_server_name(void) { return C.server_name; }
const char *jellyfin_account(void) { return C.user; }
const char *jellyfin_library(void) { return C.library; }
const char *jellyfin_why(void) { return g_why; }

static void push_job(int kind, const char *a, const char *b, volatile int *state)
{
	msrv_job *j = msrv_job_new(kind);

	if (state) *state = 1;
	LOCK(); g_why[0] = '\0'; UNLOCK();
	if (!j) { if (state) fail(state, "Out of memory"); return; }
	if (a) snprintf(j->a, sizeof j->a, "%s", a);
	if (b) snprintf(j->b, sizeof j->b, "%s", b);
	msrv_push(P, j, true);
}

void jellyfin_sign_out(void)
{
	char at[256], tok[128];

	msrv_download_cancel(P);
	LOCK();
	snprintf(at, sizeof at, "%s", g_base[0] ? g_base : C.server);
	snprintf(tok, sizeof tok, "%s", C.token);
	C.token[0] = C.user_id[0] = C.user[0] = C.view[0] = C.library[0] = '\0';
	msrv_rel_root_set(P);
	UNLOCK();
	cfg_save();
	if (tok[0] && at[0]) push_job(J_LOGOUT, at, tok, NULL);
	memset(tok, 0, sizeof tok);
	fprintf(stderr, "jellyfin: signed out\n");
}

void jellyfin_address_set(const char *address)
{
	char a[256];

	addr_norm(address, a, sizeof a);
	push_job(J_ADDR, a, NULL, &g_auth);
}

void jellyfin_tailnet_set(const char *address)
{
	char a[256] = "";

	if (address[0]) addr_norm(address, a, sizeof a);
	push_job(J_TADDR, a, NULL, &g_auth);
}

void jellyfin_login(const char *user, const char *password)
{
	push_job(J_LOGIN, user, password, &g_auth);
}

int jellyfin_auth_poll(void) { return g_auth == 2 ? 1 : g_auth < 0 ? -1 : 0; }

void jellyfin_qc_start(void)
{
	LOCK(); g_qc_code[0] = g_qc_secret[0] = '\0'; UNLOCK();
	push_job(J_QC, NULL, NULL, &g_qc);
}

int jellyfin_qc_poll(char *code, size_t n)
{
	static unsigned last;
	unsigned now = now_ms();

	LOCK(); snprintf(code, n, "%s", g_qc_code); UNLOCK();
	if (g_qc == 2) return 1;
	if (g_qc < 0) return -1;
	/* One check every 2 s once there is a code, never two queued. */
	if (code[0] && now - last > 2000) {
		msrv_job *j = msrv_job_new(J_QCPOLL);
		last = now;
		if (j) msrv_push(P, j, false);
	}
	return 0;
}

void jellyfin_qc_cancel(void)
{
	g_qc = 0;
	LOCK(); g_qc_code[0] = g_qc_secret[0] = '\0'; UNLOCK();
}

void jellyfin_libraries_ask(void)
{
	msrv_job *j = msrv_job_new(J_VIEWS);

	g_nviews = 0;
	if (j) msrv_push(P, j, true); else g_nviews = -1;
}

int jellyfin_libraries_poll(void) { return g_nviews; }
const char *jellyfin_library_at(int i) { return i >= 0 && i < g_nviews ? g_views[i].name : ""; }

void jellyfin_library_pick(int i)
{
	if (i < 0 || i >= g_nviews) return;
	LOCK();
	snprintf(C.view, sizeof C.view, "%s", g_views[i].id);
	snprintf(C.library, sizeof C.library, "%s", g_views[i].name);
	msrv_rel_root_set(P);
	UNLOCK();
	cfg_save();
}
