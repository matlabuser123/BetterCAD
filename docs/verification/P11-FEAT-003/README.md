# P11-FEAT-003 — Fillet Verification

## Status

PASS

Date: 2026-09-15. `main` was at `0fbe64d` (P11-FEAT-002 Chamfer) before this
milestone.

## Scope

Every item was checked against the working tree and by tests.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Fillet feature (`FilletDefinition`: target, edges, radius or radius parameter) | IMPLEMENTED | `FilletFeature_DefinitionIsValidatedOnCreateAndEdit`; `FilletFeature_DependsOnItsTargetAndRadiusParameter` |
| Edge references (Chamfer's geometric signatures, reused) | IMPLEMENTED | `Fillet_UnresolvableReferencesAreReportedNotGuessed`; `FilletFeature_RefusesSubstitutionAfterTopologyChange` |
| Constant-radius mode | IMPLEMENTED | all fillet tests |
| Variable radius, setback, corner-transition controls | **NOT IMPLEMENTED** (optional; out of scope) | — |
| Single edge | IMPLEMENTED | `Fillet_SingleStraightEdgeMatchesAnalyticVolume`; `FilletFeature_SingleStraightEdgeMatchesAnalyticVolume` |
| Multiple independent edges | IMPLEMENTED | `Fillet_MultipleIndependentEdgesMatchAnalyticVolume`; `FilletFeature_MultipleAndAdjacentEdgesMatchAnalyticVolume` |
| Adjacent edges (2 and 3 at a vertex, 4 around a face) | IMPLEMENTED | `Fillet_AdjacentEdgesAreBlendedAtTheirCorner`; the feature test above |
| Tangent chain | IMPLEMENTED (the chain is rounded as a whole, by definition) | `Fillet_TangentChainIsRoundedAsAWhole` |
| Concave edge (adds material) | IMPLEMENTED | `Fillet_ConcaveEdgeAddsMaterial` |
| Extruded body (parameter → sketch → extrude → fillet) | IMPLEMENTED | `FilletFeature_RegeneratesAfterUpstreamDimensionChange` |
| Revolved body | IMPLEMENTED | `Fillet_CircularEdgeMatchesPappus`; `FilletFeature_WorksOnRevolvedBody` |
| Geometry validity | IMPLEMENTED | shared result validation; every test checks validity and solid count |
| Radius validation and fit preflight | IMPLEMENTED | `Fillet_RejectsInvalidRequestsBeforeTheKernel`; `Fillet_RefusesRadiiThatDoNotFitBeforeTheKernel`; `Fillet_RefusesEdgesWhereFacesJoinSmoothly` |
| Dependency-driven regeneration, parameter-driven radius | IMPLEMENTED | `FilletFeature_RegeneratesAfterUpstreamDimensionChange`; `FilletFeature_RadiusParameterDrivesTheFillet` |
| Topology-change diagnostics | IMPLEMENTED | `FilletFeature_RefusesSubstitutionAfterTopologyChange` |
| Transactions | IMPLEMENTED | `FilletFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Save/load | IMPLEMENTED | `FilletFeature_SaveLoadPreservesReferencesAndRadius`; `FilletFeature_SaveLoadPreservesCircleReferences`; `FilletFeature_FailedFilletSavesAndLoadsUnchanged`; `FilletFeature_DataIsStoredAsTransparentJson`; `FilletFeature_MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo (create, radius, edge set) | IMPLEMENTED | `FilletFeature_UndoRedoRestoresGeometry`; `FilletFeature_RadiusParameterDrivesTheFillet` |
| STEP/STL | IMPLEMENTED | `FilletFeature_ExportsStep`; `FilletFeature_ExportsClosedStl` |
| Surface character and tangency | IMPLEMENTED (through edges; see below) | `Edges_FaceAngleTellsSharpFromSmoothEdges`; the single-edge and rim tests |
| Determinism | IMPLEMENTED | `Fillet_IsDeterministic`; `FilletFeature_RegenerationIsDeterministic` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `FilletFeature_InvalidInputsFailWithStructuredDiagnostics`; `FilletFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe fillets` |

Not claimed: **semantic topology naming.** Fillet uses the geometric
references of P11-FEAT-002, with the same limits.

## Architecture

The path is the one Chamfer established. The conceptual `GeometryService`
of `ARCHITECTURE.md` is the function API of `bettercad_geometry`:

```text
FilletFeature (bettercad_features)
  └─ regenerateFillet()                               src/features/fillet/FilletRegeneration.cpp
       └─ detail::applyToTargetBody()                 src/features/SolidSupport.cpp
            └─ geometry::filletEdges(body, FilletRequest)    include/bettercad/core/geometry/Fillet.hpp
                 └─ OCCT adapter                      src/core/geometry/occt/OcctFillet.cpp
                      ├─ shared blend code            src/core/geometry/occt/OcctBlend.{hpp,cpp}
                      └─ BRepFilletAPI_MakeFillet     Open CASCADE 8.0.1
```

No OCCT type leaves `occt/`; the layering test enforces this.

**Shared with Chamfer, not duplicated.** Chamfer's adapter was split so both
features use one implementation:

- `OcctBlend`: edge resolution, tangent-chain strips, the fit check, and
  the guarded kernel build with result validation;
- `EdgeMatching.hpp`: `detail::validateEdgeSelection()`;
- `SolidSupport`: `applyToTargetBody()`;
- `FeatureJson.cpp`: the edge-list JSON;
- `tests/support/BlockModel.hpp`: the test fixture.

Chamfer's behaviour and every one of its messages are unchanged; all 32
chamfer tests still pass with their exact-message assertions.

**New:**

- `Fillet.hpp`, `Blends.cpp` (request validation), `OcctFillet.cpp`;
- `FilletFeature.hpp`, `FilletFeature.cpp`, `FilletRegeneration.cpp`;
- the regenerator handler, `CreateFilletCommand` / `ModifyFilletCommand`,
  a validation check (the radius parameter is a length), the JSON mapping,
  and the CLI `info` description;
- `EdgeInfo::faceAngle` in the edge query;
- fillet cases in `examples/geometry_accuracy`;
- `docs/architecture.md`.

**Fillet specifics** in `OcctFillet.cpp`, for each reference:

1. Resolve the reference, with the shared checks.
2. Refuse the edge if its faces join smoothly (face angle below 1e-6 rad):
   there is no corner to round.
3. `Add(r, edge)`.

For every edge of the kernel's tangent chain, the strip width on each face
is r·tan(γ/2), where γ is the largest angle between the two faces' outward
normals over 15 samples along the edge. Faces folding back onto each other
(γ within 1e-6 rad of 180°) are refused. Then come the shared fit check and
the build.

**Transactions.**

- Definitions are validated before a command changes the document.
- The target body is immutable; the fillet is built as a new body, and the
  regenerator stores it only if it is valid.
- A failure leaves the document and the upstream bodies intact and gives
  the fillet no stale body. That is the P8 policy, and it is why no "last
  valid" fillet body is shown as current. Its dependents are blocked. Undo,
  or fixing the input, restores it.

## Edge References

Fillet reuses P11-FEAT-002's `EdgeSignature`: the edge's supporting line or
circle in canonical form, matched within 1e-7 mm and 1e-9 rad. It is never
a kernel index or `TopoDS_Edge`. The shared `resolveBlendEdge()` requires
exactly one matching edge, between two faces, not already in another
reference's chain.

| Upstream change | Behaviour | Tested by |
| --- | --- | --- |
| Width 100 → 120 mm (edge longer, same line) | resolves; V = 119356.19 mm³ | `FilletFeature_RegeneratesAfterUpstreamDimensionChange` |
| Revolve sweep 360° → 270° (rims become arcs of the same circles) | resolves; ¾ of the analytic volume | `FilletFeature_WorksOnRevolvedBody` |
| Height 20 → 30 mm (edge moves beyond the signature) | `NotFound`; the new top edge is **not** used | `FilletFeature_RefusesSubstitutionAfterTopologyChange` |
| Notch depth 10 → 25 mm (edge split in two) | `FailedPrecondition` "ambiguous: 2 edges" | same |
| Notch depth back to 10 mm (edges merged again) | resolves to the one edge; the original volume again | same |
| Turned-part radius 15 → 20 mm (rim circle changes) | `NotFound` | `FilletFeature_WorksOnRevolvedBody` |
| An earlier fillet removed the edge | `NotFound` | `FilletFeature_RefusesSubstitutionAfterTopologyChange` |
| Target missing | `NotFound`; target without a body: `FailedPrecondition` | `FilletFeature_InvalidInputsFailWithStructuredDiagnostics` |

**Limitation.** This is geometric matching, not semantic topology naming. A
reference does not follow an edge whose supporting curve moves. What is
guaranteed:

- a known match regenerates;
- a missing or ambiguous match fails;
- another edge is never chosen.

## Analytical Validation

| | |
| --- | --- |
| Base solid | box 100 × 50 × 20 mm, V0 = 100000 mm³ |
| Selected edge | the top front edge: the line y = 0, z = 20 mm along X |
| Edge length | L = 100 mm |
| Radius | r = 5 mm |
| Formula | V_removed = L r² (1 − π/4) |
| Removed volume | 536.50459150637925 mm³ |
| Expected volume | 99463.495408493618 mm³ |
| Actual volume | 99463.495408493603 mm³ (kernel, release build, `geometry-accuracy-release.txt`) |
| Absolute error | 1.46e-11 mm³ |
| Relative error | 1.46e-16 (the kernel's own estimate: 1.9e-16) |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Result | **PASS** |

The formula applies exactly here. Both ends of the edge meet planar end
faces at right angles, and nothing else is rounded, so the removed region is
the corner area r²(1 − π/4) swept straight along the whole edge.

**Also checked** (release values from `geometry-accuracy-release.txt`):

- Surface area: 16000 − 1000 − 2r²(1 − π/4) + (πr/2)·L =
  15774.6680716 mm².
- Topology: 7 faces and 15 edges.
- **Surface character.** The rounded face meets the top and front faces on
  the lines y = 5 mm and z = 15 mm. It meets the end faces in arcs of radius
  5 mm about (x, 5, 15) mm, each of length π·5/2. These are the boundaries
  of a cylinder of radius r about the line y = 5, z = 15.
- **Tangency (G1).** At both tangent lines the two faces' normals differ by
  less than 1e-9 rad (`EdgeInfo::faceAngle`); at the arcs, 90°.
- The input body is unchanged.

The query layer exposes edges, not face surface types, so the cylinder is
verified through its boundary. The kernel's fillet builder produces the
tangency by construction, and BetterCAD checks it independently at the
boundary edges.

## Multiple-Edge Validation

| Case | Formula (V) | Expected (mm³) | Actual, release (mm³) | Relative error |
| --- | --- | --- | --- | --- |
| 2 independent edges, r = 5 | V0 − 2 L r²(1 − π/4) | 98926.990816987236 | 98926.990816987236 | 0 |
| 2 edges meeting at a vertex, r = 5 | V0 − r²(1 − π/4)(L1 + L2) + r³(5/3 − π/2) | 99207.226905224394 | 99207.226905219242 | 5.19e-14 |
| 3 edges meeting at a vertex, r = 5 | V0 − r²(1 − π/4)(ΣL − 3r) − r³(1 − π/6) | 99108.8677301149 | 99108.8677301149 | 0 |
| concave L edge, r = 5 (adds material) | V0 + r²(1 − π/4)·H, with V0 = 32000 | 32107.300918301276 | 32107.30091830128 | 1.13e-16 |
| 1 edge, r = 0.5 | V0 − L r²(1 − π/4) | 99994.634954084933 | 99994.634954084933 | 0 |

**Derivations** (for an edge meeting its faces at 90°):

- **Two edges at a vertex.** The corner regions of the two edges overlap
  near the vertex. Relative to the two cylinder axes, the overlap is
  ∫₀^r (r − √(r² − s²))² ds = r³(5/3 − π/2). The kernel's result is the
  box less the union of the two regions. This is the least exact case
  (5.2e-14, see below), presumably from the kernel's approximation of where
  the two fillets meet; it is still 20× inside the tolerance.
- **Three edges at a vertex.** The kernel builds a spherical corner. Each
  edge loses its corner area over L − r, and the corner cube r³ keeps only
  an eighth of a ball: r³(1 − π/6) removed.
- **Concave edge.** The L is a 50 × 50 mm square less its 30 × 30 mm
  corner, 20 mm high. The fillet fills the concave corner with the same
  area; convex fillets remove material, concave ones add it.

**Feature-level checks** (within 1e-12, same formulas): the four edges
around the top face (four two-edge corners); the three-edge corner of
`FilletVariants`; and radius and edge-set edits.

## Revolved-Body Validation

A cylinder with R = 15 and h = 40 mm, with its top rim (a circle reference)
filleted at r = 2 mm:

- The corner area A = r²(1 − π/4) has its centroid
  ū = r(10 − 3π)/(3(4 − π)) inside the rim.
- By Pappus, V = πR²h − 2π(R − ū)·A. Expected 28195.840380353548 mm³,
  actual 28195.840380353555 mm³ (relative error 2.58e-16).
- The area uses the torus of the fillet: a quarter circle (arc πr/2, with
  its centroid 2r/π from its centre) swept about the axis.
  A = 5100.94558626 mm², relative error 1.78e-16.
- The fillet meets the top face in a circle of radius R − r and the side in
  a circle at h − r, both tangentially (face angle below 1e-9 rad).

**Feature chain** (`FilletedShaft`: revolve → bore → groove → fillet of
both top rims, R = 15 and b = 5):

- The two rims remove 2π·r²(1 − π/4)·(R + b) per full turn; the centroid
  offsets cancel. This holds within 1e-12 at 360° and after changing the
  sweep to 270° (¾ of it).
- A radius change to 20 mm fails with `NotFound` on the moved rim.
- The circle references survive save/load, with bit-identical geometry.

## Regeneration

Chain: width, length → Base → Pad ← height; Pad → Round ← radius
(`tests/support/FilletModels.hpp`).

- **Width 100 → 120 mm** (`ModifyParameterCommand`):
  - changed: `width`; regenerated, in order: Base, Pad, Round;
  - V = 120·50·20 − 120·r²(1 − π/4) = 119356.19449 mm³, and the bounding
    box reaches x = 120;
  - undo gives back the original volume to rounding level (the sketch is
    solved again from the 120 mm state, as for chamfers).
- **Radius parameter 5 → 2 → 5 → 8 mm:**
  - each change regenerates only Round, and Pad stays up to date;
  - V = 99914.15926535898, 99463.49540849362 and 98626.54824574367 mm³
    for 2, 5 and 8 mm (V0 − L r²(1 − π/4), within 1e-12);
  - returning to 5 mm gives bit-identical geometry, and undo and redo
    through all three steps are bit-identical.

## Topology-Change Behaviour

See the table under Edge References. In every case:

- the fillet fails with its reference number and curve in the message;
- it keeps no body;
- the upstream body is rebuilt and valid;
- the document stays valid;
- the model recovers when the input is restored.

No case substitutes another edge.

## Radius Safety

**Why a preflight.** Measured in pure OCCT, without BetterCAD code
(`kernel-crash/occt_fillet_reproducer.cpp`, `kernel-crash/reproducer.log`):
OCCT 8.0.1's fillet builder, in this GCC 16.1 MinGW build, **crashes the
process (0xC0000005 after 2–4 s) for every radius that does not fit** that
was tried:

- 20 and 25 mm (three runs out of three) on the 20 mm face;
- 1 + 1 mm and 1 + 1.5 mm on a 2 mm plate;
- 26 + 26 mm across a 50 mm face;
- a concave 30 or 31 mm on 30 mm faces;
- 16 mm on the r = 15 rim.

Feasible radii build valid solids up to the limit:

- 19.999 mm on the 20 mm face;
- 0.999 mm next to 1 mm on the 2 mm plate;
- 24.9 + 24.9 mm across 50 mm;
- 29.99 mm concave;
- two- and three-edge corners at 19.99 mm;
- 15 mm on the rim.

This is the P11-FEAT-002 crash class, in the same kernel builder.

**Guaranteed, before the kernel runs:**

| Input | Result |
| --- | --- |
| radius 0, negative, NaN, +∞, −∞ | `InvalidArgument` "the fillet radius must be positive and finite, got …" (definition, request and file) |
| no edges, invalid or duplicate references | `InvalidArgument` |
| missing input body, unresolved or ambiguous edge | `NotFound` / `FailedPrecondition` |
| edge whose faces join smoothly | `FailedPrecondition` "joins its two faces smoothly; there is no corner to round" |
| strip wider than its face (20 and 25 mm on 20 mm; concave 30 mm on 30 mm; 15 and 16 mm on the r = 15 disc) | `FailedPrecondition` "does not fit: its fillet needs … which leaves only …" |
| two strips meeting on a face (1 + 1 mm on 2 mm; 26 + 26 mm on 50 mm; a tangent chain 5.5 mm on a 10 mm stadium) | `FailedPrecondition` "… do not fit together …" |

Every refused case that the kernel crashes on is a regression test.

- **Built, not refused:** 19.9 mm convex and 29.9 mm concave build, with
  analytic volumes.
- **Deliberately conservative:** r = 15 on the r = 15 rim is refused
  although the kernel builds it, because each fillet must leave 0.001 mm.

**Not guaranteed:**

- Fillets interacting through faces other than the two next to the edge
  are not analysed.
- Adjacent strips sharing a vertex are not pair-checked. They were measured
  robust for 2- and 3-edge box corners up to the face limit (19.99 mm).
- The width r·tan(γ/2) is exact for planes and for the planes and cylinders
  of turned parts, and estimated on other surfaces.

For those cases, the kernel's own failure handling and the result
validation (a valid solid with the same solid count and finite positive
volume) remain the protection. There is no universal fillet-radius
validator, and none is claimed.

## Persistence

`FilletFeature_SaveLoadPreservesReferencesAndRadius` works on
`FilletVariants`: Round (radius parameter) → Corner (literal 3 mm, three
edges). The sequence is save → an empty document → load → regenerate, and it
checks:

- `equivalent()` documents and identical item IDs;
- each `FilletDefinition` equal field for field (target `FeatureId`, edge
  signatures, radius, radius parameter);
- the same dependencies for every node, and the same regeneration order;
- bit-identical volume, area, centroid, bounding box and topology for Pad,
  Round and Corner;
- that the model is still parametric: radius 5 → 3 mm regenerates Round and
  Corner to the analytic volume.

Also checked:

- Circle references (`FilletedShaft`) round-trip with bit-identical
  geometry.
- A failed fillet saves and loads unchanged, and recovers.
- The JSON is checked text for text:
  `{"target": 6, "edges": [...], "radius": 0.005, "radius_parameter": 4}`.
- Malformed data is rejected with its path, e.g.
  `objects[2].data.radius: expected a number`, and
  `objects[2].data.mode: unknown field` (a fillet has no mode, and unknown
  fields are refused, not ignored).
- `ExampleModelTests` still re-saves `plate.bcad` byte for byte.

## Undo / Redo

`FilletFeature_UndoRedoRestoresGeometry`:

1. **Create** "Corner", three edges, 3 mm (`CreateFilletCommand`, "Create
   fillet 'Corner'").
2. **Change the radius** to 2 mm; only Corner regenerates.
3. **Change the edge set** to two edges.
4. Undo and redo through these states. Each leaves a document equivalent to
   the recorded one, with the definition and a bit-identical volume.
5. Undo everything: Corner is gone, and Round is the result again.
6. Redo: Corner is recreated with the same ID and a bit-identical volume.

Commands check the feature kind. An invalid edit changes nothing and records
nothing (`FilletFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`).
The radius-parameter path is covered in Regeneration above.

## STEP

`exportStep` of the filleted block writes one product, `PRODUCT('Round',
'Round'`. Read back with the kernel, it is 1 solid and valid, with V and A
within 1e-9 of the closed forms (`kRelStep`: STEP stores decimal text).

## STL

Binary and ASCII, each with a linear deflection of 0.01 mm, parsed
independently:

- closed: every edge shared by exactly two oppositely oriented triangles;
- the enclosed volume is below the exact volume, because the convex fillet
  is approximated by chords inside it;
- the enclosed volume is within 0.01 mm × the surface area of it.

## Diagnostics

The project's convention is `Result`/`Error` with a stable `ErrorCode` and a
message naming the feature, the reference and the cause. The regeneration
report keys each error by the feature's `ObjectId`, and validation names it
as "Round (object:7)". The suggested diagnostic names map to it as follows:

| Suggested | Code | Message (example) |
| --- | --- | --- |
| FilletNoEdges | InvalidArgument | "a fillet needs at least one edge" |
| FilletInvalidRadius | InvalidArgument | "Round: fillet: the fillet radius must be positive and finite, got -2 mm" |
| FilletMissingInput | NotFound / FailedPrecondition | "object:7 references object:6, which does not exist"; "Round: a fillet needs the body of its target feature" |
| FilletEdgeNotFound | NotFound | "Round: fillet: edge reference 1 (line through (0, 0, 20) mm along (1, 0, 0)) matches no edge of the body" |
| FilletAmbiguousEdgeReference | FailedPrecondition | "… is ambiguous: 2 edges of the body lie on it" |
| FilletRadiusTooLarge | FailedPrecondition | "… does not fit: its fillet needs 25 mm on a face next to the edge, which leaves only 20 mm (a fillet must leave at least 0.001 mm)"; "… do not fit together …" |
| FilletUnsupportedGeometry | FailedPrecondition | "… bounds 1 face(s); a fillet needs an edge between two faces"; "… joins its two faces smoothly; there is no corner to round" |
| FilletKernelFailure | FailedPrecondition | "fillet: the kernel cannot build the fillet on this geometry" |
| FilletInvalidResult | Internal | "fillet: the kernel produced an invalid solid" |
| (radius parameter of another dimension) | DimensionMismatch | "Round: …"; validation: "Round (object:7): the radius is driven by tilt (object:8), which is an angle, not a length" |
| (failed target) | — | the fillet is blocked |

**Not exercised by a test.** Three defensive paths have no deterministic
trigger:

- a kernel failure after a passed fit check;
- an invalid kernel result;
- the knife-edge (folded faces) refusal.

The same result validation guards them as for Chamfer.

`bettercad-cli info` describes fillets ("target Pad, 1 edge, radius
radius"). `validate` reports a moved edge as an error and exits 1.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors. CTest ran only after a
successful build. No source, test or CMake file changed after the
qualification started (checked by modification time).

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 220 files compiled | 0 | **395/395 passed** |
| Release | exit 0 | exit 0, 220 files compiled | 0 | **395/395 passed** |
| Debug-shared | exit 0 | exit 0, 220 files compiled | 0 | **395/395 passed** |

**Compiler warnings: 0** in every preset. P11-FEAT-002 compiled 212 files;
the 8 new ones are:

- `Blends.cpp`, `OcctBlend.cpp` and `OcctFillet.cpp` (geometry);
- `FilletFeature.cpp` and `FilletRegeneration.cpp` (features);
- three test files.

The 33 new tests (all passed in every preset):

- `tests/core/geometry/FilletTests.cpp`: 12, of which 1 tests the edge
  face angle;
- `tests/features/FilletFeatureTests.cpp`: 12;
- `tests/io/FilletFileTests.cpp`: 7;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

Every fillet and chamfer test (the 64 matching "fillet" or "chamfer") also
ran 5 times in Release and in Debug, one process per run: 320/320 passes
each (`ctest-repeat-*.log`). This matters because the kernel crash class
depends on memory layout.

## Legacy Regression

P0–P11-FEAT-002: **PASS**. All 362 tests of the P11-FEAT-002 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-002/ctest-release.log`: each legacy test appears as "Passed" in
each new log. That log has 361 distinct names because two older test cases
share a name, and both pass.

Chamfer's 32 tests pass unchanged, including every exact-message
assertion, after its adapter, request validation, regeneration, JSON code
and test fixture moved onto the shared implementations.

No legacy test assertion was changed. New tests were only appended to
`ValidationTests.cpp` and `CliTests.cpp`, and `ChamferBlockModel` now
derives from the shared `BlockModel` with the same object IDs.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: every fillet and chamfer test run 5
  times.
- `kernel-crash/occt_fillet_reproducer.cpp` and
  `kernel-crash/reproducer.log`: the kernel's behaviour without BetterCAD
  code.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including the fillet cases.

## Final Result

**PASS.** P11-FEAT-003 Fillet (constant radius) is implemented and verified
in all three presets with zero warnings:

- single, multiple, adjacent, chained and concave edges, on extruded and
  revolved bodies;
- Chamfer's geometric edge references, with the same fail-never-substitute
  rule;
- a fit preflight that keeps the kernel away from its crashing inputs;
- analytic validation, regeneration, undo/redo, save/load, STEP and STL;
- structured diagnostics.

Variable radius and corner controls are not implemented.
