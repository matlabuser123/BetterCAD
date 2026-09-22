# P14-VIEW-002 — Section / Detail / Auxiliary Views

```text
TASK:            P14-VIEW-002
BASELINE:        08bf558 (P14-VIEW-001, qualified)
RESULT:          PASS
```

## TASK

Section views (full, half, offset), detail and cropped views, auxiliary views,
the cutting-plane representation these need in the view model, and section
hatch generation — with the section geometry validated against closed forms
rather than against the routine that produces it.

## SCOPE

In scope: the `drawing` module's section geometry and the three new view kinds,
their validation, derivation, persistence and tests.

Out of scope, and NOT done: hidden-line removal and curved cut edges
(P14-HLR-001), section labels and cutting-plane annotation on the parent view
(P14-ANNO-001), drawing commands and undo (P14-CMD-001). Nothing was
implemented outside this milestone.

## BASELINE

`08bf558`, clean tree, `HEAD == origin/main`. P14-VIEW-001 qualified at
1763/1763 across `debug`, `release` and `debug-shared`.

An audit of the geometry the milestone would build on found:

| Question | Answer |
| --- | --- |
| What cuts a solid with a plane? | `geometry::splitBody(body, Frame3D, SplitKeep)`, qualified by P12-FEAT-002 |
| What enumerates edges? | `geometry::listEdges` → `EdgeInfo{curve, start, end, midpoint, …}` |
| Can a face give its boundary loops? | **No.** `FaceInfo` exposes no loops |
| What 2D geometry exists? | `Point2D` and `BoundingBox2D`, and nothing else |
| Is there polygon, clipping or hatch code? | None anywhere in the repository |

The third row decided the design: the outline of a cut face has to be **rebuilt
from the edges lying on the cutting plane**, because nothing can hand it over.
The fourth and fifth meant the 2D layer had to be written, which is why it was
written with its own tests against closed forms before any of it was used.

## ARCHITECTURE

No new ADR. The milestone is governed by ones already recorded, and adds
nothing they do not cover:

- **ADR-011** — intent canonical, projection derived. A `CuttingPlane` is
  stored; the cut solid, the cut faces, the hatch and the loops are recomputed
  on every request and never written to the file.
- **ADR-013** — a view normal points from the model toward the viewer. A
  section view's basis **is** its cutting plane's frame, so a section cannot be
  oriented against its own cut.
- **ADR-015** — `drawing` stays at layer 4; `Section.hpp` uses `core/geometry`
  and `core/math` only. No OCCT header is reached for.
- **ADR-017** — views are `DocumentObject`s. The three new kinds are the same
  object type (`"view"`), so no dispatch site was added.
- **ADR-018** — projection convention is sheet intent. `sheetDisplacement`
  generalises `placementStep` to an arbitrary direction, and a test asserts the
  two agree on all four orthogonal directions in both conventions.

Two decisions worth recording, neither architecturally significant enough for
an ADR:

**A view now carries an explicit `ViewKind`.** P14-VIEW-001 inferred base vs
projected from which optional fields were set. With five kinds that inference
becomes a guess, and a file with the wrong keys would have been read as
whichever kind matched last rather than refused. `"kind"` is written first and
always; `viewFromJson` dispatches on it.

**The removed side of a section is derived, never stored.** In a section the
material between the viewer and the plane goes. That follows from the view's
own direction, so storing it would create a second fact that could disagree
with the first. `keptSide(plane, viewBasis)` computes it.

## IMPLEMENTATION

New:

| File | Lines | What |
| --- | --- | --- |
| `include/bettercad/drawing/Section.hpp` | 226 | `SectionKind`, `SectionLeg`, `CuttingPlane`, `HatchSettings`, `SectionLoop`, `SectionGeometry`, and the 2D helpers |
| `src/drawing/Section.cpp` | 776 | the above, plus loop chaining, edge cancellation, hatch clipping |
| `tests/drawing/Section2dTests.cpp` | 255 | the 2D layer against closed forms |
| `tests/drawing/SectionGeometryTests.cpp` | 599 | section geometry against closed forms |
| `tests/drawing/ViewKindTests.cpp` | 747 | the three new view kinds |

Changed: `View.hpp`/`View.cpp` (the kinds and their validation), `Views.hpp`/
`Views.cpp` (derivation and geometry), `ViewJson.cpp` (persistence),
`ViewAssemblyTests.cpp`, `ViewTests.cpp`, two `CMakeLists.txt`.

### How a section is built

1. `legFrames` — the plane, plus one parallel plane per offset leg.
2. `cutBody` — for a full section, one `splitBody`. For half and offset, the
   removed region is assembled from half-spaces and subtracted, so the cut is
   composed of qualified primitives rather than a new intersection routine.
3. `listEdges` on the result; keep the edges lying on **any** leg plane, tested
   at both ends *and* the midpoint so an edge that merely touches the plane at
   a corner is not mistaken for one lying on it.
4. Project through the view basis, centre, scale, place.
5. Cancel coincident segments, chain the rest into closed loops, drop the
   collinear vertices a cancelled jog leaves behind.
6. Sort loops largest-area first; classify material and void by containment
   parity; canonicalise each loop's start vertex and winding.
7. Hatch by sweeping parallel lines and clipping on the sorted crossings, so
   alternate spans are material.

### Why offset sections need no development step

An offset section's legs are parallel, and the view looks along their shared
normal, so the legs already project into one plane. What is left is the jog:
both legs' cut faces end on it, and seen along the normal those two edges land
on each other. An edge with material on both sides is not a boundary, so the
pair cancels — which is exactly the rule that develops a stepped section, and
why ISO 128 draws no line at the jog. Step 5 is that rule, and
`SectionGeometry_AnOffsetSectionDevelopsIntoOnePlaneWithNoJogLine` is the
assertion.

## INDEPENDENT VALIDATION

Every expected number is computed in the test from the fixture's dimensions.
No expected value comes from the routine under test.

### The 2D layer

| Check | Reference |
| --- | --- |
| Polygon area | regular n-gon closed form `(1/2)·n·R²·sin(2π/n)`, n = 3…12 |
| Area independent of start vertex | the same polygon rotated |
| Point-in-polygon | concave L, vertex-height cases |
| Line/polygon crossings | shared vertices, parallel edges |

### Section areas

Fixtures: a 100 × 60 × 40 block, and the same block with a 20 × 20 duct through
it. Both are made of boxes, so every cut face is a rectangle and every expected
area is a product of two lengths.

| Case | Expected | Why |
| --- | --- | --- |
| Block, full section across depth | 4000 mm² | 100 × 40 |
| Ducted block, same plane | 3600 mm² | 4000 − 400, duct as an inner loop |
| Block at 1:2 | 1000 mm² | area scales as the **square** of the factor |
| Block at 2:1 | 16 000 mm² | likewise |
| Half section, split at mid-width | 2000 mm² | exactly half of 4000 |
| Half section, split at the near edge | 4000 mm² | must equal the full section |
| Half section through the duct | 1800 mm² | 2000 − 200, a notch, one loop, 8 corners |
| Offset, one jog, block | 4000 mm² | develops to one 4-corner loop, no jog line |
| Offset, one jog, ducted block | 3600 mm² | the void spans the jog and joins into one |
| Offset, three jogs | 4000 mm² | however many jogs, the same developed face |
| Offset with zero-offset legs | 3600 mm² | must equal the plain full section |
| Section view of the block, across width | 2400 mm² | 60 × 40, through the document API |
| Section of an assembly component | 2400 mm² | cut at the **solved** position |

The scale cases are there because a factor applied once per axis but squared
into the area, or applied twice to one axis, both survive a test that only
checks a width.

### Auxiliary views show true length

The decisive claim, as a number. The wedge fixture's slanted face runs from
(40, 0) to (0, 40), so its true width is the hypotenuse **40√2 = 56.5685 mm**.

| View | Extent across the face | |
| --- | --- | --- |
| Front | 40.000 mm | foreshortened |
| Auxiliary along the face normal | 56.5685 mm | true length, to 1e-9 relative |

### The placement rule

`sheetDisplacement` is asserted to reproduce `placementStep` exactly (1e-12) for
all four orthogonal directions in both conventions, using normals read off the
standard bases rather than restated. The general rule and the four-entry table
are therefore the same rule, not two.

### Detail views

Parent Front view at 1:1 spans x 150…250, y 130…170. A 2:1 detail of a 20 mm
circle at its top-right corner (250, 170), placed at (300, 100):

- the clipped top edge (230,170)…(250,170), 20 mm in the parent, is drawn
  (260,100)…(300,100) — **40 mm**, doubled exactly;
- nothing is drawn further than 40 mm from the placement, and something
  reaches exactly 40 mm, so the crop is at the circle and not inside it;
- uncropped, the same edge is kept whole: 100 mm parent → **200 mm** drawn.

## ADVERSARIAL REVIEW

Four defects found. All four fixed, each with a regression test.

**1. A curved cut edge was dropped silently.** The worst of the four. A round
hole through a sectioned wall would have vanished from the outline: the wall's
own rectangle still closes into a loop, the hole is not there to be a void, and
the section comes back *looking correct* with hatch drawn straight across
material that is not there. It would have passed every test in this milestone,
because every fixture is made of boxes. Now refused by name, pointing at
P14-HLR-001. Test: a cylinder cut across its axis.

**2. A cutting plane edge-on to its view reported the wrong thing.** A plane at
right angles to the direction of sight has no side between it and the viewer.
The symptom was "the cut edges did not close into any loop" — true, and
explaining nothing. Now says so directly.

**3. Loop start vertex and winding were accidents of the kernel's edge order.**
Free to differ between builds while the shape stayed identical. Loops now start
at their lowest corner and wind counter-clockwise for material, clockwise for a
void — which also leaves them in the winding a non-zero fill rule expects.

**4. `cutBody` built its slab bounds by passing `splitFrame` a `CuttingPlane`
that `validate()` would reject** (an offset plane carrying a split position). It
worked only because `splitFrame` does not validate. The frame maths moved to
its own helper taking a frame and a distance.

Also considered and found sound: failure atomicity (`createView` checks before
adding, so no ID is consumed by a failed call); parent-chain loops (refused at
the edit, and both derivations are depth-limited at 64); units (SI throughout,
`Area` from the shoelace sum); no hidden global state; no test-only behaviour;
no architectural boundary crossed.

## FAILURE PATHS

Each refused with a reason naming what failed and why:

- a plane that misses the body entirely;
- a half section whose split is past the model ("removes nothing");
- an edge-on cutting plane;
- a curved cut edge;
- hatch with a spacing of zero;
- a detail region nothing reaches;
- an auxiliary reference parallel to its normal;
- a definition carrying another kind's intent (six cases: a section with a
  detail region, a detail with a cutting plane, a section with no plane, a
  detail given a spacing, a section given a placement, a section naming its own
  source);
- a file naming an unknown view kind;
- a section of an assembly component with no solved transform — refused, not
  drawn at the origin.

## PERSISTENCE

`"kind"` is written first and always, and the reader dispatches on it. Each kind
writes the fields it has and no others: a section writes its plane and hatch, a
detail its region, an auxiliary its two directions. Placement is written for the
kinds placed directly and spacing for the kinds that align to a parent, so the
file never carries derived state that a later edit could contradict.

Round trips run the real cycle — create → save → destroy → load → regenerate →
compare — for all three new kinds including a two-leg offset plane, and compare
`ViewDefinition` by value, then the **derived** placement, then the **derived**
section geometry loop for loop, point for point, with the area and hatch count
compared exactly.

## DETERMINISM

- Loop order: sorted largest-area first, ties broken on the first point's x.
- Loop start vertex: the lowest corner.
- Loop winding: counter-clockwise for material, clockwise for a void.
- Hatch lines start half a spacing inside the bounds, so no line lands on a
  boundary where containment is deliberately undefined.
- No unordered iteration, wall-clock, random seed, thread scheduling or locale
  dependence anywhere in the new code.

Asserted bit for bit: two calls on the same input produce identical loop counts,
points, area and hatch, and the same after a save and load.

## KNOWN LIMITATIONS

Recorded, not worked around:

1. **Curved cut faces are refused**, not drawn. Sections of cylinders, fillets
   and holes with round cross-sections need the curve handling P14-HLR-001
   adds. Refusing is deliberate — see defect 1 above.
2. **Offset sections take parallel legs only.** An arbitrary swept cutting
   surface is not supported. This is the "foundation" the milestone asked for,
   and the restriction is validated (`legs` must step strictly forward).
3. **No cutting-plane line is drawn on the parent view**, and a section carries
   no label. The `CuttingPlane` is in the model and the section view names its
   parent, so both are available; drawing the line and the "A-A" label is
   annotation work (P14-ANNO-001).
4. **Section labels cannot be object names.** A view's name is a document
   object name, which must be an identifier, so `"A-A"` is rejected. The tests
   use `SectionAA`. A displayable label belongs with P14-ANNO-001.
5. **Hidden lines are not removed**, in any view kind. P14-HLR-001.
6. **A half or offset section can refuse a part whose material does not fill
   its bounding box where a leg cuts.** The region is assembled from
   half-spaces, and whether a plane crosses the body is decided from the
   bounds; where the bounds straddle a plane but the material does not,
   `splitBody` refuses and the refusal is passed on. The result is a
   diagnostic saying the plane does not cross the body, which is indirect but
   is a refusal and not a wrong drawing. Reachable only for a non-convex part
   sectioned on a leg that misses its material; every fixture here is convex,
   so it is recorded rather than demonstrated.

## REGRESSION

Three presets, each configured, fully cleaned, rebuilt with warnings as
errors, proved fresh, and only then tested. Every stage exited 0.

| Preset | Build | Warnings | No-op rebuild | CTest |
| --- | --- | --- | --- | --- |
| `debug` | 0 | **0** | 0 compile, 0 link | **1818 / 1818** |
| `release` | 0 | **0** | 0 compile, 0 link | **1818 / 1818** |
| `debug-shared` | 0 | **0** | 0 compile, 0 link | **1818 / 1818** |

The no-op rebuild is the fresh-binary proof: a second build of an already
built tree must have nothing to compile and nothing to link, so the binaries
CTest ran are provably the ones just built. This closes the stale-executable
failure P13-XFORM-001 recorded — and it earned its place again in this
milestone, when a link failure during development left an old executable in
place and the drawing tests "passed" against code that no longer existed.

Repeat runs, this milestone's subject and everything it could have disturbed
(section geometry, the view and sheet model, the splitting and boolean
primitives a cut is composed of, persistence and its dispatch sites, assembly
state, the layering checker, the CLI):

| Preset | Selection | Runs | Result |
| --- | --- | --- | --- |
| `release` | 785 tests | `until-fail:5` | 100% passed |
| `debug` | 785 tests | `until-fail:5` | 100% passed |

Baseline was 1763 tests; this milestone adds **55**. Measured directly:
`[section]` 834 assertions in 44 cases, `[drawing]` 3094 assertions in 141.

### The qualified tree is the committed tree

Git tree IDs taken from a scratch index before the first build and again after
the last test run, identical in both:

```text
apps             b32ce14e7be30b1c05432f740c2d25607be73b39
include          76e8adff59591601808c5c9f14e4552fe689e79e
src              99ec0fca974066e08cad0da91bc0acd064253a4b
tests            b8c26793a5a9365f48a5cb3f381f6eeb43307d74
examples         d0d2ae4277ba99b46ff1384725292deb3519c199
cmake            a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt   a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

Nothing that can affect the executable or the tests changed between the freeze
and the commit.

### One environment note, not a failure

`cli.new.unicode-path` fails under console code page 437 and passes under
65001. It is a property of the console the test is launched from, not of the
build: the qualification harness sets `chcp 65001` for exactly this reason,
and all three presets pass it. An earlier incremental run outside the harness
reproduced the 437 failure; the test was then re-run under 65001 on the same
binaries and passed.

## RESULT

```text
TASK:            P14-VIEW-002 — Section / detail / auxiliary views
IMPLEMENTATION:  full / half / offset sections; detail and cropped views;
                 auxiliary views; the cutting-plane representation in the
                 view model; hatch generation; the 2D geometry layer none of
                 it could be built without
TESTS:           55 new; 1818/1818 in debug, release and debug-shared, each
                 from clean; 785/785 five times over in release and debug
VALIDATION:      closed-form section areas (4000, 3600, 2400, 2000, 1800 mm2
                 and the scale cases), auxiliary true length 40*sqrt(2),
                 sheetDisplacement against placementStep, detail enlargement
                 by the ratio of two scales -- every expected value computed
                 in the test, none read back from BetterCAD
ADVERSARIAL:     4 defects found, 4 fixed, 4 regression tests. The serious
                 one: a curved cut edge was dropped silently, which would
                 have made a round hole vanish from a section and be hatched
                 over as solid material
WARNINGS:        0 in all three builds
DETERMINISM:     loops canonical in order, start vertex and winding; two
                 calls identical bit for bit; identical after save and load
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-VIEW-002 -> [x]
NEXT:            P14-HLR-001 -- hidden-line / visible-edge generation
```

## FILES

```text
qualification/qualify.cmd                the three-preset harness
qualification/run-qualification.cmd      its entry point
qualification/qualification-times.txt    every stage, its exit code, tree IDs
qualification/configure-*.log            3 presets
qualification/clean-*.log                3 presets
qualification/build-*.log                3 presets, 0 warnings each
qualification/rebuild-*.log              the no-op freshness proof
qualification/ctest-*.log                3 presets, 1818/1818 each
qualification/ctest-repeat-*.log         release and debug, until-fail:5
```
