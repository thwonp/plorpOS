/* SPDX-License-Identifier: MIT */
/* See ota.h. */
#include "ota.h"

#ifdef OTA_PORT
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "net.h"
#include "platform.h"
#include "rajson.h"

#ifndef TORTOS_VERSION
#define TORTOS_VERSION "0.0"
#endif

/* Ten is plenty: a port's newest zip is never further back than that. A card
 * can point the check at a test mirror: one URL in <card>/.ota-url. */
#define OTA_API   "https://api.github.com/repos/thwonp/plorpOS/releases?per_page=10"
#define REL_JSON  "/tmp/plorpos-ota-releases.json"
#define SUMS_TMP  "/tmp/plorpos-ota-SHA256SUMS"
#define STATE     "/tmp/plorpos-ota.state"
#define CFG       "/tmp/plorpos-ota.curl"
#define MARGIN_MB 64

static unsigned now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* Dotted numbers, compared as numbers: 1.10 is newer than 1.9, 1.9.1 than 1.9. */
static int ver_cmp(const char *a, const char *b)
{
	for (;;) {
		char *ea, *eb;
		long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);

		if (x != y) return x < y ? -1 : 1;
		if (*ea != '.' && *eb != '.') return 0;
		a = *ea == '.' ? ea + 1 : ea;
		b = *eb == '.' ? eb + 1 : eb;
	}
}

static void fail(ota_job *j, const char *why)
{
	snprintf(j->err, sizeof j->err, "%s", why);
	j->phase = OTA_ERROR;
	fprintf(stderr, "ota: %s\n", why);
}

static void dir_path(char *out, size_t n, const char *leaf)
{
	snprintf(out, n, "%s/.ota%s%s", P_CARD, leaf[0] ? "/" : "", leaf);
}

static bool get(ota_job *j, const char *url, const char *out, int timeout_s)
{
	j->pid = net_spawn_get(url, out, CFG, timeout_s);
	return j->pid > 0;
}

/* -1 still running, else curl's exit status. */
static int reap(ota_job *j)
{
	int st;
	pid_t r;

	if (j->pid <= 0) return 1;
	r = waitpid(j->pid, &st, WNOHANG);
	if (r == 0) return -1;
	j->pid = 0;
	unlink(CFG);
	return (r > 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : 1;
}

/* plorpOS/ota.sh in the background, never waited for: the menu stays live
 * and the script reports through STATE. */
static void script(const char *a1, const char *a2, const char *a3)
{
	char sh[512];
	pid_t p;

	snprintf(sh, sizeof sh, "%s/ota.sh", P_ROOT);
	p = fork();
	if (p == 0) {
		if (fork() == 0) {
			int fd = open("/dev/null", O_RDWR);

			if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); close(fd); }
			setsid();
			execl("/bin/sh", "sh", sh, a1, P_CARD, a2, a3, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) waitpid(p, NULL, 0);
}

void ota_check(ota_job *j)
{
	char url[512], over[600];
	FILE *f;

	ota_cancel(j);
	memset(j, 0, sizeof *j);
	snprintf(url, sizeof url, "%s", OTA_API);
	snprintf(over, sizeof over, "%s/.ota-url", P_CARD);
	if ((f = fopen(over, "r"))) {
		if (fgets(url, sizeof url, f)) url[strcspn(url, "\r\n")] = '\0';
		fclose(f);
		fprintf(stderr, "ota: checking the test mirror %s\n", url);
	}
	unlink(REL_JSON);
	if (!get(j, url, REL_JSON, 30)) { fail(j, "fetch"); return; }
	j->phase = OTA_CHECKING;
}

/* The newest release that is neither a draft nor a pre-release and has this
 * port's zip. GitHub lists them newest first. */
static void pick(ota_job *j)
{
	static const char pre[] = "plorpOS-v", suf[] = "-" OTA_PORT ".zip";
	char *text = NULL, name[96], ver[24], path[600];
	long len;
	jsv root, it = {0}, rel, v, assets, ai, as;
	FILE *f = fopen(REL_JSON, "rb");

	if (f && fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && len < (4 << 20)
	    && fseek(f, 0, SEEK_SET) == 0 && (text = malloc((size_t)len + 1))
	    && fread(text, 1, (size_t)len, f) == (size_t)len) {
		text[len] = '\0';
	} else {
		free(text);
		text = NULL;
	}
	if (f) fclose(f);
	unlink(REL_JSON);
	if (!text) { fail(j, "fetch"); return; }

	root = js_root(text, (size_t)len);
	while (js_next(root, &it, &rel)) {
		bool zip = false;

		if (js_member(rel, "draft", &v) && js_is_true(v)) continue;
		if (js_member(rel, "prerelease", &v) && js_is_true(v)) continue;
		if (!js_member(rel, "assets", &assets)) continue;
		j->sums_url[0] = '\0';
		ai = (jsv){0};
		while (js_next(assets, &ai, &as)) {
			size_t n;

			if (!js_member(as, "name", &v) || !js_str(v, name, sizeof name)) continue;
			n = strlen(name);
			if (!strcmp(name, "SHA256SUMS")) {
				if (js_member(as, "browser_download_url", &v))
					js_str(v, j->sums_url, sizeof j->sums_url);
				continue;
			}
			if (zip || n <= sizeof pre - 1 + sizeof suf - 1 || strncmp(name, pre, sizeof pre - 1)
			    || strcmp(name + n - (sizeof suf - 1), suf))
				continue;
			n -= sizeof pre - 1 + sizeof suf - 1;
			if (n >= sizeof ver || memchr(name + sizeof pre - 1, '-', n)) continue;
			memcpy(ver, name + sizeof pre - 1, n);
			ver[n] = '\0';
			if (!js_member(as, "browser_download_url", &v) || !js_str(v, j->url, sizeof j->url))
				continue;
			snprintf(j->name, sizeof j->name, "%s", name);
			snprintf(j->ver, sizeof j->ver, "%s", ver);
			j->size = js_member(as, "size", &v) ? js_int(v) : 0;
			zip = true;
		}
		if (zip) break;
	}
	free(text);
	if (!j->name[0] || ver_cmp(j->ver, TORTOS_VERSION) <= 0) {
		fprintf(stderr, "ota: up to date (v%s; newest with a %s zip: %s)\n",
		        TORTOS_VERSION, OTA_PORT, j->name[0] ? j->ver : "none");
		j->phase = OTA_UPTODATE;
		return;
	}
	if (!j->sums_url[0] || j->size <= 0) { fail(j, "fetch"); return; }
	fprintf(stderr, "ota: v%s available (%lld bytes)\n", j->ver, j->size);
	/* Downloaded and unpacked already, then left for later: ready as it is. */
	dir_path(path, sizeof path, "ready");
	if ((f = fopen(path, "r"))) {
		char l1[64] = "", l2[32] = "";

		if (fgets(l1, sizeof l1, f) && fgets(l2, sizeof l2, f)) {
			l2[strcspn(l2, "\n")] = '\0';
			if (!strcmp(l2, j->ver)) { fclose(f); j->phase = OTA_READY; return; }
		}
		fclose(f);
	}
	j->phase = OTA_FOUND;
}

int ota_need_mb(const ota_job *j)
{
	return (int)(j->size * 4 / 1048576) + MARGIN_MB;
}

void ota_download(ota_job *j)
{
	struct statvfs s;
	char path[600];

	if (j->phase != OTA_FOUND) return;
	if (statvfs(P_CARD, &s) != 0
	    || (long long)s.f_bavail * (long long)s.f_frsize / 1048576 < ota_need_mb(j)) {
		fail(j, "space");
		return;
	}
	/* An update downloaded or unpacked before and never installed: gone. */
	dir_path(path, sizeof path, "");
	if (access(path, F_OK) == 0) {
		char cmd[700];

		snprintf(cmd, sizeof cmd, "rm -rf '%s'", path);
		if (system(cmd) != 0) { fail(j, "failed"); return; }
	}
	if (mkdir(path, 0755) != 0) { fail(j, "failed"); return; }
	unlink(SUMS_TMP);
	if (!get(j, j->sums_url, SUMS_TMP, 30)) { fail(j, "fetch"); return; }
	j->step = 0;
	j->got = 0;
	j->phase = OTA_FETCHING;
}

/* The zip's line in SHA256SUMS: "<64 hex>  <name>". */
static bool find_sum(ota_job *j)
{
	char line[256];
	FILE *f = fopen(SUMS_TMP, "r");
	bool ok = false;

	if (!f) return false;
	while (fgets(line, sizeof line, f)) {
		char *n = line + 64;

		line[strcspn(line, "\r\n")] = '\0';
		if (strlen(line) < 66) continue;
		while (*n == ' ' || *n == '*') n++;
		if (strcmp(n, j->name)) continue;
		memcpy(j->sum, line, 64);
		j->sum[64] = '\0';
		ok = strspn(j->sum, "0123456789abcdef") == 64;
		break;
	}
	fclose(f);
	unlink(SUMS_TMP);
	return ok;
}

void ota_poll(ota_job *j)
{
	char zip[600], part[620];
	unsigned now;
	int rc;

	switch (j->phase) {
	case OTA_CHECKING:
		if ((rc = reap(j)) < 0) return;
		if (rc != 0) { fail(j, "fetch"); return; }
		pick(j);
		return;
	case OTA_FETCHING:
		dir_path(zip, sizeof zip, j->name);
		snprintf(part, sizeof part, "%s.part", zip);
		now = now_ms();
		if (j->step == 1 && now - j->last_ms >= 250) {
			struct stat st;

			j->last_ms = now;
			if (stat(part, &st) == 0) j->got = st.st_size;
		}
		if ((rc = reap(j)) < 0) return;
		if (rc != 0) { unlink(part); fail(j, "fetch"); return; }
		if (j->step == 0) {
			if (!find_sum(j)) { fail(j, "fetch"); return; }
			/* An hour at most: a slow network is slow, and B stops it. */
			if (!get(j, j->url, part, 3600)) { fail(j, "fetch"); return; }
			j->step = 1;
			return;
		}
		if (rename(part, zip) != 0) { unlink(part); fail(j, "failed"); return; }
		j->got = j->size;
		unlink(STATE);
		script("stage", zip, j->sum);
		j->verifying = true;
		j->phase = OTA_STAGING;
		return;
	case OTA_STAGING: {
		char l[96] = "";
		FILE *f;

		now = now_ms();
		if (now - j->last_ms < 250) return;
		j->last_ms = now;
		if (!(f = fopen(STATE, "r"))) return;
		if (!fgets(l, sizeof l, f)) l[0] = '\0';
		fclose(f);
		l[strcspn(l, "\n")] = '\0';
		if (!strncmp(l, "unpack ", 7)) {
			j->verifying = false;
			sscanf(l + 7, "%d %d", &j->done, &j->total);
		} else if (!strncmp(l, "ready", 5)) {
			j->phase = OTA_READY;
		} else if (!strncmp(l, "error ", 6)) {
			fail(j, !strcmp(l + 6, "space") ? "space" : !strcmp(l + 6, "checksum") ? "checksum"
			        : !strcmp(l + 6, "unpack") ? "unpack" : "failed");
		}
		return;
	}
	default:
		return;
	}
}

void ota_cancel(ota_job *j)
{
	char zip[600], part[620];

	if (j->pid > 0) {
		kill(j->pid, SIGKILL);
		waitpid(j->pid, NULL, 0);
		j->pid = 0;
		unlink(CFG);
	}
	if (j->phase == OTA_FETCHING && j->name[0]) {
		dir_path(zip, sizeof zip, j->name);
		snprintf(part, sizeof part, "%s.part", zip);
		unlink(part);
		unlink(zip);
		dir_path(zip, sizeof zip, "");
		rmdir(zip);
	}
	if (j->phase == OTA_CHECKING) unlink(REL_JSON);
	j->phase = OTA_IDLE;
}

void ota_boot(void)
{
	char prev[600];

	snprintf(prev, sizeof prev, "%s/plorpOS.prev", P_CARD);
	if (access(prev, F_OK) != 0) return;
	fprintf(stderr, "ota: v%s started; removing the previous version\n", TORTOS_VERSION);
	script("ok", NULL, NULL);
}

#else

void ota_check(ota_job *j)    { j->phase = OTA_ERROR; }
void ota_download(ota_job *j) { (void)j; }
void ota_poll(ota_job *j)     { (void)j; }
void ota_cancel(ota_job *j)   { j->phase = OTA_IDLE; }
int  ota_need_mb(const ota_job *j) { (void)j; return 0; }
void ota_boot(void) {}

#endif
