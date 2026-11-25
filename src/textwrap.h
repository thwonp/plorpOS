/* SPDX-License-Identifier: MIT */
#ifndef TEXTWRAP_H
#define TEXTWRAP_H

#define TEXTWRAP_LINE 192

/* `text` in at most `max` lines `room` wide: cut at each '\n' first, then at
 * the last space that fits. `width` measures a line (in the caller's units).
 * Returns how many lines; a line with no space to cut at stays as it is, and
 * text past the last line is dropped. A list's empty line (main.c). */
int text_wrap(const char *text, int room, int (*width)(void *ctx, const char *s),
              void *ctx, char lines[][TEXTWRAP_LINE], int max);

#endif
