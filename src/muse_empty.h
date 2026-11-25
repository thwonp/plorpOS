/* SPDX-License-Identifier: MIT */
#ifndef MUSE_EMPTY_H
#define MUSE_EMPTY_H

/* Muse+ on a Local Source with none of it on the card (plorpos-5tm.23): every
 * tab says where it goes. A line a phrase, at each '\n' (text_wrap), so none
 * is cut mid-phrase: tools/emptywrap-check.c holds every port and face to
 * each line whole. */
#define MUSE_NO_BOOKS      "No audiobooks found.\nPut them in Audiobooks/\nor download from Audiobookshelf"
#define MUSE_NO_MUSIC_SRV  "No music found.\nPut it in Music/\nor download from Plex or Jellyfin"
#define MUSE_NO_MUSIC_CARD "No music found.\nPut it in Music/"   /* the Flip: no Plex, no Jellyfin */

#endif
