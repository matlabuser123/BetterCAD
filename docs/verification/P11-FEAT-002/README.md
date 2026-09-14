# P11-FEAT-002 — Chamfer Verification

## 1. Status

PASS

Dates: 2026-09-14 to 2026-09-15. `main` was at `4af7e3e` (P11-FEAT-001
Revolve) before this milestone. The first qualification found a kernel crash
in release; it was diagnosed and fixed, and the final qualification passed
(section 11).

## 2. Scope

Every item was checked against the working tree and by tests; all are
**IMPLEMENTED**.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent `ChamferDefinition` (target, edges, mode, distances, angle, reference side) | IMPLEMENTED | `ChamferFeature_DefinitionIsValidatedOnCreateAndEdit`; `ChamferFeature_DependsOnItsTargetAndDistanceParameter` |
| Equal-distance mode | IMPLEMENTED | `ChamferFeature_SingleEdgeMatchesAnalyticVolume`; `Chamfer_SingleEdgeMatchesAnalyticVolume` |
| Two-distance mode | IMPLEMENTED | `ChamferFeature_TwoDistancesPutTheFirstDistanceOnTheReferenceFace`; `Chamfer_TwoDistancesPutTheFirstDistanceOnTheReferenceFace` |
| Distance-angle mode | IMPLEMENTED | `ChamferFeature_DistanceAngleMeasuresTheAngleFromTheReferenceFace`; `Chamfer_DistanceAngleMeasuresTheAngleFromTheReferenceFace` |
| Edge references (geometric, see limits) | IMPLEMENTED | `Edges_*` and `EdgeSignature_*` tests; `ChamferFeature_EdgeChangedUpstreamFailsInsteadOfSubstituting` |
| Multiple edges (separate, adjacent, closed loop) | IMPLEMENTED | `ChamferFeature_MultipleEdgesMatchAnalyticVolume`; `Chamfer_MultipleSeparateEdgesMatchAnalyticVolume`; `Chamfer_AdjacentEdgesMeetInAMitredCorner` |
| Extrude → Chamfer, Revolve → Chamfer | IMPLEMENTED | the block model; `ChamferFeature_OnRevolvedBodyMatchesPappusAndFollowsTheSweep` |
| Geometry validity | IMPLEMENTED | `chamferEdges` requires one valid solid per input solid with a finite, positive volume; tests check `isValid()`, solid, face and edge counts |
| Dependency graph and regeneration | IMPLEMENTED | `ChamferFeature_RegeneratesWhenTheUpstreamWidthChanges` |
| Topology change: fail, never substitute | IMPLEMENTED | `ChamferFeature_EdgeChangedUpstreamFailsInsteadOfSubstituting`; `Chamfer_UnresolvableReferencesAreReportedNotGuessed` |
| Transactions (failure leaves the model intact) | IMPLEMENTED | `ChamferFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Undo/redo | IMPLEMENTED | `ChamferFeature_UndoRedoRestoresIdenticalGeometry` |
| Save/load | IMPLEMENTED | `ChamferFeature_SaveLoadPreservesDefinitionsAndGeometry`; `ChamferFeature_SaveLoadPreservesCircleReferences`; `ChamferFeature_FailedChamferSavesAndLoadsUnchanged`; `ChamferFeature_DataIsStoredAsTransparentJson`; `ChamferFeature_MalformedDataIsRejectedWithTheJsonPath` |
| STEP/STL export | IMPLEMENTED | `ChamferFeature_ExportsToStepAndStl` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `ChamferFeature_InvalidInputsFailWithStructuredDiagnostics`; `ChamferFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe chamfers` |
| No kernel crash on chamfers that do not fit (fit check; found in qualification, section 11) | IMPLEMENTED | `Chamfer_RejectsOversizedDistance`; `Chamfer_RefusesChamfersThatDoNotFitBeforeTheKernel`; `kernel-crash/` |

Not claimed: **semantic topology naming.** Edge references are geometric
signatures with the documented limits below. The TODO item "Semantic
topology naming" stays unticked under "Later".

## 3. Implementation

**Geometry** (`bettercad_geometry`; Open CASCADE only in `occt/`):

- `include/bettercad/core/geometry/Edges.hpp`, `src/core/geometry/Edges.cpp`,
  `src/core/geometry/EdgeMatching.hpp`:
  - `EdgeSignature`: a reference to an edge by its supporting line (the
    point nearest the origin and the direction) or circle (centre, axis,
    radius), in canonical form;
  - `lineSignature()`, `circleSignature()`, `validate()` and `describe()`;
  - `listEdges(body)` and `findEdges(body, signature)`, which match within
    1e-10 m and 1e-9 rad.
- `include/bettercad/core/geometry/Chamfer.hpp`:
  - `ChamferMode` (`EqualDistance`, `TwoDistance`, `DistanceAngle`);
  - `ChamferRequest`;
  - `validate(request)`;
  - `chamferEdges(body, request)`, with its error contract.
- `src/core/geometry/occt/OcctTopology.hpp`, `OcctEdges.cpp`:
  - edge enumeration with each edge's distinct adjacent faces, via
    `TopExp::MapShapesAndAncestors`, with degenerate edges skipped;
  - edge description (`BRepAdaptor_Curve`);
  - outward face normals at an edge's midpoint.
- `src/core/geometry/occt/OcctChamfer.cpp`: `BRepFilletAPI_MakeChamfer`.
  Each reference must match exactly one edge that bounds two faces:
  - no match is `NotFound`;
  - more than one match is `FailedPrecondition` ("ambiguous");
  - a seam is `FailedPrecondition`.

  Two references to one tangent chain are rejected. The reference face is
  chosen by the reference side, and a side that cannot tell the faces apart
  is an error.

  **Fit check** before `Build()`. Every edge the kernel will chamfer counts,
  including edges it adds along a tangent chain (`maker.NbEdges/Edge`). For
  each such edge and each of its two faces, the chamfer's strip must:
  - stay clear of the face's other edges (those not sharing a vertex with
    the edge), measured with `BRepExtrema_DistShapeShape`;
  - not run across the face: the face must reach farther than the width,
    judged from boundary samples plus interior samples classified with
    `BRepClass_FaceClassifier`;
  - not meet another strip on the same face.

  Each must hold by at least 0.001 mm. The widths:
  - equal distance: d on both faces;
  - two distances: d1 on the reference face and d2 on the other;
  - distance and angle: d and d·tan θ;
  - chained edges: the larger width on both faces.

  Straight-line distances are never longer than distances along a curved
  face, so the check errs towards refusing.

  A kernel failure or exception in `Build()` becomes `FailedPrecondition`;
  any other kernel exception becomes `Internal` (`guardKernelCall`). The
  result must be valid, with the same solid count and a finite, positive
  volume.
- `src/core/geometry/CMakeLists.txt`: links `TKFillet` and `TKG2d`
  (PRIVATE).

**Features** (`bettercad_features`):

- `include/bettercad/features/ChamferFeature.hpp`,
  `src/features/chamfer/ChamferFeature.cpp`: `ChamferFeature` (type
  `"chamfer"`) with its persistent `ChamferDefinition`:
  - `target` (a `FeatureId`);
  - `edges` (`EdgeSignature`s);
  - `mode`;
  - `distance`, or a `distanceParameter` (a `ParameterId`);
  - `distance2`;
  - `angle`;
  - `referenceSide`.

  `validate()` rejects values the mode does not use. `dependencies()` are
  the target and the distance parameter. The feature's identity is its
  `ObjectId`/`FeatureId`; geometry is never stored.
- `include/bettercad/features/Regeneration.hpp`,
  `src/features/chamfer/ChamferRegeneration.cpp`:
  - `resolveChamferDistance()`;
  - `regenerateChamfer()`, which needs a non-empty target body and prefixes
    errors with the feature's name.
- `Regenerator.cpp`: the `"chamfer"` handler, through the existing
  `regenerateSolidFeature<F, Evaluate>` template.
- `FeatureCommands.hpp`: `CreateChamferCommand` and `ModifyChamferCommand`.
- `Validation.cpp`: the distance parameter must be a length. The target
  check, result bodies and exports use `SolidFeature` unchanged.
- `Feature.hpp`: `SolidFeature` now means "may consume a target body" and
  exposes only `target()`. `operation()` stays on Extrude and Revolve, and
  the CLI's operation description was adapted.

**Persistence** (`bettercad_io`): `FeatureJson.cpp`, `ObjectJson.hpp`,
`DocumentJson.cpp`. `"chamfer"` objects are an additive change in `.bcad`
format version 1. The point and direction helpers moved from
`SketchJson.cpp` to be shared. The data is:

- target;
- edges: `{"curve": "line", "point", "direction"}` or `{"curve": "circle",
  "center", "axis", "radius"}`;
- mode: `"equal_distance"`, `"two_distance"` or `"distance_angle"`;
- distance, distance_parameter, distance2, angle (radians) and
  reference_side.

**CLI:** `info` describes chamfers ("target Pad, 1 edge, equal distance
size"); `validate` and the exports handle them through `SolidFeature`.

**Other:** chamfer cases in `examples/geometry_accuracy`, and
`docs/architecture.md`.

### Edge references and their limits

A reference is the edge's supporting curve, never a kernel index or
`TopoDS_Edge`. At each regeneration it is resolved against the target's
current body, and it must match exactly one edge:

| Upstream change | Effect on the reference | Tested by |
| --- | --- | --- |
| Width 100 → 120 mm (edges along X get longer) | still one edge on the line: chamfer regenerates, V = 118500 mm³ | `…RegeneratesWhenTheUpstreamWidthChanges` |
| Revolve sweep 360° → 270° (rims become arcs on the same circles) | still one edge on each circle: V = ¾ of the full part | `…OnRevolvedBodyMatchesPappusAndFollowsTheSweep` |
| Height 20 → 30 mm (the top edge moves to z = 30) | no edge on the line: `NotFound`; the new top edge is **not** taken | `…EdgeChangedUpstreamFailsInsteadOfSubstituting` |
| Notch depth 10 → 25 mm (the notch splits the top edge) | 2 edges on the line: `FailedPrecondition` "ambiguous" | same |
| Turned-part radius 15 → 20 mm (the rim circle changes) | no edge on the circle: `NotFound` | `…OnRevolvedBodyMatchesPappusAndFollowsTheSweep` |
| An earlier chamfer removed the edge | `NotFound` | `…InvalidInputsFailWithStructuredDiagnostics` |

This is geometric matching, not semantic topology naming: a reference does
not follow an edge whose curve moves.

### Transactional semantics

- **Definitions.** They are validated before a command changes the
  document. An invalid `ModifyChamferCommand` leaves the document equivalent,
  at the same revision, and records nothing to undo.
- **Bodies.** `Body` is an immutable handle, and `chamferEdges` builds a new
  shape, so the target body is never modified. The regenerator stores a
  body only when evaluation succeeds.
- **Failures.** A failed chamfer keeps no stale body, its dependents are
  blocked, and regeneration never changes the model (the P8 semantics,
  unchanged). The upstream body stays bit-identical and up to date. Fixing
  the input, or undoing the edit, restores the chamfer.

## 4. Analytical Case

| | |
| --- | --- |
| Base geometry | box 100 × 50 × 20 mm, V0 = 100000 mm³ (`Pad`, extruded from a constrained 100 × 50 mm sketch) |
| Edge | the top front edge: the line y = 0, z = 20 mm along X; L = 100 mm |
| Distance | d = 5 mm, equal distance |
| Formula | V = V0 − ½ d² L |
| Expected | 100000 − ½ · 25 · 100 = **98750 mm³** |
| Actual | 98749.999999999985 mm³ (kernel, release build, `geometry-accuracy-release.txt`) |
| Absolute error | 1.46e-11 mm³ |
| Relative error | 1.47e-16 |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Result | **PASS** |

The tolerance is justified because a chamfer of a box has only planar faces,
which the kernel integrates to rounding level. The kernel's own estimate
here is 1.9e-16, and every chamfer error measured below is at most 1.5e-16.
The 1e-12 figure is the project's existing tight tolerance for exact
surfaces.

Also checked for this case, by `Chamfer_SingleEdgeMatchesAnalyticVolume`
and `ChamferFeature_SingleEdgeMatchesAnalyticVolume`:

- surface area 16000 − 1000 − 25 + 500√2 = 15682.106781186547 mm²
  (relative error 0 on the release build);
- 1 valid solid with 7 faces and 15 edges;
- an unchanged bounding box;
- the chamfer face meets the top and front faces on the lines y = 5 mm and
  z = 15 mm;
- the original edge no longer resolves;
- the input body is unchanged.

Further cases (release build, `geometry-accuracy-release.txt`):

| Case | Formula | Expected V (mm³) | Actual V (mm³) | Relative error |
| --- | --- | --- | --- | --- |
| 1 edge, d = 0.5 | V0 − ½d²L | 99987.5 | 99987.499999999985 | 1.46e-16 |
| 2 separate edges, d = 5 | V0 − 2 · ½d²L | 97500 | 97500 | 0 |
| 2 adjacent edges, d = 5 (mitred corner) | V0 − ½d²(100 + 50) + d³/3 | 98166.666666666672 | 98166.666666666686 | 1.48e-16 |
| two distances 5 / 3 mm | V0 − ½ d1 d2 L | 99250 | 99250 | 0 |
| distance 5 mm, angle 30° | V0 − ½ d (d tan 30°) L | 99278.312163512965 | 99278.312163512979 | 1.47e-16 |
| cylinder r = 15, h = 40, rim d = 2 (Pappus) | πr²h − πd²(r − d/3) | 28094.215903502321 | 28094.215903502321 | 0 |

The feature tests also assert the following within 1e-12:

- all four top edges: V0 − ½d²(2·100 + 2·50) + 4·d³/3;
- the turned part (revolve, bore, groove) with both top rims chamfered:
  (s/360) · [π(r² − b²)h − groove] − (s/360) · πd²(r + b), for sweeps of
  360° and 270°;
- the three-chamfer chain used for persistence:
  V0 − ½·25·100 − ½·4·2·100 − ½·3·(3 tan 30°)·50 = 98220.0961894 mm³.

## 5. Regeneration

Dependency chain: width, length → Base → Pad ← height; Pad → Edge ← size
(`tests/support/ChamferBlockModel.hpp`).

- **Width 100 → 120 mm** (`ModifyParameterCommand`):
  - changed: `width`; regenerated, in order: Base, Pad, Edge;
  - V = 120·50·20 − ½·25·120 = 118500 mm³ (1e-12), and the bounding box
    reaches x = 120 mm.
  - Undo regenerates the same three and returns to 98750 mm³ within 1e-12.
    It is not bit for bit, because the sketch is solved again from the
    120 mm state and reaches the original corners to rounding level
    (~1e-14 mm, measured).
- **Distance parameter `size` 5 → 3 mm:**
  - regenerated: Edge only; Pad stays up to date;
  - V = 99550 mm³; undo is bit-identical.
- **Revolve → Chamfer:** a sweep change regenerates Turn, Bore, Groove and
  Rims, as analysed above.
- **Topology changes:** see the table in section 3. In every case the
  chamfer fails with the reference's number and curve in the message, keeps
  no body, and the upstream body is rebuilt and valid.

## 6. Persistence

`ChamferFeature_SaveLoadPreservesDefinitionsAndGeometry` saves the block with
all three modes (`ChamferVariants`: Edge → Bevel → Slope), replaces the
document with an empty one, loads the file and regenerates. It checks:

- `equivalent()` documents and identical item IDs;
- each `ChamferDefinition` equal field for field: target `FeatureId`, edge
  signatures, mode, distances, angle, reference side and distance
  parameter;
- the same dependencies for every node, and the same regeneration order;
- bit-identical volume, area and centroid, with identical bounding box and
  topology, for Pad, Edge, Bevel and Slope;
- that the loaded model is still parametric: `size` 5 → 3 mm regenerates
  Edge, Bevel and Slope to the analytic volume.

Circle references (the turned part's rims) round-trip the same way, with
bit-identical geometry. A **failed** chamfer (height 30 mm) saves and loads
unchanged, fails with the same message, and recovers when the height is
restored. The stored JSON is checked text for text, for example:

```json
"type": "chamfer",
"name": "Edge",
"data": {
  "target": 6,
  "edges": [ { "curve": "line", "point": [0.0, 0.0, 0.02], "direction": [1.0, 0.0, 0.0] } ],
  "mode": "equal_distance",
  "distance": 0.005,
  "distance_parameter": 4
}
```

(pretty-printed one value per line in the file). Malformed data is rejected
with its JSON path. Examples:

- `objects[2].data.mode: unknown value 'round'`;
- `objects[2].data.edges[0].radius: a line reference has no radius`;
- `objects[2].data.edges[0].direction: expected a unit vector`;
- `objects[2].data: edge references 1 and 2 refer to the same line …`;
- `objects[2].data: only a chamfer by two distances takes a second
  distance`.

## 7. Undo / Redo

`ChamferFeature_UndoRedoRestoresIdenticalGeometry` runs on the block model:

1. **Create** "Bevel" (4 mm on the bottom back edge of Edge's result) with
   `CreateChamferCommand`, described as "Create chamfer 'Bevel'". V = 97950
   mm³, and Bevel is the only result body.
2. **Modify** the distance to 2 mm (`ModifyChamferCommand`): only Bevel
   regenerates, V = 98550 mm³.
3. **Undo** (to the state after create), then **redo** (to the state after
   modify). Each leaves a document equivalent to that state, with a
   bit-identical volume.
4. **Undo twice**: Bevel is gone, the document is equivalent to the initial
   one, Bevel has no body, and Edge is the result again.
5. **Redo**: Bevel is recreated with the same ID, and its volume is
   bit-identical.

A modify command aimed at a non-chamfer is `NotFound`. An invalid modify
changes nothing and records nothing to undo
(`ChamferFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`).

## 8. STEP

`exportStep` on the block writes one product, `PRODUCT('Edge','Edge'`. Read
back with the kernel (`tests/support/occt/StepReadBack`), it is 1 solid and
valid, with V = 98750 mm³ and A = 15682.107 mm², both within 1e-9. That
tolerance is `kRelStep`: STEP stores coordinates as decimal text.

## 9. STL

`exportStl` writes binary and ASCII files, each parsed independently
(`tests/support/MeshAnalysis.hpp`):

- **Closed:** every edge is shared by exactly two oppositely oriented
  triangles.
- **Vertices:** 10 distinct: the box's 8 corners, less the chamfered edge's
  2 ends, plus the chamfer face's 4 corners.
- **Volume:** the enclosed volume is 98750 mm³ within 1e-12.
- **Area:** within 1e-12.

1e-12 holds here because every face is planar and every corner lies on a
whole millimetre, which 32-bit floats represent exactly. The mesh is
therefore the solid itself.

## 10. Failure Diagnostics

Every failure is a `Result` error with a code and a message naming the
feature and the cause. Nothing crashed; failed chamfers keep no body, and
their dependents are blocked.

| Input | Code | Message (examples) |
| --- | --- | --- |
| distance 0, negative, NaN, ∞ (definition) | InvalidArgument | "the chamfer distance must be positive and finite, got …" |
| distance parameter 0 or −2 mm | InvalidArgument | "Edge: chamfer: the chamfer distance must be positive and finite, got …" |
| distance too large (25 mm on a 20 mm block; also exactly 20 mm, and d2 = 25 mm in two-distance mode) | FailedPrecondition | "Edge: chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) does not fit: its chamfer needs 25 mm on a face next to the edge, which leaves only 20 mm (a chamfer must leave at least 0.001 mm)" |
| two chamfers that each fit but meet across a face (1.5 + 1.5 mm on a 2 mm face; 26 + 26 mm across a 50 mm face) | FailedPrecondition | "chamfer: edge reference 1 (…) and edge reference 2 (…) do not fit together: their chamfers need 1.5 mm and 1.5 mm on a face they share, where the edges are only 2 mm apart (…)" |
| rim chamfer wider than its disc (16 mm on r = 15 mm) | FailedPrecondition | "… does not fit: its chamfer needs 16 mm on a face next to the edge, which leaves only 15 mm …" |
| tangent chain whose sides meet (5.5 mm on a 10 mm wide stadium) | FailedPrecondition | "chamfer: edge reference 1 (…) and an edge that joins edge reference 1 (…) smoothly do not fit together …" |
| distance parameter that is an angle | DimensionMismatch | "Edge: …"; validation: "Edge (object:7): the distance is driven by tilt (object:8), which is an angle, not a length" |
| missing distance parameter | NotFound | "object:7 references object:4, which does not exist" |
| no edges | InvalidArgument | "a chamfer needs at least one edge" |
| duplicate references (same line, other point and direction) | InvalidArgument | "edge references 1 and 2 refer to the same line through (0, 0, 20) mm along (1, 0, 0)" |
| invalid reference (NaN point, zero radius, other curve) | InvalidArgument | "edge reference 1: an edge reference needs a finite point" |
| unused mode values; missing or superfluous reference side | InvalidArgument | "only a chamfer by distance and angle takes an angle"; "a chamfer by two distances needs a reference side" |
| angle 0, 90°, negative, NaN | InvalidArgument | "the chamfer angle must be in (0, 90) deg, got …" |
| missing target (body) | NotFound | "object:7 references object:6, which does not exist" |
| target without a body (a sketch) | FailedPrecondition | "Edge: a chamfer needs the body of its target feature"; validation: "…the target is Base (object:5), which is a sketch, not a feature with a body" |
| target with an empty body | FailedPrecondition | same message |
| target that failed | — | the chamfer is blocked, with no body |
| reference that matches no edge | NotFound | "Edge: chamfer: edge reference 2 (line through (0, 0, 21) mm along (1, 0, 0)) matches no edge of the body" |
| edge removed by an upstream change | NotFound | "Edge: chamfer: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body" |
| edge split by an upstream change | FailedPrecondition | "… is ambiguous: 2 edges of the body lie on it" |
| seam edge (one face) | FailedPrecondition | "… bounds 1 face(s); a chamfer needs an edge between two faces" |
| reference side that cannot tell the faces apart | FailedPrecondition | "chamfer: the reference side does not tell the two faces of edge reference 1 (…) apart" |
| two references to one tangent chain | InvalidArgument | "chamfer: edge reference 2 (line through (0, 5, 10) mm along (1, 0, 0)) is already chamfered by an earlier reference (the edges join smoothly)" (`Chamfer_FollowsTangentEdgesAndRejectsTwoReferencesToOneChain`, which also checks that one reference chamfers a whole stadium outline to the analytic volume) |

`bettercad-cli validate` reports a moved edge as "error: Edge (object:7)
failed to regenerate: …", with the dependents blocked, and exits 1
(`info and validate describe chamfers`).

**Not exercised by a test.** Two defensive paths have no deterministic
trigger, so no test reaches them:

- a kernel exception outside `Build()`, which `guardKernelCall` turns into
  `Internal`;
- a kernel result that is invalid, or has no finite positive volume, which
  gives `Internal` "chamfer: the kernel produced an invalid solid".

Both are in `OcctChamfer.cpp` and follow the P3 pattern.

## 11. Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors. CTest ran only after a
successful build. No source, test or CMake file changed after the
qualification started (checked by modification time).

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 212 files compiled | 0 | **362/362 passed** |
| Release | exit 0 | exit 0, 212 files compiled | 0 | **362/362 passed** |
| Debug-shared | exit 0 | exit 0, 212 files compiled | 0 | **362/362 passed** |

**Warnings: 0** in every preset. P11-FEAT-001 compiled 204 files; the 8 new
ones are:

- `Edges.cpp`, `OcctEdges.cpp` and `OcctChamfer.cpp` (geometry);
- `ChamferFeature.cpp` and `ChamferRegeneration.cpp` (features);
- three test files.

The 36 new tests (all passed in every preset):

- `tests/core/geometry/ChamferTests.cpp`: 16, of which 4 are edge-query
  tests;
- `tests/features/ChamferFeatureTests.cpp`: 12;
- `tests/io/ChamferFileTests.cpp`: 6;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

Every chamfer test (the 32 matching "chamfer") also ran 5 times in Release
and in Debug, one process per run: 160/160 passes each
(`ctest-repeat-*.log`).

### Attempt 1: a kernel crash in release (fixed)

The first full qualification of the complete P11-FEAT-002 code (361 tests)
passed Debug and Debug-shared with 361/361. **Release failed with 360/361:**
`unit.Chamfer_RejectsOversizedDistance` ended in an access violation. The
log is kept: `ctest-release-attempt1.log` shows "***Exception: SegFault".
That is the stop condition "kernel crash", so work stopped until it was
diagnosed and fixed.

**Diagnosis:**

1. **Reproducible.** The failure repeats deterministically in release:
   0xC0000005 after 2–4 s, 3 runs out of 3. Debug passed in 0.12 s, and
   starting the release binary under gdb made the crash disappear.
2. **Backtrace.** gdb attached to an already-running process gives:
   - `BRepFilletAPI_MakeChamfer::Build` → `ChFi3d_Builder::Compute` →
     `PerformSetOfSurf` → `PerformSetOfKGen` →
     `PerformSetOfSurfOnElSpine`.
   - That function throws `Standard_Failure("PerformSetOfSurfOnElSpine :
     Chaining is impossible.")`. This is OCCT's designed failure path, and
     `Compute` catches it.
   - The process faults inside the Windows exception dispatcher
     (`ntdll!RtlVirtualUnwind2`) while raising that exception. It is the
     only C++ exception thrown before the fault.
3. **Pure OCCT.** A stand-alone OCCT program with no BetterCAD code
   (`kernel-crash/occt_chamfer_reproducer.cpp`, log
   `kernel-crash/reproducer.log`) crashes the same way. The committed copy
   was built at -O2; the investigation's probe, of which it is a cleaned-up
   copy, also crashed at -O0 and -O3.
   - Crash: 25, 20 or 60 mm on the 20 mm face; 1 mm + 1.5 mm on the two
     edges of a 2 mm face.
   - Build: feasible chamfers, up to 19.9999999 mm on the 20 mm face and
     1 mm + 0.9999 mm on the 2 mm face.
   - 1 mm + 0.99999 mm on the 2 mm face crashed in one run and failed
     cleanly in another.
   - Ordinary OCCT exceptions are caught normally, including one thrown
     by the fillet builder itself.
   - The probe linked with a 64 MB stack instead of 2 MB (the same 25 mm
     chamfer) crashed after about 117 s instead of about 3 s, 3 runs out
     of 3.
4. **Ruled out:**
   - BetterCAD's adapter: the pure-OCCT program crashes.
   - Disabled OCCT range checks: the superbuild sets
     `BUILD_RELEASE_DISABLE_EXCEPTIONS=OFF`, and the installed release
     flags are `-O3 -DNDEBUG -s`, without `No_Exception`.
   - C++ exceptions or GCC hot/cold function splitting in general: a
     throw from a `.cold` fragment and OCCT's own exceptions unwind
     normally.

   The exact defect inside OCCT's fillet builder on this toolchain was not
   isolated further. BetterCAD cannot catch it. OCCT's own
   signal-to-exception mechanism, `OSD::SetSignal`, is process-wide: on
   Windows it replaces the unhandled-exception filter and takes over
   Ctrl-C. A library must not do that.
5. **Why Debug passed.** The fault depends on memory layout (a debugger
   changes it, and so does the build), so the Debug pass was luck, not
   correctness.

**Fix:** the fit check (section 3). The kernel is never given a chamfer
that does not fit. The 0.001 mm margin is ten times the smallest remainder
the kernel built near the limit: 1e-4 mm, for two chamfers sharing a 2 mm
face.

**Regression tests:**

- `Chamfer_RejectsOversizedDistance`: the crashing test, now asserting the
  exact message.
- `Chamfer_RefusesChamfersThatDoNotFitBeforeTheKernel`: every crashing case
  from the reproducer, plus a rim wider than its disc, a tangent chain whose
  sides meet, and a just-fitting 19.9 mm chamfer that is built (V0 − ½ ·
  19.9² · 100, within 1e-12).
- `ctest-repeat-release.log` and `ctest-repeat-debug.log`: every chamfer
  test run 5 times, one process per test.

**Limits of the fit check.** It analyses the two faces of each chamfered
edge and the chamfers that share a face. Chamfers interacting through other
faces are not analysed; one example is several chamfered edges meeting at
acute angles at one vertex. For those, the kernel's own failure handling
and the result validation remain the protection. No such case is claimed
as tested.

## 12. Legacy Regression

P0–P11-FEAT-001: **PASS**. All 326 tests of the P11-FEAT-001 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-001/ctest-release.log`: each legacy test appears as "Passed" in
each new log. That log has 325 distinct names because two legacy test
cases share a name, and both pass.

Legacy code touched, with unchanged behaviour:

- `SolidFeature` no longer declares `operation()`. Extrude and Revolve
  keep it, and the CLI now passes the operation explicitly.
- The JSON point and direction helpers moved within `bettercad_io`.
- The `.bcad` format gained the additive `"chamfer"` type. Files without
  chamfers are unchanged: `ExampleModelTests` still re-saves
  `examples/models/plate.bcad` byte for byte.

No legacy test was changed. New tests were only appended to
`ValidationTests.cpp` and `CliTests.cpp`.

## 13. Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-release-attempt1.log`: the first release run, with the kernel
  crash (section 11).
- `ctest-repeat-{release,debug}.log`: every chamfer test run 5 times.
- `kernel-crash/occt_chamfer_reproducer.cpp` and
  `kernel-crash/reproducer.log`: the crash without BetterCAD code, and the
  kernel's behaviour near the limit.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including the chamfer cases.

## 14. Final Result

**PASS.** P11-FEAT-002 Chamfer is implemented and verified in all three
presets with zero warnings:

- all three modes (equal distance, two distances, distance and angle);
- geometric edge references with documented limits (not semantic topology
  naming);
- analytic validation;
- regeneration, undo/redo, save/load, STEP and STL;
- structured diagnostics.

The kernel crash found during qualification is prevented by the fit check,
and it has a regression test.
