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
FONT = os.path.join(ROOT, "res", "fonts", "head.ttf")   # card labels are headings (plorpos-8m1.2)

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
    """An open DS Lite, front on, every part placed from a product photo
    (494 px wide there, 360 here: x and y below are in the photo's frame,
    scaled). Two shells with small corners; between them the hinge barrel,
    a cylinder with round end caps a step taller than its middle, the mic slot
    in the middle, the right cap carrying the two LED lines. Screens at
    about half the width, centered. On the lower shell the d-pad and ABXY sit centered in
    the space between the shell's edge and the raised frame round the screen,
    and start/select hang low just right of that frame. All of it punched out
    of the accent to the card stock."""
    acc, stock = ACCENT + (255,), STOCK + (255,)
    k = 360 / 494.0
    ox, oy = cx - 180, top

    def X(v): return ox + (v - 137) * k
    def Y(v): return oy + (v - 127) * k

    def rect(x0, y0, x1, y1, fill, r=0):
        box = s(X(x0), Y(y0), X(x1), Y(y1))
        if r: d.rounded_rectangle(box, radius=r * SS, fill=fill)
        else: d.rectangle(box, fill=fill)

    def dot(x, y, rad, fill):
        d.ellipse(s(X(x) - rad, Y(y) - rad, X(x) + rad, Y(y) + rad), fill=fill)

    # Shells: top 127-362, bottom from behind the barrel to 640.
    rect(137, 127, 631, 362, acc, 7)
    rect(137, 392, 631, 640, acc, 7)
    # The barrel, 366-400: end caps full height, the middle a hair thinner
    # and running into them (the user's edit: no grooves).
    bh = (400 - 366) * k
    d.rectangle(s(X(137), Y(362), X(631), Y(404)), fill=stock)
    d.rounded_rectangle(s(X(140), Y(366), X(203), Y(400)), radius=int(bh / 2) * SS, fill=acc)
    d.rounded_rectangle(s(X(567), Y(366), X(628), Y(400)), radius=int(bh / 2) * SS, fill=acc)
    rect(180, 368, 590, 398, acc)                      # runs into the caps: no grooves
    rect(384 - 2, 377, 384 + 2, 389, stock)            # the mic slot
    for lx in (597, 604):                               # the LED lines
        rect(lx - 1, 371, lx + 1, 395, stock)

    # Top screen, centered on the shell (384). No grilles or corner tabs:
    # too busy at card size (the user's edit, 2026-10-18).
    rect(266, 160, 502, 338, stock)

    # Bottom screen, centered like the top one; the raised frame round it
    # (252-516) is what the d-pad and ABXY are centered against.
    rect(266, 426, 502, 600, stock)
    # The raised frame itself, as a hairline: inset again by its thickness.
    d.rounded_rectangle(s(X(252), Y(411), X(516), Y(614)), radius=6 * SS,
                        outline=stock, width=2 * SS)
    dx, dy = (137 + 252) / 2, 490
    arm, th = 33, 11
    rect(dx - arm, dy - th, dx + arm, dy + th, stock, 2)
    rect(dx - th, dy - arm, dx + th, dy + arm, stock, 2)
    bx, by = (516 + 631) / 2, 482
    for ddx, ddy in ((0, -28), (0, 28), (-28, 0), (28, 0)):
        dot(bx + ddx, by + ddy, 8.5, stock)
    for y in (573, 602):                                # start, select
        dot(541, y, 5, stock)


def main():
    im = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W * SS - 1, H * SS - 1], radius=RADIUS * SS, fill=STOCK + (255,))
    d.rectangle([0, RULE_Y * SS, W * SS - 1, (RULE_Y + RULE_H) * SS - 1], fill=ACCENT + (255,))
    # Centered on the panel above the rule: 374 px of mark in ~587.
    mark(d, W // 2, (RULE_Y - 374) // 2)
    im = im.resize((W, H), Image.LANCZOS)
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(FONT, 54)
    d.text((W // 2, 727), "NINTENDO DS", font=f, fill=LABEL + (255,), anchor="ms")
    im.save(OUT)
    print(OUT, os.path.getsize(OUT), "bytes")


if __name__ == "__main__":
    main()
