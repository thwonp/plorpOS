/* SPDX-License-Identifier: MIT */
#include "setmap.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct { const char *set, *title; } ent;
typedef struct { char path[512]; bool tried; char *buf; ent *e; int n; } map;

#define MAPS 2
static map g_maps[MAPS];

const char *setmap_path(const char *res_dir, const char *core, char *buf, size_t n)
{
	const char *name = !strcmp(core, "mame2000") ? "mame2000"
	                 : !strncmp(core, "fbalpha2012", 11) ? "fbalpha2012" : NULL;

	if (!name || !res_dir || !res_dir[0]) return NULL;
	if (snprintf(buf, n, "%s/maps/%s.txt", res_dir, name) >= (int)n) return NULL;
	return buf;
}

static int cmp(const void *a, const void *b)
{
	return strcasecmp(((const ent *)a)->set, ((const ent *)b)->set);
}

static void load(map *m)
{
	FILE *f = fopen(m->path, "r");
	long len;
	char *p, *nl, *tab, *dot;
	int cap = 0;

	m->tried = true;
	if (!f) return;
	if (fseek(f, 0, SEEK_END) || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) ||
	    !(m->buf = malloc((size_t)len + 1)) || fread(m->buf, 1, (size_t)len, f) != (size_t)len) {
		fclose(f);
		free(m->buf);
		m->buf = NULL;
		return;
	}
	fclose(f);
	m->buf[len] = '\0';
	for (p = m->buf; p && *p; p = nl) {
		size_t k;

		if ((nl = strchr(p, '\n'))) *nl++ = '\0';
		if ((k = strlen(p)) && p[k - 1] == '\r') p[k - 1] = '\0';
		if (!(tab = strchr(p, '\t')) || !tab[1]) continue;
		*tab = '\0';
		if ((dot = strrchr(p, '.'))) *dot = '\0';   /* "1941.zip" -> "1941" */
		if (m->n == cap) {
			ent *e = realloc(m->e, (size_t)(cap = cap ? cap * 2 : 1024) * sizeof *e);

			if (!e) break;
			m->e = e;
		}
		m->e[m->n++] = (ent){ p, tab + 1 };
	}
	qsort(m->e, (size_t)m->n, sizeof *m->e, cmp);
	fprintf(stderr, "setmap: %d sets from %s\n", m->n, m->path);
}

const char *setmap_title(const char *path, const char *set)
{
	char stem[256], *dot;
	map *m = NULL;
	ent key = { stem, NULL }, *hit;
	int i;

	if (!path || !set || snprintf(stem, sizeof stem, "%s", set) >= (int)sizeof stem) return NULL;
	if ((dot = strrchr(stem, '.'))) *dot = '\0';
	for (i = 0; i < MAPS && g_maps[i].path[0]; i++)
		if (!strcmp(g_maps[i].path, path)) { m = &g_maps[i]; break; }
	if (!m) {
		if (i == MAPS || snprintf(g_maps[i].path, sizeof g_maps[i].path, "%s", path) >= (int)sizeof g_maps[i].path) return NULL;
		m = &g_maps[i];
	}
	if (!m->tried) load(m);
	if (!m->n) return NULL;
	hit = bsearch(&key, m->e, (size_t)m->n, sizeof *m->e, cmp);
	return hit ? hit->title : NULL;
}
