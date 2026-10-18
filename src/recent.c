/* SPDX-License-Identifier: MIT */
/* The game switcher's recent games - see recent.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db.h"
#include "recent.h"

typedef struct {
	char tag[RECENT_TAG_MAX];
	char file[RECENT_FILE_MAX];
} recent_entry;

static recent_entry g_rec[RECENT_MAX];
static int g_count;
static bool g_dirty;

static int find(const char *tag, const char *file)
{
	int i;

	for (i = 0; i < g_count; i++)
		if (!strcmp(g_rec[i].tag, tag) && !strcmp(g_rec[i].file, file))
			return i;
	return -1;
}

void recent_load(void)
{
	static char buf[RECENT_MAX * (RECENT_TAG_MAX + RECENT_FILE_MAX + 2)];
	char *line, *save = NULL;

	g_count = 0;
	g_dirty = false;
	if (!db_get_str(db_lib(), "recent", buf, sizeof buf, "")) return;
	for (line = strtok_r(buf, "\n", &save); line && g_count < RECENT_MAX;
	     line = strtok_r(NULL, "\n", &save)) {
		char *tab = strchr(line, '\t');

		if (!tab || tab == line || !tab[1]) continue;
		if ((size_t)(tab - line) >= RECENT_TAG_MAX || strlen(tab + 1) >= RECENT_FILE_MAX)
			continue;
		*tab = '\0';
		if (find(line, tab + 1) >= 0) continue;
		snprintf(g_rec[g_count].tag, RECENT_TAG_MAX, "%s", line);
		snprintf(g_rec[g_count].file, RECENT_FILE_MAX, "%s", tab + 1);
		g_count++;
	}
}

void recent_note(const char *tag, const char *file)
{
	int i;

	if (!tag || !file || !*tag || !*file) return;
	if (strlen(tag) >= RECENT_TAG_MAX || strlen(file) >= RECENT_FILE_MAX) return;
	i = find(tag, file);
	if (i == 0) return;
	if (i < 0) i = g_count < RECENT_MAX ? g_count++ : RECENT_MAX - 1;
	memmove(&g_rec[1], &g_rec[0], (size_t)i * sizeof g_rec[0]);
	snprintf(g_rec[0].tag, RECENT_TAG_MAX, "%s", tag);
	snprintf(g_rec[0].file, RECENT_FILE_MAX, "%s", file);
	g_dirty = true;
}

void recent_remove(const char *tag, const char *file)
{
	int i = find(tag, file);

	if (i < 0) return;
	memmove(&g_rec[i], &g_rec[i + 1], (size_t)(g_count - i - 1) * sizeof g_rec[0]);
	g_count--;
	g_dirty = true;
	recent_flush();
}

bool recent_flush(void)
{
	static char buf[RECENT_MAX * (RECENT_TAG_MAX + RECENT_FILE_MAX + 2)];
	size_t n = 0;
	int i;

	if (!g_dirty) return true;
	buf[0] = '\0';
	for (i = 0; i < g_count; i++)
		n += (size_t)snprintf(buf + n, sizeof buf - n, "%s\t%s\n", g_rec[i].tag, g_rec[i].file);
	if (!db_set_str(db_lib(), "recent", buf)) return false;
	g_dirty = false;
	return true;
}

int recent_count(void) { return g_count; }

bool recent_at(int i, const char **tag, const char **file)
{
	if (i < 0 || i >= g_count) return false;
	if (tag)  *tag  = g_rec[i].tag;
	if (file) *file = g_rec[i].file;
	return true;
}
