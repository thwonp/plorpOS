/* SPDX-License-Identifier: MIT */
/* The Anbernic RG Rotate (plorpos-3yv): Unisoc T618, a 720x720 swivel panel
 * with touch, on Armbian's Debian trixie from the card (mainline 7.2 kernel,
 * Mesa panfrost). Built with PLATFORM_H700 as well as PLATFORM_ROTATE: every
 * H700 branch in the shared code is "a Linux handheld that is not a Brick"
 * (Menu as the brightness modifier, no mute switch, Diatom's audio, the
 * PICO-8 pad mapping), and PLATFORM_ROTATE marks where this one differs.
 *
 * The session is the GKD's shape (sd/rotate): sway on the panel, the launcher
 * and Diatom as Wayland clients of it, PipeWire for the sound (PLAT_PIPEWIRE).
 * So the window, the hand-overs and the volume follow platform_gkd.c. sway
 * turns the picture when the swivel closes and maps touch to match
 * (sd/rotate/sway.conf), so nothing here rotates.
 *
 * Every input is a discrete GPIO key under mainline gpio-keys: the d-pad is
 * four buttons, not a hat. The nodes are found by name, as their numbers
 * follow probe order. Read straight from evdev, as on the GKD; sway's own
 * keyboard handling is off. */
#include "atomic.h"
#include "db.h"
#include "platform.h"
#include "audioout.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "platform_dev.h"
#include "ui.h"

/* The card's exFAT data partition is mounted here (plorpos-3yv.7), so the
 * Brick's spelling holds. */
const char *P_ROOT = "/mnt/SDCARD/TortOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/rotate";
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
const char *P_WEB = "/mnt/SDCARD/TortOS/res/web";
const char *P_FONT = "/mnt/SDCARD/TortOS/menu.ttf";

/* What a game's process is told about the device - see build_child_env(). */
const char *const plat_child_env[] = {
	"PLATFORM=rotate", "DEVICE=rgrotate",
	"SDCARD_PATH=/mnt/SDCARD",
	"BIOS_PATH=/mnt/SDCARD/Bios",
	"CHEATS_PATH=/mnt/SDCARD/Cheats",
	"SAVES_PATH=/mnt/SDCARD/Saves",
	NULL
};
/* SDL2 and FFmpeg are Debian's, in the system paths. */
const char plat_child_libpath[] = "";
/* Neither, as on the GKD: Debian's SDL has sensors, PipeWire quiets PICO-8
 * (child_quiet) and follows the output, and Debian's wget is GNU, with TLS. */
const char *const plat_pico8_preload = NULL;
const char *const plat_pico8_path = NULL;

bool plat_is_brick_pro(void) { return false; }
bool plat_two_sticks(void) { return false; }
const char *plat_bright_keys(void) { return "Menu+Vol"; }

/* gpio-keys-gamepad: the buttons, the d-pad as BTN_DPAD_*, and the side
 * "reset" key (BTN_THUMBR, a plain GPIO - .14). gpio-keys-system: POWER, the
 * volume keys and the swivel's hall switch (SW_LID, 1 closed). PROVISIONAL
 * map by evdev name; every button is pressed and logged in .6. */
#define PAD_NAME    "gpio-keys-gamepad"
#define SYS_NAME    "gpio-keys-system"
#define CODE_MENU   BTN_MODE
static const struct { int code; in_button b; } padmap[] = {
	{ BTN_SOUTH, IN_ACCEPT }, { BTN_EAST, IN_BACK }, { BTN_NORTH, IN_X }, { BTN_WEST, IN_Y },
	{ BTN_TL, IN_L1 }, { BTN_TR, IN_R1 }, { BTN_TL2, IN_L2 }, { BTN_TR2, IN_R2 },
	{ BTN_SELECT, IN_SELECT }, { BTN_START, IN_START },
	{ BTN_DPAD_LEFT, IN_LEFT }, { BTN_DPAD_RIGHT, IN_RIGHT },
	{ BTN_DPAD_UP, IN_UP }, { BTN_DPAD_DOWN, IN_DOWN },
};
/* The same buttons for native PICO-8's SDL. SDL numbers the present codes in
 * order, measured on the device (2026-10-17, every key injected): A b0, B b1,
 * X b2, Y b3, L1 b4, R1 b5, L2 b6, R2 b7, Select b8, Start b9, Menu b10 (the
 * launcher's, left out), side key b11 (Muse's), d-pad up/down/left/right
 * b12-b15. x/y positional, as on the H700. No name CRC: SDL matches it
 * whatever the name. */
const char plat_pico8_pad[] =
	"SDL_GAMECONTROLLERCONFIG=19000000010000000100000000010000,gpio-keys-gamepad,"
	"a:b0,b:b1,y:b2,x:b3,leftshoulder:b4,rightshoulder:b5,back:b8,start:b9,"
	"lefttrigger:b6,righttrigger:b7,"
	"dpup:b12,dpdown:b13,dpleft:b14,dpright:b15,platform:Linux,";

static SDL_Window *win;
static SDL_Renderer *ren;
static int fd_pad = -1;
/* Menu is a modifier as well as a button, as on the H700: held with another
 * button it goes down with it, with a volume key it is spent on brightness,
 * and alone it is a press on release (menu_pulse: up on the next poll). */
static bool menu_down, menu_sent, menu_spent, menu_pulse;

SDL_Renderer *plat_renderer(void) { return ren; }

/* An input node by its name, or -1. */
static int open_by_name(const char *want)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	int found = -1;

	if (!d) return -1;
	while (found < 0 && (e = readdir(d)) != NULL) {
		char path[300], name[128] = "";
		int fd;

		if (strncmp(e->d_name, "event", 5) != 0) continue;
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) continue;
		if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) >= 0 && strcmp(name, want) == 0)
			found = fd;
		else
			close(fd);
	}
	closedir(d);
	return found;
}

/* ---- video -----------------------------------------------------------
 *
 * A fullscreen Wayland window under sway, 720x720 whichever way the swivel
 * is: a turn is sway's output transform, and a square output keeps its size,
 * so the window is never resized. */

bool plat_video_init(void)
{
	int w = 0, h = 0;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	/* At the output's own size: 0x0 leaves SDL's Wayland backend a 1x1
	 * window that fullscreen never grows (platform_gkd.c). */
	{
		SDL_DisplayMode dm = { 0 };
		if (SDL_GetDesktopDisplayMode(0, &dm) != 0) { dm.w = 720; dm.h = 720; }
		win = SDL_CreateWindow("plorpOS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		                       dm.w, dm.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
	}
	if (!win) {
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return false;
	}
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren) {
		fprintf(stderr, "renderer: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	/* sway sizes the window in its first configure, after SDL_CreateWindow
	 * returns. Bounded, as on the GKD. */
	for (int i = 0; i < 100; i++) {
		SDL_PumpEvents();
		SDL_GetRendererOutputSize(ren, &w, &h);
		if (w > 1 && h > 1) break;
		SDL_Delay(10);
	}
	plat_geometry_init(ren);
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(ren, &info) == 0)
		fprintf(stderr, "renderer: %s, driver: %s, output %dx%d, vsync %s, render-to-texture %s\n",
		        info.name, SDL_GetCurrentVideoDriver(), w, h,
		        (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "granted" : "DECLINED (requested)",
		        (info.flags & SDL_RENDERER_TARGETTEXTURE) ? "available" : "MISSING");
	return true;
}

/* sway takes the frozen child off the screen (child_hide), so the menu over
 * it is an ordinary window. */
bool plat_video_init_over_child(void) { return plat_video_init(); }

static bool resident_away_pending;
static void resident_place(bool on_screen);

void plat_present(SDL_Renderer *r)
{
	ui_hit_frame();   /* a tap now means what this frame shows */
	SDL_RenderPresent(r);
	/* The menu's first frame is up: only now does the paused game go
	 * (screen_yield). */
	if (resident_away_pending) {
		resident_away_pending = false;
		resident_place(false);
	}
}

void plat_video_quit(void)
{
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

/* The hand-over with the resident, in the order that never shows black
 * (plorpos-3yv.25). A paused Diatom keeps its window mapped, its last frame
 * on it (port/gkd.c, PARK_KEEP), and sway will not reliably raise a newly
 * shown window over a fullscreen one - measured: the menu was open but
 * hidden behind the game 2 times in 3. So each side is put on screen by
 * name, and only once it has something to show:
 *
 *   pause:  the launcher's window is shown and draws its first frame
 *           underneath, THEN the game goes to the hidden workspace
 *           (plat_present) - the menu is there complete the instant it is.
 *   resume: the game comes back on top, still showing its paused frame,
 *           THEN the launcher's window goes.
 *
 * Hiding first left sway's black background up for the 12-29 ms until the
 * launcher's first buffer. At a game's start and exit there is no resident
 * window to move; the move fails silently and nothing else changes. */
void screen_yield(bool to_game)
{
	if (!win) return;
	if (to_game) {
		resident_away_pending = false;
		resident_place(true);
		SDL_HideWindow(win);
		SDL_PumpEvents();
	} else {
		SDL_ShowWindow(win);
		resident_away_pending = true;
	}
}

/* ---- input: raw evdev, no SDL joystick --------------------------------- */

bool plat_input_init(void)
{
	if (fd_power < 0) fd_power = open_by_name(SYS_NAME);
	if (fd_pad < 0) fd_pad = open_by_name(PAD_NAME);
	if (fd_pad < 0) fprintf(stderr, "input: no %s\n", PAD_NAME);
	return fd_pad >= 0;
}

#define TOUCH_SWIPE_PX  60     /* farther than this is a swipe: the first step */
#define TOUCH_STEP_PX   200    /* then each this much farther, one more */
#define TOUCH_HOLD_MS   500
#define FLICK_MIN_PX    200    /* a flick has come this far: shorter is a nudge */
#define FLICK_MIN_PXMS  3.0f   /* lifted moving this fast (px/ms): it coasts */
#define FLICK_FRESH_MS  60     /* ... if it was still moving this recently */
#define FLICK_WINDOW_MS 80     /* speed = travel over the last this long */
#define FLICK_SPAN_MS   20     /* and over no less: two reports 1 ms apart lie */
#define FLICK_TAU_MS    350.0f /* the coast slows by e every this long */
#define FLICK_STOP_PXMS 2.0f   /* and stops while still brisk, not trickling */
#define FLICK_STEP_PX   120    /* a step per this much coast */

static struct {
	int    fingers, most;      /* down now, and the most at once */
	float  x0, y0, x, y;       /* the first finger, in panel pixels */
	unsigned t0;
	bool   held;               /* the long press already fired */
	int    axis;               /* -1 until the first step, then 0 x / 1 y */
	float  anchor;             /* where along it the last step was taken */
	int    steps;              /* taken by this touch */
	float  hp[8];              /* the last positions along the axis ... */
	unsigned ht[8];            /* ... and when; the newest is hn-1, mod 8 */
	int    hn;
	bool   caught;             /* this touch stopped a coast: not a tap */
	bool   coasting;
	float  cv, cpos;           /* coast speed, and distance since its last step */
	unsigned ct;
	in_button queue[32];       /* a tap on a far item is many presses */
	int    qn;
	in_button up;              /* pressed last poll, released this one ... */
	bool   pulsing;            /* ... if this is set */
} touch;

void plat_input_flush(void)
{
	struct input_event ev;
	SDL_Event e;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	menu_down = menu_sent = menu_spent = menu_pulse = false;
	memset(&touch, 0, sizeof touch);
}

void plat_input_quit(void) { }

int menu_key(int *code)
{
	*code = CODE_MENU;
	return fd_pad;
}

/* The volume keys are on the system node with POWER, which plat_run reads. */
int levels_fd(void) { return -1; }

bool levels_alt(void)
{
	unsigned char bits[KEY_MAX / 8 + 1] = { 0 };

	return fd_pad >= 0 && ioctl(fd_pad, EVIOCGKEY(sizeof bits), bits) >= 0 &&
	       (bits[CODE_MENU / 8] >> (CODE_MENU % 8) & 1);
}

int levels_key(int code, bool *bright)
{
	*bright = levels_alt();
	if (code == KEY_VOLUMEUP)   return +1;
	if (code == KEY_VOLUMEDOWN) return -1;
	return 0;
}

/* A helper to completion - swaymsg, pactl - with its chatter kept out of
 * the log. true when it ran and said 0. */
static bool helper(char *const argv[])
{
	extern char **environ;
	posix_spawn_file_actions_t fa;
	pid_t pid;
	int st = 0;
	bool ok;

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
	ok = posix_spawnp(&pid, argv[0], &fa, NULL, argv, environ) == 0 &&
	     waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0;
	posix_spawn_file_actions_destroy(&fa);
	return ok;
}

/* The frozen child goes to a workspace of its own, where it is not drawn,
 * and the pad is grabbed so its read does not queue the menu's presses: the
 * GKD's way, and its reasons (platform_gkd.c). */
#define HIDE_WS "plorpos-hidden"

/* The resident's window on screen (on top, fullscreen) or off it. */
static void resident_place(bool on_screen)
{
	pid_t pid = diatom_pid();
	char crit[96];
	char *cmd[] = { "swaymsg", crit, NULL };

	if (pid <= 0) return;
	snprintf(crit, sizeof crit, on_screen
	         ? "[pid=%d] move container to workspace 1, focus, fullscreen enable"
	         : "[pid=%d] move container to workspace " HIDE_WS, (int)pid);
	helper(cmd);
}

bool child_hide(pid_t pid)
{
	char crit[80];
	char *move[] = { "swaymsg", crit, NULL };

	snprintf(crit, sizeof crit, "[pid=%d] move container to workspace " HIDE_WS,
	         (int)pid);
	if (!helper(move)) {
		fprintf(stderr, "run: sway would not hide the child\n");
		return false;
	}
	if (fd_pad >= 0 && ioctl(fd_pad, EVIOCGRAB, 1) != 0)
		fprintf(stderr, "run: pad grab failed: %s\n", strerror(errno));
	return true;
}

void child_restore(pid_t pid, bool show)
{
	char crit[80];
	char *cmd[] = { "swaymsg", crit, NULL };

	if (fd_pad >= 0) ioctl(fd_pad, EVIOCGRAB, 0);
	if (!show) return;
	snprintf(crit, sizeof crit,
	         "[pid=%d] move container to workspace 1, focus, fullscreen enable",
	         (int)pid);
	if (!helper(cmd)) fprintf(stderr, "run: sway would not show the child\n");
}

/* The child's own PipeWire stream, found by its pid, muted while Muse plays:
 * platform_gkd.c's, looked for at most twice a second. */
static int quiet_stream(pid_t pid)
{
	char line[256], want[48];
	int cur = -1, idx = -1;
	FILE *f = popen("pactl list sink-inputs 2>/dev/null", "r");

	if (!f) return -1;
	snprintf(want, sizeof want, "application.process.id = \"%d\"", (int)pid);
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "Sink Input #", 12)) cur = atoi(line + 12);
		else if (idx < 0 && strstr(line, want)) idx = cur;
	}
	pclose(f);
	return idx;
}

void child_quiet(pid_t pid, bool on)
{
	static pid_t q_pid;
	static int q_idx = -1, q_said = -1;
	static unsigned q_look_at;
	char idx[16];
	char *cmd[] = { "pactl", "set-sink-input-mute", idx, on ? "1" : "0", NULL };

	if (pid != q_pid) { q_pid = pid; q_idx = -1; q_said = -1; q_look_at = 0; }
	if (q_said == (int)on) return;
	if (q_idx < 0) {
		if (q_look_at && plat_now_ms() - q_look_at < 500) return;
		q_look_at = plat_now_ms() | 1;
		if ((q_idx = quiet_stream(pid)) < 0) return;
	}
	snprintf(idx, sizeof idx, "%d", q_idx);
	if (helper(cmd)) {
		q_said = on;
		fprintf(stderr, "run: child %s\n", on ? "quiet" : "heard");
	} else {
		q_idx = -1;   /* the stream went: look again */
	}
}

static void pad_read(in_state *st)
{
	struct input_event ev;

	if (menu_pulse) { set_btn(st, IN_MENU, false); menu_pulse = false; }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type != EV_KEY || ev.value == 2) continue;
		bool down = ev.value == 1;

		if (ev.code == CODE_MENU) {
			menu_down = down;
			if (down) { menu_sent = menu_spent = false; continue; }
			if (menu_sent) set_btn(st, IN_MENU, false);
			else if (!menu_spent) { set_btn(st, IN_MENU, true); menu_pulse = true; }
			menu_sent = false;
			continue;
		}
		if (down && menu_down && !menu_sent && !menu_spent) {
			set_btn(st, IN_MENU, true);
			menu_sent = true;
		}
		for (size_t k = 0; k < sizeof padmap / sizeof padmap[0]; k++)
			if (padmap[k].code == ev.code) set_btn(st, padmap[k].b, down);
	}
}

/* POWER and the volume keys, on the system node. A volume press goes to the
 * volume or, Menu held, the brightness; a release lets go of both. */
static void sys_read(in_state *st)
{
	struct input_event ev;

	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type != EV_KEY || ev.value == 2) continue;
		bool down = ev.value == 1;
		in_button vol = IN_NONE, bright = IN_NONE;

		if (ev.code == KEY_POWER) { set_btn(st, IN_POWER, down); continue; }
		if (ev.code == KEY_VOLUMEUP)   { vol = IN_VOLUP; bright = IN_BRIGHTUP; }
		if (ev.code == KEY_VOLUMEDOWN) { vol = IN_VOLDN; bright = IN_BRIGHTDN; }
		if (vol == IN_NONE) continue;
		if (down && menu_down) menu_spent = true;
		if (down) set_btn(st, menu_down ? bright : vol, true);
		else { set_btn(st, vol, false); set_btn(st, bright, false); }
	}
}

/* ---- touch: gestures as buttons (plorpos-3yv.10) ------------------------
 *
 * The panel's touches arrive as SDL finger events, already turned with the
 * swivel by sway (map_to_output), and become the buttons every screen
 * already answers to - so closed, with the face buttons hidden, the whole
 * shelf, every menu and Muse are still in reach:
 *
 *   tap = select what is under it, A on the selected item (touch_at), drag
 *   = d-pad, flick = d-pad that coasts on, two-finger tap = B, long press =
 *   Y on what is under it (favorite a game, Muse's mode, a rescan; fired
 *   while held - plorpos-3yv.22), three-finger tap = Menu. Start has nothing
 *   to do on the shelf and X (a game's info) is a button's job, open.
 *
 * A drag steps WHILE the finger moves (plorpos-3yv.21): the first step at
 * TOUCH_SWIPE_PX, which also locks the axis, then one per TOUCH_STEP_PX -
 * spaced so a short deliberate swipe is one tile. A flick - FLICK_MIN_PX or
 * more, lifted still moving fast - coasts: steps keep coming at FLICK_STEP_PX
 * of a speed that decays, so a harder flick goes further. Distance, not speed,
 * tells a small flick from a big one (measured by hand 2026-10-17: small
 * 97-134 px, big 302-376 px; their speeds overlap). A touch during a coast
 * stops it and is not a tap. Taps are decided when the last finger lifts.
 * Each press is down for one poll and up the next, queued so a run of steps
 * lands one poll apart. */
static void touch_queue(in_button b, int n)
{
	while (n-- > 0 && touch.qn < (int)(sizeof touch.queue / sizeof touch.queue[0]))
		touch.queue[touch.qn++] = b;
}

/* One step along `axis` for a finger travelling `d` (signed) along it. The
 * finger drags the content: a swipe to the left moves on, as the d-pad's
 * right does. */
static void touch_step(int axis, float d)
{
	if (axis == 0) touch_queue(d < 0 ? IN_RIGHT : IN_LEFT, 1);
	else           touch_queue(d < 0 ? IN_DOWN : IN_UP, 1);
	touch.steps++;
}

static void touch_drag(void)
{
	float dx = touch.x - touch.x0, dy = touch.y - touch.y0;
	float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, p, d;
	unsigned now = plat_now_ms();

	if (touch.axis < 0) {
		if ((ax > ay ? ax : ay) <= TOUCH_SWIPE_PX) return;
		touch.axis = ax > ay ? 0 : 1;
		p = touch.axis == 0 ? dx : dy;
		touch.anchor = p < 0 ? -TOUCH_SWIPE_PX : TOUCH_SWIPE_PX;
		touch.hn = 0;
		touch_step(touch.axis, p);
	}
	p = touch.axis == 0 ? dx : dy;
	if (touch.hn == 0 || touch.ht[(touch.hn - 1) & 7] != now) touch.hn++;
	touch.hp[(touch.hn - 1) & 7] = p;
	touch.ht[(touch.hn - 1) & 7] = now;
	while ((d = p - touch.anchor) >= TOUCH_STEP_PX || d <= -TOUCH_STEP_PX) {
		touch.anchor += d < 0 ? -TOUCH_STEP_PX : TOUCH_STEP_PX;
		touch_step(touch.axis, d);
	}
}

/* A tap (then = IN_ACCEPT) or long press (then = IN_Y) where the finger
 * came down, against what the screen showed (ui_hit, plorpos-3yv.23). On an
 * item the tap selects it, and on the selected one it is A; a long press
 * selects it and then is Y. Empty space is nothing, outside a menu's panel a
 * tap dismisses it, and a screen that recorded no items gets `then` as
 * before. */
static void touch_at(in_button then)
{
	float s = plat_scale() > 0 ? plat_scale() : 1;
	int b = 0, n = 0;

	switch (ui_hit_find((int)(touch.x0 / s), (int)(touch.y0 / s), &b, &n)) {
	case UI_HIT_UNMAPPED: touch_queue(then, 1); break;
	case UI_HIT_NONE:     break;
	case UI_HIT_OUTSIDE:  if (then == IN_ACCEPT) touch_queue(IN_BACK, 1); break;
	case UI_HIT_ITEM:
		touch_queue((in_button)b, n);
		if (n == 0 || then != IN_ACCEPT) touch_queue(then, 1);
		break;
	}
}

/* The finger's speed along the axis as it lifts: its travel over the last
 * FLICK_WINDOW_MS, not over the last report alone - one report ~16 ms
 * apart can jump as the finger rolls off the glass, and read a small flick
 * as a fast one. */
static float touch_speed(void)
{
	int last = (touch.hn - 1) & 7, k = last, i;

	for (i = 1; i < 8 && i < touch.hn; i++) {
		int j = (last - i) & 7;
		if (touch.ht[last] - touch.ht[j] > FLICK_WINDOW_MS) break;
		k = j;
	}
	if (touch.ht[last] - touch.ht[k] < FLICK_SPAN_MS) return 0;
	return (touch.hp[last] - touch.hp[k]) / (float)(touch.ht[last] - touch.ht[k]);
}

static void touch_event(const SDL_Event *e)
{
	/* Panel pixels, so the distances above are what a finger travels:
	 * plat_screen_w is in layout units (1024 wide here, at scale 0.70). */
	float w = plat_screen_w * plat_scale(), h = plat_screen_h * plat_scale();

	if (w <= 0 || h <= 0) w = h = 720;

	if (e->type == SDL_FINGERDOWN) {
		if (touch.fingers++ == 0) {
			touch.most = 1;
			touch.x0 = touch.x = e->tfinger.x * w;
			touch.y0 = touch.y = e->tfinger.y * h;
			touch.t0 = plat_now_ms();
			touch.held = false;
			touch.axis = -1;
			touch.steps = 0;
			touch.caught = touch.coasting;
			touch.coasting = false;
		} else if (touch.fingers > touch.most)
			touch.most = touch.fingers;
	} else if (e->type == SDL_FINGERMOTION) {
		if (touch.fingers == 1) {
			touch.x = e->tfinger.x * w; touch.y = e->tfinger.y * h;
			if (touch.most == 1 && !touch.held) touch_drag();
		}
	} else if (e->type == SDL_FINGERUP && touch.fingers > 0) {
		unsigned now = plat_now_ms();
		float sv = 0, v;

		if (--touch.fingers > 0 || touch.held) return;
		if (touch.steps > 0) {
			float d = touch.hp[(touch.hn - 1) & 7];
			if (touch.hn > 0 && now - touch.ht[(touch.hn - 1) & 7] <= FLICK_FRESH_MS &&
			    (d < 0 ? -d : d) >= FLICK_MIN_PX) sv = touch_speed();
			v = sv < 0 ? -sv : sv;
			if (v >= FLICK_MIN_PXMS) {
				touch.coasting = true;
				touch.cv = sv;
				touch.cpos = 0;
				touch.ct = plat_now_ms();
			}
		} else if (touch.most >= 3) touch_queue(IN_MENU, 1);
		else if (touch.most == 2)   touch_queue(IN_BACK, 1);
		else if (!touch.caught)     touch_at(IN_ACCEPT);
	}
}

static void touch_coast(void)
{
	unsigned now = plat_now_ms();
	float dt = (float)(now - touch.ct);

	touch.ct = now;
	if (dt > FLICK_TAU_MS) dt = FLICK_TAU_MS;
	touch.cpos += touch.cv * dt;
	touch.cv -= touch.cv * dt / FLICK_TAU_MS;
	while (touch.cpos >= FLICK_STEP_PX || touch.cpos <= -FLICK_STEP_PX) {
		touch_step(touch.axis, touch.cpos);
		touch.cpos += touch.cpos < 0 ? FLICK_STEP_PX : -FLICK_STEP_PX;
	}
	if ((touch.cv < 0 ? -touch.cv : touch.cv) < FLICK_STOP_PXMS) touch.coasting = false;
}

static void touch_poll(in_state *st)
{
	if (touch.pulsing) { set_btn(st, touch.up, false); touch.pulsing = false; }
	if (touch.coasting) touch_coast();
	if (touch.fingers == 1 && touch.most == 1 && !touch.held && touch.steps == 0 &&
	    plat_now_ms() - touch.t0 >= TOUCH_HOLD_MS) {
		float dx = touch.x - touch.x0, dy = touch.y - touch.y0;
		if (dx * dx + dy * dy <= TOUCH_SWIPE_PX * TOUCH_SWIPE_PX) {
			touch.held = true;
			touch_at(IN_Y);
		}
	}
	if (touch.qn > 0) {
		touch.up = touch.queue[0];
		touch.pulsing = true;
		memmove(touch.queue, touch.queue + 1, --touch.qn * sizeof touch.queue[0]);
		set_btn(st, touch.up, true);
	}
}

void plat_input_poll(in_state *st)
{
	SDL_Event e;

	memset(st->pressed, 0, sizeof st->pressed);
	if (g_terminating) st->quit_requested = true;
	plat_audio_jack_poll();
	while (SDL_PollEvent(&e)) {
		if (e.type == SDL_QUIT) st->quit_requested = true;
		else touch_event(&e);
	}
	pad_read(st);
	sys_read(st);
	touch_poll(st);
}

void plat_leds_off(void) { }

/* ---- volume and brightness --------------------------------------------- */

#define VOL_MAX    PLAT_VOL_MAX
#define BACKLIGHT  "/sys/class/backlight/backlight/brightness"
/* The panel's PWM is 0-2499. The Brick's ladder shape, scaled, until judged
 * on this panel. */
static const unsigned short bright_ladder[] = {
	20, 40, 80, 160, 310, 470, 700, 940, 1250, 1570, 1880, 2499
};
#define BRIGHT_MAX ((int)(sizeof bright_ladder / sizeof bright_ladder[0]) - 1)
_Static_assert(BRIGHT_MAX == PLAT_BRIGHT_MAX, "bright_ladder vs PLAT_BRIGHT_MAX");

/* No mixer device: volume is PipeWire's, so mixer_fd is 0 once the worker
 * below runs - "present", so the nudges run (platform_gkd.c). The codec's
 * own gain (HPL/HPR, which it resets to 4 at every PCM open, and inverted:
 * low is loud) is left at that 4; PipeWire opens the PCM, and the level is
 * the sink's software volume. */
int mixer_fd = -1, disp_fd = -1;
int cur_vol = -1, cur_bright = -1;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void levels_save(void)
{
	db_set_int(db_dev(), "volume", cur_vol);
	db_set_int(db_dev(), "brightness", cur_bright);
	db_write_boot_env();
}

/* wpctl is a process, tens of ms, so sets go to a worker; latest wins.
 * platform_gkd.c's, and diatom's. */
static pthread_mutex_t vol_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  vol_cv = PTHREAD_COND_INITIALIZER;
static int             vol_want = -1;

static void *vol_worker(void *arg)
{
	extern char **environ;
	(void)arg;
	for (;;) {
		char pct[16];
		char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", pct, NULL };
		pid_t pid;
		int st, want;

		pthread_mutex_lock(&vol_mu);
		while (vol_want < 0) pthread_cond_wait(&vol_cv, &vol_mu);
		want = vol_want;
		vol_want = -1;
		pthread_mutex_unlock(&vol_mu);

		snprintf(pct, sizeof pct, "%d%%", want * 100 / VOL_MAX);
		if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
			waitpid(pid, &st, 0);
	}
	return NULL;
}

void apply_volume(int v)
{
	cur_vol = v;
	if (mixer_fd < 0) return;
	pthread_mutex_lock(&vol_mu);
	vol_want = v;
	pthread_cond_signal(&vol_cv);
	pthread_mutex_unlock(&vol_mu);
}

/* What PipeWire is at now, in rungs, once at start. -1 if it cannot say. */
static int wpctl_level(void)
{
	FILE *f = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
	float v;
	int lv = -1;

	if (!f) return -1;
	if (fscanf(f, "Volume: %f", &v) == 1)
		lv = clampi((int)(v * VOL_MAX + 0.5f), 0, VOL_MAX);
	pclose(f);
	return lv;
}

/* The output is the codec's speaker until Bluetooth (.18) and the USB DAC
 * (.19) give PipeWire another sink to route to. */
void plat_audio_speaker_only(bool on) { (void)on; }
bool plat_bt_audio(void) { return false; }
bool plat_usb_audio(void) { return false; }
void plat_sink_follow(bool game) { (void)game; }

/* No headphone sink in the codec's UCM profile (WirePlumber lists the
 * speaker alone). */
bool plat_headphones_present(void) { return false; }
void plat_audio_jack_poll(void) { }
void jack_forget(void) { }

bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }
void mute_forget(void) { }
void plat_mute_switch_lock(bool lock) { (void)lock; }
bool plat_hold_switch(void) { return false; }

static void backlight_write(int raw)
{
	char s[16];
	int n = snprintf(s, sizeof s, "%d", raw);

	if (disp_fd >= 0 && pwrite(disp_fd, s, (size_t)n, 0) < 0)
		fprintf(stderr, "settings: backlight: %s\n", strerror(errno));
}

void apply_brightness(int b)
{
	cur_bright = b;
	backlight_write(bright_ladder[b]);
}

void backlight_off(void) { backlight_write(0); }

/* ---- the side key (BTN_THUMBR): Muse's media key, everywhere -------------
 *
 * The user's gestures (plorpos-3yv.14): a tap plays or pauses, a hold goes
 * back a track, a double tap skips forward. Written as the note a headset's
 * buttons leave (/tmp/tortos_btkey, read by musec_poll on the shelf, in the
 * menus and in a game's tick), so the key reaches Muse wherever Muse matters
 * and never a game. A thread of its own on its own fd: the pad's other
 * reader (pad_read) only runs while the launcher has the screen.
 *
 * A tap waits SIDE_DOUBLE_MS for a second one before it counts: that wait is
 * the price of telling a double tap from two taps. A hold fires while held. */
#define SIDE_DOUBLE_MS 250
#define SIDE_HOLD_MS   500

static void btkey_write(const char *act)
{
	struct timespec t;
	FILE *f = fopen("/tmp/tortos_btkey.new", "w");

	if (!f) return;
	clock_gettime(CLOCK_MONOTONIC, &t);
	fprintf(f, "%s %ld\n", act, (long)t.tv_sec * 1000 + t.tv_nsec / 1000000);
	if (fclose(f) == 0) rename("/tmp/tortos_btkey.new", "/tmp/tortos_btkey");
}

static void *side_worker(void *arg)
{
	int fd = open_by_name(PAD_NAME);
	bool down = false, held = false;
	int taps = 0;                 /* taps waiting for the double-tap window */
	unsigned since = 0;           /* the press, or the last release */

	(void)arg;
	if (fd < 0) return NULL;
	for (;;) {
		struct pollfd p = { fd, POLLIN, 0 };
		int wait = -1;
		unsigned now = plat_now_ms();

		if (down && !held) wait = SIDE_HOLD_MS - (int)(now - since);
		else if (!down && taps) wait = SIDE_DOUBLE_MS - (int)(now - since);
		if (wait < 0 && wait != -1) wait = 0;
		if (poll(&p, 1, wait) > 0) {
			struct input_event ev;
			while (read(fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
				if (ev.type != EV_KEY || ev.code != BTN_THUMBR || ev.value == 2) continue;
				now = plat_now_ms();
				if (ev.value) { down = true; held = false; since = now; continue; }
				if (!down) continue;
				down = false;
				if (held) { held = false; taps = 0; continue; }
				if (++taps == 2) { btkey_write("next"); taps = 0; continue; }
				since = now;
			}
			continue;
		}
		now = plat_now_ms();
		if (down && !held && now - since >= SIDE_HOLD_MS) {
			held = true;
			taps = 0;
			btkey_write("prev");
		} else if (!down && taps && now - since >= SIDE_DOUBLE_MS) {
			taps = 0;
			btkey_write("toggle");
		}
	}
	return NULL;
}

void plat_settings_init(void)
{
	pthread_t t;
	int v, b;

	disp_fd = open(BACKLIGHT, O_RDWR | O_CLOEXEC);
	if (disp_fd < 0) fprintf(stderr, "settings: no %s\n", BACKLIGHT);
	if (pthread_create(&t, NULL, vol_worker, NULL) == 0) {
		pthread_detach(t);
		mixer_fd = 0;
	} else
		fprintf(stderr, "settings: no volume worker\n");
	if (pthread_create(&t, NULL, side_worker, NULL) == 0) pthread_detach(t);

	v = db_get_int(db_dev(), "volume", -1);
	b = db_get_int(db_dev(), "brightness", -1);
	if (v < 0) v = wpctl_level();
	/* The panel's level now, to the nearest rung, with nothing saved. */
	if (b < 0 && disp_fd >= 0) {
		char s[16];
		ssize_t n = pread(disp_fd, s, sizeof s - 1, 0);
		if (n > 0) {
			int raw;
			s[n] = '\0';
			raw = atoi(s);
			b = 0;
			for (int i = 1; i <= BRIGHT_MAX; i++)
				if (abs(bright_ladder[i] - raw) < abs(bright_ladder[b] - raw)) b = i;
		}
	}
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX) : -1;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	if (cur_vol >= 0) apply_volume(cur_vol);
	apply_brightness(cur_bright);
}

/* ---- sleep: deep suspend works upstream (beebono's PM fixes) ------------ */

#define POWER_STATE "/sys/power/state"

bool plat_sleep_supported(void)
{
	char st[64] = "";
	int fd = open(POWER_STATE, O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, st, sizeof st - 1) : -1;

	if (fd >= 0) close(fd);
	if (n > 0) st[n] = '\0';
	return strstr(st, "mem") != NULL && access(POWER_STATE, W_OK) == 0;
}

bool plat_sleep(void)
{
	if (!plat_sleep_supported()) return false;
	sync();
	/* A pending wake source makes the kernel refuse with EBUSY; a few tries
	 * a second apart get past a stray one, as on the H700. */
	for (int tries = 0; tries < 5; tries++) {
		int fd = open(POWER_STATE, O_WRONLY | O_CLOEXEC);
		ssize_t n = fd >= 0 ? write(fd, "mem", 3) : -1;

		if (fd >= 0) close(fd);
		if (n == 3) {
			fprintf(stderr, "sleep: resumed\n");
			return true;
		}
		fprintf(stderr, "sleep: suspend refused: %s\n", strerror(errno));
		if (power_key_within(1000, 1)) return true;
	}
	return false;
}

/* ---- battery ---- */

bool plat_battery(int *pct, bool *charging)
{
	return battery_read("/sys/class/power_supply/sc27xx-fgu", pct, charging);
}
