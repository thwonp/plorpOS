/* SPDX-License-Identifier: MIT */
#include "textwrap.h"

#include <string.h>

int text_wrap(const char *text, int room, int (*width)(void *ctx, const char *s),
              void *ctx, char lines[][TEXTWRAP_LINE], int max)
{
	int n = 0;

	while (*text && n < max) {
		const char *nl = strchr(text, '\n');
		size_t k = nl ? (size_t)(nl - text) : strlen(text);
		char *l = lines[n++], *sp;

		if (k >= TEXTWRAP_LINE) k = TEXTWRAP_LINE - 1;
		memcpy(l, text, k);
		l[k] = '\0';
		/* The last space that fits; none: the whole word, too long or not. */
		while (width(ctx, l) > room && (sp = strrchr(l, ' '))) *sp = '\0';
		text += strlen(l);
		while (*text == ' ' || *text == '\n') text++;
	}
	return n;
}
