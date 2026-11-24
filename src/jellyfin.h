/* SPDX-License-Identifier: MIT */
#ifndef TORTOS_JELLYFIN_H
#define TORTOS_JELLYFIN_H

#include <stdbool.h>
#include <stddef.h>

#include "msrv.h"

/* Jellyfin as a Muse source (plorpos-vgr): the server's half of a shared
 * msrv source, as plex.c is Plex's. Muse browses, plays, downloads and keeps
 * playlists through msrv_* on jellyfin_src(); what is Jellyfin's own - the
 * address, signing in, the library - is below. Every call returns at once:
 * the network runs on the source's worker. */

void jellyfin_init(const char *card, const char *shared);
msrv *jellyfin_src(void);

bool jellyfin_signed_in(void);
bool jellyfin_ready(void);            /* signed in, library chosen */
const char *jellyfin_server(void);    /* the address, as typed and checked */
const char *jellyfin_server_name(void);
const char *jellyfin_account(void);
const char *jellyfin_library(void);
void jellyfin_sign_out(void);

/* Each of these: start, then poll each frame. Polls: 0 waiting, 1 done,
 * -1 failed - jellyfin_why() says why. */
void jellyfin_address_set(const char *address);   /* "192.168.0.2:8096" is enough */
void jellyfin_qc_start(void);                     /* Quick Connect */
int  jellyfin_qc_poll(char *code, size_t n);      /* `code`: what to approve, once there */
void jellyfin_qc_cancel(void);
void jellyfin_login(const char *user, const char *password);
int  jellyfin_auth_poll(void);                    /* the address check and the password sign-in */
const char *jellyfin_why(void);

/* The music libraries: ask, then poll (0 waiting, n, -1 none or failed).
 * With two or more, the last is "All music". */
void jellyfin_libraries_ask(void);
int  jellyfin_libraries_poll(void);
const char *jellyfin_library_at(int i);
void jellyfin_library_pick(int i);

#endif
