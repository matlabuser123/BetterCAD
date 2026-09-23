# P14-ANNO-001 — Drawing Annotations

```text
TASK:            P14-ANNO-001
BASELINE:        4e29d8c (P14-DIM-001, qualified)
STATUS:          PASS, on the second qualification. The first FAILED two
                 stages: a production defect of this milestone's, fixed, and
                 a recurrence of the filesystem fault P14-DIM-001 recorded.
                 Both runs are in the evidence. See FULL REGRESSION.
```

## SCOPE

Text notes, leaders, centrelines, centre marks, model-driven hole callouts,
and the semantic foundations of surface-finish and datum symbols; their
placement, their paper sizing, and the neutral primitives they produce.

Out of scope, and NOT done: feature-control frames and the rest of GD&T
(P14-TOL-001), BOM balloons (P14-BOM-001), assembly drawing views
(P14-ASM-001), the export writers (P14-EXPORT-001). No code for any of those
was written. The whole `DrawingScene` ADR-016 describes — the sheet frame,
layers, hatch regions, line weights — is deliberately not built here either;
see DRAWING-SCENE OUTPUT.

Carried open and untouched: **P14-HLR-001's assembly-to-assembly occlusion
validation remains blocked on P14-ASM-001.**

## BASELINE

`4e29d8c`, clean tree, `HEAD == origin/main`. Verified before starting, as the
brief requires: P14-DIM-001's fifteen checkboxes are all `[x]`, its evidence
carries no placeholders, and its one non-zero qualification stage is the
recorded Run A failure with Run D's pass documented beside it.

The audit that shaped the work:

| Question | Answer |
| --- | --- |
| Is there a `DrawingScene` to produce into? | **No.** ADR-016 decided one; nothing implements it |
| Is there an existing annotation object? | No |
| What may an annotation point at? | ADR-012's list: datum/principal plane, datum/principal axis, named face, document object |
| Can a hole's bore be named? | **No** — P12 names a hole's bottom and floors, not its wall (found in P14-DIM-001) |
| Does anything already derive a hole's callout? | **Yes.** `features::holeCallout()` resolves diameter, tolerance, deviations and thread from the document |

The last two rows decided the hole callout's design. Because a hole's bore is
not a nameable face, a callout points at the hole **feature** — which ADR-012
permits as a document object, and which is the better level anyway: the
feature knows its own diameter, depth and extent, and `holeCallout()` already
resolves them through parameters, thread standards and ISO 273 clearances.

### One addition to a qualified type

`HoleCallout` carried a diameter, a tolerance and a thread, and **no extent or
depth** — so it could not say THRU or DEEP 20. A callout type that cannot
express its own callout is incomplete, and the resolved values were already to
hand in the function that builds it. It now carries `extent` and `depth`,
filled from the same resolution, so nothing outside re-derives them and gets a
different answer.

## ANNOTATION CONTRACT

```text
canonical (stored)     the view; the kind; what it points at; the words an
                       engineer chose; the paper text height; where it sits;
                       the symbol's own values
derived (never stored) the resolved text of anything model-driven; every
                       line, arrow, arm and frame; the scene items
```

A hole callout stores **which hole**, never "10". A note stores its words,
because "DEBURR ALL EDGES" is not derivable from geometry and is intent like
any other. `annotationToJson` writes text only for the kinds whose words are
an engineer's choice, and a test greps the saved file to prove a callout's
text is not in it.

### The invariant the whole milestone turns on

```text
WHERE it points   follows the model through the view's projection,
                  so it moves when the view moves or its scale changes

HOW BIG it is     is paper millimetres, and nothing multiplies it
```

Text height, arrowheads, centre-mark arms, datum boxes, leader elbows and
symbol proportions are sheet lengths. The only thing that ever meets
`DrawingScale` is the position of the geometry being labelled.

Symbol sizes are multiples of the annotation's own **text height**, as ISO
3098 and ISO 1302 give them, so a drawing lettered at 5 mm gets symbols to
match one lettered at 3.5 mm. Those multiples are the only sizes in the file.

## IDENTITY

`AnnotationId`, a tag on the existing `Id` template whose value widens to
`ObjectId` — ADR-017's model, and no new identity scheme. Identity is the
document object's, so it survives save and load and is unaffected by
inserting, renaming or deleting anything else; a test asserts that the
definition is byte-identical after an unrelated add, rename and delete.

## PLACEMENT

**Absolute sheet coordinates, in millimetres**, and the brief asks for this to
be stated rather than left ambiguous. A note placed at (30, 20) stays at
(30, 20) when its view is moved: an engineer who put it there meant there. The
thing it POINTS at is found through the view, so the leader stretches while
the text stays put.

## TEXT NOTES

Words on paper, pointing at nothing. A note that carried a target is refused
("use a leader to point"), and so is a note with nothing to say. One
`SceneText` at its placement, at its own height, and no lines at all.

## LEADERS

An elbow one text height wide, a slanted line to the target, and an arrowhead
— all paper-sized. The elbow runs **toward** the target so the text never sits
on its own leader.

The arrow tip is asserted to land where the view drew the thing it points at:
the plate is 100 × 60, so a top view centres on (50, 30) and the hole at
(20, 30) lands 30 mm left of the view's placement. That is the one number a
leader must hit, or it is pointing at nothing.

## CENTRELINES

From anything with an axis: a named cylindrical face, a datum axis, or a hole
feature. Drawn along the projected axis, spanning the view's extent along it
plus a paper extension at each end, in `LineStyle::Centre`.

**An axis pointing at the viewer is refused**, and the refusal says a centre
mark is what that view wants — a centreline seen end-on would draw as a point,
and drawing nothing would be worse than saying so.

| Scale | Span drawn |
| --- | --- |
| 1:1 | 20 mm + 3 + 3 |
| 1:2 | 10 mm + 3 + 3 |
| 2:1 | 40 mm + 3 + 3 |

The plate's 20 mm thickness scales; the 3 mm extension does not.

## CENTRE MARKS

Two arms crossing at the projected centre, along the sheet's own axes, each
given in paper millimetres, in `LineStyle::Centre`.

This is the test that proves both halves of the invariant at once, because
either alone could pass for the wrong reason — a mark that never moved would
also never change size:

| Scale | Arm length (paper) | Distance left of the view's placement |
| --- | --- | --- |
| 1:1 | 5.0 mm | 30 mm |
| 1:2 | 5.0 mm | 15 mm |
| 2:1 | 5.0 mm | 60 mm |
| 1:10 | 5.0 mm | 3 mm |
| 5:1 | 5.0 mm | 150 mm |

## HOLE CALLOUTS

Model-driven, always. The text is built from `features::holeCallout()` plus
the resolved extent and depth, through the formatter P14-DIM-001 qualified —
integer rounding, half away from zero, locale-free — and never through a
second one.

| Hole | Callout |
| --- | --- |
| Ø10 through | `Ø10 THRU` |
| Ø10 blind, 12 deep | `Ø10 DEEP 12` |
| the same hole edited to Ø12 | `Ø12 THRU` |
| then made blind at 8 | `Ø12 DEEP 8` |

Same annotation, same target, no drawing edit between them. A callout that
tried to store its own text is refused, and a callout pointed at something
that is not a hole is refused when it is asked for its text.

## SURFACE-FINISH FOUNDATION

Semantic: a roughness as a **Length** (so 3.2 µm is 3.2 µm and not a bare
number whose unit has to be remembered) and a material-removal requirement —
ISO 1302's three basic cases. Derived: the tick, the bar or circle that says
which case, the value, and a leader.

A foundation, not the standard: lay direction, machining allowance and
two-limit requirements are not here, the "removal prohibited" circle is drawn
as a small square, and both are recorded as limitations.

## DATUM FOUNDATION

Semantic: one letter and what it is attached to. Derived: the box, the letter
centred in it, the line to the surface and the triangle on it.

ISO 5459's rules are enforced: a single letter, a capital, and **not I, O or
Q**, which read as 1 and 0. Each is a separate refusal with its own message.
Feature-control frames are P14-TOL-001's and are not here.

## SCALE INDEPENDENCE

The hard gate, asserted at five scales (1:1, 1:2, 2:1, 1:10, 5:1):

| What | Expected | Result |
| --- | --- | --- |
| a note's text height | 3.5 mm | 3.5 mm at every scale |
| a hole callout's text height | 3.5 mm | same |
| a datum letter's height | 3.5 mm | same |
| a centre mark's arms | 5.0 mm | same |
| a leader's arrowhead | √(1² + 0.25²) × 3.5 = 3.6072 mm | same |
| a datum box | 7.0 × 7.0 mm | same |

And the positions are asserted to **move** with the scale, so "nothing
changed" could not pass by everything being frozen.

## MODEL-DRIVEN UPDATES AND STABLE REFERENCES

Every model-driven annotation resolves through the qualified reference
vocabulary. A hole callout follows its hole through diameter, extent and depth
edits with the same `AnnotationId` and the same target.

**A missing target fails and draws nothing.** Deleting the hole makes the
callout unresolved with a diagnostic naming it; there is no cached text or
geometry to leave standing.

**No silent rebinding.** The hard gate: two holes of the same diameter, the
same extent, in the same face, 50 mm apart. The callout is pointed at the
first, the second is re-parented onto the plate so it survives, and the first
is deleted. A resolver that looked for "a hole like the one that went" would
find the survivor and carry on. The callout becomes unresolved instead, with
the survivor asserted still present so the test cannot pass by the geometry
merely being gone.

## FAILURE ATOMICITY

Six rejected edits, each asserted to leave the definition byte-identical and
the annotation still drawing what it drew: no view, a zero text height, a
negative text height, a target naming two things at once, a view that is not
in the document, and a centre mark with no arms.

A sheet whose annotations cannot all be drawn draws **none** of them: one that
quietly dropped the annotation it could not resolve would look complete and
not be.

## DRAWING-SCENE OUTPUT

ADR-016's boundary, as far as annotations need it: `SceneLine`, `SceneText`,
`LineStyle`, `TextAnchor` and `SceneItems`, in a neutral header under
`drawing`, carrying no writer's name, in sheet millimetres, already scaled and
placed so that a writer multiplies nothing.

It **validates itself** in `drawing`, once, as ADR-016 requires — finite
coordinates, at least two points in a line, a positive text height, no empty
text run — so every writer inherits the guarantee rather than re-deriving it.
Everything every annotation draws is asserted to pass those checks.

The rest of the scene — the sheet frame, layers, hatch regions, line weights,
the assembly of a finished sheet — is P14-EXPORT-001's and was not built. These
types are what it will carry.

## DETERMINISM

Drawn six times over and compared bit for bit on every point and every text
run. The annotations of a view are drawn in ascending `AnnotationId` order,
which is the document's own order and does not vary. Nothing in the path
depends on unordered iteration, pointer addresses, kernel traversal order or
timing.

## ADVERSARIAL REVIEW

The brief's nineteen questions, answered:

| Question | Answer |
| --- | --- |
| Can text scale with the model? | No — five scales asserted |
| Can a 1:2 view halve the text? | No — same test |
| Can moving the view move an absolute note? | No — placement is absolute sheet coordinates, and the note is asserted to stay |
| Can leader target and text use incompatible spaces? | No — the target goes through `toSheet`, which is the view's own projection; the tip is asserted at a hand-computed point |
| Can a centre mark move because of scale? | Its POSITION must, and does; its SIZE must not, and does not. Both asserted together |
| Can a centreline come from tessellation? | No — from the exact axis of the hole or cylinder |
| Can a callout go stale? | No — nothing is stored; asserted across three edits |
| Can a callout bind to another identical hole? | No — the no-rebind gate |
| Can a datum attach to a similar face? | No — resolution is by name or by object ID, never by geometry |
| Can a missing target leave old geometry visible? | No — nothing is cached |
| Can save/load change target identity? | No — definitions compared by value, and the drawn output compared too |
| Can unordered iteration change layout? | No — ID order, asserted |
| Can Debug and Release order differently? | The three-preset run is the evidence |
| Can automatic placement oscillate? | Not reachable — there is no automatic placement; every annotation is placed where it was put |
| Can the roughness be lost while the symbol still draws? | No — a surface finish without a roughness is refused at creation |
| Can drawn geometry become authoritative? | No — there is nowhere to store it |
| Did this start GD&T? | No |
| Did this start BOM balloons? | No |

**One defect found, fixed, with a regression test.** A centreline's span was
measured from the view's **policy-filtered** edges, so turning hidden lines off
made it shorter. A display setting must not change an annotation's geometry,
and this would have done it invisibly — the line would simply have been a bit
short. It now measures from the view's bounds, which P14-HLR-001 defines as
the extent *before* anything is suppressed. The test draws the same centreline
with hidden lines on and off and requires the same length.

## KNOWN LIMITATIONS

Recorded, not worked around.

1. **A centreline spans the whole view's extent along the axis**, not the
   individual feature's. A hole in a large plate gets a centreline as long as
   the plate. Bounding a face along its own axis needs face extents that
   `FaceInfo` does not carry.
2. **`toSheet` recomputes the hidden-line drawing on every call**, so drawing
   N annotations on a view runs hidden-line removal N times. Correct but
   wasteful; nothing caches, because ADR-011 and ADR-014 make drawing geometry
   derived and a cache needs an invalidation rule decided deliberately.
3. **The surface-finish symbol is a foundation.** The "removal prohibited"
   circle is drawn as a small square, and lay direction, machining allowance
   and two-limit requirements are absent.
4. **A leader starts at the text's placement point**, so it can run under the
   text rather than from the end of an underline. Cosmetic, and a writer that
   knows its font can do better.
5. **A datum's line to the surface starts at the box centre**, so it crosses
   the box. Cosmetic, the same way.
6. **No automatic placement or collision resolution.** Every annotation is
   drawn where it was put. That is why the determinism question about
   oscillating placements does not arise, and it is a deliberate omission
   rather than an oversight.
7. **Text bounds are not computed.** Nothing here knows how wide a string is,
   because that needs a font; `SceneText` carries its height and anchor and
   leaves the rest to a writer.
8. **Only a hole feature is meaningful as an object target.** Pointing at any
   other object is refused with a message saying why.

## IMPLEMENTATION

New:

| File | Lines | What |
| --- | --- | --- |
| `include/bettercad/drawing/Scene.hpp` | 124 | ADR-016's neutral primitives, as far as annotations need them |
| `include/bettercad/drawing/Annotation.hpp` | 220 | the contract: types, targets, style, the `Annotation` object |
| `include/bettercad/drawing/Annotations.hpp` | 96 | the document-facing operations, `annotationText()` and `draw()` |
| `src/drawing/Scene.cpp` | 107 | the styles, and the scene's own validation |
| `src/drawing/Annotation.cpp` | 281 | validation, including ISO 5459's datum-letter rules |
| `src/drawing/Annotations.cpp` | 496 | resolution and every line, arrow, arm and frame |
| `src/io/json/AnnotationJson.cpp` | 208 | persistence of intent, and of nothing drawn |
| `tests/drawing/AnnotationTests.cpp` | 939 | 34 cases |

Changed: `Id.hpp` (`AnnotationId`), `HoleFeature.hpp` and
`HoleRegeneration.cpp` (`HoleCallout` gains extent and depth), `Views.hpp` and
`Views.cpp` (`toSheet`, the view's own projection made available so an
annotation lands on what it labels), `DocumentJson.cpp` and `ObjectJson.hpp`
(the two dispatch sites), three `CMakeLists.txt`.

## FULL REGRESSION

**The first qualification FAILED.** The harness reported it itself — the exit
code fix P14-DIM-001 made earned its keep on its first real use, printing
`QUALIFICATION FAILED: 2 stage(s) failed` and exiting non-zero. Under the old
harness this would have exited 0 with a broken shared build and a failed
determinism repeat sitting quietly in two logs.

| Stage | First run | Cause |
| --- | --- | --- |
| `debug-shared` build | **exit 1** | a production defect of this milestone's |
| repeat `release`, `until-fail:5` | **exit 8** | the filesystem fault P14-DIM-001 recorded, recurring |

Its logs are kept as `qualification-times-voided.txt`,
`build-debug-shared-voided.log` and `ctest-repeat-release-voided.log`. A failed
gate is evidence.

### The defect, and why only one preset could find it

`SceneItems::isEmpty()` was marked with the export macro **and defined inline
in the header**. In a shared build that macro becomes `dllimport`, and a
dllimport function may not have a definition. Debug and Release compiled it
without complaint; only `debug-shared` failed. The fix is to drop the export
from the inline definition — an inline member is compiled into every
translation unit and there is nothing to import — and every other export in
the drawing headers was checked for the same shape.

This is the second time `debug-shared` has caught a real defect in P14. The
first was `DrawingScale::label()` in P14-SHEET-001, which was the mirror-image
mistake: an out-of-line definition that needed to be inline. Neither is
findable without that preset.

### The filesystem fault has now recurred

```text
P14-DIM-001   debug repeat    cannot replace built.bcad: Permission denied
P14-ANNO-001  release repeat  cannot replace built.bcad: Permission denied
```

Same test, same message, different preset, different milestone. P14-DIM-001
recorded it as intermittent because it did not reproduce; **that assessment no
longer holds** — it is a recurring fault, and its cause is structural: the
build tree lives under OneDrive, which opens files to sync them.

It passed on retry both times, so it does not indicate anything wrong in the
code. The remediation — moving build and test output off the synchronised
directory — is **still not done here**, for the same reason as before: the
build directory is set in `CMakePresets.json`, inside the frozen qualified
tree, and changing it would void this qualification and alter a file four
milestones have been qualified against. It is now due as its own decision.

### The second qualification

The tree changed when the defect was fixed, so the first run was void and the
whole qualification was run again from clean — which CLAUDE.md requires, and
which is not the same as re-running until green.

| Preset | Build | Warnings | No-op rebuild | CTest | `Annotation_` tests found |
| --- | --- | --- | --- | --- | --- |
| `debug` | 0 | **0** | 0 compile, 0 link | **1915 / 1915** | 68 |
| `release` | 0 | **0** | 0 compile, 0 link | **1915 / 1915** | 68 |
| `debug-shared` | 0 | **0** | 0 compile, 0 link | **1915 / 1915** | 68 |

| Repeat | Selection | Runs | Result |
| --- | --- | --- | --- |
| `release` | 1914 tests | `until-fail:5` | 100% passed |
| `debug` | 1914 tests | `until-fail:5` | 100% passed |

Baseline was 1881 tests; this milestone adds **34**. Measured directly:
`[annotation]` 1307 assertions in 34 cases; `[drawing]` 5592 in 224.

`verify-harness.cmd` was run before the qualification and passes: it points the
harness at a preset that does not exist — a real failed stage, not a simulated
one — and requires a non-zero exit, getting 3.

### The qualified tree is the committed tree

Tree IDs from a scratch index, before the first build, after the last test run,
and again before the commit — identical in all three:

```text
apps             b32ce14e7be30b1c05432f740c2d25607be73b39
include          93950b011d58ca911b431935519d69333162294c
src              3d3f13dd1495707bf0f8bc2103c641ab239b74e1
tests            13697090c7749b84cb54f58425a1ae04a124fc42
examples         d0d2ae4277ba99b46ff1384725292deb3519c199
cmake            a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt   a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

The `include` tree differs from the FIRST run's freeze, which is correct: that
is the one file the defect fix touched, and the second run froze the fixed
tree.

## RESULT

```text
TASK:            P14-ANNO-001 -- Drawing annotations
IMPLEMENTATION:  notes, leaders, centrelines, centre marks, model-driven hole
                 callouts, surface-finish and datum foundations; ADR-016's
                 neutral primitives as far as they are needed; HoleCallout
                 gains extent and depth; toSheet exposes the view's own
                 projection
TESTS:           34 new; 1915/1915 in debug, release and debug-shared, each
                 from clean; 1914/1914 five times over in release and debug
VALIDATION:      text 3.5 mm and centre-mark arms 5.0 mm at five scales while
                 their positions move 30/15/60/3/150 mm; a leader's tip at a
                 hand-computed (170, 150); an arrowhead at sqrt(1 + 0.0625) x
                 3.5; a datum box 7 x 7; a centreline spanning 20 + 3 + 3 and
                 scaling only its span -- all computed in the tests
ADVERSARIAL:     19 questions, 1 defect found, 1 fixed, 1 regression test
WARNINGS:        0 in all three builds
DETERMINISM:     drawn six times, bit for bit; annotations in ID order
RESULT:          PASS, on the second qualification
EVIDENCE:        this directory
TODO:            P14-ANNO-001 -> [x]
CARRIED OPEN:    P14-HLR-001's assembly-to-assembly occlusion validation,
                 still blocked on P14-ASM-001 and untouched here
OPEN DECISION:   move build and test output off OneDrive; the filesystem
                 fault has now recurred in a second milestone
NEXT:            P14-TOL-001 -- tolerances / fits / GD&T foundation
```

## FILES

```text
qualification/qualify.cmd                      the harness (exit-code fix carried from P14-DIM-001)
qualification/verify-harness.cmd               its regression; run and passing
qualification/run-qualification.cmd            the entry point
qualification/qualification-times.txt          the passing run: every stage, exit code, tree IDs
qualification/qualification-times-voided.txt   the FAILED run's stage exits
qualification/build-debug-shared-voided.log    the dllimport error, as found
qualification/ctest-repeat-release-voided.log  the Permission denied recurrence
qualification/configure-*.log                  3 presets
qualification/clean-*.log                      3 presets
qualification/build-*.log                      3 presets, 0 warnings each
qualification/rebuild-*.log                    the no-op freshness proof
qualification/ctest-*.log                      3 presets, 1915/1915 each
qualification/ctest-repeat-*.log               release and debug, until-fail:5
```

## REVISION

First revision of the milestone; second qualification of it. The first
qualification failed on two stages and is preserved rather than discarded.
