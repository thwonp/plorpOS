/* SPDX-License-Identifier: MIT */
/* Zero 40, 32-bit PortMaster ports only (plorpos-auv.7): the GE8300 has no
 * 32-bit driver, so an armhf port renders with Mesa's software GL into an
 * offscreen surface (SDL_VIDEODRIVER=offscreen) and nothing reaches the
 * panel. Preloaded into the port (sd/zero40/portmaster/pm-run.sh), this
 * reads each finished frame back at eglSwapBuffers and copies it into the
 * framebuffer page on glass: fit to the panel's width (or height), centred,
 * nearest neighbour - a landscape frame on the portrait panel is a band
 * across its middle, as Diatom draws a game.
 *
 * Both ways a frame ends: SDL_GL_SwapWindow (SDL reaches EGL through its
 * own dlopen, past any preload) and eglSwapBuffers for a port on EGL itself.
 * Everything through dlsym: preloaded into every process of the port's
 * script (bash, tee, gptokeyb), most of which never touch GL. */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Only a 32-bit process loads this (LD_PRELOAD through $LIB; the 64-bit
 * side finds an empty stub): what Mesa and SDL need, said here so that no
 * 64-bit program sees it. */
__attribute__((constructor)) static void setup(void)
{
	const char *r = "/tmp/plorpos-lib32";
	char p[256];

	/* Not for a client of Westonpack's X11 or Wayland: its SDL talks to
	 * that, and Westonpack's own presenter (Crusty) picks its driver. */
	if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY"))
		setenv("SDL_VIDEODRIVER", "offscreen", 1);
	/* An offscreen window never has focus, and SDL drops a pad's events
	 * while its window has none. */
	setenv("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1", 1);
	snprintf(p, sizeof p, "%s/lib/arm-linux-gnueabihf/dri", r);
	setenv("LIBGL_DRIVERS_PATH", p, 1);
	snprintf(p, sizeof p, "%s/share/glvnd/egl_vendor.d/50_mesa.json", r);
	setenv("__EGL_VENDOR_LIBRARY_FILENAMES", p, 1);
	setenv("GALLIUM_DRIVER", "llvmpipe", 1);
}

static unsigned long frames;
static struct timespec t_first;

static double since(const struct timespec *t)
{
	struct timespec n;

	clock_gettime(CLOCK_MONOTONIC, &n);
	return (double)(n.tv_sec - t->tv_sec) + (double)(n.tv_nsec - t->tv_nsec) / 1e9;
}

/* One line per run: how fast software GL kept up. */
__attribute__((destructor)) static void summary(void)
{
	double s;

	if (frames < 2) return;
	s = since(&t_first);
	fprintf(stderr, "fbpresent32: %lu frames in %.1f s (%.1f fps)\n", frames, s, s > 0 ? (double)(frames - 1) / s : 0);
}

static int fb = -1;
static uint8_t *fbmem;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var0;
static uint32_t *frame;
static size_t frame_px;
static int *xmap;
static int xmap_w, xmap_dw;

static int fb_open(void)
{
	if (fb >= 0) return fbmem != NULL;
	fb = open("/dev/fb0", O_RDWR);
	if (fb < 0 || ioctl(fb, FBIOGET_FSCREENINFO, &fix) || ioctl(fb, FBIOGET_VSCREENINFO, &var0) ||
	    var0.bits_per_pixel != 32) {
		fprintf(stderr, "fbpresent32: no 32-bit fb0\n");
		return 0;
	}
	fbmem = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if (fbmem == MAP_FAILED) fbmem = NULL;
	if (fbmem) {
		/* The panel black once, every page: what the frame leaves uncovered. */
		memset(fbmem, 0, fix.smem_len);
		fprintf(stderr, "fbpresent32: %ux%u panel, frames copied from the GL surface\n",
		        var0.xres, var0.yres);
	}
	return fbmem != NULL;
}

typedef void (*readpx_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);

static void present(int w, int h, readpx_fn readpx)
{
	struct fb_var_screeninfo v;
	int dw, dh, x0, y0, y;

	if (!readpx || w <= 0 || h <= 0 || !fb_open()) return;
	if (!frames++) clock_gettime(CLOCK_MONOTONIC, &t_first);
	/* The rate so far, every 5 s, in one line on tmpfs (no card writes): a
	 * port quit from the menu ends on SIGTERM, before any destructor. */
	if (frames % 32 == 0) {
		static double last;
		double t = since(&t_first);

		if (t - last >= 5) {
			FILE *f = fopen("/tmp/plorpos32.fps", "w");

			last = t;
			if (f) {
				fprintf(f, "%lu frames in %.1f s (%.1f fps)\n", frames, t, (double)(frames - 1) / t);
				fclose(f);
			}
		}
	}
	if ((size_t)w * (size_t)h > frame_px) {
		free(frame);
		frame_px = (size_t)w * (size_t)h;
		if (!(frame = malloc(frame_px * 4))) { frame_px = 0; return; }
	}
	readpx(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, frame);
	if (ioctl(fb, FBIOGET_VSCREENINFO, &v)) return;
	/* Fit: width-bound for a frame wider than the panel's shape. */
	if ((int64_t)w * v.yres >= (int64_t)h * v.xres) {
		dw = (int)v.xres;
		dh = (int)((int64_t)h * v.xres / w);
	} else {
		dh = (int)v.yres;
		dw = (int)((int64_t)w * v.yres / h);
	}
	if (dw <= 0 || dh <= 0) return;
	x0 = ((int)v.xres - dw) / 2;
	y0 = ((int)v.yres - dh) / 2;
	if (xmap_w != w || xmap_dw != dw) {
		free(xmap);
		if (!(xmap = malloc((size_t)dw * sizeof *xmap))) { xmap_w = 0; return; }
		for (int x = 0; x < dw; x++) xmap[x] = (int)((int64_t)x * w / dw);
		xmap_w = w;
		xmap_dw = dw;
	}
	for (y = 0; y < dh; y++) {
		/* GL's rows run bottom up. */
		const uint32_t *src = frame + (size_t)(h - 1 - (int)((int64_t)y * h / dh)) * (size_t)w;
		uint32_t *dst = (uint32_t *)(fbmem + ((size_t)v.yoffset + (size_t)(y0 + y)) * fix.line_length) +
		                v.xoffset + x0;
		if ((size_t)((uint8_t *)(dst + dw) - fbmem) > fix.smem_len) return;
		for (int x = 0; x < dw; x++) {
			uint32_t p = src[xmap[x]];           /* R G B A in memory */
			dst[x] = 0xff000000u | (p & 0xff) << 16 | (p & 0xff00) | (p >> 16 & 0xff);
		}
	}
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf)
{
	static EGLBoolean (*real)(EGLDisplay, EGLSurface);
	EGLBoolean (*query)(EGLDisplay, EGLSurface, EGLint, EGLint *) =
		(EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint, EGLint *))dlsym(RTLD_NEXT, "eglQuerySurface");
	EGLint w = 0, h = 0;

	if (!real) real = (EGLBoolean (*)(EGLDisplay, EGLSurface))dlsym(RTLD_NEXT, "eglSwapBuffers");
	if (query && query(dpy, surf, EGL_WIDTH, &w) && query(dpy, surf, EGL_HEIGHT, &h))
		present(w, h, (readpx_fn)dlsym(RTLD_DEFAULT, "glReadPixels"));
	return real ? real(dpy, surf) : EGL_FALSE;
}

/* SDL's, declared here: no SDL headers needed for two calls. */
typedef struct SDL_Window SDL_Window;

void SDL_GL_SwapWindow(SDL_Window *win)
{
	static void (*real)(SDL_Window *);
	static void (*size)(SDL_Window *, int *, int *);
	static void *(*proc)(const char *);
	static readpx_fn readpx;
	int w = 0, h = 0;

	if (!real) {
		real = (void (*)(SDL_Window *))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
		size = (void (*)(SDL_Window *, int *, int *))dlsym(RTLD_NEXT, "SDL_GL_GetDrawableSize");
		proc = (void *(*)(const char *))dlsym(RTLD_NEXT, "SDL_GL_GetProcAddress");
	}
	if (!readpx && proc) readpx = (readpx_fn)proc("glReadPixels");
	if (size) size(win, &w, &h);
	present(w, h, readpx);
	if (real) real(win);
}
