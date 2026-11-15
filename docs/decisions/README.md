# Decisions

Architecture Decision Records. One decision per file: the context that forced
it, the options weighed, what was chosen, and what it costs.

They exist because the expensive thing to lose is not *what* was decided, it is
*why*. Six months on, the reasoning is what separates a decision that still
holds from one whose premises expired.

**Accepted records are immutable.** Changed your mind? Write a new one that
supersedes the old, and mark the old `Superseded by NNNN`. Never edit the
reasoning of an accepted record - having believed it is itself the history.

**The bar.** Not everything needs one. A convention that is pervasive but cheap
to reverse belongs in a comment or in `docs/`. An ADR is for a decision that is
expensive to reverse, that a reasonable person would question later, or where
the rejected option was genuinely tempting.

plorpOS is the consumer side of a two-project pair; diatom keeps its own
decisions in [its own tree](https://github.com/thwonp/diatom/tree/main/docs/decisions),
and anything crossing the socket between them is recorded on the diatom side
because that is where the protocol is defined. The ADR numbers the other docs
cite (0023, 0025, 0026, ...) are diatom's unless they say otherwise.

| # | Title | Status |
|---|---|---|
| [0001](0001-screens-declare-menus.md) | Screens declare menus; a runner owns the loop | Accepted |
