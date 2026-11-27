# 0001. Screens declare menus; a runner owns the loop

- **Status:** Accepted
- **Date:** 2026-09-04
- **Supersedes:** -
- **Superseded by:** -

## Context

Every list-shaped screen is drawn by `menu_draw`, and that is the only thing
they share. Each one writes the surrounding loop by hand: poll input, check
quit, check power and idle, check back, move the cursor, act on accept, draw the
backdrop, dim it, draw the menu, present, delay. There are five or six copies.

On 2026-09-04 five defects turned up in one evening, all downstream of that:

1. **The cursor could rest on a dead row.** `live == false` has always meant
   "does nothing", but every screen walks `(sel + 1) % nrows`, so a placeholder
   could be selected and answered with A to no effect. Fixed in `wifi_screen`
   only; the other screens have not been audited.
2. **There was no confirm anywhere in the launcher.** The first destructive
   action needed one written from scratch.
3. **`Y` rescanned the Wi-Fi list from the day that screen was written** and
   nothing on screen ever said so - a feature available only to people who
   already knew it existed.
4. **A rule existed for headings and nowhere else**, so the first screen wanting
   to divide a list from a footer would have painted its own line, and the next
   would have painted a slightly different one.
5. **`menu_measure` is a second copy of `menu_draw`'s measuring pass**,
   maintained separately. It would have called `ui_text_width` on a NULL label
   the first time anything used `MENU_RULE`.

None of these are hard problems. They are all the same problem: `menu_draw`
abstracts *drawing* a menu and nothing abstracts *being* one, so the rules are
conventions each screen can forget independently. `docs/how-it-works.md#menus` writes those
conventions down, and says plainly that a convention enforced by reading is not
enforced.

A further constraint, and it is what tipped this: **there is no way to test a
menu.** `make check` already runs offline checks for hare, httpd, xfer, idle,
rahash and others. Nothing can check a menu, because building the rows is welded
to a loop that needs SDL, a renderer and a device.

There are no users and no release pressure, so migration cost is a weak
consideration compared to getting the shape right.

## Options considered

### Option A - a runner owns the loop; screens supply callbacks

    void menu_run(app *a, const char *heading, unsigned accent,
                  int  (*build)(void *ctx, menu_row *rows, int max),
                  menu_result (*on_key)(void *ctx, in_button key, int sel),
                  void *ctx);

The runner owns polling, power, idle, quit, back, cursor movement including
skipping dead rows, and the whole frame. A screen declares what it contains and
what its keys mean.

Costs: inverts control. Each screen's locals - `nets[]`, `vals[]`, `on`,
`rescan`, cursors, scratch buffers - become a named context struct. That is real
work per screen, though it also replaces a pile of function locals with a
declared state.

Screens that interleave a keyboard prompt, a confirm or a wait panel mid-flow
call those from the key handler. They each run their own loop already, so this
works synchronously and needs nothing new.

### Option B - a stepper; screens keep their loops

    menu_ctx m;
    menu_begin(&m, a, "Wi-Fi", MENU_ACCENT);
    while (menu_step(&m)) { ...build rows...; if (m.chose) ... }

Screens keep their shape and their locals; the helper owns one iteration. It
still centralizes every rule that was broken above.

Rejected. Two reasons, and the first was originally argued the other way:

- **Migration is not the discriminator.** B was preferred on the grounds that A
  converts the launcher in one commit. That is wrong - `menu_run` is a function,
  and hand-written loops can sit beside it while screens move over one at a
  time. The incremental path exists for both.
- **B cannot be tested.** Row building stays inside a loop that needs SDL and a
  device, so a menu can only be checked by looking at it. That is the difference
  between a rule that is tested and a rule that is read about, and it is the
  whole reason `make check` exists.

## Decision

**Option A.** Screens declare their rows and their key meanings; `menu_run` owns
the loop, the frame, and every rule in `docs/how-it-works.md#rules-a-screen-must-follow`.

The `build` callback must be callable with no renderer and no device, so that
"given this state, what does this menu contain" is answerable by `make check`.
That is a requirement of the design, not a side effect: if building rows ever
needs a live SDL context, this decision has failed.

Migration is screen by screen. Hand-written loops and `menu_run` coexist until
the last screen moves.

## Consequences

**Easier.** The five defects above become structurally impossible rather than
documented. Menus gain offline tests for the first time. A new screen gets
power, idle, back and cursor behavior for free instead of retyping them
correctly. Each screen's state becomes a named struct rather than scattered
locals.

**Harder.** Control is inverted, which reads less directly than a `while` loop
for anyone following a single screen. Screens with genuinely unusual flow -
Over The Hare polls a server every frame and holds its own idle rules - may fit
badly, and if one does, that is a signal about the runner's shape rather than a
reason to write a private loop.

**Foreclosed.** Screens owning their own frame. Anything drawn outside
`menu_draw` and the runner's backdrop is now a deliberate exception.

## Revisit if

- A screen cannot express itself through `build` and `on_key` without the runner
  growing a special case for it. One special case is a signal; two means the
  shape is wrong.
- `build` is ever unable to run without a renderer. That breaks the testability
  the decision rests on, and the decision should be reopened rather than the
  requirement quietly dropped.
