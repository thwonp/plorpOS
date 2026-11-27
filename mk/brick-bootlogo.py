#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""The Brick's boot pictures for plorpOS (plorpos-act.5):
brick-bootlogo.py <wordmark.png> <out dir>

Writes bootlogo.bmp (24-bit, u-boot's splash slot on mmcblk0p1) and splash.png
(pic2fb's loading splash, /etc/splash.png): the same picture, the plorpOS
wordmark centred on black at the panel's 1024x768, so the screen shows one
still from power-on until the launcher's first frame. launch.sh applies them
when BOOT_ART there changes; bump it with any change to this picture."""
import os
import sys

from PIL import Image

W, H, MARK_W = 1024, 768, 576  # the wordmark at the GKD's share of the width

mark = Image.open(sys.argv[1]).convert("RGBA")
mark = mark.resize((MARK_W, round(mark.height * MARK_W / mark.width)), Image.LANCZOS)
img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
img.alpha_composite(mark, ((W - mark.width) // 2, (H - mark.height) // 2))
img = img.convert("RGB")
img.save(os.path.join(sys.argv[2], "bootlogo.bmp"))  # PIL: 24-bit, bottom-up
img.save(os.path.join(sys.argv[2], "splash.png"), optimize=True)
