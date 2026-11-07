/* SPDX-License-Identifier: MIT */
/* A power_supply node's gauge for plat_battery (plorpos-b8b). Split out of
 * platform.c under ADR-0001 so tools/battery-check.c drives the real code
 * against fake sysfs directories with no SDL.
 *
 * The kernel's `capacity` is used whenever it says something. Two ways a
 * device can have none: the node is not the one the platform file names (a
 * sibling sharing the platform's code, the RG35XX on the H700 build), or the
 * driver's own gauge reads 0 on a charged cell (its capacity constants are
 * for another battery). So the node is found by type=Battery when the named
 * one is missing, and a 0 is checked against the cell's own charge counters
 * (charge or energy, now over full), then against its voltage, before it is
 * believed. Nothing here is a capacity written in the source, so any cell
 * size works. The result is remembered: the scan runs once. */
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Declared in platform_dev.h, which needs SDL; repeated so the check links without. */
bool battery_read(const char *hint, int *pct, bool *charging);

static bool rd_long(const char *dir, const char *name, long *v)
{
	char path[320];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	bool ok = fscanf(f, "%ld", v) == 1;
	fclose(f);
	return ok;
}

static bool rd_str(const char *dir, const char *name, char *out, size_t n)
{
	char path[320];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	bool ok = fgets(out, (int)n, f) != NULL;
	fclose(f);
	return ok;
}

/* The node to read: `hint` if it has a gauge, else the first sibling of it
 * whose type is Battery. False if neither exists. */
static bool batt_node(const char *hint, char *out, size_t n)
{
	long v;
	if (rd_long(hint, "capacity", &v) || rd_long(hint, "voltage_now", &v)) {
		snprintf(out, n, "%s", hint);
		return true;
	}
	char parent[256];
	snprintf(parent, sizeof parent, "%s", hint);
	char *slash = strrchr(parent, '/');
	if (!slash) return false;
	*slash = '\0';
	DIR *d = opendir(parent);
	if (!d) return false;
	struct dirent *e;
	bool found = false;
	while (!found && (e = readdir(d))) {
		char node[512], type[32] = { 0 };
		if (e->d_name[0] == '.') continue;
		snprintf(node, sizeof node, "%s/%s", parent, e->d_name);
		if (rd_str(node, "type", type, sizeof type) && strncmp(type, "Battery", 7) == 0) {
			snprintf(out, n, "%s", node);
			found = true;
		}
	}
	closedir(d);
	return found;
}

/* Percent from the cell's own counters: now / full, in charge or energy. -1
 * when the node does not give both. */
static int pct_counters(const char *dir)
{
	static const char *const now[]  = { "charge_now", "charge_counter", "energy_now" };
	static const char *const full[] = { "charge_full", "charge_full", "energy_full" };
	for (int i = 0; i < 3; i++) {
		long a, b;
		if (rd_long(dir, now[i], &a) && rd_long(dir, full[i], &b) && b > 0 && a > 0)
			return (int)(a >= b ? 100 : a * 100 / b);
	}
	return -1;
}

/* Percent from the resting-ish voltage (microvolts) of a Li-ion cell, linear
 * between the usual points. Rough by nature: used only when nothing better
 * exists. */
static int pct_voltage(long uv)
{
	static const struct { long mv; int pct; } t[] = {
		{ 3300, 0 }, { 3500, 5 }, { 3650, 20 }, { 3750, 40 }, { 3850, 60 },
		{ 3950, 78 }, { 4050, 90 }, { 4200, 100 },
	};
	long mv = uv / 1000;
	if (mv <= t[0].mv) return 0;
	for (unsigned i = 1; i < sizeof t / sizeof t[0]; i++)
		if (mv <= t[i].mv)
			return t[i - 1].pct + (int)((mv - t[i - 1].mv) * (t[i].pct - t[i - 1].pct)
			                            / (t[i].mv - t[i - 1].mv));
	return 100;
}

bool battery_read(const char *hint, int *pct, bool *charging)
{
	const char *fake = getenv("TORTOS_FAKE_BATT");
	if (fake && *fake) {   /* "80", or "80c" for charging */
		if (pct) *pct = atoi(fake);
		if (charging) *charging = strchr(fake, 'c') != NULL;
		return true;
	}
	static char node[512], was[256];   /* the scan's answer for this hint */
	if (strcmp(was, hint) != 0 || !node[0]) {
		node[0] = '\0';
		if (!batt_node(hint, node, sizeof node)) return false;
		snprintf(was, sizeof was, "%s", hint);
	}
	long v = -1;
	int p = rd_long(node, "capacity", &v) ? (int)v : -1;
	if (p <= 0) {   /* missing or 0: ask the cell before believing it */
		int c = pct_counters(node);
		if (c < 0 && rd_long(node, "voltage_now", &v) && v > 0) c = pct_voltage(v);
		if (c > 0 || p < 0) p = c;
	}
	if (p < 0) return false;
	if (pct) *pct = p > 100 ? 100 : p;
	if (charging) {
		char st[32] = { 0 };
		*charging = rd_str(node, "status", st, sizeof st) &&
		            (strncmp(st, "Charging", 8) == 0 || strncmp(st, "Full", 4) == 0);
	}
	return true;
}
