/* SPDX-License-Identifier: MIT */
#include "syncthing.h"

#if SYNC_ROWS
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "db.h"
#include "platform.h"
#include "wifi.h"

bool st_on(void)
{
	return db_get_int(db_dev(), "syncthing", 0) != 0;
}

/* plorpOS/syncthing.sh start | stop. It backgrounds Syncthing and the Saves
 * setup, so it returns at once. */
static bool run_script(bool start)
{
	char script[256];
	pid_t pid;

	snprintf(script, sizeof script, "%s/syncthing.sh", P_ROOT);
	if ((pid = fork()) == 0) {
		execl("/bin/sh", "sh", script, start ? "start" : "stop", (char *)NULL);
		_exit(127);
	}
	if (pid > 0) waitpid(pid, NULL, 0);
	return pid > 0;
}

/* Saved first, so a reboot agrees with the switch even if the script fails. */
bool st_set(bool on)
{
	if (!db_set_int(db_dev(), "syncthing", on ? 1 : 0)) return false;
	return run_script(on);
}

/* The GKD's Syncthing was ROCKNIX's (plorpos-gkd.42) until plorpOS shipped its
 * own: a switch left on there becomes this one. */
void st_boot(void)
{
#ifdef PLATFORM_GKD
	if (wifi_rocknix_syncthing_handover()) db_set_int(db_dev(), "syncthing", 1);
#endif
	if (st_on()) run_script(true);
}

#else

bool st_on(void) { return false; }
bool st_set(bool on) { (void)on; return false; }
void st_boot(void) {}

#endif
