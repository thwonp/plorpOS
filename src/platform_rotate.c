/* SPDX-License-Identifier: MIT */
/* The Anbernic RG Rotate (plorpos-3yv): Unisoc T618, a 720x720 swivel panel
 * with touch, on Armbian's Debian trixie from the card (mainline 7.2 kernel,
 * Mesa panfrost). Built with PLATFORM_H700 as well as PLATFORM_ROTATE: every
 * H700 branch in the shared code is "a Linux handheld that is not a Brick"
 * (Menu as the brightness modifier, no mute switch, Diatom's audio, the
 * PICO-8 pad mapping), and PLATFORM_ROTATE marks where this one differs.
 *
 * The panel is KMS/DRM, drawn through SDL2's kmsdrm driver and GLES2. Every
 * input is a discrete GPIO key under mainline gpio-keys: the d-pad is four
 * buttons, not a hat. The nodes are found by name, as their numbers follow
 * probe order.
 *
 * Not here yet, by plan: drawing over a frozen child (.12), the swivel's
 * rotation (.9), touch (.10), the volume control (.6). */
#include "atomic.h"
#include "db.h"
#include "platform.h"
#include "audioout.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include "platform_dev.h"

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
const char *const plat_pico8_preload = "/mnt/SDCARD/TortOS/pico8sdl.so";
const char *const plat_pico8_path = "/mnt/SDCARD/TortOS/pico8";

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
/* SDL numbers gpio-keys' buttons by code from BTN_SOUTH up. PROVISIONAL with
 * the map above. */
const char plat_pico8_pad[] =
	"SDL_GAMECONTROLLERCONFIG=19000000010000000100000000010000,gpio-keys-gamepad,"
	"a:b0,b:b1,y:b3,x:b4,leftshoulder:b6,rightshoulder:b7,back:b10,start:b11,"
	"lefttrigger:b8,righttrigger:b9,platform:Linux,";

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

/* ---- video ----------------------------------------------------------- */

bool plat_video_init(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	win = SDL_CreateWindow("plorpOS", 0, 0, 720, 720,
	                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
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
	plat_geometry_init(ren);
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(ren, &info) == 0)
		fprintf(stderr, "renderer: %s, driver: %s, vsync %s, render-to-texture %s\n",
		        info.name, SDL_GetCurrentVideoDriver(),
		        (info.flags & SDL_RENDERER_PRESENTVSYNC) ? "granted" : "DECLINED (requested)",
		        (info.flags & SDL_RENDERER_TARGETTEXTURE) ? "available" : "MISSING");
	return true;
}

/* Over a frozen child: KMS has one master, the child, until .12 works out
 * the handover. Its only caller (native PICO-8's menu) quits on false. */
bool plat_video_init_over_child(void) { return false; }

void plat_present(SDL_Renderer *r) { SDL_RenderPresent(r); }

void plat_video_quit(void)
{
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (win) { SDL_DestroyWindow(win); win = NULL; }
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void screen_yield(bool to_game) { (void)to_game; }

/* ---- input: raw evdev, no SDL joystick --------------------------------- */

bool plat_input_init(void)
{
	if (fd_power < 0) fd_power = open_by_name(SYS_NAME);
	if (fd_pad < 0) fd_pad = open_by_name(PAD_NAME);
	if (fd_pad < 0) fprintf(stderr, "input: no %s\n", PAD_NAME);
	return fd_pad >= 0;
}

void plat_input_flush(void)
{
	struct input_event ev;
	SDL_Event e;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	menu_down = menu_sent = menu_spent = menu_pulse = false;
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

bool child_hide(pid_t pid)
{
	(void)pid;
	if (fd_pad < 0 || ioctl(fd_pad, EVIOCGRAB, 1) != 0) {
		fprintf(stderr, "run: pad grab failed: %s\n", strerror(errno));
		return false;
	}
	return true;
}

void child_restore(pid_t pid, bool show)
{
	(void)pid; (void)show;
	if (fd_pad >= 0) ioctl(fd_pad, EVIOCGRAB, 0);
}

#define PICO8_QUIET "/tmp/plorpos-pico8-quiet"   /* tools/pico8sdl.c's too */

void child_quiet(pid_t pid, bool on)
{
	static pid_t q_pid;
	static int q_said = -1;

	if (pid != q_pid) { q_pid = pid; q_said = -1; }
	if (q_said == (int)on) return;
	if (on) {
		int fd = open(PICO8_QUIET, O_WRONLY | O_CREAT, 0644);
		if (fd < 0) return;
		close(fd);
	} else if (unlink(PICO8_QUIET) != 0 && errno != ENOENT) {
		return;
	}
	q_said = on;
	fprintf(stderr, "run: child %s\n", on ? "quiet" : "heard");
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

void plat_input_poll(in_state *st)
{
	SDL_Event e;

	memset(st->pressed, 0, sizeof st->pressed);
	if (g_terminating) st->quit_requested = true;
	plat_audio_jack_poll();
	while (SDL_PollEvent(&e))
		if (e.type == SDL_QUIT) st->quit_requested = true;
	pad_read(st);
	sys_read(st);
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

int mixer_fd = -1, disp_fd = -1;
int cur_vol = -1, cur_bright = -1;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void levels_save(void)
{
	db_set_int(db_dev(), "volume", cur_vol);
	db_set_int(db_dev(), "brightness", cur_bright);
	db_write_boot_env();
}

/* Remembered only: which control is the speaker's level is decided by ear
 * in .6 (HPL/HPR Gain or the DSP's VBC_VOLUME). */
void apply_volume(int v) { cur_vol = v; }

/* The jack is on the codec; no headphones on this device's own panel yet. */
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

void plat_settings_init(void)
{
	int v, b;

	mixer_fd = open("/dev/snd/controlC0", O_RDWR | O_CLOEXEC);
	disp_fd  = open(BACKLIGHT, O_RDWR | O_CLOEXEC);
	if (mixer_fd < 0) fprintf(stderr, "settings: no /dev/snd/controlC0\n");
	if (disp_fd  < 0) fprintf(stderr, "settings: no %s\n", BACKLIGHT);

	v = db_get_int(db_dev(), "volume", -1);
	b = db_get_int(db_dev(), "brightness", -1);
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
