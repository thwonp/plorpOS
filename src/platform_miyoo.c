/* SPDX-License-Identifier: MIT */
/* The Miyoo Mini Flip (plorpos-f1p): SigmaStar SSD202D, two Cortex-A7s,
 * 128 MB (101 MB to Linux), Miyoo's stock 4.9.84 kernel and glibc 2.28,
 * started from the card's .tmp_update/updater. The build defines
 * PLATFORM_H700 as well as PLATFORM_MIYOO (mk/cross.mk), as the Rotate's does:
 * the H700's branches fit a BusyBox handheld with one fbdev panel and a codec
 * one opener at a time, and PLATFORM_MIYOO marks where this file differs.
 *
 * No GPU. The launcher draws with SDL's software renderer into a surface and
 * plat_present copies it onto the fb0 page on glass - the H700's way over a
 * frozen child, here the only way. Our SDL2 is offscreen-only
 * (mk/fetch-miyoo-sysroot.sh).
 *
 * Not here yet, by plan (plorpos-f1p.7): volume (MI_AO, not ALSA - ALSA and
 * OSS play at half rate on this device, f1p.3), brightness (pwm0), battery,
 * sleep and the lid (event1, "hall wake key"). Until then they are stubs that
 * say so. */
#include "atomic.h"
#include "db.h"
#include "platform.h"
#include "audioout.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include "platform_dev.h"

/* The card is the Flip's only storage, at Miyoo's spelling. */
const char *P_ROOT = "/mnt/SDCARD/TortOS";
const char *P_CARD = "/mnt/SDCARD";
const char *P_ROMS = "/mnt/SDCARD/Roms";
const char *P_USERDATA = "/mnt/SDCARD/.userdata/miyooflip";
const char *P_SHARED = "/mnt/SDCARD/.userdata/shared";
const char *P_WEB = "/mnt/SDCARD/TortOS/res/web";
const char *P_FONT = "/mnt/SDCARD/TortOS/menu.ttf";

/* What a game's process is told about the device - see build_child_env(). */
const char *const plat_child_env[] = {
	"PLATFORM=miyoo", "DEVICE=miyooflip",
	"SDCARD_PATH=/mnt/SDCARD",
	"BIOS_PATH=/mnt/SDCARD/Bios",
	"CHEATS_PATH=/mnt/SDCARD/Cheats",
	"SAVES_PATH=/mnt/SDCARD/Saves",
	NULL
};
/* Our SDL2 and FFmpeg live on the card (the firmware has SDL 1.2 only), and
 * a standalone diatom needs the firmware's libmi_* for MI_AO and MI_GFX. */
const char plat_child_libpath[] = ":/mnt/SDCARD/TortOS/lib:/config/lib:/customer/lib";
/* Native PICO-8 is out of scope here (no PICO-8 for armhf glibc 2.28 tested). */
const char *const plat_pico8_preload = "";
const char *const plat_pico8_path = "/mnt/SDCARD/TortOS/pico8";
const char plat_pico8_pad[] = "";

bool plat_is_brick_pro(void) { return false; }
bool plat_two_sticks(void) { return false; }
/* No brightness keys: Menu held turns the volume keys into them. */
const char *plat_bright_keys(void) { return "Menu+Vol"; }

/* "soc:gpio_keys" (event0) is a keyboard: Miyoo's map, read off the Flip
 * (2026-10-04, EVIOCGBIT) and the same as every Miyoo Mini's. The hall
 * sensor is event1 ("hall wake key": KEY_POWER and KEY_WAKEUP). */
#define PAD_NODE   "/dev/input/event0"
#define LID_NODE   "/dev/input/event1"
#define CODE_MENU  KEY_ESC
static const struct { int code; in_button b; } padmap[] = {
	{ KEY_SPACE, IN_ACCEPT }, { KEY_LEFTCTRL, IN_BACK },
	{ KEY_LEFTSHIFT, IN_X }, { KEY_LEFTALT, IN_Y },
	{ KEY_E, IN_L1 }, { KEY_T, IN_R1 }, { KEY_TAB, IN_L2 }, { KEY_BACKSPACE, IN_R2 },
	{ KEY_RIGHTCTRL, IN_SELECT }, { KEY_ENTER, IN_START },
	{ KEY_LEFT, IN_LEFT }, { KEY_RIGHT, IN_RIGHT }, { KEY_UP, IN_UP }, { KEY_DOWN, IN_DOWN },
};

static SDL_Renderer *ren;
static int fd_pad = -1;

/* Menu as on the H700: a modifier when held with another button, brightness
 * with a volume key, a press on release alone, the switcher held. */
static bool menu_down, menu_sent, menu_spent, menu_pulse;
static unsigned menu_t0;
static bool switcher_pulse;

SDL_Renderer *plat_renderer(void) { return ren; }

/* ---- video: software renderer -> fb0 ------------------------------------ */

static SDL_Surface *surf;
static uint8_t *fb;
static int fb_fd = -1;
static struct fb_fix_screeninfo fix;

static bool fb_open(void)
{
	struct fb_var_screeninfo v;

	fb_fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb_fd < 0 || ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) != 0 ||
	    ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) != 0) {
		fprintf(stderr, "video: fb0: %s\n", strerror(errno));
		return false;
	}
	if (v.bits_per_pixel != 32 || v.red.offset != 16 || v.green.offset != 8 ||
	    v.blue.offset != 0) {
		fprintf(stderr, "video: fb0 is not 32-bit xRGB\n");
		return false;
	}
	fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb == MAP_FAILED) { fb = NULL; return false; }
	surf = SDL_CreateRGBSurfaceWithFormat(0, (int)v.xres, (int)v.yres, 32,
	                                      SDL_PIXELFORMAT_ARGB8888);
	ren = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
	if (!ren) {
		fprintf(stderr, "video: %s\n", SDL_GetError());
		return false;
	}
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
	plat_geometry_init(ren);
	fprintf(stderr, "video: software renderer, %ux%u on fb0\n", v.xres, v.yres);
	return true;
}

bool plat_video_init(void)
{
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
	/* No window: the offscreen driver is there only so SDL's video
	 * subsystem (textures, surfaces) initialises. */
	SDL_SetHint(SDL_HINT_VIDEODRIVER, "offscreen");
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "video init: %s\n", SDL_GetError());
		return false;
	}
	if (!fb_open()) { plat_video_quit(); return false; }
	return true;
}

/* The same renderer over a frozen child: nothing else can draw here. */
bool plat_video_init_over_child(void)
{
	if (!fb_open()) { plat_video_quit(); return false; }
	return true;
}

void plat_present(SDL_Renderer *r)
{
	struct fb_var_screeninfo v;
	size_t row;

	SDL_RenderPresent(r);
	if (!surf || r != ren || ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) != 0) return;
	row = (size_t)surf->w;
	if (((size_t)v.yoffset + (size_t)surf->h) * fix.line_length > fix.smem_len) return;
	/* The panel is mounted upside down (seen on the Flip, 2026-11-04), as on
	 * other Miyoo Minis: the surface goes on glass turned 180 degrees - the
	 * last row first, each row back to front. */
	for (int y = 0; y < surf->h; y++) {
		const uint32_t *s = (const uint32_t *)((uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch);
		uint32_t *d = (uint32_t *)(fb + ((size_t)v.yoffset + (size_t)(surf->h - 1 - y)) *
		                           fix.line_length) + row - 1;
		for (size_t x = 0; x < row; x++) *d-- = s[x];
	}
}

void plat_video_quit(void)
{
	if (ren) { SDL_DestroyRenderer(ren); ren = NULL; }
	if (surf) { SDL_FreeSurface(surf); surf = NULL; }
	if (fb) munmap(fb, fix.smem_len);
	fb = NULL;
	if (fb_fd >= 0) { close(fb_fd); fb_fd = -1; }
}

void screen_yield(bool to_game) { (void)to_game; }

/* ---- input: raw evdev --------------------------------------------------- */

bool plat_input_init(void)
{
	if (fd_pad < 0) fd_pad = open(PAD_NODE, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	/* platform.c reads fd_power on its own and drains everything on it, so it
	 * must never be the pad's descriptor: aliased to it, it swallowed every
	 * press the in-game menu waited for (2026-11-04). The hall sensor's node
	 * reports KEY_POWER too; which node the power BUTTON uses is f1p.7's. */
	if (fd_power < 0) fd_power = open(LID_NODE, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd_pad < 0) fprintf(stderr, "input: %s: %s\n", PAD_NODE, strerror(errno));
	return fd_pad >= 0;
}

void plat_input_flush(void)
{
	struct input_event ev;
	SDL_Event e;

	SDL_PumpEvents();
	while (SDL_PollEvent(&e)) { }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) { }
	menu_sent = menu_pulse = switcher_pulse = false;
	{
		unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = { 0 };

		menu_down = fd_pad >= 0 && ioctl(fd_pad, EVIOCGKEY(sizeof bits), bits) >= 0 &&
		            (bits[CODE_MENU / (8 * sizeof bits[0])] >> (CODE_MENU % (8 * sizeof bits[0])) & 1);
		menu_spent = menu_down;
	}
}

void plat_input_quit(void) { }

int menu_key(int *code)
{
	*code = CODE_MENU;
	return fd_pad;
}

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

void child_quiet(pid_t pid, bool on) { (void)pid; (void)on; }

static void pad_read(in_state *st)
{
	struct input_event ev;

	if (menu_pulse) { set_btn(st, IN_MENU, false); menu_pulse = false; }
	if (switcher_pulse) { set_btn(st, IN_SWITCHER, false); switcher_pulse = false; }
	while (fd_pad >= 0 && read(fd_pad, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		if (ev.type != EV_KEY || ev.value == 2) continue;
		bool down = ev.value == 1;
		in_button vol = IN_NONE, bright = IN_NONE;

		if (ev.code == KEY_POWER) { set_btn(st, IN_POWER, down); continue; }
		if (ev.code == CODE_MENU) {
			menu_down = down;
			if (down) { menu_sent = menu_spent = false; menu_t0 = plat_now_ms(); continue; }
			if (menu_sent) set_btn(st, IN_MENU, false);
			else if (!menu_spent) { set_btn(st, IN_MENU, true); menu_pulse = true; }
			menu_sent = false;
			continue;
		}
		if (ev.code == KEY_VOLUMEUP)   { vol = IN_VOLUP; bright = IN_BRIGHTUP; }
		if (ev.code == KEY_VOLUMEDOWN) { vol = IN_VOLDN; bright = IN_BRIGHTDN; }
		if (vol != IN_NONE) {
			if (down && menu_down) menu_spent = true;
			if (down) set_btn(st, menu_down ? bright : vol, true);
			else { set_btn(st, vol, false); set_btn(st, bright, false); }
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

void plat_input_poll(in_state *st)
{
	SDL_Event e;
	struct input_event ev;

	memset(st->pressed, 0, sizeof st->pressed);
	if (g_terminating) st->quit_requested = true;
	while (SDL_PollEvent(&e))
		if (e.type == SDL_QUIT) st->quit_requested = true;
	pad_read(st);
	/* fd_power (the lid's node): KEY_POWER as POWER; the lid itself is f1p.7's. */
	while (fd_power >= 0 && read(fd_power, &ev, sizeof ev) == (ssize_t)sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value != 2)
			set_btn(st, IN_POWER, ev.value == 1);
}

void plat_leds_off(void) { }

/* ---- volume: the resident diatom's (it owns MI_AO); brightness a stub until
 * plorpos-f1p.7 ----------------------------------------------------------- */

#define VOL_MAX    PLAT_VOL_MAX
#define BRIGHT_MAX PLAT_BRIGHT_MAX

int mixer_fd = -1, disp_fd = -1;
int cur_vol = -1, cur_bright = -1;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void levels_save(void)
{
	db_set_int(db_dev(), "volume", cur_vol);
	db_set_int(db_dev(), "brightness", cur_bright);
	db_write_boot_env();
}

/* MI_AO has one owner, the resident diatom, so the level goes to it as
 * SETLEVEL, on the shelf and in a game alike (platform.c sends the same at
 * a game's start). */
void apply_volume(int v)
{
	cur_vol = v;
	if (v >= 0) plat_resident_line("SETLEVEL\tkind=volume\tindex=%d\tcount=%d", v, VOL_MAX + 1);
}
bool plat_headphones_present(void) { return false; }
void plat_audio_jack_poll(void) { }
void jack_forget(void) { }

bool plat_mute_poll(bool own_volume) { (void)own_volume; return false; }
bool plat_muted(void) { return false; }
void mute_forget(void) { }
void plat_mute_switch_lock(bool lock) { (void)lock; }
bool plat_hold_switch(void) { return false; }

void apply_brightness(int b) { cur_bright = b; }
void backlight_off(void) { }

void plat_settings_init(void)
{
	int v = db_get_int(db_dev(), "volume", -1);
	int b = db_get_int(db_dev(), "brightness", -1);

	/* No saved level is the resident's quiet default (8 of 20), so the
	 * first key press moves from there rather than from full. */
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX) : 8;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	apply_volume(cur_vol);
}

/* ---- sleep and battery: stubs until plorpos-f1p.7 ------------------------ */

bool plat_sleep_supported(void) { return false; }
bool plat_sleep(void) { return false; }

bool plat_battery(int *pct, bool *charging)
{
	(void)pct; (void)charging;
	return false;
}
