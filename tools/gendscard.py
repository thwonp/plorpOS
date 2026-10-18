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
    """An open DS Lite, front on, in its own proportions (measured off a
    product photo at 0.465 scale): nearly square overall, two shells with
    small corners, and between them the hinge barrel - nearly the full width,
    round-ended, set off from both shells by a hair of card stock. Screens at
    about half the width, 3x2 speaker grilles beside the top one, the d-pad
    high on the left, ABXY as a diamond on the right and start/select below
    it - all punched out to the card stock."""
    acc, stock = ACCENT + (255,), STOCK + (255,)
    W_, r = 360, 10
    x0 = cx - W_ // 2
    top_h, gap, barrel = 171, 4, 24
    y_bar = top + top_h + gap
    y_bot = y_bar + barrel + gap
    bot_h = 372 - (y_bot - top)

    d.rounded_rectangle(s(x0, top, x0 + W_, top + top_h), radius=r * SS, fill=acc)
    d.rounded_rectangle(s(x0, y_bot, x0 + W_, y_bot + bot_h), radius=r * SS, fill=acc)
    d.rounded_rectangle(s(x0 + 3, y_bar, x0 + W_ - 3, y_bar + barrel),
                        radius=(barrel // 2) * SS, fill=acc)

    # Screens, 4:3 and the same size, centered.
    sw_, sh_ = 172, 129
    sx = cx - sw_ // 2
    d.rectangle(s(sx, top + 20, sx + sw_, top + 20 + sh_), fill=stock)
    d.rectangle(s(sx, y_bot + 14, sx + sw_, y_bot + 14 + sh_), fill=stock)

    # Speaker grilles: three across, two down, either side of the top screen.
    gy = top + top_h // 2 - 7
    for gx in (x0 + 44, x0 + W_ - 44):
        for row in range(2):
            for col in range(3):
                px, py = gx - 13 + col * 13, gy + row * 13
                d.ellipse(s(px - 3, py - 3, px + 3, py + 3), fill=stock)

    # D-pad: high on the left of the lower shell.
    dx, dy = x0 + 41, y_bot + 62
    arm, th = 24, 15
    d.rectangle(s(dx - arm, dy - th // 2, dx + arm, dy + th // 2), fill=stock)
    d.rectangle(s(dx - th // 2, dy - arm, dx + th // 2, dy + arm), fill=stock)
    # ABXY, a diamond on the right; start and select below it.
    bx, by = x0 + W_ - 45, y_bot + 55
    br, off = 10, 20
    for ox, oy in ((0, -off), (0, off), (-off, 0), (off, 0)):
        d.ellipse(s(bx + ox - br, by + oy - br, bx + ox + br, by + oy + br), fill=stock)
    for k in range(2):
        py = y_bot + 120 + k * 22
        d.ellipse(s(bx - 25 - 5, py - 5, bx - 25 + 5, py + 5), fill=stock)


def main():
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W * SS - 1, H * SS - 1], radius=RADIUS * SS, fill=STOCK + (255,))
    d.rectangle([0, RULE_Y * SS, W * SS - 1, (RULE_Y + RULE_H) * SS - 1], fill=ACCENT + (255,))
    # Centered on the panel above the rule: 372 px of mark in ~587.
    mark(d, W // 2, (RULE_Y - 372) // 2)
    im = im.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, 54)
    d.text((W // 2, 727), "NINTENDO DS", font=f, fill=LABEL + (255,), anchor="ms")
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
