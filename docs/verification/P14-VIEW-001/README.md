# P14-VIEW-001 — Base / Projected Views

```text
STATUS:          PASS
BASELINE:        0e0f633 (P14-SHEET-001), clean, HEAD == origin/main
SCOPE:           the view model, the six standard orientations, the isometric,
                 projected views and their alignment, per-view scale, sheet
                 placement, and projection of a part or a component. No
                 hidden-line removal, no sections, no dimensions, no
                 annotations, no export.
IMPLEMENTATION:  ADR-018; ViewId; View as a DocumentObject; the projection
                 basis and its derivations; its JSON mapping; the CLI
                 description
TESTS:           43 new -- 15 in ViewProjectionTests.cpp, 23 in ViewTests.cpp,
                 5 in ViewAssemblyTests.cpp; 1504 assertions
REGRESSION:      1763/1763 on debug, release and debug-shared, each from
                 clean; 683/683 five times over in release and debug;
                 0 compiler warnings in all three builds; 17/17 stages exit 0
ADVERSARIAL:     18 questions, 1 finding, 1 production defect, fixed and
                 covered by a regression test
EVIDENCE:        this directory
```

## Scope

Implements
[ADR-013](../../architecture/decisions/ADR-013-view-orientation-and-drawing-scale.md)
(orientation and scale) and
[ADR-018](../../architecture/decisions/ADR-018-projection-convention-is-sheet-intent.md)
(the projection convention), on the identity and canonical/derived rules
ADR-011 and ADR-017 set.

Absent on purpose, and verified absent: hidden-line classification, section
and detail views, dimensions, annotations and export.

## The decision this milestone had to make first

**No P14 ADR chose first-angle or third-angle projection**, and that rule
decides which side of its parent every projected view lands on. Searching
`docs/`, `include/` and `src/` for `first.angle`, `third.angle`, `ISO 5456`
and `ASME Y14.3` found nothing. The milestone brief says such a convention
"must not remain implicit" and to resolve it as an architecture decision
rather than hardcode it, so implementation stopped until
[ADR-018](../../architecture/decisions/ADR-018-projection-convention-is-sheet-intent.md)
was written.

It makes the convention **per-sheet drawing intent, defaulting to first
angle**: ISO 128 requires the convention to be *shown* on the drawing, and a
value that must be shown to be read correctly is intent by definition
(ADR-011). The default is first angle because the project is already ISO 216
sheets, ISO 5455 scales, ISO 273 holes, ISO 286 tolerances and ISO 965
threads; placing views by ASME inside that would be internally inconsistent.

## The view contract

```text
CANONICAL — persisted                 DERIVED — computed on every call
---------------------                 --------------------------------
the sheet it sits on                  the projection basis
the source it draws (base views)      the projected 2D geometry
the orientation (base views)          the projected bounds
the parent and direction (projected)  a projected view's placement
the spacing from the parent           a view's effective source
the scale, when it overrides          the effective scale
the placement (base views)
```

A view is a **base** view or a **projected** view and never both; `validate()`
refuses a definition that is neither or both.

Three of those rows are the milestone's load-bearing choices:

- **A projected view has no orientation of its own.** It has a parent and a
  direction, and its basis is derived from the parent's. ADR-018 removes the
  possibility of contradicting its parent rather than testing for it.
- **A projected view has no source of its own.** It inherits its parent's,
  walking the chain, so two views of one thing cannot disagree about what that
  thing is.
- **A projected view stores no placement.** Its placement is derived from its
  parent's, its spacing and the sheet's convention — which is what makes the
  alignment *exact* rather than *maintained*.

## View identity

`ViewId` follows the established pattern: a tag, an `isDocumentObjectTag`
specialisation so it widens to `ObjectId`, and an alias. It comes from the
document's one allocator, so no ID is reused.

A view's `dependencies()` are its sheet, the object it draws, and its parent
if it has one — so the graph dirties a view when any of them moves. An
external source contributes no edge, per ADR-003.

## Standard orientations

Under ADR-013 a view frame's normal points **from the model toward the
viewer**, which makes the three principal frames the three principal views.
Every basis below was written out by hand in the test file from what it means
to stand somewhere and look at the origin, and compared component by
component:

| View | right | up | normal (toward viewer) | viewer stands at |
| --- | --- | --- | --- | --- |
| Front | +X | +Z | **−Y** | −Y |
| Rear | −X | +Z | +Y | +Y |
| Right | +Y | +Z | +X | +X |
| Left | −Y | +Z | −X | −X |
| Top | +X | +Y | +Z | +Z |
| Bottom | +X | −Y | −Z | −Z |

Checked beyond the values themselves: every basis is orthonormal and
right-handed (`up == normal × right`); **no two of the six share a basis**, so
a copy-paste in `basisOf()` cannot pass; and opposite views have exactly
opposed normals, which is what catches a Rear that is really a Front.

## Orthographic equations

```text
x = (P - O) . right        y = (P - O) . up        depth = (P - O) . normal
```

which is `Frame3D::toLocal` and `Frame3D::signedDistance` — qualified since
`P0`. The projection is **not reimplemented**; the milestone supplies the
basis and uses the existing primitive.

The full chain to the sheet:

```text
model point
  -> toLocal                         view-plane coordinates, model units
  -> minus the projected centre      centred on the bounding box
  -> x scale factor                  sheet units
  -> plus the view's placement       sheet coordinates, mm
```

## Mathematical validation

An asymmetric point, `(30, 70, 110)` mm — every coordinate different and no
two equal in magnitude, so a swapped or negated axis cannot give the right
answer by accident. Expected values are the dot products computed in the test:

| View | expected (x, y) mm | from |
| --- | --- | --- |
| Front | (30, 110) | (+X, +Z) |
| Rear | (−30, 110) | (−X, +Z) |
| Right | (70, 110) | (+Y, +Z) |
| Left | (−70, 110) | (−Y, +Z) |
| Top | (30, 70) | (+X, +Y) |
| Bottom | (30, −70) | (+X, −Y) |

```text
maximum projection error: < 1e-9 mm  (the assertion tolerance; the observed
                                      residual is at double-precision noise)
```

Also checked: the model origin projects to (0, 0); a point on the view normal
projects to (0, 0) — which is what "orthographic" means — and its depth equals
its distance; and **depth is positive toward the viewer**, so a point at −Y is
nearer in a Front view than one at +Y. That last one is what catches a normal
pointing the wrong way, which would mirror every view on the drawing.

A 100 × 60 × 40 box — three different sizes, so a swapped axis pair changes
the answer, which a cube could not detect — gives the right extents in all six
views:

```text
Front 100 x 40    Rear 100 x 40    Right 60 x 40
Left   60 x 40    Top  100 x 60    Bottom 100 x 60
```

## Isometric

A standard isometric preset, not a free camera. The normal is (1,1,1)
normalised — looking along (−1,−1,−1) — and up is the model's +Z projected
into the view plane.

```text
each model axis foreshortens to sqrt(2/3) = 0.816496580927726
```

computed in the test as `sqrt(1 - (axis . normal)^2)` and checked for all
three axes to 1e-12. Also checked: +Z projects straight up (x component zero,
y positive), and the two horizontal axes come down symmetrically either side.
No perspective anywhere; the projection is orthographic by construction.

## Projected views and the convention

A projected view's basis is its parent's turned 90° about one of the parent's
own in-plane axes. Derived from Front, the four directions produce **exactly
the standard bases** of Top, Bottom, Right and Left — checked component by
component. Derived from every one of the six parents, each result is still
orthonormal, right-handed, and different from its parent.

The convention, as coordinates rather than as a flag:

| Direction | first angle | third angle |
| --- | --- | --- |
| Top | (0, −1) — below | (0, +1) — above |
| Bottom | (0, +1) | (0, −1) |
| Right | (−1, 0) — to the left | (+1, 0) |
| Left | (+1, 0) | (−1, 0) |

Third angle is the exact negation of first angle in every case, asserted as
such.

## Alignment

| Check | Result |
| --- | --- |
| A Top or Bottom view shares its parent's **x** | exact equality, both conventions |
| A Left or Right view shares its parent's **y** | exact equality, both conventions |
| First angle: Top 80 mm below, Right 90 mm left of a parent at (200, 150) | (200, 70) and (110, 150) |
| Third angle: the same views, opposite sides | (200, 230) and (290, 150) |
| Moving the base view to three different places | the shared coordinate stays **exactly** equal |
| Changing the base view's scale to 1:1, 1:5, 2:1 | alignment unaffected |

The equality is exact rather than approximate because the derivation produces
**the same number** — a Top view's x *is* its parent's x — rather than keeping
two numbers in step. That is the difference between alignment that is derived
and alignment that is maintained, and only the first cannot drift.

Checked with real geometry as well as placement: a Top view projected from a
Front view of the 100 × 60 × 40 box draws 100 × 60, and a Right view draws
60 × 40 — not the parent's 100 × 40.

## Per-view scale

Absent means the sheet's scale; a view may override it. A projected view
inherits the **sheet's**, not its parent's override.

| Scale | drawn width | drawn height |
| --- | --- | --- |
| 1:1 | 100 mm | 40 mm |
| 1:2 | 50 mm | 20 mm |
| 2:1 | 200 mm | 80 mm |

and at every scale the view's centre stays exactly where it was placed, which
is what catches a scale applied after placement instead of before.

## Sheet placement

**Placement anchors the projected bounding-box centre**, stated rather than
left ambiguous. A base view stores it; a projected view derives it.

The box sits at the model origin corner, so centring on the projection rather
than on the model origin is observable: a Front view at (200, 150) spans
x 150–250 and y 130–170.

Moving a view by (+30, −45) shifts **every projected point by exactly that**,
checked point by point rather than by bounding box.

## Model-to-view references

A view names its source by `ObjectReference` — the qualified P13
infrastructure — never a pointer, a kernel handle, an index, a display name or
a path. `effectiveSource` walks the parent chain, so a view projected from a
projected view still resolves to the one source the chain is rooted in.

A source that produced no body **fails explicitly** rather than drawing an
empty view, and the view is unchanged by the failure, so it draws again when
the body returns.

## Part validation

The fixture is a 100 × 60 × 40 box, asymmetric in all three axes. Extents in
every view are arithmetic on those three numbers, done in the test.

**A box projects to exactly 12 straight segments and 36 points** (start, end
and midpoint of each edge), asserted as numbers so the scope boundary cannot
drift silently. See "Known limitations" for what curved edges do.

## Assembly validation

The failure these tests exist to prevent is a view drawn from a component's
**canonical placement** rather than its **solved transform**. Both are real
numbers on the same component, and for an unmated assembly they are equal — so
a test on an unmated assembly cannot tell them apart. Every fixture therefore
makes them observably different.

| Check | Result |
| --- | --- |
| A view of a component draws its part at the solved transform | PASS |
| Two instances of one part have different IDs, different solved transforms, and each view resolves to its own occurrence | PASS |
| A component rotated 90° about Z draws 60 × 100 instead of 100 × 60 | PASS |
| A component with **no** solved transform fails explicitly rather than drawing at the origin | PASS, `FailedPrecondition` |
| It draws again once the solved state is available | PASS |
| An assembly projection is bit-identical between two calls | PASS |

The rotation test is the decisive one: a view that ignored the solved
transform would still draw 100 × 60.

## Failure cases

Twelve malformed definitions, each refused with a message naming the problem:
no sheet; both an orientation and a parent; neither; a base view carrying a
direction; a projected view naming a source; zero spacing; a projected view
storing a placement; a zero scale term; a sheet that is not in the document; a
source that is not; a parent that is not; and a parent on another sheet.

Also refused: a view projected from itself, a **loop** of projected views (see
the adversarial finding), and removing a view that another is still projected
from.

A malformed file is refused on load, because `View::create` validates and the
reader goes through it.

## Determinism

| Check | Result |
| --- | --- |
| The same view gives a bit-identical basis and projection | PASS, all seven orientations |
| The same view projects bit-identically twice | PASS, base and projected |
| An assembly view projects bit-identically twice | PASS |
| 683 related tests, five times over, in release and in debug | PASS |
| Debug, Release and Debug-shared | 1763/1763 each |

Nothing is cached, so "twice" is the same arithmetic twice rather than a cache
hit. Tolerances: 1e-12 for dimensionless well-conditioned algebra (basis
components, foreshortening ratios) and 1e-9 mm for lengths after subtraction,
which is CLAUDE.md's figure for well-conditioned double-precision algebra
expressed in the unit compared.

## Adversarial review

Eighteen questions. One finding, and it was a genuine crash.

| Question | Answer |
| --- | --- |
| Is Front mirrored? | No — basis and depth sign both tested |
| Is Top upside-down? | No — hand-written basis |
| Are Left and Right swapped? | No — and no two views share a basis |
| Does Rear equal Front? | No — opposed normals asserted |
| Is the basis ever left-handed? | No — `Frame3D::fromAxes` refuses, and `up == normal × right` is asserted |
| Is scale applied twice? | No — the drawn size is exact at 1:1, 1:2 and 2:1 |
| Is placement applied before scale? | No — the centre stays put at every scale |
| Can 1:2 enlarge? | No |
| Can alignment drift after moving the base? | No — exact equality at three positions |
| Can a projected view contradict its parent? | **Structurally impossible** — it has no orientation field |
| Can an assembly view use canonical placement? | No — the rotation test would fail |
| Can two instances collapse onto one identity? | No — each resolves to its own occurrence |
| Can an unresolved reference keep stale geometry? | No — nothing is cached |
| Can view order become identity? | No — ascending ID, no order field |
| Can save/load change orientation? | No — round-tripped |
| Can Debug and Release disagree? | No — 1763/1763 in all three |
| Did HLR or section scope leak in? | No — 12 segments for a box, curves sampled only |
| **Can a loop of projected views be built?** | **It could. See below.** |

### Finding — a loop of projected views recursed until the stack ran out

Only *self*-parenting was prevented. A view projected from a second view that
was itself projected from the first was constructible:

```text
create A          base view
create B          projected from A
edit   A          projected from B     <- accepted
```

and `effectiveBasis` and `effectivePlacement` walked the parent chain by
direct recursion **with no depth guard**, so evaluating either would have
recursed until the stack was exhausted — a crash, not a diagnostic.

Fixed two ways. The loop is refused at the edit, walking up from the proposed
parent and failing if the chain reaches the view being edited, with a message
that names what is wrong. And both recursive derivations are depth-limited as
a second line, because a document loaded from a file has not been through that
check.

Covered by `View_ALoopOfProjectedViewsIsRefusedRatherThanRecursedInto`, which
checks the two-view loop, a three-view loop, and that the views still derive
normally afterwards.

An existing test then caught the fix: the general loop message had swallowed
the clearer "cannot be projected from itself" one. Both now exist, the
specific case keeping the specific message.

## Regression

| Preset | Configure | Clean | Build | No-op rebuild | Tests | Warnings |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0 | exit 0, 11m52s | **0 compiles** | **1763/1763** | 0 |
| `release` | exit 0 | exit 0 | exit 0, 12m28s | **0 compiles** | **1763/1763** | 0 |
| `debug-shared` | exit 0 | exit 0 | exit 0, 10m02s | **0 compiles** | **1763/1763** | 0 |

```text
repeat release, --repeat until-fail:5   683/683
repeat debug,   --repeat until-fail:5   683/683

17 stages with a recorded exit code, 17 of them 0
total 59m59s
```

1720 → 1763 is this milestone's 43 new tests, and all 43 are confirmed **by
name** in each of the three ctest logs (30 `ViewProjection_`, 46 `View_` and
10 `ViewAssembly_` log lines, each test appearing at its start and its
result).

## Known limitations

- **Curved edges contribute sampled points, not curves.** A straight edge
  becomes a segment; a circle contributes its start, end and midpoint only.
  Drawing a curve, and deciding which parts of it are visible, is
  `P14-HLR-001`. The boundary is asserted as a number: a box gives 12
  segments.
- **Every edge is projected, including ones the model hides.** There is no
  visibility classification yet — that is the whole of `P14-HLR-001` — and
  this is the documented scope boundary the brief allows.
- **A view draws one component, not a whole assembly.** Multiple instances,
  rotation and solved transforms are all validated, but a single view showing
  every active component at once needs a decision about what "the assembly" is
  as a reference, and no ADR has made it.
- **Isometric is a preset, not a free camera.** A general arbitrary
  orientation is expressible as a `Frame3D` but is not offered as intent.
- **Suppression is not consulted.** A view of a suppressed component still
  draws it. A view of an assembly would have to respect the active
  configuration; a view of one named component arguably should too, and that
  is `P14-ASM-001`'s to settle.
- **A view is not clipped to its sheet.** A view placed outside the usable
  region still projects; nothing refuses it.
- **No drawing-specific commands.** The generic `AddObjectCommand` and
  `DeleteObjectCommand` work, as `P14-SHEET-001` established; validating
  wrappers are `P14-CMD-001`'s.
- **A view has no regeneration handler**, so an unresolvable source is
  reported when the view is drawn rather than during regeneration.
  `P14-REGEN-001` owns that.

## Result

```text
TASK:            P14-VIEW-001 — Base / projected views
IMPLEMENTATION:  ADR-018; ViewId; View; the projection basis and its
                 derivations; JSON; the CLI description
TESTS:           43 new, 1504 assertions; 1763/1763 in debug, release and
                 debug-shared, each from clean; 683/683 five times over
VALIDATION:      every basis and every projected coordinate against
                 arithmetic done in the test, never read back from BetterCAD
ADVERSARIAL:     18 questions, 1 finding, 1 production defect, fixed
WARNINGS:        0 in all three builds
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-VIEW-001 → [x]
NEXT:            P14-VIEW-002 — section / detail / auxiliary views
```

## Files

```text
qualification/qualify.cmd                the three-preset harness
qualification/run-qualification.cmd      its entry point
qualification/qualification-times.txt    every stage, its exit code, tree IDs
qualification/configure-*.log            3 presets
qualification/clean-*.log                3 presets
qualification/build-*.log                3 presets, 0 warnings each
qualification/rebuild-*.log              the no-op freshness proof
qualification/ctest-*.log                3 presets, 1763/1763 each
qualification/ctest-repeat-*.log         release and debug, until-fail:5
```
