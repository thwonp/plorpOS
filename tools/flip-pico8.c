/* SPDX-License-Identifier: MIT
 *
 * Preloaded into the owner's pico8_dyn (PICO-8's Raspberry Pi build, armhf
 * glibc + SDL2) on the Miyoo Mini Flip (plorpos-f1p.78). The Flip's glibc is
 * 2.28 and PICO-8 needs 2.17, so it runs on the card's own SDL2 - which is
 * offscreen/dummy only, with no sound driver (mk/fetch-miyoo-sysroot.sh). SDL
 * keeps PICO-8's window, renderer, timers and events; this does what the Flip
 * needs on top, as tools/nano-pico8.c does on the RG Nano:
 *
 * - The screen. The display is said to be fb0's own size (752x560), so
 *   PICO-8's Display flags (-draw_rect, -pixel_perfect: main.c p8_flags) fit
 *   it to the panel. Each frame PICO-8 presents - through its renderer or
 *   straight from the window surface - is copied onto the fb0 page NOT on
 *   glass, turned 180 degrees (the panel is mounted upside down), and panned
 *   to. FBIOPAN_DISPLAY waits for vblank here, so frames lock to the panel and
 *   none tears: the launcher's plat_present and diatom's flip do the same.
 * - The buttons, as fake08 has them under diatom: B is O, A is X, Start
 *   pauses, the d-pad moves. Read from the pad's own node, event0; Menu, the
 *   volume keys and power are the launcher's (plat_run freezes PICO-8 under
 *   its menu and grabs the pad meanwhile, so nothing leaks into the game).
 * - The sound. MI_AO has one owner, the resident diatom; PICO-8's callback is
 *   run by a thread here, its samples resampled to 48 kHz stereo and sent to
 *   diatom's game-sound socket, which feeds the same ring a core's sound
 *   does - so Muse's music plays instead of it, as over any game, and the
 *   volume keys work as in any game.
 *
 * PLORPOS_P8_TRACE=1 logs a present line every 300 frames (fps, copy and
 * vblank-wait ms); without it there is one summary line at exit.
 */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define PAD_NODE  "/dev/input/event0"
#define PCM_SOCK  "/tmp/diatom-pcm.sock"   /* diatom-miyoo port/miyoo.c */
#define OUT_RATE  48000

#define REAL(f) static __typeof__(f) *real_##f; \
	if (!real_##f) real_##f = (__typeof__(f) *)dlsym(RTLD_NEXT, #f)

static long long now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}

/* ---------------------------------------------------------------- screen */

static int fb = -1;
static uint8_t *fbmem;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;
static int fb_w = 752, fb_h = 560;     /* until fb0 says */
static SDL_Window *win;
static int trace;

/* Stats for the trace and the exit line: frames PICO-8 presented, flips
 * that reached the glass, frames replaced before they did, copy time. */
static unsigned st_n, st_all, st_flips, st_drop, st_drop_all;
static long long st_copy, st_t0, st_first, st_last;

/* The flip thread. fb0 has three pages: one on glass (front), one the thread
 * is panning to (panning), one waiting for it (queued); PICO-8's next frame
 * is drawn into the one left. FBIOPAN_DISPLAY blocks until vblank, so it is
 * done here and PICO-8 never waits for the panel - a 60 fps cart drew at 36
 * with the wait in its own loop. When all three are taken, the queued frame,
 * not yet shown, is taken back and drawn over (a drop). */
static pthread_mutex_t fmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fcv = PTHREAD_COND_INITIALIZER;
static int f_front, f_panning = -1, f_queued = -1, f_pages, f_thread;

static void *flip_thread(void *u)
{
	struct fb_var_screeninfo v = var;
	(void)u;

	for (;;) {
		int p;

		pthread_mutex_lock(&fmx);
		while (f_queued < 0) pthread_cond_wait(&fcv, &fmx);
		p = f_panning = f_queued;
		f_queued = -1;
		pthread_mutex_unlock(&fmx);
		v.yoffset = (unsigned)p * var.yres;
		if (ioctl(fb, FBIOPAN_DISPLAY, &v) != 0) ioctl(fb, FBIOPUT_VSCREENINFO, &v);
		pthread_mutex_lock(&fmx);
		f_front = p;
		f_panning = -1;
		st_flips++;
		pthread_mutex_unlock(&fmx);
	}
	return NULL;
}

static void fb_open(void)
{
	void *m;
	pthread_t th;

	if (fb >= 0) return;
	fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fb < 0 || ioctl(fb, FBIOGET_FSCREENINFO, &fix) != 0 ||
	    ioctl(fb, FBIOGET_VSCREENINFO, &var) != 0 || var.bits_per_pixel != 32) {
		fprintf(stderr, "flip-pico8: no 32 bpp fb0\n");
		return;
	}
	m = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if (m == MAP_FAILED) return;
	fbmem = m;
	fb_w = (int)var.xres;
	fb_h = (int)var.yres;
	f_pages = var.yres ? (int)(var.yres_virtual / var.yres) : 1;
	if (f_pages > 3) f_pages = 3;
	if ((size_t)f_pages * var.yres * fix.line_length > fix.smem_len) f_pages = 1;
	f_front = var.yres ? (int)(var.yoffset / var.yres) : 0;
	if (f_front >= f_pages) f_front = 0;
	if (f_pages == 3 && pthread_create(&th, NULL, flip_thread, NULL) == 0) {
		pthread_detach(th);
		f_thread = 1;
	}
}

/* The window surface onto a free page, turned 180 degrees (the panel is
 * mounted upside down), queued for the flip thread. */
static void show(SDL_Surface *s)
{
	int page = 0, w, h;
	long long t0, t1;

	fb_open();
	if (!fbmem || !s || s->format->BytesPerPixel != 4) return;
	w = s->w < fb_w ? s->w : fb_w;
	h = s->h < fb_h ? s->h : fb_h;
	pthread_mutex_lock(&fmx);
	/* Back from a gap - the launcher's menu, drawn and panned while this
	 * process was frozen: the page on glass is whichever it left. */
	if (f_thread && f_panning < 0 && f_queued < 0 && st_last && now_us() - st_last > 200000) {
		struct fb_var_screeninfo v;

		if (ioctl(fb, FBIOGET_VSCREENINFO, &v) == 0 && v.yres && (int)(v.yoffset / v.yres) < 3)
			f_front = (int)(v.yoffset / v.yres);
	}
	if (f_thread) {
		for (page = 0; page < 3; page++)
			if (page != f_front && page != f_panning && page != f_queued) break;
		if (page == 3) {          /* all taken: the queued frame was never shown */
			page = f_queued;
			f_queued = -1;
			st_drop++;
			st_drop_all++;
		}
	} else {
		page = f_front;            /* fewer than three pages: on glass, as it comes */
	}
	pthread_mutex_unlock(&fmx);
	t0 = now_us();
	for (int y = 0; y < h; y++) {
		const uint32_t *src = (const uint32_t *)((const uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch);
		uint32_t *d = (uint32_t *)(fbmem + ((size_t)page * var.yres + (size_t)(fb_h - 1 - y)) *
		                           fix.line_length) + (fb_w - 1);

		for (int x = 0; x < w; x++) *d-- = src[x];
	}
	t1 = now_us();
	if (f_thread) {
		pthread_mutex_lock(&fmx);
		f_queued = page;
		pthread_cond_signal(&fcv);
		pthread_mutex_unlock(&fmx);
	}
	st_copy += t1 - t0;
	st_last = t1;
	if (!st_first) st_first = t0;
	st_all++;
	if (!st_t0) st_t0 = t0;
	if (++st_n == 300) {
		unsigned flips;

		pthread_mutex_lock(&fmx);
		flips = st_flips;
		st_flips = 0;
		pthread_mutex_unlock(&fmx);
		if (trace)
			fprintf(stderr, "flip-pico8: present %.2f fps, %u flips, %u replaced unseen, copy %.2f ms (%dx%d)\n",
			        300 * 1e6 / (double)(t1 - st_t0), flips, st_drop, st_copy / 300e3, s->w, s->h);
		st_n = 0;
		st_drop = 0;
		st_copy = 0;
		st_t0 = t1;
	}
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
	REAL(SDL_CreateWindow);

	fb_open();
	(void)w; (void)h;
	win = real_SDL_CreateWindow(title, x, y, fb_w, fb_h,
	                            flags & ~(Uint32)(SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP));
	return win;
}

int SDL_UpdateWindowSurface(SDL_Window *w)
{
	REAL(SDL_UpdateWindowSurface);
	int r = real_SDL_UpdateWindowSurface(w);

	win = w;
	show(SDL_GetWindowSurface(w));
	return r;
}

/* The software renderer draws into the window surface, so after its present
 * that surface is the frame. */
void SDL_RenderPresent(SDL_Renderer *r)
{
	REAL(SDL_RenderPresent);
	SDL_Window *w = SDL_RenderGetWindow(r);

	real_SDL_RenderPresent(r);
	if (w) {
		win = w;
		show(SDL_GetWindowSurface(w));
	}
}

static void say_fb(SDL_DisplayMode *m) { fb_open(); m->w = fb_w; m->h = fb_h; }

int SDL_GetDesktopDisplayMode(int i, SDL_DisplayMode *m)
{
	REAL(SDL_GetDesktopDisplayMode);
	int r = real_SDL_GetDesktopDisplayMode(i, m);

	if (r == 0) say_fb(m);
	return r;
}

int SDL_GetCurrentDisplayMode(int i, SDL_DisplayMode *m)
{
	REAL(SDL_GetCurrentDisplayMode);
	int r = real_SDL_GetCurrentDisplayMode(i, m);

	if (r == 0) say_fb(m);
	return r;
}

int SDL_GetDisplayMode(int i, int k, SDL_DisplayMode *m)
{
	REAL(SDL_GetDisplayMode);
	int r = real_SDL_GetDisplayMode(i, k, m);

	if (r == 0) say_fb(m);
	return r;
}

int SDL_GetDisplayBounds(int i, SDL_Rect *b)
{
	REAL(SDL_GetDisplayBounds);
	int r = real_SDL_GetDisplayBounds(i, b);

	if (r == 0) { fb_open(); b->x = b->y = 0; b->w = fb_w; b->h = fb_h; }
	return r;
}

/* -------------------------------------------------------------- buttons */

static SDL_Scancode map(unsigned code)
{
	switch (code) {   /* the Flip's keyboard codes: platform_miyoo.c padmap */
	case KEY_UP:        return SDL_SCANCODE_UP;
	case KEY_DOWN:      return SDL_SCANCODE_DOWN;
	case KEY_LEFT:      return SDL_SCANCODE_LEFT;
	case KEY_RIGHT:     return SDL_SCANCODE_RIGHT;
	case KEY_LEFTCTRL:  return SDL_SCANCODE_Z;        /* B: O */
	case KEY_SPACE:     return SDL_SCANCODE_X;        /* A: X */
	case KEY_ENTER:     return SDL_SCANCODE_RETURN;   /* Start: pause */
	default:            return SDL_SCANCODE_UNKNOWN;
	}
}

static Uint8 keys[SDL_NUM_SCANCODES];
static int pad = -2;                   /* -2: not opened yet */

const Uint8 *SDL_GetKeyboardState(int *n)
{
	if (n) *n = SDL_NUM_SCANCODES;
	return keys;
}

/* The dummy window never gets focus, and PICO-8 reads no keys without it. */
SDL_Window *SDL_GetKeyboardFocus(void)
{
	REAL(SDL_GetKeyboardFocus);
	SDL_Window *f = real_SDL_GetKeyboardFocus();

	return f ? f : win;
}

/* One button from event0 as an SDL event, or 0 when none is waiting. While
 * the launcher's menu is up it holds the pad (EVIOCGRAB) and this process is
 * frozen; what was held then is let go here on the way back. */
static int key_event(SDL_Event *e)
{
	struct input_event ev;

	if (pad == -2) {
		pad = open(PAD_NODE, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (pad < 0) fprintf(stderr, "flip-pico8: %s: %s\n", PAD_NODE, strerror(errno));
	}
	while (pad >= 0 && read(pad, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		SDL_Scancode to;

		if (ev.type != EV_KEY || ev.value == 2) continue;   /* no autorepeat */
		to = map(ev.code);
		if (to == SDL_SCANCODE_UNKNOWN) continue;
		if (keys[to] == (ev.value ? 1 : 0)) continue;
		keys[to] = ev.value ? 1 : 0;
		memset(e, 0, sizeof *e);
		e->type = ev.value ? SDL_KEYDOWN : SDL_KEYUP;
		e->key.state = ev.value ? SDL_PRESSED : SDL_RELEASED;
		e->key.windowID = win ? SDL_GetWindowID(win) : 0;
		e->key.keysym.scancode = to;
		e->key.keysym.sym = SDL_GetKeyFromScancode(to);
		return 1;
	}
	return 0;
}

/* Keys held when the launcher froze PICO-8 for its menu may have been let go
 * under the grab: released here when the real state says so. */
static void key_resync(SDL_Event *e, int *have)
{
	unsigned long bits[KEY_MAX / (8 * sizeof(unsigned long)) + 1];
	static long long last;
	long long t = now_us();

	if (*have || pad < 0 || t - last < 250000) return;
	last = t;
	memset(bits, 0, sizeof bits);
	if (ioctl(pad, EVIOCGKEY(sizeof bits), bits) < 0) return;
	static const unsigned codes[] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	                                  KEY_LEFTCTRL, KEY_SPACE, KEY_ENTER };
	for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
		SDL_Scancode to = map(codes[i]);
		int down = (bits[codes[i] / (8 * sizeof bits[0])] >> (codes[i] % (8 * sizeof bits[0]))) & 1;

		if (keys[to] && !down) {
			keys[to] = 0;
			memset(e, 0, sizeof *e);
			e->type = SDL_KEYUP;
			e->key.state = SDL_RELEASED;
			e->key.windowID = win ? SDL_GetWindowID(win) : 0;
			e->key.keysym.scancode = to;
			e->key.keysym.sym = SDL_GetKeyFromScancode(to);
			*have = 1;
			return;
		}
	}
}

int SDL_PollEvent(SDL_Event *e)
{
	REAL(SDL_PollEvent);
	int have;

	if (!e) return real_SDL_PollEvent(e);
	have = key_event(e);
	key_resync(e, &have);
	return have || real_SDL_PollEvent(e);
}

/* ---------------------------------------------------------------- sound */

static SDL_AudioSpec spec;              /* what PICO-8 asked for, and has */
static pthread_mutex_t amx = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_t ath;
static volatile int a_on, a_paused = 1, a_stop;
static unsigned a_sent_ms, a_drop_ms, a_fail;
static int sock = -1;

static int sock_open(void)
{
	struct sockaddr_un a;
	int fd, sz = 4096;

	if ((fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) return -1;
	memset(&a, 0, sizeof a);
	a.sun_family = AF_UNIX;
	snprintf(a.sun_path, sizeof a.sun_path, "%s", PCM_SOCK);
	if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
	return fd;
}

/* One sample of PICO-8's, as a float in -1..1. */
static float sample_at(const uint8_t *buf, int i)
{
	switch (spec.format) {
	case AUDIO_S16SYS: return ((const int16_t *)buf)[i] / 32768.0f;
	case AUDIO_F32SYS: return ((const float *)buf)[i];
	case AUDIO_U8:     return (buf[i] - 128) / 128.0f;
	case AUDIO_S8:     return ((const int8_t *)buf)[i] / 128.0f;
	default:           return 0.0f;
	}
}

/* Sends what is there, waiting as diatom drains (its backpressure is the
 * clock); 0 if diatom is not there, so the caller paces itself. */
static int send_all(const int16_t *fr, size_t n)
{
	const char *p = (const char *)fr;
	size_t left = n * 4;

	if (sock < 0) {
		static long long next;
		long long t = now_us();

		if (t < next) return 0;
		next = t + 1000000;
		if ((sock = sock_open()) < 0) {
			if (!a_fail++) fprintf(stderr, "flip-pico8: no %s: %s\n", PCM_SOCK, strerror(errno));
			return 0;
		}
	}
	while (left) {
		ssize_t w = send(sock, p, left, MSG_NOSIGNAL);

		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) { close(sock); sock = -1; return 0; }
		p += w;
		left -= (size_t)w;
	}
	return 1;
}

static void *audio_thread(void *u)
{
	int ch = spec.channels ? spec.channels : 1;
	int bps = SDL_AUDIO_BITSIZE(spec.format) / 8;
	int frames = spec.samples ? spec.samples : 1024;
	int len = frames * ch * bps;
	uint8_t *buf = malloc((size_t)len);
	/* Out: as many 48 kHz frames as `frames` in, plus one for the carry. */
	size_t cap = (size_t)frames * OUT_RATE / (size_t)(spec.freq > 0 ? spec.freq : OUT_RATE) + 4;
	int16_t *out = malloc(cap * 4);
	double step = (double)(spec.freq > 0 ? spec.freq : OUT_RATE) / OUT_RATE, pos = 0;
	float prev[2] = { 0, 0 };
	(void)u;

	if (!buf || !out) { free(buf); free(out); return NULL; }
	while (!a_stop) {
		long long t0 = now_us();
		size_t n = 0;
		int sent;

		if (a_paused) { usleep(10000); continue; }
		pthread_mutex_lock(&amx);
		spec.callback(spec.userdata, buf, len);
		pthread_mutex_unlock(&amx);
		/* Linear from PICO-8's rate to 48 kHz; mono to both sides. `pos` runs
		 * from -1 (prev, the last frame of the previous buffer) to frames-1. */
		while (pos < frames - 1 + 1e-9 && n < cap) {
			int i = (int)(pos < 0 ? -1 : pos);
			float f = (float)(pos - i);

			for (int c = 0; c < 2; c++) {
				int cc = c < ch ? c : 0;
				float a = i < 0 ? prev[c] : sample_at(buf, i * ch + cc);
				float b = sample_at(buf, (i + 1) * ch + cc);
				float v = a + (b - a) * f;

				out[2 * n + (size_t)c] = (int16_t)(v >= 1.0f ? 32767 : v <= -1.0f ? -32768 : v * 32767.0f);
			}
			n++;
			pos += step;
		}
		pos -= frames;
		for (int c = 0; c < 2; c++) prev[c] = sample_at(buf, (frames - 1) * ch + (c < ch ? c : 0));
		sent = send_all(out, n);
		if (sent) a_sent_ms += (unsigned)(n * 1000 / OUT_RATE);
		else {
			/* Nobody to take it: keep PICO-8's time anyway. */
			long long due = t0 + (long long)frames * 1000000 / (spec.freq > 0 ? spec.freq : OUT_RATE);
			long long t = now_us();

			a_drop_ms += (unsigned)(n * 1000 / OUT_RATE);
			if (due > t) usleep((useconds_t)(due - t));
		}
	}
	free(buf);
	free(out);
	return NULL;
}

int SDL_OpenAudio(SDL_AudioSpec *d, SDL_AudioSpec *o)
{
	if (!d || !d->callback) return -1;
	if (a_on) { SDL_SetError("Audio device is already opened"); return -1; }
	spec = *d;
	if (spec.samples == 0) spec.samples = 1024;
	spec.silence = spec.format == AUDIO_U8 ? 0x80 : 0;
	spec.size = (Uint32)spec.samples * spec.channels * (SDL_AUDIO_BITSIZE(spec.format) / 8);
	if (o) *o = spec;
	fprintf(stderr, "flip-pico8: sound %d Hz, %d ch, format 0x%x, %d frames -> %s\n",
	        spec.freq, spec.channels, spec.format, spec.samples, PCM_SOCK);
	a_stop = 0;
	a_paused = 1;
	if (pthread_create(&ath, NULL, audio_thread, NULL) != 0) return -1;
	a_on = 1;
	return 0;
}

void SDL_PauseAudio(int on) { a_paused = on; }

SDL_AudioStatus SDL_GetAudioStatus(void)
{
	return !a_on ? SDL_AUDIO_STOPPED : a_paused ? SDL_AUDIO_PAUSED : SDL_AUDIO_PLAYING;
}

void SDL_LockAudio(void) { pthread_mutex_lock(&amx); }
void SDL_UnlockAudio(void) { pthread_mutex_unlock(&amx); }

void SDL_CloseAudio(void)
{
	if (!a_on) return;
	a_stop = 1;
	pthread_join(ath, NULL);
	a_on = 0;
	if (sock >= 0) { close(sock); sock = -1; }
}

/* ------------------------------------------------------------- lifetime */

__attribute__((constructor)) static void init(void)
{
	const char *t = getenv("PLORPOS_P8_TRACE");

	/* Before PICO-8's SDL_Init: both drivers are demand-only, and without
	 * an audio driver PICO-8 would never open its sound. */
	setenv("SDL_VIDEODRIVER", "dummy", 0);
	setenv("SDL_AUDIODRIVER", "dummy", 0);

	trace = t && *t == '1';
}

__attribute__((destructor)) static void fini(void)
{
	long long el = st_first ? now_us() - st_first : 0;

	if (!st_all && !a_sent_ms && !a_drop_ms) return;   /* not PICO-8, or it never ran */
	fprintf(stderr, "flip-pico8: %u frames in %.1f s (%.2f fps), %u replaced unseen, sound sent %u ms, dropped %u ms\n",
	        st_all, el / 1e6, el ? st_all * 1e6 / (double)el : 0.0, st_drop_all, a_sent_ms, a_drop_ms);
}

