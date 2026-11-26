/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_OTA_H
#define TORTOS_OTA_H
#include <stdbool.h>

/* Check for update (plorpos-koo): Network Services finds the newest stable
 * release on GitHub with this port's zip, downloads it, and has
 * plorpOS/ota.sh unpack it; the next boot swaps it in (the launch scripts run
 * `ota.sh apply`). The release rule it relies on is docs/building.md's
 * "Releases": plorpOS-v<VERSION>-<OTA_PORT>.zip and a SHA256SUMS beside it.
 *
 * No drawing here: main.c's update screen polls an ota_job once a frame. */

/* The port's name in the release's zip names. The RG DS also defines
 * PLATFORM_GKD, and the Rotate and the Flip PLATFORM_H700: they go first. */
#if defined(PLATFORM_RGDS)
#define OTA_PORT "rgds"
#elif defined(PLATFORM_GKD)
#define OTA_PORT "gkd"
#elif defined(PLATFORM_ROTATE)
#define OTA_PORT "rotate-update"
#elif defined(PLATFORM_MIYOO)
#define OTA_PORT "miyoominiflip"
#elif defined(PLATFORM_H700)
#define OTA_PORT "h700"
#elif defined(PLATFORM_OAKMOSS)
#define OTA_PORT OAKMOSS_BOARD
#elif defined(PLATFORM_BRICK)
#define OTA_PORT "brick"
#endif

#ifdef OTA_PORT
#define OTA_ROWS 1
#else
#define OTA_ROWS 0
#endif

typedef enum {
	OTA_IDLE,
	OTA_CHECKING,    /* asking GitHub */
	OTA_UPTODATE,    /* nothing newer than this build: "No updates available" */
	OTA_FOUND,       /* ver/size say what; ota_download() to take it */
	OTA_FETCHING,    /* got of size */
	OTA_STAGING,     /* verifying, then unpacking done of total */
	OTA_READY,       /* unpacked; the next boot installs it */
	OTA_ERROR        /* err says which */
} ota_phase;

typedef struct {
	ota_phase phase;
	char      ver[24];        /* the release found */
	long long size;           /* its zip, bytes */
	long long got;            /* downloaded so far */
	bool      verifying;      /* OTA_STAGING: checking the checksum */
	int       done, total;    /* OTA_STAGING: entries unpacked */
	/* OTA_ERROR: "fetch" (network, GitHub), "space" (the card), "checksum",
	 * "unpack" or "failed". */
	char      err[24];
	/* private */
	char      name[96], url[512], sums_url[512], sum[65];
	int       pid, step;
	unsigned  last_ms;
} ota_job;

void ota_check(ota_job *j);      /* -> OTA_CHECKING */
void ota_download(ota_job *j);   /* OTA_FOUND -> OTA_FETCHING, or OTA_ERROR "space" */
void ota_poll(ota_job *j);       /* once a frame: a waitpid and, 4x a second, a stat */
void ota_cancel(ota_job *j);     /* stops a check or a download; the partial file goes */
/* MB of free card the update needs before it is downloaded: the zip, and what
 * it unpacks to (about 2.3 times the zip on every port; 3 to be safe). */
int  ota_need_mb(const ota_job *j);
/* At startup: an update installed at this boot runs, so the previous version
 * it kept (plorpOS.prev) can go. In the background. */
void ota_boot(void);

#endif
