# ADR-011 — Drawing intent is canonical; every projected curve is derived

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-005 (placement is intent, transforms are derived)
           ADR-010 (drawings live in the document)
```

## Context

A drawing holds two very different kinds of thing, and the whole subsystem
turns on not confusing them.

One kind is what an engineer decided: this sheet is A3, this view is a Front
view at 1:2 placed here, this dimension measures *that* face to *that* face
and is shown to two decimals. None of it can be recomputed from the model,
because none of it is implied by the model. It is intent.

The other kind is what falls out of the first: the projected curves, which
edges are visible and which hidden, where the silhouette of a cylinder lies,
what the dimension's value actually *is* in millimetres, which rows the BOM
has. All of it is a pure function of the model and the intent.

`P13` already answered this question once, for the assembly solve. ADR-005
made the solved transform derived state, returned and never written, and
`P13-REGEN-001` added the sharper rule that a *stale* derived result is worse
than an absent one: "a transform that is one edit out of date still renders,
which makes it worse than absent" (`src/assembly/Resolution.cpp:262-264`).

A projected drawing view has exactly that failure mode, and worse
consequences. A stale transform puts a part in the wrong place, which an
engineer may notice. A stale projection is a *drawing that looks right and is
wrong*, and drawings are what get sent to a machine shop.

## Options

1. **Derive everything.** Persist intent only; recompute all 2D geometry.
2. **Cache the projection in the file.** Persist the projected curves too, so
   a large assembly drawing opens instantly, with a validity stamp.
3. **Cache in a sidecar.** As 2, but beside the `.bcad` rather than inside it.

## Decision

**Option 1.** The `.bcad` file holds drawing **intent** only. No projected
curve, no edge classification, no computed dimension value and no BOM row is
ever written to it.

The boundary, stated exactly:

```text
CANONICAL — persisted                  DERIVED — recomputed, never persisted
--------------------------            -------------------------------------
sheet size, orientation, margins       projected 2D curves
title-block field values               visible / hidden / silhouette sets
view orientation                       section cut curves and hatch geometry
view scale                             the dimension's measured VALUE
view placement on the sheet            witness and extension line geometry
section / detail definition            computed BOM rows and quantities
which objects a view shows             balloon leader routing
dimension references + formatting      the export scene handed to a writer
annotation text and placement
tolerance intent (± , limits, GD&T)
BOM settings and item numbering rule
```

The two rows that matter most are on opposite sides of the line. A
dimension's **references and formatting** are intent; a dimension's
**value** is derived, measured from the model every time. That split is what
makes a drawing update when the model does, and it is the same split ADR-005
drew between placement intent and the solved transform.

## Rationale

Option 2 fails on the argument that has already been made twice in this
project and is stronger here than in either previous case.

A cached projection is correct only while the model has not moved. The moment
it has, the file contains a drawing that renders cleanly, prints cleanly, and
describes a part that no longer exists. Every mitigation is a validity stamp
of some kind — a revision, a hash, a timestamp — and every validity stamp is
a thing that can be wrong. `P13-REGEN-001` measured exactly this class of
problem and found the case a revision-based stamp misses: a configuration
overriding a free parameter changes **no object's revision**, because the base
value is untouched and only the value in force differs
(`src/assembly/Resolution.cpp:116-122`). A drawing cached against revisions
would go stale on precisely that edit, silently.

The performance argument for Option 2 is real and is not dismissed: hidden-line
removal on a large assembly is expensive, and opening a drawing under Option 1
pays for it. That cost is accepted here for the same reason ADR-005 accepted
"opening an assembly costs a solve": correctness first, and if it ever matters
it is a **measured** performance decision, not a reason to persist derived
state. ADR-014 records where a cache may later live and the discipline it must
follow.

Option 3 has every correctness problem of Option 2 plus a new one: two files
that can be separated, so the cache can outlive the model or the model the
cache. It also reintroduces the multi-file problem ADR-010 just declined.

## Consequences

- Opening a drawing regenerates and re-projects. This is the accepted cost,
  and it is the reason a drawing can never be stale.
- The file format gains no geometry section. Combined with ADR-010, a drawing
  is an `objects`-array citizen and nothing else.
- A drawing of a broken model must publish **nothing** rather than its last
  good projection — the all-or-nothing rule `P13-REGEN-001` established. A
  view whose source failed is an explicitly failed view, not an old picture.
- Dimension values are never stored, so a saved drawing cannot disagree with
  its model about a measurement. It can only fail to resolve, which is loud.
- A future "drawing revision" feature — freezing what was issued to
  manufacturing — is **not** served by persisting the projection. It is a
  versioning problem (`P22`), and solving it by caching geometry here would
  be solving the wrong problem in the wrong layer.

## Rejected alternatives, and what would make them right

**Caching the projection (Options 2, 3)** becomes worth revisiting only with
a measurement showing that re-projection dominates a real workflow, and only
with an invalidation rule that does not rely on revisions alone — because the
configuration-override case proves revisions are not sufficient. ADR-014
specifies that any such cache must compare its own resolved inputs, the way
`assembly.solve` does.

## Verification

For `P14`: saving a document before and after a drawing is regenerated
produces byte-identical files; the saved bytes contain no projected
coordinate, no edge classification and no computed dimension value; a
dimension's text is measured from the model, not read from the file; and a
view whose source object fails publishes no geometry at all.
