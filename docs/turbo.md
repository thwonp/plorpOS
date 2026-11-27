# Turbo

**Turbo is chosen in the game, per button, with the Turbo Assign hotkey**
(plorpos-tkh, plorpos-c4l). Bind it under Hotkeys (in a game, or in
<Shelf> Defaults). Then,
in a game:

- Turbo Assign, then the button to turbo, then the button it goes on.
  Turbo Assign + A + X: X is a turbo A - held, A is pressed three frames and
  released three, about ten times a second at 60 Hz - and A stays a plain A.
  Turbo Assign + A + A: A is its own turbo. The presses that pick never reach
  the game.
- The same again: that button is normal again. Another turbo on the same
  button replaces it. Any number of buttons can carry a turbo at once.
- Turbo Assign again before the last press: cancelled.
- Turbo Assign held two seconds: every turbo button is cleared.

A turbo is whatever the first button sends, so it follows Button Mapping
(Hotkeys > Button Mapping): with A and B swapped there, Turbo Assign + A + X
makes X a turbo of the game's B.

A, B, X, Y, L1, R1, L2 and R2 can be turbo. Directions, START and SELECT
cannot. A notice says what happened each time.

**It is kept for the game** while **Save Per-Game Overrides** is on (Game
Settings, the default; plorpos-bn9.4): every change is saved as
`turbomap.<TAG>/<ROM file>` (`x:a,a:a`, button:turbo) in the library database,
and the next launch starts with it. Off, a change lasts until the game quits.
A saved map always applies, whichever way the setting is.

**A shelf has a default**, `turbomap.<TAG>`, set from <Shelf> Defaults >
Turbo Assignments. A game with no map of its own starts with it. A game's
first change copies the whole default and changes that copy, so later changes
to the default do not reach it. A game that clears every turbo keeps an empty
row, so it stays at none.

**Turbo Assignments** is the same map as a list: one row per button (A to R2),
None or the turbo it fires, left/right/A to change it. From <Shelf> Defaults it
edits the shelf's; from a game's X menu > Game Overrides it edits that game's,
with **Use Shelf Default** to drop the game's own. Reset Overrides > Turbo
Assignments drops every game's on the shelf.

A game saved by an older build as `turbo.<TAG>/<ROM file>` is read once and
converted. The old `persistturbo` setting is no longer read.

## Why X and Y are not turbo any more

Until plorpos-tkh, X and Y were a fixed turbo A and turbo B on the nine systems
with two face buttons (`turbo.<tag>` = `x:a~3,y:b~3` in the library database),
and on Game Boy Advance mGBA's own Turbo L and Turbo R sat on L2 and R2. Both
took buttons the player wants for hotkeys. The fixed maps are gone - the
launcher drops any `turbo.<tag>` rows a card still has - and on Game Boy
Advance L2 and R2 are mapped to nothing, so mGBA's turbo never sees them.

## Where the pulsing happens

Diatom does it, not the emulator core, so it behaves the same on every system.
Diatom reports Turbo Assign to the launcher (`TURBO arm=1|0`, `btn=<name>`,
`clear=1`; diatom ADR-0045) and keeps nothing itself. The launcher holds the
game's turbo buttons and answers each change with the whole map, `b:b~3` per
turbo button, plus `l2:none,r2:none` on Game Boy Advance. It sends that map
just after RUN too, because RUN resets the map to identity.

`turbo_period` is half a cycle in frames - see diatom's `src/env.c` and its
ADR-0028.
