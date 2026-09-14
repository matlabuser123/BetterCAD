# P11-FEAT-006 — Circular Pattern Verification

## Status

PASS

Date: 2026-09-15. `main` was at `fe280cb` (P11-FEAT-005 Linear Pattern)
before this milestone.

## Scope

Every item was checked against the working tree and by tests.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Circular Pattern feature (`CircularPatternDefinition`) | IMPLEMENTED | `CircularPattern_DefinitionIsValidatedOnCreateAndEdit`; `CircularPattern_DependsOnItsSourceAndParameters` |
| Stable source reference (the source's `FeatureId`) | IMPLEMENTED | `CircularPattern_ReusesStableSourceReference`; `CircularPattern_SaveLoadPreservesDefinition` |
| Axis (origin + any finite, non-zero direction, normalized when used; any 3D axis) | IMPLEMENTED | `CircularPattern_ArbitraryAxisPreservesRadius`; `CircularPattern_RegeneratesWhenAxisChanges`; `CircularPattern_RejectsZeroAxis` |
| Count (the source included; literal or a dimensionless parameter holding a whole number) | IMPLEMENTED | `CircularPattern_CountIncludesOriginal`; `CircularPattern_RejectsZeroCount`; `CircularPattern_RegeneratesWhenCountChanges` |
| Full circle, equal spacing (360°/N, no instance at 360°) | IMPLEMENTED | `CircularPattern_FullCircleDoesNotDuplicate360Degrees`; `CircularPattern_FourInstancesAreAtQuadrants` |
| Partial angle (included angle, angle/(N − 1) apart, both ends included) | IMPLEMENTED | `CircularPattern_PartialAngleIncludesExpectedEndpoints`; `CircularPattern_RegeneratesWhenAngleChanges` |
| Explicit angle step (preferred) | IMPLEMENTED | `CircularPattern_AngleStepSpacing` |
| Symmetric pattern (optional) | **NOT IMPLEMENTED** (refused as an unknown spacing in files) | `CircularPattern_MalformedDataIsRejectedWithTheJsonPath` |
| Positive/negative direction | IMPLEMENTED | `CircularPattern_NegativeDirectionReversesOrder`; `CircularPattern_RegeneratesWhenDirectionChanges` |
| Deterministic rotation, computed from the source per instance | IMPLEMENTED | `CircularPattern_DoesNotAccumulateAngularDrift`; `RigidTransform_*` (4 tests); `CircularPattern_RegenerationIsDeterministic` |
| Body pattern (new-body extrude or revolve) | IMPLEMENTED | `CircularPattern_BodyPatternMatchesAnalyticVolume`; `CircularPattern_TouchingAndOverlappingInstancesFuse`; `CircularPattern_WorksOnRevolvedSource` |
| Additive feature pattern (join extrude) | IMPLEMENTED | `CircularPattern_AdditiveBossesMatchAnalyticVolume` |
| Hole/subtractive pattern (hole, cut extrude) | IMPLEMENTED | `CircularPattern_HoleBoltCircleMatchesAnalyticVolume`; `CircularPattern_SubtractiveExtrudePatternMatchesAnalyticVolume` |
| Pattern of Chamfer and Fillet | IMPLEMENTED (by exactly rotated edge references; see Reference Safety) | `CircularPattern_PatternsChamferAndFilletByRotatedReferences` |
| Source on the axis | DEFINED POLICY (see Pattern Semantics) | `CircularPattern_SourceOnTheAxis` |
| Geometry validity | IMPLEMENTED | each instance through its feature's own checks; the final body is validated |
| Atomic failure, instance-specific diagnostics | IMPLEMENTED | `CircularPattern_FailsAtomicallyWhenInstanceInvalid`; `CircularPattern_InvalidInputsFailWithStructuredDiagnostics` |
| Regeneration (source, count, angle, direction, axis) | IMPLEMENTED | `CircularPattern_RegeneratesWhen…Changes` (5 tests) |
| Count safety (at most 500 instances, shared with linear patterns) | IMPLEMENTED | `CircularPattern_RejectsZeroCount` |
| Transactions | IMPLEMENTED | `CircularPattern_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Save/load | IMPLEMENTED | `CircularPattern_SaveLoadPreservesDefinition`; `CircularPattern_SaveLoadKeepsAnArbitraryAxis`; `CircularPattern_FailedPatternSavesAndLoadsUnchanged`; `CircularPattern_DataIsStoredAsTransparentJson`; `CircularPattern_MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo (create, count, angle, direction) | IMPLEMENTED | `CircularPattern_UndoRedoRestoresGeometry`; `CircularPattern_RegeneratesWhenCountChanges` |
| STEP/STL | IMPLEMENTED | `CircularPattern_ExportsStep`; `CircularPattern_ExportsClosedStl` |
| Analytical validation | IMPLEMENTED | the volume tests below; `examples/geometry_accuracy` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `CircularPattern_ModelsAreValidatedLikeAnyOther`; `info and validate describe circular patterns` |
| Rigid-motion infrastructure | IMPLEMENTED | `RigidTransform_*` (4 tests); `Transform_RotatedBodyMovesExactly`; `Transform_RotatedReferencesDescribeTheMovedGeometry` |
| Pattern of a pattern | **REFUSED** (explicitly unsupported, both ways between linear and circular) | `CircularPattern_RefusesUnsupportedSources` |

**Supported source categories**, exactly as for linear patterns:

- extrude and revolve features whose operation is new body, join or cut
  (intersect is refused);
- hole features (all types and extents);
- chamfer and fillet features.

Sketches and other patterns are refused. Not claimed: semantic topology
naming, suppressed instances, symmetric patterns, or axis references to
sketch lines, edges or datum axes.

## Architecture

```text
CircularPatternFeature (bettercad_features)           include/bettercad/features/CircularPatternFeature.hpp
  └─ regenerateCircularPattern()                      src/features/pattern/CircularPatternRegeneration.cpp
       ├─ resolveCircularPatternInstances(): axis, count and angle parameters
       │    → checkCircularAngle() → circularPatternStep() → circularPatternInstances()
       └─ shared pattern support (private)            src/features/pattern/PatternSupport.{hpp,cpp}
            ├─ instanceOperation(source): the source's own operation, resolved once
            │    ├─ extrude/revolve: extrudeTool()/revolveTool() + geometry::transformed(Body)
            │    │                   + booleanUnion()/booleanDifference()
            │    ├─ hole:            geometry::transformed(HoleRequest) + cutHole()
            │    └─ chamfer/fillet:  geometry::transformed(EdgeSignature) + chamferEdges()/filletEdges()
            ├─ buildPattern(): instance 0 = the source's body → instances 1 … N−1 in order → validation
            ├─ resolvePatternCount(), checkCircularAngle()
            └─ used by LinearPatternRegeneration.cpp as well (placements = translations)
RigidTransform3D (bettercad_core, header only)        include/bettercad/core/math/RigidTransform.hpp
geometry::transformed(Body, RigidTransform3D)         include/bettercad/core/geometry/Transform.hpp
  └─ OCCT adapter: gp_Trsf::SetValues + BRepBuilderAPI_Transform (copy)   src/core/geometry/occt/OcctTransform.cpp
```

No OCCT type leaves `occt/`; `gp_Trsf` stays in the adapter
(`architecture.layering` passes).

**One pattern subsystem, not two.** The Linear Pattern's per-source
operation, build loop, count resolution and failure handling moved into
`PatternSupport` and now take a `RigidTransform3D` placement. A linear
pattern passes translations; for those, `transformed()` and every reference
`transformed()` delegate to the existing `translated()` path, so linear
patterns build bit-identical geometry with unchanged messages (all 39 Linear
Pattern tests and all 488 P11-FEAT-005 tests pass unchanged).

**Reused, not duplicated:** the regenerator, dependency graph, commands
(`CreateFeatureCommand`/`ModifyFeatureCommand` as
`CreateCircularPatternCommand`/`ModifyCircularPatternCommand`), result
bodies, validation, the JSON reader, the Boolean operations, and each
source's own evaluation and checks (the Hole's placement and containment,
the blend fit checks).

**New:**

- core math: `RigidTransform.hpp` (Rodrigues rotation about an axis, pure
  translations);
- geometry: `transformed()` for bodies, edge references, face references and
  hole requests;
- features: `Pattern.hpp` (the shared `kMaxPatternInstances`),
  `CircularPatternFeature.hpp/.cpp`, `CircularPatternRegeneration.cpp`,
  `PatternSupport.hpp/.cpp`, the regenerator handler, the commands, and
  validation of the count (dimensionless) and angle (angle) parameters;
- io: the `circular_pattern` JSON mapping;
- CLI: `info` describes circular patterns;
- rotated-copy cases in `examples/geometry_accuracy`;
- test fixtures `CubeRingModel`, `BoltCircleModel` and `PegModel` in
  `tests/support/PatternModels.hpp`;
- `docs/architecture.md`.

**Transactions.** The pattern builds its whole body before anything is
stored. The first failing instance fails the pattern; the regenerator keeps
no body for it (P8 policy), and the source, its body and the document are
unchanged. Definitions are validated before a command changes the document.

## Pattern Semantics

Count includes the original: **YES** (as for linear patterns). Count 1 is
the source alone, 2 the source and one turned copy, N the source and N − 1
copies.

Instance i (i = 0 … N − 1) is the source turned by θ_i about the axis, with
T_i = Rotation(axis, θ_i) computed from θ_i for every instance, never by
composing the previous one.

| Spacing | Step Δ | θ_i | Angle input |
| --- | --- | --- | --- |
| `FullCircle` | 2π/N | 2πi/N; the last is 2π(N − 1)/N, never 2π | none (a full circle with an angle is refused) |
| `IncludedAngle` | α/(N − 1) (0 for N = 1) | iα/(N − 1); both ends, 0 and α, included | 0 < α < 360°; α → 360° is refused, since the last instance would land on the source |
| `AngleStep` | α | iα | 0 < α < 360° and (N − 1)α < 360° |

- **Direction.** `Positive` turns counter-clockwise looking against the
  axis direction (right-hand rule); `Negative` negates Δ. Reversing the axis
  direction is the same as reversing the rotation.
- **No silent normalization.** Angles outside (0°, 360°) are refused, not
  wrapped: 400° and 720° are errors, not 40° and 0°. The 360° boundary uses
  a 1e-9 rad tolerance, the tolerance at which edge and face matching could
  no longer tell an instance from the source.
- **Instance identity.** Instances are not document objects. An instance is
  its pattern's `FeatureId` plus a deterministic index (0 is the source),
  listed in order by `circularPatternInstances()`.
- **Operation semantics** follow the source, as for linear patterns:
  new-body instances fuse where they touch or overlap and stay separate
  solids where they do not; join and cut instances may overlap; each hole
  instance must fit its face (the Hole's policy), so overlapping holes are
  refused.
- **Source on the axis.** Nothing special is done: a source the axis passes
  through turns onto itself. New-body instances coincide and fuse into the
  source (the result is the source's volume), and a hole is refused by the
  hole's own placement check at instance 1, whose centre is already inside
  the first hole. Both outcomes are deterministic and tested.

## Axis

A `PatternAxis` is `{Point3D origin; Vector3D direction}`, the same content
as the core `Axis3D` that the Revolve's geometry (`makeRevolution`) takes. The
direction is kept as given, like a linear pattern's direction, so a file
stores exactly what the user entered and validation can report an invalid
vector. It is normalized into an `Axis3D` (`Direction3D`) when used.
Validation: a finite origin, and a finite, non-zero direction.

**Reuse of the Revolve axis infrastructure.** The core types (`Axis3D`,
`Direction3D`) and the kernel path are shared. The Revolve *feature's*
`RevolveAxis` (sketch X, sketch Y or a sketch line) is not reused. It is
defined relative to the revolve's profile sketch, and a pattern has no
profile sketch; a hole source, for example, has none at all. Referencing a
sketch line, model edge or datum axis would need a new reference kind, so
the axis is explicit model coordinates in this milestone (see Known
Limitations).

The rotation is `RigidTransform3D::rotation(axis, θ)`: Rodrigues' matrix
R = cos θ I + sin θ [k]× + (1 − cos θ) k kᵀ from θ's own sine and cosine,
and t = o − R·o, so the axis stays fixed. The OCCT adapter receives this
matrix (`gp_Trsf::SetValues`) rather than recomputing it.

## Basic Four-Instance Validation

`CircularPattern_FourInstancesAreAtQuadrants`:

| | |
| --- | --- |
| Axis | Z through the origin, (0, 0, 1) |
| Source | 10 mm cube [45, 55] × [−5, 5] × [0, 10] mm (new-body extrude driven by `size`), centre (50, 0, 5) |
| Count | 4 (a dimensionless parameter), full circle |
| Expected angles | 0°, 90°, 180°, 270° |
| Actual angles | 0, 90, 180, 270 (exact) |
| Expected centres | (50, 0, 5), (0, 50, 5), (−50, 0, 5), (0, −50, 5) mm |
| Actual centres | (50, 0, 5), (3.1e-15, 50, 5), (−50, 6.1e-15, 5), (−9.2e-15, −50, 5) mm |
| Faces | each cube's outer (r = 55) and inner (r = 45) side found once at 0°, 90°, 180°, 270°; none at 45° |
| Bounding box | (−54.999999999999993, −54.999999999999993, 0) to (54.999999999999993, 54.999999999999993, 10) mm |
| Centre of mass | (−1.3e-15, 3.6e-15, 4.99999999999999911) mm, expected (0, 0, 5) |
| Validity | valid, 4 solids |
| Result | **PASS** |

## Body Volume Validation

| | |
| --- | --- |
| Source volume | 1000 mm³ (actual 999.99999999999932) |
| Count | 4 (disjoint instances) |
| Formula | V = N × V_source |
| Expected | 4000 mm³ |
| Actual | 3999.99999999999636 mm³ (feature, release) |
| Error | 3.6e-12 mm³ (9.1e-16 relative) |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Result | **PASS** |

The area is 2399.99999999999955 mm² (expected 2400). Counts 1, 2 and 4 give
1, 2 and 4 solids of 1000, 2000 and 4000 mm³ (`CircularPattern_CountIncludesOriginal`).

**Touching, overlapping and non-overlapping** (`CircularPattern_TouchingAndOverlappingInstancesFuse`):

| Case | Expected | Actual | Solids |
| --- | --- | --- | --- |
| disjoint (the quadrant ring above) | 4000 | 3999.99999999999636 | 4 |
| touching: a half turn about x = 45 → one 20 × 10 × 10 bar | 2000 | 1999.99999999999864 | 1, no inner faces |
| overlapping: a half turn about (50, 2.5) → 10 × 15 × 10 | 1500 | 1499.99999999999932 | 1 |
| overlapping: 36 cubes 10° apart → one ring | 31498.165374387190 | 31498.165374387227 | 1 |

The 36-cube value is derived by hand, not by the kernel. Each square covers
its ±5° sector between the lines 45 and 55 mm from the axis, so the union is
the band between two regular 36-gons (apothems 45 and 55 mm; area
n·a²·tan(π/n)), plus 72 corner tips where neighbours cross: right triangles
with legs 5 − 55 tan 5° and 55 − (55 − 5 sin 10°)/cos 10°. The relative
error is 1.2e-15. Every result is valid.

## Bolt-Circle Hole Validation

`CircularPattern_HoleBoltCircleMatchesAnalyticVolume` (the flagship case):

| | |
| --- | --- |
| Base geometry | disc R = 60 mm, H = 10 mm (extrude of a constrained circle, radius and thickness parameters): πR²H = 36000π mm³ |
| Hole diameter | 10 mm, through, from the bottom face (a parameter) |
| Bolt-circle radius | 40 mm (the source hole at (40, 0)) |
| Count | 6, full circle |
| Formula | V = πR²H − N·π(d/2)²H = 34500π |
| Expected volume | 108384.94654884786 mm³ |
| Actual volume | 108384.94654884789 mm³ |
| Error | 2.9e-11 mm³ (2.7e-16 relative); tolerance 1e-12 relative |
| Area | 27331.856086231193 mm², expected 8700π = 27331.856086231201 (2πR² + 2πRH − 12πr² + 6·2πrH) |
| Expected centres | 40·(cos 60i°, sin 60i°), i = 0 … 5 |
| Actual centres | instance angles 0, 59.999999999999993, 119.99999999999999, 180, 239.99999999999997, 299.99999999999994°; each hole's rim is found as a circle of radius 5 mm at 40·(cos 60i°, sin 60i°) on both faces (z = 0 and z = 10), and none at 30° |
| Validity | valid, 1 solid; the pattern is the only result body |
| Result | **PASS** |

The source hole's own body is an unchanged intermediate: 112311.93736583513
mm³ (expected 112311.93736583511).

## Additive Pattern

`CircularPattern_AdditiveBossesMatchAnalyticVolume`: a 10 × 10 × 5 mm boss
(a join extrude sketched on z = 10) at (15…25, −5…5) mm on the drilled
flange, between the holes, six times around Z.

- V = 34500π + 6 × 500 = 111384.94654884786 expected; 111384.94654884785 actual.
- There are six boss tops on z = 15, and each boss's outer side (25 mm out)
  is found turned by 60i°. The maximum z is 15. The result is 1 valid solid,
  and the pattern is the only result body.

The same square as a pocket (cut 5 mm down,
`CircularPattern_SubtractiveExtrudePatternMatchesAnalyticVolume`): V =
34500π − 3000 = 105384.94654884786 expected; 105384.94654884790 actual. There
are six floors on z = 5, and each pocket wall faces inwards at 25 mm.

## Partial-Angle Pattern

`CircularPattern_PartialAngleIncludesExpectedEndpoints`: 4 holes over an
included 90° on the bolt circle.

- The angles are 0, 29.999999999999996, 59.999999999999993 and 90°
  (expected 0/30/60/90).
- The holes are found at those angles, and none at 120°.
- V = 109955.74287564278 mm³; expected πH(R² − 4r²) = 109955.74287564275.
- 2 holes over 90° gives 0°, 90°. 1 hole gives 0° (the source alone,
  112311.93736583513 mm³).

**Angle step** (`CircularPattern_AngleStepSpacing`): 45° × 4 gives 0, 45, 90
and 135° (exact), with V = 109955.74287564277. 120° × 4 is refused ("the
instances would go all the way around: 4 instances 120 deg apart span
360 deg, which must stay below 360 deg"), while 120° × 3 is accepted.

**Full vs partial duplicates**
(`CircularPattern_FullCircleDoesNotDuplicate360Degrees`):

- For N = 1, 2, 3, 4, 6, 7, 12 the last full-circle angle is 360(N − 1)/N:
  0, 180, 239.99999999999997, 270, 299.99999999999994, 308.57142857142861
  and 329.99999999999994°. It is always below 360 − 1e-9.
- Four cubes, not five.
- An included angle of 360° is refused: "the included angle must be less
  than 360 deg, got 360 deg: the last instance would land on the source (use
  a full circle)".

## Negative Direction

`CircularPattern_NegativeDirectionReversesOrder`: 4 holes over 90°,
negative.

- The angles are 0, −29.999999999999996, −59.999999999999993 and −90°.
- The holes are at those angles, and none at +30°.
- V = 109955.74287564278, identical to the positive pattern.

Changing the direction of an existing pattern rebuilds only the pattern and
moves the holes from +30/+60/+90° to −30/−60/−90°
(`CircularPattern_RegeneratesWhenDirectionChanges`). Reversing the axis
(0, 0, −1) turns instance 1 of (40, 0) to (20.000000000000004,
−34.641016151377549), expected (20, −20√3).

## Arbitrary Axis

`CircularPattern_ArbitraryAxisPreservesRadius`:

- **Source:** a peg, a cylinder r = 3 mm from z = 17 to 23 mm about
  x = 40, y = 10.
- **Pattern:** four instances 90° apart about axes through the origin along
  X, Y, Z and (1, 1, 1).
- **Independent reference:** Rodrigues' formula, written in the test
  without BetterCAD math, gives each instance's two rim centres and axes.
  Each rim must be found on the body as a circle there (the kernel's circles
  match within 1e-7 mm). The distance from the axis of each rim centre the
  kernel reports is then compared with the source's.

| Axis | Initial rim radii (z = 17 / 23) | Instances | V (expected 216π = 678.58401317539528) | Largest radius change | Centre of mass (expected: the foot of (40, 10, 20) on the axis) |
| --- | --- | --- | --- | --- | --- |
| X | 19.723 / 25.080 mm | 4 solids | 678.58401317539563 | 3.6e-15 mm | (39.999999999999993, −1.7e-15, −1.3e-15) |
| Y | 43.463 / 46.141 mm | 4 solids | 678.58401317539551 | 7.1e-15 mm | (−1.7e-16, 9.99999999999999822, −6.7e-16) |
| Z | 41.231 / 41.231 mm | 4 solids | 678.58401317539563 | 7.1e-15 mm | (−3.0e-15, 2.0e-15, 19.999999999999996) |
| (1, 1, 1) | 22.196 / 21.276 mm | 4 solids | 678.58401317539551 | 7.1e-15 mm | (23.333333333333332, 23.333333333333339, 23.333333333333329), expected 70/3 = 23.333333333333336 |

Tolerance: 1e-9 mm for the radius change and centre of mass; 1e-12 relative
for the volume. Maximum error: 7.1e-15 mm.

At the transform level (`RigidTransform_RotationMatchesRodrigues`,
`RigidTransform_RotationPreservesDistanceToAxis`), 6 axes × 2 origins × 9
angles match Rodrigues within 1e-11 mm. 360 one-degree rotations about
(1, 1, 1) through (5, −2, 1) mm keep the radius within 1.07e-14 mm, and
positions within 2.20e-14 mm.

## Angular Drift

`CircularPattern_DoesNotAccumulateAngularDrift`:

| | |
| --- | --- |
| Count | 100, full circle |
| Expected θ₉₉ | 99 × 3.6 = 356.4° |
| Actual θ₉₉ | 356.40000000000003° (3.4e-14° from 356.4); every θ_i equals i × Δ bit for bit |
| Last instance of (50, 0, 0) | (49.901336421413582, −3.1395259764656633) mm; expected 50·(cos 356.4°, sin 356.4°) = (49.901336421413575, −3.1395259764656633) |
| 360 instances, 1° apart | worst radius error 1.42e-14 mm; worst angle error 1.78e-15 rad |
| On the body | 36 cubes of 2 mm, 10° apart: 36 solids, V = 287.99999999999994 mm³ (288); the last cube's (index 35, 350°) outer face is found at radius 47 |
| Result | **PASS** (no accumulated drift) |

## Regeneration

**Source** (`CircularPattern_RegeneratesWhenSourceChanges`):

| Change | Rebuilt | V actual (mm³) | V expected (mm³) |
| --- | --- | --- | --- |
| hole diameter 10 → 12 mm | Bolt, Bolts | 106311.49539747853 | 106311.49539747860 |
| flange 10 → 20 mm thick | Flange, Bolt, Bolts | 216769.89309769578 (every hole still through: exit rims at z = 20) | 216769.89309769572 |
| flange radius 60 → 70 mm | Disc, Flange, Bolt, Bolts | 149225.65104551511 | 149225.65104551517 |
| chamfer 1 → 2 mm (pattern of a chamfer) | Ease, Eases | 107957.68994795965 | 107957.68994795965 |
| revolve sweep 360° → 270° (pattern of a revolve) | Turn and what builds on it, Shafts included | 84823.00164692440 | 84823.00164692441 |

**Count 3 → 6 → 8** (`CircularPattern_RegeneratesWhenCountChanges`):

- Each change rebuilds only the pattern; the source stays up to date.
- V = 110741.14103904019, 108384.94654884789 and 106814.15022205299 mm³
  (expected 110741.14103904020, 108384.94654884786 and 106814.15022205297).
- Holes are found at 360i/N°, and none halfway between.
- Returning to 6 gives the 6-hole volume bit for bit. Undo and redo through
  the three steps give bit-identical volumes.

**Angle 90° → 180°** (`CircularPattern_RegeneratesWhenAngleChanges`): an
included angle driven by the angle parameter `span`.

- The angles go from 0/30/60/90° to 0, 59.999999999999993,
  119.99999999999999 and 180°.
- Only the pattern rebuilds. The holes are found there, and none at 30°.
- V = 109955.74287564275 (exact).

**Direction:** see Negative Direction.

**Axis** (`CircularPattern_RegeneratesWhenAxisChanges`): moving the axis to
(5, 0, 0) rebuilds only the pattern. The holes then orbit it at 35 mm:
found at (5 + 35 cos 60i°, 35 sin 60i°), with V = 108384.94654884785. The
reversed axis is described under Negative Direction.

**Revolved source** (`CircularPattern_WorksOnRevolvedSource`): the turned
part's revolve (R = 15, h = 40 mm) four times about the axis through
(60, 0). That gives 4 valid solids with V = 4πR²h: 113097.33552923251
actual, 113097.33552923254 expected. The bounds are x −15 … 135 and
y −75 … 75 mm.

## Atomic Failure

`CircularPattern_FailsAtomicallyWhenInstanceInvalid`, on the drilled block
(100 × 50 × 20 mm, hole at (80, 25)). The pattern turns about the axis
through (60, 25) over an included 90°, 4 instances:

- Instances 1 and 2 (30°, 60°) fit; their holes are 10.35 mm apart.
- Instance 3 fails with `FailedPrecondition`: "Arc: circular pattern:
  instance 3 at 90 deg: hole: the hole does not fit on its face: its entry
  is 10 mm across, but the centre (60, 45) mm is only 5 mm from the face's
  edge (a hole must stay at least 0.001 mm inside its face)".
- The pattern keeps no body; no partial pattern of three holes is shown.
- The source hole's body (98429.20367320513 mm³) and the document are
  untouched.
- 3 instances over 60° then build: V = 95287.61101961532 mm³, equal to
  100000 − 1500π.

**Other failures:**

- **40 holes on the bolt circle** overlap (`…FailuresLeaveTheDocumentAndUpstreamBodiesIntact`):
  "Bolts: circular pattern: instance 1 at 9 deg: hole: the hole does not fit
  on its face: its entry is 10 mm across, but the centre (39.5075, 6.25738)
  mm is only 1.27673 mm from the face's edge (…)". Regeneration does not
  change the model. Undo returns to the working model and its exact
  geometry.
- **A hole on the axis:** "Bolts: circular pattern: instance 1 at 60 deg:
  hole: the centre (40, 0) mm is not on a face of the body on the plane
  through (0, 0, 0) mm facing (0, 0, -1) (1 face(s) lie on that plane
  elsewhere)".
- **Driven values out of range** fail at regeneration, and the source stays
  up to date. A span of 0°: "Bolts: circular pattern: the included angle
  must be positive and finite, got 0 deg". A count of 0, −1, 2.5 or 501:
  "Ring: circular pattern: the count must be a whole number from 1 to 500,
  got …".

## Reference Safety (Chamfer, Fillet, Hole)

A patterned hole, chamfer or fillet is applied with its references turned
by the instance's rotation: a face plane and centre, or an edge's line or
circle (a circle's axis turns with it). Each turned reference then goes
through the feature's normal rules:

- **Unique:** it proceeds.
- **Missing or ambiguous:** the pattern fails.
- **Never:** an edge or face is never substituted, and "similar" edges are
  never searched for.

This adds no topology matching beyond the features' own. The results
(`CircularPattern_PatternsChamferAndFilletByRotatedReferences`):

- **Chamfer** of the first hole's top rim, 1 mm, patterned 6 times: V =
  V_holes − 6π·1²·(5 + 1/3) = 108284.41558393299 expected,
  108284.41558393292 actual. Each rim is chamfered, and the top face meets
  it at radius 6. The chamfer's distance parameter drives every instance
  (2 mm: 107957.68994795965, exact).
- **The same chamfer about the axis through (5, 0)**, where the turned rims
  do not exist, fails with `NotFound`: "Eases: circular pattern: instance 1
  at 60 deg: chamfer: edge reference 1 (circle around (22.5, 30.3109, 10) mm
  with axis (0, 0, 1) and radius 5 mm) matches no edge of the body". No other
  edge is taken.
- **Fillet** of the rim, 2 mm, patterned 6 times: V = V_holes −
  6·2π(5 + ū)·r_f²(1 − π/4), with ū = r_f(10 − 3π)/(12 − 3π).
  108208.68366939064 expected, 108208.68366939062 actual; the rims meet the
  top face at radius 7. The blend fit checks (the OCCT crash guard) apply to
  every instance.

## Persistence

`CircularPattern_SaveLoadPreservesDefinition` uses every field. The pattern
has a driven count (4), an included angle driven by `span` (180°), the
negative direction and the axis direction (0, 0, 2) stored as given. It is
saved, the document is replaced by an empty one, then loaded and
regenerated. The test checks:

- `equivalent()` documents, identical item IDs, and the same dependencies
  for every node;
- the definition equal field for field, and the source's `FeatureId`
  unchanged;
- the same instances in the same order (the last at −180°);
- the same regeneration order, and bit-identical volume, area, centroid,
  bounds and topology for Flange, Bolt and Bolts;
- that the loaded model is still parametric. Diameter 8 mm rebuilds Bolt
  and Bolts (111086.71623093510; expected 111086.71623093508). Span 90° and
  count 2 rebuild only Bolts (112092.02588008381; expected
  112092.02588008382).

**Also checked:**

- **An arbitrary axis** (1, 1, 1) through (5, −2, 1) mm, in angle-step
  mode, is stored as `"origin": [0.005, -0.002, 0.001]` and
  `"direction": [1.0, 1.0, 1.0]`, with `"angle": 1.5707963267948966`. It
  comes back with bit-identical geometry
  (`CircularPattern_SaveLoadKeepsAnArbitraryAxis`).
- **A failed pattern** saves and loads unchanged, with the same error, and
  recovers.
- **The JSON** is checked text for text: `{"source": 7, "axis": {"origin":
  [0.0, 0.0, 0.0], "direction": [0.0, 0.0, 1.0]}, "count": 6,
  "count_parameter": 4, "spacing": "full_circle", "rotation": "positive"}`.
  `angle` and `angle_parameter` appear only for the angle spacings.
- **Malformed data** is rejected with its path:
  - `objects[3].data.count: expected a non-negative integer` and `…: expected
    at most 4294967295`;
  - `objects[3].data.axis.direction: expected 3 numbers, got 2`, and
    `objects[3].data.axis: missing required field`;
  - `objects[3].data.axis.radius: unknown field`;
  - `objects[3].data.spacing: unknown value 'symmetric'` and
    `objects[3].data.rotation: unknown value 'clockwise'`.
- **Invalid content** is reported at the pattern with the definition's own
  rules, e.g. `objects[3].data: a full-circle pattern takes no angle: …` and
  `objects[3].data: the included angle must be less than 360 deg, got
  360 deg: …`.
- **No instance geometry is serialized.**

`CircularPattern_ReusesStableSourceReference` covers the source reference
without saving:

- Renaming the source changes nothing.
- Deleting it fails the pattern with `NotFound` ("object:8 references
  object:7, which does not exist").
- Putting it back with its ID restores the pattern, bit for bit.

## Undo / Redo

`CircularPattern_UndoRedoRestoresGeometry`, on the flange with one hole:

1. **Create** "Holes": 6 around Z (`CreateCircularPatternCommand`, "Create
   circular_pattern 'Holes'"). V = 108384.94654884789.
2. **Count** → 4; only the pattern rebuilds, and a hole appears at 90°.
   V = 109955.74287564278.
3. **Angle** → included 90°; a hole appears at 30°.
4. **Direction** → negative; the holes are at −30° and not +30°.
5. Undo and redo through these states. Each leaves a document equivalent to
   the recorded one, with the definition and a bit-identical volume; the
   first undo puts the hole back at +30°.
6. Undo everything: the pattern is gone, and Bolt is the result again.
7. Redo: the pattern is recreated with the same ID and a bit-identical
   volume.

Commands check the feature kind. An invalid edit (axis direction (0, 0, 0))
changes nothing and records nothing.

## STEP

`CircularPattern_ExportsStep`, read back with the kernel:

- **The bolt circle:** one product, `PRODUCT('Bolts','Bolts'`; 1 solid,
  valid. V = 108384.94654884789 mm³ and A = 27331.856086231193 mm², within
  1e-9 of 34500π and 8700π. The bounds are (−60, −60, 0) to (60, 60, 10) mm.
- **The cube ring:** one body of 4 solids, valid; V = 3999.99999999999909
  mm³; bounds ±55 mm in X and Y.

## STL

`CircularPattern_ExportsClosedStl`:

- **The bolt circle, binary and ASCII** (0.01 mm deflection): closed
  (edge-manifold). The enclosed volume is 108379 mm³ against the exact
  108385 mm³, printed to 6 digits. The rim's chords cut into the material and
  the holes' chords cross the holes, so the sign is not fixed. The difference
  is within the bound 0.01 mm × area = 273 mm³.
- **The cube ring:** 48 triangles (4 closed cubes) in one file, as for any
  body. The enclosed volume is 4000 mm³ exactly: flat faces mesh exactly, and
  the quarter-turned vertices are multiples of 5 mm to 1e-14 mm, exact in
  single precision.

## Diagnostics

The project's convention is `Result`/`Error`: a stable `ErrorCode` and a
message naming the pattern, the step, the instance with its angle, and the
cause. The suggested categories map to it as follows:

| Suggested | Code | Message (example, from the tests) |
| --- | --- | --- |
| CircularPatternMissingSource | NotFound / FailedPrecondition | "object:5 references object:4, which does not exist"; "Ring: a circular pattern needs the body of its source feature" |
| CircularPatternInvalidCount | InvalidArgument | "the count must be at least 1, got 0"; "Ring: circular pattern: the count must be a whole number from 1 to 500, got 2.5" |
| CircularPatternInvalidAngle | InvalidArgument | "the included angle must be positive and finite, got 0 deg"; "the included angle must be less than 360 deg, got 360 deg: the last instance would land on the source (use a full circle)"; "the angle step must be less than 360 deg, got 360 deg"; "a full-circle pattern takes no angle: its instances are 360 deg / count apart" |
| CircularPatternDuplicateInstance | InvalidArgument | "the instances would go all the way around: 4 instances 120 deg apart span 360 deg, which must stay below 360 deg" |
| CircularPatternInvalidAxis | InvalidArgument | "the axis direction must be a finite, non-zero vector, got (0, 0, 0)"; "the axis origin must be finite" |
| CircularPatternTooManyInstances | InvalidArgument | "a circular pattern may have at most 500 instances, got 501" |
| CircularPatternTransformFailed | FailedPrecondition / InvalidArgument / Internal | "transform: the body is empty" (tested); "transform: the motion must be finite"; "transform: the kernel produced an invalid shape" |
| CircularPatternBooleanFailed | Internal | "Ring: circular pattern: instance k at … deg: boolean union failed: …" |
| CircularPatternInvalidResult | Internal | "circular pattern: the pattern produced no valid solid" |
| (instance failure) | the instance's code | "Arc: circular pattern: instance 3 at 90 deg: hole: the hole does not fit on its face: …" |
| (unsupported source) | FailedPrecondition | "Twice: circular pattern: a circular pattern cannot repeat another pattern"; "Row: linear pattern: a linear pattern cannot repeat another pattern; use a second direction for a grid" |
| (parameter of another dimension) | DimensionMismatch | regeneration: "Ring: circular pattern: …"; validation: "Bolts (object:8): the count is driven by radius (object:1), which is a length, not dimensionless"; "Bolts (object:8): the angle is driven by count (object:4), which is dimensionless, not an angle" |

**Not exercised by a test.** Four defensive paths have no deterministic
trigger; they are the same kernel-failure paths as for the other features:

- a non-finite motion (valid definitions only make finite ones);
- a kernel failure while transforming;
- a boolean failure on valid inputs;
- a final result without a valid solid.

`bettercad-cli info` describes circular patterns, e.g. "source Bolt, count
around the axis through (0, 0, 0) mm along (0, 0, 1), full circle", or "…,
span included, negative" and "…, 30 deg apart, negative". `validate` reports
the bolt circle as valid (1 solid, 108384.947 mm³, bounds (−60, −60, 0) to
(60, 60, 10) mm). It reports an overlapping pattern as an error and exits 1.

## Performance

`performance/circular_pattern_timing.cpp` and
`performance/circular-pattern-timing-release.log` time one full
regeneration through the feature API. The build is the release build of the
qualified tree, on an AMD Ryzen 7 5800H, with single-threaded kernel calls.
The instances are about 10 mm apart along the bolt circle, as in the linear
pattern timing:

| Instances | Hole pattern (disc R = r + 10 mm, 6 mm holes) | Cube (new-body) pattern |
| --- | --- | --- |
| 10 | 0.60 s | 0.18 s |
| 50 | 10.99 s | 3.18 s |
| 100 | 45.02 s | 12.28 s |

Every result was valid and matched its analytic volume (relative error at
most 6.0e-15): 1 solid for the holes, N separate solids for the cubes.

The time grows about with the square of the count: from 50 to 100
instances it grows 4.1× for holes and 3.9× for cubes. The cube ring costs
the same as the linear cube row (P11-FEAT-005: 0.22, 3.11 and 12.4 s). The
hole ring is slower than the linear hole row at 50 and 100 instances (11.0
against 9.0 s, 45.0 against 33.5 s). The models differ (a disc against a
plate), and the cause was not profiled.

No optimization was attempted; performance work is P24. The circular
pattern reuses the linear pattern's limit (`kMaxPatternInstances` = 500)
and its cost model: one boolean on the growing body per instance, plus that
feature's whole-body checks.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors. CTest ran only after a successful
build. The qualification ran from 07:51:44 to 08:13:31. No source, test or
CMake file changed after it started (checked by modification time).

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 241 files compiled | 0 | **536/536 passed** (171.6 s) |
| Release | exit 0 | exit 0, 241 files compiled | 0 | **536/536 passed** (161.0 s) |
| Debug-shared | exit 0 | exit 0, 241 files compiled | 0 | **536/536 passed** (169.4 s) |

**Compiler warnings: 0** in every preset. P11-FEAT-005 compiled 235 files;
the 6 new ones are:

- `PatternSupport.cpp`, `CircularPatternFeature.cpp` and
  `CircularPatternRegeneration.cpp` (features);
- `RigidTransformTests.cpp`, `CircularPatternFeatureTests.cpp` and
  `CircularPatternFileTests.cpp`.

The 48 new tests (all passed in every preset):

- `tests/features/CircularPatternFeatureTests.cpp`: 33;
- `tests/io/CircularPatternFileTests.cpp`: 7;
- `tests/core/math/RigidTransformTests.cpp`: 4;
- `tests/core/geometry/TransformTests.cpp`: 2;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

The related tests were also run 5 times in Release and in Debug, one
process per run (`ctest-repeat-*.log`). That is every test whose name
mentions a pattern, transform, hole, fillet, chamfer, revolve or extrude:
233 tests, including the Linear Pattern suite. Every run passed:
1165/1165 in Release (405.9 s) and 1165/1165 in Debug (421.1 s).

## Legacy Regression

P0–P11-FEAT-005: **PASS**. All 488 tests of the P11-FEAT-005 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-005/ctest-release.log`: each of its 487 distinct names (two
older test cases share a name) appears as "Passed" in each new log. That
includes all 39 Linear Pattern tests (30 feature, 7 file, 1 validation,
1 CLI), which run through the new shared pattern subsystem.

No legacy test was changed: the diff of every existing test file only adds
lines (0 removed). Changes to existing code:

- the linear pattern's regeneration now uses the shared
  `PatternSupport`, with unchanged behaviour and messages;
- `kMaxPatternInstances` moved to `Pattern.hpp`, still reachable through
  `LinearPatternFeature.hpp`;
- the OCCT transform adapter was factored into one copy-and-check path, and
  `translated()` keeps its messages;
- validation and `checkTarget` know circular patterns.

New tests were only appended to `TransformTests.cpp`, `ValidationTests.cpp`,
`CliTests.cpp` and `PatternModels.hpp`.

## Known Limitations

- **The axis is explicit model coordinates.** It does not reference a
  sketch line, a model edge or a datum axis, so it does not follow the
  geometry when the model moves (e.g. if a flange is moved, the pattern axis
  stays). The Revolve's sketch-relative axis does not apply to patterns (see
  Axis).
- **Not implemented:** symmetric patterns, suppressed or skipped instances,
  and per-instance variation. A pattern is all or nothing.
- **Unsupported sources:** patterns of patterns (both ways between linear
  and circular), intersect extrudes and revolves, and sketches are refused.
- **Overlapping holes** are refused by the Hole's containment policy,
  including a hole on the axis. Overlapping new-body, join and cut instances
  are allowed.
- **References:** chamfer, fillet and hole instances use the features'
  geometric references rotated exactly. There is no semantic topology
  naming, so an instance whose rotated reference is missing or ambiguous
  fails the pattern.
- **Performance:** quadratic in the count (see Performance); at most 500
  instances.
- **Instances are not document objects.** They are identified by the
  pattern and their index.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: the 233 related tests run 5 times.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including rotated copies. The relative errors are:
  - cube about (1, 1, 1): 5.7e-16;
  - 4 cubes: 2.3e-16;
  - touching: 1.1e-16;
  - overlapping: 1.5e-16;
  - 36 cubes: 1.9e-15;
  - bolt circle: 2.7e-16.
- `circular-pattern-values-release.txt`: measured feature-level volumes,
  areas, bounds, angles and distances, as printed by the tests.
- `performance/circular_pattern_timing.cpp` and
  `performance/circular-pattern-timing-release.log`: the timing measurement.

## Final Result

**PASS.** P11-FEAT-006 Circular Pattern is implemented and verified in all
three presets with zero warnings:

- full-circle, included-angle and angle-step patterns about any 3D axis;
- both directions;
- patterns of new-body, join and cut extrudes and revolves, holes,
  chamfers and fillets;
- explicit count semantics (the source included), with no instance at 360°;
- rotations computed from the source for every instance, in a deterministic
  order;
- atomic failure naming the instance and its angle;
- analytic validation, regeneration, undo/redo, save/load, STEP and STL.

The circular pattern shares one pattern subsystem with the linear pattern.
Symmetric patterns and axis references are not implemented.
