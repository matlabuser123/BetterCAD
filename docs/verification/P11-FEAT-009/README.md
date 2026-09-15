# P11-FEAT-009 — Loft Verification

## Status

**PASS.** A persistent, parametric ruled loft through the closed profiles
of two or more sketches, in the order given, with new body, join, cut and
intersect. It is validated against the extrude, the revolve and closed-form
volumes. Debug, Release and Debug-shared each passed 674/674 tests from
clean rebuilds with 0 compiler warnings, and every P11-FEAT-008 test still
passes. Sections of different shapes and sections on non-parallel planes
are **not implemented**: they are refused with structured errors (see Known
Limitations).

Date: 2026-09-15. `main` was at `0a59e4c` (P11-FEAT-008 Sweep) before this
milestone. Every value below was measured in this session and is recorded
in this directory:

- the Release test output of the qualified tree (`loft-values-release.txt`);
- the kernel accuracy example, from the same build
  (`geometry-accuracy-release.txt`);
- the raw-OCCT kernel probe, which is independent of BetterCAD's code
  (`kernel-probe/`);
- the OCCT-defaults experiment, run on a deliberately modified adapter
  (`regression/`).

## Scope

Every item was checked against the working tree and by tests. `Loft_*`
tests exercise the geometry layer and `LoftFeature_*` tests the feature.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Loft feature (`LoftDefinition`: ordered sections, interpolation, operation, target) | IMPLEMENTED | `LoftFeature_DefinitionIsValidatedOnCreateAndEdit`; `LoftFeature_DependsOnEverySectionAndTarget` |
| Ordered profile references (sketch IDs, each with an offset, literal or driven) | IMPLEMENTED | `LoftFeature_SaveLoadPreservesSectionOrder`; `LoftFeature_PreservesSectionOrder` |
| Two-section loft | IMPLEMENTED | `LoftFeature_EqualRectanglesMatchPrismVolume`; `…CircularFrustumMatchesAnalyticVolume` |
| Three-or-more-section loft | IMPLEMENTED | `LoftFeature_ThreeCircularSectionsMatchPiecewiseFrustums`; `Loft_MultipleSectionsArePiecewiseRuled` |
| Different profile sizes | IMPLEMENTED | frustum, rectangular frustum and hexagon cases |
| Different profile positions (offset sections) | IMPLEMENTED | `LoftFeature_OffsetSectionsProduceValidSolid`; `Loft_OffsetSectionsKeepTheirPositions` |
| Different orientations: turned sketch axes, reversed normals, turned sections | IMPLEMENTED on parallel planes | `Loft_MatchesSectionsWithTheLeastTwist`; `Loft_TwistedSectionsFollowTheTwistLaw`; `LoftFeature_Remove` (a section sketch facing down) |
| Closed profiles → solid; open profiles refused | IMPLEMENTED | `LoftFeature_RejectsOpenProfile`; `…RejectsInvalidProfile` |
| Profile compatibility validation | IMPLEMENTED | `LoftFeature_RejectsIncompatibleSections`; `Loft_RejectsSectionsItCannotLoft` |
| Section ordering (kept, never sorted) | IMPLEMENTED | `LoftFeature_PreservesSectionOrder`; `Loft_KeepsTheOrderAndDirectionOfItsSections` |
| Deterministic correspondence (least twist, ties, winding) | IMPLEMENTED | `Loft_MatchesSectionsWithTheLeastTwist`; `Loft_BreaksTiesDeterministically` |
| NewBody / Add / Remove / Intersect | IMPLEMENTED | `LoftFeature_NewBody`; `…_Add`; `…_Remove`; `…_Intersect` |
| Operations without overlap (the shared policy) | IMPLEMENTED | `LoftFeature_OperationsWithoutOverlapFollowTheSharedPolicy` |
| Geometry validity, with an independent volume check of every result | IMPLEMENTED | `makeLoft()`'s checks; `regression/occt-defaults-postcheck.log` |
| Loft vs Extrude | PASS | `LoftFeature_EqualSectionsMatchExtrude`; `…EqualCirclesMatchCylinderVolume`; `Loft_EqualSectionsMatchPrisms` |
| Loft vs Revolve | PASS | `LoftFeature_CircularFrustumMatchesRevolve`; `Loft_CircularFrustumMatchesAnalyticVolumeAndRevolution` |
| Regeneration (section geometry, spacing, section position, target) and dependency tracking | IMPLEMENTED | `LoftFeature_RegeneratesWhenSectionChanges`; `…WhenSpacingChanges`; `…OffsetSectionsProduceValidSolid`; `…WhenTargetChanges`; `…DependsOnEverySectionAndTarget` |
| Atomic failure | IMPLEMENTED | `LoftFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`; `…RejectsMissingProfile` |
| Self-intersection and folding | IMPLEMENTED (a fold preflight, then the kernel's self-interference check) | `LoftFeature_FailsSafelyOnSelfIntersection`; `Loft_RejectsLoftsThatFoldOrIntersect` |
| Save/load (section order preserved) | IMPLEMENTED | `LoftFeature_SaveLoadPreservesSectionOrder`; `…FailedLoftSavesAndLoadsUnchanged`; `…DataIsStoredAsTransparentJson`; `…MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo | IMPLEMENTED | `LoftFeature_UndoRedoRestoresGeometry` |
| STEP/STL | IMPLEMENTED | `LoftFeature_ExportsStep`; `LoftFeature_ExportsClosedStl` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | the failure tests above; `LoftFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe lofts` |
| Determinism | IMPLEMENTED | `LoftFeature_RegenerationIsDeterministic`; `Loft_BreaksTiesDeterministically` |
| Different topology (circle → polygon, 4 → 6 corners, lines against arcs) | **NOT IMPLEMENTED** (refused with a structured error) | `LoftFeature_RejectsIncompatibleSections`; `Loft_RejectsSectionsItCannotLoft` |
| Non-parallel sections | **NOT IMPLEMENTED** (refused with a structured error) | the same tests |
| Smooth interpolation, guide curves, centreline loft, tangency/curvature end conditions, sections with holes, thin/surface loft, closed loft | **NOT IMPLEMENTED** | — |

**Not a wrapped ThruSections call.** The loft is a document feature with
persistent references. BetterCAD makes the rules; the kernel only builds
the solid:

1. `LoftFeature` stores the sections in order (sketch IDs with offsets),
   the interpolation, the operation and the target. It stores no kernel
   wire or shape.
2. `resolveLoftSections()` rebuilds every section from its sketch at each
   regeneration and applies the offsets.
3. `planLoft()` has no kernel dependency. It checks:
   - the section count, the loops and their finiteness;
   - parallel planes and strict order along the loft;
   - matching shapes.

   It then chooses the correspondence (least twist, deterministic ties,
   normalized winding), refuses folds, and predicts the volume with the
   prismatoid formula.
4. Only then does the OCCT adapter build the loft, ruled and with the
   kernel's own matching switched off. The result is checked for validity,
   self-interference and the predicted volume. It is then combined with the
   target through the shared operations.

The kernel probe shows why the checks are needed. OCCT's `ThruSections`
returns solids that pass `BRepCheck` but are wrong when:

- the matching is shifted by one edge;
- a section is traversed the other way;
- the interpolation is smooth;
- the sections coincide or go back.

The OCCT-defaults experiment shows that the post-checks refuse those solids
(see Kernel Probe).

## Architecture

```text
LoftFeature (bettercad_features)                    include/bettercad/features/LoftFeature.hpp
  └─ regenerateLoft()                               src/features/loft/LoftRegeneration.cpp
       ├─ resolveLoftSections(): each section's sketch → its one closed profile
       │    (extractRegions(), shared with extrude/revolve/sweep) → plane moved by the offset
       ├─ geometry::makeLoft(sections)
       └─ combineWithTarget(operation, tool, target)  shared: new body / join / cut / intersect
geometry::makeLoft(sections)                        include/bettercad/core/geometry/Sweeps.hpp
  ├─ checkRegion() per section                      shared with prisms, revolutions and sweeps
  ├─ detail::planLoft() (no kernel)                 src/core/geometry/LoftPlan.{hpp,cpp}
  │    count, holes, finiteness → loft direction, parallel planes, strict order
  │    → common frame, counter-clockwise loops → shape match, least-twist start, ties
  │    → fold check on the quadratic area A(t) → expected volume Σ h/6 (A0 + 4 Am + A1)
  ├─ OCCT adapter                                   src/core/geometry/occt/OcctSweeps.cpp
  │    profile face (validity) → wires (circle seams on the common X axis)
  │    → BRepOffsetAPI_ThruSections(solid, ruled), CheckCompatibility(false)
  └─ checks: one valid solid → BRepAlgoAPI_Check (self-interference)
             → finite positive volume → |V − V_prismatoid| ≤ 1e-8 × V_prismatoid
```

No OCCT type leaves `occt/`, and `architecture.layering` passes. Features
call only the geometry API; the conceptual `GeometryService` is the function
API of `bettercad_geometry`.

**Reused, not duplicated:**

- the regenerator, the dependency graph and the commands
  (`CreateFeatureCommand` / `ModifyFeatureCommand` as `CreateLoftCommand` /
  `ModifyLoftCommand`);
- the profile builder `extractRegions()` (closed loops, open-profile
  diagnostics) and `combineWithTarget()`;
- `checkRegion()`, the profile-face builder and `WireBuilder` of the sweep
  adapter (given an option to put circle seams on the plane's X axis, off
  for prisms, revolutions and sweeps, which are unchanged);
- `signedArea()`, `reversed()`, `regionCentroid()` and `arcSweep()`;
- `drivingValue<Length>()` for driven offsets, as for the mirror's offset;
- result bodies, validation, the JSON reader and the CLI's `info` and
  `validate`.

**New:**

- geometry: `makeLoft()` and `LoftPlan`;
- features: `LoftFeature.hpp`, `LoftFeature.cpp` and `LoftRegeneration.cpp`
  (`resolveLoftSections()`, `loftTool()`, `regenerateLoft()`), plus the
  regenerator handler, the commands and the validation of loft sections;
- io: the `loft` JSON mapping;
- CLI: `info` describes lofts;
- loft cases in `examples/geometry_accuracy`;
- test fixtures in `tests/support/LoftModels.hpp`;
- `docs/architecture.md`.

**Transactions.** As for every feature, the loft builds its whole body
before anything is stored. On failure:

- the loft fails and the regenerator keeps no body for it (the last valid
  body is not replaced by a partial one);
- its sections, its target and the document are unchanged;
- its dependents are blocked.

Definitions are validated before a command changes the document.

## Profile References

- **Sections.** `LoftDefinition::sections` is a list of `LoftSection{SketchId
  sketch; Length offset; optional<ParameterId> offsetParameter}`. A section
  is the one closed profile of its sketch, moved along the sketch plane's
  normal by the offset, which is literal or driven by a length parameter.
  The offset makes the spacing parametric: two sketches on one plane with
  the second moved by `height` give a loft whose height is a parameter.
- **One profile each.** A section sketch must hold exactly one closed
  profile without holes. It is found by the profile builder that extrude,
  revolve and sweep use, so open profiles and zero-area loops give the same
  diagnostics as there. Sketches with several profiles, or a profile with a
  hole, are refused with FailedPrecondition, naming the section:
  - "Loft: section 2 (sketch 'Pair'): it has 2 closed profiles; a loft
    section must be exactly one";
  - "Loft: section 2 (sketch 'Ring'): its profile has a hole; loft sections
    must be single closed profiles".
- **Stable identity.** A reference is a `SketchId`. A missing sketch fails
  the loft with NotFound ("object:6 references object:5, which does not
  exist"). Putting the sketch back with its ID restores the loft
  bit-identical; nothing is ever substituted.
- **Repeats.** The same sketch may appear twice at different offsets (the
  same profile at two heights). A section that repeats another (the same
  sketch at the same offset) is refused by the definition: "section 3
  repeats section 1: the same sketch at the same offset".
- **JSON:** `{"sections": [{"sketch": 7, "offset": 0.0}, {"sketch": 8,
  "offset": 0.0, "offset_parameter": 4}], "interpolation": "ruled",
  "operation": "cut", "target": 6}`.

## Section Ordering

- **The list order is the loft's order.** It is stored, saved and resolved
  as given. It is never sorted by ID, by creation, by coordinate or by the
  kernel.
- **Direction.** The loft runs from section 1 towards section 2, along
  section 1's plane normal (either way).
- **Strictly in order.** Every section must lie strictly beyond the one
  before along that direction (1e-10 m). Sections on one plane, or a section
  that goes back, are refused, not reordered:
  - "makeLoft: sections 1 and 2 lie on the same plane: a loft needs its
    sections apart";
  - "makeLoft: section 3 lies 50 mm behind section 2 along the loft: the
    sections must be listed in the order they follow one another".
- **Evidence:**
  - `LoftFeature_PreservesSectionOrder` swaps the last two sections and gets
    the refusal. Listed top down, the loft runs down through the same
    sections: 18325.957145940468763 mm³, the same printed value as the
    upward loft (the test compares within 1e-12), centre of mass (0, 0, 50).
  - `LoftFeature_SaveLoadPreservesSectionOrder` stores the sections as
    [7, 6, 5] (against their IDs' order) and reloads [7, 6, 5]. The JSON
    lists them in that order.
- **The probe shows why the order must be checked.** For sections at z 0,
  50 and 20, OCCT builds a "valid" solid of 8723.2 mm³ (the first frustum
  less the backward one) and its self-check passes.

## Correspondence Policy

Which point of a section goes to which point of the next. BetterCAD decides
this before the kernel is called; the kernel is told not to re-match
(`CheckCompatibility(false)`).

1. **Common frame.** Every section is expressed in one frame: the loft
   direction as normal, and section 1's X axis. Sketches on turned axes or
   with reversed normals (on parallel planes) are handled by this mapping.
2. **Winding.** Every loop is taken counter-clockwise about the loft
   direction, whatever its sketch's orientation. A reversed section would
   otherwise twist the loft: in the probe, a frustum whose top circle faces
   down gives 2356.2 mm³ instead of 5497.8, and its self-check fails.
3. **Shape.** Consecutive sections must be both circles, or the same lines
   and arcs in the same cyclic order, matched arcs turning by the same
   signed angle (1e-9 rad). Anything else is refused with a message saying
   how the shapes differ (see Diagnostics).
4. **Start.** Among the starts that fit, a section's loop starts where its
   corners (segment starts), measured from its centroid, lie nearest the
   previous section's corners, measured from that section's centroid
   (least squares). This is the least twist, independent of lateral offsets
   and of which corner a profile was drawn from.
5. **Ties.** Starts whose costs differ by less than 1e-9 of the sections'
   size (the sum of squared corner distances) tie. A tie goes to the loop's
   first matching start, in the order the profile builder gives (from the
   sketch's entity order), so rounding noise cannot flip a twist.
6. **Circles** match angle for angle: every circle's seam is placed on the
   common X axis.

**Evidence:**

| Check | Result |
| --- | --- |
| 20 × 10 → 10 × 20, the top drawn from each of its 4 corners, both ways (8 variants) | 6500 mm³ exactly every time (corner to nearest corner); a quarter-turn twist would give 4000 |
| A square to the same square turned 30° | twists by 30°, not 60°: 11464.10161513775529 mm³ = hA (2 + cos 30°)/3 |
| A square to the same square turned 45° (a tie) | twists +45° with noise 0 and ±1e-12 rad: the side edge from (10, 10, 0) runs to (0, 14.14, 30) each time, never to (14.14, 0, 30); 10828.42712474619111 mm³ = hA (2 + cos 45°)/3 |
| A frustum's top circle on axes turned 70°, and on a plane facing down | 5497.787143782137 mm³, the frustum (seams and winding aligned) |
| A half disc to the same half disc turned 90° (a line and an arc match only line to line) | 785.39816388887 mm³ against hA (2 + cos 90°)/3 = 250π = 785.39816339745 (6.3e-10; a B-spline face) |

**The twist law.** A section lofted to itself turned by θ, each point to
its image, has the section halfway of area A (1 + cos θ)/2. So
V = hA (2 + cos θ)/3, a closed form independent of the implementation.

## Interpolation Policy

- **Ruled.** `LoftInterpolation::Ruled`, the only mode, stored as
  `"ruled"`. Straight lines join matching points of consecutive sections.
  The kernel is asked for exactly that: `ThruSections(isSolid = true,
  ruled = true)`. No kernel default is relied on.
- **Piecewise.** Through three or more sections, each interval is ruled on
  its own: circles give frustums of cones. Smooth interpolation (OCCT's
  default) is not offered. In the probe it gives 22514.7 mm³ for three
  circles whose frustums hold 18326.0 mm³.
- **The volume law.** Between two sections whose points correspond at equal
  parameters, the cross-section at height t is the section of interpolated
  points. Its area is quadratic in t:
  A(t) = A0 (1 − t)(1 − 2t) + 4 Am t (1 − t) + A1 t (2t − 1),
  with Am the area of the section of averaged points. Averaging maps lines
  to lines and arcs of equal sweep to arcs, so Am is exact. Two things
  follow:
  - the volume is h/6 (A0 + 4 Am + A1) (the prismatoid formula), exact;
  - A(t) must stay positive, or the loft folds. Its minimum on [0, 1] is
    checked in closed form.
- **Tangency and curvature end conditions** are not offered.

## Equal Rectangle Validation

`LoftFeature_EqualRectanglesMatchPrismVolume` (the primary acceptance case):
two sketches with 10 × 20 mm rectangles (`width`, `height`), the second
moved up by `length` = 100 mm.

| | |
| --- | --- |
| Expected | A h = 200 × 100 = 20000 mm³ |
| Actual | 19999.99999999999636 mm³ |
| Absolute error | 3.6e-12 mm³ |
| Relative error | 1.8e-16 |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Area | 6400 mm² exactly |
| Centre of mass | (4.99999999999999911, 9.99999999999999822, 50) mm; expected (5, 10, 50) |
| Bounds | (0, 0, 0) to (10, 20, 100) mm (padded by at most 1e-7 mm) |
| Validity | valid, 1 solid; the loft is the only result body |
| Result | **PASS** |

At the geometry level (`Loft_EqualSectionsMatchPrisms`): 20000.0 exactly,
against `makePrism()`'s 20000.00000000000364 (area 6400 both).

## Equal Circle Validation

`LoftFeature_EqualCirclesMatchCylinderVolume`: the frustum model with r1 =
r2 = 5 mm and h = 100 mm, and the extrude of its bottom sketch by the same
height.

| | Expected | Actual | Error |
| --- | --- | --- | --- |
| Volume | πr²h = 2500π = 7853.98163397448297 mm³ | 7853.98163397447843 mm³ | 4.5e-12 mm³ (5.8e-16) |
| Against the extrude | 7853.98163397448025 mm³ | | 2.3e-16 |
| Area | 1050π = 3298.67228626928272 mm² | 3298.67228626928272 mm² | 0 |
| Centre of mass | (0, 0, 50) | on the axis, z = 50 (loft and extrude) | |

Tolerance: 1e-12 relative. **Result: PASS.**

## Circular Frustum Validation

`LoftFeature_CircularFrustumMatchesAnalyticVolume`:

| | |
| --- | --- |
| r1 | 10 mm (parameter `r1`, the bottom sketch) |
| r2 | 5 mm (parameter `r2`, the top sketch) |
| h | 30 mm (parameter `height`, the top section's offset) |
| Expected | V = πh/3 (r1² + r1r2 + r2²) = 1750π = 5497.78714378213772 mm³ |
| Actual | 5497.78714378213863 mm³ |
| Error | 9.1e-13 mm³ absolute, 1.65e-16 relative |
| Tolerance | 1e-12 relative |
| Area | π(r1 + r2)√(h² + (r1 − r2)²) + π(r1² + r2²) = 1825.91623760243510 expected, 1825.91623760243419 actual (5.0e-16) |
| Centre of mass | z = h (r1² + 2r1r2 + 3r2²)/(4(r1² + r1r2 + r2²)) = 11.7857142857142847 expected, 11.78571428571428648 actual; x and y ~1e-16 |
| End faces | the bottom disc 100π and the top disc 25π, exact |
| Result | **PASS** |

The kernel makes the side a cone (coaxial circles), so the volume is exact.
`geometry-accuracy-release.txt` gives 5497.7871437821368 (1.65e-16).

## Rectangular Frustum Validation

`LoftFeature_RectangularFrustumMatchesAnalyticVolume`: 20 × 10 mm to
10 × 5 mm over 30 mm, similar and centred.

| | Expected | Actual | Error |
| --- | --- | --- | --- |
| Volume | h/3 (A1 + A2 + √(A1A2)) = 10 (200 + 50 + 100) = 3500 mm³ | 3500.00000000000091 mm³ | 2.6e-16 |
| Area | 250 + 2 × 15 √906.25 + 2 × 7.5 √925 = 1609.32678318178864 mm² (the trapezoid sides) | 1609.32678318178841 mm² | 1.4e-16 |
| End faces | 200 and 50 mm² (planes) | 200, 50.00000000000001 | |
| Centre of mass | z = 30 × 0.392857… = 11.7857142857142847 | exact | |
| Bounds | (−10, −5, 0) to (10, 5, 30) | padded by ≤ 1e-7 mm | |

Tolerance: 1e-12 relative. **Result: PASS.** Also:

- **Regular hexagons** R 10 → R 5 (similar): 4546.63336986830291 mm³, equal
  to h/3 (A1 + A2 + √(A1A2)).
- **A rectangle to its transpose** (20 × 10 → 10 × 20, not similar,
  `Loft_PolygonsMatchFrustumsAndPrismatoids`): 6500 mm³ exactly, the
  prismatoid with Am = 15 × 15.

## Three-Section Validation

`LoftFeature_ThreeCircularSectionsMatchPiecewiseFrustums`: circles r 5, 10
and 5 at z 0, 50 and 100. The middle and top sections are moved by the
parameters `mid` and `length`.

| | Expected | Actual | Error |
| --- | --- | --- | --- |
| Volume | two frustums, 2 × π·50/3 (25 + 50 + 100) = 17500π/3 = 18325.95714594046149 mm³ | 18325.957145940468763 mm³ | 4.0e-16 |
| Centre of mass | z = 50 (symmetric) | on the axis, z = 50 | |
| Sections | the r 10 circle at z = 50, the r 5 circles at z = 0 and 100 | each found as an edge | |

**Four sections**, r 10, 6, 8, 4 at z 0, 20, 35, 60 (literal offsets): the
sum of three frustums, 2980π = 9361.94610769758401 mm³; actual
9361.94610769758219 (1.9e-16). The kernel builds two and three conical
faces. Tolerance: 1e-12 relative. **Result: PASS.**

## Loft vs Extrude

`LoftFeature_EqualSectionsMatchExtrude`: the equal-rectangle loft against
the extrude of its bottom sketch by the same `length`, in one document.

| State | Loft | Extrude | Comparison |
| --- | --- | --- | --- |
| width 10, length 100 | V 19999.99999999999636, A 6400 | V 19999.99999999999272, A 6400 | volumes 1.8e-16 apart; areas equal; centres 8.7e-16, 0 and 6.9e-15 mm apart; the same bounds; 1 solid each |
| width 15, length 60 | V 18000, A 4800.0000000000009 | V 18000, A 4800 | the same; 15 × 20 × 60 = 18000 |

Also the equal circles (above: 2.3e-16 against the extrude) and, at the
geometry level, both against `makePrism()`. **Result: PASS.**

## Loft vs Revolve

`LoftFeature_CircularFrustumMatchesRevolve`. The frustum is lofted between
two circles, and also revolved: the trapezoid (0, 0), (r1, 0), (r2, h),
(0, h) in the XZ plane, 360° about Z. Both are driven by the same
parameters `r1`, `r2` and `height`.

| r2, h | Analytic πh/3 (r1² + r1r2 + r2²) | Loft | Revolve | Loft vs revolve |
| --- | --- | --- | --- | --- |
| 5, 30 | 5497.78714378213772 | 5497.78714378213863 | 5497.78714378213863 | identical |
| 8, 60 | 15330.97214951818933 | 15330.97214951819115 | 15330.97214951820388 | 8.3e-16 |

- **Areas agree:**
  - 1825.91623760243419 against 1825.91623760243556;
  - 3910.02569334976670 against 3910.02569334976943.
- **Centres of mass agree** with the analytic z, h (r1² + 2r1r2 + 3r2²) /
  (4(r1² + r1r2 + r2²)):
  - 11.78571428571428648 and 11.7857142857142847 against
    11.7857142857142847;
  - 27.78688524590163667 and 27.78688524590164732 against
    27.78688524590164022.
- **Bounds agree:** (±10, ±10, 0 … h), both padded by at most 1e-7 mm.

All three paths agree: analytic, loft and revolve. **Result: PASS.**

## Offset Sections

`LoftFeature_OffsetSectionsProduceValidSolid`: the top circle (r 5) is 10
mm away from a fixed point, level with it, by the parameter `shift`.

- **shift 10, centred:** 5497.78714378213863 mm³, a cone (exact).
- **shift 20, the top centre at x = 10:** the loft leans. It is valid, with
  1 solid.
  - The volume is 5497.78714391515132 mm³ against 1750π (Cavalieri:
    2.4e-11, a B-spline side face). Tolerance: 1e-9 relative.
  - The centre of mass is (3.92857142877393883, 2.7e-15, 11.78571428576180224)
    mm. The expected value is (10t, 0, 30t) with t = 0.392857…:
    (3.92857142857142838, 0, 11.7857142857142847). The errors are
    2.0e-10 and 4.8e-11 mm.
  - The bounds are (−10, −10, 0) to (15, 10, 30).
  - No recentring: the top disc is at (10, 0, 30), 25π exactly, and there
    is none at (0, 0, 30).
  - Only Top and Loft were rebuilt.
- **A leaning prism** (geometry level): a 10 × 20 rectangle to the same
  rectangle moved (7, −3) and 50 up. V = 10000 exactly, centre of mass
  (3.5, −1.5, 25).

**Result: PASS.**

## Operations

All four use the shared `combineWithTarget()` of extrude, revolve and sweep.

**NewBody** (`LoftFeature_NewBody`): the lofted tool alone is the result
body: 1 valid solid of 1750π. **PASS.**

**Add** (`LoftFeature_Add`, `LoftBossModel`): a tapered boss. It rises from
a 40 × 20 rectangle on the 100 × 50 × 20 block's top face to a 20 × 10 one
`rise` = 30 mm above it, centred at (50, 25).

- **Formula:** V = 100000 + h/3 (800 + 200 + √(800 × 200)) = 114000.
- **Actual:** 114000.00000000001455 mm³ (1.3e-16), 1 valid solid.
- **Checks:**
  - the boss's top face is 200.00000000000006 mm²;
  - the block's top is 5000 − 800 = 4200.0000000000018 mm²;
  - the bounds are (0, 0, 0) to (100, 50, 50).

**PASS.**

**Remove** (`LoftFeature_Remove`, `TaperedHoleModel`): a tapered blind hole.
It runs from a circle r 8 on the top face (z = 20) to a circle r 4 `depth` =
15 mm below. The deeper section's sketch faces down, so its positive offset
moves it into the block.

- **Formula:** V = 100000 − π·15/3 (64 + 32 + 16) = 100000 − 560π =
  98240.70811398972 mm³.
- **Actual:** 98240.70811398973456 mm³ (1.5e-16), 1 valid solid.
- **Checks:**
  - the mouth circle r 8 at (50, 25, 20) is found;
  - the flat bottom at z = 5 is 16π, 50.26548245743667 mm² against
    50.26548245743669;
  - the top face is 5000 − 64π, 4798.9380701702557 mm² against
    4798.9380701702530.

**PASS.**

**Intersect** (`LoftFeature_Intersect`): the tapered hole's tool intersected
with the block is the frustum itself. It has 560π = 1759.29188601028409 mm³;
actual 1759.29188601028363 (2.6e-16), with bounds (42, 17, 5) to (58, 33,
20). **PASS.**

**Without overlap** (`LoftFeature_OperationsWithoutOverlapFollowTheSharedPolicy`):
the established policy applies, and there are no loft-specific boolean
rules.

- **An intersection that misses** leaves an empty body. It is not an
  invalid one:
  - `validateDocument` reports "Taper (object:9) produced an empty body"
    under the geometry check;
  - a feature built on it fails with FailedPrecondition, "Rejoin: a join
    feature needs the body of its target feature".
- **A join that misses** keeps the tool as a separate solid: 2 valid solids
  of 114000.00000000004 mm³.

## Regeneration

Each change regenerates exactly the listed nodes, in order, and leaves no
stale section.

| Change | Rebuilt | Expected (mm³) | Actual (mm³) | Stale geometry |
| --- | --- | --- | --- | --- |
| **Profile geometry:** top radius 5 → 8 mm | Top, Loft | frustum(10, 8, 30) = 7665.48607475909466 | 7665.48607475909557 | the r 8 top circle is found, no r 5 one |
| bottom radius 10 → 12 mm | Bottom, Loft | frustum(12, 5, 30) = 7194.24717672062525 | 7194.24717672062525 | bounds ±12 |
| **Section spacing:** height 30 → 60 mm | Loft only | 10995.57428756427544 (V ∝ h) | 10995.57428756427726; ratio 2.0 exactly | bounds up to z = 60 |
| middle section's offset `mid` 50 → 40 mm | Loft only | frustum(5, 10, 40) + frustum(10, 5, 60) = 18325.95714594046149 | 18325.95714594046513 | the r 10 circle now at z = 40 |
| **Section offset:** top centre x 0 → 10 mm (`shift` 10 → 20) | Top, Loft | 1750π (Cavalieri) | 5497.78714391515132 | the top disc at x = 10, none at x = 0 |
| **Target body:** block height 20 → 30 mm | Pad, Taper | 150000 − 560π = 148240.70811398971 | 148240.70811398979 (5.9e-16) | bounds up to z = 30 |
| hole depth 15 → 10 mm | Taper only | 150000 − frustum(8, 4, 10) = 148827.13874265982 | 148827.13874265988 (3.9e-16) | |

**Dependencies** (`LoftFeature_DependsOnEverySectionAndTarget`):

- **Declared.** The three-section loft depends on {Bottom, Middle, Top,
  `mid`, `length`}: every section's sketch in order, then the offset
  parameters. The tapered hole depends on {Mouth, Tip, `depth`, Pad}, and
  its `target()` is Pad.
- **What rebuilds.** `r_mid` rebuilds {Middle, Loft}; `r_end` rebuilds
  {Bottom, Top, Loft}; `mid` rebuilds {Loft}. A regeneration with nothing
  changed rebuilds nothing.
- **The target.** The block's height rebuilds {Pad, Taper}. No manual
  regeneration is needed.

**Result: PASS** (profile geometry, spacing, offset, target).

## Atomic Failure

`LoftFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`, on the
tapered hole:

- **An invalid edit** (one section) through `ModifyLoftCommand` is refused
  with InvalidArgument. The document is unchanged (`equivalent()`), the
  revision is unchanged and nothing is recorded to undo.
- **A valid edit the geometry cannot take** (the tip replaced by the block's
  rectangle) is recorded. It fails only at regeneration: "Taper: makeLoft:
  sections 1 and 2 cannot be matched: section 1 is a circle and section 2 is
  4 lines; lofts between different shapes are not supported".
  - The loft keeps no body.
  - Regeneration does not change the document.
  - Pad stays up to date, with a bit-identical volume.
- **Undo** restores the previous document, and the loft rebuilds
  bit-identical.

**Also:**

- **Missing sections** (`LoftFeature_RejectsMissingProfile`). A deleted
  section sketch fails the loft with NotFound. It is restored bit-identical
  when the sketch is put back with its ID. A missing offset parameter
  ("object:6 references object:99, which does not exist") fails it the same
  way.
- **Saved while failed** (`LoftFeature_FailedLoftSavesAndLoadsUnchanged`).
  A failed loft (sections on one plane) saves and loads unchanged, with the
  same error, and recovers at `height` 30.

**Result: PASS.**

## Save / Load

`LoftFeature_SaveLoadPreservesSectionOrder`. Each model is saved, replaced
by an empty document, loaded and regenerated.

**The tapered hole (every field in use).** Checked after loading:

- `equivalent()` documents with identical item IDs and the same
  dependencies for every node;
- the definition equal field for field:
  - the feature ID and name ("Taper");
  - the sections [Mouth, Tip] in order, with the tip's offset parameter
    `depth`;
  - `Ruled`, `Cut`, and the target (Pad);
  - dependencies {7, 8, 4, 6};
- the same regeneration order;
- bit-identical volume, area, centre of mass, bounds and topology for Pad
  and Taper.

It is still parametric after loading:

- `depth` 10 mm rebuilds {Taper}: 98827.13874265984 mm³ against
  100000 − frustum(8, 4, 10) = 98827.13874265981;
- `height` 30 mm rebuilds {Pad, Taper}.

**Also:**

- **Section order** against the sketches' IDs: [7, 6, 5] reloads as
  [7, 6, 5], with bit-identical geometry. In the JSON, sketch 7 comes before
  6 and 6 before 5.
- **Literal offsets** (0, 20.3 and 35.7 mm) round-trip bit for bit
  (`operator==` on the doubles). The loft is 6553.36227538830917 mm³, equal
  to frustum(10, 6, 20.3) + frustum(6, 8, 15.4).
- **The JSON** is checked text for text, as the file indents it. Compacted,
  the tapered hole's data is `{"sections": [{"sketch": 7, "offset": 0.0},
  {"sketch": 8, "offset": 0.0, "offset_parameter": 4}], "interpolation":
  "ruled", "operation": "cut", "target": 6}`. A literal
  offset of 12.5 mm is `"offset": 0.0125`, and a new body has no `target`.
- **Malformed data** is rejected with its JSON path:
  - `objects[4].data.sections: missing required field` and `…sections:
    expected an array`;
  - `…sections[0]: expected an object`;
  - `…sections[1].sketch: missing required field` and `…sections[1].offset:
    missing required field`;
  - `…sections[0].offset: expected a number`;
  - `…sections[1].offset_parameter: expected an ID (a non-negative
    integer)`;
  - `…sections[0].twist: unknown field` and `…data.guides: unknown field`;
  - `…interpolation: unknown value 'smooth'` and `…interpolation: missing
    required field`.
- **Invalid content** is reported at the loft with the definition's own
  rules (InvalidArgument):
  - "a loft needs at least two sections, got 1";
  - "section 2 repeats section 1: the same sketch at the same offset";
  - "section 1 needs a sketch";
  - "a cut feature needs a target feature".
- **No lofted geometry is serialized.**

**Result: PASS.**

## Undo / Redo

`LoftFeature_UndoRedoRestoresGeometry` starts from the frustum model
without its loft.

**Commands on the loft: bit for bit.**

1. **Create** "Loft" (`CreateLoftCommand`, "Create loft 'Loft'"): 1750π.
2. **Add** a crown sketch (r 3 at z = 45).
3. **Modify** the loft to three sections (`ModifyLoftCommand`):
   6267.47734391163976 mm³, against frustum(10, 5, 30) + frustum(5, 3, 15)
   = 6267.47734391163704.
4. **Undo and redo through every state.** Each step compares:
   - document equivalence;
   - the definition (the `FeatureId` and the section IDs in order);
   - a bit-identical volume;
   - an identical centre of mass and bounds.
5. **Undo everything:** the loft is gone and the document equals the
   initial one.
6. **Redo:** the loft comes back with the same ID and bit-identical
   geometry.
7. **Kind check:** `ModifyLoftCommand` on a sketch fails with NotFound.

**Parameter edits** (top radius 5 → 8 mm, then height 30 → 60 mm), with the
analytic volume checked at every state:

- after the edits: 7665.48607475909557 mm³ (frustum(10, 8, 30)), then
  15330.97214951819115 (frustum(10, 8, 60));
- undo, undo, redo, redo: 7665.48607475909557, 5497.78714378213863,
  7665.48607475909557 and 15330.97214951819115;
- the parameters are restored exactly (r2 = 5 mm after the undos).

The test compares these within 1e-12 relative, because undoing a
sketch-driving parameter re-solves the sketch (as in Sweep). In this run
the printed values of each state are identical.

**Result: PASS.**

## STEP

`LoftFeature_ExportsStep`, read back with the kernel:

| Model | Solids | Volume (mm³) | Other |
| --- | --- | --- | --- |
| Circular frustum (`PRODUCT('Loft','Loft'`) | 1, valid | 5497.78714377671440 (expected 1750π; 9.9e-13) | area 1825.91623760133007 (6.1e-13); bounds ±10.0000001, z −1e-7 to 30.0000001 |
| Three-section loft | 1, valid | 18325.95714594022502 (expected 17500π/3; 1.3e-14) | z from −1e-7 to 100.0000001, x max 10.0000001 |
| Block with the tapered hole | 1, valid | 98240.70811398864316 (expected 100000 − 560π; 1.1e-14) | |

Tolerances: 1e-9 relative (STEP stores decimal text) and 1e-6 mm for
bounds. **Result: PASS.**

## STL

`LoftFeature_ExportsClosedStl`, at 0.01 mm deflection. Each mesh is checked
with the independent tools in `MeshAnalysis.hpp`:

- **closed and consistently oriented:** every edge is shared by exactly two
  triangles, in opposite directions;
- **finite** vertices;
- **enclosed volume:** positive, and within deflection × area of the
  B-Rep's.

| Model | Format | Closed | Meshed volume (mm³) | Exact (mm³) | Difference | Bound |
| --- | --- | --- | --- | --- | --- | --- |
| Circular frustum | binary | yes | 5490.9875995993789 | 5497.7871437821377 | 6.80 | 18.26 |
| Circular frustum | ASCII | yes | 5490.9875995993789 | 5497.7871437821377 | 6.80 | 18.26 |
| Rectangular frustum | binary | yes | 3500.0 | 3500 | 0 | 16.09 |
| Leaning frustum (shift 20) | binary | yes | 5493.5407056239683 | 5497.7871437821377 | 4.25 | 18.63 |

A positive enclosed volume with consistently oriented triangles means the
facets face outward. **Result: PASS.**

## Determinism

- **Two regenerators** (`LoftFeature_RegenerationIsDeterministic`), on the
  three-section loft and the leaning frustum. They give the same
  regeneration order and bit-identical volume, area, centre of mass, bounds
  and topology. The section list is unchanged.
- **Ties under noise** (`Loft_BreaksTiesDeterministically`). The same
  inputs give the same bits, and a tie resolves the same way under ±1e-12
  rad of noise.
- **Section order** is the stored list; nothing sorts it.
- **Save/load and commands on the loft** reproduce geometry bit for bit.
- **The two Release captures agree.** The Release `[loft]` capture was taken
  twice, once in the first qualification attempt (see Qualification) and
  once in the final one. Both record 2885 passed assertions, and the
  B-spline cases give the same values.

**Result: PASS.**

## Diagnostics

Every failure is a `Result` error with a stable code, and nothing is built.
The feature prefixes its name. Section-level causes name the section by its
index and sketch. Rules marked "preflight" are checked before any kernel
call.

| Case | Code | Message (from the tests) | Where |
| --- | --- | --- | --- |
| Too few sections | InvalidArgument | "a loft needs at least two sections, got 1" (definition); "makeLoft: a loft needs at least two sections, got 1" (geometry) | `validate()`; preflight |
| Missing section sketch | NotFound | "object:6 references object:5, which does not exist" | regenerator |
| Section reference that is not a sketch | NotFound | "section 2: sketch:6 is not a sketch in this document" | `resolveLoftSections()` |
| Open profile | FailedPrecondition | "Loft: section 2 (sketch 'Open'): the profile is open: an edge ends at (0, 0) mm without a neighbour" | profile builder |
| Several profiles in a section | FailedPrecondition | "Loft: section 2 (sketch 'Pair'): it has 2 closed profiles; a loft section must be exactly one" | `resolveLoftSections()` |
| A profile with a hole | FailedPrecondition | "Loft: section 2 (sketch 'Ring'): its profile has a hole; loft sections must be single closed profiles" | `resolveLoftSections()` |
| Zero-area loop | InvalidArgument | "Loft: section 2 (sketch 'Flat'): a profile loop encloses no area" | profile builder |
| Self-intersecting loop | Internal | "Loft: makeLoft: section 2: the profile face is invalid (self-intersecting or overlapping loops?)" | profile face |
| Offset driven by an angle | DimensionMismatch | "Loft: section 2 (sketch 'Top'): parameter 'tilt' has dimension angle, not length" | `resolveLoftSections()` |
| Coincident sections | InvalidArgument | "Loft: makeLoft: sections 1 and 2 lie on the same plane: a loft needs its sections apart" | preflight |
| Out of order | InvalidArgument | "Loft: makeLoft: section 3 lies 50 mm behind section 2 along the loft: …" | preflight |
| Non-parallel planes | InvalidArgument | "Loft: makeLoft: section 2 is not parallel to section 1: their planes are 10 deg apart; only sections on parallel planes can be lofted" | preflight |
| Different topology | InvalidArgument | "… sections 1 and 2 cannot be matched: section 1 is a circle and section 2 is 4 lines; …"; "… section 1 has 4 lines and section 2 has 6 lines; …"; "… an arc of section 1 turns by 180 deg where the matching arc of section 2 turns by 270 deg; …"; "… the lines and arcs of sections 1 and 2 do not follow one another in the same order; …" | preflight |
| Holes, non-finite coordinates (geometry level) | InvalidArgument | "makeLoft: section 1 has a hole: loft sections must be single closed loops"; "makeLoft: section 2 is not finite" | preflight |
| A fold (a half disc turned 180°) | InvalidArgument | "Pinch: makeLoft: the loft between sections 1 and 2 folds over itself: its cross-section would lose all its area 50% of the way from section 1 to section 2" | preflight |
| Self-intersection (an L turned 90°) | InvalidArgument | "Twist: makeLoft: the lofted solid would intersect itself: its sides pass through one another between the sections" | kernel self-interference check |
| Invalid definitions | InvalidArgument | "section 2 needs a sketch", "section 2's offset must be finite, got inf mm", "section 1's offset parameter ID must be valid", "section 3 repeats section 1: …", operation/target pairing | `validate()` |

Kernel exceptions are caught at the geometry boundary
(`occt::guardKernelCall`) and returned as `Internal`.

**Validation and CLI** (`LoftFeature_ModelsAreValidatedLikeAnyOther`, `info
and validate describe lofts`):

- **Consistency:**
  - "Taper (object:9): section 2 is Pad (object:6), which is an extrude,
    not a sketch";
  - "Taper (object:9): section 2's offset is driven by tilt (object:10),
    which is an angle, not a length".
- **Missing references:** "Taper (object:9) references object:8, which
  does not exist".
- **Regeneration:** a loft that does not build is reported with its
  message.
- **`bettercad-cli info`:** "object:9  loft     Taper  sections Mouth to
  Tip (offset depth), ruled, cut Pad".
- **`validate`:** reports the tapered block (98240.708 mm³, bounds (0, 0, 0)
  to (100, 50, 20) mm). A loft that does not build is an error, and the
  command exits 1.

**Defensive paths not exercised by a test:**

- a kernel failure in `ThruSections` (`IsDone` false, or an exception);
- a result with no valid solid, or without a finite positive volume;
- a prismatoid mismatch on the committed tree.

No valid input in the test matrix reaches them. The prismatoid check is
exercised by the OCCT-defaults experiment below.

**Result: PASS.**

## Kernel Probe

`kernel-probe/loft_kernel_probe.cpp` calls raw OCCT 8.0.1 on 29 cases, with
4 builders each, one process per run (`kernel-probe/loft-kernel-probe.log`;
none ended with an error). The builders:

| Builder | What it is |
| --- | --- |
| `ruled` | `ThruSections(solid, ruled)`, `CheckCompatibility(false)`: the wires as built by BetterCAD (`makeLoft`'s choice) |
| `ruled-cc` | the same with `CheckCompatibility(true)`, OCCT's default matching |
| `smooth` | `ThruSections(solid, not ruled)` |
| `shifted` | `ruled`, with every later section starting one edge on (a circle's seam turned 90°): a wrong matching |

What the probe showed:

| Case | Finding |
| --- | --- |
| Prisms, cylinders, frustums, pyramidal frustums, prismatoids, hexagons, piecewise circles, a turned square | `ruled` equals the closed form to ≤ 4.6e-16 (the kernel makes cones, cylinders, planes and bilinear B-spline patches); coaxial pac-man shapes to ≤ 7.9e-16 |
| Offset circles; slots, rounded rectangles, pac-man shapes offset or turned, the half disc turned 90° | `ruled` within 1.9e-11 … 6.3e-10 (worst: the half disc, 6.26e-10): B-spline ruled faces. It is the surface, not the integration: `geometry-accuracy-release.txt` shows the integration's own error estimate at 2.3e-16 for the slots (2.27e-10 off) and 2.4e-15 for the offset frustum (2.42e-11 off) |
| 22 of the 23 cases with a reference volume, `shifted` | 5.3% (rounded rectangles) to 57% (the flipped frustum) off, still `valid=1`: the matching decides the solid |
| The half disc turned 90°, `shifted` | the same volume as `ruled` (785.39816388887), but a different solid: its centre of mass is (0.672, −0.672, 15) instead of (1.061, −1.061, 15). A wrong matching can keep the volume, so the volume check alone cannot catch every one; BetterCAD's matching never pairs a line with an arc |
| Three and four circles, `smooth` | 22514.7 against 18326.0, and 9965.9 against 9361.9: not piecewise frustums |
| A frustum whose top circle faces down, `ruled` | 2356.2 against 5497.8, self-check 0: winding must be normalized. `ruled-cc` fixes this case, but the kernel's own matching overrides the correspondence it is given (the bowtie becomes the 12000 prism; the dart becomes 1680, still self-intersecting), and it flips the 45° tie under noise (see below), so it is not used |
| Circle to square, `ruled` | `valid=0`, V = 0; `ruled-cc` builds something of its own (10742.9): different topology is refused |
| A tilted section | valid, no reference volume: non-parallel sections are refused |
| Coincident sections | V = 0, `valid=1`: refused in preflight |
| Sections at z 0, 50, 20 (r 10, 8, 5) | 8723.2 = (4066.67 − 1290)π, the first frustum less the backward one, `valid=1`, self-check 1: refused in preflight |
| A square to itself matched corner to opposite corner (bowtie); a concave dart to itself turned 180°, matched index by index | `valid=1` but self-intersecting (self-check 0): 4000 (= hA/3, the twist law at 180°) and 840. The least-twist matching never pairs a square's corners with opposite ones; the self-interference check refuses such solids (an L turned 90° in `LoftFeature_FailsSafelyOnSelfIntersection`) |

**The post-checks are proven necessary**
(`regression/occt-defaults-postcheck.log`). The adapter was switched to
OCCT's default settings (smooth, `CheckCompatibility(true)`) and the `[loft]`
tests were run. 13 of 48 test cases fail. No wrong solid was committed:

- smooth multi-section lofts are refused by the prismatoid check;
- a re-matched loft is refused by the prismatoid or self-interference
  check;
- the 45° tie flips under 1e-12 rad of noise with the kernel's matching.

The change was then reverted; `OcctSweeps.cpp` was checked by SHA-256
(`58336d2c…`). Afterwards, only a comment in it changed: the measured
B-spline tolerance it states became 6.3e-10. The turned half disc measured
6.3e-10 in the qualification runs, confirmed in the probe (`halfdisc-turn`,
6.26e-10).

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors, from `cmd` under code page 65001.
CTest ran only after a successful build. The run started at 23:04:35
(`qualification-times.txt`). No source, test or CMake file was modified
after 23:04:31: a `find -newer` over `apps`, `include`, `src`, `tests`,
`examples`, `cmake` and the top-level CMake files finds none.

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 257 files compiled | 0 | **674/674 passed** (194.5 s) |
| Release | exit 0 | exit 0, 257 files compiled | 0 | **674/674 passed** (168.6 s) |
| Debug-shared | exit 0 (shared libraries ON) | exit 0, 257 files compiled | 0 | **674/674 passed** (225.1 s) |

**Compiler warnings: 0** in every preset. P11-FEAT-008 compiled
251 files. The 6 new ones are `LoftPlan.cpp`, `LoftFeature.cpp`,
`LoftRegeneration.cpp`, `LoftTests.cpp`, `LoftFeatureTests.cpp` and
`LoftFileTests.cpp`.

The 49 new tests:

- `tests/features/LoftFeatureTests.cpp`: 29;
- `tests/core/geometry/LoftTests.cpp`: 12;
- `tests/io/LoftFileTests.cpp`: 6;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

The Release `[loft]` run records 2885 passed assertions and 0 failed
(`loft-values-release.txt`).

**Repeats.** The related tests were run 5 times each (`ctest -R
"[Ll]oft|[Ss]weep|[Mm]irror|[Pp]attern|[Tt]ransform|[Hh]ole|[Ff]illet|[Cc]hamfer|[Rr]evolve|[Ee]xtrude"
--repeat until-fail:5`). That is 375 tests, the 49 loft tests among them.

| Preset | Tests | Results | Time |
| --- | --- | --- | --- |
| Release | 375/375 passed | 1875 of 1875 runs passed, each test 5 times | 613.5 s |
| Debug | 375/375 passed | 1875 of 1875 runs passed, each test 5 times | 666.6 s |

There were no "Not Run" entries. The word "Failed" appears only in the names
of the `…FailedXSavesAndLoadsUnchanged` tests.

**A first attempt was stopped.** It started at 22:36. Its Debug (159.8 s)
and Release (153.6 s) runs passed 674/674, and its Debug-shared build exited
0. I then stopped it deliberately, before its Debug-shared tests: its Release
capture showed the turned half disc at 6.3e-10, above the worst B-spline
case stated in four comments. `OcctSweeps.cpp`, `LoftTests.cpp` and
`docs/architecture.md` gave 2.3e-10, and `LoftFeatureTests.cpp` gave 2.4e-11.
The comments were corrected, the probe was given that case,
and the whole qualification was run again from clean. Only the second run's
logs are kept here. No code or test logic changed between the two runs.

## Legacy Regression

Compared by test name against `../P11-FEAT-008/ctest-release.log`, which
has 625 entries and 624 distinct names:

- **Every P11-FEAT-008 test** is present in the Debug, Release and
  Debug-shared logs, and **passed** in each. None is missing, failed or not
  run.
- **Duplicate entry.** The one name listed twice there ("Dependency cycles
  are reported and block their dependents") is listed twice here too.
- **New tests.** The new logs have 674 entries and 673 distinct names. The
  49 new names are exactly the loft tests listed above: the 36
  `LoftFeature_*` tests, the 12 `Loft_*` tests and "info and validate
  describe lofts".

**The P0–P11-FEAT-008 regression suite remains green.**

No legacy test was changed: the diff of every existing test file only adds
lines (0 removed). Changes to existing code:

- **`OcctSweeps.cpp`:**
  - `makeLoft()` added;
  - `WireBuilder` gained an optional flag placing circle seams on the
    plane's X axis. It is off by default, so prisms, revolutions and sweeps
    build as before.
- **`Sweeps.hpp`:** `makeLoft()` declared and documented.
- **`Validation.cpp`:** loft consistency checks added (sections are
  sketches; offsets are lengths).
- **Loft support** was added to JSON, the CLI's `info`, the commands, the
  regenerator and `Regeneration.hpp`.

## Known Limitations

- **Same shape only.** Consecutive sections must be both circles, or the
  same lines and arcs in the same cyclic order, with arcs of equal sweep.
  A circle to a polygon, different corner counts, or a side split in two is
  refused.
- **Parallel planes only.** Non-parallel sections are refused, not
  approximated.
- **Ruled only.** No smooth interpolation, guide curves, centreline,
  tangency or curvature end conditions, closed lofts, sections with holes,
  or thin-wall or surface lofts.
- **Ties** in the matching go to the first start of the later section's
  loop, in the profile builder's order (the sketch's entity order). A tie is
  a genuinely ambiguous twist (e.g. a square to the same square turned
  45°); redrawing such a profile from another corner can choose the other
  twist.
- **B-spline sides.** The kernel represents a loft's sides as B-spline
  surfaces, even planar ones. Plane references (holes, face matching) find
  only its end faces. Volumes between non-coaxial or turned arcs and circles
  agree with the closed forms within 6.3e-10 (the representation).
- **The volume check is a guard, not a proof.** It catches every wrong
  kernel result the probe and the OCCT-defaults experiment produced that
  changed the volume. The probe also shows a wrong matching that keeps it (a
  half disc matched line to arc). BetterCAD's own matching never builds
  that, and the kernel is told not to re-match.
- **Undo of a sketch-driving parameter** re-solves the sketch. It is
  compared within rounding (1e-12), as in Sweep. The printed values were
  identical in this run.
- **References.** Sections are sketch IDs. There is no semantic topology
  naming for the lofted body's faces and edges.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `qualification-times.txt`: the start, exit code and time of every step.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, with 11 loft lines and the revolved frustum they are compared
  with. The relative errors are:
  - 0 to 5.8e-16 for lines and coaxial circles;
  - 2.4e-11 for the offset frustum;
  - 2.3e-10 for the slots.
- `loft-values-release.txt`: measured feature-level volumes, areas, bounds,
  positions, errors and messages, as printed by the tests (Release).
  `values.py` is the filter that produced it.
- `kernel-probe/`: the raw-OCCT probe source and its log.
- `regression/occt-defaults-postcheck.log`: the run with OCCT's default
  `ThruSections` settings, which the post-checks refuse.

## Final Result

**PASS.** Every mandatory item is implemented and tested, with its
evidence recorded in this directory. The acceptance cases match their closed
forms:

- equal rectangles: 20000 mm³, to 1.8e-16;
- equal circles: 2500π, to 5.8e-16;
- the circular frustum: 1750π, to 1.65e-16, with the same printed value as
  the revolve (5497.78714378213862801 mm³ for both);
- similar rectangles: 3500 mm³, to 2.6e-16;
- three circles: 17500π/3, to 4.0e-16.

The loft matches the extrude and the revolve. It is ruled only, and its
sections must have the same shape and lie on parallel planes. Everything
else is refused, not approximated.
