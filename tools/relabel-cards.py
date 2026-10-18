#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Redraw the label on every classic card in the UI font (plorpos-8m1.2).

The classic cards' labels are baked into the PNGs, so a new res/fonts/menu.ttf
leaves them in the old face until this runs. The band under the accent rule
is repainted from the card's own left edge, row by row - it is flat across
each row, a slight gradient down - and the label drawn centered on the set's
baseline (y=727), letter-spaced as the set always was. The one place labels
are drawn: run it after genfavcard.py or gendscard.py, which draw their own.

Run from the repo root: python3 tools/relabel-cards.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, "res", "cards", "classic")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")

LABELS = {
    "32X": "SEGA 32X", "ARCADE": "ARCADE", "FAVORITES": "FAVORITES",
    "GBA": "GBA", "GBC": "GAME BOY COLOR", "GB": "GAME BOY",
    "GENESIS": "GENESIS", "GG": "GAME GEAR", "MUSE": "MUSE",
    "NDS": "NINTENDO DS", "NEOGEO": "NEO GEO", "NES": "NES",
    "NGPC": "NGP COLOR", "NGP": "NGP", "PCE": "TURBOGRAFX-16",
    "PICO8": "PICO-8", "PS": "PLAYSTATION", "SEGACD": "SEGA CD",
    "SMS": "MASTER SYSTEM", "SNES": "SUPER NES",
}

LABEL = (239, 239, 244, 255)
BAND = (604, 792)          # under the rule, above the rounded corners
BASELINE = 727
SIZE = 50
TRACK = 0.14               # of the size, between letters
MAX_W = 600                # a longer label is set smaller to fit


def width(font, text, track):
    return sum(font.getlength(c) for c in text) + track * (len(text) - 1)


def main():
    for name, text in LABELS.items():
        path = os.path.join(DIR, name + ".png")
        im = Image.open(path).convert("RGBA")
        px = im.load()
        for y in range(*BAND):
            c = px[4, y]
            for x in range(im.width):
                px[x, y] = c
        size = SIZE
        while True:
            f = ImageFont.truetype(FONT, size)
            track = TRACK * size
            w = width(f, text, track)
            if w <= MAX_W or size <= 30:
                break
            size -= 1
        d = ImageDraw.Draw(im)
        x = (im.width - w) / 2
        for c in text:
            d.text((x, BASELINE), c, font=f, fill=LABEL, anchor="ls")
            x += f.getlength(c) + track
        im.save(path)
        print("%-10s %-15s %dpx" % (name, text, size))


if __name__ == "__main__":
    main()
