/* SPDX-License-Identifier: MIT */
/* The two menus behind the MENU button, as rows rather than as pixels.
 *
 * MENU has two menus behind it, chosen by where it was pressed. From the
 * systems row it is about the firmware. From inside a system it is about THAT
 * system, because a menu that repeated the firmware's settings while a shelf
 * of NES games sat behind it would be answering a question nobody asked.
 *
 * Split out of main.c under ADR-0001 so that what these menus CONTAIN can be
 * checked on a build machine. Nothing here may include SDL: tools/menu-check.c
 * links this file without it, and that is the whole point.
 *
 * The caller reads the device and fills a sys_ui; this file turns that into
 * rows. Wi-Fi state in particular is asked for by the caller because asking
 * costs a fork - see menu_wifi in main.c - and this file must not care. */
#ifndef SYS_MENU_H
#define SYS_MENU_H

#include <stdbool.h>
#include <stddef.h>

#include "menu.h"
#include "config.h"
#include "wifi.h"
#include "audioout.h"

/* Bluetooth: none on the Flip (f1p.57) or the Zero 28 (no antenna, plorpos-08c). */
#if !defined(PLATFORM_MIYOO) && !defined(PLATFORM_ZERO28)
#define SYS_HAS_BT 1
#else
#define SYS_HAS_BT 0
#endif

/* The plorpOS menu, in the order it is read.
 *
 * Play Time leads because it is the only row here anyone opens twice. Wi-Fi,
 * Bluetooth and Audio Output are setup: you use them when something is wrong
 * or new, and then never again. A menu ordered by what a device needs on its
 * first day puts the thing you actually come back to five rows down. */
/* Controls and About are the two rows you only read, so they sit together at
 * the end, after everything that changes something.
 *
 * The rest are grouped a level down since plorpos-z0d.1: the timers under
 * System Settings, the look under UI Settings, and Cheevos and Over The Hare
 * on the Network Services screen with the other things that need the network. */
/* The order (owner, plorpos-eiz): Play Time, Wi-Fi - its own row now, the
 * radio and the networks - Bluetooth, Network Services (what the network is
 * for), System Settings (Audio Output is in there), UI Settings, Game
 * Settings, Controls, About. */
typedef enum {
	PM_STATS,
	PM_WIFI,
#if SYS_HAS_BT
	PM_BT,
#endif
	PM_NET,
	PM_SYSTEM, PM_UI,
#if defined(PLATFORM_RGDS)
	PM_DUAL,                    /* Dual Screen Settings (plorpos-rgds.18.1) */
#endif
	PM_GAME,
	PM_CONTROLS, PM_ABOUT, PM_ROWS
} pm_row;

/* A side mute switch: the Bricks only. The GKD, the H700s (the Rotate
 * defines PLATFORM_H700 too) and the oakMOSS boards (Brick code, no switch) have
 * none; Playback Settings' Sleep Button Lock is a plain software toggle there. */
#if !defined(PLATFORM_GKD) && !defined(PLATFORM_H700) && !defined(PLATFORM_OAKMOSS)
#define SYS_HAS_MUTESW 1
#else
#define SYS_HAS_MUTESW 0
#endif

/* System Settings > Volume Limiter (plorpos-bn9.9): a limit for each output
 * the port can tell apart - Bluetooth where there is a radio, a USB DAC, and
 * the 3.5 mm jack where its switch is read. The Flip has none of the three
 * (no row at all there), the Rotate no jack. The speaker has no limit. */
#define VL_HAS_BT SYS_HAS_BT
#if !defined(PLATFORM_MIYOO)
#define VL_HAS_DAC 1
#else
#define VL_HAS_DAC 0
#endif
#if !defined(PLATFORM_MIYOO) && !defined(PLATFORM_ROTATE)
#define VL_HAS_WIRED 1
#else
#define VL_HAS_WIRED 0
#endif
#define SYS_HAS_VLIM (VL_HAS_BT || VL_HAS_DAC || VL_HAS_WIRED)
typedef enum {
#if VL_HAS_BT
	VL_BT,
#endif
#if VL_HAS_DAC
	VL_DAC,
#endif
#if VL_HAS_WIRED
	VL_WIRED,
#endif
	VL_ROWS
} vl_row;

/* What SELECT opens where it opened Muse+ (Select Shortcut, plorpos-bn9.12),
 * and a Menu hold (Menu Hold, .13): db "shortcut.select" / "shortcut.hold". */
typedef enum { SC_MUSE, SC_SWITCHER, SC_OFF, SC_COUNT } sc_action;

/* Settings > System Settings: the clock, the two timers, the battery
 * indicator, and the side switch where there is one. */
typedef enum {
	ST_CLOCK,
	ST_AUDIO,                   /* Audio Output, from the top menu (plorpos-eiz) */
#if SYS_HAS_VLIM
	ST_VOLLIM,                  /* a door: Volume Limiter (plorpos-bn9.9) */
#endif
#if defined(PLATFORM_MIYOO)     /* Flip: Sleep Poweroff sits below Auto Sleep (f1p.56) */
	ST_SLEEP, ST_AUTO_OFF, ST_LID,
#else
#if !defined(PLATFORM_ROTATE)   /* sleep and suspend do it there (plorpos-3yv.100) */
	ST_AUTO_OFF,
#endif
	ST_SLEEP,                   /* Battery Percentage: UI Settings (plorpos-bn9.11) */
#endif
	ST_SELECT,                  /* Select Shortcut (plorpos-bn9.12) */
	ST_HOLD,                    /* Menu Hold (plorpos-bn9.13) */
#if defined(PLATFORM_ROTATE)
	ST_SW_CLOSED, ST_SW_OPEN,   /* what a turn of the swivel does (plorpos-5eq.1) */
	ST_SW_GAME,                 /* opening never leaves a running game (5eq.23) */
#endif
#if SYS_HAS_MUTESW
	ST_MUTESW,
#endif
#if defined(PLATFORM_RGDS) || defined(PLATFORM_OAKMOSS)
	ST_ADB,                     /* adb over USB (plorpos-rgds.11, auv.12) */
#endif
	ST_ROWS
} st_row;

/* Settings > UI Settings: how the shelves look, and nothing about what is on
 * them. */
typedef enum { US_THEME, US_DIR, US_ORDER, US_FONTS /* a door: Fonts */,
       US_BATTPCT /* from System Settings (plorpos-bn9.11) */,
       US_TILES /* a door: Visible Tiles, was Ports Tile (plorpos-bn9.10) */,
       US_ROWS } us_row;

/* UI Settings > Fonts (plorpos-bn9.6): each face with its size under it. */
typedef enum { UF_HEADFONT, UF_HEADSIZE, UF_FONT, UF_BODYSIZE, UF_ROWS } uf_row;

/* Settings > Scraping (TortOS-mh0): everything that puts art and text on a
 * card, in one place - the Box Art job, the ScreenScraper account it signs in
 * with, and importing gamelist.xml metadata already on the card. */
typedef enum { SC_BOXART, SC_SS, SC_IMPORT, SC_ROWS } sc_row;

/* The system menu (plorpos-bn9.3): what this shelf is and does. Everything
 * about how its games play moved a level down - the shelf's defaults under
 * "<Shelf> Defaults", every game's own under Reset Overrides - so the MENU
 * button on a shelf is system options only and a game's own settings live in
 * its X menu (Game Overrides). Scrape Shelf was Box Art. */
typedef enum {
	SM_SORT, SM_DEFAULTS,
	SM_RESET, /* Reset Overrides: only on a shelf diatom plays */
	SM_BOXART, SM_RESCAN,
	SM_ROWS
} sm_row;

/* SM_RESET is drawn only on a shelf diatom plays - see sys_ui.overrides - so
 * on Ports the rows below it sit one higher. The build and the key handler
 * both go through these two, so they cannot disagree about which row is where. */
static inline int sm_index(bool reset, sm_row r) { return !reset && r > SM_RESET ? (int)r - 1 : (int)r; }
static inline sm_row sm_row_at(bool reset, int i) { return (sm_row)(!reset && i >= SM_RESET ? i + 1 : i); }

/* <Shelf> Defaults (plorpos-bn9.3): what a game on this shelf plays with until
 * it has its own. Fill is the DS's Display, shown where the panel has more
 * than one; Palette a Game Boy shelf's; Hotkeys and Turbo Assignments only
 * where diatom plays. sys_menu_defaults_build says which rows are there. */
typedef enum { SD_CORE, SD_DISPLAY, SD_FILL, SD_SHADER, SD_HOTKEYS, SD_PALETTE, SD_TURBO,
               SD_ROWS } sd_row;

/* Reset Overrides (plorpos-bn9.3): one row per kind of per-game setting,
 * each with how many games on the shelf have one. Display covers the DS's
 * fill and swap too, Hotkeys the Button Mapping. */
typedef enum { SR_CORE, SR_DISPLAY, SR_SHADER, SR_HOTKEYS, SR_PALETTE, SR_TURBO,
               SR_ROWS,
               SR_ALL = SR_ROWS   /* Reset All, the top row (plorpos-bn9.24): every kind at once */
} sr_row;
#define SR_MENU_ROWS (SR_ROWS + 1)

/* What the Favorites shelf's menu is, which is two of those rows.
 *
 * Favorites is not a system: build_favorites_shelf memsets one and fills in a
 * name, a tag, a card and an accent, leaving no core, no folder and no
 * extensions. Four of the six rows have nothing to work with - Core is blank,
 * Box Art and Rescan have no folder to scrape or sweep, and Display Mode is
 * worse than blank, because launch() resolves a game's CORE through its owner
 * and its display mode through the shelf it was started from. Setting one here
 * would give a game a different aspect depending on which shelf launched it.
 *
 * Sort By is true of a shelf of favorites, so that is what it gets (Eric's
 * call, 2026-09-17; the Games count row went from every shelf menu,
 * plorpos-9jo - the shelf itself shows the count). */
#define SM_FAV_ROWS 1

/* What Muse's shelf menu is: how many albums - Sort By - by artist or by album, its own
 * two orders, where a console's shelf has four - Album Art to fetch covers for
 * the albums that have none or only a small one, and Rescan Folder for music
 * copied onto the card while the Brick was on - and Playback Settings, Muse's own
 * options (TortOS-28l). Muse has no core, no ROM folder and no display mode. Album order was Eric's, 2026-09-19.
 *
 * On books the count is Books, Sort By is by author or title, and Album Art is
 * not there: MusicBrainz knows records. So the rows move, and which one is where is sys_menu_muse_rows's to say. */
/* The order (owner, plorpos-935): the count, Source and Sort By, then Playback
 * Settings, UI Settings (Tab Settings, Now-Playing Layout, Visible Sources)
 * and the Source's own settings - a card Source's Album Art and Rescan Folder
 * are behind Local Music Settings / Local Books Settings (SMM_LOCAL). */
typedef enum { SMM_COUNT, SMM_SOURCE, SMM_SORT, SMM_SETTINGS, SMM_UI, SMM_LOCAL,
               SMM_SRV, SMM_ABS, SMM_POD, SMM_DLQ,
               SMM_HOTKEYS /* always last, from Playback Settings (plorpos-bn9.14) */
} sm_muse_row;
#define SM_MUSE_ROWS 8
/* Source (plorpos-9rf.2): what Muse's shelf holds, one of four. It took over
 * Show (music or books) and Local/Plex. On Plex and Audiobookshelf, Rescan
 * Folder goes (Resync Library is in the source's own settings, which follow
 * Source), and Album Art is music on the card only. */
/* Podcasts (plorpos-7ni) is the fifth: no Sort By (episodes are newest
 * first), no Rescan Folder (Check for Updates is in Podcast Settings).
 * Jellyfin (plorpos-vgr) is a music server as Plex is, its rows Plex's; its
 * Settings row is SMM_SRV, as Plex's. The values are not kept anywhere
 * ("muse.src" holds names), so the order is the Source list's. */
typedef enum { MSRC_MUSIC, MSRC_BOOKS, MSRC_PLEX, MSRC_JELLYFIN, MSRC_ABS, MSRC_PODCASTS,
               MSRC_COUNT } muse_src;
const char *sys_menu_src_label(muse_src s);
/* No Display row: the coverflow is one of Muse's tabs (plorpos-era.8). */

/* Muse's rows in order, for the kind its shelf shows and its Source. Returns
 * how many. The build and the key handler both ask this, so the
 * two cannot disagree about what row 2 is. */
/* `dlq`: the source has downloads on their way, so Download Queue follows its
 * settings row (owner, plorpos-ps8.8). */
int sys_menu_muse_rows(bool books, muse_src src, bool dlq, sm_muse_row *out);

#define MENU_MAX_ROWS 17

/* The array every caller declares must hold every row a build can produce, and
 * on 2026-09-16 it stopped: PM_ROWS went to 13 against a cap of 12 when the
 * ScreenScraper row landed, so sys_menu_build wrote one past the end of the
 * caller's stack array. The launcher aborted on its first frame and
 * tools/menu-check.c passed, because it overflows identically and nothing
 * there was watching that memory.
 *
 * A number that has to be kept in step with an enum, by hand, in another file.
 * Now it is the compiler's job.
 *
 * _Static_assert and not #if: PM_ROWS is an enum constant, and the
 * preprocessor reads an identifier it does not know as 0 - so `#if
 * MENU_MAX_ROWS < PM_ROWS` is `13 < 0`, false forever, a guard that compiles
 * and protects nothing. Written that way first, and only caught by putting the
 * cap back to 12 and watching the build succeed. */
_Static_assert(MENU_MAX_ROWS >= PM_ROWS, "MENU_MAX_ROWS < PM_ROWS");
_Static_assert(MENU_MAX_ROWS >= SM_ROWS, "MENU_MAX_ROWS < SM_ROWS");
_Static_assert(MENU_MAX_ROWS >= SC_ROWS, "MENU_MAX_ROWS < SC_ROWS");
_Static_assert(MENU_MAX_ROWS >= ST_ROWS, "MENU_MAX_ROWS < ST_ROWS");

/* Where the built rows' text lives. A row holds pointers, not copies, so the
 * strings a build formats have to outlive the build; the caller owns this and
 * keeps it alive as long as it keeps the rows. */
typedef struct { char a[24], b[CFG_STR], c[16], d[40];
                 char h[CFG_STR + 16];   /* "<Shelf> Defaults" (plorpos-bn9.3) */
                 char n[SR_ROWS][12];    /* Reset Overrides' counts */
} menu_bufs;

/* Everything either menu needs to know about the device, gathered by the
 * caller. A struct rather than a dozen arguments so that adding a fact to a
 * row does not change every call site, and so a check can state a device's
 * whole situation in one initializer. */
typedef struct {
	bool games;              /* the system menu, rather than TortOS's own */
	bool fav;                /* and that shelf is Favorites; see SM_FAV_ROWS */
	bool muse;               /* or Muse's; see SM_MUSE_ROWS */
	bool muse_books;         /* and it shows books rather than music */
	muse_src muse_src;       /* and the Source chosen (plorpos-9rf.2) */
	bool muse_src_only;      /* it is the one Visible Source: nothing to choose (owner) */
	const char *muse_dlq;    /* its downloads on their way ("3 albums"), NULL none */
	bool muse_list;          /* and the artist list itself is on screen: always by artist */

	wifi_state  wifi;        /* already cached by the caller; see menu_wifi */
	const char *ssid;        /* the network's name when connected, else NULL */

	/* The plorpOS menu */
	/* The ScreenScraper account, which needs one more fact than the
	 * RetroAchievements one: whether this build can reach them at all. The
	 * developer key comes from the environment at build time and a build
	 * without it cannot sign anybody in, so the row has three states rather
	 * than two - see src/ss.h and BACKLOG 31. */
	bool        ss_have;     /* this build carries a developer key */
	bool        ss_in;
	const char *ss_name;     /* only read when ss_in */
	const char *cards;       /* the showing card set's name, from CARD_SETS */
	const char *gamelist;    /* how game shelves draw, from GAMELISTS */
	const char *font;        /* the body face's name (plorpos-8m1.3) */
	const char *head_font;   /* the headings' face's name (plorpos-3yv.60) */
	int         head_size, body_size; /* their sizes, percent (plorpos-bn9.6) */
	const char *sys_order;   /* the systems shelf's order, from SYS_ORDERS */
	int         auto_off;    /* Auto Sleep, seconds, 0 for off */
	int         auto_poweroff; /* Auto Off, seconds, 0 for off - mutually
	                             * exclusive with auto_off, see PM_AUTO_OFF */
	const char *clock;       /* now, as Date & Time's row shows it */
	bool        mute_lock;   /* the mute switch is a button lock instead */
	bool        batt_pct;    /* the battery's percentage, top right (plorpos-gkd.86.3) */
	bool        lid_sleep;   /* Flip: closing the lid sleeps (f1p.61) */
	int         sw_closed, sw_open; /* Rotate: the swivel's rows (5eq.1), SWC_ / SWO_ */
	bool        sw_game;     /* Rotate: opening always resumes a running game (5eq.23) */
	bool        adb;         /* RG DS, Zero 40: adb over USB is on (plorpos-rgds.11) */
	bool        adb_restart; /* Zero 40: turned off, still on until a restart (auv.12) */
	/* RG DS, Dual Screen Settings (plorpos-rgds.18.1): the second panel
	 * shows Muse rather than Details; the launcher and single-screen games
	 * are on the bottom panel. A DS game uses both panels whatever these
	 * say. The second panel is off at brightness 0 (plorpos-rgds.30). */
	bool        dual_muse, dual_bottom;
	/* Where sound goes: the policy the player set, and where it actually ends
	 * up under that policy. Both, because the row has to name a place - "Auto"
	 * on its own is a rule, not somewhere you can hear. */
	aout_policy audio_policy;
	aout_dest   audio_dest;
	int         vlim[AOUT_USB + 1]; /* Volume Limiter, percent, by aout_dest (bn9.9) */
	int         sc_select;   /* Select Shortcut, an sc_action (bn9.12) */
	int         sc_hold;     /* Menu Hold, an sc_action (bn9.13) */

	/* Bluetooth: the name of the connected headset, or NULL. The row used to
	 * read "not yet" and be dead, which was true of the pairing screen and
	 * false of the feature - game audio has gone to a headset since
	 * 2026-09-03. */
	const char *bt_name;

	/* The system menu */
	const char *sys_name;
	const char *sys_core;
	/* PICO-8's shelf: the engine's label; a shelf with two cores (main.c
	 * ALT_CORES): the shelf's default core. Shown in Core's place and turned over there.
	 * NULL on every other shelf, whose Core is a fact. */
	const char *engine;
	int         game_count;
	const char *dmode;
	/* The DS's shelf: its layout row is Layout, and fill is the Display row's
	 * value - "Aspect", "Stretch" or "Integer" - or NULL where they all draw
	 * the same, as on the Brick (plorpos-6ei.13, .17). */
	bool        nds;
	const char *palette;     /* a Game Boy shelf's Default Palette; NULL elsewhere */
	bool        overrides;   /* diatom plays this shelf: Reset Overrides, Hotkeys and Turbo are listed */
	bool        core_game;   /* a game here can pick its own core (main.c ALT_CORES; not PICO-8) */
	const char *fill;
	/* The system's shader name (plorpos-8xu), the in-game Shader row's choice,
	 * and whether choosing one means anything: not without a list, and not
	 * on PICO-8's shelf running native, which never goes through Diatom. */
	const char *shader;
	bool        shader_live;
	/* The label of the shelf's sort order. A label rather than an index for
	 * the same reason dmode is one: this file must not know the table, or a
	 * check that links it would have to link the table too. */
	const char *sort;
} sys_ui;

/* Seconds to the label a row shows, in NextUI's own spelling (its
 * settings.cpp labels): seconds up to 90s, whole minutes from 2m. Pure, and
 * here rather than in main.c so the check can hold it to "never", "90s" and
 * "2m" without a device. */
void sys_menu_auto_off_label(int seconds, char *out, size_t n);

/* Build whichever menu u->games calls for. Returns the row count, so the input
 * loop never needs to know which of the two it is driving. */
int sys_menu_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                   const char **heading);

/* <Shelf> Defaults' rows; ids[i] is the sd_row that row i shows. Returns how many. */
int sys_menu_defaults_build(const sys_ui *u, menu_row *out, sd_row *ids, menu_bufs *b,
                            const char **heading);

/* Reset Overrides' rows, `count` games per kind (indexed by sr_row). A kind
 * that cannot apply here and has nothing to clear is not listed; one at 0 is
 * greyed. Reset All leads, greyed when nothing is set (plorpos-bn9.24).
 * ids[i] is the sr_row that row i shows, SR_ALL for Reset All. Returns how
 * many; out and ids hold SR_MENU_ROWS. */
int sys_menu_reset_build(const sys_ui *u, const int *count, menu_row *out, sr_row *ids,
                         menu_bufs *b, const char **heading);

/* Settings > Scraping's rows, from the same sys_ui. Returns SC_ROWS. */
int sys_menu_scraping_build(const sys_ui *u, menu_row *out, const char **heading);

/* Scraping > Scrape Systems (plorpos-bn9.2): "All", then one row per system
 * with games, `on` saying which are checked, then a note for START. Rows go
 * from SP_FIRST; returns the count, at most n + 3. */
enum { SP_ALL, SP_FIRST };
int sys_menu_scrape_pick_build(const char *const *names, const bool *on, int n,
                               menu_row *out, const char **heading);

/* An sc_action as its row reads: "Muse+", "Switcher", "off". */
const char *sys_menu_sc_label(int a);

/* Settings > System Settings. Returns ST_ROWS. */
int sys_menu_system_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                          const char **heading);

/* System Settings > Volume Limiter (plorpos-bn9.9). Returns VL_ROWS; the
 * percentages are written into b. */
int sys_menu_vollimit_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                            const char **heading);

/* Settings > UI Settings. Returns US_ROWS. */
int sys_menu_ui_build(const sys_ui *u, menu_row *out, const char **heading);

/* UI Settings > Visible Tiles (plorpos-bn9.10): a row a shelf, on or off,
 * in the order given (Ports, Muse+, then the systems). Returns n. */
int sys_menu_tiles_build(const char *const *names, const bool *on, int n,
                         menu_row *out, const char **heading);

/* UI Settings > Fonts. Returns UF_ROWS; the sizes are written into b. */
int sys_menu_fonts_build(const sys_ui *u, menu_row *out, menu_bufs *b,
                         const char **heading);

/* Settings > Dual Screen Settings (RG DS). Returns DU_ROWS. */
typedef enum { DU_FUNCTION, DU_PRIMARY, DU_ROWS } du_row;
int sys_menu_dual_build(const sys_ui *u, menu_row *out, const char **heading);

#endif
