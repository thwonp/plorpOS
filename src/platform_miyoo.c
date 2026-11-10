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
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
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
	/* Nearest: the software renderer's linear stretch cost about 30% of a
	 * scroll frame on the SSD202D (plorpos-f1p.14.3). */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
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

/* How long the last present sat in FBIOPAN_DISPLAY waiting for vblank, for the
 * frame trace: draw minus this is what the frame itself cost. */
unsigned plat_pan_us;

static unsigned mono_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000000u + (unsigned)ts.tv_nsec / 1000u);
}

/* HALF-SIZE FRAMES. A scroll frame cost about 26 ms, nearly all of it blending
 * and scaling roughly 600K pixels (measured 2026-11-08: dimming alone was 1.7
 * ms of it). While a shelf moves the frame is drawn into a half-size target at
 * render scale 0.5 - a quarter of the pixels - read back, and doubled into the
 * page by plat_present. At rest it draws full size, so nothing stays soft. */
static SDL_Texture *lo_tex;
static uint32_t *lo_buf;
static bool lo_on;

void plat_lowres(bool on)
{
	if (!on || lo_on || !ren || !surf) return;
	if (!lo_tex) {
		lo_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
		                           surf->w / 2, surf->h / 2);
		lo_buf = malloc((size_t)(surf->w / 2) * (size_t)(surf->h / 2) * 4);
		if (!lo_tex || !lo_buf) {
			if (lo_tex) SDL_DestroyTexture(lo_tex);
			free(lo_buf);
			lo_tex = NULL; lo_buf = NULL;
			return;
		}
	}
	/* The launcher lays out in a 1024x768 stage under a render scale of its own
	 * (plat_geometry_init): halve THAT, do not replace it. SetRenderTarget
	 * resets the scale to 1 and puts it back on the way out. */
	float sx = 1.0f, sy = 1.0f;

	SDL_RenderGetScale(ren, &sx, &sy);
	if (SDL_SetRenderTarget(ren, lo_tex) != 0) return;
	SDL_RenderSetScale(ren, sx * 0.5f, sy * 0.5f);
	lo_on = true;
}

void plat_present(SDL_Renderer *r)
{
	struct fb_var_screeninfo v;
	size_t row;
	bool lo = false;

	SDL_RenderPresent(r);
	if (lo_on && r == ren) {
		lo = SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, lo_buf,
		                          (surf->w / 2) * 4) == 0;
		SDL_SetRenderTarget(ren, NULL);   /* restores the full-size scale */
		lo_on = false;
		if (!lo) return;
	}
	if (!surf || r != ren || ioctl(fb_fd, FBIOGET_VSCREENINFO, &v) != 0) return;
	row = (size_t)surf->w;
	/* DRAW OFF GLASS, THEN FLIP. Copying onto the page being scanned out tore
	 * every scroll frame, and frames of about 27 ms against a 16.7 ms refresh
	 * showed for one refresh then two. So the copy goes to the next page and
	 * FBIOPAN_DISPLAY - a blocking flip that waits for vblank, as diatom
	 * measured on this fb (port/miyoo.c) - puts it on glass: frames lock to the
	 * panel and none is half one picture, half the other. With under two pages
	 * it draws on glass as it did before. */
	unsigned pages = v.yres ? v.yres_virtual / v.yres : 1;
	unsigned front = v.yres ? v.yoffset / v.yres : 0, back = front;
	if (pages > 3) pages = 3;
	if (pages >= 2) back = (front + 1) % pages;
	if ((size_t)(back * v.yres + (unsigned)surf->h) * fix.line_length > fix.smem_len) return;
	/* The panel is mounted upside down (seen on the Flip, 2026-11-04), as on
	 * other Miyoo Minis: the surface goes on glass turned 180 degrees - the
	 * last row first, each row back to front. */
	if (lo && row <= 2048) {
		static uint32_t line[2048];
		size_t sw = row / 2;

		for (int y = 0; y < surf->h / 2; y++) {
			const uint32_t *s = lo_buf + (size_t)y * sw;
			uint8_t *d0 = fb + ((size_t)back * v.yres + (size_t)(surf->h - 1 - 2 * y)) * fix.line_length;
			uint8_t *d1 = d0 - fix.line_length;

			for (size_t x = 0; x < sw; x++)
				line[row - 1 - 2 * x] = line[row - 2 - 2 * x] = s[x];
			memcpy(d0, line, row * 4);
			memcpy(d1, line, row * 4);
		}
	} else
	for (int y = 0; y < surf->h; y++) {
		const uint32_t *s = (const uint32_t *)((uint8_t *)surf->pixels + (size_t)y * (size_t)surf->pitch);
		uint32_t *d = (uint32_t *)(fb + ((size_t)back * v.yres + (size_t)(surf->h - 1 - y)) *
		                           fix.line_length) + row - 1;
		for (size_t x = 0; x < row; x++) *d-- = s[x];
	}
	if (back != front) {
		unsigned t0 = mono_us();

		v.yoffset = back * v.yres;
		if (ioctl(fb_fd, FBIOPAN_DISPLAY, &v) != 0)
			ioctl(fb_fd, FBIOPUT_VSCREENINFO, &v);
		plat_pan_us = mono_us() - t0;
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
	 * press the in-game menu waited for (2026-11-04). It is its OWN open of
	 * the pad's node, though: the power button is on event0 (captured
	 * 2026-11-08, nothing on the hall node's event1 for a tap), and every
	 * open of an evdev node has its own queue, so this one sees the press
	 * the in-game wait needs without taking it from fd_pad. The hall sensor
	 * (event1, lid) is f1p.7's. */
	if (fd_power < 0) fd_power = open(PAD_NODE, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
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

static void lid_poll(void);

void plat_input_poll(in_state *st)
{
	SDL_Event e;
	struct input_event ev;

	lid_poll();
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

/* Backlight: pwm0's duty over the period the boot hook sets (800). Judged by
 * eye on the Flip (plorpos-f1p.53): the panel is at full light by duty ~75-100
 * and readable down to 3, so the twelve rungs are raw duties spanning 3..100,
 * roughly geometric. diatom's bright_apply carries the same table. */
#define PWM_DUTY "/sys/class/pwm/pwmchip0/pwm0/duty_cycle"
void apply_brightness(int b)
{
	static const unsigned char ladder[] = { 3, 5, 8, 12, 17, 24, 33, 44, 56, 70, 85, 100 };
	_Static_assert(sizeof ladder == PLAT_BRIGHT_MAX + 1, "ladder vs PLAT_BRIGHT_MAX");
	cur_bright = b;
	FILE *f = fopen(PWM_DUTY, "w");
	if (!f) return;
	fprintf(f, "%d", ladder[clampi(b, 0, PLAT_BRIGHT_MAX)]);
	fclose(f);
}
void backlight_off(void) { }

/* The lid (plorpos-f1p.7): the hall sensor's level, read four times a second
 * (the level, not event1's key events, whose sense is unknown). Closed is 0:
 * 1 was read with the lid open. Closed blanks the backlight and mutes; open
 * puts both back. No suspend and no power-off: the Flip has neither yet, and
 * a lid misread must never end a session. diatom does the same in a game. */
#define HALL_VALUE "/sys/devices/soc0/soc/soc:hall-mh248/hallvalue"
static void lid_poll(void)
{
	static unsigned next;
	static bool closed;
	static int saved_vol = -1;
	unsigned now = plat_now_ms();
	char buf[8] = "";
	int fd;
	bool c;

	if ((int)(now - next) < 0) return;
	next = now + 250;
	if ((fd = open(HALL_VALUE, O_RDONLY | O_CLOEXEC)) < 0) return;
	if (read(fd, buf, sizeof buf - 1) <= 0) { close(fd); return; }
	close(fd);
	c = buf[0] == '0';
	if (c == closed) return;
	closed = c;
	fprintf(stderr, "lid: %s (hall %c)\n", c ? "closed" : "open", buf[0]);
	if (c) {
		saved_vol = cur_vol;
		if (mixer_fd >= 0) apply_volume(0);
		{
			FILE *f = fopen(PWM_DUTY, "w");
			if (f) { fputs("0", f); fclose(f); }
		}
	} else {
		apply_brightness(cur_bright);
		if (mixer_fd >= 0 && saved_vol >= 0) apply_volume(saved_vol);
	}
}

void plat_settings_init(void)
{
	int v = db_get_int(db_dev(), "volume", -1);
	int b = db_get_int(db_dev(), "brightness", -1);

	/* No saved level is the resident's quiet default (8 of 20), so the
	 * first key press moves from there rather than from full. */
	cur_vol    = v >= 0 ? clampi(v, 0, VOL_MAX) : 8;
	cur_bright = b >= 0 ? clampi(b, 0, BRIGHT_MAX) : BRIGHT_MAX / 2;
	/* platform.c's nudges return early while these are negative, and the Flip
	 * has no device to open for either (volume is the resident's, brightness a
	 * sysfs write per change), so they hold /dev/null: any non-negative fd
	 * says "controls present". Without it neither key ever did anything. */
	mixer_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	disp_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
	apply_volume(cur_vol);
	apply_brightness(cur_bright);
	plat_battery(NULL, NULL);   /* the first read starts now, so the first draw has it */
}

/* ---- sleep: a stub until plorpos-f1p.7; battery from the stock tool ------ */

bool plat_sleep_supported(void) { return false; }
bool plat_sleep(void) { return false; }

/* The Flip has no power_supply node: the gauge is the stock /customer/app/axp_test,
 * which prints {"battery":N, "voltage":mV, "charging":C} and exits (the tool
 * Allium reads too). Read at most every 30 s, whatever the caller's rate.
 * charging == 3 is taken as "on the charger" (unverified: seen 3 at 100 %,
 * 4205 mV; needs an unplugged reading).
 *
 * The read runs on its own thread and plat_battery only returns the last
 * answer (plorpos-f1p.44). popen() forked the launcher and waited for the tool
 * on the render thread: 12-17 ms (a lost frame) at 30 MB, and 108 ms once
 * while the launcher was busy browsing. posix_spawn shares the address space
 * instead of copying the page tables of a launcher that can be 55 MB. */
static SDL_atomic_t batt_state = { -1 };   /* -1 unknown, else pct | charging << 8 */
static SDL_atomic_t batt_busy;

static int batt_read(void *arg)
{
	static char *const argv[] = { "axp_test", NULL };
	static char *const envp[] = { "LD_LIBRARY_PATH=/lib:/config/lib:/customer/lib", NULL };
	char l[128] = "";
	int fds[2], b, v, c, st = -1;

	(void)arg;
	if (pipe2(fds, O_CLOEXEC) == 0) {
		posix_spawn_file_actions_t fa;
		pid_t pid;

		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
		posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
		if (posix_spawn(&pid, "/customer/app/axp_test", &fa, NULL, argv, envp) == 0) {
			size_t n = 0;
			ssize_t r;

			close(fds[1]);
			fds[1] = -1;
			while (n < sizeof l - 1 && (r = read(fds[0], l + n, sizeof l - 1 - n)) > 0)
				n += (size_t)r;
			l[n] = '\0';
			waitpid(pid, NULL, 0);
			if (sscanf(l, "{\"battery\":%d, \"voltage\":%d, \"charging\":%d}", &b, &v, &c) == 3)
				st = clampi(b, 0, 100) | (c == 3 ? 1 << 8 : 0);
		}
		posix_spawn_file_actions_destroy(&fa);
		close(fds[0]);
		if (fds[1] >= 0) close(fds[1]);
	}
	SDL_AtomicSet(&batt_state, st);
	SDL_AtomicSet(&batt_busy, 0);
	return 0;
}

bool plat_battery(int *pct, bool *charging)
{
	static Uint32 next;
	Uint32 now = SDL_GetTicks();
	int s;

	if ((!next || (int)(now - next) >= 0) && SDL_AtomicCAS(&batt_busy, 0, 1)) {
		SDL_Thread *t = SDL_CreateThread(batt_read, "battery", NULL);

		next = now + 30000;
		if (t) SDL_DetachThread(t);
		else SDL_AtomicSet(&batt_busy, 0);
	}
	s = SDL_AtomicGet(&batt_state);
	if (s < 0) return false;
	if (pct) *pct = s & 0xff;
	if (charging) *charging = (s >> 8) & 1;
	return true;
}
