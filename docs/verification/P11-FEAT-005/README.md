# P11-FEAT-005 — Linear Pattern Verification

## Status

PASS

Date: 2026-09-15. `main` was at `56d4f8e` (P11-FEAT-004 Hole) before this
milestone.

## Scope

Every item was checked against the working tree and by tests.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Linear Pattern feature (`LinearPatternDefinition`) | IMPLEMENTED | `LinearPattern_DefinitionIsValidatedOnCreateAndEdit`; `LinearPattern_DependsOnItsSourceAndParameters` |
| Stable source reference (the source's `FeatureId`) | IMPLEMENTED | `LinearPattern_ReusesStableSourceReference`; `LinearPattern_SaveLoadPreservesDefinition` |
| Direction (any finite, non-zero vector, normalized when used) | IMPLEMENTED | `LinearPattern_NormalizesDirection`; `LinearPattern_ReverseDirectionWorks`; `LinearPattern_RejectsZeroDirection` |
| Count (the source included; literal or a dimensionless parameter holding a whole number) | IMPLEMENTED | `LinearPattern_CountIncludesOriginal`; `LinearPattern_RejectsZeroCount`; `LinearPattern_RegeneratesWhenCountChanges` |
| Spacing (unit-safe `Length`, literal or a length parameter) | IMPLEMENTED | `LinearPattern_RejectsInvalidSpacing`; `LinearPattern_RegeneratesWhenSpacingChanges` |
| One-direction pattern | IMPLEMENTED | all pattern tests |
| Two-direction (rectangular) pattern (preferred) | IMPLEMENTED | `LinearPattern_TwoDirectionGridHasExpectedInstanceCount` |
| Symmetric pattern, total-length mode (optional) | **NOT IMPLEMENTED** | — |
| Deterministic instance placement and order | IMPLEMENTED | `LinearPattern_SingleDirectionPlacesInstancesExactly`; `LinearPattern_DoesNotAccumulateTransformDrift`; `LinearPattern_RegenerationIsDeterministic` |
| Body pattern (a new-body extrude or revolve) | IMPLEMENTED | `LinearPattern_NonOverlappingBodiesMatchesAnalyticVolume`; `LinearPattern_TouchingAndOverlappingInstancesFuse`; `LinearPattern_WorksOnRevolvedSource` |
| Additive feature pattern (join extrude) | IMPLEMENTED | `LinearPattern_AdditivePatternMatchesAnalyticVolume` |
| Subtractive feature pattern (hole, cut extrude) | IMPLEMENTED | `LinearPattern_HolePatternMatchesAnalyticVolume`; `LinearPattern_SubtractiveExtrudePatternMatchesAnalyticVolume` |
| Pattern of Chamfer and Fillet | IMPLEMENTED (by exactly moved edge references; see Reference Safety) | `LinearPattern_PatternsChamferAndFilletByTranslatedReferences` |
| Geometry validity | IMPLEMENTED | each instance through its feature's own checks; the final body is validated |
| Parameter-driven regeneration (source, count, spacing, direction) | IMPLEMENTED | `LinearPattern_RegeneratesWhenSourceChanges`; `…WhenCountChanges`; `…WhenSpacingChanges`; `LinearPattern_UndoRedoRestoresGeometry` |
| Atomic failure, instance-specific diagnostics | IMPLEMENTED | `LinearPattern_FailsAtomicallyWhenInstanceInvalid`; `LinearPattern_InvalidInputsFailWithStructuredDiagnostics` |
| Count safety (at most 500 instances) | IMPLEMENTED | `LinearPattern_RejectsTooManyInstances` |
| Transactions | IMPLEMENTED | `LinearPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Save/load | IMPLEMENTED | `LinearPattern_SaveLoadPreservesDefinition`; `LinearPattern_SaveLoadKeepsTheDirectionAsGiven`; `LinearPattern_FailedPatternSavesAndLoadsUnchanged`; `LinearPattern_DataIsStoredAsTransparentJson`; `LinearPattern_MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo (create, count, spacing, direction) | IMPLEMENTED | `LinearPattern_UndoRedoRestoresGeometry`; `LinearPattern_RegeneratesWhenCountChanges` |
| STEP/STL | IMPLEMENTED | `LinearPattern_ExportsStep`; `LinearPattern_ExportsStl` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `LinearPattern_ModelsAreValidatedLikeAnyOther`; `info and validate describe linear patterns` |
| Transform infrastructure | IMPLEMENTED | `Transform_*` (5 tests) |
| Pattern of a pattern | **REFUSED** (explicitly unsupported; use a second direction) | `LinearPattern_RefusesUnsupportedSources` |

**Supported source categories**, exactly:

- extrude and revolve features whose operation is new body, join or cut
  (intersect is refused);
- hole features (all types and extents);
- chamfer and fillet features.

Sketches and other patterns are refused. Not claimed: semantic topology
naming, suppressed instances, or SolidWorks-style pattern options.

## Architecture

```text
LinearPatternFeature (bettercad_features)             include/bettercad/features/LinearPatternFeature.hpp
  └─ regenerateLinearPattern()                        src/features/pattern/LinearPatternRegeneration.cpp
       ├─ resolvePatternInstances(): parameters → PatternStep → patternInstances()
       ├─ instanceOperation(source): the source's own operation, resolved once
       │    ├─ extrude/revolve: extrudeTool()/revolveTool() + geometry::translated(Body)
       │    │                   + booleanUnion()/booleanDifference()        (Booleans.hpp, existing)
       │    ├─ hole:            geometry::translated(HoleRequest) + cutHole()
       │    └─ chamfer/fillet:  geometry::translated(EdgeSignature) + chamferEdges()/filletEdges()
       └─ detail::applyToTargetBody()                 src/features/SolidSupport.cpp
            └─ the source's body (instance 0) → instances 1 … N−1 in order → validation
geometry::translated(Body, Translation3D)             include/bettercad/core/geometry/Transform.hpp
  └─ OCCT adapter: BRepBuilderAPI_Transform (copy)    src/core/geometry/occt/OcctTransform.cpp
```

No OCCT type leaves `occt/`; `gp_Trsf` stays in the adapter. The conceptual
`GeometryService` is the function API of `bettercad_geometry`.

**Reused, not duplicated:**

- the regenerator, dependency graph, commands (`CreateFeatureCommand`,
  `ModifyFeatureCommand`), result bodies, validation and the JSON reader;
- the Boolean operations;
- each source's own evaluation. `extrudeTool()` and `revolveTool()` were
  split out of the extrude and revolve regeneration; `resolveChamferRequest()`
  out of the chamfer's. Behaviour and messages are unchanged: all 444 earlier
  tests pass.

**New:**

- core math: `Vector.hpp` (`Vector3D`, `Translation3D`);
- geometry: `Transform.hpp` and `OcctTransform.cpp`, plus `translated()` for
  edge references, face references and hole requests;
- features: `LinearPatternFeature.hpp`, `LinearPatternFeature.cpp`,
  `LinearPatternRegeneration.cpp`, the regenerator handler, the commands, and
  validation of the count (dimensionless) and spacing (length) parameters;
- io: the `linear_pattern` JSON mapping;
- CLI: `info` describes patterns;
- pattern cases in `examples/geometry_accuracy`;
- test fixtures in `tests/support/PatternModels.hpp`;
- bounds in the test-only STEP read-back;
- `docs/architecture.md`.

**Transactions.**

- The pattern builds its whole body before anything is stored.
- The first failing instance fails the pattern; the regenerator keeps no
  body for it (P8 policy), and the source, its body and the document are
  unchanged. Dependents are blocked.
- Definitions are validated before a command changes the document.

## Pattern Semantics

Count includes the original: **YES**. Count 1 is the source alone, 2 the
source and one copy, N the source and N − 1 copies.

Instance formula (one direction):

  p_i = p_0 + i · s · d̂,  i = 0, 1, …, count − 1

where d̂ is the direction vector normalized. With two directions:

  p_(i,j) = p_0 + i · s1 · d̂1 + j · s2 · d̂2

That gives count1 × count2 instances, the source once.

- **Computed from the source for every instance.** Each offset component is
  one product (`Translation3D::along(d̂, s · i)`), never a sum of previous
  offsets.
- **Instance identity.** Instances are not document objects and get no IDs
  of their own. An instance is its pattern's `FeatureId` plus a
  deterministic index, i + j · count1 (0 is the source). `patternInstances()`
  lists them in that order.
- **Operation semantics.** A new-body or join source is united with the
  body, instance by instance; a cut source or hole is subtracted; a chamfer
  or fillet is applied. Overlap and touching follow each source's own rules:
  - New-body instances fuse where they touch or overlap, like the regions of
    one extrude, and stay separate solids where they do not.
  - Join and cut instances may overlap; the boolean result is used and
    validated.
  - Hole instances must each fit on their face (P11-FEAT-004 policy B), so
    overlapping holes are refused.

## Basic Translation Validation

| | |
| --- | --- |
| Source | 10 × 10 × 10 mm cube (a new-body extrude, driven by `size`) |
| Direction | (1, 0, 0) |
| Count | 4 (a dimensionless parameter) |
| Spacing | 20 mm (a length parameter) |
| Expected positions | x = 0, 20, 40, 60 mm |
| Actual | offsets k · 20 mm, bit for bit (`resolvePatternInstances`); one −X face each at x = 0, 20, 40, 60 and one +X face each at 10, 30, 50, 70; none at x = 80 or −20; four top faces |
| Bounding box | (0, 0, 0) to (70, 10, 10) mm (10 + 3 × 20 = 70), within 1e-9 mm |
| Validity | valid, 4 solids |
| Order | index 0 … 3 = step 0 … 3 along the direction |
| Result | **PASS** |

`LinearPattern_BoundingBoxMatchesAnalyticExtent` checks the extent
10 + (count − 1) × 20 for counts 2, 4 and 8 (30, 70 and 150 mm).

## Separate-Body Analytical Validation

| | |
| --- | --- |
| Source volume | 1000 mm³ (10 mm cube) |
| Count | 4 |
| Spacing | 20 mm (disjoint instances) |
| Formula | V = count × V_source |
| Expected | 4000 mm³ |
| Actual | 3999.99999999999909 mm³ (feature, release) |
| Absolute error | 9.1e-13 mm³ |
| Relative error | 2.3e-16 |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Result | **PASS** |

The surface area is 2400 mm² (exact) and the centre of mass is at
x = 35.000000000000007 mm (expected 35).

**Touching and overlapping** (`LinearPattern_TouchingAndOverlappingInstancesFuse`):

- 10 mm apart the cubes touch and fuse into one 40 × 10 × 10 bar: 1 solid,
  V = 4000.0000000000014 mm³, one top face of 400 mm², and no inner faces.
- 5 mm apart they overlap into the union: 1 solid, V = 2500 mm³ (exact).
- Both results are valid, and nothing crashed.

## Hole Pattern Analytical Validation

| | |
| --- | --- |
| Block | 120 × 50 × 20 mm, V0 = 120000 mm³ |
| Hole | 10 mm through hole at (20, 25) in the top face, H = 20 mm: π·5²·20 = 1570.7963 mm³ |
| Count | 5 (a parameter) |
| Spacing | 20 mm along +X (holes at x = 20, 40, 60, 80, 100) |
| Formula | V = V0 − 5 π r² H |
| Expected | 112146.01836602551 mm³ |
| Actual | 112146.01836602551 mm³ (feature and kernel, release) |
| Error | 0 |
| Tolerance | 1e-12 relative |
| Result | **PASS** |

**Also checked:**

- The area is 18800 + 5·(200π − 50π) mm²: actual 21156.194490192342,
  expected 21156.194490192345.
- 1 solid, valid.
- The bore of every hole meets the top and bottom faces, and there is no
  sixth hole.
- The top face's area is 6000 − 125π.
- The source hole's own body is an unchanged intermediate
  (118429.20367320512 mm³); the pattern is the only result body.

## Additive Pattern Validation

A block 100 × 50 × 20 mm with a 10 × 10 × 5 mm boss (a join extrude on
z = 20), patterned 4 times 20 mm apart:

| Case | Expected (mm³) | Actual (mm³) |
| --- | --- | --- |
| separate bosses: V0 + 4 × 500 | 102000 | 102000.00000000001 |
| touching bosses (10 mm apart) | 102000 | 102000.00000000003 |
| overlapping bosses (5 mm apart): V0 + 25 × 10 × 5 | 101250 | 101250.00000000003 |
| the boss as a pocket (cut 5 mm down): V0 − 4 × 500 | 98000 | 98000.00000000007 |

- The separate bosses have four top faces of 100 mm² each at z = 25 (400
  in all).
- Touching bosses fuse into one top face of 400 mm².
- The pockets have four floors at z = 15 (400 mm²).
- Every result is 1 valid solid.

## Transform Drift

| | |
| --- | --- |
| Count | 100 |
| Spacing | 1.25 mm along +X |
| Expected final location | 99 × 1.25 = 123.75 mm |
| Actual (instance offset) | 123.75 mm; every offset equals k × s bit for bit |
| Actual (geometry) | a hundred 1 mm cubes: the last one's −X face at x = 123.75, bounding box max 124.75 mm (exact); 100 solids, V = 100.00000000000065 mm³ |
| Slanted (1, 2, 2) | ‖offset₉₉‖ = 123.74999999999997 mm (2.3e-16 relative) |
| Error | 0 along X; ≤ 2.3e-16 relative on a slant |
| Result | **PASS** |

## Regeneration

**Source changes** (`LinearPattern_RegeneratesWhenSourceChanges`):

| Change | Rebuilt | Result |
| --- | --- | --- |
| hole diameter 10 → 12 mm | Drill, Holes | every hole 12 mm: V = 108690.26644707678 (expected 108690.26644707675) |
| block 20 → 40 mm thick (holes from the bottom face) | Pad, Drill, Holes | every hole still through (exit circles at z = 40): V = 224292.03673205103 (exact) |
| block 120 → 150 mm long | Base, Pad, Drill, Holes | the holes stay where they are: V = 142146.01836602556 (expected 142146.01836602553) |
| chamfer distance 1 → 2 mm (pattern of a chamfer) | Ease, Eases | every rim: V = 111789.97119861867 (exact) |
| revolve sweep 360° → 270° (pattern of a revolve) | Turn and what builds on it, Shafts included | three 270° wedges: V = 63617.251235193304 (expected 63617.251235193311) |

**Count 2, 4, 8** (holes 12 mm apart; `LinearPattern_RegeneratesWhenCountChanges`):

- each change rebuilds only the pattern; the source stays up to date;
- V = 116858.40734641021, 113716.81469282042 and 107433.62938564082 mm³
  (V0 − n·500π; errors ≤ 1.5e-11 mm³);
- exactly n holes each time;
- undo and redo through the three steps give bit-identical volumes.

**Spacing:**

- Cubes 10, 15 and 25 mm apart end at 40, 55 and 85 mm (10 + 3s), with V
  4000 mm³; only the pattern rebuilds.
- Holes 15 mm apart sit at x = 20, 35, 50, 65 and 80.

**Direction** (`LinearPattern_NormalizesDirection`, `…ReverseDirectionWorks`):

- +X, +Y and +Z end at 70 mm along the axis.
- (2, 0, 0) gives the same body as (1, 0, 0).
- (1, 1, 0) steps 20 mm along the diagonal, not 20 mm in X and in Y: the
  bounds end at 10 + 3·20/√2 = 52.426406871192846 mm in X and Y, and the
  last offset is 59.999999999999993 mm long (expected 60). (3, 3, 0) gives
  the same X bound, within 1e-12 mm.
- (−1, 0, 0) places the instances at x = 0, −20, −40, −60; the bounds start
  at −60.

**Revolved source** (`LinearPattern_WorksOnRevolvedSource`): the turned
part's revolve (R = 15, h = 40 mm) three times, 40 mm apart. That gives 3
valid solids with V = 3πR²h: actual 84823.00164692440, expected
84823.00164692441 mm³. The bounds are x = −15 to 95 mm.

**Two-direction grid** (`LinearPattern_TwoDirectionGridHasExpectedInstanceCount`):

- 4 holes along X (20 mm) by 3 along Y (12 mm), from (20, 13).
- There are 12 instances with indices i + 4j and offsets (20i, 12j, 0)
  exactly; the source is instance 0, once.
- V = 101150.44407846121 mm³ (expected 101150.44407846124).
- Each of the 12 holes is at its place, and none at (100, 13) or (20, 49).
- A row count of 1 gives the one-direction result (113716.81469282042).

## Invalid Instance / Atomic Failure

`LinearPattern_FailsAtomicallyWhenInstanceInvalid`, on the hole row:

- **Count 5 → 6.** The sixth hole's centre is on the block's end:
  `FailedPrecondition` "Holes: linear pattern: instance 5 at (100, 0, 0) mm:
  hole: the hole does not fit on its face: its entry is 10 mm across, but
  the centre (120, 25) mm is only … mm from the face's edge". The pattern
  keeps no body: no partial pattern of five holes is shown. The document is
  unchanged, and the source hole and its body are untouched. Count 5 again
  gives the original volume, bit for bit.
- **Spacing 20 → 25 mm.** Instance 4 at (100, 0, 0) mm fails the same way.
- **Spacing 8 mm.** The second hole would cut into the first one's rim:
  "instance 1 at (8, 0, 0) mm: hole: the hole does not fit on its face …
  the centre (28, 25) mm is only 3 mm from the face's edge".

Pattern of a chamfer with one instance too many: `NotFound` "Eases: linear
pattern: instance 5 at (100, 0, 0) mm: chamfer: edge reference 1 (circle
around (120, 25, 20) mm with axis (0, 0, 1) and radius 5 mm) matches no
edge of the body". No other edge is chosen, and the chamfer's own body is
kept.

## Reference Safety (Chamfer, Fillet, Hole)

A patterned hole, chamfer or fillet is applied with its references moved by
the instance's offset: a face plane and centre, or an edge's line or circle.
Each moved reference then goes through the feature's normal rules:

- **Unique:** it proceeds.
- **Missing or ambiguous:** the pattern fails.
- **Never:** an edge or face is never substituted, and "similar" edges are
  never searched for.

This adds no topology matching beyond the features' own. A moved edge that
the source's instance 0 already consumed (e.g. a line along the pattern
direction) matches no edge and fails. The results:

- Chamfer of the first hole's rim, 1 mm, patterned 5 times: V = V_holes −
  5·π·1²·(5 + 1/3) = 112062.24256192978 expected, 112062.2425619298 actual.
  Each rim is chamfered, and the top face meets it at radius 6.
- Fillet of the rim, 2 mm, patterned 5 times: V = V_holes −
  5·2π(5 + ū)·r_f²(1 − π/4), with ū = r_f(10 − 3π)/(12 − 3π).
  111999.13263314449 expected, 111999.1326331445 actual.

## Persistence

`LinearPattern_SaveLoadPreservesDefinition` works on the hole grid: two
directions, a driven spacing and literal counts. The sequence is save → an
empty document → load → regenerate, and it checks:

- `equivalent()` documents and identical item IDs;
- the pattern's definition equal field for field: the source's `FeatureId`
  (the same stable ID), both direction vectors, counts, spacings and their
  parameters;
- the same instances in the same order;
- the same dependencies for every node, and the same regeneration order;
- bit-identical volume, area, centroid, bounds and topology for Pad, Drill
  and Holes;
- that the loaded model is still parametric: a hole diameter of 8 mm
  rebuilds Drill and Holes; a 22 mm spacing rebuilds only Holes, and each
  gives the analytic volume.

Also checked:

- The direction is stored as given: (1, 1, 0) is `[1.0, 1.0, 0.0]` in the
  file and is restored exactly.
- A failed pattern saves and loads unchanged, with the same error, and
  recovers.
- The JSON is checked text for text:
  `{"source": 9, "first": {"direction": [1.0, 0.0, 0.0], "count": 5,
  "count_parameter": 7, "spacing": 0.02, "spacing_parameter": 8}}`, with
  `"second"` only for grids.
- Malformed data is rejected with its path, e.g.
  `objects[3].data.first.count: expected a non-negative integer`,
  `objects[3].data.first.count: expected at most 4294967295`, and
  `objects[3].data.mode: unknown field` (no pattern modes beyond these).
  Invalid content is reported at the pattern, e.g. `objects[3].data:
  direction 1: the direction must be a finite, non-zero vector, got
  (0, 0, 0)`.
- No instance geometry is serialized; the file holds only the definition.

`LinearPattern_ReusesStableSourceReference` covers the source reference
without saving:

- Renaming the source changes nothing.
- Deleting it makes the pattern fail with `NotFound`. It does not adopt
  another hole on the same face.
- Putting it back with its ID restores the pattern, bit for bit.

## Undo / Redo

`LinearPattern_UndoRedoRestoresGeometry`, on the drilled block:

1. **Create** "Holes": 3 holes 20 mm apart (`CreateLinearPatternCommand`,
   "Create linear_pattern 'Holes'").
2. **Count** → 2; only the pattern regenerates.
3. **Spacing** → 15 mm; the second hole moves to x = 65.
4. **Direction** → (0, 1, 0); the second hole moves to (50, 40).
5. Undo and redo through these states. Each leaves a document equivalent to
   the recorded one, with the definition and a bit-identical volume; the
   first undo puts the hole back at x = 65.
6. Undo everything: the pattern is gone, and Drill is the result again.
7. Redo: the pattern is recreated with the same ID and a bit-identical
   volume.

Commands check the feature kind. An invalid edit (direction (0, 0, 0))
changes nothing and records nothing
(`LinearPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`).

## STEP

`LinearPattern_ExportsStep`, read back with the kernel:

- **The hole row:** one product, `PRODUCT('Holes','Holes'`; 1 solid, valid.
  V and A are within 1e-9 of the closed forms; the bounds reach 120 and
  20 mm.
- **The cube row:** one body of 4 solids, valid; V = 4000 mm³ within 1e-9;
  bounds 0 to 70 mm in X.

## STL

`LinearPattern_ExportsStl`:

- **The hole row, binary and ASCII** (0.01 mm deflection): closed. The
  enclosed volume is above the exact volume, because the hole walls are
  concave and their chords lie across the holes, and it is within
  0.01 mm × the surface area of it.
- **The cube row:** the current STL policy writes one file per export with
  every result body in it. Here that is 48 triangles (4 closed cubes), with
  an enclosed volume of 4000 mm³ within 1e-12. Flat faces mesh exactly, and
  the vertices are exact in single precision. The export semantics are
  unchanged.

## Diagnostics

The project's convention is `Result`/`Error`: a stable `ErrorCode` and a
message naming the pattern, the step, the instance and the cause. The
suggested categories map to it as follows:

| Suggested | Code | Message (example) |
| --- | --- | --- |
| LinearPatternMissingSource | NotFound / FailedPrecondition | "object:6 references object:5, which does not exist"; "Row: a linear pattern needs the body of its source feature" |
| LinearPatternInvalidCount | InvalidArgument | "direction 1: the count must be at least 1, got 0"; "Row: linear pattern: direction 1: the count must be a whole number from 1 to 500, got 2.5" |
| LinearPatternInvalidSpacing | InvalidArgument | "direction 1: the spacing must be positive and finite, got -20 mm" |
| LinearPatternInvalidDirection | InvalidArgument | "direction 1: the direction must be a finite, non-zero vector, got (0, 0, 0)"; "the two directions must not be parallel, got (1, 0, 0) and (-2, 0, 0)" |
| LinearPatternMissingTarget | — | the pattern's target is its source (above) |
| LinearPatternSourceEvaluationFailed | the source's code | the source's error (e.g. its sketch), or the pattern is blocked when the source itself failed |
| LinearPatternTransformFailed | InvalidArgument / Internal | "translate: the translation must be finite"; "translate: the kernel produced an invalid shape" |
| LinearPatternBooleanFailed | Internal | "Row: linear pattern: instance k at (…) mm: boolean union failed: …" |
| LinearPatternInvalidResult | Internal | "linear pattern: the pattern produced no valid solid" |
| LinearPatternTooManyInstances | InvalidArgument | "a linear pattern may have at most 500 instances, got 600 (30 x 20)" |
| (instance failure) | the instance's code | "Holes: linear pattern: instance 5 at (100, 0, 0) mm: hole: the hole does not fit on its face: …" |
| (unsupported source) | FailedPrecondition | "a linear pattern cannot repeat another pattern; use a second direction for a grid"; "repeating an intersect extrude is not supported: …" |
| (parameter of another dimension) | DimensionMismatch | validation: "Holes (object:10): direction 1's count is driven by width (object:1), which is a length, not dimensionless" |

**Not exercised by a test.** Three defensive paths have no deterministic
trigger; they are the same kernel-failure paths as for the other features:

- a kernel failure while translating;
- a boolean failure on valid inputs;
- a final result without a valid solid.

`bettercad-cli info` describes patterns ("source Drill, count x pitch along
(1, 0, 0)"). `validate` reports a failing instance as an error and exits 1.

## Performance

`performance/pattern_timing.cpp` and `performance/pattern-timing-release.log`
time one full regeneration through the feature API (release build of the
qualified tree, AMD Ryzen 7 5800H, single-threaded kernel calls):

| Instances | Hole pattern | Cube (new-body) pattern |
| --- | --- | --- |
| 10 | 0.63 s | 0.22 s |
| 50 | 9.03 s | 3.11 s |
| 100 | 33.5 s | 12.4 s |
| 200 | 178.6 s | 50.2 s |

Every result was valid and matched its analytic volume (relative error at
most 1.2e-14).

The time grows about with the square of the count. Each instance is a
boolean on the growing body, plus that feature's whole-body checks (a
hole's face lookup, clearance and containment). Wall-clock times vary
between runs on this machine, so the source comment cites only
conservative figures.

No optimization was attempted in this milestone; the performance work is
P24. The finding sets the instance limit (**500**). 1000 hole instances
would take over an hour at this rate. Raising the limit later keeps every
saved file valid, while lowering it would break files, so it starts
conservative.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors. CTest ran only after a
successful build.

The final qualification ran from 06:12:04 to 06:31:29; no source, test or
CMake file changed after it started (checked by modification time). An
earlier qualification (05:36–05:56) also passed 488/488 in all three
presets. It was repeated because a source comment then changed to cite
conservative timing figures, and its logs were replaced by the final run's.

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 235 files compiled | 0 | **488/488 passed** |
| Release | exit 0 | exit 0, 235 files compiled | 0 | **488/488 passed** |
| Debug-shared | exit 0 | exit 0, 235 files compiled | 0 | **488/488 passed** |

**Compiler warnings: 0** in every preset. P11-FEAT-004 compiled 229 files;
the 6 new ones are:

- `OcctTransform.cpp` (geometry);
- `LinearPatternFeature.cpp` and `LinearPatternRegeneration.cpp`
  (features);
- `TransformTests.cpp`, `LinearPatternFeatureTests.cpp` and
  `LinearPatternFileTests.cpp`.

The 44 new tests (all passed in every preset):

- `tests/core/geometry/TransformTests.cpp`: 5;
- `tests/features/LinearPatternFeatureTests.cpp`: 30;
- `tests/io/LinearPatternFileTests.cpp`: 7;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

Every pattern, transform, hole, fillet and chamfer test (the 159 tests
matching those words, 4 of them older tests that mention holes) also ran 5
times in Release and in Debug, one process per run: 795/795 passes each
(`ctest-repeat-*.log`).

## Legacy Regression

P0–P11-FEAT-004: **PASS**. All 444 tests of the P11-FEAT-004 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-004/ctest-release.log`: each of its 443 distinct names (two
older test cases share a name) appears as "Passed" in each new log.

No legacy test assertion was changed. Changes to existing code:

- extrude, revolve and chamfer regeneration were split into tool/request
  functions and a combine step;
- `applyToTargetBody()` names the consumed feature's role ("source" for
  patterns);
- validation says "the source is" for patterns;
- the test-only STEP read-back also returns bounds.

New tests were only appended to `ValidationTests.cpp` and `CliTests.cpp`.

## Known Limitations

- **Performance:** quadratic in the count (see Performance); at most 500
  instances.
- **Not implemented:** symmetric patterns, the total-length mode, and
  suppressed or skipped instances. A pattern is all or nothing.
- **Unsupported sources:** patterns of patterns (a second direction makes
  grids), intersect extrudes and revolves, and sketches are refused.
- **Overlapping holes** are refused by the Hole's containment policy.
  Overlapping new-body, join and cut instances are allowed.
- **References:** chamfer, fillet and hole instances use the features'
  geometric references moved exactly. There is no semantic topology naming,
  so an instance whose moved reference is missing or ambiguous fails the
  pattern.
- **Instances are not document objects.** They are identified by the
  pattern and their index; selecting individual instances comes later.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: every pattern, transform, hole,
  fillet and chamfer test run 5 times.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including translated copies (4 cubes: 2.3e-16; touching:
  3.4e-16; overlapping: 0; 5 holes: 0).
- `pattern-feature-values-release.txt`: measured feature-level volumes,
  areas, bounds and distances, as printed by the tests.
- `performance/pattern_timing.cpp` and
  `performance/pattern-timing-release.log`: the timing measurement.

## Final Result

**PASS.** P11-FEAT-005 Linear Pattern is implemented and verified in all
three presets with zero warnings:

- one- and two-direction patterns of new-body, join and cut extrudes and
  revolves, holes, chamfers and fillets;
- explicit count semantics (the source included) and a unit-safe spacing;
- a validated, normalized direction;
- offsets computed from the source for every instance, in a deterministic
  order;
- atomic failure with the instance in the message;
- analytic validation, regeneration, undo/redo, save/load, STEP and STL.

Symmetric and total-length modes are not implemented.
