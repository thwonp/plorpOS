/* SPDX-License-Identifier: MIT */
/* Touch as buttons, shared by the touch devices (touch.h): the RG Rotate,
 * whose engine this was (plorpos-3yv.10), and the RG DS (plorpos-rgds.24).
 * Each feeds its SDL finger events in panel pixels and polls once per input
 * poll; the screens answer the buttons and tap targets (ui_hit) they always
 * have. */
#include "ui.h"

#ifdef UI_TOUCH
#include "platform.h"
#include "platform_dev.h"
#include "touch.h"

#include <string.h>

#define TOUCH_SWIPE_PX  60     /* farther than this is a swipe: the first step */
#define TOUCH_STEP_PX   200    /* then each this much farther, one more */
#define TOUCH_HOLD_MS   500
#define TOUCH_SLOP_PX   30     /* a tap or long press never strayed farther than this */
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
	float  far;                /* the farthest the first finger got, squared */
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
	d->down = touch.fingers > 0;
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

void plat_touch_catch(void)
{
	if (touch.fingers > 0) touch.caught = true;
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

void touch_event(const SDL_Event *e, float w, float h)
{
	/* Panel pixels, so the distances above are what a finger travels. */
	if (w <= 0 || h <= 0) w = h = 720;
	if (e->type == SDL_FINGERDOWN || e->type == SDL_FINGERUP || e->type == SDL_FINGERMOTION)
		touch_note();

	if (e->type == SDL_FINGERDOWN) {
		if (touch.fingers++ == 0) {
			touch.most = 1;
			touch.x0 = touch.x = e->tfinger.x * w;
			touch.y0 = touch.y = e->tfinger.y * h;
			touch.t0 = plat_now_ms();
			touch.held = false;
			touch.far = 0;
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
			float fx, fy;

			touch.x = e->tfinger.x * w; touch.y = e->tfinger.y * h;
			fx = touch.x - touch.x0; fy = touch.y - touch.y0;
			if (fx * fx + fy * fy > touch.far) touch.far = fx * fx + fy * fy;
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
		/* Nor a tap after a slow drag that fell short of a step: it would be A
		 * on the item under the finger - a launch (plorpos-3yv.69). */
		else if (!touch.caught && touch.far <= TOUCH_SLOP_PX * TOUCH_SLOP_PX)
			touch_at(IN_ACCEPT);
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

/* When the last long press fired: its Y reaches the screen a few polls
 * later, behind the steps that walk the cursor to the row. */
static unsigned long_at;

bool touch_long_take(void)
{
	bool was = long_at && plat_now_ms() - long_at < 1500;

	long_at = 0;
	return was;
}

void touch_poll(in_state *st)
{
	if (touch.pulsing) { set_btn(st, touch.up, false); touch.pulsing = false; }
	if (touch.coasting) touch_coast();
	/* A finger that stayed put: one that wandered is a drag, even a slow
	 * one still short of TOUCH_SWIPE_PX at 500 ms - a careful pull on a
	 * shelf was a Favorite (plorpos-3yv.69), a switcher card's the same
	 * (plorpos-3yv.57, whose touch.moved check stays for its 16 px). */
	if (touch.fingers == 1 && touch.most == 1 && !touch.held && touch.steps == 0 &&
	    !(touch.free && touch.moved) && touch.far <= TOUCH_SLOP_PX * TOUCH_SLOP_PX &&
	    plat_now_ms() - touch.t0 >= TOUCH_HOLD_MS) {
		touch.held = true;
		long_at = plat_now_ms();
		if (!long_at) long_at = 1;
		touch_at(IN_Y);
	}
	if (touch.qn > 0) {
		touch.up = touch.queue[0];
		touch.pulsing = true;
		memmove(touch.queue, touch.queue + 1, --touch.qn * sizeof touch.queue[0]);
		set_btn(st, touch.up, true);
	}
}


void touch_reset(void)
{
	bool keep = touch.free;   /* the screen's ask outlives a flush */

	memset(&touch, 0, sizeof touch);
	touch.free = keep;
}
#endif

/* On every build - a device without a panel never moves it (plorpos-rgds.42). */
#include "touch.h"
static unsigned touch_seen;
void touch_note(void) { touch_seen++; }
unsigned touch_activity(void) { return touch_seen; }
