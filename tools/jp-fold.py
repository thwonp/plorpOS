#!/usr/bin/env python3
"""Fold the Latin letters jp.ttf lacks onto plain ones it has (plorpos-57h.4).

Kosugi Maru has kana and kanji but almost no accented Latin, and the launcher
drops a character its face has no glyph for ("Þú ert sólin" -> " ert slin").
This points every missing character that reduces to one the font has - its
accents stripped (NFKD), or by the EXTRA table - at that glyph in the cmap:
ú at u, Þ at P, Ｒ at R, so "Þú ert sólin" reads "Pu ert solin". No new outlines,
no runtime cost. Run once when jp.ttf changes:

    python3 tools/jp-fold.py res/fonts/jp.ttf   (needs fonttools)
"""
import sys
import unicodedata

from fontTools.ttLib import TTFont

# Letters with no decomposition to a plain one.
EXTRA = {
    "Þ": "P", "þ": "p", "Ð": "D", "ð": "d", "Đ": "D", "đ": "d", "Ø": "O", "ø": "o",
    "Æ": "A", "æ": "a", "Œ": "O", "œ": "o", "ß": "s", "Ł": "L", "ł": "l",
    "Ħ": "H", "ħ": "h", "ı": "i", "Ŧ": "T", "ŧ": "t", "Ŋ": "N", "ŋ": "n",
    "Ĳ": "I", "ĳ": "i", "ĸ": "k", "Ŀ": "L", "ŀ": "l", "ŉ": "n", "ſ": "s",
    "ẞ": "S", "ẜ": "s", "ẝ": "s", "ẟ": "d", "ƒ": "f", "Ƒ": "F", "ƀ": "b", "Ɓ": "B",
    "Ƃ": "B", "ƃ": "b", "Ƈ": "C", "ƈ": "c", "Ɗ": "D", "Ƌ": "D", "ƌ": "d", "Ɠ": "G",
    "Ɨ": "I", "Ƙ": "K", "ƙ": "k", "ƚ": "l", "Ɲ": "N", "ƞ": "n", "Ƥ": "P", "ƥ": "p",
    "ƫ": "t", "Ƭ": "T", "ƭ": "t", "Ʈ": "T", "Ʋ": "V", "Ƴ": "Y", "ƴ": "y", "Ƶ": "Z",
    "ƶ": "z", "Ǥ": "G", "ǥ": "g", "Ȼ": "C", "ȼ": "c", "Ƀ": "B", "Ɇ": "E", "ɇ": "e",
    "Ɉ": "J", "ɉ": "j", "Ɍ": "R", "ɍ": "r", "Ɏ": "Y", "ɏ": "y", "ȥ": "z", "Ȥ": "Z",
    "‐": "-", "‑": "-", "‒": "-", "–": "-", "—": "-", "―": "-",
    "‘": "'", "’": "'", "‚": ",", "“": '"', "”": '"', "„": '"', "′": "'", "″": '"',
    "«": '"', "»": '"', "‹": "<", "›": ">", "¡": "!", "¿": "?", "·": ".", "•": ".",
}


def plain(c, have):
    """The one character c reads as that the font has, or None."""
    if c in EXTRA:
        return EXTRA[c]
    d = "".join(x for x in unicodedata.normalize("NFKD", c) if not unicodedata.combining(x))
    d = "".join(EXTRA.get(x, x) for x in d)
    return d if len(d) == 1 and d != c and ord(d) in have else None


def main(path):
    font = TTFont(path)
    best = font.getBestCmap()
    added = 0
    for u in range(0xA0, 0x30000):
        if u in best or 0xD800 <= u < 0xE000:
            continue
        p = plain(chr(u), best)
        if not p:
            continue
        for t in font["cmap"].tables:
            # Format 4 holds only the BMP; a font with no format 12 keeps the rest out.
            if t.isUnicode() and (t.format == 12 or (t.format == 4 and u < 0x10000)):
                t.cmap[u] = best[ord(p)]
        added += 1
    font.save(path)
    print(f"{path}: {added} characters folded onto plain letters")


if __name__ == "__main__":
    main(sys.argv[1])
