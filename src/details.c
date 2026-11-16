/* SPDX-License-Identifier: MIT */
/* The Details screen (details.h). Laid out on a 640x480 panel - the RG DS's
 * bottom one - and scaled for any other size. Drawn only when what it shows
 * changes, so nothing here is cached but the cover. */
#include "details.h"

#include <SDL_image.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static char g_dir[512];
static float g_k = 1.0f;                     /* panel width / 640 */
static struct { bool head; int px; TTF_Font *f; } g_fonts[12];
static SDL_Texture *g_art;
static char g_art_path[1024];
static int g_art_w, g_art_h;
static time_t g_art_mtime;     /* a state's preview keeps its path when rewritten */

static const SDL_Color BG = { 22, 24, 32, 255 }, PANEL = { 12, 13, 18, 255 }, TRACK = { 40, 42, 54, 255 },
                       MUTED = { 138, 143, 163, 255 }, WHITE = { 240, 240, 245, 255 }, LIGHT = { 210, 210, 220, 255 },
                       ACC = { 0x8A, 0x5C, 0xD6, 255 }, DOT = { 78, 82, 100, 255 };

void details_init(const char *font_dir) { snprintf(g_dir, sizeof g_dir, "%s", font_dir); }

static int S(int v) { return (int)(v * g_k + 0.5f); }

static TTF_Font *font(bool head, int size)
{
	int px = S(size), i;

	for (i = 0; i < 12 && g_fonts[i].f; i++)
		if (g_fonts[i].head == head && g_fonts[i].px == px) return g_fonts[i].f;
	if (i == 12) return g_fonts[11].f;
	{
		char p[600];
		snprintf(p, sizeof p, "%s/%s", g_dir, head ? "head.ttf" : "lato.ttf");
		g_fonts[i].f = TTF_OpenFont(p, px);
		if (!g_fonts[i].f) { fprintf(stderr, "details: no font %s\n", p); return NULL; }
		g_fonts[i].head = head;
		g_fonts[i].px = px;
	}
	return g_fonts[i].f;
}

static int text_w(bool head, int size, const char *s)
{
	TTF_Font *f = font(head, size);
	int w = 0;

	if (f && s && *s) TTF_SizeUTF8(f, s, &w, NULL);
	return w;
}

/* anchor: 0 left, 1 centre, 2 right; y is the top of the line. */
static void text(SDL_Renderer *r, bool head, int size, const char *s, int x, int y, int anchor, SDL_Color c)
{
	TTF_Font *f = font(head, size);
	SDL_Surface *surf;
	SDL_Texture *t;

	if (!f || !s || !*s || !(surf = TTF_RenderUTF8_Blended(f, s, c))) return;
	t = SDL_CreateTextureFromSurface(r, surf);
	if (t) {
		SDL_Rect d = { anchor == 0 ? x : anchor == 1 ? x - surf->w / 2 : x - surf->w, y, surf->w, surf->h };
		SDL_RenderCopy(r, t, NULL, &d);
		SDL_DestroyTexture(t);
	}
	SDL_FreeSurface(surf);
}

/* `s` cut to fit `w` px, with an ellipsis. */
static void fit(bool head, int size, const char *s, int w, char *out, size_t n)
{
	size_t len;

	snprintf(out, n, "%s", s);
	if (text_w(head, size, out) <= w) return;
	for (len = strlen(out); len > 0; len--) {
		while (len > 0 && (out[len] & 0xC0) == 0x80) len--;   /* whole UTF-8 characters */
		snprintf(out + len, n - len, "...");
		if (text_w(head, size, out) <= w) return;
	}
}

/* Greedy word wrap into at most `max` lines of `w` px; the last one ends in an
 * ellipsis when there was more. Returns the number of lines. */
static int wrap(bool head, int size, const char *s, int w, char lines[][160], int max)
{
	int n = 0;
	char cur[160] = "";

	while (*s && n < max) {
		char word[160], t[320];
		size_t k = 0;

		while (*s == ' ' || *s == '\n') s++;
		while (*s && *s != ' ' && *s != '\n' && k + 1 < sizeof word) word[k++] = *s++;
		word[k] = '\0';
		if (!k) break;
		snprintf(t, sizeof t, "%s%s%s", cur, *cur ? " " : "", word);
		if (!*cur || text_w(head, size, t) <= w) { snprintf(cur, sizeof cur, "%s", t); continue; }
		snprintf(lines[n++], 160, "%s", cur);
		snprintf(cur, sizeof cur, "%s", word);
	}
	if (*cur && n < max) snprintf(lines[n++], 160, "%s", cur);
	else if (*cur || *s) {                         /* more than fits */
		char t[200];
		snprintf(t, sizeof t, "%s ...", lines[max - 1]);
		fit(head, size, t, w, lines[max - 1], 160);
	}
	return n;
}

static void fill(SDL_Renderer *r, int x, int y, int w, int h, SDL_Color c)
{
	SDL_Rect q = { x, y, w, h };

	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(r, &q);
}

/* A rounded rectangle, row by row: few enough pixels to not matter. */
static void rrect(SDL_Renderer *r, int x, int y, int w, int h, int rad, SDL_Color c)
{
	if (rad * 2 > h) rad = h / 2;
	if (rad * 2 > w) rad = w / 2;
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
	for (int i = 0; i < h; i++) {
		int dy = i < rad ? rad - i : i >= h - rad ? i - (h - rad - 1) : 0, in = 0;
		if (dy) { while ((rad - in) * (rad - in) + dy * dy > rad * rad && in < rad) in++; }
		SDL_RenderDrawLine(r, x + in, y + i, x + w - 1 - in, y + i);
	}
}

static void bar(SDL_Renderer *r, int x, int y, int w, int h, float frac, SDL_Color c)
{
	int fw = (int)(w * frac);

	rrect(r, x, y, w, h, h / 2, TRACK);
	if (fw > 0) rrect(r, x, y, fw < h ? h : fw, h, h / 2, c);
}

static SDL_Color rgb(unsigned v) { return (SDL_Color){ (Uint8)(v >> 16), (Uint8)(v >> 8), (Uint8)v, 255 }; }

static void pill(SDL_Renderer *r, const details_info *d, int x, int y)
{
	int w = text_w(false, 14, d->sys) + S(16);
	SDL_Color c = rgb(d->accent ? d->accent : 0x816EBA);
	/* Dark letters on a light accent (the DS's is a pale grey). */
	bool light = c.r * 299 + c.g * 587 + c.b * 114 > 150000;

	if (!d->sys[0]) return;
	rrect(r, x, y, w, S(22), S(6), c);
	text(r, false, 14, d->sys, x + w / 2, y + S(2), 1, light ? BG : WHITE);
}

static bool art(SDL_Renderer *r, const char *path)
{
	struct stat st;
	time_t mt = *path && stat(path, &st) == 0 ? st.st_mtime : 0;

	if (strcmp(path, g_art_path) || mt != g_art_mtime) {
		g_art_mtime = mt;
		if (g_art) SDL_DestroyTexture(g_art);
		g_art = NULL;
		snprintf(g_art_path, sizeof g_art_path, "%s", path);
		if (*path && (g_art = IMG_LoadTexture(r, path)))
			SDL_QueryTexture(g_art, NULL, NULL, &g_art_w, &g_art_h);
	}
	return g_art != NULL;
}

/* The cover inside the box, aspect kept, centred. */
static void cover(SDL_Renderer *r, int x, int y, int w, int h)
{
	float k;
	SDL_Rect q;

	if (!g_art || g_art_w <= 0 || g_art_h <= 0) { rrect(r, x, y, w, h, S(10), PANEL); return; }
	k = (float)w / g_art_w < (float)h / g_art_h ? (float)w / g_art_w : (float)h / g_art_h;
	q.w = (int)(g_art_w * k); q.h = (int)(g_art_h * k);
	q.x = x + (w - q.w) / 2; q.y = y + (h - q.h) / 2;
	SDL_SetTextureColorMod(g_art, 255, 255, 255);
	SDL_RenderCopy(r, g_art, NULL, &q);
}

static void cheevo_line(char *out, size_t n, const details_info *d)
{
	snprintf(out, n, "%d/%d   %d/%d pts", d->ch_earned, d->ch_total, d->pts_earned, d->pts_total);
}

/* A - Card: cover left, facts right, play and achievements below. */
static void page_card(SDL_Renderer *r, int W, int H, const details_info *d)
{
	char lines[3][160], s[96];
	int x = S(268), y = S(58), n, i;
	const struct { const char *k, *v; } facts[] = {
		{ "Year", d->year }, { "Publisher", d->publisher }, { "Genre", d->genre }, { "Players", d->players } };

	cover(r, S(24), S(24), S(220), S(300));
	pill(r, d, x, S(26));
	n = wrap(true, 28, d->title, W - x - S(24), lines, 3);
	for (i = 0; i < n; i++, y += S(34)) text(r, true, 28, lines[i], x, y, 0, WHITE);
	y += S(6);
	for (i = 0; i < 4; i++) {
		if (!facts[i].v[0]) continue;
		fit(false, 17, facts[i].v, W - x - S(110) - S(24), s, sizeof s);
		text(r, false, 17, facts[i].k, x, y, 0, MUTED);
		text(r, false, 17, s, x + S(110), y, 0, WHITE);
		y += S(26);
	}
	rrect(r, S(24), S(340), W - S(48), H - S(364), S(14), PANEL);
	{
		char launches[16];
		const char *big[3] = { d->played[0] ? d->played : "-", launches, d->last[0] ? d->last : "Never" };
		const char *small[3] = { "played", "launched", "last played" };

		snprintf(launches, sizeof launches, "%dx", d->launches);
		for (i = 0; i < 3; i++) {
			int cx = S(24) + (W - S(48)) * (2 * i + 1) / 6;
			text(r, true, 24, big[i], cx, S(356), 1, WHITE);
			text(r, false, 15, small[i], cx, S(388), 1, MUTED);
		}
	}
	if (d->cheevos) {
		cheevo_line(s, sizeof s, d);
		text(r, false, 16, "Achievements", S(44), S(414), 0, MUTED);
		text(r, false, 16, s, W - S(44), S(414), 2, WHITE);
		bar(r, S(44), S(440), W - S(88), S(10), d->ch_total ? (float)d->ch_earned / d->ch_total : 0, ACC);
	} else {
		text(r, false, 16, "No achievements for this game", W / 2, S(424), 1, MUTED);
	}
}

/* C - Story: the cover as a dimmed banner, title and facts, the synopsis. */
static void page_story(SDL_Renderer *r, int W, int H, const details_info *d)
{
	char lines[9][160], s[300];
	int y = S(172), n, i, ban = S(150);

	if (g_art && g_art_w > 0) {
		/* The cover's width across the panel, a band from a fifth of the way down. */
		SDL_Rect from = { 0, g_art_h / 5, g_art_w, ban * g_art_w / W }, to = { 0, 0, W, ban };
		if (from.y + from.h > g_art_h) from.y = g_art_h - from.h > 0 ? g_art_h - from.h : 0;
		SDL_SetTextureColorMod(g_art, 110, 110, 118);
		SDL_RenderCopy(r, g_art, &from, &to);
		SDL_SetTextureColorMod(g_art, 255, 255, 255);
	}
	pill(r, d, S(24), S(22));
	fit(true, 26, d->title, W - S(48), s, sizeof s);
	text(r, true, 26, s, S(24), S(92), 0, WHITE);
	s[0] = '\0';
	{
		const char *parts[4] = { d->year, d->publisher, d->genre, d->players };
		for (i = 0; i < 4; i++) {
			if (!parts[i][0]) continue;
			if (s[0]) strncat(s, "  \xC2\xB7  ", sizeof s - strlen(s) - 1);
			strncat(s, parts[i], sizeof s - strlen(s) - 1);
		}
	}
	{
		char f[300];
		fit(false, 16, s, W - S(48), f, sizeof f);
		text(r, false, 16, f, S(24), S(128), 0, LIGHT);
	}
	if (d->synopsis[0]) {
		n = wrap(false, 19, d->synopsis, W - S(48), lines, (H - S(100) - y) / S(28));
		for (i = 0; i < n; i++, y += S(28)) text(r, false, 19, lines[i], S(24), y, 0, WHITE);
	} else {
		text(r, false, 19, "No synopsis yet - Scrape fills this in.", S(24), y, 0, MUTED);
	}
	rrect(r, S(24), H - S(84), W - S(48), S(60), S(14), PANEL);
	snprintf(s, sizeof s, "%s  \xC2\xB7  played %dx", d->played[0] ? d->played : "Not played yet", d->launches);
	text(r, false, 16, s, S(44), H - S(72), 0, WHITE);
	if (d->cheevos) {
		snprintf(s, sizeof s, "%d/%d achievements", d->ch_earned, d->ch_total);
		text(r, false, 16, s, W - S(44), H - S(72), 2, WHITE);
		bar(r, S(44), H - S(42), W - S(88), S(6), d->ch_total ? (float)d->ch_earned / d->ch_total : 0, ACC);
	}
}

/* B - Stats: small cover and title, two big tiles, the achievements as cells. */
static void page_stats(SDL_Renderer *r, int W, int H, const details_info *d)
{
	char s[300], f[300];
	int i;

	cover(r, S(24), S(20), S(60), S(80));
	fit(true, 22, d->title, W - S(124), s, sizeof s);
	text(r, true, 22, s, S(100), S(24), 0, WHITE);
	snprintf(s, sizeof s, "%s%s%s%s%s", d->sys, d->year[0] ? "  \xC2\xB7  " : "", d->year,
	         d->publisher[0] ? "  \xC2\xB7  " : "", d->publisher);
	fit(false, 16, s, W - S(124), f, sizeof f);
	text(r, false, 16, f, S(100), S(58), 0, MUTED);
	for (i = 0; i < 2; i++) {
		int x0 = S(24) + i * S(300);
		char n[16];
		snprintf(n, sizeof n, "%d", d->launches);
		rrect(r, x0, S(116), S(288), S(90), S(14), PANEL);
		text(r, true, 36, i ? n : d->played[0] ? d->played : "0 min", x0 + S(144), S(126), 1, WHITE);
		text(r, false, 14, i ? "TIMES PLAYED" : "PLAY TIME", x0 + S(144), S(178), 1, MUTED);
	}
	text(r, false, 14, "ACHIEVEMENTS", S(24), S(220), 0, MUTED);
	if (!d->cheevos) {
		text(r, false, 16, "No achievements for this game", W / 2, S(320), 1, MUTED);
	} else {
		int cells = 33, on = d->ch_total ? (cells * d->ch_earned + d->ch_total / 2) / d->ch_total : 0;
		snprintf(s, sizeof s, "%d of %d  \xC2\xB7  %d/%d pts", d->ch_earned, d->ch_total, d->pts_earned, d->pts_total);
		text(r, false, 14, s, W - S(24), S(220), 2, WHITE);
		for (i = 0; i < cells; i++) {
			int x0 = S(24) + (i % 11) * S(54), y0 = S(250) + (i / 11) * S(54);
			rrect(r, x0, y0, S(46), S(46), S(8), i < on ? ACC : TRACK);
			if (i < on) {
				SDL_SetRenderDrawColor(r, WHITE.r, WHITE.g, WHITE.b, 255);
				for (int t = -1; t <= 1; t++) {   /* a tick, three lines thick */
					SDL_RenderDrawLine(r, x0 + S(13), y0 + S(24) + t, x0 + S(20), y0 + S(31) + t);
					SDL_RenderDrawLine(r, x0 + S(20), y0 + S(31) + t, x0 + S(33), y0 + S(16) + t);
				}
			}
		}
	}
	snprintf(s, sizeof s, "Last played: %s", d->last[0] ? d->last : "never");
	text(r, false, 14, s, W / 2, H - S(44), 1, MUTED);
}

void details_draw(SDL_Renderer *r, int w, int h, const details_info *d, int page)
{
	g_k = w / 640.0f;
	fill(r, 0, 0, w, h, BG);
	art(r, d->art);
	switch (page) {
	case 0: page_card(r, w, h, d); break;
	case 1: page_story(r, w, h, d); break;
	default: page_stats(r, w, h, d); break;
	}
	for (int i = 0; i < DETAILS_PAGES; i++) {   /* where the swipe is */
		int cx = w / 2 + (i - 1) * S(18);
		rrect(r, cx - S(4), h - S(17), S(8), S(8), S(4), i == page ? WHITE : DOT);
	}
}

void details_draw_system(SDL_Renderer *r, int w, int h, const char *name, unsigned accent, const char *line)
{
	char s[160];

	g_k = w / 640.0f;
	fill(r, 0, 0, w, h, BG);
	rrect(r, S(24), S(24), w - S(48), h - S(48), S(14), PANEL);
	fill(r, S(24), h / 2 + S(2), w - S(48), S(4), rgb(accent ? accent : 0x816EBA));
	fit(true, 40, name, w - S(96), s, sizeof s);
	text(r, true, 40, s, w / 2, h / 2 - S(58), 1, WHITE);
	text(r, false, 20, line, w / 2, h / 2 + S(22), 1, LIGHT);
}

/* The three buttons' boxes, left to right. */
static SDL_Rect muse_btn(int w, int h, int i)
{
	int bw = S(150), bh = S(76), gap = S(24), x0 = (w - 3 * bw - 2 * gap) / 2;

	return (SDL_Rect){ x0 + i * (bw + gap), h - S(24) - bh, bw, bh };
}

int details_muse_hit(int w, int h, int x, int y)
{
	g_k = w / 640.0f;
	for (int i = 0; i < 3; i++) {
		SDL_Rect q = muse_btn(w, h, i);
		if (x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h) return i + 1;
	}
	return 0;
}

/* A solid triangle pointing left (dir -1) or right (+1), `s` tall, its flat
 * side at x. */
static void tri(SDL_Renderer *r, int x, int y, int s, int dir, SDL_Color c)
{
	SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
	for (int i = 0; i < s; i++) {
		int len = (s / 2 - (i < s / 2 ? s / 2 - i : i - s / 2)) * 7 / 4;
		SDL_RenderDrawLine(r, x, y + i, x + dir * len, y + i);
	}
}

static void stamp(char *out, size_t n, double t)
{
	int s = t > 0 ? (int)t : 0;
	snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

void details_draw_muse(SDL_Renderer *r, int w, int h, const details_muse *m)
{
	static const char *say[4] = { "Muse", "Ready", "Now Playing", "Paused" };
	char lines[2][160], s[160];
	int x = S(232), y = S(30), n, i, cs = S(184);

	g_k = w / 640.0f;
	fill(r, 0, 0, w, h, BG);
	art(r, m->art);
	cover(r, S(24), S(24), cs, cs);
	text(r, false, 15, say[m->state & 3], x, y, 0, rgb(0x816EBA));
	y += S(26);
	if (m->state == 0) {
		text(r, true, 26, "Nothing queued", x, y, 0, WHITE);
		text(r, false, 17, "SELECT opens Muse to pick music.", x, y + S(40), 0, MUTED);
		return;
	}
	n = wrap(true, 26, m->title, w - x - S(24), lines, 2);
	for (i = 0; i < n; i++, y += S(32)) text(r, true, 26, lines[i], x, y, 0, WHITE);
	fit(false, 18, m->artist, w - x - S(24), s, sizeof s);
	text(r, false, 18, s, x, y + S(6), 0, LIGHT);
	fit(false, 16, m->album, w - x - S(24), s, sizeof s);
	text(r, false, 16, s, x, y + S(34), 0, MUTED);

	y = S(232);
	bar(r, S(24), y, w - S(48), S(8), m->len > 0 ? (float)(m->at / m->len) : 0, ACC);
	stamp(s, sizeof s, m->at);
	text(r, false, 15, s, S(24), y + S(14), 0, MUTED);
	stamp(s, sizeof s, m->len);
	text(r, false, 15, s, w - S(24), y + S(14), 2, MUTED);

	if (!m->touch) {
		text(r, false, 17, "SELECT opens Muse", w / 2, h - S(60), 1, MUTED);
		return;
	}
	for (i = 0; i < 3; i++) {
		SDL_Rect q = muse_btn(w, h, i);
		int cx = q.x + q.w / 2, cy = q.y + q.h / 2, g = S(30);

		rrect(r, q.x, q.y, q.w, q.h, S(16), i == 1 ? ACC : PANEL);
		if (i == 0) { fill(r, cx - g / 2 - S(4), cy - g / 2, S(5), g, WHITE); tri(r, cx + g / 2, cy - g / 2, g, -1, WHITE); }
		else if (i == 2) { tri(r, cx - g / 2, cy - g / 2, g, 1, WHITE); fill(r, cx + g / 2, cy - g / 2, S(5), g, WHITE); }
		else if (m->state == 2) { fill(r, cx - S(11), cy - g / 2, S(8), g, WHITE); fill(r, cx + S(3), cy - g / 2, S(8), g, WHITE); }
		else tri(r, cx - g / 3, cy - g / 2, g, 1, WHITE);
	}
}

void details_quit(void)
{
	if (g_art) SDL_DestroyTexture(g_art);
	g_art = NULL;
	g_art_path[0] = '\0';
	for (int i = 0; i < 12 && g_fonts[i].f; i++) { TTF_CloseFont(g_fonts[i].f); g_fonts[i].f = NULL; }
}
