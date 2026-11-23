/* SPDX-License-Identifier: MIT */
#include "tailscale.h"

#if TS_ROWS
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "db.h"
#include "rajson.h"

#define TS_SCRIPT "/mnt/SDCARD/plorpOS/tailscale.sh"
#define TS_SOCK   "/tmp/tailscaled.sock"

bool ts_on(void)
{
	return db_get_int(db_dev(), "tailscale", 0) != 0;
}

/* Saved first, so a reboot agrees with the switch even if the script fails. */
bool ts_set(bool on)
{
	pid_t pid;

	if (!db_set_int(db_dev(), "tailscale", on ? 1 : 0)) return false;
	db_write_boot_env();
	if ((pid = fork()) == 0) {
		execl("/bin/sh", "sh", TS_SCRIPT, on ? "start" : "stop", (char *)NULL);
		_exit(127);
	}
	/* The script backgrounds the daemon and the sign-in, so it returns at once. */
	if (pid > 0) waitpid(pid, NULL, 0);
	return pid > 0;
}

/* GET /localapi/v0/status over the socket, as HTTP/1.0 so the reply is not
 * chunked: it ends when the daemon closes. Bounded by a short timeout, since
 * the menu waits on it. */
static int localapi_status(char *buf, size_t cap)
{
	static const char req[] = "GET /localapi/v0/status?peers=false HTTP/1.0\r\n"
	                          "Host: local-tailscaled.sock\r\n\r\n";
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	struct timeval tv = { 0, 300 * 1000 };
	size_t got = 0;
	ssize_t r;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd < 0) return -1;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", TS_SOCK);
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0 ||
	    write(fd, req, sizeof req - 1) != (ssize_t)(sizeof req - 1)) {
		close(fd);
		return -1;
	}
	while (got < cap - 1 && (r = read(fd, buf + got, cap - 1 - got)) > 0) got += (size_t)r;
	close(fd);
	buf[got] = '\0';
	return (int)got;
}

ts_state ts_status(char *ip, size_t ipn, char *url, size_t urln)
{
	static char buf[16384];
	char st[32] = "";
	const char *body;
	jsv root, v, it = { 0 }, e;
	int n;

	if (ip && ipn) ip[0] = '\0';
	if (url && urln) url[0] = '\0';
	if (!ts_on()) return TS_OFF;
	if ((n = localapi_status(buf, sizeof buf)) <= 0 || !(body = strstr(buf, "\r\n\r\n")))
		return TS_STARTING;
	body += 4;
	root = js_root(body, (size_t)(buf + n - body));
	if (js_member(root, "BackendState", &v)) js_str(v, st, sizeof st);
	if (!strcmp(st, "Running")) {
		/* The IPv4 one: it is what gets typed into another machine. */
		if (ip && js_member(root, "TailscaleIPs", &v))
			while (js_next(v, &it, &e))
				if (js_str(e, ip, ipn) && strchr(ip, '.')) break;
		if (ip && !strchr(ip, '.')) ip[0] = '\0';
		return TS_RUNNING;
	}
	/* NeedsLogin has no link until the control server answers - with no
	 * network, never: still starting. NeedsMachineAuth is a tailnet that
	 * approves devices by hand, with no link to give. */
	if (!strcmp(st, "NeedsMachineAuth")) return TS_LOGIN;
	if (!strcmp(st, "NeedsLogin")) {
		char u[256] = "";

		if (js_member(root, "AuthURL", &v)) js_str(v, u, sizeof u);
		if (!u[0]) return TS_STARTING;
		if (url) snprintf(url, urln, "%s", u);
		return TS_LOGIN;
	}
	return TS_STARTING;
}

bool ts_connected(void)
{
	static volatile unsigned asked;   /* a benign race: two threads may both ask */
	static volatile bool up;
	struct timespec t;
	unsigned now;
	char buf[4096];
	const char *v;

	clock_gettime(CLOCK_MONOTONIC, &t);
	now = (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u) | 1u;   /* never 0 */
	if (asked && now - asked < 2000u) return up;
	/* BackendState comes early in the reply, well inside the first 4 KB -
	 * which may be cut mid-object, so it is found by hand, not parsed. The
	 * daemon indents its JSON. */
	v = localapi_status(buf, sizeof buf) > 0 ? strstr(buf, "\"BackendState\"") : NULL;
	if (v) v += strspn(v + 14, " \t:") + 14;
	up = v && !strncmp(v, "\"Running\"", 9);
	asked = now;
	return up;
}

static bool tailnet_ip(const struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		const unsigned char *b = (const unsigned char *)&((const struct sockaddr_in *)sa)->sin_addr;

		return b[0] == 100 && (b[1] & 0xc0) == 64;            /* 100.64.0.0/10 */
	}
	if (sa->sa_family == AF_INET6) {
		static const unsigned char ts[6] = { 0xfd, 0x7a, 0x11, 0x5c, 0xa1, 0xe0 };

		return !memcmp(&((const struct sockaddr_in6 *)sa)->sin6_addr, ts, sizeof ts);
	}
	return false;
}

bool ts_tailnet_addr(const char *address)
{
	const char *s = strstr(address, "://");
	char host[256];
	size_t n = 0;
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM }, *res, *r;
	bool yes = false;

	s = s ? s + 3 : address;
	if (*s == '[') {                                          /* [fd7a:...]:13378 */
		for (s++; *s && *s != ']' && n < sizeof host - 1; s++) host[n++] = *s;
	} else {
		for (; *s && *s != ':' && *s != '/' && n < sizeof host - 1; s++) host[n++] = (char)tolower((unsigned char)*s);
	}
	host[n] = '\0';
	if (n && host[n - 1] == '.') host[--n] = '\0';
	if (!n) return false;
	if (n > 7 && !strcmp(host + n - 7, ".ts.net")) return true;
	if (getaddrinfo(host, NULL, &hints, &res) != 0) return false;
	for (r = res; r && !yes; r = r->ai_next) yes = tailnet_ip(r->ai_addr);
	freeaddrinfo(res);
	return yes;
}

#else
bool     ts_on(void) { return false; }
bool     ts_connected(void) { return false; }
bool     ts_tailnet_addr(const char *address) { (void)address; return false; }
bool     ts_set(bool on) { (void)on; return false; }
ts_state ts_status(char *ip, size_t ipn, char *url, size_t urln)
{
	(void)ip; (void)ipn; (void)url; (void)urln;
	return TS_OFF;
}
#endif
