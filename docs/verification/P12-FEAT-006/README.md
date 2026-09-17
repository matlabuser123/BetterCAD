# P12-FEAT-006 — Variable-Radius Fillet Verification

## Status

**PASS** for the scope decided on 2026-09-18: variable-radius fillets of
straight edges, with a radius law BetterCAD computes itself and checks every
result against.

- **Deferred, not implemented:** setback distances and selectable corner
  transitions. OCCT 8.0.1 has no input for either, and both would need a
  surface-patch subsystem that P12 does not authorize
  ([investigation](investigation/README.md)). `TODO.md` lists them as
  deferred (unticked) under the milestone and under *Advanced surface
  modeling*.
- **The feature.** A `variable_fillet` rounds straight edges between two
  planar faces. Each edge carries radius stations: a position from 0 to 1
  and a radius, literal or driven by a parameter.
  - The radius is the stations' exactly at the stations.
  - Between stations it follows the kernel's clamped cubic spline, which
    BetterCAD computes, and it never leaves the range of the two stations
    around it. Stations whose law would leave it are refused.
  - The kernel's result is kept only if its law equals BetterCAD's and its
    surface lies on that law's rolling-ball sections.

Debug, Release and Debug-shared each passed **1036/1036** tests
with **0 compiler warnings** from a verified clean rebuild. Every test of
the P11 qualification and of `P12-FEAT-005` still passes in all three. Every
value the existing tests measure is unchanged, bit for bit.

Date: 2026-09-18. `main` was at `d14c817` (the blocker record) before this
milestone.

## History and Layout

1. **Investigation.** OCCT's fillet interface, its sources and two probes
   were assessed; the milestone was recorded as blocked at `d14c817`. That
   record is now [investigation/README.md](investigation/README.md),
   unchanged apart from a note and its probe paths.
2. **Scope decision (2026-09-18), options A and C.** Implement the
   variable-radius fillets that can be verified; defer setbacks and corner
   transitions. `TODO.md`, `ROADMAP.md` and `README.md` were updated to say
   so.
3. **Contract probes.** Before any product code, the exact kernel call
   sequence and the checks were tried on OCCT (below).
4. **Implementation and qualification:** this file, and
   [qualification/](qualification/).

| Path | What |
| --- | --- |
| `README.md` | This record: design, validation, qualification, limitations. |
| `investigation/README.md` | The capability assessment that blocked the milestone. |
| `kernel-probe/` | Probes and logs. The investigation's: `fillet_probe`, `variable_radius_probe`, `occt-source-excerpts.txt`, `api-search.log`. The implementation's: `station_contract_probe`, `face_area_probe`, `face_count_probe`. |
| `qualification/` | The qualification scripts, logs, comparisons and measured values, and the spine-direction diagnostic. |

## Scope

The deliverables are the ones `TODO.md` lists for `P12-FEAT-006` after
the scope decision.

| Deliverable | Status | Main evidence (test cases) |
| --- | --- | --- |
| `geometry::variableFilletEdges`: stations; straight edges between planes; smooth continuations and edges meeting another edge of the request refused | IMPLEMENTED | `VariableFillet_RoundsIsolatedStraightEdges`; `VariableFillet_RefusesEdgesItCannotVerify`; `VariableFillet_RequestsAreValidated` |
| The radius law computed by BetterCAD; laws that leave their stations' range refused; the fit check at the largest radius | IMPLEMENTED | `VariableFillet_RequestsAreValidated`; `VariableFillet_RefusesEdgesItCannotVerify` |
| Result verification (completion, validity, solids, self-intersection, one fillet face, the kernel's law, sections and contact lines) | IMPLEMENTED | every successful build in the tests passes it; `kernel-probe/station-contract-probe.log` |
| `VariableFilletFeature` (target, edges with stations, literal or driven radii); consumes its target, carries its names | IMPLEMENTED | `VariableFilletFeature_FollowsItsBlockAndRadii`; `VariableFilletFeature_DefinitionsAreValidated` |
| Dependencies, validation, undo/redo, save/load (new type; constant fillets unchanged), CLI; patterns and mirrors refuse it | IMPLEMENTED | `VariableFilletFeature_CreationAndEditsAreUndoable`; `VariableFilletFeature_RegeneratesDeterministically`; `VariableFilletFeature_FailuresAreStructuredAndAtomic`; `VariableFilletFile_*`; `VariableFilletCli_*`; `cli.info.tapered-block`, `cli.validate.tapered-block`, `cli.export-step.tapered-block` |
| Evidence | this directory | — |
| Setback controls | **DEFERRED**, not implemented | [investigation](investigation/README.md) |
| Selectable corner-transition controls | **DEFERRED**, not implemented | [investigation](investigation/README.md) |

**Also not in scope, and refused with a diagnostic:**

- curved edges;
- edges next to a non-planar face;
- edges that continue smoothly into others (tangent chains);
- edges that meet another edge of the same fillet;
- patterns and mirrors of a variable-radius fillet;
- positions driven by parameters (positions are literal numbers).

## The Contract

```text
geometry::RadiusStation            position u in [0, 1], radius
  u = 0                            the end that comes first along the edge's line in its canonical
                                   direction (EdgeSignature::direction); u = 1 the other end. The
                                   canonical direction belongs to the line, so no regeneration
                                   reverses the stations
VariableFilletEdge                 a line signature and its stations: at least two, first at 0,
                                   last at 1, positions increasing by at least 1e-6
the law                            the clamped cubic spline in u with knots -1/2, the stations, 3/2,
                                   through r_first at -1/2 and r_last at 3/2, zero slope at both
                                   ends (what OCCT 8.0.1 builds for an edge that meets no other
                                   blend: investigation, items 5a-10 of occt-source-excerpts.txt).
                                   Two stations a, b: r(u) = a + (b - a) (u - 4/7 u (1 - u)(1 - 2 u))
RadiusLaw                          src/core/geometry, kernel-free: the spline (second-derivative
                                   form), exact extremes per span (the slope's quadratic)
validate(request)                  InvalidArgument: no edges; invalid, duplicate or curved
                                   signatures; station count, positions, radii; radii 0 < spread
                                   < 1e-6 mm (the kernel merges radii within 1e-7 mm); a law that
                                   leaves [min, max] of the two stations around it (by more than
                                   1e-12 relative), with the radius it reaches and where
radiusAt(stations, u)              the law, for tools
variableFilletEdges(body, request) occt/OcctVariableFillet.cpp:
  refused before the kernel        NotFound: no matching edge. FailedPrecondition: ambiguous; not
                                   between two faces; a face not planar; faces joining smoothly or
                                   folding back; the edge meets an earlier edge of the request;
                                   the kernel's contour has more than this edge (a smooth
                                   continuation); stations under 1e-6 mm apart on the edge; the
                                   shared fit check fails at the law's largest radius
  the build                        Add(E) without a radius; FirstVertex(contour) says which way the
                                   spine runs; the stations go to SetRadius(UandR, contour, 1) in
                                   that direction (1 - u, reversed, against it). Add(Law_Function)
                                   and SetLaw are never used
  the result is kept only if       the kernel reports completion; one valid solid per input solid
                                   with finite positive volume and area; no self-intersection;
                                   exactly one fillet face generated from each edge; the kernel's
                                   law equals BetterCAD's within 1e-9 mm at 65 points, over bounds
                                   [-L/2, 3L/2] within 1e-9 L (IsConstant/Radius for equal
                                   stations); 17 x 17 surface samples on the rolling-ball arc of
                                   the law's radius within 1e-7 mm; boundary samples on either
                                   face at r tan(g/2) from the edge within 1e-7 mm, on both faces.
                                   Otherwise Internal, and nothing is kept
features::VariableFilletFeature    type variable_fillet: target, edges (signature + stations, each
                                   radius literal or driven); depends on the target and each radius
                                   parameter once; validate() checks the law when all radii of an
                                   edge are literal, regeneration otherwise
resolveVariableFilletRequest       driven radii replaced by their values (NotFound,
                                   DimensionMismatch)
Validation                         a radius parameter that is not a length
PatternSupport                     "a <pattern> cannot repeat a variable-radius fillet"
VariableFilletJson                 "target"; "edges": [{"edge": <edge reference>, "stations":
                                   [{"position", "radius", "radius_parameter"?}]}]
CLI info                           "target Block, 2 edges: low at 0, high at 1; 2 mm at 0, 4 mm at
                                   0.3, 7 mm at 1"
Commands                           Create/ModifyVariableFilletCommand
```

**Tolerances and their reasons.**

- **The kernel's law, 1e-9 mm.** It matched BetterCAD's to 3e-15 mm in
  every probed case, so anything beyond rounding means a different law.
- **Sections and contact lines, 1e-7 mm.** This is the kernel's precision
  (`Precision::Confusion`). The probes measured 4e-14 mm.
- **Station spacing, 1e-6 mm.** The kernel merges stations closer than its
  1e-7 mm precision; ten times that is required.

## Contract Probes

Run on OCCT 8.0.1 before the product code was written. Build commands
are at the top of each log.

- **`kernel-probe/station_contract_probe.cpp`** (8 cases, each in its own
  process):
  - **Spine direction.** The first vertex of the kernel's spine agrees with
    the canonical start on all 12 edges of a kernel box, and adding the edge
    reversed does not turn the spine. A prism edge built running -y has a
    reversed spine, and the stations handed over as 1 - u match.
  - **Law.** The kernel's law matches the spline within 2.7e-15 mm.
  - **Geometry.** Sections are within 3.9e-14 mm of the rolling-ball arcs,
    contact lines within 4.2e-14 mm.
  - **Volume.** The volume change is within 4.1e-9 of (tan(g/2) - g/2)
    times the integral of r².
  - **Other edges.** The same holds on faces at 120° (g = 60°) and on a
    concave edge, where the fillet adds material. Equal stations give the
    kernel's constant radius.
  - **Extension.** The law's bounds are [-L/2, 3L/2] each time.
- **`kernel-probe/face_area_probe.cpp`.** The top face of a block whose
  back edge is filleted (the tests' case) measures 5504.796273347 mm² with
  the kernel's default surface integration, which is what
  `geometry::listFaces()` reports. That is 1.85e-6 off the exact 6000 mm²
  less the strip, 5504.786097472 mm²; the same integration with a 1e-10
  tolerance is exact to 1.7e-16.
- **`kernel-probe/face_count_probe.cpp`.** Edges from 1 to 2000 mm with 2
  to 25 stations always give one generated fillet face: the one-face check
  does not refuse valid results there.
- **Bounds.** The kernel's bounds of B-spline faces and edges are padded by
  its precision whatever tolerance is asked for
  (`OCCT-8_0_1/src/ModelingData/TKGeomBase/GeomBndLib/GeomBndLib_BSplineSurface.cxx`,
  line 370: `aBox.Enlarge(anEps)` with
  `anEps = max(theTol, Precision::Confusion())`). Bodies with these fillets
  report bounds up to 1e-7 mm loose; the tests measured exactly that.
- **`qualification/spine-direction-diagnostic.log`.** A temporary print,
  removed before the qualification, showed that the tests round edges of
  both directions. The asymmetric stations on the reversed ones pass the
  law check, which a wrong mapping would fail.

## Independent Validation

The tests (`tests/support/VariableFilletModels.hpp`) compute every expected
value without BetterCAD's law code.

- **The law.** `ReferenceLaw` builds the spline in Hermite form from its
  knot slopes, a different linear system from the product's
  second-derivative form. For two stations the tests also use the
  hand-derived closed form. φ was checked in exact rational arithmetic
  (φ(1/4) = 11/56, among others), and `radiusAt()` matches both references
  within 1e-12 mm.
- **Overshoot.** The refusal messages are compared with the reference
  law's extremes, found by a 20000-step scan and golden-section search. The
  investigation's case (stations 5, 5, 36, 36 at 0, 0.4, 0.6, 1) was
  solved exactly in rational arithmetic: -3.2348900088144264 at
  0.24711258399416494, and 44.234890008814426 at 0.75288741600583506.
- **Sections.** A fillet of radius r between faces whose outward normals
  are g apart:
  - takes r²(tan(g/2) - g/2) from a convex corner, or adds it to a concave
    one;
  - has that area's centroid on the bisector at r c(g), from the kite and
    sector centroids;
  - touches each face r tan(g/2) from the edge.

  Volume, centre and the strip each face loses are integrated along the
  edge exactly: 6-point Gauss–Legendre per station span, exact to degree
  11, for integrands of degree 10 at most.
- **Cases:**
  - a block's top front edge (spine along +x) and top back edge (spine
    against it), with rising, falling, constant, 3-, 4- and 5-station laws,
    one at a time and together;
  - an edge between faces at 120° (a hexagonal prism), including where the
    crest of the fillet bounds the body;
  - the inside edge of an L prism (concave);
  - the tapered block model, while `low`, `high` and `width` change.
- **A third implementation.** The CLI's volume of the example,
  98709.906 mm³, agrees with 98709.906469 mm³ from exact rational
  arithmetic (the moment-form spline in fractions, 7-point Newton–Cotes per
  span).

`qualification/deviations.txt` (`deviations.py` over
`qualification/reference-values-release.txt`) lists the largest
deviations:

| Check | Tolerance | Checks | Largest deviation measured (Release) |
| --- | --- | --- | --- |
| Volumes of variable-radius results: the tapered block and its edits | 1e-9 rel | 10 | 6.7e-12 |
| Volumes of variable-radius results: blocks, the hexagonal and L prisms | 1e-9 rel | 12 | 3.5e-12 |
| Volume and centre of the equal-station (constant) result | 1e-12 rel; 1e-9 mm | 1; 3 | 1.2e-16; 1.1e-14 mm |
| Centres of mass | 1e-7 mm | 65 | 1.8e-10 mm |
| Bounds (see the kernel's padding above) | 2e-7 mm | 124 | 1.0e-7 mm |
| Areas of the planes the fillets border (the kernel's default integration) | 1e-5 rel | 18 | 4.7e-6 |
| `radiusAt()` against the references | 1e-12 mm | 156 | 2.7e-15 mm |
| φ(1/4) against 11/56 | 1e-15 | 1 | 0 |
| The reference law's extremes against the exact ones: radius; position | 1e-9 mm; 1e-6 | 2; 2 | 1.8e-15 mm; 2.0e-9 |
| STEP read-back volume; bounds | 1e-9 rel; 2e-7 mm | 1; 3 | 7.3e-11; 1.0e-7 mm |

The volumes are far closer than the 1e-9 the probes suggested: the body's
volume is the reference, and the fillets are a small part of it. The
bounds are the exact ones plus the kernel's 1e-7 mm padding in every case,
the hexagon's crest included (29.3811979 against 29.3811978).

**Following the parameters** (`VariableFilletFeature_FollowsItsBlockAndRadii`):

- `low` 3 → 5 → 15 and `high` 8 → 12 → 2 regenerate only the fillet, and
  undo restores it bit for bit.
- `width` 100 → 150 → 60 stretches the law with the edges.
- Undoing the width re-solves the sketch, which restores it to rounding
  (P12-SKETCH-003). A fresh regeneration of a copy gives the same bits;
  redo gives the wider block again.

## Diagnostics

Asserted verbatim (or by their stated prefix) by the tests:

| Case | Code | Message |
| --- | --- | --- |
| requests | InvalidArgument | `a variable-radius fillet needs at least one edge`; `edge references 1 and 2 refer to the same line through (0, 0, 0) mm along (1, 0, 0)`; `edge reference 1 (circle around (0, 0, 0) mm with axis (0, 0, 1) and radius 10 mm) is not straight; a variable-radius fillet rounds straight edges only` |
| stations | InvalidArgument | `edge reference 1: a variable-radius fillet needs two or more radius stations on each edge, got 1`; `edge reference 1, station 2: the position must be finite, got nan`; `edge reference 1: the first station must be at position 0, got 0.1`; `edge reference 1: the last station must be at position 1, got 0.9`; `edge reference 1: station 3 (at 0.4) does not follow station 2 (at 0.5); stations are listed by increasing position, at least 1e-06 apart` (also for equal positions, positions 5e-7 apart, and positions past 1) |
| radii | InvalidArgument | `edge reference 1, station 2: the radius must be positive and finite, got 0 mm` (and -1, inf, nan); `edge reference 1: the station radii differ by only 4e-07 mm; give them one radius, or radii at least 1e-06 mm apart` |
| laws | InvalidArgument | `edge reference 1: between stations 1 and 2 (5 mm at 0, 5 mm at 0.4) the radius would fall to -3.23489 mm at 0.247113; a variable radius must stay between the radii of the stations on either side`, and four more cases, the numbers from the reference law |
| edges | NotFound; FailedPrecondition | `variable-radius fillet: edge reference 1 (line through (0, 0, 41) mm along (1, 0, 0)) matches no edge of the body`; `… meets edge reference 1 (…) at a vertex; a variable-radius fillet rounds edges that meet no other edge it rounds`; `… continues smoothly into 2 other edge(s); a variable-radius fillet rounds single edges only`; `… is not between two planar faces; a variable-radius fillet rounds edges between planes only`; `… does not fit: its variable-radius fillet needs 45 mm on a face next to the edge, which leaves only 40 mm` (also 41 mm at a middle station); `… has stations 2 and 3 only 5e-07 mm apart on it; they must be at least 1e-06 mm apart`; `variable-radius fillet: the body is empty` |
| feature | InvalidArgument | `a variable-radius fillet needs a target feature`; `edge reference 1, station 2: the radius parameter ID must be valid`; a literal law checked at creation, a driven one deferred |
| regeneration | FailedPrecondition; InvalidArgument; NotFound; DimensionMismatch | `Taper: variable-radius fillet: … does not fit …`; `Taper: variable-radius fillet: edge reference 1, station 1: the radius must be positive and finite, got 0 mm`; `Taper: variable-radius fillet: edge reference 1: between stations 2 and 3 (5 mm at 0.5, 5 mm at 1) the radius would rise to 5.21659 mm at 0.68…`; `Taper: variable-radius fillet: edge reference 2 (line through (0, 50, 20) mm along (1, 0, 0)) matches no edge of the body` (the block got deeper); the meeting and continuing edges as above; `Taper: …` for an angle parameter |
| validation | error | `Taper (object:7): the radius of edge reference 1, station 1 is driven by tilt (object:8), which is an angle, not a length` |
| copies and names | error | `… a linear pattern cannot repeat a variable-radius fillet`; `… a mirror cannot repeat a variable-radius fillet`; `OnFillet (object:10): Taper (object:7) is a variable fillet, whose faces are not named (extrudes, revolves, sweeps, lofts, holes, chamfers and ribs name theirs)` |
| file | — | `….data.order: unknown field`, `….data: a variable-radius fillet needs a target feature`, `….data.edges: expected an array`, `….data.edges: missing required field`, `….data: a variable-radius fillet needs at least one edge`, `….edges[0].edge: missing required field`, `….edges[0]: expected an object`, `….edges[1].line: unknown field`, `….edges[1].edge.curve: unknown value 'spline'`, `….stations[2].radius: expected a number`, `….stations[2].position: missing required field`, `….stations[2].fixed: unknown field`, `….stations[2].radius_parameter: expected an ID`, `….stations: expected an array`, `….stations[1]: expected an object`, `….data: edge reference 2: the last station must be at position 1, got 0.9`, `….data: edge reference 2: between stations 2 and 3 (4 mm at 0.3, 4 mm at 1) the radius would rise to …` |
| an invalid edit | InvalidArgument | refused by `ModifyVariableFilletCommand`; the document is unchanged |
| CLI `validate` | failure | the regeneration message, `Result: invalid` |

A failed fillet keeps no body; the rest of the model is built.

## Tests

16 new Catch2 test cases, tagged `[variable]` with `[p12]`, and 3 process
tests:

- `tests/core/geometry/VariableFilletTests.cpp`: 3 cases. Requests and the
  law; isolated edges (rising, falling, constant and multi-station laws,
  both spine directions, 120° and concave edges, determinism); refusals.
- `tests/features/VariableFilletFeatureTests.cpp`: 5 cases. Validation; the
  block following its radii and width; determinism; undo/redo; failures.
- `tests/io/VariableFilletFileTests.cpp`: 6 cases. The save → destroy →
  load → regenerate round trip; constant fillets written as before next to
  a variable one; the written form; malformed files; the example file;
  STEP.
- `tests/cli/VariableFilletCliTests.cpp`: 2 cases (`info`, `validate`).
- `cli.info.tapered-block`, `cli.validate.tapered-block` and
  `cli.export-step.tapered-block` run on `examples/models/tapered_block.bcad`,
  each in a fresh process.

The Release run of the 16 cases records **1596 passed
assertions and 0 failed** (`qualification/reference-values-release.txt`).

**Determinism** is checked with an exact comparison of every property the
reference-model fingerprint (P11-QUAL-001) holds:

- validity and topology counts;
- volume, area, centre and bounds, bit for bit;
- the names of every face.

It is applied to repeated full regenerations, a new regenerator, a fresh
document, save → destroy → load → regenerate, undo of radius changes, and
repeated geometry calls.

## Qualification

`qualification/qualify.cmd` was run through
`qualification/run-qualification.cmd` and did the following:

- configured each preset;
- removed every build output;
- rebuilt with warnings as errors under code page 65001;
- ran CTest after each successful build;
- repeated the related tests five times in Release and Debug.

The Git tree IDs it recorded equal the working tree's after the run.

| Preset | Configure | Clean | Build | TUs compiled | `warning` lines | Tests |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | exit 0 | attempt 1 | exit 0 | 352 | 0 | **1036/1036 passed** (103.2 s) |
| Release | exit 0 | attempt 1 | exit 0 | 352 | 0 | **1036/1036 passed** (101.6 s) |
| Debug-shared | exit 0 | attempt 1 | exit 0 | 352 | 0 | **1036/1036 passed** (110.1 s) |

352 is `P12-FEAT-005`'s 342 translation units plus 10 new ones:

- `RadiusLaw.cpp`, `VariableFillet.cpp`, `occt/OcctVariableFillet.cpp`;
- `fillet/VariableFilletFeature.cpp`, `fillet/VariableFilletRegeneration.cpp`;
- `json/VariableFilletJson.cpp`;
- the four new test files.

**Repeats.**
`ctest -R "[Vv]ariable|[Rr]ib|[Dd]raft|[Ss]hell|[Ss]plit|[Cc]ombine|BodyOps|[Ff]illet|[Hh]ole|[Rr]evolve|[Ee]xtru|ThroughAll|[Rr]esult|[Pp]attern|[Mm]irror|[Ff]ace|[Rr]eference|[Dd]atum|[Bb]oolean|[Pp]ropert|[Rr]egenerat|[Vv]alidat|[Dd]ocument|[Ff]ile|[Cc]ommand|[Ee]xport|cli|tapered-block|ribbed-bracket|drafted-block|shelled-block|body-ops|P9" --repeat until-fail:5`
selected 690 tests.

| Preset | Result | Time |
| --- | --- | --- |
| Release | 690/690, each 5 times (3450 passed runs) | 332.6 s |
| Debug | 690/690, each 5 times (3450 passed runs) | 346.9 s |

**Across configurations** (`qualification/values-determinism.txt`), the
Debug, Release and Debug-shared executables printed identical values for
the 16 cases (1175 lines, MD5 `a954c1090c0267c45da1adaba736d1db`).

**Timing** (`qualification/timing-comparison.txt`: CTest's per-test times
summed, `-j 8`, same machine), P12-FEAT-005 → P12-FEAT-006, with 19 more
tests:

| Preset | P12-FEAT-005 | P12-FEAT-006 |
| --- | --- | --- |
| Release | 718.2 s | 736.0 s |
| Debug | 741.4 s | 757.2 s |
| Debug-shared | 762.3 s | 761.6 s |

The 19 new tests take 20.3 s in Release, 25.5 s in Debug and 25.1 s in
Debug-shared. The existing tests' times moved by -2.4, -9.7 and -25.8 s
(`timing-split.py`, its output in `timing-comparison.txt`).

- In Release, the twelve slowest tests took 1.4 to 2.7 s (up to 15 %)
  longer than in P12-FEAT-005, while the 758 tests under 0.5 s took 27.9 s
  less. Debug and Debug-shared show no such change: their medians for the
  tests of 0.5 s or more are 0.952 (1.017 in Release).
- The same Release build run again with nothing else running
  (`qualification/ctest-release-rerun.log`) summed 681.3 s. Eleven of its
  twelve slowest tests were faster than in P12-FEAT-005, the twelfth 0.4 s
  slower, and its median ratio was 0.920. The qualification run's
  differences are therefore run-to-run variation on this machine.
- The only code the existing tests run that changed is one more handler
  and one more type check in the pattern instance lookup. No speed change is
  claimed.

## Legacy Regression

`qualification/regression-comparison.txt` compares every log by test name
with `../P11-QUAL-001/release-ctest.log`, and
`qualification/regression-comparison-feat005.txt` with
`../P12-FEAT-005/ctest-release.log`:

- the P11 baseline has 737 names, and the P12-FEAT-005 log 1016;
- every baseline name is present and passed in the Debug, Release and
  Debug-shared logs, and no entry failed;
- the new names are this milestone's 19 tests (298 against P11).

**Every value the existing tests measure is unchanged**
(`qualification/all-values-comparison.txt`):

- Before its clean rebuild, the qualified P12-FEAT-005 Release build (the
  `aa2d41a` tree) ran every test case with `-s`. So did this milestone's
  qualified Release build.
- After dropping the 16 new cases (no existing case's name matches the
  pattern) and normalizing temporary paths, document UUIDs and the reported
  Git revision (line numbers kept: no existing test file changed), the
  37530 measured-value lines of the 946 existing cases are identical (MD5
  `703431ec5ebf36e87c7e213342efb40d` both). They include the constant-radius
  fillets' cases.

**The P0–P12-FEAT-005 regression suite remains green.** Changes to existing
production code:

- `Regenerator`: the handler;
- `Validation`: the radius parameters;
- `PatternSupport`: the refusal;
- `FeatureJson`: `edgeToJson()` and `edgeFromJson()` shared (moved out of
  the file's private namespace);
- `DocumentJson`, `ObjectJson`: the type;
- `FeatureCommands`, `Regeneration.hpp`: the command aliases and the
  regeneration functions;
- the geometry library links `TKGeomAlgo` (`Law_Function`);
- CLI `info`: the description.

Constant-radius fillets are untouched: their code, type and file format are
unchanged.

## Known Limitations

- **Setback distances and selectable corner transitions are not
  implemented** (deferred; [investigation](investigation/README.md)).
- **Straight edges between two planar faces only**, each rounded on its
  own: no tangent chains, no corners with other rounded edges of the same
  fillet, no curved edges or faces. These are the edges on which the
  kernel's law depends on the edge's stations alone.
- **The radius between stations is the kernel's spline**, not a linear or
  user-chosen blend. It is exact at the stations and never leaves the range
  of the two around it; station sets whose spline would are refused, even
  where the kernel could build them.
- **Positions are literal**; radii may be driven by parameters.
- **Edge references are geometric** (the edge's line). A parameter that
  moves the line fails the fillet with NotFound; nothing is substituted.
  A change of length keeps the stations at the same fractions.
- **Not repeated** by patterns or feature mirrors (a body mirror copies the
  result).
- **The fillet faces are not named.** The target's names are carried.
- **Measurement.** Volumes of these bodies come from the kernel's adaptive
  integration (1e-9, not 1e-12), bounds may be 1e-7 mm loose, and areas of
  planes bounded by the fillet come from the kernel's default integration
  (up to 4.7e-6 relative off in the tests, 1.85e-6 in the probe). The
  geometry itself is exact to 4e-14 mm in the probes.

## Evidence Files

- `README.md`: this file.
- `investigation/README.md`: the capability assessment (blocked record).
- `kernel-probe/*.cpp`, `kernel-probe/*.log`, `kernel-probe/occt-source-excerpts.txt`,
  `kernel-probe/api-search.log`: the probes, their output and the OCCT
  source cited.
- `qualification/qualify.cmd`, `qualification/run-qualification.cmd`: the
  qualification as run.
- `qualification/qualification-times.txt`: every step's start, exit code and
  time, the HEAD and the Git tree IDs of the qualified sources.
- `qualification/configure-*.log`, `clean-*.log`, `build-*.log`,
  `ctest-*.log`: the three presets.
- `qualification/ctest-repeat-{release,debug}.log`: the related tests, 5
  times each.
- `qualification/regression-comparison.txt`,
  `qualification/regression-comparison-feat005.txt`,
  `qualification/compare-regression.py`: the legacy comparisons.
- `qualification/all-values-comparison.txt`: every measured value of the
  P12-FEAT-005 tests, against the P12-FEAT-005 Release build.
- `qualification/reference-values-release.txt`, `values.py`,
  `values-header.txt`: the new tests' measured values.
- `qualification/values-determinism.txt`, `compare-values.py`: the same from
  all three configurations.
- `qualification/deviations.txt`, `deviations.py`: the largest deviations.
- `qualification/timing-comparison.txt`, `compare-times.py`,
  `timing-split.py`: per-test times.
- `qualification/spine-direction-diagnostic.log`: the temporary diagnostic.

## Final Result

```text
TASK:            P12-FEAT-006 Variable-radius fillet (setback and corner
                 transitions deferred by the scope decision of 2026-09-18)
IMPLEMENTATION:  variableFilletEdges() (stations along the canonical
                 direction, BetterCAD's own radius law, laws leaving their
                 stations refused, the kernel's law and surface checked
                 before a result is kept); RadiusLaw; radiusAt();
                 VariableFilletFeature; validation, JSON, CLI, commands
TESTS:           16 new test cases and 3 process tests; 1036/1036 in
                 Debug, Release, Debug-shared; 690 related tests x5 in
                 Release and Debug
VALIDATION:      variable-radius fillets of isolated straight edges
                 (rising, falling, constant, 3- to 5-station laws, both
                 kernel directions, faces at 90 and 120 degrees, a concave
                 edge) match an independent derivation of the law within
                 6.7e-12 (volume), 1.8e-10 mm (centre) and the kernel's
                 1e-7 mm bound padding, as radii and the block change;
                 radiusAt() within 2.7e-15 mm; laws leaving their stations,
                 oversized radii and unverifiable edges refused; every
                 existing measured value unchanged; values identical across
                 configurations
RESULT:          PASS (variable radius); setback and corner transitions
                 DEFERRED, not implemented
EVIDENCE:        docs/verification/P12-FEAT-006/
TODO:            the six variable-radius deliverables ticked; setback and
                 corner-transition controls left unticked as deferred
```
