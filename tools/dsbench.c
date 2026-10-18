/* SPDX-License-Identifier: MIT */
/* dsbench CORE ROM WARMUP FRAMES SHOT.ppm [key=value ...] - a libretro core run
 * unthrottled on the device, outside Diatom: per-frame retro_run percentiles,
 * scripted input, core options from the command line, a PPM of the last frame.
 * Written for the DS spike and the melonDS work (plorpos-3yv.44, .51); the PGO
 * profile is trained with it (tools/ds-pgo-train.sh).
 *
 * Environment:
 *   BENCH_NOINPUT=1   no input (attract modes)
 *   BENCH_OPTS=1      dump the core's options;  BENCH_LOG=1  every log level
 *   BENCH_STATE=file  unserialize a state right after the load (a Diatom state
 *                     works: the payload starts at melonDS's "MELN")
 *   BENCH_RT=1        serialize, run 120 frames, unserialize, run them again:
 *                     the two frame hashes must match
 *   BENCH_SER=N       time N serializes (rewind's capture cost)
 *   BENCH_RELOAD=N    unload and load the game N more times
 *   BENCH_EXIT=1      exit() rather than _exit(): destructors run, so an
 *                     instrumented core writes its .gcda files
 *
 * Build: docker run --rm -v $PWD:/w -w /w tortos-toolchain \
 *          aarch64-linux-gnu-gcc -O2 -Wall tools/dsbench.c -ldl -o dsbench */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_variable { const char *key; const char *value; };
struct retro_system_av_info { struct { unsigned bw, bh, mw, mh; float ar; } g; struct { double fps, rate; } t; };
struct retro_log_callback { void (*log)(int, const char *, ...); };

static int fmt = 0;            /* 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565 */
static unsigned vw, vh; static size_t vpitch; static uint8_t *last; static size_t lastsz;
static long frame, samples;
static char *opts[64]; static int nopts;
static char seen[8192];

static void logf_(int lvl, const char *f, ...)
{ if (lvl < (getenv("BENCH_LOG") ? 0 : 2)) return; va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }

static const char *opt(const char *k)
{
	for (int i = 0; i < nopts; i++) {
		size_t n = strlen(k);
		if (!strncmp(opts[i], k, n) && opts[i][n] == '=') return opts[i] + n + 1;
	}
	return NULL;
}

static bool env(unsigned cmd, void *d)
{
	switch (cmd & 0xffff) {
	case 3: *(bool *)d = true; return true;                     /* CAN_DUPE */
	case 9: case 31: *(const char **)d = cmd == 9 ? "/mnt/SDCARD/Bios" : "/tmp/dsb"; return true;
	case 10: fmt = *(int *)d; return true;                      /* SET_PIXEL_FORMAT */
	case 15: {                                                  /* GET_VARIABLE */
		struct retro_variable *v = d;
		if (!strstr(seen, v->key) && strlen(seen) + strlen(v->key) + 2 < sizeof seen) {
			strcat(seen, v->key); strcat(seen, " ");
		}
		v->value = opt(v->key);
		return v->value != NULL;
	}
	case 16: {                                                  /* SET_VARIABLES: dump */
		for (const struct retro_variable *v = d; v->key; v++)
			if (getenv("BENCH_OPTS")) fprintf(stderr, "OPT %s = %s\n", v->key, v->value);
		return true;
	}
	case 17: { static int once; *(bool *)d = !once++; return true; }
	case 27: ((struct retro_log_callback *)d)->log = logf_; return true;
	case 51: return true;
	case 24: *(uint64_t *)d = (1 << 1) | (1 << 6); return true;  /* JOYPAD + POINTER */                                       /* INPUT_BITMASKS */
	case 52: *(unsigned *)d = 0; return true;                   /* CORE_OPTIONS_VERSION */
	case 14: return false;                                      /* SET_HW_RENDER: no GL */
	default: return false;
	}
}

static void video(const void *p, unsigned w, unsigned h, size_t pitch)
{
	if (!p) return;
	vw = w; vh = h; vpitch = pitch;
	if (lastsz < pitch * h) { last = realloc(last, pitch * h); lastsz = pitch * h; }
	memcpy(last, p, pitch * h);
}
static void audio1(int16_t l, int16_t r) { samples++; }
static size_t audiob(const int16_t *d, size_t n) { samples += n; return n; }
static void poll_(void) {}
static int16_t input(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
	/* A every 45 frames (6 frames held), Start every 400; touch none. */
	int a = frame % 45 < 6, st = frame % 400 >= 200 && frame % 400 < 206;
	int16_t mask = (a << 8) | (st << 3);
	if (port || getenv("BENCH_NOINPUT")) return 0;
	if (dev == 1) return id == 256 ? mask : (mask >> id) & 1;
	if (dev == 6) {   /* POINTER: tap the bottom screen, wandering, every 60 frames */
		int down = frame % 60 >= 30 && frame % 60 < 36, k = (int)(frame / 60) % 5;
		static const short xs[5] = { 0, -16000, 16000, -8000, 8000 };
		static const short ys[5] = { 16000, 26000, 26000, 12000, 20000 };
		return id == 0 ? xs[k] : id == 1 ? ys[k] : id == 2 ? down : 0;
	}
	return 0;
}

static uint64_t fhash(void)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < vpitch * vh; i++) h = (h ^ last[i]) * 1099511628211ull;
	return h;
}
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

int main(int argc, char **argv)
{
	if (argc < 6) { fprintf(stderr, "bench CORE ROM WARMUP FRAMES SHOT [k=v...]\n"); return 2; }
	for (int i = 6; i < argc && nopts < 64; i++) opts[nopts++] = argv[i];
	mkdir("/tmp/dsb", 0755);
	void *h = dlopen(argv[1], RTLD_NOW);
	if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
#define S(n) void *n = dlsym(h, #n)
	S(retro_set_environment); S(retro_set_video_refresh); S(retro_set_audio_sample);
	S(retro_set_audio_sample_batch); S(retro_set_input_poll); S(retro_set_input_state);
	S(retro_init); S(retro_load_game); S(retro_run); S(retro_get_system_av_info);
	((void (*)(void *))retro_set_environment)(env);
	((void (*)(void *))retro_set_video_refresh)(video);
	((void (*)(void *))retro_set_audio_sample)(audio1);
	((void (*)(void *))retro_set_audio_sample_batch)(audiob);
	((void (*)(void *))retro_set_input_poll)(poll_);
	((void (*)(void *))retro_set_input_state)(input);
	((void (*)(void))retro_init)();
	struct retro_game_info gi = { argv[2], NULL, 0, NULL };
	FILE *f = fopen(argv[2], "rb");
	fseek(f, 0, SEEK_END); gi.size = ftell(f); rewind(f);
	gi.data = malloc(gi.size); fread((void *)gi.data, 1, gi.size, f); fclose(f);
	if (!((bool (*)(void *))retro_load_game)(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
	/* BENCH_STATE=file: unserialize a Diatom state (or a raw melonDS one) - the
	 * payload starts at the "MELN" magic - right after the load. */
	if (getenv("BENCH_STATE")) {
		S(retro_unserialize);
		FILE *sf = fopen(getenv("BENCH_STATE"), "rb");
		fseek(sf, 0, SEEK_END); long sl = ftell(sf); rewind(sf);
		uint8_t *sb = malloc(sl); fread(sb, 1, sl, sf); fclose(sf);
		long off = 0; while (off + 4 <= sl && memcmp(sb + off, "MELN", 4)) off++;
		bool ok = ((bool (*)(const void *, size_t))retro_unserialize)(sb + off, sl - off);
		printf("state %s (%ld bytes): %s\n", getenv("BENCH_STATE"), sl - off, ok ? "loaded" : "REFUSED");
	}
	struct retro_system_av_info av;
	((void (*)(void *))retro_get_system_av_info)(&av);
	long warm = atol(argv[3]), n = atol(argv[4]);
	void (*run)(void) = (void (*)(void))retro_run;
	double t0 = now();
	for (; frame < warm; frame++) run();
	double tw = now() - t0;
	double *ms = malloc(n * sizeof *ms), sum = 0;
	samples = 0;
	for (long i = 0; i < n; i++, frame++) { double a = now(); run(); ms[i] = now() - a; sum += ms[i]; }
	qsort(ms, n, sizeof *ms, cmp);
	double avg = sum / n;
	printf("core fps %.3f rate %.0f | video %ux%u fmt %d | warmup %ld frames %.1f s\n",
	       av.t.fps, av.t.rate, vw, vh, fmt, warm, tw / 1e3);
	printf("ms/frame avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f | unthrottled %.0f fps | audio %.0f/frame\n",
	       avg, ms[n / 2], ms[n * 95 / 100], ms[n * 99 / 100], ms[n - 1], 1e3 / avg,
	       (double)samples / n);
	/* BENCH_RELOAD=N: unload and load the same game N more times, 120 frames each
	 * (the resident's second-DS-game death, 2026-10-18). */
	for (int k = 0, nk = getenv("BENCH_RELOAD") ? atoi(getenv("BENCH_RELOAD")) : 0; k < nk; k++) {
		S(retro_unload_game);
		((void (*)(void))retro_unload_game)();
		fprintf(stderr, "reload %d: unloaded\n", k); fflush(stderr);
		if (!((bool (*)(void *))retro_load_game)(&gi)) { fprintf(stderr, "reload %d: load failed\n", k); return 1; }
		fprintf(stderr, "reload %d: loaded\n", k); fflush(stderr);
		for (int i = 0; i < 120; i++) run();
		fprintf(stderr, "reload %d: ran 120 frames\n", k); fflush(stderr);
	}
	/* BENCH_RT=1: serialize, hash 120 frames, unserialize, hash them again -
	 * a state that misses anything shows as a different hash. */
	if (getenv("BENCH_RT")) {
		S(retro_serialize_size); S(retro_serialize); S(retro_unserialize);
		size_t sz = ((size_t (*)(void))retro_serialize_size)();
		void *st = malloc(sz);
		bool sok = ((bool (*)(void *, size_t))retro_serialize)(st, sz);
		uint64_t h1 = 0, h2 = 0;
		for (int i = 0; i < 120; i++) { run(); h1 = h1 * 31 + fhash(); }
		bool uok = ((bool (*)(const void *, size_t))retro_unserialize)(st, sz);
		for (int i = 0; i < 120; i++) { run(); h2 = h2 * 31 + fhash(); }
		printf("roundtrip %zu bytes: serialize %s unserialize %s, 120 frames %s (%016llx %016llx)\n",
		       sz, sok ? "ok" : "FAIL", uok ? "ok" : "FAIL", h1 == h2 ? "IDENTICAL" : "DIFFER",
		       (unsigned long long)h1, (unsigned long long)h2);
	}
	/* BENCH_SER=N: time retro_serialize N times (rewind's capture cost). */
	if (getenv("BENCH_SER")) {
		S(retro_serialize_size); S(retro_serialize);
		size_t sz = ((size_t (*)(void))retro_serialize_size)();
		void *buf = malloc(sz); double t = 0, mx = 0;
		int ns = atoi(getenv("BENCH_SER"));
		for (int i = 0; i < ns; i++) {
			run();
			double a = now(); ((bool (*)(void *, size_t))retro_serialize)(buf, sz); double d = now() - a;
			t += d; if (d > mx) mx = d;
		}
		printf("serialize %zu bytes: avg %.2f max %.2f ms over %d\n", sz, t / ns, mx, ns);
	}
	long over = 0; for (long i = 0; i < n; i++) over += ms[i] > 16.74;
	printf("frames over 16.74 ms: %ld of %ld (%.1f%%)\n", over, n, 100.0 * over / n);
	fprintf(stderr, "options asked: %s\n", seen);
	FILE *o = fopen(argv[5], "wb");
	if (o && last) {
		fprintf(o, "P6 %u %u 255\n", vw, vh);
		for (unsigned y = 0; y < vh; y++) for (unsigned x = 0; x < vw; x++) {
			uint8_t px[3]; const uint8_t *r = last + y * vpitch;
			if (fmt == 1) { uint32_t c = ((const uint32_t *)r)[x]; px[0] = c >> 16; px[1] = c >> 8; px[2] = c; }
			else { uint16_t c = ((const uint16_t *)r)[x];
				if (fmt == 2) { px[0] = (c >> 11) << 3; px[1] = ((c >> 5) & 63) << 2; px[2] = (c & 31) << 3; }
				else { px[0] = ((c >> 10) & 31) << 3; px[1] = ((c >> 5) & 31) << 3; px[2] = (c & 31) << 3; } }
			fwrite(px, 1, 3, o);
		}
		fclose(o);
	}
	fflush(stdout);
	if (getenv("BENCH_EXIT")) exit(0);   /* PGO: run destructors so libgcov writes .gcda */
	_exit(0);
}
