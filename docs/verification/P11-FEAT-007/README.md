# P11-FEAT-007 — Mirror Verification

## Status

PASS

Date: 2026-09-15. `main` was at `0c0e6c0` (P11-FEAT-006 Circular Pattern)
before this milestone.

**Starting state.** An earlier, interrupted session had left an uncommitted
Mirror draft in the working tree. It had never been built: it did not
compile (an ambiguous `sameCurve` overload in `Edges.cpp`). This session
reviewed the draft file by file and fixed the compile error. It then added
what the milestone lacked: save/load, STEP and STL tests, hole types and
extents, a revolved cut, the straddling-hole case, the redo checks, the
mirror cases in `examples/geometry_accuracy`, and documentation. Nothing
below relies on the draft's own claims; every value comes from this
session's runs.

## Scope

Every item was checked against the working tree and by tests.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Persistent Mirror feature (`MirrorDefinition`) | IMPLEMENTED | `MirrorFeature_DefinitionIsValidatedOnCreateAndEdit`; `MirrorFeature_DependsOnItsSourceAndOffsetParameter` |
| Stable source reference (the source's `FeatureId`) | IMPLEMENTED | `MirrorFeature_ReusesStableSourceReference`; `MirrorFeature_SaveLoadPreservesDefinition` |
| Explicit 3D plane (origin, normal as given, offset literal or driven) | IMPLEMENTED | `MirrorFeature_ReflectsAcrossOffsetPlane`; `MirrorFeature_SaveLoadKeepsAnArbitraryPlane` |
| Plane validation (finite origin and offset, finite non-zero normal) | IMPLEMENTED | `MirrorFeature_RejectsZeroNormal`; `MirrorFeature_RejectsNonFinitePlane` |
| Reflection transform (Householder, det −1) | IMPLEMENTED | `RigidTransform_ReflectionMatchesPointFormula`; `…IsAnInvolution`; `…NegatesSignedDistanceToThePlane` |
| Arbitrary-plane reflection (X, Y, Z, skew, offset planes) | IMPLEMENTED | `MirrorFeature_ArbitraryPlaneMatchesAnalyticPointReflection` |
| Offset plane | IMPLEMENTED | `MirrorFeature_ReflectsAcrossOffsetPlane` |
| Double-mirror invariant (unit and geometry level) | IMPLEMENTED | `RigidTransform_ReflectionIsAnInvolution`; `MirrorFeature_DoubleMirrorReturnsOriginal`; `Transform_MirroredBodyKeepsVolumeAndPointsOutward` |
| Volume, area and centroid invariance | IMPLEMENTED | `MirrorFeature_PreservesVolume`; `MirrorFeature_ReflectsCubeAcrossYZPlane`; `MirrorFeature_SaveLoadKeepsAnArbitraryPlane` |
| Orientation of mirrored solids (outward faces, positive volume) | IMPLEMENTED; a kernel-adapter bug was found and fixed (see Orientation) | `Transform_MirroredBodyKeepsVolumeAndPointsOutward`; `regression/face-normal-fix-regression.log` |
| Body mirror, keep original / image only | IMPLEMENTED | `MirrorFeature_KeepOriginalDoublesNonOverlappingBodyVolume`; `MirrorFeature_BodyScopeMirrorsTheWholeBody` |
| Additive feature mirror (join extrude) | IMPLEMENTED | `MirrorFeature_AdditiveBossMirrorMatchesAnalyticVolume` |
| Subtractive mirror (hole of every type and extent; cut extrude; cut revolve) | IMPLEMENTED | `MirrorFeature_HoleMirrorMatchesAnalyticVolume`; `MirrorFeature_MirrorsHoleTypesAndExtents`; `MirrorFeature_AdditiveBossMirrorMatchesAnalyticVolume`; `MirrorFeature_WorksWithRevolvedBody` |
| Mirror of Chamfer and Fillet | IMPLEMENTED (exactly reflected edge references; see Reference Safety) | `MirrorFeature_MirrorsChamferAndFilletByMirroredReferences` |
| Mirror of Extrude and Revolve bodies | IMPLEMENTED | `MirrorFeature_WorksWithExtrudedBody`; `MirrorFeature_WorksWithRevolvedBody` |
| Plane through, touching or centred on the source; symmetric sources | DEFINED POLICY (see Mirror Semantics) | `MirrorFeature_PlaneThroughSourceGeometry` |
| Geometry validity | IMPLEMENTED | every result through the shared build loop; `transformed()` checks each image |
| Regeneration (source, plane origin, offset, normal) | IMPLEMENTED | `MirrorFeature_RegeneratesWhenSourceChanges`; `MirrorFeature_RegeneratesWhenPlaneMoves` |
| Atomic failure, image-specific diagnostics | IMPLEMENTED | `MirrorFeature_FailsAtomicallyWhenMirroredFeatureInvalid`; `MirrorFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Save/load | IMPLEMENTED | `MirrorFeature_SaveLoadPreservesDefinition`; `…SaveLoadKeepsAnArbitraryPlane`; `…FailedMirrorSavesAndLoadsUnchanged`; `…DataIsStoredAsTransparentJson`; `…MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo (create, plane position, normal, scope, keep-original) | IMPLEMENTED | `MirrorFeature_UndoRedoRestoresGeometry` |
| STEP/STL | IMPLEMENTED | `MirrorFeature_ExportsStep`; `MirrorFeature_ExportsClosedStl` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `MirrorFeature_InvalidInputsFailWithStructuredDiagnostics`; `MirrorFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe mirrors` |
| Determinism | IMPLEMENTED | `MirrorFeature_RegenerationIsDeterministic` |
| Mirror plane from a datum plane, planar face or sketch plane (optional) | **NOT IMPLEMENTED** | — |
| Named global XY/YZ/ZX planes (optional) | **NOT IMPLEMENTED** (expressed as explicit planes) | — |
| Feature mirror of a pattern or mirror; pattern of a mirror | **REFUSED** (a body mirror of a pattern or mirror is supported) | `MirrorFeature_RefusesUnsupportedSources`; `MirrorFeature_BodyScopeMirrorsTheWholeBody` |

**Supported sources.**

- **Feature scope:** extrude and revolve features whose operation is new
  body, join or cut; hole features (all types and extents); chamfer and
  fillet features. These are exactly the pattern sources. Intersect
  extrudes and revolves, sketches, patterns and mirrors are refused.
- **Body scope:** any feature with a body, patterns and mirrors included.

This is not SolidWorks-style feature mirroring in general. A feature mirror
re-applies the source's own operation, reflected, to the body the source
made. A body mirror reflects the source's resulting body. Semantic topology
naming is not claimed.

## Architecture

```text
MirrorFeature (bettercad_features)                    include/bettercad/features/MirrorFeature.hpp
  └─ regenerateMirror()                               src/features/pattern/MirrorRegeneration.cpp
       ├─ resolveMirrorReflection(): origin + offset·n̂ (offset literal or a length parameter), unit n̂
       │    → mirrorReflection() → RigidTransform3D::reflection(p0, n̂)
       ├─ feature scope: self-coincidence checks (hole, chamfer/fillet edges), then
       │    shared pattern support (private)          src/features/pattern/PatternSupport.{hpp,cpp}
       │    ├─ instanceOperation(source): the source's own operation, resolved once
       │    │    ├─ extrude/revolve: extrudeTool()/revolveTool() + geometry::transformed(Body)
       │    │    │                   + booleanUnion()/booleanDifference()
       │    │    ├─ hole:            geometry::transformed(HoleRequest) + cutHole()
       │    │    └─ chamfer/fillet:  geometry::transformed(EdgeSignature) (+ reference side) + chamferEdges()/filletEdges()
       │    └─ buildPattern(): instance 0 = the source's body → instance 1 = the image → validation
       └─ body scope: geometry::transformed(source body) (+ booleanUnion() with the original) through buildPattern()
RigidTransform3D (bettercad_core, header only)        include/bettercad/core/math/RigidTransform.hpp
geometry::transformed(Body, RigidTransform3D)         include/bettercad/core/geometry/Transform.hpp
  └─ OCCT adapter: gp_Trsf::SetValues (negative for a reflection)
     + BRepBuilderAPI_Transform (copy) + image-volume check              src/core/geometry/occt/OcctTransform.cpp
face descriptor: surface normal = XDirection × YDirection               src/core/geometry/occt/OcctFaces.cpp
```

No OCCT type leaves `occt/`. `gp_Trsf` stays in the adapter
(`architecture.layering` passes). The conceptual `GeometryService` is the
function API of `bettercad_geometry`.

**Transform infrastructure (architectural change).** `RigidTransform3D`
held only rotations and translations up to P11-FEAT-006 ("It never scales or
mirrors"). A mirror is not faked as a rotation. The class is widened to a
Euclidean isometry, p' = A·p + t with A orthogonal:

- `RigidTransform3D::reflection(pointOnPlane, normal)` builds Householder's
  A = I − 2·n·nᵀ with t = p0 − A·p0;
- `reversesOrientation()` returns the sign of det A;
- `rotationMatrix()` is renamed `matrix()`.

`rotation()` keeps its arithmetic (the same Rodrigues matrix and
t = o − A·o, now in the shared `fixPoint()`), so rotations and translations
are bit-identical to before. All 48 Circular Pattern and 39 Linear Pattern
tests pass unchanged (see Legacy Regression). A second transform type was
rejected: patterns and mirrors share every reference `transformed()`
function.

**Reused, not duplicated:**

- the regenerator, dependency graph and commands (`CreateFeatureCommand` /
  `ModifyFeatureCommand` as `CreateMirrorCommand` / `ModifyMirrorCommand`);
- result bodies, validation and the JSON reader;
- the pattern subsystem: `instanceOperation()`, `buildPattern()` and the
  reference `transformed()` functions for edges, faces and hole requests;
- each source's own evaluation and checks (the Hole's placement and
  containment, the blend fit checks that guard the OCCT ChFi3d crash).

**New:**

- core math: `RigidTransform3D::reflection()`, `reversesOrientation()`;
- geometry: reflections through `transformed()`, with the image-volume
  check; the planar-face normal fix; a public `sameCurve()` (a wrapper of
  the existing edge matcher);
- features: `MirrorFeature.hpp`, `MirrorFeature.cpp`,
  `MirrorRegeneration.cpp`, the regenerator handler, the commands, and
  validation of the offset parameter (length);
- io: the `mirror` JSON mapping;
- CLI: `info` describes mirrors;
- mirror cases in `examples/geometry_accuracy`;
- test fixtures `CubeMirrorModel`, `HoleMirrorModel` and `BossMirrorModel`
  in `tests/support/MirrorModels.hpp`;
- `docs/architecture.md`.

**Transactions.** The mirror builds its whole body before anything is
stored. A failure fails the mirror, and the regenerator keeps no body for it
(P8 policy). The source, its body and the document are unchanged, and
dependents are blocked. Definitions are validated before a command changes
the document.

## Mirror Semantics

- **The formula.** Every point goes to p' = p − 2((p − p0)·n̂)n̂, and every
  direction to v' = v − 2(v·n̂)n̂.
- **Scope.**
  - `Feature` applies the source's own operation, reflected, once more to
    the body the source made (one extra pattern instance): a boss gets a
    mirrored boss, a hole a mirrored hole, a chamfer or fillet the mirrored
    edge, and a new-body extrude or revolve a mirrored copy.
  - `Body` reflects the source's whole body, with everything that built it.
- **Keep original.**
  - A feature mirror always keeps the original: its image is added to the
    source's body. `keepOriginal = false` with the feature scope is refused
    with InvalidArgument.
  - A body mirror with `keepOriginal = true` unites the image with the
    original. Disjoint bodies stay separate solids; touching or overlapping
    ones fuse.
  - A body mirror with `keepOriginal = false` is the image alone (a
    mirror-only result).
- **Result ordering.** Instance 0 is the source's body and instance 1 its
  image, built in that order. The image is not a document object; it is
  identified by the mirror (index 1). Within a kept-original body, the solid
  order is the kernel's union output. It is deterministic (bit-identical
  across regenerations, `MirrorFeature_RegenerationIsDeterministic`) but not
  exposed through an API.
- **Plane through the source.**
  - A body that straddles or touches the plane fuses with its image.
  - A body centred on the plane coincides with its image: the union is the
    body itself, 1000 mm³ for the 10 mm cube.
  - A boss symmetric about the plane is its own image: the join changes
    nothing (50500 mm³).
- **Coincident holes and blends are refused explicitly.** Re-applying them
  would drill or blend what is already there. A hole the plane maps onto
  itself (the same axis for a through hole, or the same entry and direction
  for a blind one) is refused, as is a chamfer or fillet edge the plane maps
  onto one of the feature's own edges. Both fail with FailedPrecondition and
  a message saying so. A hole image that only overlaps the source is refused
  by the Hole's own containment rule.

## Plane Representation

`MirrorPlane`:

| Field | Meaning | Validation |
| --- | --- | --- |
| `origin` | a point (`Point3D`, lengths) | finite |
| `normal` | a `Vector3D` as given, normalized when used; its sense does not matter | finite, non-zero ("the plane's normal must be a finite, non-zero vector, got (0, 0, 0)") |
| `offset` | a `Length` along the unit normal: the plane passes through origin + offset·n̂ | finite when literal ("the plane's offset must be finite, got inf mm") |
| `offsetParameter` | optional length parameter driving the offset | a valid ID; DimensionMismatch at regeneration and in validation otherwise |

The normal is stored as entered, like the linear pattern's direction and the
circular pattern's axis, so a file holds exactly what the user gave. The
offset lets a parameter drive the plane, e.g. the plane x = `mid` through the
origin along X. Reversing the normal and negating the offset gives the same
plane (tested: (1, 0, 0) with +10 mm and (−2, 0, 0) with −10 mm give the same
image). A driving parameter cannot hold NaN or infinity at all (the document
refuses it).

## Reflection Mathematics (unit level)

Independent reference in the tests: p' = p − 2((p − p0)·n̂)n̂, written without
BetterCAD math.

| Check | Result |
| --- | --- |
| p = (4, 1, 3), p0 = (1, 0, 0), n = (1, 1, 0)/√2: expected (0, −3, 3) | transform (8.7e-16, −2.99999999999999911, 3) mm; the formula evaluated in doubles gives (8.9e-16, −2.99999999999999911, 3); the feature-resolved motion is 1.24e-15 mm from (0, −3, 3) |
| Axis planes through the origin | one coordinate changes sign, exactly |
| Offset plane x = 10: (30, 2, 3) | (−9.99999999999999822, 2, 3) mm, expected (−10, 2, 3) |
| 7 normals × 2 origins × 3 points, and directions | match the formula within 1e-12 mm (directions within 1e-15) |
| Involution: A·A − I, det A | largest entry 6.7e-16; det A = −1.00000000000000044 |
| M(M(p)) = p, for points up to 2.3e3 mm from the origin | worst 2.03e-12 mm |
| Signed distance: d(p′) = −d(p); p′ − p ⟂ plane | worst 2.84e-14 mm |
| Points on the plane stay put | 4.3e-16 mm |
| Rotations, translations and the identity | `reversesOrientation()` false |

## Basic Body Test

`MirrorFeature_ReflectsCubeAcrossYZPlane`:

| | |
| --- | --- |
| Source | 10 mm cube [15, 25] × [−5, 5] × [−5, 5] mm (a new-body extrude driven by `size`), centre (20, 0, 0) |
| Plane | x = 0: origin (0, 0, 0), normal (1, 0, 0) |
| Expected mirrored centre | (−20, 0, 0) mm (formula) |
| Actual mirrored centre (image only) | (−20.00000000000000355, 1.1e-15, 0) mm; error 3.6e-15 mm |
| Source volume | 1000.00000000000034 mm³ |
| Mirrored volume (image only) | 1000.00000000000034 mm³ |
| Keep original | 2000.00000000000068 mm³, area 1200 mm² (exact), 2 solids, centre of mass (1.4e-15, 1.1e-15, 0) |
| Bounds (keep original) | (−25, −5, −5) to (25, 5, 5) mm, i.e. X in [−xmax, −xmin] ∪ [xmin, xmax] = [−25, 25] |
| Bounds (image only) | (−25, −5, −5) to (−15, 5, 5) mm |
| Orientation | the image's sides are found facing out: x = −25 facing −X, x = −15 facing +X; none at x = −25 facing +X |
| Validity | valid |
| Result | **PASS** |

## Offset Plane Test

`MirrorFeature_ReflectsAcrossOffsetPlane`:

- **Source:** the cube centred at x = 30.
- **Plane:** x = 10.
- **Expected image:** centred at 2·10 − 30 = −10, spanning X from 2a − 35 = −15
  to 2a − 25 = −5.
- **Actual:** centre (−10.00000000000000178, 1.1e-15, 0) mm, bounds
  (−15, −5, −5) to (−5, 5, 5) mm, V = 1000.00000000000034 mm³.
- **With the original:** 2000 mm³ from x = −15 to 35.

The same image results from:

- the plane through the origin offset 10 mm along +X;
- the normal (−2, 0, 0) with offset −10 mm;
- the offset driven by `plane_x` = 10 mm. Then `plane_x` = 20 mm rebuilds only
  the mirror, and the image is centred at 2·20 − 30 = 10 (checked by the
  formula).

**Result: PASS.**

## Arbitrary Plane Test

`MirrorFeature_ArbitraryPlaneMatchesAnalyticPointReflection`, "pegs": a peg
(r = 3 mm, z = 17 … 23 mm about x = 40, y = 10) mirrored on its own. Each
reflected rim circle must be found on the body where the formula puts it,
with its axis reflected. The error is the larger of |d(p′) + d(p)| and
|p′ − formula| over both rims.

| Plane (through, normal) | V (expected 54π = 169.64600329384882) | Largest rim error | Centre of mass vs reflected (40, 10, 20) |
| --- | --- | --- | --- |
| (0, 0, 0), X | 169.64600329384885 | 0 mm | (−40.000000000000007, 10.000000000000002, 20.000000000000004) |
| (0, 0, 0), Y | 169.64600329384885 | 0 mm | (40.000000000000007, −10.000000000000002, 20.000000000000004) |
| (0, 0, 0), Z | 169.64600329384885 | 0 mm | (40.000000000000007, 10.000000000000002, −20.000000000000004) |
| (0, 0, 0), (1, 1, 0) | 169.64600329384885 | 1.33e-14 mm | (−9.999999999999989, −39.999999999999986, 19.999999999999993), expected (−10, −40, 20) |
| (1, 0, 0), (1, 1, 0) | 169.64600329384885 | 1.42e-14 mm | (−8.999999999999988, −38.999999999999993, 19.999999999999993), expected (−9, −39, 20) |
| (5, −2, 1), (1, 1, 1) | 169.64600329384896 | 2.45e-14 mm | (−4.0000000000000098, −34.000000000000007, −24), expected (−4, −34, −24) |

Tolerances: 1e-9 mm for positions, 1e-12 relative for volumes. **Result:
PASS.**

Face normals mirror as vectors. Across the plane with normal (1, 1, 0), the
cube's +X face (x = 25) is found through (0, −25, 0) facing (0, −1, 0), and
its +Y face facing −X; it is not found facing the other way.

## Double Mirror Invariant

`MirrorFeature_DoubleMirrorReturnsOriginal`: the cube is mirrored across the
skew plane through (3, −2, 1) with normal (1, 2, 2), and that image is
mirrored again across the same plane (a body mirror of the mirror).

| | Expected | Actual |
| --- | --- | --- |
| Once: centre of mass | (15.777777777777779, −8.444444444444443, −8.444444444444443) mm (formula) | (15.777777777777777, −8.444444444444445, −8.444444444444445) mm |
| Twice: volume | the source's, 1000.00000000000034 | 1000.00000000000034 mm³ (bit-identical) |
| Twice: area | 600 | 600.00000000000011 mm² |
| Twice: centre of mass | (20, 0, 0) | (20.000000000000004, 1.7e-15, −1.3e-15) mm |
| Twice: bounds | (15, −5, −5) to (25, 5, 5) | (14.999999999999996, −5.0000000000000018, −5.0000000000000009) to (25.000000000000007, 5.0000000000000053, 5.0000000000000018) mm |
| Twice: faces | x = 15 facing −X, x = 25 facing +X | both found |

At the kernel level (`Transform_MirroredBodyKeepsVolumeAndPointsOutward`),
the cube mirrored twice across (1, 1, 0) is back at (15, −5, −5) to
(25, 5, 5) mm. `geometry-accuracy-release.txt` gives 1000.0000000000001 mm³.
**Result: PASS.**

## Volume Invariance

| Case | Source | Image | Error |
| --- | --- | --- | --- |
| Extruded cube across (1, 2, 2) through (3, −2, 1) | V 1000.00000000000034, A 600 | V 1000.00000000000102, A 600.00000000000023; centre (15.777777777777777, −8.444444444444445, −8.444444444444445), formula (15.777777777777779, −8.444444444444443, −8.444444444444443) | 6.8e-16 relative |
| Drilled block (100 × 50 × 20, 10 mm hole) across (1, 1, 1) | V 98429.203673205106 (analytic) | V 98429.203673205222, A 16471.238898038479 (source 16471.238898038471) | 1.2e-15 relative |
| Turned part (revolve, bored, grooved) across (0.3, −0.1, 0.9) | V 7720π = 24253.095285713203 | 24253.095285713178; centre of mass within 7.1e-15 mm of the reflected source centre | 1.0e-15 relative |

Kernel volumes of a body and its image, compared directly
(`geometry-accuracy-release.txt`): at most 5.87e-16 relative over a cube,
cylinder, sphere, torus, countersunk block, three-edge fillet corner and
Steinmetz solid. `transformed()` refuses an image whose volume differs by
more than 1e-9 relative. **Result: PASS.**

**Keep original** (`MirrorFeature_KeepOriginalDoublesNonOverlappingBodyVolume`):
for a disjoint pair, V_total = 2V. The feature scope and the body scope with
the original each give 2000.00000000000068 mm³ in 2 solids. The body scope
without it gives 1000.00000000000034 mm³ in 1 solid.

## Hole Mirror Validation

`MirrorFeature_HoleMirrorMatchesAnalyticVolume` (the flagship case):

| | |
| --- | --- |
| Block | 100 × 50 × 20 mm (a parametric extrude) |
| Hole | 10 mm through hole, from the bottom face, at x = 30, y = 25 (driven by `diameter`, `hole_x`, `hole_y`) |
| Plane | the plane through the origin facing +X, moved by `mid` = 50 mm: x = 50 |
| Expected mirrored centre | (70, 25) mm, by the formula (exact) |
| Actual mirrored centre | the rims are found as circles r = 5 mm at x = 70 (and 30) on both faces, z = 0 and z = 20; none at x = 50 or 10. The resolved motion maps (30, 25, 0) to (70, 25, 0) exactly |
| Formula | V = LWH − 2π(d/2)²H |
| Expected volume | 100000 − 1000π = 96858.407346410211 mm³ |
| Actual volume | 96858.407346410226 mm³ |
| Error | 1.5e-11 mm³ (1.5e-16 relative) |
| Tolerance | 1e-12 relative (`kRelTight`) |
| Area | 16942.477796076942 mm², expected 16000 + 300π = 16942.477796076939 |
| Through behaviour | both holes open on both faces; still so at 40 mm thick (see Regeneration) |
| Validity | valid, 1 solid; the mirror is the only result body |
| Result | **PASS** |

The source hole's body is an unchanged intermediate: 98429.203673205135 mm³
(expected 98429.203673205106).

**Hole types and extents** (`MirrorFeature_MirrorsHoleTypesAndExtents`), at
(30, 25) in the top face:

| Hole | Plane | Expected V | Actual V | Rims found |
| --- | --- | --- | --- | --- |
| Blind counterbore: d 10, depth 12, counterbore 16 × 4 (456π each) | x = 50 | 100000 − 912π = 97134.867499926113 | 97134.867499926157 | r 8 at z 20 and 16, r 5 at z 16 and 8, none at z 0; at x 30 and 70 |
| Through countersink: d 10, 20 mm at 90° ((500 + 500/3)π each) | x = 50 | 95811.209795213610 | 95811.209795213625 | r 10 at z 20, r 5 at z 15 and 0; at x 30 and 70 |
| Blind d 10 × 6 mm from the top | z = 10 | 100000 − 300π = 99057.522203923058 | 99057.522203923087 | r 5 at z 20, 14, 6, 0: the image enters the bottom face |

## Additive Mirror

`MirrorFeature_AdditiveBossMirrorMatchesAnalyticVolume`:

- **Model:** a 10 × 10 × 5 mm boss (a join extrude on z = 10), centred at
  x = 30 on a 100 × 50 × 10 mm plate, mirrored across x = 50.
- **Formula:** V = V_plate + 2V_boss = 51000 mm³.
- **Actual:** 51000.000000000015 mm³, 1 solid, valid.
- **Checks:**
  - Two boss tops on z = 15 total 200 mm², and the plate's top keeps
    4800 mm².
  - The sides are found at x = 25, 35 (source) and 65, 75 (image, 2·50 − x),
    each facing out.
  - The maximum z is 15.
- **As a pocket** (a cut 5 mm down): V = 50000 − 1000 = 49000.000000000015
  mm³; two floors on z = 5 total 200 mm².

**Result: PASS.**

## Chamfer and Fillet Mirror (Reference Safety)

A mirrored chamfer or fillet is applied with its edge references reflected
exactly: a line's point and direction, or a circle's centre and axis. A
two-distance chamfer's reference side is reflected as a vector. Each
reflected reference then goes through the feature's normal rules:

- **Unique:** it proceeds.
- **Missing or ambiguous:** the mirror fails.
- **Never:** an edge is never substituted. The blend fit checks (the OCCT
  crash guard) run for the image.

`MirrorFeature_MirrorsChamferAndFilletByMirroredReferences`:

| Case | Expected | Actual |
| --- | --- | --- |
| Asymmetric chamfer (3 mm on the −X side, 5 mm on top) of the top-left edge, across x = 50 | V = 100000 − 2·½·3·5·50 = 99250; top face (100 − 5 − 5)·50 = 4500 (keeping −X as the side would give 4600) | 99250.000000000015; top 4500.0000000000027; each end face 850 |
| Chamfered hole rim, 1 mm, across x = 50 | V_holes − 2π(5 + 1/3) = 96824.897024771926 | 96824.897024771926; rims at radius 6 at x 30 and 70 |
| Filleted hole rim, 2 mm | 96799.653053257804 | 96799.653053257818; rims at radius 7 |
| … the fillet's radius 2 → 1.5 mm | 96826.035724011716 | 96826.035724011730; rebuilds the fillet and the mirror; no stale radius-7 rim |
| The chamfer across x = 45 (the image rim does not exist) | NotFound, no substitution | "…chamfer: edge reference 1 (circle around (60, 25, 20) mm with axis (0, 0, 1) and radius 5 mm) matches no edge of the body" |
| Both top edges chamfered, across x = 50 (each is the other's image) | refused | "… of edge reference 1 (line through (0, 0, 20) mm along (0, 1, 0)) of the chamfer 'Bevel' is its edge reference 2 (…), which is already chamfered, so there is nothing to mirror onto" |
| The same across y = 25 (each edge is its own image) | refused | "… is that edge itself, which is already chamfered" |
| The same across z = 10 (images are the bottom edges) | V = 100000 − 4·½·4·50 = 99600 | 99600.000000000029; bottom face 4800 |
| A filleted rim across a plane through its axis | refused | "… of the fillet 'Soften' is that edge itself, which is already filleted …" |

**Chamfer: PASS. Fillet: PASS.** Both are limited to what geometric edge
references can express (see Known Limitations).

## Extruded and Revolved Sources

- **Extrude** (`MirrorFeature_WorksWithExtrudedBody`). Sketch → extrude →
  mirror, with the cube's `size` driving both cubes. Size 6 gives
  V = 2·6³ = 432 with X bounds ±21; size 14 gives 5488 (2·14³) with X bounds
  ±29. Each change rebuilds the sketch, the extrude and the mirror.
- **Revolve** (`MirrorFeature_WorksWithRevolvedBody`):
  - the turned part's revolve (R = 15, h = 40 about Z) across x = 30: 2
    valid solids. V = 2πR²h: 56548.667764616264 actual, 56548.667764616272
    expected. X runs from −15 to 75, and the image's top rim is at x = 60;
  - the revolve's sweep 360° → 270° drives both: 42411.500823462207
    (exact);
  - the whole turned part (bored and grooved) mirrored on its own:
    24253.095285713192 against 7720π = 24253.095285713203; its bore rim is
    at x = 60;
  - a revolved *cut* (the groove, r 13 … 16 mm at z 15 … 20) as a feature
    mirror across z = 20: the band z 15 … 25 is cut. V = 7440π:
    23373.449342708038 actual, 23373.449342708060 expected. The floor runs
    from z = 15 to 25, and no wall is left at z = 20.
- **Save/load of a revolve mirror:** see Persistence (arbitrary plane).

**Result: PASS.**

## Regeneration

**Source** (`MirrorFeature_RegeneratesWhenSourceChanges`):

| Change | Rebuilt | V actual (mm³) | V expected (mm³) |
| --- | --- | --- | --- |
| hole diameter 10 → 12 mm | Drill, Mirror | 95476.106578830717 (both rims at r 6, none at r 5) | 95476.106578830702 |
| block 20 → 40 mm thick | Pad, Drill, Mirror | 193716.81469282045 (both holes open at z 0 and 40: still through) | 193716.81469282042 |
| hole x 30 → 20 mm | Drill, Mirror | 96858.407346410226; holes at 20 and 80, none left at 30 or 70 | 96858.407346410211 |
| boss 10 → 14 mm | BossSketch, Boss, Mirror | 51960.000000000022; sides at 39 and 61, none at 65 | 51960 |
| fillet radius 2 → 1.5 mm | Soften, Rounds | see Chamfer and Fillet Mirror | |
| cube size 6, 14 mm; revolve sweep 270° | see Extruded and Revolved Sources | | |

**Plane** (`MirrorFeature_RegeneratesWhenPlaneMoves`), with the hole at
x = 30:

| Change | Rebuilt | Expected image | Actual |
| --- | --- | --- | --- |
| `mid` 50 → 60 mm (x′ = 2a − x) | Mirror only | 70 → 90 | rim at 90, none at 70; V 96858.407346410226 |
| plane origin (60, 0, 0), no offset | Mirror only | 90 | rim at 90 |
| normal (0, 1, 0) through (0, 16, 0) | Mirror only | (30, 7) | rim at (30, 7), none at (70, 25); V unchanged |

No stale mirrored copy remains in any case. **Result: PASS** (source,
plane origin, plane normal).

## Atomic Failure

`MirrorFeature_FailsAtomicallyWhenMirroredFeatureInvalid`, on the flagship
model:

- **`mid` = 64 mm:** the image of the hole at x = 30 would be at 98, 2 mm from
  the block's end. The mirror fails with `FailedPrecondition`:

  > Mirror: mirror: the mirror image across the plane through (64, 0, 0) mm
  > facing (1, 0, 0): hole: the hole does not fit on its face: its entry is
  > 10 mm across, but the centre (98, 25) mm is only 2 mm from the face's
  > edge (a hole must stay at least 0.001 mm inside its face)

  The message names the mirrored instance (the image and its plane), the
  reflected centre and the underlying Hole containment failure.
- **What remains:**
  - The mirror keeps no body, so no half-applied mirror is shown.
  - The document equals the edited one; regeneration does not change it.
  - The source hole is up to date, with its body unchanged (bit-identical
    volume).
- **Recovery:** back at `mid` = 50 the mirror rebuilds, bit-identical.
- **`mid` = 80 mm:** "…: hole: the centre (130, 25) mm is not on a face of the
  body …".

**Also** (`MirrorFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`):

- An invalid edit (normal (0, 0, 0)) through `ModifyMirrorCommand` changes
  nothing and records nothing. The revision is unchanged, and there is
  nothing to undo.
- A valid edit the geometry cannot take fails at regeneration only, and
  undo restores the working model exactly.

**Result: PASS.**

## Persistence

`MirrorFeature_SaveLoadPreservesDefinition`:

- **Setup:** the hole mirror with every field in use. The origin
  (0, 12, −3) mm is off the X axis (a move within the plane), the normal is
  stored as given (2, 0, 0) and the offset is driven by `mid`.
- **Round trip:** saved, replaced by an empty document, loaded and
  regenerated.
- **Checked:**
  - `equivalent()` documents with identical item IDs and the same
    dependencies for every node (the mirror depends on Drill and `mid`);
  - the definition equal field for field: the source `FeatureId`, the
    plane's origin, normal and offset parameter, the scope and
    keep-original;
  - the resolved reflection identical;
  - the same regeneration order;
  - bit-identical volume, area, centroid, bounds and topology for Pad,
    Drill and Mirror.
- **Still parametric after loading:**
  - `mid` = 60 rebuilds only the mirror, and the image moves to x = 90;
  - diameter 12 mm rebuilds Drill and Mirror: 95476.106578830731 mm³.
- **Second round trip:** a body mirror without the original. The definition
  comes back equal with bit-identical geometry, bounds (20, 0, 0) to
  (120, 50, 20) mm.

**Plane precision** (`MirrorFeature_SaveLoadKeepsAnArbitraryPlane`):

- **Model:** the turned part (a revolve source) mirrored alone across the
  plane through (5, −2, 1) mm with normal (0.3, −0.1, 0.9).
- **Stored as:** `"origin": [0.005, -0.002, 0.001]`,
  `"normal": [0.3, -0.1, 0.9]`, `"offset": 0.0`, `"scope": "body"`,
  `"keep_original": false`.
- **Reloaded:** the definition compares equal, bit for bit (`operator==` on
  the doubles), with bit-identical geometry and no plane drift.
- **Values:**
  - V = 24253.095285713178 mm³ (7720π);
  - the centre of mass (−10.20765245117577, 3.4025508170585868,
    −10.532283778397765) mm is the reflected source centre within 7.1e-15
    mm;
  - after a 270° sweep: 18189.821464284887 mm³ (expected
    18189.821464284902).

**Also:**

- **A failed mirror** (`mid` = 64) saves and loads unchanged, with the same
  error, and recovers.
- **The JSON** is checked text for text: `{"source": 9, "plane": {"origin":
  [0.0, 0.0, 0.0], "normal": [1.0, 0.0, 0.0], "offset": 0.0,
  "offset_parameter": 10}, "scope": "feature", "keep_original": true}`. A
  literal offset of 12.5 mm is `"offset": 0.0125` with no
  `offset_parameter`.
- **Malformed data** is rejected with its path:
  - `objects[3].data.plane.normal: expected 3 numbers, got 2` and
    `…normal[1]: expected a number`;
  - `…plane.origin: missing required field` and `…plane.offset: missing
    required field`;
  - `…plane.offset: expected a number` and `…plane.offset_parameter:
    expected an ID (a non-negative integer)`;
  - `…plane.radius: unknown field`, `…plane: expected an object` and
    `…plane: missing required field`;
  - `…scope: unknown value 'left'` and `…keep_original: expected true or
    false`;
  - `…data.copies: unknown field` and `…data.count: unknown field`.
- **Invalid content** is reported at the mirror with the definition's own
  rules (InvalidArgument):
  - `objects[3].data: the plane's normal must be a finite, non-zero vector,
    got (0, 0, 0)`;
  - the feature scope without the original;
  - source 0;
  - offset parameter 0.
- **No image geometry is serialized.**

`MirrorFeature_ReusesStableSourceReference` covers the source reference
without saving:

- Renaming the source changes nothing.
- Deleting it fails the mirror with NotFound ("object:11 references
  object:9, which does not exist").
- Putting it back with its ID restores the mirror, bit for bit.

**Result: PASS.**

## Undo / Redo

`MirrorFeature_UndoRedoRestoresGeometry`, on the drilled block:

1. **Create** "Mirror" across x = 50 (`CreateMirrorCommand`, "Create mirror
   'Mirror'"): V = 96858.407346410226, image at 70.
2. **Plane position** → x = 60: image at 90.
3. **Plane normal** → (0, 1, 0) through y = 16: image at (30, 7).
4. **Scope** → body, keep-original off: V = 98429.203673205135, Y from −18.
5. **Keep-original** → on: the blocks overlap and fill each other's holes,
   giving V = 100·68·20 = 136000.00000000015.
6. **Undo, then redo, through every state.** Each step compares:
   - document equivalence and the definition (`FeatureId`, source ID, plane,
     scope, keep-original);
   - a bit-identical volume;
   - an identical centre of mass and bounds.
7. **Undo everything:** the mirror is gone, the document equals the initial
   one, and Drill is the result again.
8. **Redo:** the mirror is recreated with the same ID and a bit-identical
   volume.

Commands check the feature kind: `ModifyMirrorCommand` on the hole fails
with NotFound. **Result: PASS.**

## STEP

`MirrorFeature_ExportsStep`, read back with the kernel:

| Model | Solids | Volume (mm³) | Area (mm²) | Bounds (mm) |
| --- | --- | --- | --- | --- |
| Block + hole + mirrored hole (`PRODUCT('Mirror','Mirror'`) | 1, valid | 96858.407346410182 (expected 96858.407346410211; 3.0e-16) | 16942.477796076932 | (0, 0, 0) to (100, 50, 20) |
| Cube + its image | 2, valid | 2000.0000000000007 | 1200 | X ±25, Y and Z ±5 |
| The cube's image alone, across (1, 2, 2) through (3, −2, 1) | 1, valid | 999.99999999985403 (1.5e-13) | 599.99999999983447 | each bound within 4.1e-12 mm of the eight corners reflected by the formula, e.g. X 7.4444444444432 … 24.111111111115 (7.4444444444444 … 24.111111111111) |

Tolerances: 1e-9 relative (decimal STEP text) and 1e-6 mm for bounds.
**Result: PASS.**

## STL

`MirrorFeature_ExportsClosedStl`. A reflection makes face frames
left-handed, so every mesh is checked with the independent tools in
`MeshAnalysis.hpp`:

- **closed:** every edge is shared by two triangles in opposite directions;
- **enclosed volume:** positive, compared with the B-Rep's;
- **facet normals:** agree with the triangle winding and point out of the
  material (away from the centre, for the boxes).

| Model | Triangles | Closed | Enclosed volume (mm³) | Check |
| --- | --- | --- | --- | --- |
| Block + mirrored hole pair, binary and ASCII (0.01 mm deflection) | | yes | 96862.506428179287 vs 96858.407346410211 | difference 4.10 ≤ 0.01 mm × area = 169.4 |
| Cube + image (x = 0) | 24 | yes | 2000 exactly | 0 inward and 0 disagreeing facets in each cube |
| Image alone across x = 0 | 12 | yes | 1000 exactly | 0 inward, 0 disagreeing |
| Image alone across (1, 2, 2) | 12 | yes | 1000 − 5.8e-5 | within the single-precision bound 2.0e-3; 0 inward, 0 disagreeing |
| Drilled block mirrored alone (cylinder faces) | | yes | 98431.253 vs 98429.204 | 2.05 ≤ 164.7 |
| Turned part mirrored alone across (0.3, −0.1, 0.9) (cylinders, annuli, groove) | | yes | 24244.915 vs 24253.095 | 8.18 ≤ 0.01 × 2092π = 65.7; the kernel's area is 6572.2118313098417, against 2092π = 6572.2118313098472 |

**Winding:** the mesher follows each face's parametrization and orientation
(`OcctMesh.cpp`), so mirrored faces mesh outward. No exporter change was
needed, and the tests above are the permanent regression. **Result: PASS.**

## Orientation (a bug found and fixed)

The mirror image of a solid is valid only if its faces still point out of
the material. `transformed()` gives OCCT a negative `gp_Trsf`. The kernel's
copy (`BRepBuilderAPI_Transform`, copying) then reverses every face, and
`transformed()` checks the image's volume (positive, within 1e-9 relative of
the source's).

**Bug.** The face descriptor used by face references (holes, face
matching) took a planar face's normal as its frame's main direction. That
equals the surface normal XDirection × YDirection only for a right-handed
frame, and a reflected plane has a left-handed one. On mirrored bodies every
planar face therefore reported an inward normal. A hole on a mirrored face
could not be placed, and face checks failed.

**Fix.** `OcctFaces.cpp` reverses the direction for a left-handed frame. The
change is correct for any frame and leaves right-handed ones (all earlier
geometry) unchanged: every legacy test passes.

**Regression proof.** `regression/face-normal-fix-regression.log`: with the
three-line fix removed, 26 assertions fail in 7 test cases. Among them:

- `Transform_MirroredBodyKeepsVolumeAndPointsOutward` (10): the outward
  normal of every face of a mirrored box, and a hole on a mirrored face;
- `MirrorFeature_AdditiveBossMirrorMatchesAnalyticVolume` (6).

The file was then restored (SHA-256 checked), and the evidence above is
from the fixed tree.

## Diagnostics

The project's convention is `Result`/`Error`: a stable `ErrorCode` and a
message naming the mirror, the image and its plane, and the cause.
Validation adds the object's name and ID. The suggested categories map to it
as follows:

| Suggested | Code | Message (example, from the tests) |
| --- | --- | --- |
| MirrorMissingSource | NotFound / FailedPrecondition | "object:11 references object:9, which does not exist"; "Mirror: a mirror needs the body of its source feature" (a sketch source) |
| MirrorInvalidPlane | InvalidArgument / DimensionMismatch | "the plane's origin must be finite"; "the plane's offset must be finite, got inf mm"; "Mirror (object:11): the plane's offset is driven by tilt (object:12), which is an angle, not a length" |
| MirrorInvalidNormal | InvalidArgument | "the plane's normal must be a finite, non-zero vector, got (0, 0, 0)" |
| MirrorSourceEvaluationFailed | the source's code; the mirror is blocked | a 0 mm hole diameter fails Drill, and the mirror is blocked with no body |
| MirrorTransformFailed | FailedPrecondition / InvalidArgument / Internal | "transform: the body is empty" (tested); "transform: the motion must be finite"; "transform: the mirror image encloses … mm^3, not the body's … mm^3" |
| MirrorBooleanFailed | Internal | "Mirror: mirror: the mirror image across …: boolean union failed: …" |
| MirrorInvalidResult | Internal | "Mirror: mirror: the pattern produced no valid solid" (shared build-loop wording) |
| MirrorReflectedFeatureInvalid | the feature's code | "Mirror: mirror: the mirror image across the plane through (64, 0, 0) mm facing (1, 0, 0): hole: the hole does not fit on its face: …" |
| MirrorCoincidentGeometryUnsupported | FailedPrecondition | "Mirror: mirror: the mirror image across the plane through (30, 0, 0) mm facing (1, 0, 0) is the hole 'Drill' itself: the plane maps the hole centred at (30, 25, 0) mm onto itself, so there is nothing to mirror"; the chamfer/fillet messages above |
| (feature scope without the original) | InvalidArgument | "a feature mirror keeps its source: the mirrored operation is added to the body the source made (mirror the body to keep only the mirror image)" |
| (unsupported source) | FailedPrecondition | "a feature mirror cannot repeat a linear_pattern; mirror its body instead"; "… a mirror; mirror its body instead"; "repeating an intersect extrude is not supported: …"; "Row: linear pattern: a linear pattern cannot repeat a mirror" |

Kernel exceptions are caught at the geometry boundary
(`occt::guardKernelCall`) and returned as `Internal`.

**Not exercised by a test.** Four defensive paths have no deterministic
trigger; they are the same kernel-failure paths as for patterns:

- a non-finite motion (valid definitions only make finite ones);
- a kernel failure while transforming;
- an image whose volume disagrees with its source's (OCCT reversed the faces
  correctly in every case);
- a boolean failure, or a final result without a valid solid.

**CLI.**

- `bettercad-cli info` describes mirrors, e.g. "source Drill, feature mirror
  across the plane through (0, 0, 0) mm facing (1, 0, 0), offset mid", or
  "…, body mirror …, offset 12.5 mm, mirror image only".
- `validate` reports the hole pair as valid: 1 solid, 96858.407 mm³, bounds
  (0, 0, 0) to (100, 50, 20) mm. The image alone gives bounds (−75, 0, 0) to
  (25, 50, 20) mm.
- `validate` reports a mirror that does not fit as an error and exits 1.

**Result: PASS.**

## Determinism

`MirrorFeature_RegenerationIsDeterministic`: two regenerators give
bit-identical volume, area, centre of mass, bounds and topology, and an
identical resolved reflection. Across the whole suite:

- undo, redo and save/load reproduce geometry bit for bit;
- Debug and Release give identical numbers: the two `-s` captures differ
  only in temporary paths and document UUIDs.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors, from `cmd` under code page 65001.
CTest ran only after a successful build. The qualification ran from
13:38:50 to 14:12:40 (`qualification-times.txt`). No source, test or CMake
file changed after it started (checked by modification time over 270
files). The newest is `OcctFaces.cpp` at 13:38:10: the byte-for-byte
restore after the regression proof, checked by SHA-256.

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 245 files compiled | 0 | **577/577 passed** (155.3 s) |
| Release | exit 0 | exit 0, 245 files compiled | 0 | **577/577 passed** (152.7 s) |
| Debug-shared | exit 0 (shared libraries ON) | exit 0, 245 files compiled | 0 | **577/577 passed** (161.0 s) |

**Compiler warnings: 0** in every preset. P11-FEAT-006 compiled 241 files;
the 4 new ones are `MirrorFeature.cpp`, `MirrorRegeneration.cpp`,
`MirrorFeatureTests.cpp` and `MirrorFileTests.cpp`.

The 41 new tests (all passed in every preset):

- `tests/features/MirrorFeatureTests.cpp`: 27;
- `tests/io/MirrorFileTests.cpp`: 7;
- `tests/core/math/RigidTransformTests.cpp`: 3;
- `tests/core/geometry/TransformTests.cpp`: 2;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

The related tests were also run 5 times in Release and in Debug, one
process per run (`ctest-repeat-*.log`, `--repeat until-fail:5`). That is
every test whose name mentions a mirror, pattern, transform, hole, fillet,
chamfer, revolve or extrude: 274 tests, including the Linear and Circular
Pattern suites and every blend test (the OCCT ChFi3d crash guard). Every
run passed: 1370/1370 in Release (440.4 s) and 1370/1370 in Debug
(445.8 s). No log contains a failure or "Not Run". The 60 lines matching
"Failed" are the names of the six `…_Failed…SavesAndLoadsUnchanged` tests.

**An environment note** (not part of the qualification). A preliminary
Debug run of the draft from Git Bash failed `cli.new.unicode-path`. That
shell's console uses code page 437, so CTest's check script decoded the
CLI's UTF-8 output as `Pl├Ñt`; the file itself held `"name": "Plåt ✓"`. The
same test passed from PowerShell (code page 65001) on the unchanged build.
The qualification therefore runs under code page 65001, as every earlier
qualification did.

## Legacy Regression

P0–P11-FEAT-006: **PASS**. All 536 tests of the P11-FEAT-006 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-006/ctest-release.log`: each of its 535 distinct names (two
older test cases share a name) appears as "Passed", and only as "Passed", in
each new log. The 41 new names are exactly the tests listed under
Qualification.

No legacy test was changed: the diff of every existing test file only adds
lines (0 removed). Changes to existing code:

- **`RigidTransform3D`** is widened to reflections (`reflection()`,
  `reversesOrientation()`; `rotationMatrix()` is now `matrix()`). Rotations
  and translations compute exactly as before.
- **`transformed(Body)`** handles reflections (negative `gp_Trsf`) and checks
  each image's volume. Rotations and translations take the unchanged path,
  with unchanged messages.
- **`OcctFaces.cpp`**: planar face normals follow XDirection × YDirection
  (see Orientation). This is unchanged for right-handed frames.
- **Edge signatures** (`Edges.cpp`) are now made without negative zeros, as
  face signatures already were (P11-FEAT-004/006). A reversed or mirrored
  line or circle is then written and printed like one entered directly
  (e.g. axis "(0, 0, 1)", not "(-0, 0, 1)"); comparisons were never
  affected, since −0 = 0. A public `sameCurve()` wraps the existing matcher.
  The one internal call that became ambiguous is qualified.
- **Mirror support** was added to validation (the offset's dimension, the
  source's kind), JSON, the CLI's `info`, the commands and the regenerator.

`PatternSupport` itself is unchanged; the mirror only calls it.

## Known Limitations

- **The plane is explicit model coordinates** (origin, normal, offset). It
  does not reference a datum plane, a planar face or a sketch plane, so it
  does not follow the geometry when the model moves. Only its offset can be
  driven by a parameter. Named global planes (XY, YZ, ZX) are not offered;
  they are the explicit planes through the origin with normals Z, X and Y.
- **Feature-scope sources** are the pattern sources: extrudes and revolves
  (new body, join, cut), holes, chamfers and fillets. Intersect sources,
  sketches, and feature mirrors of patterns or mirrors are refused, as are
  patterns of mirrors. A body mirror of a pattern or mirror works. Nested
  patterns stay out of scope, as in P11-FEAT-005/006.
- **References:** chamfer, fillet and hole images use the features'
  geometric references, reflected exactly. There is no semantic topology
  naming. An image whose reflected reference is missing or ambiguous fails
  the mirror, and an image the plane maps onto the feature's own hole or
  edge is refused.
- **Overlapping hole images** are refused by the Hole's containment policy.
  Coincident extrude and revolve images are allowed and change nothing.
- **Result ordering:** the image is instance 1 of the mirror, not a document
  object. The solid order inside a kept-original body is the kernel's
  (deterministic, not exposed).
- **Wording:** the defensive "no valid solid" message still says "the
  pattern" because the build loop is shared.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: the related tests, 5 times each.
- `qualification-times.txt`: the start, exit code and time of every step.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including mirror images, and the direct image-vs-source volume
  comparison. The relative errors are:
  - mirrored cube: 3.4e-16;
  - hole pair: 3.0e-16;
  - turned cylinder: 2.6e-16;
  - sphere: 4.5e-16;
  - countersunk block: 8.9e-16;
  - image vs source: at most 5.9e-16.
- `mirror-values-release.txt`: measured feature-level volumes, areas,
  bounds, positions, errors and messages, as printed by the tests (Release),
  with `values.py`, the filter that produced it.
- `regression/face-normal-fix-regression.log`: the failing run without the
  face-normal fix.

## Final Result

**PASS.** P11-FEAT-007 Mirror is implemented and verified in all three
presets with zero warnings:

- a persistent, parametric mirror of a feature's operation or of a body,
  across an explicit plane (origin, normal, offset literal or driven);
- keep-original and image-only results;
- mirrors of new-body, join and cut extrudes and revolves, holes of every
  type and extent, chamfers and fillets;
- a Householder reflection checked against the point formula, the
  involution M(M(p)) = p and the signed-distance invariant;
- orientation-safe mirror images (a face-normal bug found and fixed, with a
  regression proof);
- atomic failure naming the image and its plane;
- analytic validation, regeneration, undo/redo, save/load, STEP and STL.

The mirror shares the pattern subsystem and the transform infrastructure
with the linear and circular patterns, whose tests all still pass. Plane
references (datum plane, planar face, sketch plane) are not implemented.
