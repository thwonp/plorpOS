#!/usr/bin/env python3
"""The GKD 350H Ultra's boot logo for plorpOS (plorpos-gkd.94.26):
gkdb-bootlogo.py <wordmark.png> <menu font> <out.bmp.gz> [--keys]

U-Boot shows /.backup/logo_mini_plus.bmp.gz from the eMMC's boot partition from
power-on; once the kernel has blanked the screen, BasedOS draws
/boot/basedos/boot-logo.bmp.gz until the launcher's first frame (the launch
script puts both there). The same format as stock's: a 24-bit bottom-up BMP,
1440x1600 as the panel is built (portrait), gzipped. Drawn as the device is
held (1600x1440): the wordmark centred on black; with --keys (BasedOS's, shown
while it watches for them), the boot keys' labels on the right edge at their
buttons' heights (BasedOS's tools/boot-ui/basedos-splash.c measured them)."""
import gzip
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1600, 1440
GREY = (150, 150, 150, 255)
PURPLE = (138, 92, 214, 255)  # the wordmark's
KEYS = (("Hold for Stock OS", 704), ("Hold for Maintenance", 839))  # Start, Select

mark = Image.open(sys.argv[1]).convert("RGBA")
mark = mark.resize((900, round(mark.height * 900 / mark.width)), Image.LANCZOS)
img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
img.alpha_composite(mark, ((W - mark.width) // 2, (H - mark.height) // 2))
d = ImageDraw.Draw(img)
font = ImageFont.truetype(sys.argv[2], 46)
for label, permille in KEYS if sys.argv[4:] == ["--keys"] else ():
    cy, tri = H * permille // 1000, 30
    tx = W - 24 - tri
    d.text((tx - 22 - d.textlength(label, font=font), cy), label, font=font, fill=GREY, anchor="lm")
    d.polygon([(tx, cy - tri // 2), (tx + tri, cy), (tx, cy + tri // 2)], fill=PURPLE)

# As the panel is built: held landscape, it is the portrait panel turned a quarter clockwise.
native = img.convert("RGB").rotate(90, expand=True)
nw, nh = native.size
rows = native.transpose(Image.FLIP_TOP_BOTTOM).tobytes("raw", "BGR")
bmp = (b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
       + struct.pack("<IiiHHIIiiII", 40, nw, nh, 1, 24, 0, len(rows), 0, 0, 0, 0) + rows)
with open(sys.argv[3], "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", mtime=0, filename="") as z:
    z.write(bmp)
