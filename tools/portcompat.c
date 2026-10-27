/* SPDX-License-Identifier: MIT
 *
 * Preloaded into every PortMaster port on the H700 by sd/h700/portmaster/
 * pm-run.sh (plorpos-2wl.10), never into the launcher or Diatom. Ports are
 * written against ROCKNIX and the like, and where BaseOS differs the port
 * dies instead of doing without. Each fix here says what it was.
 *
 * Rumble: Apotris drives the motor through /sys/class/pwm/pwmchipN/pwm0/
 * duty_cycle, finds no pwm0 channel (nothing exports one on BaseOS), logs
 * "couldn't open rumble file", and frees an uninitialised pointer when the
 * game starts. Any open of a PWM file lands on /dev/null instead: the writes
 * succeed and nothing vibrates.
 *
 * Keys: most ports are played through gptokeyb, which turns the pad into keys
 * on a uinput "Fake Keyboard" the game reads through SDL. Our SDL has no udev,
 * so it never sees a device that appears after it started - and gptokeyb
 * needs a moment to make its device, after the script has started the game.
 * SDL_Init here waits for the device (only while gptokeyb runs, 3 s at most)
 * and names it to SDL through SDL_EVDEV_DEVICES, class 2 = keyboard.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PWM_DIR "/sys/class/pwm/"

static const char *fix(const char *path)
{
	return path && !strncmp(path, PWM_DIR, sizeof PWM_DIR - 1) ? "/dev/null" : path;
}

int open(const char *path, int flags, ...)
{
	static int (*real)(const char *, int, ...);
	mode_t mode = 0;

	if (!real) real = dlsym(RTLD_NEXT, "open");
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return real(fix(path), flags, mode);
}

int open64(const char *path, int flags, ...)
{
	static int (*real)(const char *, int, ...);
	mode_t mode = 0;

	if (!real) real = dlsym(RTLD_NEXT, "open64");
	if (flags & (O_CREAT | O_TMPFILE)) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return real(fix(path), flags, mode);
}

FILE *fopen(const char *path, const char *mode)
{
	static FILE *(*real)(const char *, const char *);

	if (!real) real = dlsym(RTLD_NEXT, "fopen");
	return real(fix(path), mode);
}

FILE *fopen64(const char *path, const char *mode)
{
	static FILE *(*real)(const char *, const char *);

	if (!real) real = dlsym(RTLD_NEXT, "fopen64");
	return real(fix(path), mode);
}

/* A process called NAME* ("gptokeyb", "gptokeyb2"), by comm. */
static int running(const char *name)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	size_t n = strlen(name);
	int found = 0;

	while (d && !found && (e = readdir(d))) {
		char path[300], comm[32] = "";
		FILE *f;

		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
		if ((f = fopen(path, "r"))) {
			if (fgets(comm, sizeof comm, f) && !strncmp(comm, name, n)) found = 1;
			fclose(f);
		}
	}
	if (d) closedir(d);
	return found;
}

/* "2:/dev/input/eventN" for each "Fake Keyboard", comma-separated; "" if none. */
static void fake_keyboards(char *out, size_t size)
{
	char path[96], name[64];
	size_t used = 0;
	int i;

	out[0] = 0;
	for (i = 0; i < 32; i++) {
		FILE *f;

		snprintf(path, sizeof path, "/sys/class/input/event%d/device/name", i);
		if (!(f = fopen(path, "r"))) continue;
		if (fgets(name, sizeof name, f) && !strncmp(name, "Fake Keyboard", 13) && used + 32 < size)
			used += (size_t)snprintf(out + used, size - used, "%s2:/dev/input/event%d", used ? "," : "", i);
		fclose(f);
	}
}

static void wait_for_keys(void)
{
	static int done;
	char list[256], comm[32] = "";
	FILE *f;
	int i;

	if (done) return;
	done = 1;
	if (getenv("SDL_EVDEV_DEVICES")) return;
	/* gptokeyb's own SDL has nothing to wait for. */
	if ((f = fopen("/proc/self/comm", "r"))) {
		if (!fgets(comm, sizeof comm, f)) comm[0] = 0;
		fclose(f);
	}
	if (!strncmp(comm, "gptokeyb", 8) || !running("gptokeyb")) return;
	for (i = 0; i < 150; i++) {
		fake_keyboards(list, sizeof list);
		if (list[0]) {
			setenv("SDL_EVDEV_DEVICES", list, 1);
			return;
		}
		usleep(20000);
	}
}

int SDL_Init(uint32_t flags)
{
	static int (*real)(uint32_t);

	if (!real) real = dlsym(RTLD_NEXT, "SDL_Init");
	wait_for_keys();
	return real(flags);
}

int SDL_InitSubSystem(uint32_t flags)
{
	static int (*real)(uint32_t);

	if (!real) real = dlsym(RTLD_NEXT, "SDL_InitSubSystem");
	wait_for_keys();
	return real(flags);
}
