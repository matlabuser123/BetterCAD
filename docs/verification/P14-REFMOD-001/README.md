# P14-REFMOD-001 — Production Drawing Reference Models

```text
STATUS:    PENDING QUALIFICATION
MILESTONE: P14-REFMOD-001
DATE:      2026-09-25
BASELINE:  772d965 (P14-EXPORT-001)
```

## TASK

Eight production-style drawings, built through nothing but the public API,
that prove the qualified P14 drawing system works end to end: real sheets,
real views, real dimensions against named faces, real annotations — then
regeneration, persistence, the CLI and all three export formats over the same
models.

This milestone adds **no drafting capability**. Every feature it uses was
qualified by an earlier P14 milestone. What it adds is evidence that they work
together on models that look like drawings rather than like test scenes.

## SCOPE

Authorized by `TODO.md`, `P14-REFMOD-001`. Not started here: `P14-QUAL-001` or
anything later. `P14-STREF-001`'s chamfer gap is untouched — no reference model
in this suite uses a chamfer, deliberately, so none of them depends on the one
face name that is still positional.

Two production defects were found by the models and fixed generally, with
regression tests shown failing against the pre-fix code. Both are recorded
under ADVERSARIAL REVIEW.

---

## REFERENCE SUITE DEFINITION

Eight models, each owning a capability no other one owns. The rule the part
and assembly suites already keep applies here too: **a builder carries no
expected answer.** What each model should measure is derived independently in
the tests and in this file, from the dimensions the model is defined by.

```text
RM-DWG-01  DrawnStepPlate       the whole pipeline, every number obvious
RM-DWG-02  DrawnAngleBracket    disagreeing views, an angle, a 1:2 view, a
                                second sheet, and the auxiliary view no
                                standard direction gives
RM-DWG-03  DrawnPocketBlock     a section and a detail of internal geometry
RM-DWG-04  DrawnHolePlate       a hole pattern: four identical bores, each
                                named and dimensioned separately
RM-DWG-05  DrawnToleranceBlock  deviations, limits, an ISO 286 fit, datums
                                and a feature-control frame
RM-DWG-06  DrawnClampSet        an assembly: repeated parts, a rotated part,
                                and occlusion between components
RM-DWG-07  DrawnBoltedStack     a bill of materials and the balloons on it
RM-DWG-08  DrawnGuardedFrame    two configurations, and what changes between
```

Document IDs are fixed, `d4a70000-0000-4000-8000-0000000000NN`, beside the
part suite's `5eed0000-...` and the assembly suite's `a55e0000-...`, so a
saved model is reproducible byte for byte.

Location follows the two existing suites exactly:

```text
examples/reference_models/Drawing*.cpp     the builders, in the library both
                                           the example program and the tests
                                           link
tests/reference/Drawing*Tests.cpp          the per-model and suite-wide tests
tests/CMakeLists.txt                       the CLI process tests
```

### Every model is asymmetric, and that is not decoration

`RM-DWG-04` began with its pattern at `(20, 20)` on a 120 × 80 plate and the
clearance hole at the plate's centre. That is symmetric about **both** axes, so
a mirrored or reflected top view would have drawn every circle exactly where
the test expected to find one, and the suite would have passed on a wrong
picture. The rows are now at `y = 15` and `y = 55` about a centre of 40, and
the clearance hole is at `(75, 62)` — off both axes. The adversarial review
found this; it is recorded here because it is the kind of thing a reference
model is for.

---

## COVERAGE MATRIX

Owned, not merely reached. `tests/reference/DrawingModelsTests.cpp` asserts
this table — `DrawingReference_TheSuiteCoversEveryQualifiedDrawingCapability`
walks every model, collects what it actually contains, and fails naming any
capability with no owner. A table in a document can go stale; an assertion
cannot.

```text
Capability                 01  02  03  04  05  06  07  08
Sheets                      x   x   x   x   x   x   x   x
  A3 landscape              x   x   x   x   x   x   x   x
  A4 portrait                   x
  more than one sheet           x
Base view                   x   x   x   x   x   x   x   x
Projected view              x   x       x   x   x
Section view                        x
Detail view                         x
Auxiliary view                  x
Assembly view                                   x   x   x
Hidden-line removal         x   x   x   x   x   x   x   x
  occlusion BY ANOTHER                          x   x   x
  component
Dimensions
  linear                    x   x   x           x
  horizontal                            x
  vertical                              x
  aligned                       x
  angular                       x
  radius                                x
  diameter                              x       x
  ordinate (signed)                     x
Annotations
  note                      x   x   x   x       x
  leader                                        x
  centreline                            x
  centremark                            x
  hole callout              x           x
  surface finish                                x*
  datum                                         x*
  feature-control frame                         x*
  balloon                                           x   x
  BOM table                                         x   x
Tolerances
  deviation pair                                x*
  limits                                        x*
  ISO 286 fit                                   x*
GD&T datum order A|B                            x*
Patterns, copied face names             x
Repeated part definitions                           x   x   x
Component occurrence identity                       x   x   x
Configurations                                              x
Suppression                                         x       x
Stable references           x   x   x   x   x   x   x   x
  identical survivors left            x               x   x
  in place
Regeneration (driven)       x   x   x   x   x   x   x   x
Persistence (round trip)    x   x   x   x   x   x   x   x
CLI (separate process)      x   x   x   x   x   x   x   x
PDF / SVG / DXF             x   x   x   x   x   x   x   x
```

`x*` marks RM-DWG-05, whose column is the tolerance one; the matrix above
lists it in position 5.

**Not covered by any reference model, and why.** A hole's POSITION cannot be
dimensioned. A cylindrical face may be the target of a radius or a diameter
and of nothing else (`P14-DIM-001`), and a bore's axis has no name of its own
under ADR-012 — so the most common dimension on a machining drawing has no
spelling in this build. RM-DWG-04 therefore carries ordinates between the
plate's own datum edges, and the pattern's geometry is validated from the
**drawn sheet** instead, against positions computed from the pitch. This is a
capability gap, not a defect, and it is not this milestone's to close.

---

## RM-DWG-01 — SIMPLE MACHINED PART

A 100 × 60 × 20 plate, a 40 × 20 × 10 step on one corner, a Ø12 hole through
the plate beside it. Asymmetric in both directions, so a mirrored or
transposed projection cannot look correct.

```text
PlateProfile -> Plate -> StepProfile -> Step -> Bore
Sheet1 (A3, 1:1) -> Front -> {Top, Right}
                 -> Length, Thickness, BoreCallout, GeneralNote
```

| Quantity | Expected (computed here) | Source of the expectation |
| --- | --- | --- |
| Volume | `120000 + 8000 - 720π` = 125738.053289415349 mm³ | `L·W·T + l·w·h - πr²T` |
| Length | 100.000 mm | the two side faces the short sketch lines sweep |
| Thickness | 20.000 mm | the extrude's own two caps |
| Callout | `Ø12 THRU` | the hole feature's own diameter and extent |

## RM-DWG-02 — MULTI-VIEW DIMENSIONED PART

Views that disagree, a 45° corner, a view drawn at 1:2 on a 1:1 sheet, and a
**second sheet** in a second format and the other orientation carrying the
auxiliary view.

```text
BodyProfile -> Body
Sheet1 (A3 landscape, 1:1) -> Front (1:2) -> {Top, Side}
                           -> Overall, Rise, Corner, ScaleNote
Sheet2 (A4 portrait, 1:2)  -> Sheet2Front -> SlantAuxiliary
                           -> AcrossSlant
```

| Quantity | Expected | Source |
| --- | --- | --- |
| Volume | `(120·60 - 40·40/2)·50` = 320000 mm³ | rectangle less the corner triangle, times the width |
| Overall | 120.000 mm | left face to right shoulder |
| Rise | 60.000 mm | bottom face to top face |
| Corner | 45.000° | a 40 cutback over a 40 rise |
| AcrossSlant (aligned) | 50.000 mm | the extrude depth, in the auxiliary view's plane |
| Front view paper span | 120 / 2 = 60.000 mm | the model length at the view's 1:2 |
| Auxiliary view, across | 50 / 2 = 25.000 mm | the depth at the sheet's 1:2 |
| Auxiliary view, along | `180/√2 / 2` = 63.639610306789272 mm | the profile's extent along `(-1,1,0)/√2`, halved |
| Sheet 2 page | 210.000 × 297.000 mm | A4 portrait |

The auxiliary view exists because the 45° face is foreshortened in **every**
orthographic view of this part: its true shape is visible only along its own
normal, `(1, 1, 0)/√2`.

## RM-DWG-03 — SECTION / DETAIL

A prismatic pocket with a step in its floor, off centre in both directions, so
a mirrored section would not look right. A section through it and a 2:1 detail
of the corner where the two depths meet.

| Quantity | Expected | Source |
| --- | --- | --- |
| Volume | 127200 mm³ | block less pocket less step |
| Across | 80.000 mm | measured **on the section** |
| Tall | 30.000 mm | the block's caps |
| Cut area | `2400 - 480 - 160` = 1760 mm² | the 80 × 30 cross-section less the pocket's 40 × 12 and the step's 20 × 8 |
| Detail scale | 2:1 | the view's own override |

The cut area is the assertion a picture cannot fake. A section that missed the
step reads 1920; one that missed both reads 2400; **one that hatched across the
voids reads 2400 as well.** Every wrong section has a wrong number there.

## RM-DWG-04 — HOLE / PATTERN

A 120 × 80 × 10 plate, a Ø10 bore at `(20, 15)`, a 2 × 2 rectangular pattern
at 80 × 40 pitch, and a Ø16 clearance hole at `(75, 62)` that is not part of
it.

**The bore is an extrude cut of a circle, not a hole feature, and that is the
model's point.** A cut's side face is named by the sketch entity that swept it,
and a pattern's copy of that face is named by the same entity plus the copy it
belongs to — so each of four identical bores has a semantic name of its own.

| Quantity | Expected | Source |
| --- | --- | --- |
| Volume | `96000 - 1640π` = 90847.788048112736 mm³ | plate less four Ø10 bores less one Ø16 hole |
| Bore centres, sheet mm | (110, 75) (190, 75) (110, 115) (190, 115) | view placed at (150, 100), bbox centre (60, 40), 1:1 |
| Clearance centre | (165, 122) | same mapping, model (75, 62) |
| Length (horizontal) | 120.000 mm | plate's end faces, along the top view's X |
| Width (vertical) | 80.000 mm | plate's side faces, along the top view's Y |
| AcrossFromLeft (ordinate) | **+**120.000 mm | signed, datum = the left-hand end |
| DownFromTop (ordinate) | **−**80.000 mm | signed, datum = the top edge |
| BoreDiameter | 10.000 mm | the source bore's cylinder |
| FarBoreDiameter | 10.000 mm | pattern copy 3's cylinder |
| BoreRadius | 5.000 mm | the same cylinder, exactly half the diameter |
| Callout | `Ø16 THRU` | the clearance hole feature |

The negative ordinate is what distinguishes an ordinate from a horizontal or a
vertical dimension of the same pair of faces: those report a magnitude and
could not produce it.

**Four identical circles, four different names.** Each centre mark names one
bore, and the four land in four different places — checked pairwise at more
than 1 mm apart. A reference that had collapsed onto the source would draw four
marks on top of each other and measure the same diameter four times, which a
diameter check alone cannot tell apart.

## RM-DWG-05 — TOLERANCE / GD&T

An 80 × 50 × 25 block with a Ø20 bore. Bottom face → datum A, left-hand end →
datum B, the bore held to `⌖ Ø0.05 A B`.

| Quantity | Expected | Source |
| --- | --- | --- |
| Volume | `100000 - 2500π` = 92146.018366025517 mm³ | block less the bore |
| Length | 80.000 ±0.10 → [79.90, 80.10] | a stored symmetric deviation pair |
| Width | 50 +0.15 −0.05 → limits 50.15 / 49.95 | a stored asymmetric pair, written as limits |
| BoreSize | Ø20 **H7** → [20.000, 20.021] | **ISO 286-1: EI = 0, ES = +0.021 at 20 mm** |
| Frame | Position, cylindrical zone, 0.05 mm, datums A then B | ISO 1101 |
| Finish | Ra 1.6 µm, material removal required | ISO 1302 |

The H7 limits are transcribed **from the standard** into the test, not read out
of the codebase's own table: a test that read the limits from the same data the
implementation reads them from would agree with a transcription error rather
than catch one. The file stores `H7` and two zeros; the deviations are resolved
when the dimension is measured.

Datum order is asserted as an order, not a set: `datums[0] == 'A'` and
`datums[1] == 'B'`. `A|B` is a different requirement from `B|A`, and a frame
that kept its datums as a set would be a different instruction.

**Valid → broken → repaired**, on the datum structure: removing the datum B
symbol makes the frame report `B` as undefined — not silently pick another
letter, and not quietly drop the requirement — and restoring it satisfies the
frame again.

## RM-DWG-06 — ASSEMBLY

Body 90 × 50 × 15 grounded; Jaw 20 × 30 × 35 placed **twice** from one part;
Pin Ø14 × 40; Key 60 × 10 × 8 turned 35° about Z. Five occurrences of four part
definitions, all fully constrained.

| Quantity | Expected | Source |
| --- | --- | --- |
| Components / active | 5 / 5 | four part definitions, the jaw placed twice |
| Solve | FULLY_CONSTRAINED, 0 DOF, 12 equations | five mates locating each jaw, three grounded |
| Jaw origins | (12, 10, 15) and (58, 10, 15) | the values passed to `locate()` |
| Key rotation | 35.000° about Z | read back from the solved transform's local +X |
| Front view extent | 90.000 × 63.000 mm | body length × (key top 55 + 8) |
| Top view extent | 90.000 × 50.000 mm | the body's footprint |
| BodyLength | 90.000 mm | the body part's two end faces |

**Occlusion between components.** The jaws and the pin stand behind the body's
front face, so part of each is hidden by *a different solid*. The evidence from
outside is that the sheet carries ISO 128 hidden detail; turning hidden lines
off removes exactly that and leaves **every visible line identical**, which is
asserted as a vector comparison rather than a count.

**The key is the one that catches a mirror.** A 35° rotation reads differently
from either side, so a transposed or reflected top view cannot look correct —
which a cylinder spun about its own axis could not tell anybody, being
unchanged by it.

**Valid → broken → repaired**, through the assembly: a mate that contradicts
the jaw's position leaves the assembly unsolvable, P13 publishes **no**
transforms rather than a partial set, and the sheet refuses to draw rather than
showing its last good picture. Removing the mate restores the original sheet
item for item.

## RM-DWG-07 — BOM / BALLOONS

Bracket ×1, Bolt ×4, Spacer ×2 — seven occurrences of three part definitions.

| Quantity | Expected | Source |
| --- | --- | --- |
| Rows | 3 | three part definitions |
| Quantities | 1, 4, 2 | the occurrences placed |
| Item numbers | 1, 2, 3 | ascending part ObjectId = creation order |
| `totalOccurrences()` | 7 | 1 + 4 + 2 |
| Balloon texts | 1, 2, 2, 3 | the item number of each occurrence's row |

**The four bolts are the point.** Two of them carry balloons: the two must show
the same number — they label the same part — and land in different places,
because they label different instances. A balloon that had collapsed onto the
part would draw both arrows at one bolt and read entirely correctly.

**Qty 4 → suppress one → Qty 3 → restore → Qty 4.** The balloon on the
suppressed bolt becomes **unresolved**; it does not move to one of the three
identical bolts that are left. The other bolt's balloon is untouched and still
reads 2.

Every row keeps the occurrences it grouped rather than counting them: the
union across rows is seven distinct ComponentIds, with no duplicates.

## RM-DWG-08 — CONFIGURATION

Frame ×1, Guard ×1, Bolt ×4. `Guarded` overrides nothing; `Open` suppresses the
guard and two bolts.

| | Guarded | Open |
| --- | --- | --- |
| Active occurrences | 6 | 3 |
| BOM rows | 3 | 2 |
| Quantities | 1, 1, 4 | 1, 2 |
| The bolt's item number | **3** | **2** |
| GuardBalloon | resolved, reads `2` | **unresolved** |
| FrameBalloon | resolved, reads `1` | resolved, reads `1` |

**The bolt's item number changes, and that is correct.** Item numbers are
computed over the parts that are actually there, contiguously (ADR-022): with
no guard drawn, the bolt is item 2 rather than item 3 with a gap where the
guard used to be. A stored number could not do that.

**A → B → A, exactly.** Returning to `Guarded` gives the identical sheet —
lines, arcs and text, item for item. No history-dependent drift.

**Unresolved is loud, not quiet.** In `Open` the guard is not drawn, so its
balloon has nothing to label. BetterCAD's answer (ADR-014) is that regeneration
**reports that annotation by name** and the sheet **refuses to draw**:

```text
GuardBalloon (object:33): GuardBalloon (annotation:33) points at
component:17, which the active configuration does not place
```

and `bettercad-cli drawing ... --configuration Open` exits 1. The balloon still
names the guard — its intent is intact and only its resolution changed, which
is asserted directly on the stored target. A rebinding implementation would
have had five identical-looking alternatives here.

---

## MODEL-CHANGE REGENERATION

Every model has one controlled mutation, driven through the production path:
set a parameter, regenerate, and require the **drawing** to have changed.
Nothing tells the regenerator which objects to rebuild.

```text
RM-DWG-01  plate_length      100 -> 120
RM-DWG-02  body_length       120 -> 140
RM-DWG-03  pocket_depth       12 -> 16
RM-DWG-04  pitch_x            80 -> 70
RM-DWG-05  block_length       80 -> 90
RM-DWG-06  body_length        90 -> 100
RM-DWG-07  bracket_w          90 -> 100
RM-DWG-08  frame_w           100 -> 110
```

"Changed" is checked on the **sheet**, not on the model: a model that rebuilt
while its drawing kept the old picture is exactly the failure this milestone
exists to catch.

### The A → B → A round trip

Each mutation is then reversed, and the restored sheet must be the original
one. Measured across the suite:

```text
RM-DWG-01   2.78e-14 mm     RM-DWG-05   1.39e-14 mm
RM-DWG-02   0                RM-DWG-06   1.39e-14 mm
RM-DWG-03   0                RM-DWG-07   0
RM-DWG-04   0                RM-DWG-08   0
```

The bound asserted is **1e-12 mm**. The restored sheet is RECOMPUTED, not
remembered — the kernel runs again from the restored parameter — so a
coordinate of order 100 mm may differ in its last bit or two. The worst
observed is 2.78e-14 mm, one to two ulp; 1e-12 is CLAUDE.md's figure for
well-conditioned double-precision work and is still some thirty times tighter
than the spread. A drawing that had actually failed to regenerate would be out
by micrometres at least, and a **structural** difference — a line gained, a
style changed, an arc's radius moved — returns infinity from the comparison and
fails whatever the bound.

Targeted mutations beyond the sweep:

* `pitch_x` 80 → 70 moves the two bores in the right-hand column 10 mm and
  leaves the left-hand column exactly where it was. A pattern that rebuilt
  about a different origin would move all four.
* `clearance_d` 16 → 20 changes what the callout says, with no edit to the
  annotation: it stores no number.
* `block_length` 80 → 90 moves the whole ±0.10 interval with the nominal,
  because nothing in the file is a copy of the size.
* `body_length` 90 → 100 moves both the dimension and the assembly view's
  extent, through a component's part.

---

## STABLE-REFERENCE STRESS

Ambiguous geometry, deliberately, in three shapes:

```text
four identical bores        RM-DWG-04   named apart by pattern copy, and the
                                        four centre marks land in four places
four identical bolts        RM-DWG-07   suppress one -> ITS balloon unresolves;
                                        the others are untouched
identical repeated parts    RM-DWG-08   suppress the guard -> its balloon
  and a whole component                 unresolves and still names the guard
```

In every case the target is removed and then restored, and recovery is exact.
No reference is rewritten, and none adopts a survivor.

`DrawingReference_EveryDrawingObjectResolvesAgainstItsModel` walks every view,
every dimension and every annotation of all eight models in their committed
state and requires each to resolve — a committed reference model carrying an
unresolved reference would be a fixture nobody could trust to fail for the
right reason.

---

## SAVE / LOAD

`create → save → DESTROY → load → regenerate → compare`, for every model. Not a
file size and not an object count:

* the canonical JSON, which under ADR-011 **is** the intent — objects, IDs,
  references, face roles, formats, placements, datum order, tolerance
  designations;
* then the regenerated sheet, compared item for item.

`RM-DWG-08` is saved in `Open` and loaded: it comes back in `Open`, with the
same reduced BOM and the same balloon still unresolved and still naming the
guard. A round trip that healed it would have rebound it.

### Fresh process

`bettercad_example_reference_models --drawings --out <dir>` builds, regenerates,
validates and exports all eight in **one process**; every `bettercad-cli` test
below opens a file that process wrote, in **another**, with nothing shared but
the bytes on disk. An in-process test cannot prove that, because it never
leaves the process that built the model.

---

## CLI WORKFLOWS

Fifteen process tests on the real executables (`ctest -R cli.refmod`), all
through published commands:

```text
cli.refmod.build                       the example program: build, regenerate,
                                       save, export all eight (the fixture)
cli.refmod.validate                    references, cycles, sketches, features
cli.refmod.drawing.pattern             every dimension kind RM-DWG-04 carries,
                                       including the negative ordinate
cli.refmod.drawing.tolerances          a deviation pair, a limit, an ISO 286 fit
cli.refmod.drawing.sheets              two sheets, two formats, two
                                       orientations, the auxiliary view
cli.refmod.drawing.bom                 3 rows, 1/4/2, 7 occurrences
cli.refmod.drawing.configuration       --configuration Open: exit 1, the
                                       balloon named, the reduced BOM
cli.refmod.drawing.configuration.base  the committed state: 3 rows, ok
cli.refmod.solve                       FULLY_CONSTRAINED, 0 DOF, 5 transforms
cli.refmod.regenerate                  rebuilt from the file alone
cli.refmod.export.{svg,dxf,pdf}        written by a process that loaded it
cli.refmod.export.section              the section/detail model, whole
cli.refmod.export.second-sheet         --sheet Sheet2: a 210 x 297 page
```

`cli.refmod.build` is a **fixture**, not merely ordered first: ctest pulls a
required fixture's setup in even when the run is filtered, whereas a `DEPENDS`
on an excluded test is dropped. It rewrites its whole output directory, so the
set survives `--repeat`, and every reader is read-only.

`--drawings` was added to the example program for this: the full program also
builds twelve parts and eight assemblies with their STEP and STL exports, which
is 52 s of work this fixture has no use for. The drawing suite alone is 12.4 s
in Debug.

### CLI / core equivalence

`DrawingReference_TheCliAndTheCoreApiWriteTheSameDrawing` builds the **same**
drawing on the **same** part twice — once through the public core API and once
through `bettercad-cli` — and requires:

```text
identical canonical JSON        the whole of what either interface writes
identical IDs                   sheet, view, dimension, annotation, named
identical DrawingScene          what the two actually draw
byte-identical SVG, DXF, PDF    what a plotter would receive
identical measured value        20.00 mm, by both routes
identical bill of materials     3 rows, 1/4/2, 7 occurrences (assembly case)
identical balloon mapping       one item number, two arrow tips
```

The part is RM-DWG-01's, built by the reference builder and then stripped of
its drawing objects through the public API, so the geometry, parameters, sketch
entities and face names are the committed model's rather than a simplified
stand-in.

**The same, on the assembly**, so the workflow reaches the verbs a part
drawing has no use for:
`DrawingReference_TheCliBuildsAnAssemblyDrawingWithItsBomAndBalloons` takes
RM-DWG-07's assembly, strips its drawing, and rebuilds it through
`sheet-add`, `view-add --assembly`, `annotation-add --type bom_table` and two
`annotation-add --type balloon --target object:Bolt1` — then saves, reloads,
regenerates and exports. Compared with the core-built drawing it requires the
same canonical JSON, the same annotation IDs, the **same bill of materials**,
the same item number on both balloons, the same arrow tips, and byte-identical
SVG, DXF and PDF.

`--target object:Bolt1` names an OCCURRENCE. Two balloons on two instances of
one bolt must come out with the same number and different arrows; a CLI that
had resolved the target to the PART would write two annotations that looked
right and pointed at the same place.

The other direction: a model built through the core, saved, and read back by
the CLI — every number the core measures appears in the CLI's report, written
the way the dimension's own format says.

---

## PDF

Read back, never merely opened. `drawex::pdfMediaBox` and
`drawex::pdfPathPoints` parse the generated file; they share no code with the
writer.

| Check | Expected | Where |
| --- | --- | --- |
| Page box, A3 landscape | 1190.5512 × 841.8898 pt | computed in the test as `mm × 72 / 25.4` |
| Header | `%PDF-1.4` | first bytes |
| Half-scale span | the two ends of a 120 mm edge, 60.000 mm apart on paper | RM-DWG-02's 1:2 front view |
| Orientation | +Y up, no flip | the same points appear unflipped |

## SVG

Parsed as elements and attributes.

| Check | Expected | Where |
| --- | --- | --- |
| Page | `width="420.0000mm" height="297.0000mm"` and the matching `viewBox` | every model |
| Sheet 2 | `width="210.0000mm" height="297.0000mm"` | RM-DWG-02 |
| Circles | four of `r="5.0000"`, one of `r="8.0000"` | RM-DWG-04 |
| Circle centres | the four pattern positions, computed from the pitch | RM-DWG-04 |
| The flip | the two edge points appear at `height − y`, and **not** at `y` | RM-DWG-02 |

## DXF

Read back with the group-code reader `P14-EXPORT-001` wrote — the same reader,
not a second copy that could share a blind spot with it.

| Check | Expected | Where |
| --- | --- | --- |
| `$INSUNITS` | `4` (millimetres), declared | every model |
| Entity kinds | LINE, LWPOLYLINE, CIRCLE, TEXT | every model |
| Circles | four `CIRCLE` of radius 5, one of radius 8 | RM-DWG-04 |
| Circle centres (code 10/20) | the four pattern positions, +Y up | RM-DWG-04 |
| Half-scale span | the same two points as the PDF | RM-DWG-02 |

### Nothing is silently dropped

Every primitive in the scene becomes exactly one entity in the file, for every
model:

```text
DXF entities  ==  lines + arcs + texts
SVG elements  ==  lines + arcs + texts     (polyline + circle + path + text)
```

For RM-DWG-04: 66 scene items → 29 LINE + 23 LWPOLYLINE + 5 CIRCLE + 9 TEXT =
66, and 52 `<polyline>` + 5 `<circle>` + 9 `<text>` = 66.

A writer that quietly skipped a primitive it did not recognise would produce a
drawing that looked complete and was missing a hole, a dimension or a note —
and would pass every check that only asked whether the file had the right
*kinds* of thing in it.

---

## INDEPENDENT GEOMETRY VALIDATION

Expected values come from the arithmetic in this file and in the tests, from
the dimensions each model is defined by. No expectation is read back from the
code under test, and no fixture asks a production helper where it put
something.

```text
Quantity                       Expected              Tolerance   Basis
volume, RM-DWG-01              125738.053289415 mm3  1e-6 mm3    L*W*T + l*w*h - pi r^2 T
volume, RM-DWG-02              320000 mm3            1e-9 rel    (rect - triangle) * width
volume, RM-DWG-03              127200 mm3            1e-9 rel    block - pocket - step
volume, RM-DWG-04              90847.788048113 mm3   1e-6 mm3    96000 - 1640 pi
volume, RM-DWG-05              92146.018366026 mm3   1e-6 mm3    100000 - 2500 pi
section cut area, RM-DWG-03    1760 mm2              1e-9 rel    2400 - 480 - 160
linear dimensions              exact, per table      1e-9 mm     distances between exact planes
angular dimension              45.000 deg            1e-9 deg    40 cutback over 40 rise
radius / diameter              5.000 / 10.000 mm     1e-9 mm     the sketch's radius parameter
ordinate, signed               +120.000 / -80.000    1e-9 mm     datum edge to opposite edge
H7 at 20 mm                    [20.000, 20.021] mm   1e-9 mm     ISO 286-1, transcribed
paper span at 1:2              60.000 mm             1e-4 mm     120 mm model / 2
bore centres on the sheet      per table             1e-4 mm     view placement + model - bbox centre
file coordinates               per table             1e-6 mm     the writers print 4 decimals
```

### Why each tolerance

```text
exact equality      IDs, names, item numbers, quantities, BOM row order,
                    canonical JSON, byte comparisons of exported files. These
                    are not measurements and a tolerance on them would mean
                    nothing.
1e-12 mm            the A -> B -> A restoration. Well-conditioned double
                    precision; measured worst case 2.78e-14.
1e-9 mm / deg       distances between exact planes and exact cylinders,
                    resolved through a projection. Geometric accumulation,
                    CLAUDE.md's figure for it.
1e-6 mm3            volumes of order 1e5 mm3 -- a relative 1e-11, on exact
                    prisms less exact cylinders.
1e-4 mm (kPaperMm)  paper coordinates. This is the FORMAT's own resolution:
                    the writers print four decimals of a millimetre, so a
                    coordinate read back from a file cannot be closer than
                    this to the scene's.
1e-6 mm (kMm)       comparisons inside one file's parsed numbers, where no
                    projection has intervened.
```

No tolerance was loosened to make a test pass. The one that moved during this
milestone moved the other way: the A → B → A check began as exact equality,
which failed on two models, and was replaced by 1e-12 mm **after** measuring
the actual spread and confirming it was one to two ulp rather than a
regeneration fault.

---

## FAILURE / RECOVERY

Three valid → broken → repaired paths, each on a different subsystem:

```text
RM-DWG-05   remove the datum B symbol   -> the frame reports B undefined
            restore it                  -> the frame is satisfied again
RM-DWG-06   add a contradicting mate    -> the assembly does not solve, P13
                                           publishes no transforms, the sheet
                                           REFUSES to draw
            remove the mate             -> the original sheet, item for item
RM-DWG-07   suppress one of four        -> Qty 3, and THAT balloon unresolved
            identical bolts                while the others are untouched
            restore it                  -> Qty 4, resolved, reads 2 again
RM-DWG-08   activate `Open`             -> the guard's balloon unresolved,
                                           reported by name, sheet refuses
            activate `Guarded`          -> the identical sheet
```

No silent rebind anywhere. No stale geometry anywhere: a drawing that cannot
resolve publishes nothing rather than its last good picture.

---

## DETERMINISM

```text
built twice in one process       identical canonical JSON, identical sheet
regenerated twice over           identical sheet (idempotent)
scene built three times          identical scene, and identical SVG, DXF and
                                 PDF bytes from the first and the third
save -> load -> regenerate       identical sheet
A -> B -> A                      identical within 1e-12 mm (see above)
CLI vs core                      byte-identical SVG, DXF and PDF
```

Fixed document IDs mean a freshly built model saves byte for byte the same
file. No test depends on unordered iteration, wall-clock timing, an unfixed
seed, thread scheduling or locale.

**What is NOT asserted, said plainly.** Every comparison above is *within one
run of one build*. No test compares a generated file against a committed
artifact, so **cross-preset byte identity is not pinned by an assertion** — if
Release ordered scene items differently from Debug, both would be
self-consistent and both would pass. The three-preset regression establishes
that each preset passes its own determinism checks; it does not compare their
output to each other.

This follows the two existing suites: `Fingerprint.cpp` compares measured
summaries within a run for the part models, and the assembly models do the
same. Pinning cross-preset identity would mean committing eight `.bcad` files
and their exports, or a hash constant in a test — the first is generated data
the project deliberately does not commit, the second breaks on every
legitimate change and says nothing about why. The ordering itself is
deterministic by construction (`Scene.hpp`: sheets before views, views in
ascending ID order, edges in the order hidden-line removal canonicalised
them), and `P14-EXPORT-001` qualified the writers in all three presets.

---

## RUNTIME

Debug, this machine, per model, from the example program's own timings:

```text
Model        build    regeneration   scene + 3 exports
RM-DWG-01    1.6 ms        95 ms           69 ms
RM-DWG-02    0.6 ms        16 ms           57 ms
RM-DWG-03    1.1 ms        42 ms          378 ms
RM-DWG-04    1.1 ms       243 ms          250 ms
RM-DWG-05    0.7 ms        24 ms           59 ms
RM-DWG-06    1.2 ms        22 ms          132 ms
RM-DWG-07    0.8 ms        13 ms          151 ms
RM-DWG-08    0.6 ms        14 ms           85 ms
```

`--drawings` end to end: 12.4 s in Debug including process start and OCCT load.
The full example program, all three suites, is 52 s.

RM-DWG-04 is the most expensive to regenerate — a pattern of five cuts through
one plate — and RM-DWG-03 the most expensive to draw, because a section and a
2:1 detail are two extra hidden-line problems. Neither is optimised here:
nothing was traded for speed, and no model was made smaller to save time.

---

## ADVERSARIAL REVIEW

Sixteen questions, worked through against the code and the tests. **Three
credible production defects found and fixed; one fixture weakness and six
coverage gaps found and closed.** Every fix is general, and every regression test was shown failing
against the pre-fix code.

### DEFECT 0 — every hole on every drawing was exported as a polyline

`SheetScene.cpp` recovers a circle from a projected circular edge's three
points — start, midpoint and end — so a writer with a circle primitive can use
one. Its circumcircle determinant is

```text
d = 2 (ax(by - cy) + bx(cy - ay) + cx(ay - by))
```

which is identically zero when two of the three points coincide. **A CLOSED
circular edge has `start == end`** — which is what a hole is — so only two of
the three points were ever distinct, `d` was always zero, and the recovery
always returned `nullopt`.

Every full circle in the system was therefore exported as its **sampled
polyline**: over a thousand points where a DXF `CIRCLE`, an SVG `<circle>` or
four Béziers belong. The drawing looked right, which is why `P14-EXPORT-001`
did not catch it — its tests asserted that arcs existed, and arcs (whose three
points *are* distinct) worked.

**The fix.** Two cases, because a circular edge comes in two shapes. When the
start and end coincide, the remaining pair are half a turn apart — a
**diameter**, which defines the circle exactly. Otherwise the determinant path
runs as before. The polyline remains available as a fallback, so failing to
recover a circle costs fidelity and never correctness.

**Regression:** `DrawingReference_StepPlateDrawsItsHoleAsARealCircle`, and
`DrawingReference_HolePlateDrawsItsBoresAsCirclesInEveryFormat`, which requires
four `<circle>`/`CIRCLE` of radius 5 and one of radius 8 in the generated files
and checks their centres against positions computed from the pitch.

**This changes `P14-EXPORT-001`'s byte baselines** for any drawing containing a
hole: the same intent now writes fewer, better primitives. Determinism is a
property of a tree, so no qualified claim is invalidated — but `P14-QUAL-001`
must re-establish those baselines rather than cite the earlier logs.

### DEFECT 1 — `annotationText()` reported a balloon as a hole callout

`isModelDriven()` admits three kinds — a hole callout, a balloon and a BOM
table — because none of the three stores words of its own. `annotationText()`
handled only the first:

```cpp
if (!anchor->hole) {
    return makeError(..., "{} ({}) is a hole callout, and what it points at is not a hole");
}
```

So asking a **balloon** what it says produced

```text
BracketBalloon (annotation:31) is a hole callout, and what it points at is not a hole
```

— a false statement about the document, on a public API, in a diagnostic an
engineer cannot act on.

**Root cause.** `annotationText()` was written in `P14-ANNO-001`, when
"model-driven" meant exactly "hole callout". `P14-BOM-001` widened
`isModelDriven()` to three kinds and extended `draw()` and
`resolveAnnotationTarget()` with guards for the two new ones — but not
`annotationText()`.

**Why nothing caught it.** `P14-BOM-001`'s own tests read a balloon's number out
of `draw()`'s `SceneItems` (`balloonText()` → `drawn()`). **No test had ever
asked `annotationText()` about a balloon.** The same blind-spot shape as
`P14-EXPORT-001`'s UTF-8 defect, where no test had put a non-ASCII character
through a writer.

### DEFECT 2 — `annotationText()` on a BOM table was undefined behaviour

A BOM table points at nothing, so its `AnnotationTarget` is empty. Sent down
the same path, it reached

```cpp
const ComponentId asComponent = ComponentId::fromValue(target.object->value());
```

and dereferenced a disengaged `std::optional<ObjectId>`. The Debug build's
libstdc++ assertions caught it:

```text
optional:1228: Assertion 'this->_M_is_engaged()' failed.
```

In Release that is an unchecked read of whatever was there. `draw()` and
`resolveAnnotationTarget()` both guard `Note` and `BomTable` before reaching
`resolveTarget()`; `annotationText()` did not.

**The fix, one change for both.** `annotationText()` now dispatches on the
annotation's kind, exactly as `draw()` and `resolveAnnotationTarget()` already
do — a balloon answers with `itemNumberOf()`, the one path `draw()` uses, so
the text and the drawing cannot come to disagree; a table refuses **naming
itself**, because its words are its rows. `resolveTarget()` was additionally
hardened to return a diagnostic rather than dereference an empty target, which
is defence in depth on the same function.

**Regression tests** (`tests/drawing/BomTests.cpp`), all shown failing first:

```text
Balloon_AnnotationTextIsTheNumberTheBalloonDraws
Balloon_AnnotationTextFollowsTheAssemblyLikeTheDrawnNumberDoes
Balloon_AnnotationTextOfAnOccurrenceThatIsNotDrawnFailsAsItself
BomTable_AnnotationTextSaysATableHasNoOneRunOfText
Annotation_EveryModelDrivenKindIsAnsweredByAnnotationText
```

The last is the contract as one case: every kind `isModelDriven()` admits must
be answered — with its text, or with a refusal that names **that** kind.

### DEFECT 3 — the CLI drawing report merged its columns

`{:<28}` pads to 28 and stops, so a label of exactly 28 characters ran straight
into the next column:

```text
  ClearanceCallout (object:21)hole_callout          on Top (object:15)
```

An ordinary name on an ordinary model produced it. A report whose columns can
merge cannot be read by a person or split by a script.

**The fix.** A `column()` helper that pads to a width and **always** appends at
least one space, applied to every column of the report rather than to the one
that happened to collide. Regression:
`DrawingCli_TheReportKeepsItsColumnsApartWhateverAnObjectIsCalled` sweeps name
lengths across the boundary rather than guessing which one lands on it; it
fails on two assertions against the pre-fix padding and passes on the fix (both
runs recorded).

### FIXTURE WEAKNESS — a symmetric pattern would have survived a mirror

Covered under REFERENCE SUITE DEFINITION above. RM-DWG-04's pattern and
clearance hole were symmetric about both axes; a mirrored or reflected top view
would have drawn every circle where one was expected. Moved off both axes.

### COVERAGE GAPS — six qualified capabilities had no owner

The first draft of this suite left these unexercised, behind an aggregate PASS:

```text
auxiliary views          P14-VIEW-002 qualified them; no model had one
horizontal dimensions    the view's own X axis, never read
vertical dimensions      the view's own Y axis, never read
aligned dimensions       "the distance as drawn", never read
radius dimensions        only diameters were measured
ordinate dimensions      the one signed dimension type, never used
leader annotations       only plain notes pointed at nothing
a second sheet format    every sheet was A3 landscape
```

All are now owned, and
`DrawingReference_TheSuiteCoversEveryQualifiedDrawingCapability` asserts each
by name so the next gap fails a test rather than hiding.

### The remaining questions, and their answers

```text
symmetric geometry hides a mirror        fixed; see above
dimensions right, scale wrong            RM-DWG-02: the paper span is computed
                                         from the model length and the scale
                                         and then read back from all three files
PDF passes while SVG/DXF differ          the same two points checked in all
                                         three, with SVG's flip asserted to
                                         happen exactly once
repeated components collapse             RM-DWG-07: seven distinct occurrence
                                         IDs across three rows, no duplicates
BOM right, balloon mapping wrong         two balloons, one number, two places
configuration B leaves stale geometry    A -> B -> A is identical; B differs
save/load keeps a different reference    canonical JSON compared; the guard's
                                         balloon still names the guard
hatch leaks into holes                   RM-DWG-03's cut area is 1760 mm2
                                         against a closed form; hatching the
                                         voids reads 2400
identical holes rebind                   four centre marks, four places
hidden-line masked by coincidence        turning hidden lines off leaves every
                                         VISIBLE line identical, as a vector
fixture uses production code             every expectation restated by hand in
                                         the test or derived in this file
parser shares the writer's blind spot    the readers are the ones P14-EXPORT-001
                                         wrote, in the test TU, sharing no code
CLI relies on same-process state         two processes, bytes on disk only
Debug and Release order differently      the three-preset regression below
primitives silently dropped              entity count == scene item count, in
                                         both DXF and SVG, for every model
a capability has no owner                asserted by name; six were found
```

**No credible defect remains unresolved.**

---

## NO HIDDEN TEST BYPASSES

Audited, not assumed. Every reference test drives the production path:

```text
no addObject            nothing is put into a document behind the drawing API
no modifyObject         nothing is edited behind it either
no hand-built scene     every DrawingScene comes from drawing::sheetScene()
no skipped regeneration Drawn regenerates with the assembly AND drawing
                        handlers registered, and REQUIREs it succeeded --
                        except where a test asserts a failure, which uses
                        activate() and then says which object failed
no injected geometry    no body, transform or solved state is supplied
```

The complete list of production calls the reference tests make:

```text
drawing::  createSheet createView createDimension createAnnotation
           removeSheet removeView removeDimension removeAnnotation
           setViewDefinition sheets views dimensions annotations
           findSheet findView findDimension findAnnotation
           sheetScene projectedGeometry sectionOf toSheet effectiveScale
           measure draw annotationText undefinedDatums
           billOfMaterials itemNumberOf
           viewResolution dimensionResolution annotationResolution
           registerHandlers sheetCount sheetNumber toString validate
assembly:: components activeComponents createMate removeMate
           setComponentDefinition suppressComponent solve registerHandlers
features:: validateDocument Regenerator
io::       saveDocument loadDocument documentToJson
           svgDocument dxfDocument pdfDocument exportSvg exportDxf exportPdf
```

Every one is a published header's function. The builders are the same: they go
through `ModelBuilder`, which calls `Document::addObject` and the feature and
drawing factories exactly as any client would, and nothing else.

---

## THREE-PRESET REGRESSION

Run by `qualification/run-qualification.cmd`, which calls `qualify.cmd` (the
harness `P14-DIM-001` built and `verify-harness.cmd` regression-tests). For
each preset: configure, remove every build output, rebuild with warnings as
errors, prove the binaries are fresh with a no-op rebuild, then run CTest —
and only after a successful build. Then repeat the whole filtered suite five
times in Release and in Debug.

`verify-harness.cmd` was run first and passed: pointed at a preset that does
not exist, the harness fails a real stage and exits 3, so a failed stage
cannot reach nobody.

### The frozen tree

```text
qualification candidate   772d965 + the working tree of this milestone
apps                      81dbb23207c5365f98d2596d9474f37f21d10827
include                   4285ad422f7d739a219596bdc13071b36e12dacb
src                       e5446fb33825a1b20f23d9bb558b615b9cffca50
tests                     baee164dd9cc9819a6847cb85eca1463523eb916
examples                  273eeda0dc706f7c4506d75c93bcdfc809909c84
cmake                     a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt            a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json         951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Recorded before the first build, again after the last test run, a third time
immediately before the commit, and a fourth time **from the commit object
itself** — `git rev-parse HEAD:src` and its seven siblings return exactly the
IDs above. Identical all four times, which is the qualification gate: the tree
that was qualified is the tree that is committed, established from the commit
rather than from the working tree it was made out of. The IDs come
from a scratch index (`GIT_INDEX_FILE`, `git read-tree HEAD`, `git add -A`,
`git write-tree --prefix=`), so they cover the untracked new files as well as
the modified ones. No source or test file changed inside the freeze.

**The qualified tree is the committed tree, and line endings cannot break
that.** The IDs above are of git's normalised blobs, while the compiler read
the working tree, and `.gitattributes` says `* text=auto eol=lf` — so the
committed bytes are LF where the working tree is CRLF. That is only a
difference a compiler can see inside a raw string literal, where a newline is
data. There is no raw string literal in any source this milestone adds or
changes: the one `R"` that `grep` finds is the tail of `"JawR"` in
`DrawingClampSet.cpp`. So every translation unit is byte-identical modulo a
line terminator the lexer discards, and a fresh clone compiles what was
qualified.

### The stages

```text
stage                        exit   wall clock
debug configure                 0   19:59:25 -> 19:59:43
debug clean (attempt 1)         0   19:59:46
debug build                     0   19:59:46 -> 20:24:37
debug no-op rebuild             0   20:24:39      compiled 0, linked 0
debug ctest                     0   20:24:39 -> 20:31:05   2245/2245, 386.64 s
release configure               0   20:31:05 -> 20:31:14
release clean (attempt 1)       0   20:31:15
release build                   0   20:31:15 -> 20:59:45
release no-op rebuild           0   20:59:47      compiled 0, linked 0
release ctest                   0   20:59:47 -> 21:05:33   2245/2245, 345.26 s
debug-shared configure          0   21:05:40
debug-shared clean (attempt 1)  0   21:05:42
debug-shared build              0   21:05:42 -> 21:29:06
debug-shared no-op rebuild      0   21:29:08      compiled 0, linked 0
debug-shared ctest              0   21:29:08 -> 21:35:15   2245/2245, 366.72 s
repeat release (5x)             8   21:35:15 -> 21:59:08   SEE BELOW
repeat debug (5x)               8   21:59:08 -> 22:16:55   SEE BELOW
```

```text
Debug          2245 / 2245     0 warnings
Release        2245 / 2245     0 warnings
Debug-shared   2245 / 2245     0 warnings
```

Every clean was first-attempt: the harness retries a clean up to five times
because OneDrive or a scanner can hold a build output open, and none of the
three needed a second try.

**0 warnings in each preset**, under the 22 warning flags
`cmake/BetterCADCompilerOptions.cmake` sets, `-Werror` among them.
`grep -ci warning` returns 0 over all six build logs — the three builds and
the three no-op rebuilds:

```text
build-debug 0   build-release 0   build-debug-shared 0
rebuild-debug 0 rebuild-release 0 rebuild-debug-shared 0
```

### Fresh-binary proof

Each preset's build was followed immediately by a second build of the same
preset. All three had nothing left to do — the only edge that ran was the
git-revision check, which is always dirty by design, and it produced no
recompile and no relink. So the binaries CTest ran are the binaries that
build produced, and none of the 2245 tests ran against a stale executable.

`ctest -N` on the freshly configured tree lists 2245 tests, 77 more than the
2168 `P14-EXPORT-001` qualified at the baseline commit. The 77 are accounted
for exactly, which is worth doing because a lost test is invisible in an
aggregate PASS:

```text
 56   Catch2 cases in tests/reference/Drawing*Tests.cpp
        5 AngleBracket   8 Bom            7 ClampSet     3 Equivalence
        8 HolePlate     11 Models         3 PocketBlock  4 StepPlate
        7 ToleranceBlock
 15   cli.refmod.* process tests, cli.refmod.build .. export.second-sheet
  5   the DEFECT 1 / 2 regressions in tests/drawing/BomTests.cpp
  1   the DEFECT 3 regression in tests/cli/DrawingCliTests.cpp
 ---
 77   = 2245 - 2168
```

`tests/io/DrawingExportTests.cpp` lost 266 lines and gained none: its readers
moved to `DrawingExportSupport.hpp` so the reference tests read exported files
with `P14-EXPORT-001`'s own parsers rather than a second implementation. Its
test count is unchanged, which is the point — a refactor that changed it would
have shown up in this reconciliation.

The repeat filter matches 2244 of the 2245, and the one it leaves out was
checked rather than assumed: `ctest -N -R` against `ctest -N` names it as
`gui.launch.smoke`. It launches the Qt application, so it is not a drawing
test and repeating it five times would repeat a window, not a determinism
claim. It ran and passed in all three presets' full runs — the 2245/2245 —
and only the repeat stages leave it out.

### BOTH REPEAT STAGES FAILED, ON THE RECORDED ENVIRONMENT FAULT

This is reported before the conclusion because it is a failed stage, and a
failed stage is a failed gate until it is understood.

```text
release   cli.drawing.batch   repeat 1 Passed, 2 Passed, 3 Passed, 4 ***Failed
          bettercad-cli batch: cannot replace
            '.../build/release/tests/cli-output/drawing/drawn.bcad':
            Permission denied
          -> 12 dependents Not Run: the failed test is the drawing_cli
             FIXTURES_SETUP, so ctest correctly refused to run what depends
             on it

debug     cli.refmod.build (the example program)
          DrawnToleranceBlock: DXF export failed: cannot replace
            '.../build/debug/tests/cli-output/refmod/
             drawing_tolerance_block.dxf': Permission denied
          -> its dependents Not Run for the same reason
```

**Neither is a defect in BetterCAD or in this milestone.** Both are a Windows
sharing violation on an ATOMIC REPLACE of a file inside the OneDrive-synced
build tree, on a file the same test had already written successfully in the
same run — in the release case, three times.

`TODO.md` records the identical signature three times before this milestone:

```text
P14-DIM-001    debug repeat
P14-ANNO-001   release repeat
P14-BOM-001    debug repeat -- "the same test passed three times in one run
               before failing on the fourth repeat"
```

and records that "each time one controlled rerun passed". This milestone is
the fourth and fifth occurrence, and the first in which BOTH repeat presets
failed in one qualification.

Measured while the qualification ran: **OneDrive had consumed 20,633 seconds
of CPU — 5.7 hours — and held 596 MB resident**, while twelve compiler
processes saturated the machine. The same clean Debug build that took 16
minutes with no concurrent qualification took 25 here. This is the cost side
of the decision `TODO.md` has flagged as due since `P14-DIM-001`.

Nothing was relaxed in response. No test was excluded, no filter narrowed,
no tolerance changed, no retry added to a test.

### The controlled rerun

`qualification/rerun-repeats.cmd` re-runs the SAME two stages with the SAME
filter — shared with `run-qualification.cmd` through `repeat-filter.cmd` so
the rerun cannot drift from the gate it repeats — and exits with the number
of stages that failed.

```text
stage                     exit   wall clock              result
rerun repeat release (5x)    0   22:18:38 -> 22:34:58    2244/2244 x 5, 979.78 s
rerun repeat debug (5x)      0   22:34:58 -> 22:52:26    2244/2244 x 5, 1048.23 s
```

**The release stage passed, and the repeats are provable rather than asserted.**
`ctest --repeat until-fail:5` reports `100% tests passed out of 2244` — a count
of tests, not of runs — so the runs were counted directly:

```text
11220  "Passed" lines in ctest-repeat-rerun-release.log
 2244  tests x 5 repeats
 ----
       11220 = 2244 x 5, exactly. Every test ran five times and passed
       five times; no test was skipped and none was run fewer times.
```

And the test that failed the gate is in the log five times over:

```text
Test #2155: cli.drawing.batch  ->  Passed 0.34 sec
Test #2155: cli.drawing.batch  ->  Passed 0.30 sec
Test #2155: cli.drawing.batch  ->  Passed 0.29 sec
Test #2155: cli.drawing.batch  ->  Passed 0.19 sec
Test #2155: cli.drawing.batch  ->  Passed 0.15 sec
```

On the qualification run this test passed three times and failed the fourth
with `cannot replace ... drawn.bcad: Permission denied`. Nothing about the test
or the code changed between the two runs — the frozen tree's IDs are the ones
recorded above — so the difference is the filesystem, which is the diagnosis.

**The debug stage passed on the same terms**, counted the same way:

```text
11220  "Passed" lines in ctest-repeat-rerun-debug.log = 2244 x 5, exactly
     0  occurrences of ***Failed, ***Exception, "Not Run" or
        "Permission denied" in either rerun log
```

and the test that failed the debug gate — `cli.refmod.build`, the example
program that builds all eight models and exports them — is in the log five
times:

```text
Test #2169: cli.refmod.build  ->  Passed 2.35 sec
Test #2169: cli.refmod.build  ->  Passed 2.06 sec
Test #2169: cli.refmod.build  ->  Passed 1.99 sec
Test #2169: cli.refmod.build  ->  Passed 2.48 sec
Test #2169: cli.refmod.build  ->  Passed 2.38 sec
```

Its fourteen dependents were not skipped this time: the log holds 75 =
15 x 5 `cli.refmod.*` test results, so every process test in the suite ran
five times over in Debug as well as in Release.

```text
controlled rerun finished Fri 25/09/2026 22:52:26.61, 0 stage(s) failed
```

**Both repeat stages therefore PASS**, and the whole filtered suite — 2244 of
the 2245 tests — has now run five consecutive times in Release and five in
Debug with no failure of any kind. Nothing was relaxed to get there: the rerun
used the same filter from the same file, `--repeat until-fail:5` as before, on
the same frozen tree and the same binaries.

### What this establishes, and what it does not

```text
established   three clean-built presets, 2245/2245 each, 0 warnings, fresh
              binaries, a frozen tree that did not move
established   the whole filtered suite five times over in Release and in
              Debug, on the controlled rerun: 11220 = 2244 x 5 passes in each
              log, counted, not inferred from ctest's summary line
NOT established by an assertion: cross-preset byte identity of exported
              files (see DETERMINISM above)
NOT closed    the OneDrive build-output decision. Two of this
              qualification's seventeen stages failed on it. It is the
              user's decision because `binaryDir` lives in
              CMakePresets.json, inside the tree that gets frozen.
```

---

## KNOWN LIMITATIONS

**A hole's position cannot be dimensioned.** A cylindrical face may be the
target of a radius or a diameter and of nothing else (`P14-DIM-001`), and a
bore's axis has no name of its own under ADR-012. This is the most common
dimension on a machining drawing and it has no spelling in this build.
RM-DWG-04 therefore dimensions between the plate's own datum edges, and the
pattern's geometry is validated from the drawn sheet against positions computed
from the pitch. Closing it means either widening `P14-DIM-001`'s target rules
or giving a hole's axis a semantic name; neither is authorized here.

**A copied face has no CLI spelling.** `face:Block:side:5` names a face;
`Selectors.cpp` says a copied face — a pattern instance's — has no spelling in
that grammar. So RM-DWG-04's per-copy references can be built through the core
API and not through the CLI. The CLI/core equivalence test therefore uses
RM-DWG-01's simpler references.

**A balloon on a component a configuration suppresses fails regeneration.** It
is reported by name and the sheet refuses, which is correct and loud — but it
means a document with configuration-dependent balloons does not regenerate
cleanly in every configuration. RM-DWG-08 is committed in its base state, where
it does. Whether an unresolved annotation should be a *reportable state* rather
than a regeneration failure is a design question, not a defect, and would need
its own decision.

**GD&T symbols still reach SVG only** (carried from `P14-EXPORT-001`): no
single-byte encoding and none of PDF's fourteen standard fonts has a glyph for
position, cylindricity, straightness, flatness or runout, so they are written
as `?` in PDF and DXF. RM-DWG-05 exports all three formats and this is visible
in its PDF and DXF.

**No chamfer or fillet appears in this suite.** Deliberate: `P14-STREF-001`'s
open gap is that a chamfer face is named by the POSITION of its edge reference,
and a reference model built on one would depend on the single reference path
that is still positional.

**Scope.** Eight models, one sheet each except RM-DWG-02's two. No title
blocks, no revision tables, no multi-body drawings, no exploded views.

---

## FILES

```text
examples/reference_models/DrawingReferenceModels.hpp   the suite and its catalogue
examples/reference_models/DrawingSupport.hpp           the drawing builder helper
examples/reference_models/DrawingStepPlate.cpp         RM-DWG-01
examples/reference_models/DrawingAngleBracket.cpp      RM-DWG-02
examples/reference_models/DrawingPocketBlock.cpp       RM-DWG-03
examples/reference_models/DrawingHolePlate.cpp         RM-DWG-04
examples/reference_models/DrawingToleranceBlock.cpp    RM-DWG-05
examples/reference_models/DrawingClampSet.cpp          RM-DWG-06
examples/reference_models/DrawingBoltedStack.cpp       RM-DWG-07
examples/reference_models/DrawingGuardedFrame.cpp      RM-DWG-08
examples/reference_models/DrawingCatalog.cpp           kind -> builder
examples/reference_models/main.cpp                     the drawing loop, --drawings

tests/reference/DrawingTestSupport.hpp                 Drawn, and the shared checks
tests/io/DrawingExportSupport.hpp                      P14-EXPORT-001's readers, shared
tests/reference/DrawingModelsTests.cpp                 the suite-wide contracts
tests/reference/Drawing*Tests.cpp                      per model
tests/reference/DrawingEquivalenceTests.cpp            CLI vs core
tests/CMakeLists.txt                                   the CLI process tests

src/drawing/Annotations.cpp                            DEFECT 1 and 2
include/bettercad/drawing/Annotations.hpp              the corrected contract
apps/bettercad_cli/DrawingReports.cpp                  DEFECT 3
src/drawing/SheetScene.cpp                             a closed circle is a circle
```

---

## RESULT

```text
TASK:            P14-REFMOD-001 — Production Drawing Reference Models

IMPLEMENTATION:  Eight production drawing reference models, RM-DWG-01..08,
                 each owning at least one qualified P14 capability that no
                 other model owns, built through published APIs only, driven
                 from one catalogue that the example program, the tests and
                 the CLI all read.

                 Three production defects the models found, each fixed
                 generally and each with a regression test shown failing
                 against the pre-fix code:
                   src/drawing/SheetScene.cpp      every full circle in the
                                                   system was exported as a
                                                   sampled polyline
                   src/drawing/Annotations.cpp     annotationText() reported a
                                                   balloon as a hole callout,
                                                   and dereferenced a
                                                   disengaged optional on a
                                                   BOM table
                   apps/bettercad_cli/DrawingReports.cpp
                                                   the drawing report merged
                                                   its columns on a 28-char
                                                   name

TESTS:           77 new, reconciled exactly against 2245 - 2168:
                   56  Catch2 cases in tests/reference/Drawing*Tests.cpp
                   15  cli.refmod.* process tests, on the real executables
                    6  the three defects' regressions
                 2245 / 2245 in Debug, Release and Debug-shared, 0 warnings,
                 from a clean build with fresh binaries in each.
                 2244 / 2244 five times over in Release and in Debug —
                 11220 = 2244 x 5 Passed lines in each log, counted.

VALIDATION:      Every expected value derived from the dimensions each model
                 is defined by, never read back from the code under test:
                 analytic volumes (96000 - 1640*pi for RM-DWG-04),
                 2400 - 480 - 160 = 1760 mm^2 for the section's cut area,
                 pitch arithmetic for the pattern's four bore centres and the
                 clearance hole that is not in it, 180/sqrt(2) for the
                 auxiliary view's extent, ISO 286 for the H7 fit. Exported files read back with
                 P14-EXPORT-001's own parsers, with entity count == scene item
                 count asserted in DXF and SVG for all eight so a dropped
                 primitive fails. CLI/core equivalence to identical canonical
                 JSON, identical IDs and byte-identical SVG, DXF and PDF.
                 Tolerances stated with reasons; the A->B->A bound was set at
                 1e-12 mm after measuring a worst deviation of 2.78e-14 mm,
                 which is tighter than the observed spread, not looser.

RESULT:          PASS

                 Three presets clean-built from an unchanged frozen tree,
                 2245/2245 each with 0 warnings and fresh binaries; both
                 determinism repeat stages failed on the recorded OneDrive
                 replace fault and both PASSED on one controlled rerun of the
                 same two stages with the same filter, 2244/2244 five times
                 over in each preset. The models found three production
                 defects; all three are fixed generally with regressions shown
                 failing first. Nothing was relaxed, excluded or loosened to
                 reach this.

EVIDENCE:        docs/verification/P14-REFMOD-001/README.md   this document
                 docs/verification/P14-REFMOD-001/qualification/
                   run-qualification.cmd    the three-preset gate
                   repeat-filter.cmd        the filter, shared with the rerun
                   rerun-repeats.cmd        the controlled rerun
                   verify-harness.cmd       proves the harness can fail
                   configure|clean|build|rebuild|ctest-<preset>.log
                   ctest-repeat-<preset>.log, ctest-repeat-rerun-<preset>.log
                   qualification-times.txt, rerun-times.txt

TODO:            P14-REFMOD-001 -> [x]; next P14-QUAL-001. P14-STREF-001
                 remains open and is a scope decision of its own. The OneDrive
                 build-output decision remains open and is now overdue: two of
                 this qualification's seventeen stages failed on it.
```

WHAT A READER SHOULD NOT TAKE FROM THIS PASS. Three things this milestone does
not establish, stated here so an aggregate PASS does not imply them:

```text
cross-preset byte identity of exported files is NOT pinned by an assertion.
    Each preset writes the same bytes as the core API within its own run, and
    the writers are deterministic by construction, but no test compares a
    Release-written file with a Debug-written one. See DETERMINISM.

a hole's POSITION is still undimensionable, so the commonest dimension on a
    machining drawing is absent from all eight models -- not overlooked, but
    absent. See KNOWN LIMITATIONS.

the suite contains no chamfer and no fillet, so it exercises none of
    P14-STREF-001's open positional-reference path. That milestone's gap is
    untouched by this PASS.
```
