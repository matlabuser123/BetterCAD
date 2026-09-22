# P14-HLR-001 — Visible / Hidden Line Generation

```text
TASK:            P14-HLR-001
BASELINE:        59e8e22 (P14-VIEW-002, qualified)
STATUS:          PASS
```

## SCOPE

Hidden-line removal for drawing views: visible and hidden edges told apart,
silhouettes of curved surfaces, a tangent-edge policy, a per-view hidden-line
toggle, and deterministic handling of lines that project onto each other.

Out of scope, and NOT done: dimensions and their references (P14-DIM-001),
annotation and section labels (P14-ANNO-001), a view that draws several
components at once (P14-ASM-001), export (P14-EXPORT-001). Nothing outside
this milestone was implemented.

## BASELINE

`59e8e22`, clean tree, `HEAD == origin/main`. P14-VIEW-002 qualified at
1818/1818 across `debug`, `release` and `debug-shared`.

The audit that decided the shape of the work:

| Question | Answer |
| --- | --- |
| Where may OCCT be reached? | `src/core/geometry/occt` — one directory, and the only one |
| Is an HLR toolkit available? | Yes: `TKHLR`, with both the exact and the polygonal algorithm |
| Was any of it used? | No. `TKHLR` was not even linked; it arrived as a transitive dependency of the renderer |
| Does an edge carry a stable identity? | No. ADR-012 already records that this codebase has no stable edge name |
| How were views drawn before? | Every edge projected, no visibility at all — P14-VIEW-001's recorded scope boundary, to be lifted by this milestone |
| Is a tangent edge detectable? | Yes, already: `EdgeInfo::faceAngle` is 0 where two faces join smoothly |

The fourth row is the constraint that shaped the contract, and the last row
meant the tangent-edge work had an independent reference to check against
rather than only the kernel's own opinion.

## HLR CONTRACT

Recorded as **ADR-019**, which also rejects the two alternatives.

A projected edge carries two independent facts, not one state:

```text
visibility   Visible | Hidden
kind         Sharp | Smooth | Outline | Sewn
```

The brief lists seven possible states; they collapse onto these two axes,
which is smaller and says more. The mapping, in full:

| Brief's state | Here |
| --- | --- |
| Visible | `{Visible, Sharp}` |
| Hidden | `{Hidden, Sharp}` |
| Silhouette | `kind == Outline`, either visibility |
| TangentVisible | `{Visible, Smooth}` |
| TangentHidden | `{Hidden, Smooth}` |
| SuppressedByPolicy | not a state of an edge — the view omits it and reports `suppressed` |
| CoincidentMerged | not a state of an edge — the view merges and reports `merged` |

The last two are outcomes of a decision rather than properties of geometry.
Putting them in the same enum as "hidden" would make an edge's class depend on
which view was asking.

What is derived, and what is not:

```text
model topology and references   source, and untouched
projected curve                 derived, recomputed every request
visibility class                derived
render policy                   per-view INTENT, stored with the view
```

Nothing hidden-line removal produces is persisted. The only thing this
milestone adds to the file is the view's two settings.

## DEPTH CONVENTION

The viewer stands on the side the basis's normal points to and looks back
along it (ADR-013), so the nearest material hides the rest. That is asserted,
not assumed, and by the one test that cannot pass by accident: the same block
with the same pocket, cut once into the near face and once into the far one.

| Pocket in | Its four lines |
| --- | --- |
| the near face | **visible**, and 4 visible lines lie inside the outline |
| the far face | **hidden**, and 0 visible lines lie inside the outline |

If the sign were inverted the two answers would simply swap, and every drawing
BetterCAD produced would be inside out.

## VISIBLE, HIDDEN AND SILHOUETTE EDGES

Validated against solids whose drawn lines can be written down. Every expected
number is computed in the test; none is read back from the routine under test.

| Solid and view | Claim | Result |
| --- | --- | --- |
| 100 x 60 x 40 box, front | 12 model edges; 4 run along the line of sight and draw nothing; 8 drawn | 8, as 4 visible + 4 hidden |
| — | the outline is the 100 x 40 rectangle, and the drawn corners are `Frame3D::toLocal` of the model corners | exact |
| — | total drawn length = the rectangle twice = 560 mm | exact |
| cylinder r20 h80, across the axis, seam facing the viewer | **two** silhouette generators, at x = +/-20, each 80 long | 2 Outline, exact |
| — | the seam is reported as a seam, not as an edge of the shape | 1 Sewn, at x = 0 |
| cylinder, along the axis | the rim IS the outline; near visible, far hidden; each 2*pi*r | 2 circles, 125.664 mm |
| sphere r25 | silhouette is a great circle: every point exactly 25 from the centre, total 2*pi*r | 157.08 mm, exact |
| — | nothing of a sphere hides anything | 0 hidden |
| stepped shaft r20 x 40 then r10 x 30 | a generator pair per step, at x = +/-20 (40 long) and +/-10 (30 long) | all four found |
| — | the shoulder draws the big rim visible and the small one hidden inside it | exact |

A silhouette is **not** a model edge. A cylinder seen across its axis has no
topological edge along its length at all, and the two lines a drawing shows
there are computed. That is why the cylinder cases are the ones that matter.

### One kernel behaviour, recorded rather than smoothed over

`makeCylinder` puts the surface's seam along +X. Seen from -Y that seam lies
**exactly on** the right-hand silhouette, so the kernel has one curve where it
otherwise has two and reports it as the model edge it is — one `Outline` and
one `Sharp` rather than two `Outline`s. What a drawing needs is unchanged: two
vertical lines, r either side of the axis, each the full height, and that is
what the test asserts. Seen from +X, where the seam is not on a silhouette,
the same cylinder gives two `Outline` generators and one `Sewn` seam.

## TANGENT POLICY

A fillet runs into the face it blends with no crease. The geometric fact and
the drawing decision are kept apart: the kernel says `Smooth`, and the view's
`TangentEdgePolicy` says whether to draw it.

The classification is checked against an independent reference rather than
against itself: a 60 x 40 x 30 block filleted r = 8 has **two** edges whose
`EdgeInfo::faceAngle` is zero — the model's own statement that the faces meet
at no angle — and the fillet's tangent line with the front face draws at
x = 60 - 8 = 52 for the block's full height, which is where the `Smooth` edge
is found.

| Policy | Smooth edges drawn |
| --- | --- |
| `Show` | at least 1, including the one at the fillet |
| `Hide` | 0, and the rest of the drawing is unchanged |

## HIDDEN-LINE TOGGLE

Per view, stored, and presentation only. Asserted to change what is drawn and
nothing else:

```text
ViewId              unchanged
source              unchanged   (effectiveSource compared)
projection basis    unchanged   (effectiveBasis compared)
scale               unchanged   (effectiveScale compared)
placement           unchanged   (effectivePlacement compared)
bounds              unchanged   <- see below
every drawn line    identical where both views draw it
```

The bounds are the one that needed care. A view is centred on what it **could**
draw, taken before anything is suppressed — not on what survives. Centring on
the survivors would move the drawing on the sheet when hidden detail was
switched off, which is a display setting silently moving geometry. There is a
test for exactly that.

## OVERLAP AND COINCIDENT HANDLING

The problem is not an edge case; it is every box in the system. A box seen
square-on returns four visible edges and four hidden edges at **identical**
coordinates, because its far face projects exactly onto its near one. A
cylinder's rims do the same. A rear pocket returns each of its lines twice,
sometimes with the endpoints reversed.

Two drawn curves are the same curve when their ends match in either direction,
their midpoints match, and their lengths match. Ends alone are not enough: an
arc and its chord share them, and so do the two halves of a circle.

Precedence when they do:

```text
visible beats hidden      ISO 128 line precedence
sharp beats outline       a silhouette falling on a real corner IS that corner
outline beats smooth      a silhouette bounds the shape; a tangent line does not
smooth beats sewn         a seam is an artefact of parameterisation
```

Merging happens **before** suppression and is unconditional, so the merged
count does not depend on the toggle. A box therefore draws 4 lines with
`merged == 4` and `suppressed == 0`.

Provenance is not preserved by merging, because there is none to preserve.
ADR-012 records that this codebase has no stable edge name, and the kernel's
hidden-line output does not carry one back either. A dimension therefore
references the MODEL and projects it; it never references a drawn line.
P14-DIM-001 inherits that, and it is a consequence of ADR-012 rather than a
new limitation introduced here.

## ADVANCED-VIEW COMPATIBILITY

**Order: a section is cut first, then classified.** Getting this backwards
would be invisible in a screenshot and wrong in every drawing — a section that
classified the uncut solid would hide the very faces the section exists to
show. It is asserted directly: the pocket in the far face has all four lines
hidden in a plain front view, and the same four become visible in a section
cut through it. Nothing else about the view changes.

**A detail applies its own settings, not its parent's.** A detail asks its
parent for everything the parent *could* draw and then decides for itself. A
detail that could only narrow its parent's choices would not have a per-view
toggle at all; the test is a parent with hidden lines off and a detail of it
with them on, which shows them.

**An auxiliary view classifies through its own direction.** Its outline
measures 113.137 mm = (100 + 60)/sqrt(2) across its own up axis and 40 mm
across its right — neither of which belongs to any standard view of that
block.

## ASSEMBLY OCCLUSION

What occludes what is decided in model space, so it is decided by where the
solver put the component and never by where its placement asked it to go
(ADR-005).

The same part is placed twice, once as authored and once turned half a turn.
Its pocket is in the face at y = 60:

| Component | Pocket | Lines inside the outline |
| --- | --- | --- |
| upright | away from the viewer | 4 hidden, 0 visible |
| turned 180 degrees about Z | toward the viewer | 4 visible, 0 hidden |

The outline is identical in both, so the difference is occlusion and not a
different projection. A component with no solved transform is refused rather
than drawn at the origin, and a **suppressed** component is refused the same
way — the solver gives it no transform, so it cannot leak into a drawing. That
is the right outcome for an indirect reason, and it is recorded as such rather
than presented as a view that understands suppression.

**Occlusion BETWEEN components is not reachable and was not tested.** A view
draws one component, which is P14-VIEW-001's recorded boundary and is
P14-ASM-001's to lift. The checklist item is met for occlusion at the solved
transform and is explicitly not met for one component hiding another; see
KNOWN LIMITATIONS.

## TOLERANCE POLICY

Every tolerance has a reason and lives with the code that uses it.

| Figure | Where | Why |
| --- | --- | --- |
| 1e-7 mm | a projected curve too short to draw | an edge along the line of sight projects to a point; this is the figure the geometry module already uses for two points coinciding |
| 1e-7 mm | two drawn curves are the same curve | coincident pairs come from the same model edges projected the same way, so they land on each other exactly, not approximately |
| 0.01 mm | polyline sampling deflection | in MODEL units; deterministic for a given curve |
| 1e-6 mm | test comparisons of kernel output | hidden-line removal intersects surfaces and measures arc length numerically — geometric accumulation, not the well-conditioned arithmetic 1e-12 is for |
| exact | counts, classifications, orderings | integers and enums, compared as such |

The merge tolerance is applied in **view-plane units, before any scale**, so
whether two lines are the same line does not depend on how big the view is
drawn.

## CURVES ARE CARRIED AS CURVES

A full circle's `start` and `end` are the same point. A renderer joining them
with a straight line would draw nothing at all, so every edge carries a
polyline sampled to a documented deflection, alongside the exact description
for the milestones that need a circle to be a circle. A straight edge's
polyline is its two endpoints and nothing more, which is asserted.

## FAILURE PATHS

Each refused with a reason, and none leaving a partial drawing:

- an empty body ("nothing to draw");
- a source that produced no body — and the next good call is unaffected,
  because nothing is cached and there is no previous result to go stale;
- a component with no solved transform;
- a suppressed component;
- a file naming an unknown tangent-edge policy;
- a kernel raise, converted to a diagnostic by the existing guard rather than
  crossing the API as an exception.

## DETERMINISM

The kernel's traversal order carries no meaning, so the result is sorted on
what it is: kind, then visibility, then the start point, then the end point,
then length. The policy pass preserves that order. Asserted bit for bit for
repeated calls, and across a save and load.

## ADVERSARIAL REVIEW

One defect found, fixed, with a regression test.

**A curve lying wholly inside a detail region came back as a fan of two-point
fragments instead of one line.** A piece that was not clipped had its endpoint
recomputed as `a + 1.0 * (b - a)`, which is the same number in arithmetic and
not always the same double, so consecutive pieces failed to join. It draws the
same, and it is not the same thing: every later milestone that walks these
edges would see dozens of lines where there is one. Fixed by keeping the
endpoint a piece already had. Test: a rod's rim circle detailed at 2:1 keeps
exactly one line with the same number of points as its parent.

The fourteen questions the brief lists, answered:

| Question | Answer |
| --- | --- |
| Can a rear edge be classified visible? | No — the near/far pocket pair proves the sign. Where a rear edge is *coincident* with a visible one the visible one wins, which is correct: the line IS visible |
| Can a front edge disappear through depth-sign inversion? | No, same test, both directions |
| Can a silhouette be missed for not being a model edge? | No — the cylinder and sphere cases have no model edge there at all |
| Can tangent edges flip class across builds? | Not within a build; the three-preset run below is the cross-build evidence |
| Can two coincident edges duplicate visually? | No — a box draws 4 lines, `merged == 4` |
| Can merging lose source provenance? | There is none to lose (ADR-012, ADR-019). Stated, not worked around |
| Can hidden-line OFF alter geometry? | No — basis, scale, placement, source, bounds and every surviving line all compared |
| Can a suppressed component leak in? | No — verified, not assumed: it has no solved transform and is refused |
| Can a fully occluded component still draw? | Not reachable — one component per view (P14-ASM-001) |
| Can a section classify pre-section geometry? | No — asserted directly, and it is the test that would catch a reordering |
| Can a failed run leave stale edges visible? | No — nothing is cached; asserted by failing a call between two good ones |
| Can Debug and Release order edges differently? | The canonical sort is on values, never addresses; the three-preset run is the evidence |
| Can tolerance changes alter results nondeterministically? | Tolerances are named constants with recorded reasons, not scattered literals |
| Did this start dimension semantics beyond scope? | No. No dimension or reference code was written |

## KNOWN LIMITATIONS

Recorded, not worked around.

1. **Occlusion between components is not supported.** A view draws one
   component. P14-ASM-001.
2. **Suppression is not consulted by the view.** A suppressed component is
   refused because the solver gives it no transform, not because the view
   understands suppression. The outcome is right; the reason is indirect.
3. **The polyline sampling deflection is in model units and is not aware of
   the drawing scale.** At 10:1 a curve is sampled more coarsely on paper than
   at 1:1. The exact curve description is carried alongside, so a scale-aware
   renderer can do better without new geometry.
4. **A cropped curve's midpoint is a sampled point on it, not an exact one.**
   Cropping a curve at a detail boundary produces a true arc; its ends are
   exact and its midpoint is the middle of the surviving polyline.
5. **Merging is quadratic in the number of drawn lines.** Fine at the sizes
   tested; it would need an index on a large assembly drawing.
6. **Exact hidden-line removal is the slower algorithm** (ADR-019). Nothing
   caches, because ADR-011 and ADR-014 make drawing geometry derived and built
   on demand; a cache needs an invalidation rule decided deliberately.
7. **Iso-parametric lines are not collected.** They are a surface-display aid,
   not a drawing.

## IMPLEMENTATION

New:

| File | Lines | What |
| --- | --- | --- |
| `include/bettercad/core/geometry/HiddenLine.hpp` | 144 | the contract: `EdgeVisibility`, `ProjectedEdgeKind`, `ProjectedEdge`, `HiddenLineDrawing` |
| `src/core/geometry/occt/OcctHiddenLine.cpp` | 231 | the kernel call, the eight result sets mapped onto two axes, the canonical sort, curve sampling |
| `include/bettercad/drawing/HiddenLine.hpp` | 116 | `TangentEdgePolicy`, `HiddenLineSettings`, `DrawnEdge`, the policy pass |
| `src/drawing/HiddenLine.cpp` | 147 | merging with ISO 128 precedence, then suppression |
| `tests/core/geometry/HiddenLineTests.cpp` | 490 | the geometry, against solids whose drawn lines can be written down |
| `tests/drawing/HiddenLineViewTests.cpp` | 565 | what a view does with it |
| `docs/architecture/decisions/ADR-019-...md` | — | exact hidden-line removal, and the two rejected alternatives |

Changed: `View.hpp`/`View.cpp` (the settings are intent), `Views.hpp`/`Views.cpp`
(the projection became a classified drawing; the detail crop works on
polylines), `ViewJson.cpp` (persistence), `ViewTests.cpp`,
`ViewAssemblyTests.cpp`, `ViewKindTests.cpp`, three `CMakeLists.txt`.

`TKHLR` is now linked by `bettercad_geometry`. It was present only as a
transitive dependency of the renderer before.

### One pipeline, not two

`projectedGeometry` classifies; there is no second function that draws a view
unclassified. P14-VIEW-001 recorded its unclassified projection as a scope
boundary for this milestone to lift, and lifting it is what happened: a box
front view now draws **4** lines rather than 12, because its far face lands on
its near face and its four depth edges point at the viewer. Thirteen call
sites moved from `segments` to `edges`.

## REGRESSION

Three presets, each configured, fully cleaned, rebuilt with warnings as
errors, proved fresh, and only then tested. Every stage exited 0.

| Preset | Build | Warnings | No-op rebuild | CTest | `HiddenLine` tests discovered |
| --- | --- | --- | --- | --- | --- |
| `debug` | 0 | **0** | 0 compile, 0 link | **1847 / 1847** | yes |
| `release` | 0 | **0** | 0 compile, 0 link | **1847 / 1847** | yes |
| `debug-shared` | 0 | **0** | 0 compile, 0 link | **1847 / 1847** | yes |

The no-op rebuild is the fresh-binary proof: a second build of an already
built tree must have nothing to compile and nothing to link, so the binaries
CTest ran are provably the ones just built. It earned its keep twice during
this milestone's development, when a link failure and then a compile error
each left an older executable in place and a test run "passed" against code
that no longer existed.

Repeat runs, this milestone's subject and everything it could have disturbed
(hidden-line classification and the view and sheet model it runs inside,
section and detail geometry, the edge and fillet queries it is validated
against, persistence and its dispatch sites, assembly state, the layering
checker, the CLI):

| Preset | Selection | Runs | Result |
| --- | --- | --- | --- |
| `release` | 877 tests | `until-fail:5` | 100% passed |
| `debug` | 877 tests | `until-fail:5` | 100% passed |

Baseline was 1818 tests; this milestone adds **29**. Measured directly:
`[hlr]` 6948 assertions in 29 cases, `[drawing]` 3354 assertions in 156.

`architecture.layering` passes in all three presets, which is what confirms
`drawing` reaching `core/geometry/HiddenLine.hpp` is within the layering and
that OCCT stayed inside its adapter.

### The qualified tree is the committed tree

Git tree IDs taken from a scratch index before the first build and again after
the last test run, identical in both:

```text
apps             b32ce14e7be30b1c05432f740c2d25607be73b39
include          f46ef8c3775600002b7606a987372b98f2a18600
src              55e83eb6e4c5d2d97a740b691fe0fb5f5ed0a255
tests            5c64a0cc7544753598e7bc8efc1c639c745315e7
examples         d0d2ae4277ba99b46ff1384725292deb3519c199
cmake            a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt   a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

### One environment note, not a failure

`cli.new.unicode-path` fails under console code page 437 and passes under
65001. It is a property of the console the test is launched from, not of the
build: the harness sets `chcp 65001`, and all three presets pass it. An
incremental run outside the harness reproduced the 437 failure; the test was
then re-run under 65001 on the same binaries and passed.

## RESULT

```text
TASK:            P14-HLR-001 -- Visible / hidden line generation
IMPLEMENTATION:  ADR-019; the hidden-line contract and its kernel adapter;
                 merging with ISO 128 line precedence; the tangent-edge
                 policy; the per-view hidden-line toggle; curve sampling;
                 the view pipeline reworked to classify rather than project
TESTS:           29 new; 1847/1847 in debug, release and debug-shared, each
                 from clean; 877/877 five times over in release and debug
VALIDATION:      sphere silhouette 2*pi*r; cylinder generators at +/-r of the
                 full height; a generator pair per step of a stepped shaft;
                 box outline corners equal Frame3D::toLocal of the model
                 corners; the near/far pocket pair for the depth sign; the
                 model's own faceAngle for tangency -- all computed in the
                 tests
ADVERSARIAL:     14 questions, 1 defect found, 1 fixed, 1 regression test
WARNINGS:        0 in all three builds
DETERMINISM:     canonical order by kind, visibility and position; identical
                 bit for bit on repeat and across save/load
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-HLR-001 -> [x], except assembly occlusion BETWEEN
                 components, which a single-component view cannot reach
NEXT:            P14-DIM-001 -- drawing dimensions
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
qualification/ctest-*.log                3 presets, 1847/1847 each
qualification/ctest-repeat-*.log         release and debug, until-fail:5
```

## REVISION

First revision. No earlier qualification of this milestone was run or voided.
