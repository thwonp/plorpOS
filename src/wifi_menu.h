/* SPDX-License-Identifier: MIT */
/* The Wi-Fi screen's state and its row building, with no SDL and no renderer.
 *
 * Split from main.c so `make check` can ask what the menu contains for a given
 * state - the requirement ADR-0001 rests on. The half that needs a screen
 * (wifi_key, wifi_screen) stays in main.c; this half is answerable offline.
 */
#ifndef TORTOS_WIFI_MENU_H
#define TORTOS_WIFI_MENU_H

#include "menu.h"
#include "ota.h"
#include "syncthing.h"
#include "tailscale.h"
#include "wifi.h"

typedef struct {
	wifi_net nets[WIFI_MAX_NETS];
	char     vals[WIFI_MAX_NETS][32];
	int      n;
	bool     on;
	bool     scanning;   /* a scan is running; the list is provisional */
	bool     scanned;    /* a scan has finished at least once this visit */
	bool     svc[WIFI_NSVC];   /* SSH, Samba; read on entry, only where WIFI_SVC_ROWS */
	bool     st;         /* the Syncthing switch, read on entry where SYNC_ROWS (plorpos-5jh) */
	bool     connected;  /* associated with an address: Over The Hare can run */
	char     conn[WIFI_SSID_MAX + 72]; /* "<ssid> · <ip>" while connected, else empty */
	char     st_ui[72];  /* Syncthing's web UI, "<ip>:8384", while connected */
	bool     services;   /* Network Services' screen, not Wi-Fi's (plorpos-eiz) */
	int      ts;         /* ts_state, asked once a second where TS_ROWS (plorpos-8ns) */
	char     ts_ip[48];  /* the device's tailnet address while TS_RUNNING */
} wifi_ui;

/* Two screens since plorpos-eiz (owner): Wi-Fi - the radio's switch, then the
 * networks, a network at WIFI_TOP_ROWS + i - and Network Services, what the
 * network is for: the services a device lets you switch (WIFI_SVC_ROWS of
 * them, from NS_ROW_SVC), Syncthing (SYNC_ROWS), Tailscale (TS_ROWS), Over
 * The Hare and Check for update (OTA_ROWS). wifi_ui.services says which. */
enum { WIFI_ROW_SWITCH, WIFI_TOP_ROWS };
enum {
	NS_ROW_SVC,
	NS_ROW_ST = NS_ROW_SVC + WIFI_SVC_ROWS,
	NS_ROW_TS = NS_ROW_ST + SYNC_ROWS,
	NS_ROW_XFER = NS_ROW_TS + TS_ROWS,
	NS_ROW_OTA,                 /* Check for update, where OTA_ROWS (plorpos-koo) */
	NS_ROWS = NS_ROW_OTA + OTA_ROWS
};

/* Signal as a word. dBm is the honest number and it is also jargon; the list is
 * sorted strongest first anyway, so the word only has to separate "this will
 * work" from "this will not".
 *
 * The -72 boundary is NOT validated. Measured throughput exists at two points
 * only - unusable at -73, fine at -58 - and the cliff is somewhere in that 15
 * dB. Left where it is rather than moved on a hunch; see BACKLOG. */
const char *wifi_strength(int dbm);

void wifi_label(wifi_ui *w);
void wifi_begin_scan(wifi_ui *w);
/* Fills `rows` and names the screen. The heading is a function of the same
 * state the rows are, so it is produced here rather than being handed to the
 * runner separately and then having to be kept in step with what is below it. */
int  wifi_build(void *ctx, menu_row *rows, int max, const char **heading);

#endif
