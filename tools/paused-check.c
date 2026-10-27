/* SPDX-License-Identifier: MIT AND PolyForm-Noncommercial-1.0.0 */
/* Exercise the real DISPLAY parser and paused renderer with a red/blue DS
 * preview. No window, device, ROM or sound. Linker GC drops unrelated platform
 * code; keeping the parser and draw together catches protocol/scale drift. */
#include "../src/platform.c"

static int failures;

static bool contains(SDL_Rect r, int x, int y)
{
	return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static void check_frame(int sw, int sh, float scale, const char *display,
                         SDL_Rect top, SDL_Rect bottom)
{
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Surface *preview = SDL_CreateRGBSurfaceWithFormat(0, 256, 384, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
	SDL_Rect half = { 0, 0, 256, 192 };
	SDL_FillRect(preview, &half, 0xffff0000);
	half.y = 192;
	SDL_FillRect(preview, &half, 0xff0000ff);
	SDL_Texture *bg = SDL_CreateTextureFromSurface(r, preview);
	Uint32 *pixels = malloc((size_t)sw * sh * sizeof *pixels);

	g_scale = scale;
	SDL_RenderSetScale(r, scale, scale);
	d_note_display(display);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	plat_draw_paused(r, bg);
	SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, pixels, sw * sizeof *pixels);
	int bad = 0;
	for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
		Uint32 expected = contains(top, x, y) ? 0xff0000 : contains(bottom, x, y) ? 0x0000ff : 0;
		if ((pixels[y * sw + x] & 0xffffff) != expected) bad++;
	}
	float sx, sy;
	SDL_RenderGetScale(r, &sx, &sy);
	if (bad || sx != scale || sy != scale) {
		fprintf(stderr, "FAIL %dx%d scale %.4f: %d wrong pixels: %s\n", sw, sh, scale, bad, display);
		failures++;
	}
	free(pixels);
	SDL_DestroyTexture(bg);
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(preview);
	SDL_FreeSurface(surface);
}

int main(void)
{
	check_frame(720, 720, .9375f,
		"DISPLAY\tmode=ds-a\trect=480x720+120+0\ttop=720x540+0+0\tbottom=240x180+240+540",
		(SDL_Rect){0,0,720,540}, (SDL_Rect){240,540,240,180});
	check_frame(720, 720, .9375f,
		"DISPLAY\tmode=ds-a-swap\trect=480x720+120+0\ttop=240x180+240+0\tbottom=720x540+0+180",
		(SDL_Rect){240,0,240,180}, (SDL_Rect){0,180,720,540});
	check_frame(720, 720, .9375f,
		"DISPLAY\tmode=ds-b\trect=480x720+120+0\ttop=480x360+120+0\tbottom=480x360+120+360",
		(SDL_Rect){120,0,480,360}, (SDL_Rect){120,360,480,360});
	check_frame(1600, 1440, 1.5625f,
		"DISPLAY\tmode=ds-a\trect=960x1440+320+0\ttop=1600x1200+0+0\tbottom=320x240+640+1200",
		(SDL_Rect){0,0,1600,1200}, (SDL_Rect){640,1200,320,240});
	check_frame(1600, 1440, 1.5625f,
		"DISPLAY\tmode=ds-a-swap\trect=960x1440+320+0\ttop=320x240+640+0\tbottom=1600x1200+0+240",
		(SDL_Rect){640,0,320,240}, (SDL_Rect){0,240,1600,1200});
	check_frame(1600, 1440, 1.5625f,
		"DISPLAY\tmode=ds-b\trect=960x1440+320+0\ttop=960x720+320+0\tbottom=960x720+320+720",
		(SDL_Rect){320,0,960,720}, (SDL_Rect){320,720,960,720});
	/* No split metadata after a DS game must clear the old two-screen path.
	 * This also covers an older Diatom and ordinary single-frame previews. */
	check_frame(720, 720, 1,
		"DISPLAY\tmode=aspect\trect=480x720+120+0",
		(SDL_Rect){120,0,480,360}, (SDL_Rect){120,360,480,360});
	printf("%s: DS paused layouts on Rotate/GKD, render scale, single-frame fallback\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
