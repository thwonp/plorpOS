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
#include "bt.h"
#include "atomic.h"
#include "db.h"
#include "platform.h"
#include "audioout.h"
#include "musec.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "platform_dev.h"
#include "ui.h"

/* The card's exFAT data partition is mounted here (plorpos-3yv.7), so the
 * Brick's spelling holds. */
const char *P_ROOT = "/mnt/SDCARD/plorpOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/rotate";
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
const char *P_WEB = "/mnt/SDCARD/plorpOS/res/web";
const char *P_FONT = "/mnt/SDCARD/plorpOS/menu.ttf";

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
/* Held alone for SWITCHER_HOLD_MS it is the game switcher instead, and its
 * release is then nothing (plorpos-gl5.2). */
static unsigned menu_t0;
static bool switcher_pulse;

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
static pid_t child_away;   /* a frozen child, off once the menu has drawn */
static void resident_place(bool on_screen);
static bool place(pid_t pid, bool on_screen);

void plat_present(SDL_Renderer *r)
{
	/* sway shares plorpos.service and takes the same SIGTERM, so a present
	 * during shutdown waits on a frame callback from a compositor that has
	 * gone - SDL spins in wl_display_prepare_read_queue until systemd's 90 s
	 * SIGKILL (plorpos-3yv.65: after a game, launch_one's black frame). */
	if (g_terminating) return;
	ui_hit_frame();   /* a tap now means what this frame shows */
	SDL_RenderPresent(r);
	/* The menu's first frame is up: only now does the paused game go
	 * (screen_yield). */
	if (resident_away_pending) {
		resident_away_pending = false;
		resident_place(false);
	}
	if (child_away > 0) {
		if (!place(child_away, false)) fprintf(stderr, "run: sway would not hide the child\n");
		child_away = 0;
	}
}

void plat_window_show(bool on)
{
	if (!win) return;
	if (on) SDL_ShowWindow(win);
	else { SDL_HideWindow(win); SDL_PumpEvents(); }
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
#define TOUCH_EDGE_PX   32     /* a touch starting this close to a side is back */
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
	bool   held;               /* the long press (or the edge's back) fired */
	int    edge;               /* started at the left side +1, right -1, else 0 */
	int    vedge;              /* else at the top +1 (Muse), bottom -1, else 0 */
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
	/* Free drag (plorpos-gl5.5): no steps, the drag read by the screen. */
	bool   free, moved, rel;
	float  lx, ly, vx, vy, rdx, rdy;
	unsigned lt;
} touch;

void plat_touch_free(bool on)
{
	touch.free = on;
	touch.rel = false;
}

bool plat_touch_drag_get(touch_drag_state *d)
{
	memset(d, 0, sizeof *d);
	if (touch.rel) {
		d->released = true;
		d->dx = touch.rdx; d->dy = touch.rdy;
		d->vx = touch.vx;  d->vy = touch.vy;
		touch.rel = false;
		return true;
	}
	if (!touch.free || touch.fingers != 1 || touch.most != 1 || touch.edge ||
	    touch.vedge || touch.held || !touch.moved) return false;
	d->active = true;
	d->dx = touch.x - touch.x0;
	d->dy = touch.y - touch.y0;
	return true;
}

void plat_input_flush(void)
{
	struct input_event ev;
	SDL_Event e;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	menu_sent = menu_pulse = switcher_pulse = false;
	/* Menu still held - the hold that opened the switcher - is spent: its
	 * release must not arrive as a fresh Menu and close what it opened. */
	{
		unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = { 0 };

		menu_down = fd_pad >= 0 && ioctl(fd_pad, EVIOCGKEY(sizeof bits), bits) >= 0 &&
		            (bits[CODE_MENU / (8 * sizeof bits[0])] >> (CODE_MENU % (8 * sizeof bits[0])) & 1);
		menu_spent = menu_down;
	}
	{
		bool keep = touch.free;   /* the screen's ask outlives a flush */

		memset(&touch, 0, sizeof touch);
		touch.free = keep;
	}
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

/* pid's window on screen (on top, fullscreen) or off it, on the hidden
 * workspace. */
static bool place(pid_t pid, bool on_screen)
{
	char crit[96];
	char *cmd[] = { "swaymsg", crit, NULL };

	snprintf(crit, sizeof crit, on_screen
	         ? "[pid=%d] move container to workspace 1, focus, fullscreen enable"
	         : "[pid=%d] move container to workspace " HIDE_WS, (int)pid);
	return helper(cmd);
}

static void resident_place(bool on_screen)
{
	pid_t pid = diatom_pid();

	if (pid > 0) place(pid, on_screen);
}

/* The frozen child's window stays up, its last frame on it, until the menu
 * has drawn its first frame over it (plat_present) - the order screen_yield
 * keeps with the resident, for the same reason (plorpos-3yv.20). */
bool child_hide(pid_t pid)
{
	child_away = pid;
	if (fd_pad >= 0 && ioctl(fd_pad, EVIOCGRAB, 1) != 0)
		fprintf(stderr, "run: pad grab failed: %s\n", strerror(errno));
	return true;
}

/* The child back on top first, still showing its frame, then the launcher's
 * window out of the way - also when it is not coming back (a reset's new
 * child maps next; after a quit run_alone shows the window again). */
void child_restore(pid_t pid, bool show)
{
	child_away = 0;
	if (fd_pad >= 0) ioctl(fd_pad, EVIOCGRAB, 0);
	if (show && !place(pid, true)) fprintf(stderr, "run: sway would not show the child\n");
	plat_window_show(false);
}

/* The child's own PipeWire stream, found by its pid, muted while Muse plays:
 * platform_gkd.c's, looked for at most twice a second. SDL's PipeWire
 * driver (launch.sh) puts no pid on the stream, only on its client, so the
 * stream is the one whose Client: is the child's (plorpos-3yv.18). */
#define QUIET_CLIENTS 4

static int quiet_stream(pid_t pid)
{
	char line[256], want[48];
	int cur = -1, idx = -1, clients[QUIET_CLIENTS], nc = 0, c, i;
	FILE *f = popen("pactl list clients 2>/dev/null", "r");

	if (!f) return -1;
	snprintf(want, sizeof want, "application.process.id = \"%d\"", (int)pid);
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "Client #", 8)) cur = atoi(line + 8);
		else if (cur >= 0 && nc < QUIET_CLIENTS && strstr(line, want)) clients[nc++] = cur;
	}
	pclose(f);
	if (!(f = popen("pactl list sink-inputs 2>/dev/null", "r"))) return -1;
	cur = -1;
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "Sink Input #", 12)) cur = atoi(line + 12);
		else if (idx >= 0) continue;
		else if (strstr(line, want)) idx = cur;
		else if (sscanf(line, " Client: %d", &c) == 1)
			for (i = 0; i < nc; i++) if (clients[i] == c) idx = cur;
	}
	pclose(f);
	return idx;
}

/* On a worker, latest wins: SDL's PipeWire stream is muted by its own
 * client, so a mute sent to a frozen child waits until it is thawed - and
 * plat_run says it before the SIGCONT, which on this thread froze the
 * launcher for pactl's 10 s timeout (plorpos-3yv.18). The worker's pactl
 * lands as the child wakes. Wanted only while plat_run keeps asking: a
 * request older than a second is a run that ended. */
static pthread_mutex_t q_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_cv = PTHREAD_COND_INITIALIZER;
static pid_t           q_want_pid;
static int             q_want = -1;
static unsigned        q_asked_at;

static void *quiet_worker(void *arg)
{
	pid_t pid = 0;
	int said = -1, idx = -1, on;
	char num[16];
	(void)arg;

	for (;;) {
		char *cmd[] = { "pactl", "set-sink-input-mute", num, NULL, NULL };

		pthread_mutex_lock(&q_mu);
		while (q_want < 0 || (q_want_pid == pid && q_want == said))
			pthread_cond_wait(&q_cv, &q_mu);
		if (plat_now_ms() - q_asked_at > 1000) { q_want = -1; pthread_mutex_unlock(&q_mu); continue; }
		if (q_want_pid != pid) { pid = q_want_pid; said = -1; idx = -1; }
		on = q_want;
		pthread_mutex_unlock(&q_mu);

		if (idx < 0 && (idx = quiet_stream(pid)) < 0) { SDL_Delay(500); continue; }
		snprintf(num, sizeof num, "%d", idx);
		cmd[3] = on ? "1" : "0";
		if (helper(cmd)) {
			said = on;
			fprintf(stderr, "run: child %s\n", on ? "quiet" : "heard");
		} else {
			idx = -1;   /* the stream went: look again */
		}
	}
	return NULL;
}

void child_quiet(pid_t pid, bool on)
{
	static bool started;

	pthread_mutex_lock(&q_mu);
	if (!started) {
		pthread_t t;
		if (pthread_create(&t, NULL, quiet_worker, NULL) == 0) pthread_detach(t);
		started = true;
	}
	q_want_pid = pid;
	q_want = on;
	q_asked_at = plat_now_ms();
	pthread_cond_signal(&q_cv);
	pthread_mutex_unlock(&q_mu);
}

static void pad_read(in_state *st)
{
	struct input_event ev;

	if (menu_pulse) { set_btn(st, IN_MENU, false); menu_pulse = false; }
	if (switcher_pulse) { set_btn(st, IN_SWITCHER, false); switcher_pulse = false; }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type != EV_KEY || ev.value == 2) continue;
		bool down = ev.value == 1;

		if (ev.code == CODE_MENU) {
			menu_down = down;
			if (down) { menu_sent = menu_spent = false; menu_t0 = plat_now_ms(); continue; }
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
	if (menu_down && !menu_sent && !menu_spent &&
	    plat_now_ms() - menu_t0 >= SWITCHER_HOLD_MS) {
		menu_spent = true;
		set_btn(st, IN_SWITCHER, true);
		switcher_pulse = true;
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
 *   tap = select what is under it, A on the selected item or on any menu row
 *   (touch_at, plorpos-3yv.56), drag
 *   = d-pad, flick = d-pad that coasts on, swipe in from the left or right
 *   edge = B (Android's back, fired once TOUCH_SWIPE_PX in - plorpos-3yv.26),
 *   long press = Y on what is under it (favorite a game, Muse's mode, a
 *   rescan; fired while held - plorpos-3yv.22), two-finger tap = X on what
 *   is under it (a game's info), three-finger tap = Menu, swipe down from
 *   the top edge = Select (Muse, from anywhere - plorpos-3yv.46), swipe up
 *   from the bottom edge = the game switcher (plorpos-gl5). Start has
 *   nothing to do on the shelf.
 *
 * A touch that starts within TOUCH_EDGE_PX of a side is only ever back, and
 * one that starts that close to the top or bottom (sides win in the corners)
 * only ever Select or the switcher: no steps, no coast, and no tap once it
 * has moved - so the shelf still scrolls from anywhere inside.
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

/* A tap (then = IN_ACCEPT), long press (IN_Y) or two-finger tap (IN_X)
 * where the finger came down, against what the screen showed (ui_hit,
 * plorpos-3yv.23). On an item the tap selects it, and on the selected one it
 * is A; a long press or two-finger tap selects it and then is Y or X. Empty
 * space is nothing, outside a menu's panel a tap dismisses it, and a screen
 * that recorded no items gets `then` as before. A key recorded by id is the
 * screen's to act on (ui_hit_id), whatever the tap. */
static void touch_at(in_button then)
{
	float s = plat_scale() > 0 ? plat_scale() : 1;
	int b = 0, n = 0;
	bool act = false;

	switch (ui_hit_find((int)(touch.x0 / s), (int)(touch.y0 / s), &b, &n, &act)) {
	case UI_HIT_UNMAPPED: touch_queue(then, 1); break;
	case UI_HIT_NONE:     break;
	case UI_HIT_OUTSIDE:  if (then == IN_ACCEPT) touch_queue(IN_BACK, 1); break;
	case UI_HIT_ITEM:
		touch_queue((in_button)b, n);
		if (n == 0 || act || then != IN_ACCEPT) touch_queue(then, 1);
		break;
	case UI_HIT_ID:       ui_hit_give(n); break;   /* any kind of tap */
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
			touch.edge = touch.x0 < TOUCH_EDGE_PX ? 1 : touch.x0 >= w - TOUCH_EDGE_PX ? -1 : 0;
			touch.moved = false;
			touch.lx = touch.x0; touch.ly = touch.y0; touch.lt = touch.t0;
			touch.vx = touch.vy = 0;
			touch.vedge = touch.edge ? 0 : touch.y0 < TOUCH_EDGE_PX ? 1
			            : touch.y0 >= h - TOUCH_EDGE_PX ? -1 : 0;
		} else if (touch.fingers > touch.most)
			touch.most = touch.fingers;
	} else if (e->type == SDL_FINGERMOTION) {
		if (touch.fingers == 1) {
			touch.x = e->tfinger.x * w; touch.y = e->tfinger.y * h;
			if (touch.free) {
				unsigned now = plat_now_ms();
				float mx = touch.x - touch.x0, my = touch.y - touch.y0;

				/* Speed over the last stretch, eased so one jittery report
				 * cannot decide a fling. */
				if (now > touch.lt) {
					float k = now - touch.lt > 40 ? 1.0f : 0.5f;
					touch.vx += k * ((touch.x - touch.lx) / (float)(now - touch.lt) - touch.vx);
					touch.vy += k * ((touch.y - touch.ly) / (float)(now - touch.lt) - touch.vy);
					touch.lx = touch.x; touch.ly = touch.y; touch.lt = now;
				}
				if (mx * mx + my * my > 16 * 16) touch.moved = true;
			} else if (touch.most == 1 && !touch.held && !touch.edge && !touch.vedge) touch_drag();
			if (touch.most == 1 && !touch.held && touch.edge &&
			    (touch.x - touch.x0) * touch.edge >= TOUCH_SWIPE_PX) {
				touch_queue(IN_BACK, 1);
				touch.held = true;
			}
			if (touch.most == 1 && !touch.held && touch.vedge &&
			    (touch.y - touch.y0) * touch.vedge >= TOUCH_SWIPE_PX) {
				touch_queue(touch.vedge > 0 ? IN_SELECT : IN_SWITCHER, 1);
				touch.held = true;
			}
		}
	} else if (e->type == SDL_FINGERUP && touch.fingers > 0) {
		unsigned now = plat_now_ms();
		float sv = 0, v;

		if (--touch.fingers > 0 || touch.held) return;
		if (touch.free && touch.moved && touch.most == 1 && !touch.edge && !touch.vedge) {
			if (now - touch.lt > FLICK_FRESH_MS) touch.vx = touch.vy = 0;
			touch.rdx = touch.x - touch.x0;
			touch.rdy = touch.y - touch.y0;
			touch.rel = true;
			return;                   /* a drag, not a tap */
		}
		if ((touch.edge || touch.vedge) && touch.most == 1) {
			float dx = touch.x - touch.x0, dy = touch.y - touch.y0;
			if (dx * dx + dy * dy > TOUCH_SWIPE_PX * TOUCH_SWIPE_PX) return;
		}
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
		else if (touch.most == 2)   touch_at(IN_X);
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
	/* Not a drag the screen follows itself (touch.free): a slow pull of a
	 * switcher card is still short of TOUCH_SWIPE_PX at 500 ms, and was a
	 * Favorite (plorpos-3yv.57). */
	if (touch.fingers == 1 && touch.most == 1 && !touch.held && touch.steps == 0 &&
	    !(touch.free && touch.moved) && plat_now_ms() - touch.t0 >= TOUCH_HOLD_MS) {
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

/* ---- touch over a game: edge swipes from evdev (plorpos-3yv.45) ---------
 *
 * The SDL path above only hears touches while the launcher's window has the
 * focus. During a game the panel is read here instead, alongside sway (evdev
 * readers each get every event; nothing is grabbed), and only the first
 * finger (slot 0) is followed: an edge swipe is one finger.
 *
 * Raw coordinates are the panel's own, so the swivel's turn is applied here
 * the way sway applies it to SDL's: closed is sway's transform 90, which puts
 * the panel's right edge at the top (measured with injected swipes,
 * 2026-10-18). */
#define TOUCH_NAME "Goodix Capacitive TouchScreen"
#define PANEL_MAX  719

static int fd_touch = -2;          /* -2: not looked for yet */
static bool g_tight;
static struct {
	int  slot, rx, ry;
	bool down_pending, up_pending, on, fired;
	int  edge;                     /* +1 top, -1 bottom, 0 none */
	bool closed;
	float y0;
	unsigned t0;
} gs;

int gesture_fd(void)
{
	if (fd_touch == -2) fd_touch = open_by_name(TOUCH_NAME);
	return fd_touch;
}

void gesture_tight(bool on) { g_tight = on; }

static bool lid_closed(void)
{
	unsigned long sw[1] = { 0 };

	return fd_power >= 0 && ioctl(fd_power, EVIOCGSW(sizeof sw), sw) >= 0 &&
	       (sw[0] >> SW_LID & 1);
}

/* The screen's y (and x) for the raw point, as the player holds it. */
static void gs_screen(float *x, float *y)
{
	if (gs.closed) { *x = gs.ry; *y = PANEL_MAX - gs.rx; }
	else           { *x = gs.rx; *y = gs.ry; }
}

gesture gesture_read(void)
{
	struct input_event ev;
	gesture out = GESTURE_NONE;

	if (gesture_fd() < 0) return GESTURE_NONE;
	while (read(fd_touch, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type == EV_ABS) {
			if (ev.code == ABS_MT_SLOT) gs.slot = ev.value;
			else if (gs.slot != 0) continue;
			else if (ev.code == ABS_MT_TRACKING_ID) {
				if (ev.value < 0) gs.up_pending = true;
				else gs.down_pending = true;
			} else if (ev.code == ABS_MT_POSITION_X) gs.rx = ev.value;
			else if (ev.code == ABS_MT_POSITION_Y) gs.ry = ev.value;
			continue;
		}
		if (ev.type != EV_SYN || ev.code != SYN_REPORT) continue;
		if (gs.down_pending) {
			int band = g_tight ? GESTURE_TIGHT_PX : GESTURE_BAND_PX;
			float x, y;

			gs.down_pending = false;
			gs.closed = lid_closed();
			gs_screen(&x, &y);
			gs.on = true;
			gs.fired = false;
			gs.y0 = y;
			gs.t0 = plat_now_ms();
			/* The sides win the corners, as on the shelf. */
			gs.edge = x < band || x > PANEL_MAX - band ? 0
			        : y < band ? 1 : y > PANEL_MAX - band ? -1 : 0;
		} else if (gs.on && gs.edge && !gs.fired) {
			float x, y, in;

			gs_screen(&x, &y);
			in = (y - gs.y0) * gs.edge;
			if (g_tight && plat_now_ms() - gs.t0 > GESTURE_TIGHT_MS) gs.edge = 0;
			else if (in >= GESTURE_IN_PX) {
				gs.fired = true;
				out = gs.edge > 0 ? GESTURE_MUSE : GESTURE_SWITCHER;
			}
		}
		if (gs.up_pending) { gs.up_pending = false; gs.on = false; }
	}
	return out;
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
	plat_sink_follow(false);
}

void plat_leds_off(void) { }

/* ---- volume and brightness --------------------------------------------- */

#define VOL_MAX    PLAT_VOL_MAX
#define BACKLIGHT  "/sys/class/backlight/backlight/brightness"
/* The panel's PWM is 0-2499. By eye 2026-10-17 (plorpos-3yv.24): black up
 * to 80, 90 the dimmest it shows - and it stays at that same dimness to 170
 * at least; 140 -> 200 is the first visible step. So one rung on the floor
 * (100, room for another panel's edge), the next at 200, and from there
 * rungs each a visible step apart. diatom port/gkd.c (DIATOM_PORT_ROTATE)
 * has the same ladder and must match. */
static const unsigned short bright_ladder[] = {
	100, 200, 270, 360, 470, 600, 760, 960, 1250, 1570, 1880, 2499
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
static int             vol_resync = -1;   /* a speaker serial: a rung beside first */
static int             speaker_synced = -1; /* the serial the last re-sync landed on */

static void *vol_worker(void *arg)
{
	extern char **environ;
	(void)arg;
	for (;;) {
		char pct[16];
		char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", pct, NULL };
		pid_t pid;
		int st, want;

		int resync;

		pthread_mutex_lock(&vol_mu);
		while (vol_want < 0) pthread_cond_wait(&vol_cv, &vol_mu);
		want = vol_want;
		resync = vol_resync;
		vol_want = vol_resync = -1;
		pthread_mutex_unlock(&vol_mu);

		/* A real change first: the same level again is a no-op to PipeWire. */
		if (resync >= 0) {
			snprintf(pct, sizeof pct, "%d%%", (want > 0 ? want - 1 : 1) * 100 / VOL_MAX);
			if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
				waitpid(pid, &st, 0);
		}
		snprintf(pct, sizeof pct, "%d%%", want * 100 / VOL_MAX);
		if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
			waitpid(pid, &st, 0);
		if (resync >= 0) __atomic_store_n(&speaker_synced, resync, __ATOMIC_RELEASE);
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

/* ---- where the sound goes: platform_gkd.c's (plorpos-3yv.18.2) ----------
 *
 * Every player is a PipeWire client, and PipeWire moves running streams when
 * the default sink changes - so routing is choosing the default sink. The
 * launcher's rules: a USB DAC wins, then a headset, Speaker never uses
 * either, and a headset or DAC going away while it was playing pauses Muse,
 * as a phone does. Driven by `pactl subscribe`, woken only when a sink comes,
 * goes or the default changes, plus the player's setting every 250 ms. */
static bool route_speaker_only;     /* Audio Output = Speaker */
static bool route_bt;               /* PipeWire has a Bluetooth sink */
static bool route_usb;              /* PipeWire has a USB DAC's sink */
static int  sink_level = -1;        /* the default sink's volume, in rungs */
static bool route_music;            /* Muse is heard (musec_heard), from the main thread */
static bool route_known;            /* route_eval has run once */
static bool route_on_speaker;       /* ... and chose the built-in speaker */
/* At a session's start WirePlumber restores the speaker's volume to the
 * card's route, and the sink's node can miss the copy down (its log:
 * "ignoring set_param on already destroyed objects"): the route - and so
 * wpctl - says the player's level while the sound goes out at 100%. A set to
 * the same level is a no-op that leaves it so; the first real change, a
 * volume key, copies it down. A game resumed at power-on played at full
 * volume until then (plorpos-3yv.63). So each speaker sink this process has
 * not seen - by pactl's index, the never-reused object.serial - gets a rung
 * beside the level and then the level (vol_worker), and no game starts
 * before that has landed (plat_audio_settle). */
static int  speaker_id = -1;        /* the speaker sink's serial, -1 none yet */
static bool speaker_fresh;          /* a new one, not yet re-synced */

/* From the main thread: the level, re-synced, onto the speaker seen last. */
static void speaker_resync(int level)
{
	pthread_mutex_lock(&vol_mu);
	vol_want   = level;
	vol_resync = __atomic_load_n(&speaker_id, __ATOMIC_ACQUIRE);
	pthread_cond_signal(&vol_cv);
	pthread_mutex_unlock(&vol_mu);
}

void plat_audio_speaker_only(bool on) { __atomic_store_n(&route_speaker_only, on, __ATOMIC_RELAXED); }
bool plat_bt_audio(void) { return __atomic_load_n(&route_bt, __ATOMIC_RELAXED); }
bool plat_usb_audio(void) { return __atomic_load_n(&route_usb, __ATOMIC_RELAXED); }

/* The note a headset's buttons leave (btplayer's on the Brick); musec reads it
 * wherever Muse matters. tmpfs, so nothing reaches the card. */
static void btkey_write(const char *act)
{
	struct timespec t;
	FILE *f = fopen("/tmp/tortos_btkey.new", "w");

	if (!f) return;
	clock_gettime(CLOCK_MONOTONIC, &t);
	fprintf(f, "%s %ld\n", act, (long)t.tv_sec * 1000 + t.tv_nsec / 1000000);
	if (fclose(f) == 0) rename("/tmp/tortos_btkey.new", "/tmp/tortos_btkey");
}

enum { ON_BUILTIN, ON_BT, ON_USB };   /* which sink route_eval chose last */

static void pactl_run(char *const argv[])
{
	extern char **environ;
	pid_t pid;
	int st;

	if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) == 0)
		waitpid(pid, &st, 0);
}

/* The speaker stays muted while a DAC or headset is the output (plorpos-3yv.37).
 * When one goes, WirePlumber moves the running streams to the speaker at
 * once, inside PipeWire - before any pause of ours can land - so a muted
 * speaker is the only way the song is never heard there. A DAC or headset
 * that went while Muse played holds the mute until Muse has stopped.
 * Applied from scratch on the first pass, so a mute WirePlumber remembered
 * across a crash does not outlive it. */
static bool route_hold;
static int  speaker_muted = -1;

static void speaker_mute(const char *builtin, bool mute)
{
	char *argv[] = { "pactl", "set-sink-mute", (char *)builtin, mute ? "1" : "0", NULL };

	if (!builtin[0] || mute == speaker_muted) return;
	speaker_muted = mute;
	fprintf(stderr, "audio: speaker %s\n", mute ? "muted" : "unmuted");
	pactl_run(argv);
}

/* A headset or DAC is a new node too, at 1.0 until WirePlumber's restore
 * reaches it - and it can miss, as the speaker's did (plorpos-3yv.63): IEMs on
 * a DAC at 100% is hearing damage (plorpos-3yv.68). So each new one gets a
 * real change and back at its own stored level - the route, which wpctl
 * reads; pactl reads the node - before it can become the default. wpctl
 * takes the object id, which only pactl's long list gives by name. */
#define OUT_CAP 6   /* the highest level a headset or DAC arrives at */

static void sink_resync(const char *name)
{
	char line[256], id[16] = "";
	bool mine = false;
	int lv = -1;
	FILE *f = popen("pactl list sinks 2>/dev/null", "r");

	if (!f) return;
	while (fgets(line, sizeof line, f)) {
		char *v = line + strspn(line, " \t");
		if (!strncmp(v, "Name: ", 6)) { v += 6; v[strcspn(v, "\n")] = '\0'; mine = !strcmp(v, name); }
		/* Whole key: a bluez sink lists spa.object.id first. */
		else if (mine && !strncmp(v, "object.id = \"", 13) && !id[0])
			snprintf(id, sizeof id, "%.*s", (int)strcspn(v + 13, "\""), v + 13);
	}
	pclose(f);
	if (!id[0]) { fprintf(stderr, "audio: no id for %s, not re-synced\n", name); return; }
	snprintf(line, sizeof line, "wpctl get-volume %s 2>/dev/null", id);
	if ((f = popen(line, "r"))) {
		float v;
		if (fscanf(f, "Volume: %f", &v) == 1) lv = (int)(v * VOL_MAX + 0.5f);
		pclose(f);
	}
	/* Its stored level can be a loud one - the user's DAC came back at 10 -
	 * so never above OUT_CAP on arrival (plorpos-3yv.73). The set is itself
	 * the real change the re-sync needs. */
	if (lv > OUT_CAP) {
		char cap[8];
		char *set[] = { "wpctl", "set-volume", id, cap, NULL };
		snprintf(cap, sizeof cap, "%.2f", (float)OUT_CAP / VOL_MAX);
		pactl_run(set);
		fprintf(stderr, "audio: new output %s capped at level %d (was %d)\n", name, OUT_CAP, lv);
		return;
	}
	{
		char *up[] = { "wpctl", "set-volume", id, "0.01+", NULL };
		char *dn[] = { "wpctl", "set-volume", id, "0.01-", NULL };
		pactl_run(up);
		pactl_run(dn);
	}
	fprintf(stderr, "audio: new output %s re-synced\n", name);
}

static int sink_seen = -1;          /* the newest sink serial route_eval looked at */

static void route_eval(int *on)
{
	static char set_last[128];          /* the default this process set last */
	char line[256], bt[128] = "", usb[128] = "", builtin[128] = "", def[128] = "";
	char fresh[2][128] = { "", "" };
	int newest = sink_seen;
	const char *want;
	bool speaker = __atomic_load_n(&route_speaker_only, __ATOMIC_RELAXED);
	FILE *f = popen("pactl list short sinks 2>/dev/null", "r");

	if (!f) return;
	/* By name: a USB DAC is an alsa_output too (alsa_output.usb-...). */
	while (fgets(line, sizeof line, f)) {
		char name[128];
		int id;
		if (sscanf(line, "%d %127s", &id, name) != 2) continue;
		if (id > newest) newest = id;
		/* pactl's index is the serial: never reused, so newer = new. */
		if (id > sink_seen && (!strncmp(name, "bluez_output.", 13) ||
		                       !strncmp(name, "alsa_output.usb-", 16)))
			for (int i = 0; i < 2; i++)
				if (!fresh[i][0]) { snprintf(fresh[i], sizeof fresh[i], "%s", name); break; }
		if (!strncmp(name, "bluez_output.", 13) && !bt[0])
			snprintf(bt, sizeof bt, "%s", name);
		else if (!strncmp(name, "alsa_output.usb-", 16) && !usb[0])
			snprintf(usb, sizeof usb, "%s", name);
		else if (!strncmp(name, "alsa_output.", 12) && !builtin[0]) {
			snprintf(builtin, sizeof builtin, "%s", name);
			if (id != speaker_id) {
				__atomic_store_n(&speaker_id, id, __ATOMIC_RELEASE);
				__atomic_store_n(&speaker_fresh, true, __ATOMIC_RELEASE);
			}
		}
	}
	pclose(f);
	for (int i = 0; i < 2 && fresh[i][0]; i++) sink_resync(fresh[i]);
	sink_seen = newest;
	if ((f = popen("pactl get-default-sink 2>/dev/null", "r"))) {
		if (fgets(def, sizeof def, f)) def[strcspn(def, "\n")] = '\0';
		pclose(f);
	}
	/* Two headsets, or two DACs: keep the one already chosen. */
	if (bt[0] && !strncmp(def, "bluez_output.", 13)) snprintf(bt, sizeof bt, "%s", def);
	if (usb[0] && !strncmp(def, "alsa_output.usb-", 16)) snprintf(usb, sizeof usb, "%s", def);
	__atomic_store_n(&route_bt, bt[0] != '\0', __ATOMIC_RELAXED);
	__atomic_store_n(&route_usb, usb[0] != '\0', __ATOMIC_RELAXED);

	if ((*on == ON_BT && !bt[0]) || (*on == ON_USB && !usb[0])) {
		fprintf(stderr, "audio: %s gone, pausing Muse\n", *on == ON_USB ? "USB DAC" : "headset");
		btkey_write("pause");
		if (__atomic_load_n(&route_music, __ATOMIC_RELAXED)) route_hold = true;
	}
	if (speaker || (!usb[0] && !bt[0])) want = builtin;
	else if (usb[0])                     want = usb;
	else                                 want = bt;
	*on = want == usb ? ON_USB : want == bt ? ON_BT : ON_BUILTIN;
	__atomic_store_n(&route_on_speaker, *on == ON_BUILTIN, __ATOMIC_RELAXED);
	/* Not from an empty list: pactl answers nothing before pipewire-pulse is up. */
	if (builtin[0] || bt[0] || usb[0]) __atomic_store_n(&route_known, true, __ATOMIC_RELEASE);
	/* Muted before the default moves, so a stream never reaches it unmuted. */
	if (want != builtin) route_hold = false;
	speaker_mute(builtin, want != builtin || route_hold);
	/* Also when PipeWire already fell back to it: the default WirePlumber
	 * keeps is the one last SET, so a DAC that left stays it and on its
	 * return takes the streams at once, before the re-sync above. */
	if (want[0] && (strcmp(want, def) || strcmp(want, set_last))) {
		char *argv[] = { "pactl", "set-default-sink", (char *)want, NULL };

		snprintf(set_last, sizeof set_last, "%s", want);
		fprintf(stderr, "audio: output %s\n",
		        want == usb ? "USB DAC" : want == bt ? "bluetooth" : "built-in");
		pactl_run(argv);
	}
}

static void *route_worker(void *arg)
{
	int on = ON_BUILTIN;
	(void)arg;

	for (;;) {
		char *argv[] = { "pactl", "subscribe", NULL };
		int fd[2];
		pid_t pid, parent = getpid();
		bool speaker = !route_speaker_only;

		speaker_muted = -1;                     /* a new session: apply afresh */
		sink_seen = -1;                         /* and its serials start again */

		if (pipe(fd) < 0) { sleep(2); continue; }
		/* Not posix_spawn: pactl ignores SIGPIPE, so a launcher that died
		 * would leave it writing to nobody. The kernel ends it instead. */
		pid = fork();
		if (pid == 0) {
			prctl(PR_SET_PDEATHSIG, SIGTERM);
			if (getppid() != parent) _exit(1);      /* died before the prctl */
			dup2(fd[1], STDOUT_FILENO);
			close(fd[0]);
			close(fd[1]);
			execv("/usr/bin/pactl", argv);
			_exit(127);
		}
		close(fd[1]);

		while (pid > 0) {
			struct pollfd p = { fd[0], POLLIN, 0 };
			char buf[1024];
			bool dirty = false;

			if (poll(&p, 1, 250) > 0) {
				ssize_t n = read(fd[0], buf, sizeof buf - 1);
				if (n <= 0) break;          /* pipewire-pulse went away */
				buf[n] = '\0';
				dirty = strstr(buf, "'new' on sink #") || strstr(buf, "'remove' on sink #")
				     || strstr(buf, "on server");
			}
			if (speaker != route_speaker_only) {
				speaker = route_speaker_only;
				dirty   = true;
			}
			if (route_hold && !__atomic_load_n(&route_music, __ATOMIC_RELAXED)) {
				route_hold = false;
				dirty      = true;
			}
			if (dirty) route_eval(&on);
			/* After route_eval, so a new default is the one read. Every
			 * volume set - ours, Diatom's, a headset's - is a sink change. */
			if (dirty || (p.revents && strstr(buf, "'change' on sink #")))
				__atomic_store_n(&sink_level, wpctl_level(), __ATOMIC_RELAXED);
		}
		close(fd[0]);
		if (pid > 0) { kill(pid, SIGTERM); waitpid(pid, NULL, 0); }
		sleep(1);
	}
	return NULL;
}

/* The output's own volume, when something other than a level set here or by
 * Diatom moved it: a headset's buttons (bluez5.hw-volume), or a change of
 * output, since each sink keeps its own. Adopted once the sink has disagreed
 * for half a second - our own sets land in tens of ms. On the shelf it is a
 * nudge; in a game it goes to Diatom, which owns the level while it runs. */
void plat_sink_follow(bool game)
{
	static unsigned since;
	static int seen = -1;
	int count, resident = plat_resident_volume(&count);
	int have = game && resident >= 0 ? resident : cur_vol;
	int want = __atomic_load_n(&sink_level, __ATOMIC_RELAXED);
	unsigned now = plat_now_ms();

	__atomic_store_n(&route_music, musec_heard(), __ATOMIC_RELAXED);
	/* In a game too, at the game's level: the level is unchanged, only
	 * copied down, so it is no fight with Diatom's ownership. */
	if (have >= 0 && __atomic_exchange_n(&speaker_fresh, false, __ATOMIC_ACQUIRE)) {
		seen = -1;
		fprintf(stderr, "audio: new speaker sink %d, level %d\n",
		        __atomic_load_n(&speaker_id, __ATOMIC_RELAXED), have);
		speaker_resync(have);
		return;
	}
	if (want < 0 || have < 0 || want == have) { seen = -1; return; }
	if (want != seen) { seen = want; since = now; return; }
	if (now - since < 500) return;
	seen = -1;
	fprintf(stderr, "audio: output is at level %d, was %d\n", want, have);
	if (game) plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d", want, VOL_MAX + 1);
	else      plat_volume_nudge(want - have);
}

/* Before a game starts: the speaker re-synced at the player's level, so no
 * game's sound can start on a sink that plays at another (plorpos-3yv.63).
 * A headset or DAC keeps its own level, so only the speaker waits. With no
 * speaker sink inside `ms` there is nothing to be heard on; one that comes
 * later is re-synced by plat_sink_follow as it appears. */
void plat_audio_settle(unsigned ms)
{
	unsigned t0 = plat_now_ms();

	if (cur_vol < 0) return;
	while (plat_now_ms() - t0 < ms) {
		if (__atomic_load_n(&route_known, __ATOMIC_ACQUIRE)) {
			int id = __atomic_load_n(&speaker_id, __ATOMIC_ACQUIRE);
			if (!__atomic_load_n(&route_on_speaker, __ATOMIC_RELAXED)) return;
			if (__atomic_exchange_n(&speaker_fresh, false, __ATOMIC_ACQUIRE)) {
				fprintf(stderr, "audio: new speaker sink %d, level %d\n", id, cur_vol);
				speaker_resync(cur_vol);
			}
			if (id >= 0 && __atomic_load_n(&speaker_synced, __ATOMIC_ACQUIRE) == id) {
				if (plat_now_ms() - t0 >= 100)
					fprintf(stderr, "audio: speaker %d re-synced after %u ms\n",
					        id, plat_now_ms() - t0);
				return;
			}
		}
		usleep(20 * 1000);
	}
	fprintf(stderr, "audio: speaker not re-synced after %u ms\n", ms);
}

/* A headset's buttons, platform_gkd.c's: BlueZ gives each AVRCP headset a
 * keyboard named `<headset> (AVRCP)` that comes and goes with the
 * connection, so the list is read again every two seconds. The WF-C510
 * sends PLAYCD/PAUSECD for a press, NEXTSONG for two, PREVIOUSSONG for
 * three. */
#define HEADSETS 4

static const char *avrcp_action(unsigned code)
{
	switch (code) {
	case KEY_PLAYCD: case KEY_PLAYPAUSE: case KEY_PLAY: return "toggle";
	case KEY_PAUSECD: case KEY_STOPCD: case KEY_PAUSE:  return "pause";
	case KEY_NEXTSONG: case KEY_FASTFORWARD:           return "next";
	case KEY_PREVIOUSSONG: case KEY_REWIND:            return "prev";
	}
	return NULL;
}

static void *avrcp_worker(void *arg)
{
	struct pollfd p[HEADSETS];
	char node[HEADSETS][24];
	int n = 0;
	(void)arg;

	for (;;) {
		FILE *f = fopen("/proc/bus/input/devices", "r");
		char line[256], *ev;
		bool avrcp = false;

		while (f && fgets(line, sizeof line, f)) {
			char path[24];
			int i;

			if (!strncmp(line, "N: Name=", 8)) {
				avrcp = strstr(line, " (AVRCP)\"") != NULL;
				continue;
			}
			if (!avrcp || strncmp(line, "H: Handlers=", 12) || !(ev = strstr(line, "event")))
				continue;
			avrcp = false;
			snprintf(path, sizeof path, "/dev/input/%.*s", (int)strspn(ev, "event0123456789"), ev);
			for (i = 0; i < n && strcmp(node[i], path); i++) ;
			if (i < n || n == HEADSETS) continue;
			if ((p[n].fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
			p[n].events = POLLIN;
			snprintf(node[n++], sizeof node[0], "%s", path);
			fprintf(stderr, "audio: headset keys on %s\n", path);
		}
		if (f) fclose(f);

		poll(p, n, 2000);
		for (int i = 0; i < n; i++) {
			struct input_event e;
			ssize_t got;

			if (!p[i].revents) continue;
			while ((got = read(p[i].fd, &e, sizeof e)) == (ssize_t)sizeof e) {
				const char *act = e.type == EV_KEY && e.value == 1 ? avrcp_action(e.code) : NULL;
				if (act) btkey_write(act);
			}
			if (got < 0 && errno != EAGAIN) {   /* the headset went */
				close(p[i].fd);
				p[i] = p[--n];
				memcpy(node[i], node[n], sizeof node[0]);
				i--;
			}
		}
	}
	return NULL;
}

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
	{
		char ao[32];
		db_get_str(db_dev(), "audioout", ao, sizeof ao, "auto");
		plat_audio_speaker_only(!strcmp(ao, "speaker"));
	}
	if (pthread_create(&t, NULL, route_worker, NULL) == 0) pthread_detach(t);
	if (pthread_create(&t, NULL, avrcp_worker, NULL) == 0) pthread_detach(t);

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

/* wlan0 administratively up - Wi-Fi on in the menu. */
static bool wlan_up(void)
{
	char buf[16] = "";
	int fd = open("/sys/class/net/wlan0/flags", O_RDONLY | O_CLOEXEC);
	ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf - 1) : -1;

	if (fd >= 0) close(fd);
	return n > 0 && (strtol(buf, NULL, 16) & 1);   /* IFF_UP */
}

static void wlan_set(bool up)
{
	char *argv[] = { "/sbin/ip", "link", "set", "dev", "wlan0", up ? "up" : "down", NULL };
	helper(argv);
}

/* Bounded: with no bluetoothd, bluetoothctl waits for one forever. */
static bool bt_powered(void)
{
	char *argv[] = { "timeout", "2", "sh", "-c",
	                 "bluetoothctl show | grep -q 'Powered: yes'", NULL };
	return helper(argv);
}

static void bt_set(bool on)
{
	char *argv[] = { "timeout", "5", "bluetoothctl", "power", on ? "on" : "off", NULL };
	helper(argv);
}

/* The headsets connected when Bluetooth went off for a sleep. Powering it
 * off is a real disconnect, after which a headset does not come back by
 * itself (it does after a link only dropped), so the wake reconnects them -
 * these and no others, so one moved to a phone meanwhile is only asked, as
 * launch.sh's bt_reconnect asks at a session's start. */
#define BT_KEEP 2
static char bt_was[BT_KEEP][18];

static void bt_note_connected(void)
{
	char line[128];
	int n = 0;
	FILE *f = popen("timeout 2 bluetoothctl devices Connected 2>/dev/null", "r");

	memset(bt_was, 0, sizeof bt_was);
	if (!f) return;
	while (n < BT_KEEP && fgets(line, sizeof line, f))
		if (!strncmp(line, "Device ", 7) && strlen(line) >= 24) {
			snprintf(bt_was[n], sizeof bt_was[n], "%.17s", line + 7);
			if (bt_mac_valid(bt_was[n])) n++;
			else bt_was[n][0] = '\0';
		}
	pclose(f);
}

static bool bt_is_connected(const char *mac)
{
	char cmd[96];

	snprintf(cmd, sizeof cmd, "bluetoothctl info %s | grep -q 'Connected: yes'", mac);
	{
		char *argv[] = { "timeout", "2", "sh", "-c", cmd, NULL };
		return helper(argv);
	}
}

/* The radios come back behind the wake, not in front of it: with wlan0
 * down the Wi-Fi driver lets the chip power off, and bringing it up again
 * downloads the firmware - ~1.2 s inside `ip link set up` (plorpos-3yv.74.20)
 * that the screen and the shelf must not wait for. A sleep that comes
 * before this is done cancels what is left of it. */
static pthread_t radios_thread;
static bool      radios_pending;
static bool      radios_wifi, radios_bt, radios_cancel;

static void *radios_restore(void *unused)
{
	(void)unused;
	if (radios_wifi) wlan_set(true);
	if (!radios_bt) return NULL;
	bt_set(true);
	for (int i = 0; i < BT_KEEP && bt_was[i][0]; i++) {
		/* By info, never by connect's return (bt_reconnect's rule). */
		for (int round = 0; round < 3; round++) {
			char *argv[] = { "timeout", "5", "bluetoothctl", "connect", bt_was[i], NULL };

			if (__atomic_load_n(&radios_cancel, __ATOMIC_ACQUIRE)) return NULL;
			helper(argv);
			if (bt_is_connected(bt_was[i])) {
				fprintf(stderr, "bluetooth: reconnected %s after the wake (round %d)\n",
				        bt_was[i], round + 1);
				break;
			}
			if (round == 2)
				fprintf(stderr, "bluetooth: %s did not reconnect after the wake\n", bt_was[i]);
			else
				sleep(2);
		}
	}
	return NULL;
}

/* Suspend leaves two things powered that the kernel does not stop: the
 * panel and display controller (~55 mA - light sleep only zeroes the
 * backlight) and the Wi-Fi firmware, still associated (~60 mA). 149 mA
 * suspended with both, 35 mA without (rtcwake runs, plorpos-3yv.72). So the
 * output goes off and wlan0 down around the write; wpa_supplicant stays and
 * re-associates in ~4 s after the wake. */
static bool sleep_mem(void)
{
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

bool plat_sleep(void)
{
	char *off[] = { "swaymsg", "output * power off", NULL };
	char *on[]  = { "swaymsg", "output * power on", NULL };
	/* Diatom lets go of the speaker between games and in its menu, but
	 * PipeWire keeps an idle sink's PCM open ~5 s before suspending it, and
	 * a sleep inside that window holds the codec analog on: 8.1 mA asleep
	 * against 5.4 (plorpos-3yv.74.19.1). Suspending it now closes the PCM;
	 * the next stream resumes it by itself, so nothing undoes this at the
	 * wake. A stream that is playing keeps the sink up regardless. 26 ms. */
	char *quiet[] = { "pactl", "suspend-sink",
	                  "alsa_output.platform-sound_0.HiFi__Speaker__sink", "1", NULL };
	bool wifi, bt, ok;

	if (!plat_sleep_supported()) return false;
	helper(off);
	/* A sleep straight after a wake: the last restore first - ended early,
	 * behind the dark screen - or wlan0 and Bluetooth read as off here and
	 * stay off after this wake. */
	if (radios_pending) {
		__atomic_store_n(&radios_cancel, true, __ATOMIC_RELEASE);
		pthread_join(radios_thread, NULL);
		radios_pending = false;
		__atomic_store_n(&radios_cancel, false, __ATOMIC_RELEASE);
	}
	sync();
	wifi = wlan_up();
	/* Bluetooth off too: it holds the chip powered by itself (~0.8 mA
	 * asleep with Wi-Fi's share gone, plorpos-3yv.74.20), and it lets a
	 * headset go to a phone instead of the Rotate taking it back. */
	bt = bt_powered();
	if (bt) bt_note_connected();
	if (wifi) wlan_set(false);
	if (bt) bt_set(false);
	helper(quiet);
	ok = sleep_mem();
	helper(on);
	radios_wifi = wifi;
	radios_bt = bt;
	if ((wifi || bt) && pthread_create(&radios_thread, NULL, radios_restore, NULL) == 0)
		radios_pending = true;
	else if (wifi || bt)
		radios_restore(NULL);
	return ok;
}

/* ---- battery ---- */

bool plat_battery(int *pct, bool *charging)
{
	return battery_read("/sys/class/power_supply/sc27xx-fgu", pct, charging);
}
