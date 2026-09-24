# BetterCAD — TODO

> `[x]` = implemented + tested + independently validated + adversarially reviewed + regression clean + evidence recorded.
> Qualification milestones require the final qualified tree to match the committed tree.
> Work top-to-bottom. Stop at failed gates. Never fake evidence.

---

## Status

```text
Current:   P14 — Technical Drawings
Next:      P14-REFMOD-001 — production drawing reference models.
           P14-STREF-001 is STILL OPEN and comes first if it is authorized:
           its audit is done and 11 of its 12 checks pass, but one gate --
           "no silent rebinding" -- is NOT met and the milestone is not [x].
           A chamfer face is named by its edge reference's POSITION in the
           chamfer's edge list, so reordering that list silently moves any
           drawing reference to it. Demonstrated in
           Reference_AChamferFaceIsNamedByItsPositionInTheChamfersEdgeList.
           Closing it changes ChamferDefinition and its file format, and the
           committed reference models with chamfers, so it needs its own
           authorization.
Carried:   nothing. P14-HLR-001's assembly-to-assembly occlusion validation,
           carried open since that milestone, is CLOSED by P14-ASM-001 with
           real multi-component fixtures
Decision:  move build and test output off OneDrive. NOW DUE. The filesystem
           fault failed a determinism repeat in P14-DIM-001 (debug),
           P14-ANNO-001 (release) and now P14-BOM-001 (debug) — three
           milestones, both repeat presets. In P14-BOM-001 the same test
           passed three times in one run before failing on the fourth
           repeat. Each time one controlled rerun passed. It needs its own
           decision because the build directory lives in CMakePresets.json,
           inside the frozen qualified tree.

Released:  v0.1.0 — P0–P10
Qualified: P11, P12, P13
```

Completed milestones and their evidence live in [ROADMAP.md](ROADMAP.md).

---

# P14 — Technical Drawings

## Goal

Produce manufacturing-ready 2D drawings from BetterCAD parts and assemblies while preserving stable links to the 3D model.

```text
3D model
→ drawing document
→ sheets
→ projected views
→ annotations
→ dimensions
→ sections/details
→ BOM/balloons
→ export/print
```

Drawing geometry is **derived from model intent**.

Do not duplicate or replace the authoritative 3D model.

---

# P14 Sequence

```text
P14-ARCH-001     Drawing architecture and contracts
P14-SHEET-001    Drawing document / sheets / formats
P14-VIEW-001     Base and projected drawing views
P14-VIEW-002     Section / detail / auxiliary views
P14-HLR-001      Hidden-line / visible-edge generation
P14-DIM-001      Linear / angular / radial / diameter dimensions
P14-ANNO-001     Notes / symbols / centerlines / centermarks
P14-TOL-001      Tolerances / fits / GD&T foundation
P14-ASM-001      Assembly drawing views
P14-BOM-001      BOM tables / item balloons
P14-STREF-001    Stable drawing-to-model references
P14-REGEN-001    Drawing regeneration / model-change propagation
P14-CMD-001      Drawing commands / undo / redo
P14-PERSIST-001  Save / load drawing intent
P14-CLI-001      Headless drawing workflows
P14-EXPORT-001   PDF / SVG / DXF export
P14-REFMOD-001   Production drawing reference models
P14-QUAL-001     Full P14 qualification
```

Do not implement a milestone until its predecessor passes.

---

# DONE — P14-ARCH-001

## Drawing Architecture and Contracts

* [x] Define drawing document ownership model — ADR-010: drawing objects are `DocumentObject`s in the model's own document; a separate drawing document needs cross-document *execution*, which does not exist
* [x] Define canonical vs derived drawing state — ADR-011: intent persisted, every projected curve and every measured value derived and never written
* [x] Define sheet / view / annotation identity model — ADR-017: four `DocumentObject` kinds, because a dimension's dependency on model geometry must be a graph edge; tables and balloons deliberately not allocated yet
* [x] Define model-to-drawing stable-reference contract — ADR-012: ADR-004's vocabulary only. There is **no stable edge name** in the codebase, and `EdgeSignature` breaks on the ordinary parametric edit
* [x] Define units / scale / coordinate conventions — ADR-013: view normal faces the viewer, so the principal frames *are* Front/Right/Top; scale is an exact `paper:model` pair. 26/26 verified
* [x] Define drawing regeneration contract — ADR-014: objects get handlers, geometry is built on demand. The final-pass mechanism cannot carry a drawing's result and its ordering is alphabetical coincidence
* [x] Define drawing module layering — ADR-015: `drawing` 4, `io` → 5, renderer/scripting → 6; projection in `core/geometry`. Second forced renumber
* [x] Define export architecture — ADR-016: one neutral self-validating scene; writers transcribe and compute nothing
* [x] Record architecture decisions as ADRs — ADR-010 … ADR-017, plus a supersession note on ADR-006
* [x] Architecture adversarial review PASS — 19 questions, 5 findings, 0 blocking
* [x] Evidence in `docs/verification/P14-ARCH-001/`

### Gate

```text
drawing architecture coherent
+ canonical/derived separation clear
+ stable model references defined
+ coordinate/scale conventions fixed
+ regeneration contract defined
+ module layering valid
+ export boundary defined
+ ADRs complete
```

Met: eight ADRs, each against two or three serious candidates, and no
executable source or test file changed — the eight qualified tree IDs are the
ones `P13-QUAL-001` recorded.

Four findings from reading the code changed what was designed rather than
confirming it. **There is no stable edge reference in this codebase** —
`EdgeSignature` matches a curve, and its own header says it breaks when the
curve moves, which is the ordinary parametric edit a drawing exists to track.
ADR-004 already refuses `FaceSignature` on that reasoning, so ADR-012 refuses
`EdgeSignature` too and accepts a real capability gap instead of a silent wrong
answer. **The final-pass mechanism cannot carry a drawing's result** — its
return type is `std::map<ComponentId, RigidTransform3D>` and passes run in
alphabetical name order, so a drawing pass would follow the assembly solve by
coincidence; ADR-014 avoids the mechanism entirely. **The layer table has no
room again**, for the second time. And **a drawing subsystem is three places,
not one**: projection behind the OCCT adapter at layer 0, the domain at 4, the
writers at 5.

Worth carrying forward: the standard views are not a new convention. Under
"the view frame's normal points at the viewer", `Frame3D::xz()`, `yz()` and
`xy()` *are* Front, Right and Top — and that `xz()` faces **−Y**, the fact that
put every component on the wrong side of its deck in `P13-REFMOD-001`, is the
same fact that makes a Front view come out right here.

```text
P14-ARCH-001 → [x]
Next → P14-SHEET-001
```

---

# DONE — P14-SHEET-001

## Drawing Documents / Sheets / Formats

* [x] Apply the ADR-015 layer renumber — `drawing` 4, `io` 5, renderer/scripting 6; no existing file's includes had to change
* [x] Update `ARCHITECTURE.md` **and** `docs/architecture.md` with the new table — and `CLAUDE.md`, found stale by two renumbers
* [x] Create the `drawing` module per ADR-015 — links `core` only, contains no OCCT; a new checker fixture fails the build if `drawing` ever includes `io`
* [x] Implement strong sheet ID — `SheetId` widens to `ObjectId`; two compile-failure cases pin what it is not. **No `DrawingId`:** ADR-017 defines none
* [x] Implement sheet creation/deletion — through the ordinary document object lifecycle; a rejected sheet consumes no ID
* [x] Implement standard sheet sizes — ISO 216 A0–A4, the standard's own rounded values, checked against the halving property
* [x] Implement portrait / landscape orientation — format and orientation stored, size derived, so a round trip cannot drift
* [x] Implement drawing units and scale — an exact `paper:model` pair, so `1:3` survives and `2:4` stays distinct from `1:2`
* [x] Implement margins / borders — four lengths; the usable region is derived and margins that leave no room are refused
* [x] Implement title-block data model — eight semantic fields; the scale text, sheet number and count are derived, never stored
* [x] Validate multiple sheets per drawing — deleting the middle sheet moves every number and no ID
* [x] Validate deterministic sheet geometry — bit-identical across 5 formats × 2 orientations, and across a file round trip
* [x] Validate save/load — existing envelope, no new key, no version bump; 11 malformed files refused; all 32 committed models still load
* [x] Adversarial review PASS — 16 questions, 4 findings, 3 production defects, all fixed
* [x] Regression PASS — 1720/1720 on three presets from clean, 0 warnings, 17/17 stages exit 0
* [x] Evidence recorded — `docs/verification/P14-SHEET-001/`

### Gate

```text
sheet model correct
+ identity stable
+ format geometry correct
+ units/scale correct
+ multi-sheet behavior correct
+ persistence PASS
+ determinism PASS
```

Met: 46 new tests; 1720/1720 on `debug`, `release` and `debug-shared`, each
from clean; 539/539 five times over in `release` and `debug`; 0 compiler
warnings in all three builds; 17/17 stages exit 0; the no-op rebuild compiled
0 and linked 0 in every preset.

Three production defects, found three different ways. **The shared build did
not link** — `DrawingScale::label()` was defined out-of-line on a struct with
no export macro, so it was hidden under `-fvisibility=hidden`. Debug passed,
Release passed, both their full suites passed; only `debug-shared` caught it,
which is the entire argument for keeping it a hard gate. **`sheetNumber()` and
`sheetCount()` were `noexcept` while calling a function that allocates**, so
an allocation failure would have terminated the process rather than
propagating — found by reading the diff, not by a test. And **`Sheet::size()`
dereferenced an optional unchecked**, unreachable today and undefined
behaviour the day a format is added without a size.

A fourth was caught by a test doing its job: the writer dispatch arm was an
early `return` instead of a branch in the chain, so a sheet's data replaced
the `{id, type, name, data}` envelope rather than filling it.

Worth carrying forward: what is stored is the **format and the orientation**,
never a width and a height. That is why portrait → landscape → portrait is
bit-identical rather than nearly so — there is nothing stored that could
drift. The same reasoning made the scale an exact pair instead of a double.

```text
P14-SHEET-001 → [x]
Next → P14-VIEW-001
```

---

# DONE — P14-VIEW-001

## Base / Projected Views

* [x] Implement base drawing view — names a source and an orientation; a projected view names a parent instead and inherits both
* [x] Implement Front / Top / Right / Left / Bottom / Rear — each checked against a hand-written basis; no two share one, and opposite views have opposed normals
* [x] Implement projected views — the basis is derived from the parent, so ADR-018 removes the possibility of contradicting it rather than testing for it
* [x] Implement isometric — a preset, not a free camera; all three axes foreshorten to sqrt(2/3), +Z projects straight up
* [x] Implement per-view scale — inherits the sheet's unless overridden; 1:1, 1:2 and 2:1 give exactly the drawn sizes
* [x] Implement view placement on sheet — anchors the projected bounding-box **centre**, stated rather than left ambiguous
* [x] Preserve model-to-view identity — `ObjectReference`, walked up the parent chain; never a pointer, handle, index or name
* [x] Validate orthographic projection mathematically — expected coordinates computed in the test, max error < 1e-9 mm
* [x] Validate projected-view alignment — **exact** equality, because the derivation gives the same number rather than keeping two in step
* [x] Validate part and assembly geometry — an asymmetric box in all six views; components drawn at their **solved** transforms, rotation and repeated instances included
* [x] Determinism PASS — bit-identical twice, and 1763/1763 across all three presets
* [x] Adversarial review PASS — 18 questions, 1 production defect (a loop of projected views recursed until the stack ran out), fixed
* [x] Regression PASS — 1763/1763 on three presets from clean, 0 warnings, 17/17 stages exit 0
* [x] Evidence recorded — `docs/verification/P14-VIEW-001/`

### Gate

```text
projection mathematically correct
+ orientation conventions correct
+ scale correct
+ view alignment correct
+ model references stable
+ deterministic output
```

Met: 43 new tests and 1504 assertions; 1763/1763 on `debug`, `release` and
`debug-shared`, each from clean; 683/683 five times over; 0 compiler warnings;
17/17 stages exit 0; the no-op rebuild compiled 0 in every preset.

**The milestone could not start until an architecture gap was closed.** No P14
ADR had chosen first-angle or third-angle projection, and that rule decides
which side of its parent every projected view lands on. ADR-018 makes it
per-sheet intent defaulting to first angle — ISO 128 requires the convention
to be shown on the drawing, so it is intent by definition, and the project is
already ISO everywhere else.

The adversarial review found a crash rather than a cosmetic issue: only
*self*-parenting was prevented, so A-from-B plus B-from-A was constructible,
and the derivations walked the parent chain by direct recursion with no depth
guard. Evaluating either would have exhausted the stack instead of
diagnosing. Refused at the edit now, and both recursions are depth-limited as
a second line for documents loaded from a file.

Worth carrying forward: alignment is **exact** rather than approximate because
a Top view's x *is* its parent's x — the derivation produces the same number
instead of keeping two numbers in step. That is the difference between
alignment that is derived and alignment that is maintained, and only the first
cannot drift.

```text
P14-VIEW-001 → [x]
Next → P14-VIEW-002
```

---

# P14-VIEW-002

## Section / Detail / Auxiliary Views

* [x] Implement full section view
* [x] Implement half section
* [x] Implement offset section foundation
* [x] Implement detail/cropped view
* [x] Implement auxiliary view
* [x] Implement cutting-plane representation
* [x] Implement section hatch generation
* [x] Validate section geometry independently
* [x] Validate stable source references
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-VIEW-002/](docs/verification/P14-VIEW-002/README.md)

### Gate

```text
section geometry correct
+ cutting planes correct
+ detail extraction correct
+ hatch behavior correct
+ stable references preserved
```

Met: 55 new tests; 1818/1818 on `debug`, `release` and `debug-shared`, each
from clean; 785/785 five times over in release and debug; 0 compiler warnings;
every stage exit 0; the no-op rebuild compiled 0 and linked 0 in every preset;
the tree IDs taken before the first build and after the last test run are
identical.

**The section geometry is validated against closed forms, never against
itself.** Every fixture is built from boxes, so every cut face is a rectangle
and every expected area is a product of two lengths computed in the test: a
100 × 60 × 40 block sections to 4000 mm², the same block with a 20 × 20 duct
to 3600 mm² with the duct as an inner loop, a half section to exactly half,
and an offset section with one jog back to 4000 mm² in a single four-corner
loop. Auxiliary views are validated on the one claim that defines them: an
inclined face 40√2 = 56.5685 mm long draws at 56.5685 mm, where the front view
foreshortens it to 40.

**Offset sections needed no development step.** Parallel legs seen along their
shared normal already project into one plane; what remains is the jog, where
both legs' cut faces end and — seen along that normal — their two edges land
on each other. An edge with material on both sides is not a boundary, so the
pair cancels. That is exactly the rule that develops a stepped section, and
why ISO 128 draws no line at the jog.

The adversarial review found four defects, the serious one silent rather than
loud: a **curved cut edge was being dropped**. A round hole through a
sectioned wall would have vanished from the outline — the wall's own rectangle
still closes into a loop, the hole is not there to be a void, and the section
comes back *looking correct* with hatch drawn straight across material that is
not there. It would have passed every test in this milestone, because every
fixture is made of boxes. Curved cut faces are now refused by name until
P14-HLR-001 adds the curve handling.

Worth carrying forward: `sheetDisplacement` generalises `placementStep` to an
arbitrary direction, and a test asserts the two agree on all four orthogonal
directions in both conventions. The four-entry table and the general rule are
therefore one rule, not two that have to be kept in step.

```text
P14-VIEW-002 → [x]
Next → P14-HLR-001
```

---

# P14-HLR-001

## Visible / Hidden Line Generation

* [x] Implement visible-edge extraction
* [x] Implement hidden-edge extraction
* [x] Implement silhouette edges
* [x] Implement tangent-edge policy
* [x] Implement per-view hidden-line toggle
* [x] Handle overlapping/projected edges
* [x] Validate against independent geometry cases
* [x] Validate assemblies with occlusion — closed by [P14-ASM-001](docs/verification/P14-ASM-001/README.md): partial, complete and repeated-instance inter-component occlusion, and a suppressed occurrence excluded
* [x] Deterministic edge classification PASS
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-HLR-001/](docs/verification/P14-HLR-001/README.md)

### Gate

```text
visible edges correct
+ hidden edges correct
+ silhouettes correct
+ occlusion correct
+ deterministic classification
```

Met, except the one item above: 29 new tests; 1847/1847 on `debug`, `release`
and `debug-shared`, each from clean; 877/877 five times over in release and
debug; 0 compiler warnings; every stage exit 0; the no-op rebuild compiled 0
and linked 0 in every preset; the tree IDs before the first build and after
the last test run are identical.

**The one open item, stated plainly.** Occlusion *between* components cannot
be reached: a view draws one component, which is `P14-VIEW-001`'s recorded
boundary and `P14-ASM-001`'s to lift. So "front component partially hides rear
component", "one fully hides another" and "repeated instances at different
depths" have no expressible fixture, and building one would be starting
`P14-ASM-001`. What *is* validated is occlusion at the **solved** transform:
the same part placed upright and turned half a turn gives 4 hidden versus 4
visible pocket lines from the same view, and a component with no solved
transform — including a suppressed one — is refused rather than drawn at the
origin. The checkbox stays `[ ]` because the item as written is not fully
done, not because the work that is authorized is unfinished.

**Hidden-line removal is exact, not polygonal** (ADR-019). Polygonal HLR is
faster and more forgiving, but its output is polylines whose shape depends on
a mesh-deflection parameter: a drawn circle would not be a circle, and exact
comparison between presets would be impossible by construction. Speed is the
wrong thing to buy with that.

**The unclassified projection is gone rather than duplicated.** `P14-VIEW-001`
recorded "no visibility classification yet — that is the whole of
`P14-HLR-001`", so lifting that boundary meant changing the one pipeline
rather than adding a second beside it. A box front view now draws **4** lines
instead of 12: its far face lands exactly on its near face, and its four depth
edges point at the viewer and draw nothing.

Two results worth carrying forward. **Coincident lines are not an edge case —
they are every box in the system**, so ISO 128 line precedence had to be
implemented, not deferred; without it every outline would be drawn twice, once
solid and once dashed. And **a view must be centred on what it could draw,
not on what survives its settings**, or turning hidden lines off silently
shifts the drawing on the sheet.

Adversarial review found one defect: a curve lying wholly inside a detail
region came back as a fan of two-point fragments, because an unclipped piece
had its endpoint recomputed as `a + 1.0 * (b - a)` — the same number in
arithmetic, not always the same double, so consecutive pieces failed to join.

```text
P14-HLR-001 → implementation complete and qualified
              1 validation item open, blocked on P14-ASM-001
Next → a scope decision: authorize P14-ASM-001 to close the open item,
       or accept it and move to P14-DIM-001
```

---

# P14-DIM-001

## Dimensions

* [x] Implement linear dimensions
* [x] Implement horizontal / vertical dimensions
* [x] Implement aligned dimensions
* [x] Implement angular dimensions
* [x] Implement radius dimensions
* [x] Implement diameter dimensions
* [x] Implement ordinate dimension foundation
* [x] Implement dimension precision / formatting
* [x] Implement model-driven dimension values
* [x] Validate dimensions against 3D geometry
* [x] Preserve dimension references across regeneration
* [x] Missing reference fails explicitly
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-DIM-001/](docs/verification/P14-DIM-001/README.md)

### Gate

```text
dimension math correct
+ units correct
+ formatting correct
+ model values correct
+ references stable
+ no silent rebinding
```

Met: 34 new tests; 1881/1881 on `debug`, `release` and `debug-shared`, each
from clean; 1880/1880 five times over in release and in debug; 0 compiler
warnings; the no-op rebuild compiled 0 and linked 0 in every preset; the tree
IDs before the first build, after the last test run and before the commit are
identical.

**The reference audit came first, and it decided everything.** ADR-012 permits
a dimension to name a datum or principal plane, a datum or principal axis, or
a NAMED face — nothing else. That supports every type on the list, and three
things it does not support were recorded rather than faked: no dimension to a
vertex, so no corner-to-corner diagonal; no dimension to an edge, which
ADR-012 already answers by naming the two faces that meet there; and **no
radius on a HOLE feature**, because hole features name their bottom and floors
and not their bore. Closing that last one needs a new `FaceRole`, which is a
change to P12's qualified face naming and not this milestone's to make.

Two contracts were decided deliberately rather than left accidental, both
because the brief asked for them to be stated. **Aligned is the distance AS
DRAWN**, not the model distance — they differ exactly when the separation has
a component along the view's normal, where a distance exists and cannot be
drawn. **An angular dimension reads what a protractor reads**: 180° minus the
angle between outward normals, so two faces of a slab read 0°, two coplanar
faces read 180°, and a wedge reads its own included angle.

The number is never stored. `dimensionToJson` writes no value, and a test
greps the saved file to prove it; `measure()` resolves against the model every
time, so there is nowhere for a stale number to live.

Adversarial review found one defect: the formatter rounded through a final
floating multiply and wrote `0.14` for 0.145. The cause is worth keeping — a
value typed in millimetres is held in METRES, and the double nearest 0.145
times 100 is 14.499999999999998, so even a value that *is* 0.145 rounds down
if the last step is a multiply. Rounding now happens in exact integers at six
decimals finer than shown.

**Two defects were also found in the qualification tooling itself, and both
are fixed.** `qualify.cmd` ended `exit /b 0` whatever happened, so a failed
stage reached nobody — which is how the Debug determinism repeat failed
without failing anything. It now counts failed stages and exits with the
count, and `verify-harness.cmd` is the regression: it points the harness at a
preset that does not exist and requires a non-zero exit. Separately, the
harness file had grown 98 → 196 → 784 lines over three milestones because the
script copying it round-tripped it through text mode; it is rebuilt from the
one clean copy, and the version that actually ran this qualification is kept
beside it.

The Debug determinism repeat **failed on its first run** — `cli.assembly.batch`
could not replace a file, "Permission denied", because the build tree lives
under OneDrive. It did not reproduce: the test passed 5/5 twice afterwards and
the gate passed 1880/1880 on one controlled rerun. The failure is kept in the
evidence rather than erased, and the structural remediation — moving build
output off the synchronised directory — is the next infrastructure decision,
deliberately not taken here because the build directory lives in
`CMakePresets.json`, inside the frozen qualified tree.

```text
P14-DIM-001 → [x]
Next → P14-ANNO-001
Carried open → P14-HLR-001 assembly-to-assembly occlusion, blocked on P14-ASM-001
```

---

# P14-ANNO-001

## Drawing Annotations

* [x] Implement text notes
* [x] Implement leaders
* [x] Implement centerlines
* [x] Implement centermarks
* [x] Implement hole callouts
* [x] Implement surface-finish symbol foundation
* [x] Implement datum symbol foundation
* [x] Implement annotation placement
* [x] Validate scale-independent text sizing
* [x] Validate deterministic annotation layout
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-ANNO-001/](docs/verification/P14-ANNO-001/README.md)

Met on the SECOND qualification: 34 new tests; 1915/1915 on `debug`, `release`
and `debug-shared`, each from clean; 1914/1914 five times over in release and
debug; 0 compiler warnings; the no-op rebuild compiled 0 and linked 0 in every
preset; the tree IDs before the first build, after the last test run and
before the commit are identical.

**The first qualification failed, and the harness said so.** The exit-code fix
P14-DIM-001 made earned its keep on its first real use: `QUALIFICATION FAILED:
2 stage(s) failed`, non-zero exit. Under the old harness this would have
exited 0 with a broken shared build sitting in a log. Both runs are in the
evidence; the failed one is not erased.

One of the two failures was **mine**: `SceneItems::isEmpty()` was marked for
export AND defined inline in the header, which makes it `dllimport` in a
shared build, and a dllimport function may not have a definition. Debug and
Release compiled it happily; only `debug-shared` failed. That is the second
time that preset has caught a real defect in P14 — the first was
`DrawingScale::label()` in P14-SHEET-001, the mirror-image mistake.

**The invariant the milestone turns on**: where an annotation points follows
the model through the view's projection and moves with the scale; how big it
is drawn is paper millimetres and meets no scale. The centre-mark test asserts
both halves at once — arms 5.0 mm at 1:1, 1:2, 2:1, 1:10 and 5:1 while the
crossing point moves 30, 15, 60, 3 and 150 mm — because a mark that never
moved would also never change size.

A hole callout stores **which hole**, never a number: `Ø10 THRU` becomes
`Ø12 DEEP 8` when the hole is edited, with the same annotation and no drawing
edit. It points at the hole FEATURE rather than its bore, because P12 names a
hole's bottom and floors and not its wall — the gap P14-DIM-001 found — and
because `features::holeCallout()` already resolves diameter, tolerance and
thread from the document.

Adversarial review found one further defect: a centreline's span was measured
from the view's policy-filtered edges, so turning hidden lines off shortened
it — a display setting silently changing an annotation's geometry, and
invisibly. It now measures from the view's pre-suppression bounds.

Only the part of ADR-016's drawing scene that annotations need was built —
lines, text, styles, anchors, and the self-validation ADR-016 requires. The
sheet frame, layers, hatch and line weights are P14-EXPORT-001's.

```text
P14-ANNO-001 → [x]
Next → P14-TOL-001
Carried open → P14-HLR-001 assembly-to-assembly occlusion, blocked on P14-ASM-001
Open decision → move build output off OneDrive; the filesystem fault recurred
```

---

# P14-TOL-001

## Tolerances / Fits / GD&T Foundation

* [x] Implement dimensional ± tolerance
* [x] Implement limit dimensions
* [x] Implement fit notation foundation
* [x] Implement datum feature symbols
* [x] Implement feature-control-frame data model
* [x] Implement core geometric characteristic symbols
* [x] Associate tolerances with stable model references
* [x] Validate semantic persistence
* [x] Validate export representation
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-TOL-001/](docs/verification/P14-TOL-001/README.md)

### Gate

```text
tolerance intent preserved
+ datum references stable
+ GD&T data model coherent
+ persistence correct
+ export representation correct
```

Met: 43 new tests; 1958/1958 on `debug`, `release` and `debug-shared`, each
from clean; 1957/1957 five times over in release and debug; 0 compiler
warnings; the no-op rebuild compiled 0 and linked 0 in every preset; the tree
IDs before the first build, after the last test run and before the commit are
identical.

**A tolerance stores the engineering meaning, never the text.** `20 ±0.05` and
the pair `20.05 / 19.95` are ONE interval with a display mode, so they cannot
come to describe different parts — asserted bit for bit. A fit stores its
designation and reads ISO 286 when the numbers are wanted, so the same `H7`
gives 15 µm at 8 mm and 21 µm at 20 mm.

**Where the tables do not reach, it fails by name.** Shaft fundamental
deviations are not transcribed in this build, so `g6` keeps its notation and
its tolerance *width* — IT is shared by holes and shafts — and its limits fail
with "the designation is kept and no numbers are invented". A hole position
outside D–H does the same. Nothing is estimated.

**A datum reference is a letter** ([ADR-020](docs/architecture/decisions/ADR-020-a-datum-reference-is-a-letter.md)),
held in an ordered vector, bound to nothing — which is what ISO 1101 defines,
and which means a frame cannot silently rebind to the wrong feature because it
binds to no feature. `A|B|C` and `B|A|C` compare unequal, serialize differently
and draw differently. The cost — a frame can cite a datum nobody defined — is
answered by `undefinedDatums()`, a live report rather than a refusal, so the
order an engineer works in stays theirs. The datum-letter rule itself was
*de-duplicated*: `validateDatumLetter` is now the one rule the datum feature
symbol and every frame share, and both give the same message word for word.

**Adversarial review found five defects, and three were the same failure in
different places**: a number rounded on its way onto the drawing. A frame cell
sized by UTF-8 *bytes* drew a one-character symbol three characters wide; the
frame rounded a 0.005 mm zone to `0.01`; and a dimension wrote its tolerance at
the *nominal's* precision, giving `±0` at zero decimals and an H7 hole's limits
as 100.04/100.00 — neither the standard's width nor its position. All three now
go through one `decimalsWithoutRounding`, so they cannot round differently. A
fourth accepted a deviation pair of no width, which made `DimensionTolerance{}`
read `±0`. The fifth was in the fix for the third: it used an *absolute* slack,
which answers "no decimals" for exactly the small values it exists to protect,
and was caught by a test written for the cap rather than by reading the code.

The shared infrastructure this touched did not move: `[annotation]` is still
1307 assertions in 34 cases and `[dimension]` still 931 in 34, identical to the
milestones that qualified them.

```text
P14-TOL-001 → [x]
Next → P14-ASM-001
Carried open → P14-HLR-001 assembly-to-assembly occlusion, blocked on P14-ASM-001
Open decision → move build output off OneDrive; the fault did not recur here,
                which does not close it
```

---

# P14-ASM-001

## Assembly Drawing Views

* [x] Generate views from solved assembly state
* [x] Respect active configuration
* [x] Respect component suppression
* [x] Preserve occurrence identity
* [x] Validate multiple instances of one part
* [x] Validate assembly hidden-line behavior
* [x] Validate sectioned assemblies
* [x] Validate regeneration after assembly changes
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-ASM-001/](docs/verification/P14-ASM-001/README.md)

Met: 41 new tests; 1999/1999 on `debug`, `release` and `debug-shared`, each
from clean; 1998/1998 five times over in release and debug; 0 compiler
warnings; the no-op rebuild compiled 0 and linked 0 in every preset; the tree
IDs before the first build, after the last test run and before the commit are
identical.

**An assembly view is ONE hidden-line problem, not one per component**
([ADR-021](docs/architecture/decisions/ADR-021-an-assembly-view-is-one-hidden-line-problem.md)).
Run one problem each, every occurrence would be classified against itself
alone, and a component standing wholly behind another would come back fully
visible — a drawing that looks entirely plausible and is wrong. Fusing them
instead answers the occlusion question and destroys the identity one, so it
was rejected too. Every occurrence goes into one `HLRBRep_Algo` and each one's
lines are extracted separately, which is what keeps both answers.

**Occlusion is asserted by comparison, because a box hides its own back
face.** "This body has hidden edges" says nothing about occlusion BETWEEN
bodies — every box has them alone. Three of the first assertions written here
claimed otherwise and passed for the wrong reason. Every occlusion test now
compares against the same geometry drawn by itself: a rear plate that shows
lines from x = 0 alone shows none left of x = 50 once a front plate is there,
and the kernel splits its edges exactly at 50.

**P14-HLR-001's carried-open item is closed**, with the four cases it asked
for: partial and complete inter-component occlusion, repeated instances at
different depths, and a suppressed occurrence excluded. It is ticked because
those fixtures exist and pass, not because assembly projection exists.

**Which occurrences are drawn is never stored.** It is
`assembly::activeComponents()` at the moment of drawing, so configuration and
suppression have one implementation and a drawing cannot disagree with the
solver about what is in the assembly — a component added after a view was made
appears in it with no edit to the view.

**A missing solved transform fails the whole view.** Not the identity
transform, not a partial drawing: one missing a component looks exactly like a
complete drawing of a smaller machine.

Adversarial review found two defects. A detail view's crop rebuilds each line
field by field and **dropped the occurrence**, so a detail of an assembly drew
perfectly and said every line belonged to nobody — the kind of loss that
surfaces two milestones later. And two bodies CAN draw the same line, so the
canonical edge order gained `source` as its last tiebreaker; without it, two
plates meeting face to face sorted against each other by whatever the kernel
returned first.

One regression run reported `cli.new.unicode-path` failing. It was run under
the wrong console code page: `qualify.cmd` sets 65001 for exactly that test,
and the same binary passes it there. Recorded in the evidence rather than
dropped, because "one test failed and I decided it did not count" is the shape
of a real failure being waved through.

```text
P14-ASM-001 → [x]
P14-HLR-001 "Validate assemblies with occlusion" → [x]
Next → P14-BOM-001
Carried open → nothing
Open decision → move build output off OneDrive; the fault did not recur here,
                which does not close it
```

---

# P14-BOM-001

## BOM / Balloons

* [x] Implement assembly BOM model
* [x] Implement unique item numbering
* [x] Group identical part definitions correctly
* [x] Preserve separate component occurrences
* [x] Implement quantity calculation
* [x] Implement BOM table
* [x] Implement item balloons
* [x] Link balloons to BOM rows
* [x] Respect configurations / suppression
* [x] Validate deterministic numbering
* [x] Validate save/load
* [x] Adversarial review PASS
* [x] Regression PASS
* [x] Evidence recorded — [docs/verification/P14-BOM-001/](docs/verification/P14-BOM-001/README.md)

### Gate

```text
BOM contents correct
+ quantities correct
+ occurrence grouping correct
+ balloon mapping correct
+ configuration behavior correct
+ deterministic numbering
```

Met: 31 new tests; 2030/2030 on `debug`, `release` and `debug-shared`, each
from clean; 2029/2029 five times over in release, and in debug on one
controlled rerun after an environmental failure; 0 compiler warnings; the
no-op rebuild compiled 0 and linked 0 in every preset; the tree IDs before the
first build, after the last test run and before the commit are identical.

**A QUANTITY IS NOT INTENT**
([ADR-022](docs/architecture/decisions/ADR-022-a-bom-is-derived-and-has-no-identity.md)).
Nobody decides that there are four brackets; there are four because four
occurrences of the bracket are active. So nothing here is stored: no row, no
quantity, no item number. ADR-017 had deferred to this milestone whether a BOM
or a balloon needs identity of its own, and the answer is neither — a BOM table
and a balloon are annotation KINDS, and the bill is computed on every call.
The regression that proves it saves a document at quantity 4, changes the
model to 3, reloads and requires 3; the file is asserted to contain no
`"quantity"`, no `"item"` and no `"rows"`.

**A balloon points at the OCCURRENCE, not at a table row.** The chain runs
balloon → occurrence → part definition → row → item number, which is the order
the information actually flows in, and every link is an identity that already
existed. It cannot point at the right bolt and show the wrong figure, because
the figure is a function of the bolt. Suppress or delete the occurrence and the
balloon is UNRESOLVED — tested in the case where a rebinding balloon would have
succeeded, because another occurrence of the same part is still active and the
row is still item 1.

**Grouping is by part-definition identity and by nothing else.** Two separately
defined 20 mm cubes are two rows, and the test asserts their bounding boxes are
equal so it is about identity rather than about the parts differing. A part
renamed to resemble another does not merge, and the printed name follows the
rename because it was never copied into the BOM.

**Numbering is compact, not retained**, and that is stated rather than left
accidental: rows sort by ascending part ObjectId and are numbered 1..N, so
removing the last occurrence of item 2 makes the old item 3 into item 2. With
nothing persisted there is nowhere a retained number could live. Building the
same assembly with the components created in the opposite order gives the same
rows, quantities and numbers.

A deliberate split worth carrying forward: **a BOM survives a failed solve.** A
parts list says what is in the assembly, which does not depend on where the
solver put anything — so the table is not stale, it is the current active set.
A balloon is the opposite and fails, because it needs a transform to put its
leader on an instance. Both halves are asserted together.

Adversarial review found one production defect: a BOM table that pointed at
something was refused with *"a note is placed on the sheet"*, whatever kind had
been made. Until now a note was the only kind that points at nothing; the
message now names the kind.

**The repeat gate FAILED and the harness said so** — `repeat debug` exit 8,
`1 stage(s) failed`. Neither failure was in this work: `cli.assembly.batch` hit
`Permission denied` on `built.bcad` after passing three times in the same run,
and a `compile_fail` test was killed by a 64-second timeout under eight-way
parallel load. One controlled rerun passed 2029/2029. Both logs are kept.

```text
P14-BOM-001 → [x]
Next → P14-STREF-001
Carried open → nothing
Open decision → move build output off OneDrive; NOW DUE, third occurrence
```

---

# P14-STREF-001

## Stable Drawing References

* [x] Stable drawing view → model reference
* [x] Stable dimension → model geometry reference
* [x] Stable annotation → model reference
* [x] Stable balloon → assembly occurrence reference
* [x] Preserve references across model regeneration
* [x] Preserve references across configuration switching
* [x] Preserve references across save/load
* [x] Missing geometry becomes unresolved
* [ ] Prevent silent rebinding — **NOT MET.** A chamfer face is named
      `{role = Chamfer, edge = N}` where N is the POSITION of an edge
      reference in `ChamferDefinition::edges`. Reordering that list leaves
      the reference resolving — to different material. Measured in
      `Reference_AChamferFaceIsNamedByItsPositionInTheChamfersEdgeList`;
      every other reference path holds, including against identical
      survivors deliberately left in place
* [x] Validate target recovery
* [x] Deterministic resolution PASS
* [x] Adversarial review PASS
* [x] Regression PASS — 2047/2047 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-STREF-001/](docs/verification/P14-STREF-001/README.md)

### Gate

```text
drawing references stable
+ no index-based identity
+ no silent rebinding
+ unresolved/recovery correct
+ persistence PASS
+ determinism PASS
```

**NOT MET, on one gate.** Everything else passes: 32 new tests; 2047/2047 on
`debug`, `release` and `debug-shared`, each from clean; 2046/2046 five times
over in both repeat presets; 0 compiler warnings; the no-op rebuild compiled 0
and linked 0 in every preset; the tree IDs before the first build, after the
last test run and before the commit are identical.

**The audit's finding.** Every face in this codebase is named semantically —
an extrude's side face by the sketch entity that sweeps it — **except a
chamfer's**, which is named by the POSITION of its edge reference in
`ChamferDefinition::edges`. That list is ordinary stored intent a user may
reorder, and the entries are `EdgeSignature`s, which `ChamferFeature.hpp`
itself says "are not persistent topological names". So a drawing reference to
a chamfer face is one stable link followed by one positional link — the shape
this milestone exists to forbid.

Measured rather than inferred: a block with two chamfers, a reference to the
face of edge reference 2, then the edge list is **reordered**. The solid is
identical and the reference is untouched; it still resolves, and it now names
the face 60 mm away. Shortening the list is safe — the name of the last
position stops matching and the reference becomes Unresolved, which is right.
Only reordering is dangerous.

**No production code was changed.** That is the audit's conclusion in the shape
of a diff: the drawing layer's contract is already right everywhere it reaches.
What was added is `drawing/Resolution.hpp` — Resolved / Unresolved / Invalid,
read from the resolvers the drawing already uses so there is no second answer
to "where is this" — and 32 tests.

**Every no-rebind fixture is built so a rebinding implementation would
succeed**: a second block of the same size beside the one removed, a second
hole of the same diameter in the same face, a second occurrence of the same
part still active and still item 1. None is adopted. Recovery and rebinding are
asserted as one paired test so the two cannot be read as one behaviour.

The remaining work is precise and is in the evidence: give each chamfer edge
reference an id, name the face by the id, migrate the format, and re-qualify
the reference models that contain chamfers. That last step changes committed
artifacts three phases are qualified against, which is why it was not done
here.

```text
P14-STREF-001 → [ ]  (11 of 12 checks pass; "no silent rebinding" does not)
Next → P14-STREF-001, to close the chamfer gap — needs its own authorization
Carried open → nothing
Open decision → move build output off OneDrive; the fault did not recur here,
                which does not close it
```

---

# P14-REGEN-001

## Drawing Regeneration

* [x] Define dirty-propagation triggers — drawing objects now carry the
      `RegenerationHandler`s `ADR-014` mandated and nobody built
* [x] Rebuild views after model changes
* [x] Update dimensions after model changes
* [x] Update annotations/BOM where required
* [x] React to configuration changes
* [x] Regenerate only affected drawing state where feasible
* [x] Preserve canonical drawing intent — a handler is a pure read
* [x] Handle unresolved references explicitly
* [x] Validate failure atomicity
* [x] Validate deterministic regeneration
* [x] Adversarial review PASS
* [x] Regression PASS — 2070/2070 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-REGEN-001/](docs/verification/P14-REGEN-001/README.md)

### Gate

```text
model changes propagate correctly
+ drawing intent preserved
+ affected state refreshed
+ stale geometry impossible
+ failures atomic
+ determinism PASS
```

**MET.** 23 new tests; 2070/2070 on `debug`, `release` and `debug-shared`,
each from clean; 2069/2069 five times over in both repeat presets; 0 compiler
warnings; the no-op rebuild compiled 0 and linked 0 in every preset; the tree
IDs before the first build and after the last test run are identical.

**The milestone's subject was a gap between an ADR and the code.** `ADR-014`
decided that `Sheet`, `View`, `Dimension` and `Annotation` each get a
`RegenerationHandler`, and warned in as many words that "an object with no
handler is silently marked `UpToDate` and never validated". **They were never
implemented.** So a dimension whose face had stopped existing regenerated as a
success — the defect `P13-REGEN-001` fixed for mates, still open for drawings.
Measured in one process, on one document:

```text
bare regenerator          UpToDate, no error, nothing in report.failed
drawing handlers          Failed, NotFound, "Width ... cannot be measured"
```

**A second defect, found while testing the first.** `View::dependencies()`
pushed `ObjectId{0}` for an assembly view, which names no source: `localTarget()`
returns a reference's object without checking validity, and `Dimension` and
`Annotation` guard with `isValid()` while `View` did not. The graph's
missing-reference check has therefore failed **every assembly view since
P14-ASM-001**. Nothing caught it because the drawing fixtures required
`regenerateAll()` to RETURN a report — which it does even when objects in it
failed — and never asserted `succeeded()`. Fixed, with a regression that runs
with no drawing handlers registered at all.

**Stale drawing geometry is impossible by construction, and is tested as
such.** Nothing in the drawing layer stores derived state, so there is no
cache to invalidate: a view's scale can be halved with NO regeneration pass
and the next projection comes back at the new scale.

**What a handler may look at is forced, not chosen** (`ADR-023`). The assembly
solve is a final pass, so during the object phase `Regenerator::transforms()`
holds the PREVIOUS pass's map — and `draw()` moves a balloon's anchor by its
occurrence's solved transform. A handler calling the full resolvers would read
a stale transform, or report "the assembly did not solve" on every pass of a
healthy document. So a handler resolves what its object NAMES and never
computes what it DRAWS.

```text
P14-REGEN-001 → [x]
Next → P14-CMD-001. P14-STREF-001 is still open on its chamfer gap and comes
       first if authorized
Carried open → nothing
Open decision → move build output off OneDrive; the fault did not recur here,
                which does not close it
```

---

# P14-CMD-001

## Commands / Undo / Redo

* [x] Sheet commands
* [x] View create/delete/move commands
* [x] Dimension commands
* [x] Annotation commands
* [x] BOM / balloon commands — the annotation commands ARE these (ADR-022);
      no row, quantity or item number is command-owned, because none is stored
* [x] Undo restores exact drawing intent
* [x] Redo restores exact post-command state
* [x] Failed commands are atomic
* [x] Redo invalidation correct
* [x] Regeneration integrates correctly
* [x] Determinism PASS
* [x] Adversarial review PASS
* [x] Regression PASS — 2097/2097 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-CMD-001/](docs/verification/P14-CMD-001/README.md)

### Gate

```text
commands mutate canonical intent
+ undo/redo exact
+ no derived state in history
+ failures atomic
+ regeneration correct
+ determinism PASS
```

**MET.** 27 new tests; 2097/2097 on `debug`, `release` and `debug-shared`, each
from clean; 2096/2096 five times over in both repeat presets; 0 compiler
warnings; the no-op rebuild compiled 0 and linked 0 in every preset; the tree
IDs before the first build, after the last test run and at the commit are
identical.

**Nothing new was built for history.** There is one command system and it
already worked, so these are fifteen commands in it — no second history, no
transaction mechanism, and no persistent history (`CommandHistory` is session
state; the file has no place for one and a test asserts it).

**Why drawing commands exist at all is validation, and for views it is not
theoretical.** `removeView()` refuses while another view is projected from it;
core's generic `DeleteObjectCommand` calls `Document::removeObject()` directly
and walks straight past that. The test does not argue this — it runs the
generic command on a clone of the same document and asserts it SUCCEEDS and
leaves the child orphaned, beside the drawing command that refuses.

**Undo is compared as the document's own serialization**, which is precisely
canonical intent because ADR-011 keeps derived state out of the file. Two
states that draw the same but serialize differently fail. Two exclusions are
asserted separately instead: `last_allocated_id` (which must NOT rewind — a
redo is holding an ID and will put it back) and the per-document UUID.

**Nothing derived is ever undo payload**, and the two tests that prove it
change the MODEL between a command and its redo: a dimension created at 100 mm
and redone after the model moved to 137.5 must read 137.5, and a balloon that
was item 2 must draw "1" once the occurrences ahead of it are gone.

**The only change to existing production code is nine lines** — extracting
`checkRemoveView()` from `removeView()` so the command and the free function
share one policy rather than two copies that could drift.

```text
P14-CMD-001 → [x]
Next → P14-PERSIST-001. P14-STREF-001 is still open on its chamfer gap and
       comes first if authorized
Carried open → nothing
Open decision → move build output off OneDrive; the fault has not recurred in
                the last two milestones, which does not close it
```

---

# P14-PERSIST-001

## Drawing Persistence

* [x] Define canonical drawing schema — inventoried key by key from the
      serializers; no new format, no new top-level section, no version bump
* [x] Persist sheets
* [x] Persist views / scales / placements
* [x] Persist dimensions
* [x] Persist annotations
* [x] Persist tolerances / GD&T — an ISO 286 fit stores its DESIGNATION, and
      a frame's datums an ORDERED array of single capitals
* [x] Persist BOM / balloon intent — table LAYOUT only; a balloon stores the
      occurrence, never the number it draws
* [x] Persist stable references
* [x] Keep generated drawing geometry derived
* [x] Validate malformed-file rejection
* [x] Validate deterministic serialization
* [x] Validate full round trip
* [x] Adversarial review PASS
* [x] Regression PASS — 2115/2115 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-PERSIST-001/](docs/verification/P14-PERSIST-001/README.md)

### Gate

```text
drawing intent persists
+ derived geometry excluded
+ references stable
+ malformed rejected
+ round trip exact
+ determinism PASS
```

**MET, and no production code was changed to meet it.** That is the finding
rather than a shortfall: drawing persistence was already correct, because each
P14 milestone shipped its serializer, its deserializer and its own persistence
tests as it introduced its object kind. This milestone is the audit no
per-type milestone could do — **one document holding every P14 object kind at
once** — and the contract held.

**Proven by tree ID, not by prose.** The qualified `include` and `src` trees
are byte-identical to the ones `P14-CMD-001` qualified (`494a2654…`,
`ecb00650…`); only `tests` differs. The same hash, independently computed,
before and after.

18 new tests; 2115/2115 on all three presets from clean; 2114/2114 five times
over in both repeat presets; 0 warnings; no-op rebuild compiled 0 and linked 0.

**Three decisions recorded rather than assumed.**

1. **No schema version bump.** The loader refuses any version but the current
   one — there is no forward compatibility by design — so a bump would have
   broken all 32 committed models and needed a migration for nothing. Drawing
   objects are new object KINDS in an extensible array; a pre-P14 file loads
   because it contains none of them. Proven per file.
2. **A view cycle in a file LOADS, and is then diagnosed.** It must: objects
   are read one at a time, so a view whose parent appears later in the file
   would be refused for a legal forward reference. The diagnosis comes from
   the DEPENDENCY GRAPH (`dependency cycle: Top`), which only reaches drawing
   objects because `P14-REGEN-001` put them in it.
3. **Unresolved is not malformed, through the file.** A balloon whose
   occurrence this configuration suppresses saves, loads, stays unresolved,
   and recovers — naming the same occurrence throughout, with an identical
   sibling present for a rebinding implementation to take.

Two defects were found in my own tests and fixed: the derived-state search
flagged `row_height` and `quantity_width`, which are table LAYOUT and
therefore intent, and two malformed anchors depended on the pretty-printer's
indentation. Neither reached production code.

```text
P14-PERSIST-001 → [x]
Next → P14-CLI-001. P14-STREF-001 is still open on its chamfer gap and comes
       first if authorized
Carried open → nothing
Open decision → move build output off OneDrive; the fault has not recurred in
                the last three milestones, which does not close it
```

---

# P14-CLI-001

## Headless Drawing Workflows

* [x] CLI create/load/save drawing — every edit is load-apply-save through
      P13-CLI-001's spine, so a one-line command is the batch of one
* [x] CLI add/remove sheets — `sheet-add`, `sheet-set`, `sheet-remove`
* [x] CLI create/edit views — `view-add` (object, assembly, projected),
      `view-set`, `view-move`, `view-remove`
* [x] CLI add/edit dimensions — `dimension-add`, `dimension-set`,
      `dimension-remove`
* [x] CLI annotations — `annotation-add`, `annotation-set`,
      `annotation-remove`
* [x] CLI regenerate — and this is where `drawing::registerHandlers` finally
      reaches production, which `P14-REGEN-001` recorded as missing
* [x] CLI BOM generation — the annotation verbs ARE the BOM and balloon verbs
      (ADR-022); `bettercad-cli drawing` reports the rows, and computes none
* [x] CLI export — the BOUNDARY, stated and tested: there is no drawing
      exporter and no command pretends there is (P14-EXPORT-001 owns it)
* [x] Structured diagnostics
* [x] Correct process exit codes — 0 / 1 / 2, on 21 in-process cases and 6
      process tests
* [x] Validate CLI/core equivalence
* [x] End-to-end scripted workflow PASS
* [x] Adversarial review PASS
* [x] Regression PASS — 2142/2142 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-CLI-001/](docs/verification/P14-CLI-001/README.md)

### Gate

```text
CLI reaches the qualified core
+ no drawing semantics reimplemented
+ exit codes correct
+ batch failures propagate
+ CLI/core equivalence PASS
+ end-to-end workflow PASS
+ determinism PASS
```

**MET, and no library code was changed to meet it.** Every API the command
line needed already existed: `include` and `src` are **byte-identical to the
trees `P14-PERSIST-001` and `P14-CMD-001` qualified**. Three milestones now
share those hashes. That is the strongest available statement that the CLI is
an adapter — it could not have reimplemented a drawing semantic even by
accident, because it added nowhere to put one.

13 edit verbs and one report, added to `P13-CLI-001`'s existing spine rather
than to a second mechanism. Each verb is one `P14-CMD-001` command object, so
the CLI inherits that milestone's validation and its all-or-nothing execution
instead of restating either.

27 new ctest entries — 17 in-process and **10 driving the built executable**,
which is what answers "can the workflow pass only because state survived in
one process": the batch writes a file and a separate invocation reads it.

**This is where `P14-REGEN-001` reaches production.** That milestone built the
drawing regeneration handlers and recorded that nothing registered them;
`regenerate`, `solve`, `status` and `drawing` now all do, so a broken drawing
is reported instead of passed over.

**The target grammar has no positional form at all** — `face:3`, `index:3`,
`edge:7`, `nearest:…` and `screen:…` are unreadable targets rather than
fragile ones. A drawing target names no component, because a dimension
measures the part.

**No BOM or balloon verbs, deliberately** (ADR-022): both are annotation
kinds, and there is no verb for a row, a quantity or an item number because
none is stored.

One adversarial finding corrected a TEST rather than the code: a balloon aimed
at a part definition is accepted and then diagnosed at regeneration as
**Invalid**, because `checkAnnotation()` validates that the named object
exists and leaves what it IS to the resolver. Same diagnose-rather-than-reject
pattern as a view cycle in a file (`P14-PERSIST-001`).

```text
P14-CLI-001 → [x]
Next → P14-EXPORT-001. P14-STREF-001 is still open on its chamfer gap and
       comes first if authorized
Carried open → nothing
Open decision → move build output off OneDrive; the fault has not recurred in
                the last four milestones, which does not close it
```

---

# P14-EXPORT-001

## PDF / SVG / DXF Export

* [x] Implement vector drawing scene representation — `SceneArc` (an exact
      arc, not a sampled polyline), a line width on every primitive,
      `DrawingScene`, `validate()`, `sheetScene()`, and `drawDimension()`:
      what a dimension DRAWS, which did not exist at all
* [x] Implement PDF export — uncompressed PDF 1.4, five objects, no dependency
* [x] Implement SVG export — SVG 1.1; the only writer that flips y, once
* [x] Implement DXF drawing export — R12 ASCII; `$INSUNITS` 4 and
      `$DWGCODEPAGE ANSI_1252` both DECLARED rather than left to be guessed
* [x] Preserve sheet size / scale — the page checked in all three formats; a
      1:2 view drawn at half size with its pen and lettering unchanged
* [x] Preserve line types / weights — style to layer/linetype/dash per format,
      weights in paper millimetres, never meeting a view's scale
* [x] Preserve dimensions / text / symbols — and the text ENCODING per format,
      which is where the adversarial review found this milestone's defect
* [x] Preserve hidden-line representation — dashed in, dashed out; turned off,
      gone, and every visible line identical
* [x] Validate PDF dimensions independently — the page box read back from the
      file against 841.8898 x 595.2756 pt computed in the test
* [x] Validate SVG geometry structurally — parsed as elements and attributes
* [x] Validate DXF entities/read-back — parsed as group codes, every entity
* [x] Validate deterministic geometric output — byte-identical, five times,
      in all three formats, and under a comma-decimal locale
* [x] Adversarial review PASS — **one credible defect found and fixed**; see
      below
* [x] Regression PASS — 2168/2168 on `debug`, `release` and `debug-shared`
* [x] Evidence recorded — [docs/verification/P14-EXPORT-001/](docs/verification/P14-EXPORT-001/README.md)

### Gate

```text
PDF PASS
+ SVG PASS
+ DXF PASS
+ sheet geometry correct
+ scale correct
+ drawing entities complete
+ independent read-back PASS
```

**MET.** ADR-016's export boundary is complete, and the signatures are what
hold it: `Result<std::string> svgDocument(const DrawingScene&)`, and the same
for DXF and PDF. A writer takes a scene and nothing else — no `Document`, no
`Body`, no regenerator, no resolver. It is not that the writers are careful
not to reach the model; **there is nothing in their signatures to reach it
through.** No new dependency: an uncompressed PDF 1.4 is 250 lines, DXF R12 is
a list of group codes, SVG is XML, and writing them here keeps byte
determinism ours rather than a library's.

26 new ctest entries — 20 export cases, 3 CLI export cases and 5 driving the
built executable. Every check reads the generated file with a parser written
in the test; no writer is asked what it thinks it wrote.

**THE ADVERSARIAL REVIEW FOUND A REAL DEFECT, AND THIS WAS QUALIFIED TWICE.**
PDF and DXF are single-byte formats and both were copying the scene's UTF-8
bytes through. `Ø` is `C3 98`; escaped byte-wise and read under
`/WinAnsiEncoding` it renders as two glyphs, so a diameter callout said
`Ã˜20`. The DXF named no code page at all, so the same file read differently
on two machines. `Ø` and `±` are on nearly every dimensioned drawing BetterCAD
can produce. Nothing caught it because **no test had put a non-ASCII character
through a writer** — every text assertion used `"TEST"`, a part name or a
number.

`src/io/TextEncoding.hpp` transcodes instead of copying. Three regression
tests were run against the pre-fix writers first: all three failed there, and
45 assertions pass now. The first qualification had already reached
2165/2165 in debug and built release clean; it was **discarded**, because a
writer changed after the freeze and the trees it qualified
(`src b6cadeed…`, `tests 7545d1d3…`) are not the trees committed
(`src 5bd82f3d…`, `tests 3d83b52f…`).

**A GD&T symbol still reaches SVG only.** No single-byte encoding and none of
PDF's fourteen standard fonts has a glyph for position, cylindricity,
straightness, flatness or runout, so they are written as `?` in PDF and DXF —
visibly missing, because nobody reads `?` as a tolerance. An ASCII substitute
like `PERP` would have been an exporter inventing GD&T semantics. Closing it
means embedding a font, which is a subsystem of its own and not authorized
here.

```text
P14-EXPORT-001 → [x]
Next → P14-REFMOD-001. P14-STREF-001 is still open on its chamfer gap and
       comes first if authorized
Carried open → GD&T symbols in PDF and DXF need an embedded font; SVG carries
               them correctly today
Open decision → move build output off OneDrive; the fault has not recurred in
                the last five milestones, which does not close it
```

---

# P14-REFMOD-001

## Production Drawing Reference Models

* [ ] Define production drawing suite
* [ ] Add simple machined-part drawing
* [ ] Add multi-view dimensioned part
* [ ] Add section/detail drawing
* [ ] Add hole / pattern drawing
* [ ] Add toleranced / GD&T drawing
* [ ] Add assembly drawing
* [ ] Add BOM / balloon drawing
* [ ] Add configuration-dependent drawing
* [ ] Validate model-change regeneration
* [ ] Validate save/load
* [ ] Validate CLI workflows
* [ ] Validate PDF / SVG / DXF output
* [ ] Independently validate dimensions / scale / geometry
* [ ] Adversarial review PASS
* [ ] Three-preset regression PASS
* [ ] Evidence recorded

---

# P14-QUAL-001

## Full P14 Qualification

* [ ] Freeze final P14 tree
* [ ] Audit all P14 milestone evidence
* [ ] Verify all TODO items complete
* [ ] Verify all P14 ADR contracts
* [ ] Clean Debug qualification
* [ ] Clean Release qualification
* [ ] Clean Debug-shared qualification
* [ ] Repeated determinism qualification
* [ ] Validate production drawing reference suite
* [ ] Validate dimensions / annotations
* [ ] Validate stable drawing references
* [ ] Validate drawing regeneration
* [ ] Validate persistence
* [ ] Validate undo / redo
* [ ] Validate BOM / balloons
* [ ] Validate CLI workflows
* [ ] Validate PDF / SVG / DXF exports
* [ ] Final adversarial review
* [ ] Confirm 0 unexpected warnings
* [ ] Confirm qualified tree == committed tree
* [ ] Evidence in `docs/verification/P14-QUAL-001/`
* [ ] Mark P14 qualified

### Final Gate

```text
all P14 milestones PASS
+ drawing architecture PASS
+ projection/view generation PASS
+ hidden-line generation PASS
+ dimensions PASS
+ annotations PASS
+ tolerance/GD&T foundation PASS
+ assembly drawings PASS
+ BOM/balloons PASS
+ stable drawing references PASS
+ regeneration PASS
+ persistence PASS
+ undo/redo PASS
+ CLI PASS
+ PDF/SVG/DXF PASS
+ production reference models PASS
+ Debug PASS
+ Release PASS
+ Debug-shared PASS
+ determinism PASS
+ adversarial review PASS
+ 0 unexpected warnings
+ qualified tree == committed tree
```

---

# P14 Core Invariants

* 3D model remains authoritative.
* Drawing views are derived from model geometry.
* Drawing annotations/dimensions preserve engineering intent.
* Derived 2D geometry is not authoritative model state.
* Model references must use stable identity, never topology index alone.
* Missing geometry becomes unresolved; never silently rebind.
* Assembly drawings use solved assembly state.
* Suppressed components do not appear in active drawing/BOM output.
* Dimensions must be unit-safe.
* Drawing regeneration must never publish stale geometry as current.
* Export must preserve physical sheet scale and geometry.

---

# Inherited Constraints

These are properties of the qualified P11–P13 system, not P14 work. P14 must
respect them, and `P14-ARCH-001` has to decide what each means for a drawing.

* Assemblies operate inside one `Document`; cross-document dependencies do not execute.
* One document-global configuration system; a component cannot select a different configuration of its part.
* Solved component transforms are derived state, recomputed on open — a drawing of an assembly costs a solve.
* An over-constrained assembly reports its redundant mates and publishes no positions; a drawing of one has nothing to project.
* A broken assembly publishes no transforms at all, not a partial set.
* Missing intended geometry fails explicitly; never silently rebind.
* Mate targets use only ADR-004-qualified reference types.
* STEP read-back is verification infrastructure, not general STEP import.

Full list with evidence: [docs/verification/P13-QUAL-001/README.md](docs/verification/P13-QUAL-001/README.md).

---

# Architecture Decisions

```text
ADR-001 — Reference-model datum placement
ADR-002 — Assembly/document model
ADR-003 — Internal/external reference contract
ADR-004 — Mate-reference semantics
ADR-005 — Placement intent / derived transforms
ADR-006 — Module layering
ADR-007 — One configuration system
ADR-008 — Assembly solve as regeneration final pass
ADR-009 — The CLI edit is a document transaction
ADR-010 — Drawings live in the document
ADR-011 — Drawing intent is canonical, projection is derived
ADR-012 — Drawing references name semantic geometry only
ADR-013 — View orientation and drawing scale
ADR-014 — Drawing geometry is built on demand
ADR-015 — Drawing module and layer (supersedes ADR-006's table)
ADR-016 — The drawing scene is the export boundary
ADR-017 — Drawing identity model
ADR-018 — Projection convention is sheet intent
```

`P14-ARCH-001` allocated ADR-010 to ADR-017 and `P14-VIEW-001` added
ADR-018. The next milestone that needs one continues from ADR-019.

---

# Carried Forward

Recorded gaps with no owning milestone. Fold into a P14 milestone only where
P14 actually needs them; otherwise they stay here.

* `Document::modifyObject` bypasses the document-level reference check (from `P13-COMP-001`).
* No CLI resolver for external parts (from `P13-REF-001`).
* No mate rebinding/repair command (from `P13-STREF-001`).
* `info` does not show where a component sits (from `P13-XFORM-001`).
* No compile-fail case pins `MateTarget`'s exclusion of `FaceSignature` (from `P13-QUAL-001`).

---

# Deferred CAD Work

Not currently authorized:

* Fillet setback controls
* Selectable fillet corner transitions
* Loft end conditions
* Full semantic topology
* General STEP import
* DXF / IGES / OBJ interoperability

`P14-EXPORT-001` authorizes DXF **drawing export** only. DXF import, and the
rest of the interoperability list, stay deferred.

---

# Future Phases

```text
P15  Materials / Engineering Data
P16  Meshing
P17  Structural FEA
P18  Thermal Analysis
P19  CFD Integration
P20  Design Optimization
P21  Semantic Topology
P22  Versioning / Collaboration
P23  Python / Automation
P24  AI Engineering Agent
P25  Manufacturing / CAM
P26  Performance / GPU / Scale
P27  Production Hardening
P28  BetterCAD 1.0
```

Do not start without explicit authorization.

---

# Workflow

```text
UNDERSTAND
→ ARCHITECT
→ BLAST RADIUS
→ IMPLEMENT
→ TARGETED TESTS
→ INDEPENDENT VALIDATION
→ FAILURE PATHS
→ PERSISTENCE
→ DETERMINISM
→ ADVERSARIAL REVIEW
→ FULL REGRESSION
→ EVIDENCE
→ [x]
→ COMMIT
→ PUSH
```

If a gate fails:

```text
STOP
→ reproduce
→ regression test
→ root cause
→ fix
→ revalidate
```

Never mark work complete because it merely compiles.

---

# Project Authority

```text
TODO.md
→ current authorized work

ROADMAP.md
→ long-term direction, and completed phases with their evidence

ARCHITECTURE.md
→ system architecture / invariants

CLAUDE.md
→ engineering process / Definition of Done

docs/architecture/decisions/
→ durable architecture decisions

docs/verification/<milestone>/
→ milestone evidence

docs/engineering/
→ reusable engineering templates
```
