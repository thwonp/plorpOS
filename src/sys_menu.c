/* SPDX-License-Identifier: MIT */
/* What the two MENU-button menus contain. See src/sys_menu.h for why this is
 * a file of its own, and docs/menus.md for the rules the rows follow.
 *
 * No SDL, no device reads, no forks. Everything this needs arrives in sys_ui. */
#include <stdio.h>

#include "sys_menu.h"
#include "shaderlist.h"

void sys_menu_auto_off_label(int seconds, char *out, size_t n)
{
	if (seconds <= 0)       snprintf(out, n, "never");
	else if (seconds < 120) snprintf(out, n, "%ds", seconds);
	else                    snprintf(out, n, "%dm", seconds / 60);
}

/* Three rows say the same thing when the radio is down, and they say it in
 * the value column rather than by vanishing: a row that disappears when it
 * cannot be used teaches nobody why. */
#define NEEDS_WIFI(on) ((on) ? NULL : "needs Wi-Fi")

const char *sys_menu_src_label(muse_src s)
{
	static const char *const L[MSRC_COUNT] = {
		"Local Music", "Local Audiobooks", "Plex Music", "Jellyfin Music", "Audiobookshelf", "Podcasts"
	};

	return s >= 0 && s < MSRC_COUNT ? L[s] : L[0];
}

int sys_menu_muse_rows(bool books, muse_src src, bool dlq, sm_muse_row *out)
{
	bool srv = src == MSRC_PLEX || src == MSRC_JELLYFIN;
	bool remote = srv || src == MSRC_ABS || src == MSRC_PODCASTS;
	int n = 0;

	out[n++] = SMM_COUNT;
	(void)books;
	out[n++] = SMM_SOURCE;
	if (src != MSRC_PODCASTS) out[n++] = SMM_SORT;   /* episodes: newest first, always */
#if !defined(PLATFORM_MIYOO)   /* all three rows are screen-off behaviour, unused on the Flip (f1p.58) */
	out[n++] = SMM_SETTINGS;
#endif
	out[n++] = SMM_UI;
	if (!remote) out[n++] = SMM_LOCAL;    /* Album Art, Rescan Folder; a server's Resync is in its own */
	if (srv) out[n++] = SMM_SRV;
	if (src == MSRC_ABS) out[n++] = SMM_ABS;
	if (src == MSRC_PODCASTS) out[n++] = SMM_POD;
	if (dlq && remote) out[n++] = SMM_DLQ;
	return n;
}

int sys_menu_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                   const char **heading)
{
	bool net = u->wifi == WIFI_CONNECTED;

	if (u->games) {
		snprintf(b->a, sizeof b->a, "%d", u->game_count);
		snprintf(b->b, sizeof b->b, "%s", u->sys_core ? u->sys_core : "");
		*heading = u->sys_name;
		/* A shelf of favorites gets the two rows that are true of it and
		 * nothing else - not even the enum's order, since Core sits between
		 * them and it has none. See SM_FAV_ROWS. */
		if (u->fav) {
			out[0] = (menu_row){ "Sort By", u->sort ? u->sort : "Name", true };
			return SM_FAV_ROWS;
		}
		if (u->muse) {
			sm_muse_row ids[SM_MUSE_ROWS];
			int k, n = sys_menu_muse_rows(u->muse_books, u->muse_src, u->muse_dlq != NULL, ids);

			for (k = 0; k < n; k++)
				switch (ids[k]) {
				case SMM_COUNT:
					out[k] = (menu_row){ u->muse_src == MSRC_PODCASTS ? "Episodes"
					                     : u->muse_books ? "Books" : "Albums",
					                     b->a, false };
					break;
				case SMM_SORT:
					/* The artist list has one order, so the row says it and
					 * is grayed; the shelf under a chosen artist sorts. */
					out[k] = (menu_row){ "Sort By",
					                     u->muse_list || !u->sort
					                     ? (u->muse_books ? "Author" : "Artist") : u->sort,
					                     !u->muse_list };
					break;
				case SMM_UI:
					out[k] = (menu_row){ "UI Settings", NULL, true };
					break;
				case SMM_LOCAL:
					out[k] = (menu_row){ u->muse_src == MSRC_BOOKS ? "Local Books Settings"
					                                               : "Local Music Settings", NULL, true };
					break;
				case SMM_SOURCE:
					out[k] = (menu_row){ "Source", sys_menu_src_label(u->muse_src), !u->muse_src_only };
					break;
				case SMM_SRV:
					out[k] = (menu_row){ u->muse_src == MSRC_JELLYFIN ? "Jellyfin Settings"
					                                                  : "Plex Settings", NULL, true };
					break;
				case SMM_ABS:
					out[k] = (menu_row){ "Audiobookshelf Settings", NULL, true };
					break;
				case SMM_POD:
					out[k] = (menu_row){ "Podcast Settings", NULL, true };
					break;
				case SMM_DLQ:
					out[k] = (menu_row){ "Download Queue", u->muse_dlq, true };
					break;
				case SMM_SETTINGS:
					out[k] = (menu_row){ "Playback Settings", NULL, true };
					break;
				}
			return n;
		}
		out[SM_SORT]    = (menu_row){ "Sort By",
		                              u->sort ? u->sort : "Name", true  };
		/* How this shelf's games play until one has its own, and the reset
		 * of every game's own: a level down each (plorpos-bn9.3). */
		snprintf(b->h, sizeof b->h, "%s Defaults", u->sys_name ? u->sys_name : "Shelf");
		out[SM_DEFAULTS] = (menu_row){ b->h, NULL, true };
		out[SM_RESET]   = (menu_row){ "Reset Overrides", NULL, true };
		/* Just this system. Needs the network like its counterpart in
		 * Settings > Scraping, and says so rather than opening a screen that
		 * can only report the same thing. */
		out[SM_BOXART]  = (menu_row){ "Scrape Shelf",  NEEDS_WIFI(net), net };
		/* Live now that there is something behind it. It rescans the whole
		 * card rather than this one folder - the work is three directory reads
		 * and the shared parts (hiding a system that emptied, rebuilding
		 * Favorites) have to run anyway - but the folder you are standing in
		 * is the one you came here to refresh, so the name still describes
		 * what you asked for. */
		out[SM_RESCAN]  = (menu_row){ "Rescan Folder", NULL,        true  };
		if (!u->overrides) {   /* no game here plays in diatom: nothing to reset */
			out[SM_RESET]  = out[SM_BOXART];
			out[SM_BOXART] = out[SM_RESCAN];
			return SM_ROWS - 1;
		}
		return SM_ROWS;
	}

	*heading = "plorpOS";
	/* The network's name, not its address. A settings row should say what the
	 * setting IS; the address is a fact about the machine and lives on the
	 * About page with the other ones. */
	if (net && u->ssid && u->ssid[0])
		snprintf(b->b, sizeof b->b, "%s", u->ssid);
	else
		snprintf(b->b, sizeof b->b, "%s",
		         u->wifi == WIFI_CONNECTING ? "connecting" :
		         u->wifi == WIFI_IDLE       ? "not connected" : "off");
	/* Wi-Fi is the radio and its networks; what the network is for - SSH,
	 * Syncthing, Tailscale, Over The Hare, updates - is Network Services,
	 * its own row since plorpos-eiz (owner). */
	out[PM_WIFI]         = (menu_row){ "Wi-Fi", b->b, true };
#if SYS_HAS_BT
	out[PM_BT]           = (menu_row){ "Bluetooth",
	                                   u->bt_name ? u->bt_name : "not connected",
	                                   true  };
#endif
	out[PM_NET]          = (menu_row){ "Network Services", NULL, true };
	out[PM_STATS]        = (menu_row){ "Play Time",  NULL,      true  };
	out[PM_SYSTEM]       = (menu_row){ "System Settings", NULL, true  };
	out[PM_UI]           = (menu_row){ "UI Settings", NULL,     true  };
#if defined(PLATFORM_RGDS)
	out[PM_DUAL]         = (menu_row){ "Dual Screen Settings", NULL, true };
#endif
	/* How games start, stop and rewind (plorpos-pyz). */
	out[PM_GAME]         = (menu_row){ "Game Settings", NULL,   true  };
	/* What every button does, per screen. Needs nothing of the device, which
	 * is the point: it is the page you reach when the thing you have forgotten
	 * is which button opens Muse. See src/controls.h. */
	out[PM_CONTROLS]     = (menu_row){ "Controls",    NULL,   true  };
	out[PM_ABOUT]        = (menu_row){ "About",       NULL,   true  };
	return PM_ROWS;
}

const char *sys_menu_sc_label(int a)
{
	static const char *const label[SC_COUNT] = { "Muse+", "Switcher", "off" };

	return label[a >= 0 && a < SC_COUNT ? a : SC_MUSE];
}

int sys_menu_system_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                          const char **heading)
{
	*heading = "System Settings";
	/* NextUI's two sleep rows, same meaning: Auto Sleep is its "Screen
	 * timeout" (idle until light sleep - screen off, CPU awake), Suspend
	 * Timeout its "Suspend timeout" (how long light sleep waits unwoken
	 * before real suspend, whether a tap or idle began it). Auto Off is
	 * TortOS's own: idle until a full shutdown, resume-into-game putting you
	 * back. It and Auto Sleep are mutually exclusive - main.c's
	 * ST_SLEEP/ST_AUTO_OFF handling - so at most one of those two labels is
	 * ever a real interval, and why the two sit together, Auto Off on top. */
	/* Date & Time leads: setup, where the timers are settings (upstream
	 * b3b4483, which put it in the top menu; ours is regrouped - z0d.1). */
	out[ST_CLOCK]    = (menu_row){ "Date & Time", u->clock, true };
	/* Where the system's sound goes - not Diatom's, which is why the label says
	 * neither "game" nor "emulator": the audiobook and music player will read
	 * the same setting. Diatom's ADR-0029, and src/audioout.c for the rule.
	 *
	 * Auto names what it resolved to, because a row that reads only "Auto"
	 * makes the player guess which of three places they are about to hear. */
	if (u->audio_policy == AOUT_AUTO)
		snprintf(b->d, sizeof b->d, "auto (%s)", aout_dest_name(u->audio_dest));
	else
		snprintf(b->d, sizeof b->d, "%s", aout_dest_name(u->audio_dest));
	out[ST_AUDIO]    = (menu_row){ "Audio Output", b->d, true };
#if SYS_HAS_VLIM
	out[ST_VOLLIM]   = (menu_row){ "Volume Limiter", NULL, true };
#endif
#if !defined(PLATFORM_ROTATE)
	sys_menu_auto_off_label(u->auto_poweroff, b->a, sizeof b->a);
#if defined(PLATFORM_MIYOO)
	/* The Flip has no suspend: how long a sleeping unit waits before it powers
	 * off, whatever started the sleep (f1p.56). */
	out[ST_AUTO_OFF] = (menu_row){ "Sleep Poweroff",  b->a, true };
	/* The hall sensor can read closed with the lid open beside a magnet (f1p.61), so
	 * the lid's sleep can be turned off; the power button and Auto Sleep stay. */
	out[ST_LID]      = (menu_row){ "Lid Close",       u->lid_sleep ? "sleep" : "nothing", true };
#else
	out[ST_AUTO_OFF] = (menu_row){ "Auto Off",        b->a, true };
#endif
#endif
	sys_menu_auto_off_label(u->auto_off, b->c, sizeof b->c);
	out[ST_SLEEP]    = (menu_row){ "Auto Sleep",      b->c, true };
	/* plorpos-gkd.86.3: the percentage in a disc, top right, on every
	 * screen but the game's. Off, the disc is only the low-battery dot. */
	out[ST_BATTPCT]  = (menu_row){ "Battery Percentage", u->batt_pct ? "on" : "off", true };
	/* plorpos-bn9.12: SELECT where it opened Muse+ - Muse+, the game switcher,
	 * or nothing. */
	out[ST_SELECT]   = (menu_row){ "Select Shortcut", sys_menu_sc_label(u->sc_select), true };
#if defined(PLATFORM_ROTATE)
	{
		/* In SWC_ / SWO_ order (platform.h). Opening always wakes a sleeping
		 * unit, so there is no "nothing" for it (plorpos-5eq.1). */
		static const char *const closed[] = { "nothing", "sleep", "open Muse+" };
		static const char *const open[] = { "wake only", "main shelf", "recents", "resume last game" };

		out[ST_SW_CLOSED] = (menu_row){ "Rotate (Closed)", closed[u->sw_closed % 3u], true };
		out[ST_SW_OPEN]   = (menu_row){ "Rotate (Open)",   open[u->sw_open % 4u], true };
		/* Off, Rotate (Open) is done as written with a game running too:
		 * "main shelf" ends the game (plorpos-5eq.23). */
		out[ST_SW_GAME]   = (menu_row){ "Rotate Open Always Resumes Game",
		                                u->sw_game ? "on" : "off", true };
	}
#endif
	/* TortOS-ib9: what the side switch does. Button Lock is an iPod's hold
	 * switch, and only while music plays with the screen off - music_dark.
	 * "muse" in the value says so (TortOS-mhw). */
#if SYS_HAS_MUTESW
	out[ST_MUTESW]   = (menu_row){ "Mute Switch",
	                               u->mute_lock ? "muse button lock" : "mute", true };
#endif
#if defined(PLATFORM_RGDS) || defined(PLATFORM_OAKMOSS)
	/* plorpos-rgds.11: a root shell for any computer on the cable, so off
	 * until asked for. The oakMOSS boards can only give their OTG port back to
	 * gamepads and DACs at the next boot (plorpos-auv.12), and says so. */
	out[ST_ADB]      = (menu_row){ "USB Debugging",
	                               u->adb ? "on" : u->adb_restart ? "off · restart needed" : "off",
	                               true };
#endif
	return ST_ROWS;
}

int sys_menu_vollimit_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                            const char **heading)
{
	*heading = "Volume Limiter";
	(void)u; (void)out; (void)b;
#if VL_HAS_BT
	snprintf(b->a, sizeof b->a, "%d%%", u->vlim[AOUT_BT]);
	out[VL_BT]    = (menu_row){ "Bluetooth Limit", b->a, true };
#endif
#if VL_HAS_DAC
	snprintf(b->c, sizeof b->c, "%d%%", u->vlim[AOUT_USB]);
	out[VL_DAC]   = (menu_row){ "DAC Limit", b->c, true };
#endif
#if VL_HAS_WIRED
	snprintf(b->d, sizeof b->d, "%d%%", u->vlim[AOUT_WIRED]);
	out[VL_WIRED] = (menu_row){ "3.5mm Limit", b->d, true };
#endif
	return VL_ROWS;
}

int sys_menu_ui_build(const sys_ui *u, menu_row *out, const char **heading)
{
	*heading = "UI Settings";
	/* All three change how the shelf looks and nothing about what is on it
	 * - System Order moves the cards, it does not add or drop any. Text
	 * Size stood here until the band it offered turned out to be too narrow
	 * to matter - src/ui.c. */
	out[US_THEME] = (menu_row){ "UI Theme",     u->cards,     true };
	out[US_DIR]   = (menu_row){ "Gamelist Layout", u->gamelist, true };
	out[US_ORDER] = (menu_row){ "System Order", u->sys_order, true };
	out[US_FONTS]    = (menu_row){ "Fonts", NULL, true };
	out[US_PORTS]    = (menu_row){ "Ports Tile",
	                               u->ports_hidden ? "Hidden" : "Visible", true };
	return u->has_ports ? US_ROWS : US_PORTS;
}

int sys_menu_fonts_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                         const char **heading)
{
	*heading = "Fonts";
	snprintf(b->a, sizeof b->a, "%d%%", u->head_size);
	snprintf(b->c, sizeof b->c, "%d%%", u->body_size);
	out[UF_HEADFONT] = (menu_row){ "Header Font", u->head_font, true };
	out[UF_HEADSIZE] = (menu_row){ "Header Size", b->a,         true };
	out[UF_FONT]     = (menu_row){ "Body Font",   u->font,      true };
	out[UF_BODYSIZE] = (menu_row){ "Body Size",   b->c,         true };
	return UF_ROWS;
}

int sys_menu_dual_build(const sys_ui *u, menu_row *out, const char **heading)
{
	*heading = "Dual Screen Settings";
	/* What the second panel shows on the shelf. It has no off here: its
	 * brightness goes down to off (plorpos-rgds.30). */
	out[DU_FUNCTION]  = (menu_row){ "Secondary Function", u->dual_muse ? "Muse+" : "Details", true };
	out[DU_PRIMARY]   = (menu_row){ "Primary Screen", u->dual_bottom ? "Bottom" : "Top", true };
	return DU_ROWS;
}

int sys_menu_defaults_build(const sys_ui *u, menu_row *out, sd_row *ids, menu_bufs *b,
                            const char **heading)
{
	int n = 0;

	snprintf(b->h, sizeof b->h, "%s Defaults", u->sys_name ? u->sys_name : "Shelf");
	snprintf(b->b, sizeof b->b, "%s", u->sys_core ? u->sys_core : "");
	*heading = b->h;
	/* A fact on a shelf with one core; PICO-8's engine, or the default of a
	 * shelf with more, where there is a choice. */
	ids[n] = SD_CORE;    out[n++] = (menu_row){ "Core", u->engine ? u->engine : b->b, u->engine != NULL };
	ids[n] = SD_DISPLAY; out[n++] = (menu_row){ u->nds ? "Layout" : "Display Mode", u->dmode, true };
	/* The DS's Display, only where the panel has more than one. */
	if (u->fill) { ids[n] = SD_FILL; out[n++] = (menu_row){ "Display", u->fill, true }; }
#if defined(PLATFORM_RGDS)
	/* A DS game always has both panels here, each screen filling its
	 * own: no layout or fit to pick (plorpos-rgds.18.1). */
	if (u->nds) {
		out[1] = (menu_row){ "Layout",  "both screens", false };
		if (u->fill) out[2] = (menu_row){ "Display", "full panel", false };
	}
#endif
	ids[n] = SD_SHADER;  out[n++] = (menu_row){ SHADER_LABEL, SHADER_VALUE(u->shader ? u->shader : "None"),
	                                            u->shader_live };
	if (u->overrides) { ids[n] = SD_HOTKEYS; out[n++] = (menu_row){ "Hotkeys", NULL, true }; }
	if (u->palette)   { ids[n] = SD_PALETTE; out[n++] = (menu_row){ "Palette", u->palette, true }; }
	if (u->overrides) { ids[n] = SD_TURBO;   out[n++] = (menu_row){ "Turbo Assignments", NULL, true }; }
	return n;
}

int sys_menu_reset_build(const sys_ui *u, const int *count, menu_row *out, sr_row *ids,
                         menu_bufs *b, const char **heading)
{
	static const char *const L[SR_ROWS] = {
		"Core", "Display Mode", SHADER_LABEL, "Hotkeys", "Palette", "Turbo Assignments"
	};
	/* A kind that can never be set here is left out, unless a card from
	 * another build or an older one left rows of it behind. */
	bool can[SR_ROWS] = { u->core_game, true, u->shader_live, true, u->palette != NULL, true };
	int n = 0, k;

	*heading = "Reset Overrides";
	for (k = 0; k < SR_ROWS; k++) {
		if (!can[k] && !count[k]) continue;
		snprintf(b->n[k], sizeof b->n[k], "%d", count[k]);
		ids[n] = (sr_row)k;
		out[n++] = (menu_row){ L[k], b->n[k], count[k] > 0 };
	}
	return n;
}

int sys_menu_scraping_build(const sys_ui *u, menu_row *out, const char **heading)
{
	bool net = u->wifi == WIFI_CONNECTED;

	*heading = "Scraping";
	/* Opens a checklist of systems (plorpos-bn9.2). */
	out[SC_BOXART] = (menu_row){ "Scrape Systems", NEEDS_WIFI(net), net };
	/* An account, named by the service it belongs to, reading "sign in"
	 * until there is one - as Cheevos does in the menu above.
	 *
	 * Dead when the build has no developer key, because then there is nothing
	 * to sign into - and saying so is better than a row that opens a keyboard
	 * and refuses whatever is typed into it. */
	out[SC_SS]     = (menu_row){ "ScreenScraper",
	                             !u->ss_have ? "not in this build"
	                             : u->ss_in ? u->ss_name : "sign in",
	                             u->ss_have };
	/* Reads Roms/<system>/gamelist.xml off the card: no network, no account. */
	out[SC_IMPORT] = (menu_row){ "Import gamelist.xml metadata", NULL, true };
	return SC_ROWS;
}

int sys_menu_scrape_pick_build(const char *const *names, const bool *on, int n,
                               menu_row *out, const char **heading)
{
	bool all = n > 0;
	int i, k = 0;

	*heading = "Scrape Systems";
	for (i = 0; i < n; i++) all = all && on[i];
	out[k++] = (menu_row){ "All", all ? "on" : "off", n > 0 };
	for (i = 0; i < n; i++) out[k++] = (menu_row){ names[i], on[i] ? "on" : "off", true };
	if (!n) out[k++] = (menu_row){ "No systems with games", NULL, false };
	out[k++] = MENU_RULE;
	out[k++] = MENU_NOTE("START: scrape");
	return k;
}
