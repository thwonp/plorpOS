/* SPDX-License-Identifier: MIT */
/* Tailscale (plorpos-8ns): the Network Services switch and what the daemon is
 * doing, for the row and the sign-in screen.
 *
 * The switch is the "tailscale" setting, which boot.env carries to launch.sh
 * so the daemon is back after a reboot; plorpOS/tailscale.sh starts and stops
 * it. The state is read from tailscaled's LocalAPI over its socket - a few
 * milliseconds, so the menu can ask every second without forking the CLI.
 */
#ifndef TORTOS_TAILSCALE_H
#define TORTOS_TAILSCALE_H

#include <stdbool.h>
#include <stddef.h>

#if defined(PLATFORM_ZERO40)
#define TS_ROWS 1
#else
#define TS_ROWS 0
#endif

typedef enum {
	TS_OFF,        /* the switch is off */
	TS_STARTING,   /* on, the daemon not answering or not connected yet */
	TS_LOGIN,      /* waiting for a sign-in: url holds the link, when there is one */
	TS_RUNNING,    /* on the tailnet: ip holds the device's 100.x address */
} ts_state;

bool     ts_on(void);
bool     ts_set(bool on);
ts_state ts_status(char *ip, size_t ipn, char *url, size_t urln);
/* On the tailnet now: safe from any thread, asks the daemon at most every 2 s.
 * Always false where there is no Tailscale. */
bool     ts_connected(void);
/* Whether a server address ("http://host:port", "host:port") is a tailnet one:
 * 100.64.0.0/10, fd7a:115c:a1e0::/48, *.ts.net, or a name that resolves to one
 * of those (MagicDNS). Looks the name up: not on the UI thread. Always false
 * where there is no Tailscale. */
bool     ts_tailnet_addr(const char *address);

#endif
