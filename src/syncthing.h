/* SPDX-License-Identifier: MIT */
/* Syncthing on every plorpOS port with Wi-Fi (plorpos-5jh): plorpOS's own
 * build, started and stopped by plorpOS/syncthing.sh, which shares the card's
 * Saves and nothing else. The Network Services switch is the "syncthing"
 * setting, and the launcher starts it at boot when the switch is on
 * (st_boot). The same ports as Tailscale: every one with Wi-Fi but the Miyoo
 * Mini Flip, for now. */
#ifndef TORTOS_SYNCTHING_H
#define TORTOS_SYNCTHING_H

#include <stdbool.h>

#include "tailscale.h"

#define SYNC_ROWS TS_ROWS

bool st_on(void);
/* Saved first, then started or stopped; false if either failed. */
bool st_set(bool on);
void st_boot(void);

#endif
