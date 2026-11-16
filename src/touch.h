/* SPDX-License-Identifier: MIT */
/* The touch engine (touch.c): gestures become the buttons every screen
 * answers - tap, drag and flick, edge swipes, long press, two- and
 * three-finger taps - and the free drag lists and the switcher read.
 * w, h: the panel the finger is on, in pixels. */
#ifndef TORTOS_TOUCH_H
#define TORTOS_TOUCH_H

#include <SDL.h>
#include "platform.h"

void touch_event(const SDL_Event *e, float w, float h);
void touch_poll(in_state *st);
void touch_reset(void);     /* a flush: all but the screen's free-drag ask */

#endif
