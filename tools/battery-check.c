/* SPDX-License-Identifier: MIT */
/* battery_read (src/battery.c, plorpos-b8b) against fake power_supply trees:
 * the kernel's capacity when it speaks, the cell's own counters or voltage when
 * it reads 0 or is absent, and the Battery node found by type when the named
 * one does not exist. Links src/battery.c only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdbool.h>

bool battery_read(const char *dir, int *pct, bool *charging);

static int fails;
static char root[] = "build-native/battery-XXXXXX";

static void put(const char *node, const char *name, const char *val)
{
	char p[400];
	snprintf(p, sizeof p, "%s/%s", root, node);
	mkdir(p, 0755);
	snprintf(p, sizeof p, "%s/%s/%s", root, node, name);
	FILE *f = fopen(p, "w");
	fputs(val, f);
	fclose(f);
}

static void ck(const char *what, const char *node, bool want_ok, int lo, int hi, int want_chg)
{
	char hint[400];
	int pct = -1;
	bool chg = false;
	snprintf(hint, sizeof hint, "%s/%s", root, node);
	bool ok = battery_read(hint, &pct, &chg);
	bool good = ok == want_ok && (!want_ok || (pct >= lo && pct <= hi)) &&
	            (want_chg < 0 || chg == (bool)want_chg);
	printf("  %s %s (ok=%d pct=%d chg=%d)\n", good ? "ok  " : "FAIL", what, ok, pct, chg);
	if (!good) fails++;
}

int main(void)
{
	mkdir("build-native", 0755);
	if (!mkdtemp(root)) { perror("mkdtemp"); return 2; }

	put("a", "capacity", "88\n"); put("a", "status", "Charging\n");
	put("a", "charge_counter", "100\n"); put("a", "charge_full", "3300000\n");
	ck("kernel capacity wins", "a", true, 88, 88, 1);

	put("b", "capacity", "0\n"); put("b", "status", "Discharging\n");
	put("b", "charge_counter", "2904000\n"); put("b", "charge_full", "3300000\n");
	ck("capacity 0, charge counters say 88", "b", true, 88, 88, 0);

	put("c", "capacity", "0\n"); put("c", "energy_now", "9000000\n"); put("c", "energy_full", "12000000\n");
	ck("capacity 0, energy counters say 75", "c", true, 75, 75, -1);

	put("d", "capacity", "0\n"); put("d", "voltage_now", "3900000\n");
	ck("capacity 0, no counters: voltage 3.9 V", "d", true, 60, 78, -1);

	put("e", "capacity", "0\n"); put("e", "voltage_now", "3250000\n");
	ck("capacity 0 and an empty cell stays 0", "e", true, 0, 0, -1);

	put("f", "voltage_now", "4150000\n"); put("f", "status", "Full\n");
	ck("no capacity file at all: voltage", "f", true, 90, 100, 1);

	put("other", "type", "Battery\n"); put("other", "capacity", "55\n");
	put("usb", "type", "USB\n"); put("usb", "online", "1\n");
	ck("named node missing: the Battery sibling", "gone", true, 55, 55, -1);

	{
		char hint[400]; int p; bool c;
		snprintf(hint, sizeof hint, "%s/nosuchdir/nobattery", root);
		bool ok = battery_read(hint, &p, &c);
		printf("  %s no node anywhere: false (ok=%d)\n", ok ? "FAIL" : "ok  ", ok);
		if (ok) fails++;
	}

	setenv("TORTOS_FAKE_BATT", "42c", 1);
	ck("TORTOS_FAKE_BATT still overrides", "a", true, 42, 42, 1);

	{ char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", root); if (system(cmd)) {} }
	printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
	return fails != 0;
}
