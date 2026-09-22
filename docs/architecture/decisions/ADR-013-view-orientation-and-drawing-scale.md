# ADR-013 — A view's orientation is a frame facing the viewer; its scale is an exact ratio

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-011 (drawing intent is canonical)
```

## Context

Two conventions in a drawing system are worth getting wrong exactly once:
which way a view looks, and what "1:2" means. Both are the kind of thing that
is decided implicitly by the first piece of code to need it and then argued
about for years, so both are decided here.

There are five coordinate spaces between a solid and a sheet of paper, and
nothing in the repository names them yet:

```text
model space        the document's own 3D coordinates, SI internally
view space         3D, oriented so one axis is the viewing direction
projection plane   2D, model-sized
sheet space        2D, paper-sized, origin at the sheet corner
paper              physical millimetres on a printed or exported sheet
```

The repository already has the primitive for the first three steps.
`Frame3D::toLocal(Point3D) -> Point2D`
(`include/bettercad/core/math/Frame.hpp:50`) is documented as "Local
coordinates of the point's orthogonal projection onto the plane" — which is
orthographic projection, qualified and tested since `P0`. `Point2D` exists
(`Point.hpp:10`). What is missing is only the convention for *which* frame.

## Options — orientation

1. **The view frame's normal points from the model toward the viewer.** A view
   is a `Frame3D`; `toLocal` gives sheet-plane coordinates directly.
2. **The view frame's normal is the viewing direction** (pointing away from
   the viewer, into the sheet).
3. **A view stores a direction plus an up-vector**, and builds a frame when
   asked.

## Options — scale

1. **An exact rational `paper : model` pair**, e.g. `{1, 2}`.
2. **A `double` factor plus an optional `ParameterId`**, matching every other
   driven field in the codebase.
3. **An enumeration of the standard scales** of ISO 5455.

## Decision

**Orientation: Option 1.** A view's orientation is a `Frame3D` whose **normal
points from the model toward the viewer**. The frame's X axis is *right* on
the sheet and its Y axis is *up*. Projection is `Frame3D::toLocal`.

**Scale: Option 1.** A view's scale is an exact rational pair
`paper : model`, stored as written. The factor is `paper / model`, so `1:2`
halves and `2:1` doubles, and a 100 mm feature at 1:2 measures 50 mm with a
ruler on the printed sheet.

**The chain**, in full:

```text
model point (3D)
  → Frame3D::toLocal(p)                 projection-plane 2D, model units
  → x (paper / model)                   sheet units
  → + the view's placement on the sheet sheet coordinates, mm from the corner
  → export                              physical mm
```

## Rationale

Option 1 for orientation was chosen because it turns out not to be a new
convention at all. Under "normal faces the viewer", the three principal frames
the codebase already has **are** the three principal views:

| View | Frame | X (right) | Y (up) | Normal (toward viewer) |
| --- | --- | --- | --- | --- |
| Front | `Frame3D::xz()` | +X | +Z | **−Y** |
| Right | `Frame3D::yz()` | +Y | +Z | +X |
| Top | `Frame3D::xy()` | +X | +Y | +Z |

and Rear, Left and Bottom are those three reversed. This was checked
arithmetically against the frames' documented definitions — 26/26, recorded in
`docs/verification/P14-ARCH-001/`. A 100 × 60 × 40 box projects to 100 wide and
40 tall in Front, with the 60 of depth collapsing eight corners onto four
points.

That `Frame3D::xz()` has normal **−Y** is the same fact that cost `P13-REFMOD-001`
a defect: a positive `Distance` mate across the XZ plane moves a component to
negative *y*, which put every located component on the wrong side of its deck
while every test still passed. Here the same fact is what makes a Front view
come out correctly, and choosing Option 2 would invert all six standard views
relative to the frames already in the codebase.

Option 3 was rejected because a direction plus an up-vector is a `Frame3D`
with the invariants unenforced. `Frame3D::create` already rejects a
near-parallel pair and `fromAxes` already checks orthonormality to 1e-12 and
restores saved frames bit for bit — rebuilding that in a view definition
would be a second, weaker copy.

For scale, Option 2 is what every other driven field in the codebase looks
like — a value plus an optional `ParameterId`, as `LinearPatternFeature` does
for its count — and it was the leading candidate until the label was
considered. **The scale is intent, and its printed form is part of it.** A
title block reads `SCALE 1:2`. Stored as a `double`, `1:2` and `2:4` are the
same value and neither can be printed back reliably; `1:3` is not
representable at all, so a drawing saved at 1:3 and reloaded is at
0.333333333333333314829616256247. The pair keeps the label exact and the
arithmetic exact, and `UnitScale`
(`include/bettercad/core/units/Quantity.hpp:15-27`) is the codebase's own
precedent for representing a conversion as an exact numerator and denominator
rather than a double.

Option 3 was rejected as too rigid: ISO 5455 lists preferred scales, not
permitted ones, and a detail view at 7:2 is legitimate.

Nothing is lost by declining Option 2's parameter binding: a drawing scale is
chosen from a standard set by a draughtsman, not driven by a design equation.
If a real need for a driven scale appears, a `std::optional<ParameterId>`
beside the pair is additive.

## Consequences

- No new projection primitive. A view's projection is `Frame3D::toLocal`,
  already qualified.
- The six standard views are three stored frames and a reversal flag, not six
  hand-written orientation tables.
- An isometric or arbitrary view is the same type — any `Frame3D` — so
  nothing special-cases the standard six.
- Scale needs a small exact-rational type. It is new; `UnitScale` is the
  shape to follow but not the type to reuse, because a `UnitScale` means
  "one unit is n/d coherent SI units", which is a different statement.
- **A dimension's text shows the model value, never the drawn length.** A
  100 mm feature drawn at 1:2 is 50 mm of ink and says `100`. This follows
  from ADR-011 — the value is measured from the model — but it is the place
  where a scale bug would hide, so it is stated here too.
- Sheet coordinates are millimetres from a sheet corner, so an A3 sheet is
  0..420 by 0..297 regardless of the model's units, and imperial sheets are
  expressible because `units::inch` already exists.
- There is no `Vector2D`, `Direction2D`, `Frame2D` or `RigidTransform2D` in
  `core/math` — only `Point2D` and `BoundingBox2D`. The sheet-space
  vocabulary a view placement needs is a small addition to `core/math`,
  beside the existing 2D types, not a private invention in `drawing`.

## Rejected alternatives, and what would make them right

**Normal as the viewing direction (orientation Option 2)** would be right in a
codebase whose principal frames were defined the other way round. This one's
are not, and matching them is worth more than matching any external
convention.

**A `double` scale (Option 2)** becomes right if a drawing scale ever needs to
be driven by a design equation, at which point the pair gains an optional
`ParameterId` and the exact label is kept for the undriven case.

## Verification

For `P14`: each of the six standard views projects a known box to
hand-computed extents; the depth axis collapses; `1:2` halves and `2:1`
doubles a measured length; a scale saved and reloaded is the same pair, and
`1:3` survives exactly; a view's content lands inside its sheet at the
placement given; and a dimension across a feature reports the model value
irrespective of the view's scale.
