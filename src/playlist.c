/* SPDX-License-Identifier: MIT */
#include "playlist.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static void pl_file(const char *dir, const char *name, char *out, size_t n)
{
	snprintf(out, n, "%s/%s.m3u", dir, name);
}

bool pl_name_ok(const char *name)
{
	const unsigned char *p;
	bool ink = false;

	if (!name || !name[0] || name[0] == '.' || strlen(name) >= PL_NAME_MAX - 5) return false;
	for (p = (const unsigned char *)name; *p; p++) {
		if (*p < 0x20 || *p == 0x7f || strchr("/\\:*?\"<>|", *p)) return false;
		if (*p != ' ') ink = true;
	}
	return ink;
}

bool pl_exists(const char *dir, const char *name)
{
	char f[PL_PATH_MAX];
	struct stat st;

	pl_file(dir, name, f, sizeof f);
	return stat(f, &st) == 0;
}

/* Entries in a file, not counting comments and blanks. */
static int pl_count(const char *path)
{
	char line[PL_PATH_MAX];
	FILE *fp = fopen(path, "r");
	int n = 0;

	if (!fp) return 0;
	while (fgets(line, sizeof line, fp)) {
		char *s = line;

		while (*s == ' ' || *s == '\t') s++;
		if (*s && *s != '#' && *s != '\n' && *s != '\r') n++;
	}
	fclose(fp);
	return n;
}

static int by_name(const void *x, const void *y)
{
	return strcasecmp(((const pl_info *)x)->name, ((const pl_info *)y)->name);
}

int pl_list(const char *dir, pl_info *out, int max)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int n = 0;

	if (!d) return 0;
	while (n < max && (e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char f[PL_PATH_MAX];

		if (e->d_name[0] == '.' || len < 5 || len - 4 >= PL_NAME_MAX ||
		    strcasecmp(e->d_name + len - 4, ".m3u")) continue;
		snprintf(out[n].name, sizeof out[n].name, "%.*s", (int)(len - 4), e->d_name);
		snprintf(f, sizeof f, "%s/%s", dir, e->d_name);
		out[n].n = pl_count(f);
		n++;
	}
	closedir(d);
	qsort(out, (size_t)n, sizeof *out, by_name);
	return n;
}

/* One line as a card-relative path, in `out`; false for a comment or blank. */
static bool pl_entry(const char *line, const char *card, char *out, size_t n)
{
	char s[PL_PATH_MAX], *p, *e;
	size_t cl = card ? strlen(card) : 0;

	snprintf(s, sizeof s, "%s", line);
	for (e = s + strlen(s); e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = '\0';
	for (p = s; *p == ' ' || *p == '\t'; p++) {}
	if (!*p || *p == '#') return false;
	for (e = p; *e; e++) if (*e == '\\') *e = '/';
	if (!strncmp(p, "../../", 6))                  snprintf(out, n, "%s", p + 6);
	else if (!strncmp(p, "../", 3))                snprintf(out, n, "Music/%s", p + 3);
	else if (cl && !strncmp(p, card, cl) && p[cl] == '/') snprintf(out, n, "%s", p + cl + 1);
	else if (p[0] == '/')                          snprintf(out, n, "%s", p);
	else                                           snprintf(out, n, PL_DIR_REL "/%s", p);
	return true;
}

int pl_read(const char *dir, const char *name, const char *card, char ***out)
{
	char f[PL_PATH_MAX], line[PL_PATH_MAX], path[PL_PATH_MAX];
	FILE *fp;
	char **v = NULL;
	int n = 0, cap = 0;

	*out = NULL;
	pl_file(dir, name, f, sizeof f);
	if (!(fp = fopen(f, "r"))) return -1;
	while (fgets(line, sizeof line, fp)) {
		if (!pl_entry(line, card, path, sizeof path)) continue;
		if (n == cap) {
			char **nv = realloc(v, sizeof *v * (size_t)(cap = cap ? cap * 2 : 32));

			if (!nv) break;
			v = nv;
		}
		if (!(v[n] = strdup(path))) break;
		n++;
	}
	fclose(fp);
	*out = v;
	return n;
}

void pl_free(char **v, int n)
{
	for (int i = 0; i < n; i++) free(v[i]);
	free(v);
}

/* A card-relative track as the playlist writes it. */
static void pl_line(const char *track, char *out, size_t n)
{
	if (!strncmp(track, "Music/", 6)) snprintf(out, n, "../%s", track + 6);
	else snprintf(out, n, "../../%s", track);
}

bool pl_add(const char *dir, const char *name, const char *track)
{
	char f[PL_PATH_MAX], line[PL_PATH_MAX];
	bool fresh;
	FILE *fp;
	bool ok;

	if (!pl_name_ok(name)) return false;
	if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
	pl_file(dir, name, f, sizeof f);
	fresh = access(f, F_OK) != 0;
	if (!(fp = fopen(f, "a"))) return false;
	pl_line(track, line, sizeof line);
	ok = (!fresh || fputs("#EXTM3U\n", fp) >= 0) && fprintf(fp, "%s\n", line) > 0;
	ok = fflush(fp) == 0 && ok;
	fsync(fileno(fp));
	return fclose(fp) == 0 && ok;
}

bool pl_remove_at(const char *dir, const char *name, int index)
{
	char f[PL_PATH_MAX], tmp[PL_PATH_MAX + 8], line[PL_PATH_MAX], path[PL_PATH_MAX];
	FILE *in, *out;
	int k = 0;
	bool ok = true;

	pl_file(dir, name, f, sizeof f);
	snprintf(tmp, sizeof tmp, "%s.tmp", f);
	if (!(in = fopen(f, "r"))) return false;
	if (!(out = fopen(tmp, "w"))) { fclose(in); return false; }
	while (fgets(line, sizeof line, in)) {
		size_t len = strlen(line);
		bool entry = pl_entry(line, NULL, path, sizeof path);

		if (entry && k++ == index) continue;
		if (!entry && line[strspn(line, " \t\r\n")] == '\0') continue;   /* blank */
		if (len && line[len - 1] != '\n') { if (fprintf(out, "%s\n", line) < 0) ok = false; }
		else if (fputs(line, out) < 0) ok = false;
	}
	fclose(in);
	ok = fflush(out) == 0 && ok;
	fsync(fileno(out));
	ok = fclose(out) == 0 && ok;
	if (!ok || k <= index || rename(tmp, f) != 0) { unlink(tmp); return false; }
	return true;
}

bool pl_rename(const char *dir, const char *from, const char *to)
{
	char a[PL_PATH_MAX], b[PL_PATH_MAX];

	if (!pl_name_ok(from) || !pl_name_ok(to)) return false;
	pl_file(dir, from, a, sizeof a);
	pl_file(dir, to, b, sizeof b);
	/* A case-only rename is the same file on exFAT, so it is allowed through;
	 * any other existing name is refused rather than overwritten. */
	if (strcasecmp(from, to) && access(b, F_OK) == 0) return false;
	return rename(a, b) == 0;
}

bool pl_delete(const char *dir, const char *name)
{
	char f[PL_PATH_MAX];

	pl_file(dir, name, f, sizeof f);
	return unlink(f) == 0;
}
