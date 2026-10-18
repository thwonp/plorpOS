#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Draw res/cards/classic/NDS.png, the classic card for the Nintendo DS shelf
(plorpos-3yv.48).

Built the way genfavcard.py builds FAVORITES: the set's measurements (640x820,
card stock (28,31,42), a 12px accent rule at y=587, the label's baseline at
y=727 in menu.ttf at 54) and one flat mark, no strokes. The mark is an open
DS Lite seen from the front - two crisp shells on the hinge barrel - in the
accent, with both screens, the grilles, the d-pad and the buttons knocked out
back to the card stock, so it reads as a DS at the 62% a side card is drawn
at. The Lite, not the original: the user's pick, and the fancy card's.

The accent is a steel silver, the original DS's "Titanium": no other shelf
is a neutral, and the hardware's own colors were white and black.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "res", "cards", "classic", "NDS.png")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")

W, H = 640, 820
STOCK = (28, 31, 42)
LABEL = (239, 239, 244)
ACCENT = (0xA9, 0xB3, 0xC4)
RULE_Y, RULE_H = 587, 12
RADIUS = 28
SS = 4


def s(*v):
    return [x * SS for x in v]


def mark(d, cx, top):
    """An open DS Lite, front on: two crisp shells with small corners (the
    Lite's, not the original's soft ovals), the hinge barrel across almost the
    whole width, speaker grilles beside the top screen, and the d-pad, four
    buttons and start/select on the lower shell - all punched out."""
    acc, stock = ACCENT + (255,), STOCK + (255,)
    sw, sh, r = 262, 160, 12           # one shell
    hinge = 16                         # the barrel between them
    x0 = cx - sw // 2
    y_top, y_bot = top, top + sh + hinge

    d.rounded_rectangle(s(x0, y_top, x0 + sw, y_top + sh), radius=r * SS, fill=acc)
    d.rounded_rectangle(s(x0, y_bot, x0 + sw, y_bot + sh), radius=r * SS, fill=acc)
    # The barrel: nearly the full width, its own small rounding, and a hair
    # of card stock either side of it so the two shells still read as two.
    d.rectangle(s(x0, y_top + sh, x0 + sw, y_bot), fill=stock)
    d.rounded_rectangle(s(x0 + 10, y_top + sh - 2, x0 + sw - 10, y_bot + 2),
                        radius=6 * SS, fill=acc)
    d.rectangle(s(x0 + 10, y_top + sh + 3, x0 + sw - 10, y_top + sh + 4), fill=stock)
    d.rectangle(s(x0 + 10, y_bot - 4, x0 + sw - 10, y_bot - 3), fill=stock)

    # Screens, 4:3, the same size top and bottom as on the hardware.
    scr_w, scr_h = 136, 102
    sx = cx - scr_w // 2
    ty = y_top + (sh - scr_h) // 2
    by = y_bot + (sh - scr_h) // 2
    d.rectangle(s(sx, ty, sx + scr_w, ty + scr_h), fill=stock)
    d.rectangle(s(sx, by, sx + scr_w, by + scr_h), fill=stock)

    # Speaker grilles: a 2x3 block of dots either side of the top screen.
    for gx in (x0 + (sx - x0) // 2, sx + scr_w + (x0 + sw - sx - scr_w) // 2):
        for row in range(3):
            for col in range(2):
                px, py = gx - 5 + col * 10, y_top + sh // 2 - 10 + row * 10
                d.ellipse(s(px - 2, py - 2, px + 2, py + 2), fill=stock)

    # D-pad left of the lower screen, ABXY right, start/select under them.
    my = y_bot + sh // 2 - 10
    dx = x0 + (sx - x0) // 2
    arm, th = 14, 9
    d.rectangle(s(dx - arm, my - th // 2, dx + arm, my + th // 2), fill=stock)
    d.rectangle(s(dx - th // 2, my - arm, dx + th // 2, my + arm), fill=stock)
    bx = sx + scr_w + (x0 + sw - sx - scr_w) // 2
    br, off = 5, 10
    for ox, oy in ((0, -off), (0, off), (-off, 0), (off, 0)):
        d.ellipse(s(bx + ox - br, my + oy - br, bx + ox + br, my + oy + br), fill=stock)
    for k in range(2):
        py = my + 30 + k * 10
        d.rounded_rectangle(s(bx - 7, py - 2, bx + 7, py + 2), radius=2 * SS, fill=stock)


def main():
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W * SS - 1, H * SS - 1], radius=RADIUS * SS, fill=STOCK + (255,))
    d.rectangle([0, RULE_Y * SS, W * SS - 1, (RULE_Y + RULE_H) * SS - 1], fill=ACCENT + (255,))
    # Centered on the panel above the rule: 336 px of mark in ~587.
    mark(d, W // 2, (RULE_Y - 336) // 2)
    im = im.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, 54)
    d.text((W // 2, 727), "NINTENDO DS", font=f, fill=LABEL + (255,), anchor="ms")
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
