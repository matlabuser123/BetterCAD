# P14-SHEET-001 — Drawing Documents / Sheets / Formats

```text
STATUS:          PASS
BASELINE:        ed3bbea (P14-ARCH-001), clean, HEAD == origin/main
SCOPE:           the drawing module, the sheet model and its persistence.
                 No views, no projection, no hidden-line removal, no
                 dimensions, no annotations, no export.
IMPLEMENTATION:  the ADR-015 layer renumber applied; a new `drawing` module
                 at layer 4; `SheetId`; `Sheet` as a DocumentObject; its JSON
                 mapping; and the CLI description
TESTS:           46 new -- 30 in tests/drawing/SheetTests.cpp, 11 in
                 SheetFileTests.cpp, 2 CLI cases, 2 compile-failure cases,
                 and 1 architecture-checker fixture
REGRESSION:      1720/1720 on debug, release and debug-shared, each from
                 clean; 539/539 five times over in release and debug;
                 0 compiler warnings in all three builds; 17/17 stages exit 0
ADVERSARIAL:     16 questions, 4 findings, 3 of them production defects, all
                 fixed and covered
EVIDENCE:        this directory
```

## Scope

The first milestone of `P14` to write code. It implements
[ADR-010](../../architecture/decisions/ADR-010-drawings-live-in-the-document.md)
(drawings are document objects),
[ADR-011](../../architecture/decisions/ADR-011-drawing-intent-is-canonical-projection-is-derived.md)
(intent is persisted, geometry is derived),
[ADR-013](../../architecture/decisions/ADR-013-view-orientation-and-drawing-scale.md)
(scale is an exact ratio),
[ADR-015](../../architecture/decisions/ADR-015-drawing-module-and-layer.md)
(the module and its layer) and
[ADR-017](../../architecture/decisions/ADR-017-drawing-identity-model.md)
(sheets are document objects), and nothing else.

Absent on purpose, and verified absent: any view, projection, hidden-line,
section, dimension, annotation, BOM or export code. `src/drawing/` and
`include/bettercad/drawing/` contain the sheet and nothing more.

**There is no `DrawingId`.** The checklist says to implement drawing and sheet
identities "only if ADR-015/P14-ARCH-001 defined both as independent
persistent identities". ADR-017 defines `SheetId`, `ViewId`, `DimensionId`
and `AnnotationId` — no `DrawingId`. A drawing is the set of sheets in the
document, so no type was allocated for it.

## ADR-015 layering

The renumber `P14-ARCH-001` decided and deferred is applied here, in the
milestone that creates `src/drawing/` — the same way ADR-006 was decided by
`P13-ARCH-001` and applied by `P13-COMP-001`.

```text
core 0, sketch 1, features 2, assembly 3, drawing 4, io 5, renderer/scripting 6
```

`tests/architecture/CheckLayering.cmake:22-36` holds it; `src/CMakeLists.txt`
adds `drawing` between `assembly` and `io`. No existing file's includes
changed, exactly as ADR-006 predicted for its own renumber: everything `io`
uses is below both its old and its new number.

**All five places that describe layering now agree.** ADR-006's identical
renumber left `ARCHITECTURE.md` and `docs/architecture.md` stale for fifteen
milestones — `P13-QUAL-001` found it — so this milestone carried both updates
as explicit checklist items:

| File | State |
| --- | --- |
| `tests/architecture/CheckLayering.cmake` | the table in force |
| `src/CMakeLists.txt` | `add_subdirectory(drawing)` in layer order |
| `ARCHITECTURE.md` | table, and the note that the renumber is applied |
| `docs/architecture.md` | table and the `bettercad_drawing` target row |
| `CLAUDE.md` | **corrected** — see finding 4 |

A new checker fixture pins the direction the renumber created:
`architecture.checker.drawing-layer-violation` fails the build if `drawing`
ever includes an `io` header. The seven existing checker self-tests still
pass, and `architecture.layering` passes over the real tree.

## The drawing module

```text
include/bettercad/drawing/Sheet.hpp     the sheet, its formats and its scale
include/bettercad/drawing/Sheets.hpp    the document-facing operations
src/drawing/Sheet.cpp                   ISO 216 sizes, validation, geometry
src/drawing/Sheets.cpp                  create / find / list / remove
src/drawing/CMakeLists.txt              bettercad_drawing, ALIAS drawing
```

It links `BetterCAD::core` only — it does not yet need `features` or
`assembly` — and contains no OCCT, as ADR-015 requires. The document-facing
operations are free functions, not `Document` members, because `Document` is
layer 0 and must not know drawings exist.

## Identity

`SheetId` follows the established pattern exactly: a tag with a diagnostic
name, an `isDocumentObjectTag` specialisation so it widens to `ObjectId`, and
an alias. It comes from the document's one allocator, so no ID is ever
reused.

Two compile-failure cases pin what it is not:

```text
compile_fail.ids.object-to-sheet-id        an ObjectId does not narrow to a SheetId
compile_fail.ids.sheet-id-as-component-id  a sheet is not a component
```

**The number a sheet shows is not its identity.** `sheetNumber()` is a
position in `sheets()` and `sheetCount()` is how many there are; both change
when another sheet is deleted, and neither is stored.
`Sheet_NumberIsAPositionAndTheIdIsNot` deletes the first of three sheets and
asserts every number shifted and no ID did.

## Canonical and derived

```text
CANONICAL — persisted            DERIVED — computed on every call
---------------------            --------------------------------
format                           width and height
orientation                      sheet bounds
custom size (Custom only)        usable region
margins                          the scale's printed label
scale (an exact pair)            the sheet number and count
title-block field values
```

Two checks make this more than a claim.
`SheetFile_HoldsIntentAndNeverDerivedGeometry` saves an A3 sheet and greps the
bytes: `format`, `orientation`, `margins` and `scale` are present; `width`,
`height`, `usable`, `bounds`, `sheet_number` and `sheet_count` are absent. And
the reader **refuses** a standard sheet that carries a width or height, rather
than silently preferring one source over the other — a file that disagreed
with ISO 216 would be the derived-state-as-truth failure ADR-011 exists to
prevent.

## Standard formats

ISO 216 A-series, in portrait, as the standard's own rounded millimetre
values:

| Format | Portrait | Landscape |
| --- | --- | --- |
| A0 | 841 × 1189 | 1189 × 841 |
| A1 | 594 × 841 | 841 × 594 |
| A2 | 420 × 594 | 594 × 420 |
| A3 | 297 × 420 | 420 × 297 |
| A4 | 210 × 297 | 297 × 210 |

They are written as integers rather than computed from A0's square metre and
the √2 ratio, because the standard rounds at every step and recomputing would
disagree with it by up to half a millimetre.
`Sheet_EachAFormatIsTheNextOneHalvedAcrossItsLongEdge` checks the defining
property against the rounded values: each size's width is the previous one's
height halved to within the standard's own 1 mm rounding, and its height is
the previous one's width exactly.

ANSI sizes are deliberately absent. An ANSI B sheet is not A3 and is not
approximated as one.

## Orientation

Portrait is the ISO definition — short edge horizontal. Landscape swaps.

What is stored is the **format and the orientation**, never a width and a
height, so a round trip cannot drift:
`Sheet_OrientationRoundTripRestoresTheSizeExactly` goes portrait → landscape →
portrait and asserts the size is bit-identical to where it started. There is
nothing to drift because there is nothing stored to drift.

## Units and scale

Every length is a `Length`; sheet coordinates are millimetres from the
sheet's bottom-left corner. Scale is an exact `paper : model` pair per
ADR-013:

```text
1:1  factor 1.0      1:2  factor 0.5      2:1  factor 2.0
```

`Sheet_ScaleKeepsARatioADoubleCouldNotHold` is the one that justifies the
pair: `1:3` round-trips exactly and prints as `1:3`, and `2:4` stays
distinguishable from `1:2` despite having the same factor, because the label
is intent. A zero term is refused; a negative one is not representable,
because the terms are unsigned.

**Scale is stored and validated here, and applied to nothing.** Applying it to
projected geometry is the view milestones'.

## Margins and the usable region

Four independent lengths, because a binding edge is conventionally wider.
The usable region is derived:

```text
usable = [left, width - right] x [bottom, height - top]
```

Worked by hand for A3 landscape (420 × 297) with margins 20/10/15/5:
x from 20 to 410, y from 5 to 282 — which is what
`Sheet_UsableRegionIsTheSheetLessItsMargins` asserts. Margins that leave no
room are refused with a message naming which dimension failed, and
`Sheet_UsableRegionFollowsAnOrientationChange` shows the region recomputing
when the sheet turns.

No border geometry is persisted: it is a function of the size and the
margins.

### One tolerance, and its reason

Derived lengths are compared to 1e-9 mm, not exactly. Lengths are stored in
SI, so a usable height is `(0.297 - 0.015) - 0.005` in doubles, which is
`0.27699999999999997` — one ULP out after two subtractions of same-magnitude
quantities, an error of 5.7e-14 mm. CLAUDE.md's figure for well-conditioned
double-precision algebra is 1e-12 in SI, which is 1e-9 mm and four orders of
magnitude wider than anything observed.

Exact decimal equality is not a property binary floating point has, and the
first version of these tests asserted it and failed. What **is** exact, and is
asserted separately, is that the same definition yields bit-identical geometry
every time (`Sheet_TheSameDefinitionAlwaysGivesTheSameGeometry`, over all five
formats in both orientations).

## Title block

Eight free-text fields: title, drawing number, revision, designer, checked by,
approved by, date, organization. Semantic content only — no lines, no boxes,
no text positions. Those are derived and belong to a later milestone.

Three things a title block displays are deliberately **not** fields:

```text
the scale text    the sheet's own DrawingScale::label()
the sheet number  its position among the document's sheets
the sheet count   how many there are
```

Storing any of them would let the file disagree with the document. An empty
title block writes no `title_block` key at all, and a partially filled one
writes only the fields that say something.

The date is text rather than a computed instant: a drawing date is what was
issued.

## Multiple sheets

`Sheet_SheetIdIsNotAffectedByDeletingAnotherSheet` creates A, B and C, deletes
B, and asserts A and C keep the IDs they had, C is still C, and a new sheet
gets a fresh ID rather than the one B freed. `sheets()` returns ascending ID
order, which is stable because IDs are never reused.

A user-chosen sheet order would need an explicit field and is not in this
milestone; ascending ID is the order, and that is stated in the header rather
than left implicit.

## Persistence

| Check | Result |
| --- | --- |
| Round trip preserves every field and the ID | PASS |
| Derived geometry recomputed, bit-identical | PASS |
| Saving twice gives the same bytes | PASS |
| Load → save reproduces the file exactly | PASS |
| Existing `{id, type, name, data}` envelope, no new top-level key | PASS |
| No format version bump | PASS |
| A custom sheet writes its size; a standard one does not | PASS |
| The scale is written as its pair, not its quotient | PASS |
| Every committed model still loads and has no sheet | PASS, 32 models |

`SheetFile_MalformedSheetsAreRefusedAndNothingPartiallyLoads` covers eleven
malformed files: an unknown format, an unknown orientation, a zero scale term,
margins that leave no room, a standard sheet carrying a size, a missing scale,
missing margins, a scale that is not a pair, a negative scale term, an unknown
key, and an unknown title-block key. Each is refused with a message naming the
problem, and a control file with the same envelope and a good sheet loads — so
the refusals are the sheet's, not the envelope's.

`Sheet::create` validates, so a malformed sheet never reaches a document even
from a file.

## Failure atomicity

| Check | Result |
| --- | --- |
| A rejected sheet consumes no ID and changes nothing | PASS — the next good sheet gets the ID the rejected one would have had |
| A failed edit leaves the definition **and the revision** unchanged | PASS |
| An edit that changes nothing does not bump the revision | PASS |
| Operations on a missing sheet fail with `NotFound` | PASS |
| A duplicate name is refused | PASS |
| A `SheetId` built from a parameter's number finds nothing | PASS |

## Commands and undo

**No drawing commands were added.** ADR-017 records that core's
`AddObjectCommand` and `DeleteObjectCommand` work on any `DocumentObject`, and
that validating wrappers are `P14-CMD-001`'s. That claim is checked rather
than assumed: `Sheet_UndoAndRedoWorkThroughTheGenericCommands` adds a sheet
through the generic command, undoes it, redoes it, and asserts the **same ID**
comes back with the same name and definition, then does the same for a delete.

Pulling `P14-CMD-001` forward would have been out of scope; leaving the claim
untested would have left the deferral unjustified.

## Regeneration

`Sheet_RegeneratesCleanlyAndProducesNoGeometry` runs a full `regenerateAll` on
a document of sheets: it succeeds, nothing fails, nothing is blocked, and no
body is produced.

A sheet gets **no regeneration handler**, and that is the honest boundary
ADR-014 draws: a handler exists to resolve an object's references and fail if
one does not resolve, and a sheet references nothing. A view does, and will
need one — an object with no handler is silently marked `UpToDate` and never
validated, which is the defect `P13-REGEN-001` fixed for mates.

## Determinism

| Check | Result |
| --- | --- |
| Same definition → bit-identical size, bounds and usable region | PASS, 5 formats × 2 orientations |
| Asking twice gives the same answer | PASS |
| Saving twice gives identical bytes | PASS |
| Geometry survives a file round trip bit-identically | PASS |
| 539 related tests, five times over, in release and in debug | PASS |

No dependence on container traversal order: `sheets()` walks `document.objects()`,
which is ascending by ID.

## Regression

| Preset | Configure | Clean | Build | No-op rebuild | Tests | Warnings |
| --- | --- | --- | --- | --- | --- | --- |
| `debug` | exit 0 | exit 0 | exit 0, 9m50s | **0 compiles, 0 links** | **1720/1720** | 0 |
| `release` | exit 0 | exit 0 | exit 0, 13m52s | **0 compiles, 0 links** | **1720/1720** | 0 |
| `debug-shared` | exit 0 | exit 0 | exit 0, 11m10s | **0 compiles, 0 links** | **1720/1720** | 0 |

```text
repeat release, --repeat until-fail:5   539/539
repeat debug,   --repeat until-fail:5   539/539

17 stages with a recorded exit code, 17 of them 0
```

1674 → 1720 is this milestone's 46 new tests, and all 46 are confirmed **by
name** in each of the three ctest logs.

**0 warnings** in all three build logs, counted by searching them rather than
inferred from the exit code, with `-Werror` and 22 warning flags.

The no-op rebuild compiled nothing and linked nothing in every preset, so the
binaries under test were the ones just built.

## Adversarial review

Sixteen questions. Four findings, three of them production defects.

| Question | Answer |
| --- | --- |
| Did ADR-015 layering get applied everywhere? | Yes — and CLAUDE.md was found stale too (finding 4) |
| Are architecture docs still stale anywhere? | No |
| Can drawing depend on io accidentally? | No — new checker fixture fails the build |
| Can io/drawing form a cycle? | No — `drawing` links only `core` |
| Can a `SheetId` change when another sheet is deleted? | No — tested |
| Can the sheet number be confused with identity? | No — tested, they diverge |
| Can orientation swap dimensions twice? | No — the size is derived, not stored |
| Can scale 1:2 be read backwards? | No — `factor()` tested both ways |
| Can margins make the usable region negative? | No — refused at validation |
| Can title-block graphics become canonical? | No — only semantic fields exist |
| Can derived border geometry enter the file as authority? | No — and a file that tries is refused |
| Can two sheets get the same persistent ID? | No — the allocator never reuses |
| Can malformed data partially load? | No — 11 cases, nothing produced |
| Can Debug and Release differ? | No — 1720/1720 identical in all three |
| Can pre-P14 documents still load? | Yes — all 32 committed models |
| Did view functionality leak in ahead of scope? | No |

### Findings

**F-1 — `sheetNumber()` and `sheetCount()` were `noexcept` but allocate.**
Both call `sheets()`, which builds a `std::vector`. A `noexcept` function that
throws calls `std::terminate`, so an allocation failure would have aborted the
process instead of propagating. Fixed: both dropped `noexcept`. `findSheet()`
keeps it, correctly — it allocates nothing. *Found by reviewing my own diff,
not by a test.*

**F-2 — `Sheet::size()` dereferenced an optional unchecked.** Unreachable
today, because `validate()` refuses a format with no standard size and a
`Sheet` cannot hold an unvalidated definition. Undefined behaviour the day a
format is added to the enum without a size. Fixed: the lookup is checked, so
that mistake yields a zero-sized sheet a test would catch rather than UB.

**F-3 — the shared build did not link.**

```text
DocumentCommands.cpp: undefined reference to
  `bettercad::drawing::DrawingScale::label[abi:cxx11]() const'
```

`DrawingScale` is a plain value struct with no export macro, so an
out-of-line member definition is hidden under `-fvisibility=hidden`. Every
other member of that struct was already inline; `label()` was written
out-of-line. Fixed by making it inline, with the reason recorded at the
declaration.

**Debug passed. Release passed. Both their full suites passed.** Only
`debug-shared` caught it — which is why it is a hard gate. The qualification
script behaved correctly under the failure: it recorded `build exit 1` and
logged `debug-shared ctest skipped: the build failed` rather than testing
stale binaries, which is the `P13-XFORM-001` lesson holding.

**F-4 — CLAUDE.md's layer table was stale by two renumbers.** It read
`core 0, sketch 1, features 2, io 3, renderer/scripting 4` — wrong since
ADR-006, and about to be wrong twice. `P13-QUAL-001` had flagged it and left
it as out of scope. Corrected here to the table in force, with a pointer to
`CheckLayering.cmake` as its source of truth. Only the numbers changed.

A fourth issue was caught by a test rather than by review: the writer dispatch
arm was inserted as an early `return` rather than into the `if`/`else if`
chain, so a sheet's data replaced the `{id, type, name, data}` envelope instead
of filling it. `SheetFile_UsesTheExistingObjectEnvelopeAndNoNewKey` failed
exactly as written to.

## Known limitations

- **Sheet order is ascending `SheetId`.** A user-chosen order needs an
  explicit field on the sheet, and is not in this milestone.
- **No drawing-specific commands.** The generic ones work and are tested;
  validating wrappers are `P14-CMD-001`'s.
- **Sheets cannot be suppressed by configuration.** The override machinery
  enumerates exactly the kinds it knows in seven places; adding a fourth is
  not free and `P14` does not do it.
- **ANSI and architectural sheet sizes are not supported.** Only ISO 216 A0–A4
  and Custom.
- **A sheet has no regeneration handler**, because it references nothing. A
  view will need one.
- **The title block is data only.** Nothing draws it.
- **Scale is stored and validated, not applied.** Nothing is projected yet.

## Result

```text
TASK:            P14-SHEET-001 — Drawing documents / sheets / formats
IMPLEMENTATION:  the drawing module at layer 4, SheetId, Sheet, its JSON
                 mapping, the CLI description, and the ADR-015 renumber
TESTS:           46 new; 1720/1720 in debug, release and debug-shared, each
                 from clean; 539/539 five times over in release and debug
VALIDATION:      ISO 216 sizes and every derived region checked against
                 values computed by hand, not read back from a Sheet
ADVERSARIAL:     16 questions, 4 findings, 3 production defects, all fixed
WARNINGS:        0 in all three builds
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-SHEET-001 → [x]
NEXT:            P14-VIEW-001 — base and projected drawing views
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
qualification/ctest-*.log                3 presets, 1720/1720 each
qualification/ctest-repeat-*.log         release and debug, until-fail:5
```
