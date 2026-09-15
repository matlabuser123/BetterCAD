# P11-FEAT-008 — Sweep Verification

## Status

PASS

Date: 2026-09-15. `main` was at `97d426f` (P11-FEAT-007 Mirror) before this
milestone. Every value below comes from this session's runs of the
qualified tree:

- the Release test output (`sweep-values-release.txt`);
- the kernel accuracy example (`geometry-accuracy-release.txt`);
- the kernel probe (`kernel-probe/`).

## Scope

Every item was checked against the working tree and by tests.
`Sweep_*` tests exercise the geometry layer and `SweepFeature_*` tests the
feature.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Sweep feature (`SweepDefinition`: profile, path, orientation, operation, target) | IMPLEMENTED | `SweepFeature_DefinitionIsValidatedOnCreateAndEdit`; `SweepFeature_DependsOnProfilePathAndTarget` |
| Profile reference (the profile sketch's `SketchId`) | IMPLEMENTED | `SweepFeature_RejectsMissingProfile`; `SweepFeature_SaveLoadPreservesDefinition` |
| Path reference (a sketch's `SketchId` and an ordered list of its entity IDs) | IMPLEMENTED | `SweepFeature_RejectsMissingPath`; `SweepFeature_SaveLoadPreservesDefinition` |
| Straight path | IMPLEMENTED | `SweepFeature_StraightRectangleMatchesAnalyticVolume`; `…StraightCircleMatchesAnalyticVolume`; `…ReversedPathSweepsTheOtherWay` |
| Arc path | IMPLEMENTED | `SweepFeature_QuarterCircleMatchesAnalyticVolume`; `Sweep_ArcsAndCirclesMatchPappus` |
| Closed path (a full circle; a closed chain of lines) | IMPLEMENTED | `SweepFeature_ClosedCircularPathMatchesTorusVolume`; `Sweep_CornersAreMitred` (closed square) |
| Polyline (mitred corners) | IMPLEMENTED | `SweepFeature_PolylinePathProducesValidSolid`; `Sweep_CornersAreMitred` |
| Mixed line/arc path (tangent joints) | IMPLEMENTED | `SweepFeature_MixedLineArcPathProducesValidSolid`; `Sweep_RejectsSolidsThatFoldOrIntersect` (a U-turn) |
| Closed profile → solid (holes included); open profile → refused | IMPLEMENTED | `SweepFeature_RejectsOpenProfile`; `…RejectsInvalidProfiles`; `Sweep_ArcsAndCirclesMatchPappus` (a tube) |
| Orientation policy (follow path: fixed binormal, no twist) | IMPLEMENTED | `Sweep_KeepsTheProfilesOrientation`; `SweepFeature_ReversedPathSweepsTheOtherWay` |
| Profile/path compatibility (the path starts on the profile's plane and leaves it at right angles) | IMPLEMENTED | `SweepFeature_RejectsMisplacedProfile`; `Sweep_RejectsMisplacedProfiles` |
| Path validation (empty, non-finite, degenerate, disconnected, branching, corners at arcs, turning back) | IMPLEMENTED | `SweepFeature_RejectsDisconnectedPath`; `…RejectsZeroLengthSegment`; `…RejectsNonFinitePath`; `…RejectsUnsupportedCorners`; `Sweep_RejectsMalformedPaths`; `Sweep_RejectsCornersItCannotMitre` |
| Folding and self-intersecting sweeps | IMPLEMENTED (a preflight, then the kernel's self-interference check) | `SweepFeature_FailsSafelyOnSelfIntersection`; `Sweep_RejectsSolidsThatFoldOrIntersect` |
| NewBody / Add / Remove / Intersect | IMPLEMENTED | `SweepFeature_NewBody`; `…_Add`; `…_Remove`; `…_Intersect` |
| Geometry validity, with an independent volume check of every result | IMPLEMENTED | `makeSweep()`'s checks; `regression/corner-mode-postcheck.log` |
| Sweep vs Extrude | PASS | `SweepFeature_StraightPathMatchesExtrude`; `Sweep_StraightPathsMatchPrisms` |
| Sweep vs Revolve | PASS | `SweepFeature_TorusMatchesRevolve`; `Sweep_ArcsAndCirclesMatchPappus` (full circle) |
| Regeneration (profile, path, target) and dependency tracking | IMPLEMENTED | `SweepFeature_RegeneratesWhenProfileChanges`; `…WhenPathChanges`; `…WhenTargetChanges`; `…DependsOnProfilePathAndTarget` |
| Atomic failure | IMPLEMENTED | `SweepFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`; `…FailsSafelyOnSelfIntersection` |
| Save/load | IMPLEMENTED | `SweepFeature_SaveLoadPreservesDefinition`; `…FailedSweepSavesAndLoadsUnchanged`; `…DataIsStoredAsTransparentJson`; `…MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo | IMPLEMENTED | `SweepFeature_UndoRedoRestoresGeometry` |
| STEP/STL | IMPLEMENTED | `SweepFeature_ExportsStep`; `SweepFeature_ExportsClosedStl` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | the failure tests above; `SweepFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe sweeps` |
| Determinism | IMPLEMENTED | `SweepFeature_RegenerationIsDeterministic` |
| Guide curves, twist, scale along the path, variable sections, multiple profiles, thin-wall and surface sweeps, corner-transition controls | **NOT IMPLEMENTED** (out of scope for this milestone) | — |
| Non-planar paths (helices, splines, 3D edge chains, model edges); other orientation modes | **NOT IMPLEMENTED** | — |

**Not a wrapped pipe call.** The sweep is a document feature with
persistent references. BetterCAD makes the rules; the kernel only builds
the solid:

1. `SweepFeature` stores the profile sketch, the path sketch with its
   ordered edge IDs, the orientation, the operation and the target. It
   stores no kernel wire or shape.
2. `resolveSweepPath()` rebuilds the path from the sketch at every
   regeneration. It orients the edges head to tail and refuses anything it
   would have to repair.
3. `planSweep()` has no kernel dependency. It checks:
   - the path;
   - the joints;
   - the placement;
   - where the solid would fold, or run out of room at a mitre.

   It then predicts the volume by Pappus's theorem, from the region's exact
   centroid.
4. Only then does the OCCT adapter build the pipe. The result is checked for
   validity, self-interference and the predicted volume. It is then combined
   with the target through the shared operations.

The kernel probe shows why the checks are needed. OCCT's default builders
return solids that pass `BRepCheck` but are wrong: half the volume at an L
corner, or none for a closed square (see Kernel Probe).

## Architecture

```text
SweepFeature (bettercad_features)                   include/bettercad/features/SweepFeature.hpp
  └─ regenerateSweep()                              src/features/sweep/SweepRegeneration.cpp
       ├─ requireProfileSketch() + profileRegions()   shared with extrude and revolve
       ├─ resolveSweepPath(): sketch entities → geometry::PlanarPath (head to tail, nothing repaired)
       ├─ uniteRegionSolids(makeSweep(region, path))  shared: one solid per closed region, united
       └─ combineWithTarget(operation, tool, target)  shared: new body / join / cut / intersect
geometry::makeSweep(region, PlanarPath)             include/bettercad/core/geometry/Sweeps.hpp
  ├─ checkRegion()                                  shared with makePrism() and makeRevolution()
  ├─ detail::planSweep() (no kernel)                src/core/geometry/SweepPlan.{hpp,cpp}
  │    segments → connectivity → joints (smooth | mitred corner) → placement
  │    → fold and mitre-room preflight → expected volume A × (length of the centroid's path)
  ├─ OCCT adapter                                   src/core/geometry/occt/OcctSweeps.cpp
  │    profile face (validity) → spine wire (shared vertices) → for each loop:
  │    BRepOffsetAPI_MakePipeShell, SetMode(path-plane normal), RightCorner, MakeSolid
  │    → the holes' solids subtracted (booleanDifference)
  └─ checks: one valid solid → BRepAlgoAPI_Check (self-interference)
             → finite positive volume → |V − V_Pappus| ≤ 1e-9 × V_Pappus
geometry::regionCentroid(PlanarRegion)              include/bettercad/core/geometry/Profile.hpp (exact, Green's theorem)
```

No OCCT type leaves `occt/`, and `architecture.layering` passes. Features
call only the geometry API; the conceptual `GeometryService` is the function
API of `bettercad_geometry`. `TKOffset` is linked privately by
`bettercad_geometry`.

**Reused, not duplicated:**

- the regenerator, the dependency graph and the commands
  (`CreateFeatureCommand` / `ModifyFeatureCommand` as `CreateSweepCommand` /
  `ModifySweepCommand`);
- the profile builder (closed regions, holes by nesting, open-profile
  diagnostics), `uniteRegionSolids()` and `combineWithTarget()`, as used by
  extrude and revolve;
- the profile-face builder, `checkRegion()` and `booleanDifference()`;
- result bodies, validation, the JSON reader and the CLI's `info` and
  `validate`;
- the arc and side-range helpers of `makeRevolution()`. They moved to the
  private `src/core/geometry/ProfileExtent.hpp` so the planner can share
  them. This was a separate step, verified on its own (577/577) before any
  sweep code.

**New:**

- geometry: `PlanarPath`, `makeSweep()`, `SweepPlan` and `regionCentroid()`;
- features: `SweepFeature.hpp`, `SweepFeature.cpp` and
  `SweepRegeneration.cpp` (`resolveSweepPath()`, `sweepTool()`,
  `regenerateSweep()`), plus the regenerator handler, the commands and the
  validation of sweep references;
- io: the `sweep` JSON mapping;
- CLI: `info` describes sweeps;
- sweep cases in `examples/geometry_accuracy`;
- test fixtures in `tests/support/SweepModels.hpp`;
- `docs/architecture.md`.

**Transactions.** As for every feature, the sweep builds its whole body
before anything is stored. On failure:

- the sweep fails and the regenerator keeps no body for it;
- its inputs and the document are unchanged;
- its dependents are blocked.

Definitions are validated before a command changes the document.

## Profile Representation

- **Reference.** `SweepDefinition::profile` is the `SketchId` of the profile
  sketch. Its closed regions are found by the profile builder that extrude
  and revolve use (outer loops, with holes by nesting). Each region is swept
  and the solids are united.
- **Separate sketches.** The path must be in another sketch. Validation
  refuses the same sketch: "the path must be in another sketch than the
  profile: it leaves the profile's plane at right angles".
- **Position.** The region may lie anywhere in its plane, and each point
  keeps its offset from the path. This is tested with a circle 5 mm outside
  an arc (Curved Sweep Validation) and in the kernel probe with a circle
  10 mm off a straight path.
- **Refused:**
  - an open profile: FailedPrecondition, "Sweep: the profile is open: an
    edge ends at (0, 0) mm without a neighbour";
  - a loop enclosing no area: InvalidArgument, "Sweep: a profile loop
    encloses no area";
  - a self-intersecting loop: Internal, "Sweep: makeSweep: the profile face
    is invalid (self-intersecting or overlapping loops?)";
  - a missing profile sketch: NotFound, "object:6 references object:4,
    which does not exist".
- **Holes** are swept as solids of their own and subtracted. The probe
  showed that one `MakePipeShell` ignores inner loops: the tube came out as
  the full disc, 888.26 mm³ instead of 493.48 mm³.

## Path Representation

- **Stored.** `SweepPath{SketchId sketch; std::vector<EntityId> edges}`, in
  JSON `"path": {"sketch": 8, "edges": [3]}`. Only IDs are stored: no
  `TopoDS_Wire`, `TopoDS_Edge` or shape.
- **Resolved at each regeneration.** `resolveSweepPath()` turns the IDs into
  a `geometry::PlanarPath{plane, segments}`: the path sketch's placement,
  and line, arc and circle segments in its coordinates. The path therefore
  follows the sketch's constraints and parameters, so a length or radius
  parameter moves it.
- **Edges.** Lines, arcs, or one circle, which is a closed path on its own.
  Points are refused, and so is a circle joined with other edges.
- **Order and direction.** The list order is the order of travel; nothing is
  re-sorted.
  - A single line or arc runs from its start to its end (sketch arcs are
    counter-clockwise).
  - A circle starts on the sketch's X axis through its centre and runs
    counter-clockwise.
  - Several edges start from the first edge's end that does not meet the
    second. Each later edge is turned to start where the one before ends.
- **Connectivity.** Ends must coincide within the sketch length tolerance
  (1e-10 m). A gap or a branch fails with InvalidArgument, naming the two
  edges and the gap: "the path is not connected: the edges entity:3 and
  entity:6 do not meet (their nearest ends are 1 mm apart)". Nothing is
  closed, bridged or reordered.
- **Closed paths:** a circle, or a chain whose end is its start.
- **Geometry layer.** `planSweep()` re-checks a bare `PlanarPath` (finite,
  non-degenerate, arcs with both ends on one circle, connected to 1e-10 m),
  so `makeSweep()` is also safe when called directly.

## Orientation Policy

`SweepOrientation::FollowPath` is the only mode, stored as `"follow_path"`.

- **Frame.** The profile is carried by the frame (T, N, B):
  - T is the path's unit tangent;
  - B is the path plane's unit normal, which is constant;
  - N = B × T.
- **No twist.** Every profile point keeps its coordinates along N and B. The
  profile turns with the path in the path plane and never twists about the
  tangent:
  - along a line it translates;
  - along an arc it turns about the arc's axis, exactly as a revolution
    does;
  - around a full circle it gives a solid of revolution.
- **Joints.** Segments meet tangentially (the sine of the angle ≤ 1e-9), or
  two straight segments meet at a corner of less than 180°. A corner is
  mitred: both segments end on the plane that bisects it. A mitred corner
  keeps V = A·L for a profile centred on the path.
- **Placement.** The path starts on the profile's plane (to 1e-10 m) and
  leaves it at right angles (sine ≤ 1e-9), towards either side. The path's
  direction decides the side (`ReversedPathSweepsTheOtherWay`).
- **Deterministic.** No Frenet frame is used: it is undefined along lines
  and flips at inflections. The same inputs give bit-identical solids (see
  Determinism).

**No-twist evidence** (`Sweep_KeepsTheProfilesOrientation`): a 4 × 2 mm
bar, 4 mm radial, swept a quarter turn about R = 50 mm.

| Check | Expected | Actual |
| --- | --- | --- |
| V = A·R·θ | 200π = 628.31853071795865 mm³ | 628.31853071795899 mm³ (5.4e-16) |
| End face at y = 0, facing −Y | 8 mm² | 8.0 mm² |
| End face at x = 0, facing −X | 8 mm² | 8.0 mm² |
| Flat side at z = 1: a quarter annulus 48 … 52 | π/4 (52² − 48²) = 314.15926535897933 mm² | 314.15926535897870 mm² (2.0e-15) |
| Bounds | (0, 0, −1) to (52, 52, 1) | within 1e-9 mm |

A twisted bar would have no planar side on z = 1, and its end faces would
not lie in the planes y = 0 and x = 0.

## Straight Sweep Analytical Validation

`SweepFeature_StraightRectangleMatchesAnalyticVolume` (the primary
acceptance case):

| | |
| --- | --- |
| Profile | 10 × 20 mm rectangle (parameters `width` and `height`), corner fixed at the origin, XY plane |
| Path | one line from the origin up Z (an XZ-plane sketch), 100 mm (parameter `length`) |
| Expected volume | A·L = 200 × 100 = 20000 mm³ |
| Actual volume | 19999.99999999999272 mm³ |
| Absolute error | 7.28e-12 mm³ |
| Relative error | 3.64e-16 |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Area | 6400 mm² exactly (2 × (200 + 1000 + 2000)) |
| Centre of mass | (4.99999999999999822, 9.99999999999999822, 49.99999999999999289) mm; expected (5, 10, 50) |
| Bounds | (0, 0, 0) to (10, 20, 100) mm, within 1e-9 mm |
| Validity | valid, 1 solid; the sweep is the only result body |
| Result | **PASS** |

**Reversed path** (`ReversedPathSweepsTheOtherWay`): the same rectangle
along (0, 0, 0) → (0, 0, −100). V = 19999.99999999999272 mm³, with bounds
(0, 0, −100) to (10, 20, 0) and the centre of mass at z = −50: the solid
lies below the profile.

The geometry-level test (`Sweep_StraightPathsMatchPrisms`) gives
20000.00000000000364 mm³ (1.8e-16), as does `geometry-accuracy-release.txt`.

## Circular Profile Straight Sweep

`SweepFeature_StraightCircleMatchesAnalyticVolume`: a circle r = 5 mm
(parameter `radius`) about the origin, along the same straight 100 mm path.

| | Expected | Actual | Error |
| --- | --- | --- | --- |
| Volume | πr²L = 2500π = 7853.98163397448297 mm³ | 7853.98163397448025 mm³ | 2.7e-12 mm³ (3.5e-16) |
| Area | 2πrL + 2πr² = 1050π = 3298.67228626928272 mm² | 3298.67228626928136 mm² | 4.1e-16 |
| Centre of mass | (0, 0, 50) mm | (−6.4e-16, −7.3e-16, 50) mm | |
| Bounds | (−5, −5, 0) to (5, 5, 100) mm | within 1e-9 mm | |

Tolerance: 1e-12 relative. Validity: valid, 1 solid. **Result: PASS.**

## Curved Sweep Validation

`SweepFeature_QuarterCircleMatchesAnalyticVolume`:

| | |
| --- | --- |
| Profile | circle r = 2 mm (parameter `radius`) about the origin, XY plane |
| Path | an arc of radius R = 20 mm (parameter `bend`) in the XZ plane about (20, 0, 0). It leaves the origin down −Z and turns a quarter turn to (20, 0, −20) |
| Expected volume | Pappus: A·R·θ = 4π × 20 × π/2 = 40π² = 394.78417604357429 mm³ |
| Actual volume | 394.78417604357429 mm³ |
| Absolute error | 0 (bit-identical to the formula evaluated in doubles) |
| Tolerance | 1e-12 relative |
| Area | 40π² + 8π = 419.91691727229261 expected, 419.91691727229266 actual (1.4e-16) |
| Centre of mass | on the arc's bisector, at (20 − k, 0, −k) with k = (2/π)(R + r²/4R) = 12.76422643597 mm. Expected (7.2357735640299925, 0, −12.7642264359700075), actual (7.23577356403127503, −4e-17, −12.76422643596872319) mm: 1.3e-12 mm off |
| End faces | a disc of 4π at the start (z = 0, facing +Z) and at the end (x = 20, facing +X): 12.56637061435917 mm² each; the rim circle r = 2 about (20, 0, −20) with axis X is found |
| Bounds | the kernel's box (−2.0000001, −2.0000001, −22.0000001) to (20.0000001, 2.0000001, 1e-7) mm contains the exact box (−2, −2, −22) to (20, 2, 0) and exceeds it by at most 1e-7 mm (the kernel pads toroidal faces by its confusion tolerance) |
| Validity | valid, 1 solid |
| Result | **PASS** |

**More arcs** (geometry level, `Sweep_ArcsAndCirclesMatchPappus`,
`Sweep_KeepsTheProfilesOrientation`):

| Case | Expected (mm³) | Actual (mm³) | Relative error |
| --- | --- | --- | --- |
| Quarter turn, profile in the XZ plane | 40π² = 394.78417604357429 | 394.78417604357446 | 4.3e-16 |
| Three quarters, clockwise | 60π² = 1184.35252813072293 | 1184.35252813072179 | 9.6e-16 |
| Profile 5 mm outside the arc: πr²(R + 5)θ | 50π² = 493.48022005446791 | 493.48022005446796 | 1.2e-16 |
| Tube r 3/2 (a region with a hole): π(3² − 2²)Rθ | 50π² = 493.48022005446791 | 493.48022005446768 | 4.6e-16 |
| 4 × 2 bar about R = 50 | 200π = 628.31853071795865 | 628.31853071795899 | 5.4e-16 |

**Regenerated arcs** (see Regeneration): r = 3 mm gives 90π² =
888.26439609804220 mm³ exactly (V grows by 9/4 = 2.25, exactly), and
R = 30 mm gives 60π² = 592.17626406536147 mm³ exactly.

## Torus Validation

`SweepFeature_ClosedCircularPathMatchesTorusVolume`: a circle r = 2 mm in
the XZ plane, centred at (R, 0, 0) with R = `bend` = 20 mm. It is swept
around the closed path circle of radius R about the origin in the XY plane.

| | Expected | Actual | Error |
| --- | --- | --- | --- |
| Volume | 2π²Rr² = 160π² = 1579.13670417429739 mm³ | 1579.13670417429717 mm³ | 2.3e-13 mm³ (1.4e-16) |
| Area | 4π²Rr = 160π² = 1579.13670417429739 mm² | 1579.13670417429694 mm² | 2.9e-16 |
| Centre of mass | (0, 0, 0) | (2.1e-14, −3.2e-15, 0) mm | |
| Bounds | ±22, ±22, ±2 mm | ±22.0000001, ±22.0000001, ±2.0000001 mm (padded by 1e-7 mm) | |

Validity: valid, 1 solid. **Result: PASS.**

## Sweep vs Extrude

`SweepFeature_StraightPathMatchesExtrude`: the same profile sketch is
extruded by the same `length` parameter in the same document.

| State | Sweep | Extrude | Comparison |
| --- | --- | --- | --- |
| width 10, length 100 | V 19999.99999999999272, A 6400 | V 19999.99999999999272, A 6400 | identical volume and area; centres of mass differ by 0; identical bounds (0, 0, 0) to (10, 20, 100); 1 solid each |
| width 15, length 60 | V 18000, A 4800 | V 18000, A 4800 | identical; bounds (0, 0, 0) to (15, 20, 60) |

Both follow the parameters, and 18000 = 15 × 20 × 60. At the geometry level
(`Sweep_StraightPathsMatchPrisms`), `makeSweep()` and `makePrism()` give
identical volumes and areas for the rectangle (20000.00000000000364 mm³,
6400 mm²) and for the circle r = 5 (7853.98163397448025 mm³,
3298.67228626928136 mm²). **Result: PASS.**

## Sweep vs Revolve

`SweepFeature_TorusMatchesRevolve`: the torus model, plus a revolve of the
same profile sketch about its Y axis (the global Z axis), for R = 20 and
30 mm.

| R | Sweep V (mm³) | Revolve V (mm³) | Formula 2π²Rr² (mm³) | Sweep vs revolve | Areas |
| --- | --- | --- | --- | --- | --- |
| 20 | 1579.13670417429717 | 1579.13670417429671 | 1579.13670417429739 | 2.9e-16 | identical, 1579.13670417429694 |
| 30 | 2368.70505626144768 | 2368.70505626144586 | 2368.70505626144586 | 7.7e-16 | identical, 2368.70505626144450 |

Both centres of mass lie within 2.5e-14 mm of the origin. Both boxes
contain the exact box (±(R + 2), ±(R + 2), ±2) and exceed it by at most
1e-7 mm. At the geometry level (`Sweep_ArcsAndCirclesMatchPappus`), the full
circle gives 1579.13670417429717 mm³ against `makeRevolution()`'s
1579.13670417429671. **Result: PASS.**

## Polyline / Mixed Path

| Case | Path | Expected | Actual | Relative error | Tolerance |
| --- | --- | --- | --- | --- | --- |
| `SweepFeature_PolylinePathProducesValidSolid` | (0, 0, 0) → (50, 0, 0) → (50, 50, 0), a mitred 90° corner, circle r = 2 in the YZ plane | A·L = 400π = 1256.63706143591730 | 1256.63706143471381 | 9.6e-13 | 1e-9 (see below) |
| `SweepFeature_MixedLineArcPathProducesValidSolid` | 50 mm line, tangent quarter arc R = 20, 50 mm line | 4π(100 + 10π) = 1651.42123747949154 | 1651.42123747949040 | 6.9e-16 | 1e-12 |
| `Sweep_CornersAreMitred`, 135° | two 50 mm legs | 400π | 1256.63706143304057 | 2.3e-12 | 1e-9 |
| `Sweep_CornersAreMitred`, closed square | four 50 mm legs, starting at a corner | 800π = 2513.27412287183461 | 2513.27412286085701 | 4.4e-12 | 1e-9 |
| `Sweep_RejectsSolidsThatFoldOrIntersect`, clear U-turn | line, half circle R 10, line back 20 mm away | 4π(100 + 10π) | 1651.42123747949199 | 2.8e-16 | 1e-12 |

- **Why A·L is exact at a mitre.** For a profile centred on the path, the
  material a mitre removes on the inside of the corner equals what it adds
  on the outside.
- **Why the tolerance is 1e-9 at a corner.** The kernel trims mitred pipes
  where it intersects them numerically, at its 1e-7 mm precision. The
  measured errors are 9.6e-13 to 4.4e-12 (also in
  `geometry-accuracy-release.txt`). The tolerance is
  `kRelApproximatedIntersection` (1e-9), which the project already uses for
  numerically intersected surfaces. Smooth paths stay within 1e-12.
- **Checks on the polyline:**
  - bounds (0, −2, −2) to (52, 50, 2): the outer corner of the mitre reaches
    x = 50 + 2 tan 45°;
  - a 4π disc at the start facing −X and at the end (50, 50, 0) facing +Y
    (12.566370614359167 and 12.566370614359169 mm²);
  - the end rim circle is found;
  - a second regeneration is bit-identical (0x4093a28c59d52e8e).
- **Checks on the mixed path:**
  - the ends face −X at the origin and +Y at (70, 70, 0);
  - the box contains (0, −2, −2) to (72, 70, 2), exceeding it by at most
    1e-7 mm;
  - the resolved path is line, counter-clockwise arc, line, head to tail.
- **Closed square:** bounds (−2, −2, −2) to (52, 52, 2), 1 solid.

**Result: PASS.**

## Feature Operations

All four use the shared `combineWithTarget()` of extrude and revolve. Each
result is valid, with the solid count shown.

**NewBody** (`SweepFeature_NewBody`): the swept tool alone is the result
body. The straight sweep gives 19999.99999999999272 mm³ (1 solid, bounds
(0, 0, 0) to (10, 20, 100), centre (5, 10, 50)). The quarter arc gives
394.78417604357429 mm³ (1 solid). **PASS.**

**Add** (`SweepFeature_Add`, a join onto the 100 × 50 × 20 mm block):

| Case | Expected (mm³) | Actual (mm³) | Relative error | Other checks |
| --- | --- | --- | --- | --- |
| A curved handle: a half-turn tube r = 2 about R = 20, standing on the top face at x = 30 and 70 | 100000 + πr²·Rπ = 100000 + 80π² = 100789.56835208715 | 100789.56835208718 | 2.9e-16 | 1 solid; top z = 20 + R + r = 42 (box padded by 1e-7); the top face loses two discs: 5000 − 8π = 4974.8672587712817 expected, 4974.8672587712826 actual |
| A straight boss: a 10 × 20 mm rectangle on the top face swept 30 mm up | 100000 + 6000 = 106000 | 106000.00000000001 | 1.4e-16 | 1 solid; bounds (0, 0, 0) to (100, 50, 50) |

**PASS.**

**Remove** (`SweepFeature_Remove`, a cut from the block):

| Case | Expected (mm³) | Actual (mm³) | Relative error | Other checks |
| --- | --- | --- | --- | --- |
| A straight channel r = 5 along X at (y, z) = (25, 10), 1 mm beyond both ends | 100000 − πr²·100 = 92146.018366025513 | 92146.018366025542 | 3.2e-16 | 1 solid; the channel opens on both end faces (a circle r 5 at x = 0 and at x = 100); the block's bounds |
| A curved blind groove: a circle r = 2 on x = 40, swept a quarter turn R = 20 in the plane z = 10, wholly inside | 100000 − 40π² = 99605.215823956431 | 99605.215823956474 | 4.4e-16 | a cavity: the outer box is the block's |

**PASS.**

**Intersect** (`SweepFeature_Intersect`): the channel's rod (r = 5, 102 mm
long) intersected with the block leaves the 100 mm inside. Expected
2500π = 7853.9816339744830, actual 7853.9816339744802 (3.5e-16), 1 solid,
bounds (0, 20, 5) to (100, 30, 15). **PASS.**

## Regeneration

Each change regenerates exactly the listed nodes, in order.

**Profile change** (`SweepFeature_RegeneratesWhenProfileChanges`):

| Change | Rebuilt | Expected (mm³) | Actual (mm³) | Stale geometry |
| --- | --- | --- | --- | --- |
| arc sweep: radius 2 → 3 mm | Profile, Sweep | πr²Rθ = 90π² = 888.26439609804220 | 888.26439609804220; the ratio to before is 2.25 exactly | none: box (−3, −3, −23) to (20, 3, 0), padded by at most 1e-7 |
| rectangle: width 10 → 15 mm | Profile, Sweep | 30000 | 30000 | none: bounds (0, 0, 0) to (15, 20, 100) |

**Path change** (`SweepFeature_RegeneratesWhenPathChanges`):

| Change | Rebuilt | Expected (mm³) | Actual (mm³) | Stale geometry |
| --- | --- | --- | --- | --- |
| straight path: length 100 → 150 mm | Path, Sweep | 200 × 150 = 30000 | 29999.99999999999272 | none: bounds up to z = 150 |
| arc: R 20 → 30 mm | Path, Sweep | 60π² = 592.17626406536147 | 592.17626406536147 | the end rim is at (30, 0, −30); none is left at (20, 0, −20) |
| torus: R 20 → 30 mm (drives the path and the profile's offset) | Profile, Path, Sweep | 240π² = 2368.70505626144586 | 2368.70505626144768 (7.7e-16) | |

**Target change** (`SweepFeature_RegeneratesWhenTargetChanges`, the channel
cut):

| Change | Rebuilt | Expected (mm³) | Actual (mm³) |
| --- | --- | --- | --- |
| block height 20 → 30 mm | Pad, Channel | 150000 − 2500π = 142146.01836602553 | 142146.01836602556 (2.0e-16); bounds (0, 0, 0) to (100, 50, 30) |
| channel radius 5 → 4 mm | ChannelProfile, Channel | 150000 − 1600π = 144973.45175425633 | 144973.45175425638 (4.0e-16) |

**Dependencies** (`SweepFeature_DependsOnProfilePathAndTarget`):

- The straight sweep depends on {Profile, Path}. The channel depends on
  {ChannelProfile, ChannelPath, Pad}, and its `target()` is Pad.
- The first regeneration builds {Profile, Path, Sweep}.
- `length` rebuilds {Path, Sweep}, and `width` rebuilds {Profile, Sweep}.
- A regeneration with nothing changed rebuilds nothing.
- The block's height rebuilds {Pad, Channel}.

**Result: PASS** (profile, path and target).

## Failure Cases

Every failure is a `Result` error with a stable code, and nothing is built.
The feature prefixes its name. Rules marked "preflight" are checked before
any kernel call.

| Case | Code | Message (from the tests) | Where |
| --- | --- | --- | --- |
| Open profile | FailedPrecondition | "Sweep: the profile is open: an edge ends at (0, 0) mm without a neighbour" | profile builder |
| Loop enclosing no area | InvalidArgument | "Sweep: a profile loop encloses no area" | profile builder |
| Self-intersecting (bow-tie) loop | Internal | "Sweep: makeSweep: the profile face is invalid (self-intersecting or overlapping loops?)" | profile face |
| Missing profile sketch | NotFound | "object:6 references object:4, which does not exist" | regenerator |
| Missing path sketch | NotFound | "object:6 references object:5, which does not exist" | regenerator |
| Path reference that is not a sketch | NotFound | "path sketch:6 is not a sketch in this document" | `resolveSweepPath()` |
| Path edge not in the sketch | NotFound | "Sweep: the path edge entity:99 does not exist in sketch 'Path'" (no other edge is taken) | `resolveSweepPath()` |
| Disconnected path (A → B, D → E, B ≠ D) | InvalidArgument | "Sweep: the path is not connected: the edges entity:3 and entity:6 do not meet (their nearest ends are 1 mm apart)" | `resolveSweepPath()` |
| Branch (a third edge leaving the first edge's start) | InvalidArgument | "… the edges entity:5 and entity:7 do not meet (their nearest ends are 50 mm apart)" | `resolveSweepPath()` |
| A point as a path edge | InvalidArgument | "Sweep: the path edge entity:1 is a point, not a line, arc or circle" | `resolveSweepPath()` |
| A circle joined with a line | InvalidArgument | "Sweep: the path edge entity:5 is a circle, which is a closed path on its own and cannot be joined with other edges" | `resolveSweepPath()` |
| Zero-length line | InvalidArgument | "the path edge entity:3 has zero length". In a document, the sketch solver refuses the collapsed line first ("sketch 'Stub' is SOLVER_FAILURE: the solution collapses entity:3 to zero size"), and the sweep is blocked with no body | `resolveSweepPath()`; solver |
| Non-finite path coordinates | InvalidArgument | sketches refuse NaN and ±infinity; `makeSweep()` refuses them directly: "makeSweep: path segment 1 is not finite" | sketch; preflight |
| Empty path, zero-radius arc, arc ends off one circle, coincident arc ends, zero-radius circle | InvalidArgument | "makeSweep: the path is empty", "… path segment 1 is an arc of zero radius", "… is an arc whose ends are not on one circle", "… is an arc whose ends coincide (use a circle for a full turn)", "… is a circle of zero radius" | preflight |
| A line meeting an arc at 90° | InvalidArgument | "Sweep: makeSweep: path segments 1 (a line) and 2 (an arc) meet at an angle of 90 deg; only two straight segments may meet at a corner, and an arc must meet its neighbours tangentially" | preflight |
| A path turning back on itself | InvalidArgument | "Sweep: makeSweep: the path turns back on itself where segments 1 and 2 meet" | preflight |
| A leg too short for its mitres | InvalidArgument | "makeSweep: path segment 1 is too short for the mitred corners at its ends: it is 5 mm long, but the profile reaches 10 mm from the path, which uses 10 mm of it" | preflight |
| Profile 5 mm off the path's start | InvalidArgument | "Sweep: makeSweep: the path must start on the profile's plane, but it starts 5 mm from it" | preflight |
| Path leaving the profile at 45° | InvalidArgument | "Sweep: makeSweep: the path must leave the profile's plane at right angles, but it leaves at 45 deg to the plane's normal" | preflight |
| Profile reaching past an arc's centre (r 25 about R 20) | InvalidArgument | "Sweep: makeSweep: the profile reaches 25 mm from the path towards the centre of path segment 1, an arc of radius 20 mm: the swept solid would fold over itself" | preflight |
| A path crossing itself (line, 270° tangent arc, line back across) | InvalidArgument | "Sweep: makeSweep: the swept solid would intersect itself: the path comes back within the profile's reach of itself" | kernel self-interference check |
| Invalid definitions | InvalidArgument | "a sweep needs a profile sketch", "a sweep needs a path sketch", "the path must be in another sketch than the profile: it leaves the profile's plane at right angles", "a sweep path needs at least one edge", "the path's edge IDs must be valid", "entity:3 is listed twice in the path", operation/target pairing | `validate()` on create and edit |

- **The probe on these cases.** For a profile reaching past an arc's centre
  and for a corner with legs too short, OCCT's default builders return
  solids that pass its validity check (see Kernel Probe). The preflight
  catches them first.
- **A fold boundary.** Just inside the centre (r = 19.9 about R = 20) is
  built.
- **A crossing that stays clear.** The same U-turn shape with the legs 20 mm
  apart is built and matches A·L to 2.8e-16.

**Defensive paths not exercised by a test:**

- kernel failures inside `MakePipeShell` (`Build`, `MakeSolid`) and edge
  construction;
- a kernel result with no valid solid, or without a finite positive volume;
- a Pappus mismatch on the committed tree.

No valid definition in the test matrix reaches them. The positive-volume
and Pappus checks were exercised by the corner-mode experiment (see Kernel
Probe).

**Result: PASS.**

## Atomic Evaluation

`SweepFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`, on the
channel model:

- **An invalid edit** (a path with no edges) through `ModifySweepCommand` is
  refused with InvalidArgument. The document is unchanged (`equivalent()`),
  the revision is unchanged and nothing is recorded to undo.
- **A valid edit the geometry cannot take** (the profile set to the block's
  base sketch, which does not sit on the path's start) is recorded. It
  fails only at regeneration: "Channel: makeSweep: the path must start on
  the profile's plane, but it starts 10 mm from it".
  - The channel keeps no body.
  - Regeneration does not change the document.
  - Pad stays up to date with a bit-identical volume.
- **Undo** restores the previous document, and the channel rebuilds
  bit-identical.

**Also:**

- **Fold, then recover** (`FailsSafelyOnSelfIntersection`). At r = 25 the
  sweep fails with no body. The profile sketch itself regenerated fine and
  the path is up to date. Back at r = 2, the sweep rebuilds 394.78417604357503
  mm³ (1.9e-15 from before; the profile is re-solved from r = 25).
- **Delete, then restore** (`RejectsMissingPath`). Deleting the path sketch
  fails the sweep with NotFound. Putting it back with its ID restores the
  same volume, bit for bit.

**Result: PASS.**

## Save / Load

`SweepFeature_SaveLoadPreservesDefinition`. Each model is saved, replaced by
an empty document, loaded and regenerated.

**The channel cut (every field in use).** Checked after loading:

- `equivalent()` documents with identical item IDs and the same
  dependencies for every node;
- the definition equal field for field:
  - the feature ID and name ("Channel");
  - the profile (ChannelProfile's ID);
  - the path sketch (ChannelPath's ID) and its edge IDs in order;
  - `FollowPath`, `Cut`, and the target (Pad's ID);
  - dependencies {7, 8, 6};
- the same regeneration order;
- bit-identical volume, area, centre of mass, bounds and topology for Pad
  and Channel, and 1 solid.

It is still parametric after loading:

- radius 4 mm rebuilds {ChannelProfile, Channel}: 94973.45175425637 mm³
  against 100000 − 1600π = 94973.45175425633;
- height 30 mm rebuilds {Pad, Channel}: 144973.45175425638 mm³.

**Also:**

- **The quarter arc** comes back bit-identical. `bend` 30 mm then rebuilds
  {Path, Sweep}: 592.17626406536147 mm³ = 60π².
- **The line–arc–line path (three edges)** keeps its edge IDs in order and
  its geometry, bit for bit.
- **A failed sweep** (`FailedSweepSavesAndLoadsUnchanged`, r = 25 about
  R = 20) saves and loads unchanged. It fails with the same message and no
  body, then recovers at r = 2 (394.78417604357503 mm³).
- **The JSON** is checked text for text: `{"profile": 7, "path":
  {"sketch": 8, "edges": [3]}, "orientation": "follow_path", "operation":
  "cut", "target": 6}`. A new body has no `target`, and a three-edge path
  lists its edge IDs in the order given.
- **Malformed data** is rejected with its JSON path:
  - `objects[4].data.path: missing required field` and `…path: expected an
    object`;
  - `…path.sketch: missing required field`, `…path.edges: missing required
    field` and `…path.edges: expected an array`;
  - `…path.edges[1]: expected an ID (a non-negative integer)`;
  - `…path.closed: unknown field` and `…data.twist: unknown field`;
  - `…orientation: unknown value 'frenet'` and `…orientation: missing
    required field`.
- **Invalid content** is reported at the sweep with the definition's own
  rules (InvalidArgument):
  - `objects[4].data: a sweep path needs at least one edge`;
  - `… entity:3 is listed twice in the path`;
  - `… the path must be in another sketch than the profile: …`;
  - `… a cut feature needs a target feature`.
- **No swept geometry is serialized.**

**Result: PASS.**

## Undo / Redo

`SweepFeature_UndoRedoRestoresGeometry` starts from the straight model
without its sweep. It has two sections.

**Commands on the sweep: bit for bit.**

1. **Create** "Sweep" (`CreateSweepCommand`, "Create sweep 'Sweep'"):
   19999.99999999999272 mm³.
2. **Add** a 50 mm extruded box (`AddObjectCommand`).
3. **Modify** the sweep to intersect the box (`ModifySweepCommand`, the
   operation and target): 10 × 20 × 50 = 9999.99999999999636 mm³.
4. **Undo and redo through every state.** Each step compares:
   - document equivalence;
   - the definition;
   - a bit-identical volume;
   - an identical centre of mass and bounds.
5. **Undo everything:** the sweep is gone and the document equals the
   initial one.
6. **Redo:** the sweep comes back with the same ID and bit-identical
   geometry.
7. **Kind check:** `ModifySweepCommand` on the profile sketch fails with
   NotFound.

**Parameter edits: exact parameters, geometry within rounding.**

1. `length` 100 → 150 mm: 29999.99999999999272 mm³.
2. `width` 10 → 15 mm: 45000 mm³.
3. **Undo width:** 30000.00000000000728 mm³, against 29999.99999999999272
   recorded (4.9e-16).
4. **Undo length:** `length` is 100 mm exactly, and the volume is
   20000.00000000000364 mm³ against 19999.99999999999272 (5.5e-16).
5. **Redo** both: 30000.00000000000728, then 45000.00000000000728 mm³, with
   bounds (0, 0, 0) to (15, 20, 150).

Centres of mass and bounds match within 1e-9 mm; the largest difference is
1.4e-14 mm.

**Why not bit for bit.** Undo restores the parameter value exactly. The
regenerator then re-solves the sketch, starting from the geometry the edit
left. The solver converges to the same rectangle within rounding, not to the
same bits. This is the sketch solver's behaviour, not the sweep's. The
existing parameter undo tests (for example Revolve's) compare within the
same relative tolerance. Commands on the sweep itself re-solve nothing and
restore every bit.

**Result: PASS.**

## STEP

`SweepFeature_ExportsStep`, read back with the kernel:

| Model | Solids | Volume (mm³) | Area (mm²) | Bounds (mm) |
| --- | --- | --- | --- | --- |
| Straight sweep (`PRODUCT('Sweep','Sweep'`) | 1, valid | 20000.0 (expected 20000) | 6400.0 | (0, 0, 0) to (10, 20, 100) |
| Torus (curved, closed path) | 1, valid | 1579.13670417429807 (expected 160π² = 1579.13670417429739; 4.3e-16) | 1579.13670417429694 | X −22.0000001 to 22, Y −22.0000001 to 22.0000001, Z −2 to 2.0000001 |
| Quarter bend | 1, valid | 394.78417604359640 (expected 40π²; 5.6e-14) | | min z −22.0000001, max x 20 |
| Block with the channel cut | 1, valid | 92146.01836602550 (expected 92146.01836602551; 1.6e-16) | | |

Tolerances: 1e-9 relative (STEP stores decimal text) and 1e-6 mm for
bounds. **Result: PASS.**

## STL

`SweepFeature_ExportsClosedStl`, at 0.01 mm deflection. Each mesh is checked
with the independent tools in `MeshAnalysis.hpp`:

- **closed and consistently oriented:** every edge is shared by exactly two
  triangles, in opposite directions;
- **finite** vertices;
- **enclosed volume:** positive, and within deflection × area of the
  B-Rep's.

| Model | Format | Closed | Meshed volume (mm³) | Exact (mm³) | Difference | Bound |
| --- | --- | --- | --- | --- | --- | --- |
| Torus | binary | yes | 1573.4898124915844 | 1579.1367041742974 | 5.65 | 15.79 |
| Torus | ASCII | yes | 1573.4898124915844 | 1579.1367041742974 | 5.65 | 15.79 |
| Quarter bend | binary | yes | 393.36270136806883 | 394.78417604357429 | 1.42 | 4.20 |
| Line–arc–line | binary | yes | 1645.9355009883163 | 1651.4212374794915 | 5.49 | 16.77 |
| Mitred polyline | binary | yes | 1253.6976178764025 | 1256.6370614359173 | 2.94 | 12.82 |

Each mesh encloses slightly less than the solid, as chords inside curved
faces must. A positive enclosed volume with consistently oriented triangles
means the facets face outward. **Result: PASS.**

## Determinism

- **Two regenerators** (`SweepFeature_RegenerationIsDeterministic`), on the
  line–arc–line sweep and on the torus. They give the same regeneration
  order and bit-identical volume, area, centre of mass, bounds and topology.
- **The mitred polyline** regenerates to the same bits (0x4093a28c59d52e8e).
- **Path ordering** is the stored list. `resolveSweepPath()` never sorts or
  searches, so a path resolves the same way every time.
- **Save/load and commands on the sweep** reproduce geometry bit for bit.
- **Debug and Release give identical numbers.** The `[sweep]` values
  captured from both builds differ only in object addresses, temporary paths
  and document UUIDs: 12 differing lines, none of them a value.

**Result: PASS.**

## Kernel Probe

`kernel-probe/sweep_kernel_probe.cpp` calls raw OCCT 8.0.1 on 22 cases, with
6 builders each, one process per run (`kernel-probe/sweep-kernel-probe.log`).
The builders:

| Builder | What it is |
| --- | --- |
| `pipe` | `MakePipe` (corrected Frenet, OCCT's default) |
| `pipe-cn` | `MakePipe` with a constant normal |
| `shell-t` | `MakePipeShell` with a fixed binormal and transformed corners (the default) |
| `shell-r` | `MakePipeShell` with a fixed binormal and right (mitred) corners |
| `shell-o` | `MakePipeShell` with a fixed binormal and round corners |
| `shell-rh` | `shell-r` with each loop swept on its own and the holes cut: `makeSweep`'s choice |

What the probe showed:

| Case | Finding |
| --- | --- |
| Straight (also reversed), arcs, torus, offset profiles, line–arc–line, a clear U-turn | every builder agrees with the formula to ≤ 5.8e-16 |
| L polyline (A·L = 1256.64) | `pipe`, `pipe-cn`, `shell-t`: 628.32, **half**, yet `valid=1`. `shell-r`: 2.8e-13. `shell-o`: 1.8e-3 (a rounded corner) |
| 135° corner | `pipe`, `pipe-cn`, `shell-t`: 184.03, `valid=1` (the self-check fails). `shell-r`: 4.3e-13 |
| Closed square | `pipe`, `pipe-cn`, `shell-t`: **V ≈ −1.7e-13**, `valid=1`. `shell-r`: 2.3e-12 |
| Tube (a hole) | `shell-t/r/o`: 888.26, the hole ignored. `shell-rh`, `pipe`, `pipe-cn`: 3.5e-16 |
| Profile past an arc's centre | every builder: `valid=1`, only the self-check fails. `makeSweep` refuses it in preflight |
| Legs too short for the mitre | `shell-r/o/rh`: `valid=0`; the others `valid=1` with the self-check failing. Refused in preflight |
| Line meeting an arc at 90° | `shell-r/rh`: `valid=0`. `pipe`, `pipe-cn`, `shell-t`: 628.32 (the first line only, against 4π(50 + 10π) = 1023.1), `valid=1`, self-check passed. Refused in preflight |
| Path crossing itself | every builder: `valid=1`, self-check 0. Caught by `makeSweep`'s self-interference check |
| Hairpin (turning back 180°) | `pipe`, `pipe-cn`, `shell-t`: V ≈ 0, `valid=1`. `shell-r/o/rh` throw `Standard_Failure` (caught; the process exits 1). Refused in preflight |

`shell-rh` is the only builder right on every valid case. The failures that
remain are the ones the preflight or the post-checks refuse.

**The post-checks are proven necessary**
(`regression/corner-mode-postcheck.log`). The adapter was switched to
OCCT's default corner mode (one line) and the `[sweep]` tests were run. Every
wrong solid was refused, and none was committed:

- the L polyline (628.32 against 1256.64) by the Pappus check;
- the 135° corner by the self-interference check;
- the closed square by the positive-volume check.

The change was then reverted, and `OcctSweeps.cpp` was checked by SHA-256
(282f28310490099a643d4e32396fe57f843beb7a6acc0597e0eef698183906d2) before
the qualification started.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors, from `cmd` under code page 65001.
CTest ran only after a successful build. The qualification ran from
20:26:14 to 20:59:55 (`qualification-times.txt`). No source, test or CMake
file changed after it started (checked by modification time over 284
files). The newest is `OcctSweeps.cpp` at 20:22:02: the restore after the
corner-mode experiment, checked by SHA-256.

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 251 files compiled | 0 | **625/625 passed** (152.5 s) |
| Release | exit 0 | exit 0, 251 files compiled | 0 | **625/625 passed** (144.3 s) |
| Debug-shared | exit 0 (shared libraries ON) | exit 0, 251 files compiled | 0 | **625/625 passed** (149.0 s) |

**Compiler warnings: 0** in every preset. P11-FEAT-007 compiled 245 files.
The 6 new ones are `SweepPlan.cpp`, `SweepFeature.cpp`,
`SweepRegeneration.cpp`, `SweepTests.cpp`, `SweepFeatureTests.cpp` and
`SweepFileTests.cpp`.

The 48 new tests all passed in every preset:

- `tests/features/SweepFeatureTests.cpp`: 31;
- `tests/core/geometry/SweepTests.cpp`: 9;
- `tests/io/SweepFileTests.cpp`: 6;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

The Release `[sweep]` run records 3011 passed assertions and 0 failed
(`sweep-values-release.txt`).

The related tests were also run 5 times in Release and in Debug, one
process per run (`ctest-repeat-*.log`, `--repeat until-fail:5`). "Related"
means every test whose name mentions a sweep, mirror, pattern, transform,
hole, fillet, chamfer, revolve or extrude: 326 tests. They include:

- the Mirror, Linear and Circular Pattern suites;
- every blend test (the OCCT ChFi3d crash guard);
- all 48 sweep tests.

Every run passed: 1630/1630 in Release (414.5 s) and 1630/1630 in Debug
(423.2 s). No log contains a failure or "Not Run". The 70 lines matching
"Failed" in each are the names of the seven `…_Failed…SavesAndLoadsUnchanged`
tests.

**An environment note** (not part of the qualification). Git Bash's
console uses code page 437, under which `cli.new.unicode-path` fails. This
was seen in P11-FEAT-007 and is unrelated to the sweep. The qualification
therefore runs under code page 65001, as every earlier qualification did.

## Legacy Regression

P0–P11-FEAT-007: **PASS**. All 577 tests of the P11-FEAT-007
qualification pass in all three presets. This was checked by name against
`../P11-FEAT-007/ctest-release.log`: each of its 576 distinct names (two
older test cases share a name) appears as "Passed", and only as "Passed", in
each new log. The 48 new names are exactly the tests listed under
Qualification.

No legacy test was changed: the diff of every existing test file only adds
lines (0 removed). Changes to existing code:

- **`Profile.cpp` / `Profile.hpp`:**
  - `regionCentroid()` added;
  - `arcSweep()` and the side-range helpers moved to `detail`, shared
    through the private `ProfileExtent.hpp`, with unchanged arithmetic.
- **`OcctSweeps.cpp`:**
  - `makeSweep()` added;
  - `makePrism()` and `makeRevolution()` unchanged apart from using the
    moved helpers.
- **`Validation.cpp`:**
  - sweep consistency checks added;
  - the missing-reference loop over sketch entities now handles revolve axis
    lines and sweep path edges. The revolve branch is the same code, and its
    tests pass.
- **Sweep support** was added to JSON, the CLI's `info`, the commands, the
  regenerator and `Regeneration.hpp`.
- **`TKOffset`** is linked privately by `bettercad_geometry`.

## Known Limitations

- **Planar paths only.** The path is one sketch's lines, arcs and circles:
  no helices, splines, 3D edge chains or model edges.
- **Joints.** Only two straight segments may meet at a corner, which is
  mitred. Arcs must join tangentially. There are no rounded or other corner
  transitions.
- **One orientation mode** (follow path, fixed binormal). No twist, scale,
  guide curves, variable or multiple sections, thin-wall or surface sweeps.
- **Placement.** The profile must lie on the plane where the path starts,
  at right angles to it. It is not moved there automatically.
- **Patterns and mirrors** do not accept a sweep as a feature-scope source.
  Their shared support accepts only extrudes, revolves, holes, chamfers and
  fillets (P11-FEAT-005 to 007), and refuses any other source with
  FailedPrecondition ("a … cannot repeat a sweep"). A body mirror takes any
  feature's body, a sweep's included. Neither behaviour has a
  sweep-specific test; this follows from the code.
- **Undo of a sketch-driving parameter** restores geometry to rounding, not
  bit for bit (see Undo / Redo).
- **References.** Path edges are sketch entity IDs, which are stable. There
  is no semantic topology naming for the swept body's faces and edges; later
  features refer to them geometrically, as before.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `qualification-times.txt`: the start, exit code and time of every step.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, with 14 sweep lines (12 sweeps, plus the prism and the
  revolution they are compared with). Relative errors are 1.2e-16 to
  9.6e-16 on smooth paths, and 9.6e-13, 2.3e-12 and 4.4e-12 at mitred
  corners.
- `sweep-values-release.txt`: measured feature-level volumes, areas,
  bounds, positions, errors and messages, as printed by the tests (Release).
  `values.py` is the filter that produced it.
- `kernel-probe/`: the raw-OCCT probe source and its log.
- `regression/corner-mode-postcheck.log`: the run with OCCT's default
  corner mode, which the post-checks refuse.

## Final Result

**PASS.** P11-FEAT-008 Sweep is implemented and verified in all three
presets with zero warnings:

- a persistent, parametric sweep feature: a profile sketch and a path made
  of another sketch's ordered line, arc and circle entities, with no kernel
  geometry stored;
- straight, arc, closed-circle, mitred-polyline and tangent line/arc paths;
- a deterministic follow-path orientation (fixed binormal, no twist);
- new body, join, cut and intersect;
- BetterCAD's own planning before the kernel. Every result is checked
  after it for validity, self-interference and the Pappus volume. The
  kernel probe and the corner-mode experiment show these checks are needed;
- analytic validation (20000 mm³, 2500π, 40π², the torus 2π²Rr²), and
  cross-checks against Extrude and Revolve;
- regeneration from the profile, the path and the target, atomic failure,
  save/load, undo/redo, STEP and closed STL.

Paths are planar sketches only, with one orientation mode. Guide curves,
twist, scale and multiple sections are not implemented.
