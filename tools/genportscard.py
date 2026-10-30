#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Draw the Ports shelf's cards (plorpos-3yv.86.3) from PortMaster's logo:
res/cards/classic/PORTS.png, res/cards/fancy/PORTS.png and
res/cards/portmaster.png, the PortMaster entry first on the shelf (as
res/cards/splore.png is on PICO-8's).

The logo is PortMaster's (the PortsMaster project); it is used, as the
console photographs are, to identify what the shelf holds. The support is
unofficial and the launcher says so the first time PortMaster opens.

Sources, in res/cards/src/ (the user's, seq 142):
  portmaster-logo.webp  the logo, 860x859, as received.
  pm-ship.png           the logo's ship and crane alone, navy on transparent,
                        the handheld on the crane's load see-through.
  pm-fancy.png          the sun, its striped reflection and the ship in sky
                        blue, on transparent, as the user edited it.

  entry    the logo, 320x320, corners rounded as a card's.
  fancy    384x384 transparent: pm-fancy.png cleaned - the export's 1 px
           line along the top and the specks it left by the sun's edge go,
           and the hairline seams between ship and sun are closed (seam) -
           then trimmed to itself, its longest side 96% of the frame and
           centred, as res/cards/fancy/SOURCE.md has the photographs.
  classic  the set's measurements (640x820, card stock (28,31,42), a 12px
           accent rule at y=587, the label's baseline at y=727 in head.ttf at
           54) and one flat mark: pm-ship.png in the accent, the logo's sun
           orange, the handheld knocked out to the stock.

  usage: genportscard.py [outdir]   (default: res/cards)
"""
import os
import sys

from collections import deque

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "res", "cards", "src")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "res", "cards")
FONT = os.path.join(ROOT, "res", "fonts", "head.ttf")

W, H = 640, 820
STOCK = (28, 31, 42)
LABEL = (239, 239, 244)
ACCENT = (0xEC, 0x97, 0x1E)   # the logo's sun; systems-rotate.cfg says the same
RULE_Y, RULE_H = 587, 12
RADIUS = 28
SPECK = 5          # px: smaller islands of pixels are export dirt, not art


def rounded(im, r):
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, im.width - 1, im.height - 1], radius=r, fill=255)
    out = im.convert("RGBA")
    out.putalpha(ImageChops.multiply(out.getchannel("A"), m))
    return out


def clean(im):
    """Without the top line and the specks: every island of visible pixels
    under SPECK px is cleared (the handheld's buttons on the crane's load,
    6 px and up, stay)."""
    px = im.load()
    w, h = im.size
    for x in range(w):
        px[x, 0] = (0, 0, 0, 0)
    seen = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            if seen[y * w + x] or not px[x, y][3]:
                continue
            island, q = [], deque([(x, y)])
            seen[y * w + x] = 1
            while q:
                cx, cy = q.popleft()
                island.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and px[nx, ny][3]:
                        seen[ny * w + nx] = 1
                        q.append((nx, ny))
            if len(island) < SPECK:
                for p in island:
                    px[p] = (0, 0, 0, 0)
    return seam(im).crop(im.getchannel("A").getbbox())


def seam(im):
    """The ship was cut out of the sun with a hairline left between them -
    dark (the logo's navy outline, blended) or see-through - which drew as
    a dark outline on the card. A dark or not quite opaque pixel, between
    opaque ones (across or up and down, within 2 px), with both blue and
    orange within 2 px is that seam - the sun's own soft rim has nothing
    outside it, so it stays soft. It takes the color its neighbours have
    most. The stripes' gaps, the lattice and the handheld's cut-outs touch
    one color only and stay."""
    px = im.load()
    w, h = im.size
    src = im.copy().load()
    def kind(p):
        if p[3] < 255 or max(p[:3]) < 160: return None
        return "blue" if p[2] > p[0] else "orange" if p[0] > p[2] else None
    for y in range(2, h - 2):
        for x in range(2, w - 2):
            if src[x, y][3] == 255 and max(src[x, y][:3]) >= 160:
                continue
            near = [src[x + i, y + j] for j in range(-2, 3) for i in range(-2, 3)]
            kinds = {kind(p) for p in near}
            solid = lambda dx, dy: any(src[x + dx * k, y + dy * k][3] == 255 for k in (1, 2))
            between = (solid(-1, 0) and solid(1, 0)) or (solid(0, -1) and solid(0, 1))
            if between and "blue" in kinds and "orange" in kinds:
                close = [src[x + i, y + j] for j in (-1, 0, 1) for i in (-1, 0, 1)
                         if kind(src[x + i, y + j])] or [p for p in near if kind(p)]
                px[x, y] = max(set(close), key=close.count)
    return im


def entry(logo):
    return rounded(logo.resize((320, 320), Image.LANCZOS), 18)


def fancy(art):
    k = 384 * 0.96 / max(art.size)
    art = art.resize((round(art.width * k), round(art.height * k)), Image.LANCZOS)
    out = Image.new("RGBA", (384, 384), (0, 0, 0, 0))
    out.alpha_composite(art, ((384 - art.width) // 2, (384 - art.height) // 2))
    return out


def classic(ship):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=RADIUS, fill=STOCK + (255,))
    d.rectangle([0, RULE_Y, W - 1, RULE_Y + RULE_H - 1], fill=ACCENT + (255,))
    k = min(520 / ship.width, 400 / ship.height)
    m = ship.getchannel("A").resize((round(ship.width * k), round(ship.height * k)), Image.LANCZOS)
    im.paste(Image.new("RGBA", m.size, ACCENT + (255,)), ((W - m.width) // 2, (RULE_Y - m.height) // 2), m)
    d = ImageDraw.Draw(im)
    d.text((W // 2, 727), "PORTS", font=ImageFont.truetype(FONT, 54), fill=LABEL + (255,), anchor="ms")
    return im


def main():
    logo = Image.open(os.path.join(SRC, "portmaster-logo.webp")).convert("RGB")
    ship = Image.open(os.path.join(SRC, "pm-ship.png")).convert("RGBA")
    art = clean(Image.open(os.path.join(SRC, "pm-fancy.png")).convert("RGBA"))
    for rel, im in (("classic/PORTS.png", classic(ship)), ("fancy/PORTS.png", fancy(art)),
                    ("portmaster.png", entry(logo))):
        p = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        im.save(p)
        print(p, os.path.getsize(p), "bytes")


if __name__ == "__main__":
    main()
