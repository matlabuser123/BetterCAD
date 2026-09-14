# P11-FEAT-004 — Hole Feature Verification

## Status

PASS

Date: 2026-09-15. `main` was at `55a3f86` (P11-FEAT-003 Fillet) before this
milestone.

## Scope

Every item was checked against the working tree and by tests.

| Item | Status | Main evidence (test cases) |
| --- | --- | --- |
| Hole feature definition (`HoleDefinition`) | IMPLEMENTED | `HoleFeature_DefinitionIsValidatedOnCreateAndEdit`; `HoleFeature_DependsOnItsTargetAndParameters` |
| Placement reference (face + centre in face coordinates, each coordinate literal or parameter-driven) | IMPLEMENTED | `FaceSignature_IsCanonicalAndHasModelAlignedCoordinates`; `Hole_PositionIsWhereTheFaceCoordinatesSay`; `HoleFeature_RegeneratesAfterPositionChange` |
| Planar face reference (unique → resolve, missing → fail, ambiguous → fail, never substitute) | IMPLEMENTED | `Faces_BoxHasSixPlanarFacesWithReferences`; `Hole_UnresolvablePlacementIsReportedNotGuessed`; `HoleFeature_RejectsMissingFace`; `HoleFeature_RejectsAmbiguousFaceReference`; `HoleFeature_DoesNotSubstituteFaceAfterTopologyChange` |
| Through hole | IMPLEMENTED | `Hole_ThroughHoleMatchesAnalyticVolume`; `HoleFeature_ThroughHoleMatchesAnalyticVolume`; `HoleFeature_ThroughHoleRemainsThroughAfterThicknessChange` |
| Blind hole | IMPLEMENTED | `Hole_BlindHoleMatchesAnalyticVolume`; `HoleFeature_BlindHoleMatchesAnalyticVolume`; `HoleFeature_BlindHoleKeepsItsDepthWhenTheBodyChanges` |
| Counterbore (optional) | IMPLEMENTED | `Hole_CounterboreMatchesAnalyticVolume`; `HoleFeature_CounterboreMatchesAnalyticVolume` |
| Countersink (optional) | IMPLEMENTED | `Hole_CountersinkMatchesAnalyticVolume`; `HoleFeature_CountersinkMatchesAnalyticVolume` |
| Diameter/depth validation | IMPLEMENTED | `HoleFeature_RejectsZeroDiameter`; `HoleFeature_RejectsNegativeDiameter`; `HoleFeature_RejectsNonFiniteDiameter`; `HoleFeature_RejectsInvalidBlindDepth`; `Hole_RejectsInvalidRequestsBeforeTheKernel`; `Hole_BlindHoleMayNotReachTheFarSide` |
| Direction (always into the material) | IMPLEMENTED | `Hole_DrillsIntoTheMaterialFromEitherSide`; `HoleFeature_BlindHoleMatchesAnalyticVolume` |
| Geometry validation | IMPLEMENTED | result checks in `cutHole()`; `Hole_BlindHoleThatBreaksIntoACavityIsRefused`; `Hole_ThatRemovesNothingMeasurableIsRefused` |
| Regeneration (thickness, depth, diameter, position) | IMPLEMENTED | `HoleFeature_ThroughHoleRemainsThroughAfterThicknessChange`; `HoleFeature_BlindHoleKeepsItsDepthWhenTheBodyChanges`; `HoleFeature_RegeneratesAfterDiameterChange`; `HoleFeature_RegeneratesAfterPositionChange` |
| Topology-change diagnostics | IMPLEMENTED | `HoleFeature_DoesNotSubstituteFaceAfterTopologyChange`; `HoleFeature_WorksOnRevolvedBody` |
| Extruded body and extruded chain | IMPLEMENTED | every block test; `HoleFeature_WorksOnAnExtrudedChain` |
| Revolved body | IMPLEMENTED | `Hole_OnRevolvedEndFace`; `HoleFeature_WorksOnRevolvedBody` |
| Hole after Chamfer/Fillet, Chamfer/Fillet of a hole's rim | IMPLEMENTED | `HoleFeature_ComposesWithChamferAndFillet` |
| Transactions | IMPLEMENTED | `HoleFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` |
| Save/load | IMPLEMENTED | `HoleFeature_SaveLoadPreservesEngineeringIntent`; `HoleFeature_SaveLoadKeepsAThroughHoleThrough`; `HoleFeature_FailedHoleSavesAndLoadsUnchanged`; `HoleFeature_DataIsStoredAsTransparentJson`; `HoleFeature_MalformedDataIsRejectedWithTheJsonPath` |
| Undo/redo (create, diameter, depth, placement) | IMPLEMENTED | `HoleFeature_UndoRedoRestoresGeometry`; `HoleFeature_RegeneratesAfterDiameterChange`; `HoleFeature_RegeneratesAfterPositionChange` |
| STEP/STL | IMPLEMENTED | `HoleFeature_ExportsStep`; `HoleFeature_ExportsClosedStl` |
| Determinism | IMPLEMENTED | `Hole_IsDeterministic`; `HoleFeature_RegenerationIsDeterministic` |
| Diagnostics (feature, validation, CLI) | IMPLEMENTED | `HoleFeature_InvalidInputsFailWithStructuredDiagnostics`; `HoleFeature_ModelsAreValidatedLikeAnyOther`; `info and validate describe holes` |
| Threads, drill points, spotface, standards databases, tolerance classes, cosmetic threads | **NOT IMPLEMENTED** (out of scope) | — |

Not claimed: **semantic topology naming.** The face reference is geometric,
like the edge references of Chamfer and Fillet (see Face Reference).

## Architecture

The conceptual `GeometryService` is the function API of
`bettercad_geometry`, as for Chamfer and Fillet:

```text
HoleFeature (bettercad_features)                    include/bettercad/features/HoleFeature.hpp
  └─ regenerateHole()                               src/features/hole/HoleRegeneration.cpp
       ├─ resolveHoleRequest(): parameters → values
       └─ detail::applyToTargetBody()               src/features/SolidSupport.cpp
            └─ geometry::cutHole(body, HoleRequest) include/bettercad/core/geometry/Hole.hpp
                 ├─ validate(HoleRequest)           src/core/geometry/Holes.cpp
                 └─ OCCT adapter                    src/core/geometry/occt/OcctHole.cpp
                      ├─ face resolution            occt/OcctFaces.cpp, BRepClass_FaceClassifier
                      ├─ fit and depth preflight    BRepExtrema_DistShapeShape, BRepIntCurveSurface_Inter
                      ├─ cutter                     makeRevolution() (Sweeps.hpp, existing)
                      ├─ Boolean cut                booleanDifference() (Booleans.hpp, existing)
                      └─ result validation          validity, solid count, volume
```

No OCCT type leaves `occt/`; the layering test enforces this.

**Reused, not duplicated:**

- the Boolean infrastructure (`booleanDifference()` with its validation) and
  the revolution sweep that builds the cutter;
- `SolidSupport::applyToTargetBody()` for target handling and the message
  prefix;
- `CreateFeatureCommand` / `ModifyFeatureCommand`, the regenerator's
  handler registration, `ResultBodies`, validation and the CLI;
- the edge-matching tolerances (`FaceMatching.hpp` uses those of
  `EdgeMatching.hpp`);
- the `BlockModel` and `TurnedPartModel` test fixtures.

The OCCT distance helpers moved from `OcctBlend.cpp` into
`OcctTopology.hpp` so the hole can use them too. The point overload now
takes any shape instead of only an edge; nothing else changed, and all
Chamfer and Fillet tests pass with their exact-message assertions.

**New:**

- geometry: `Faces.hpp` (face query and references), `Hole.hpp`,
  `Faces.cpp`, `FaceMatching.hpp`, `Holes.cpp`, `occt/OcctFaces.cpp`,
  `occt/OcctHole.cpp`;
- features: `HoleFeature.hpp`, `HoleFeature.cpp`, `HoleRegeneration.cpp`,
  the regenerator handler, `CreateHoleCommand` / `ModifyHoleCommand`, and
  validation of the four driving parameters (all lengths);
- io: the hole JSON mapping;
- CLI: `info` describes holes;
- hole cases in `examples/geometry_accuracy`;
- `docs/architecture.md`.

**Transactions.**

- Definitions are validated before a command changes the document.
- Bodies are immutable; the hole is a new body, stored by the regenerator
  only if it passes validation.
- A failure leaves the document and the upstream bodies intact and gives
  the hole no stale body (the P8 policy). Its dependents are blocked. Undo,
  or fixing the input, restores it.
- Kernel exceptions are caught by the shared `guardKernelCall()` and become
  `Internal` errors. None of the probed configurations crashed the kernel
  (see Kernel Probe).

## Face Reference

A `FaceSignature` is a planar face's supporting plane and its outward side:

- `point`: the plane's point nearest the origin (canonical, so any point of
  the plane gives the same reference);
- `normal`: the outward unit normal. Its orientation matters: it tells the
  two faces of a plate apart. `planeSignature()` stores no negative zeros.

It is never a kernel index or a `TopoDS_Face`. `findFaces()` matches faces
whose normals agree within 1e-9 rad and whose plane passes within 1e-7 mm
of the point. Curved faces cannot be referenced (`InvalidArgument`).

**The centre** is in the face's (u, v) coordinates. The origin is the
plane's point nearest the model origin; u and v are two model axes
projected into the plane (X, Y for a plane facing ±Z; X, Z for ±Y;
otherwise Y, Z). On a box's faces, (u, v) are the model coordinates, and
both sides of a plate share them. Each coordinate can be driven by a length
parameter.

**Resolution** (in `cutHole()`, before the kernel runs):

1. Faces on the referenced plane, facing its way. None: `NotFound`.
2. Of those, the faces whose classifier puts the centre IN or ON them
   (`BRepClass_FaceClassifier`, 1e-7 mm).
   - None: `FailedPrecondition` "the centre … is not on a face of the body".
   - More than one (the centre on the edge between coplanar faces):
     `FailedPrecondition` "the placement is ambiguous".
   - Exactly one: that face.

No other face is ever substituted.

| Upstream change | Behaviour | Tested by |
| --- | --- | --- |
| Height 20 → 40 mm, hole on the bottom face (the extrude's start plane) | resolves; the hole is still through | `HoleFeature_ThroughHoleRemainsThroughAfterThicknessChange` |
| Height 20 → 30 mm, hole on the top face (the face moves) | `NotFound`; the pad's new top face at z = 30 is **not** used | `HoleFeature_DoesNotSubstituteFaceAfterTopologyChange` |
| Height back to 20 mm | resolves; bit-identical volume | same |
| Width 100 → 40 mm (centre (50, 25) off the face) | `FailedPrecondition` "not on a face … (1 face(s) lie on that plane elsewhere)" | same |
| Width 100 → 52 mm (centre 2 mm from the edge) | `FailedPrecondition` "does not fit … only 2 mm from the face's edge" | same |
| Length 50 → 80 mm, hole on the front face | resolves; through along +Y (V = 153716.81469282042 mm³) | `HoleFeature_RegeneratesAfterPositionChange` |
| A hole centred inside an earlier hole | `FailedPrecondition` "not on a face" | `HoleFeature_DoesNotSubstituteFaceAfterTopologyChange` |
| Centre on the edge between two coplanar faces | `FailedPrecondition` "ambiguous: … lies on 2 faces"; away from the edge it resolves | `HoleFeature_RejectsAmbiguousFaceReference` |
| A plane with no face (z = 25), or the top plane facing into the material | `NotFound` | `HoleFeature_RejectsMissingFace` |
| Revolve sweep 360° → 270° | resolves | `HoleFeature_WorksOnRevolvedBody` |
| Bore radius 5 → 9 mm (reaches the hole) | `FailedPrecondition` "only 1 mm from the face's edge" | same |
| Bore radius 5 → 11 mm (swallows the centre) | `FailedPrecondition` "not on a face" | same |

**Known topology limitation.** BetterCAD does not yet provide semantic
topology naming. A reference to a face whose plane moves (the top face of
an extrude whose depth changes) fails with `NotFound`, and the user must
re-pick the face. A hole placed on the extrude's start plane follows any
thickness. The semantic topology naming milestone stays open.

## Through Hole Analytical Validation

Kernel values from `geometry-accuracy-release.txt` (`cutHole()` on a box);
feature values from `hole-feature-values-release.txt` (the full chain:
parameters → sketch → extrude → hole).

| | Kernel (`cutHole`) | Feature (`HoleFeature`) |
| --- | --- | --- |
| Block | 100 × 50 × 20 mm, V0 = 100000 mm³ | same |
| Diameter | d = 10 mm | same (driven by `diameter`) |
| Thickness | H = 20 mm | same (driven by `height`) |
| Formula | V = LWH − π(d/2)²H | same |
| Removed volume | π·25·20 = 1570.7963267948966 mm³ | same |
| Expected | 98429.203673205106 mm³ | 98429.20367320510558784 mm³ |
| Actual | 98429.203673205106 mm³ | 98429.20367320513469167 mm³ |
| Absolute error | 0 | 2.91e-11 mm³ |
| Relative error | 0 (kernel estimate 1.7e-16) | 2.96e-16 |
| Tolerance | 1e-12 relative (`kRelTight`) | same |
| Result | **PASS** | **PASS** |

The feature value differs from the kernel value by a few ulps because the
sketch solver writes the rectangle's corners back with rounding error.

**Also checked:**

- Surface area: 16000 − 2·25π + 2π·5·20 = 16471.238898 mm² (kernel,
  relative error 2.21e-16; feature 16471.23889803847).
- 7 faces and 1 solid.
- Through: the bore meets the top face in a circle at z = 20 and the bottom
  face in one at z = 0; both faces have area 5000 − 25π
  (4921.46018366025691648 mm²).
- The target's body (the pad) is unchanged (100000 mm³), and the hole is
  the one result body.

## Blind Hole Analytical Validation

| | Kernel | Feature |
| --- | --- | --- |
| Block | 100 × 50 × 20 mm, V0 = 100000 mm³ | same |
| Diameter, depth | d = 10 mm, h = 8 mm | same |
| Formula | V = V0 − π(d/2)²h | same |
| Removed volume | 200π = 628.3185307179587 mm³ | same |
| Expected | 99371.681469282048 mm³ | 99371.6814692820480559 mm³ |
| Actual | 99371.681469282048 mm³ | 99371.68146928209171165 mm³ |
| Error | 0 | 4.37e-11 mm³ (4.4e-16 relative) |
| Tolerance | 1e-12 relative | same |
| Result | **PASS** | **PASS** |

- A flat floor 8 mm down: one face on the plane z = 12 facing up, area 25π.
- Area 16000 + 2π·5·8 = 16251.32741228718 mm² (exact to the last digit).
- Not through: no circle on the bottom face, which keeps its full 5000 mm².

**Direction.** A hole always goes into the material, along the reversed
outward normal of its face; there is no free direction vector, so no zero,
NaN or outward direction can reach the kernel. From the top face the same
hole goes down (its floor faces up at z = 12); from the bottom face it goes
up (its end faces down at z = 8). Both remove the same analytic volume. A
reference to the top plane facing down matches no face (`NotFound`).

## Counterbore Validation

V = V0 − πr²H − π(R² − r²)h for a through hole; for a blind hole H is the
depth.

| Case | Expected (mm³) | Actual (mm³) | Relative error |
| --- | --- | --- | --- |
| Kernel: d = 10, R = 9 (18 mm), h = 5, through | 97549.557730199958 | 97549.557730199973 | 1.49e-16 |
| Feature: the same, on the parametric block | 97549.55773019995831419 | 97549.55773020000196993 | 4.5e-16 |
| Feature: driven diameter 10 → 12 mm under the same counterbore | 97031.19494235764432233 | 97031.19494235768797807 | 4.5e-16 |
| Feature: blind counterbore, d = 6, 12 deep, R = 5 × 4 (removes 172π) | 97888.84973678765527438 | 97888.84973678769893013 | 4.5e-16 |
| Feature: the volume that counterbore removes | 540.35393641744440174 | 540.35393641743576154 | 1.6e-14 (of 540 mm³) |

The shelf is a face on z = 15 (through case) or z = 16 (blind case) facing
up, with area π(R² − r²): 56π and 16π. The kernel run also matches its area
to 0 relative error.

## Countersink Validation

The cone narrows from R at the face to r at depth h = (R − r)/tan(α/2).
Beyond the bore it removes the frustum less the cylinder,
πh(R² + Rr + r²)/3 − πr²h.

**Independent derivation.** The frustum formula is checked against a second
derivation in the test: the full cone to the apex less the cone below
radius r, (π/3)(R²H − r²(H − h)) with H = R/tan(α/2). For R = 6, r = 3,
α = 90° both give 63π (agreement within 1e-14).

| Case | Expected (mm³) | Actual (mm³) | Relative error |
| --- | --- | --- | --- |
| Kernel: d = 10, D = 20, 90°, through | 97905.604897606812 | 97905.604897606798 | 1.49e-16 |
| Kernel: d = 10, D = 20, 82°, through | 97826.872183697225 | 97826.872183697211 | 1.49e-16 |
| Feature: d = 10, D = 20, 82°, blind 12 | 98455.19071441517735366 | 98455.19071441519190557 | 1.5e-16 |
| Feature: the chain's countersink, d = 6, D = 12, 90° (removes 216π) | 678.58401317539528463 | 678.58401317542302422 | 4.1e-14 (of 679 mm³) |
| Feature: the whole chain (Drill, Pocket, Sink) | 97210.26572361226135399 | 97210.26572361227590591 | 1.5e-16 |

- The chain's area, derived face by face in `HoleVariants::expectedArea()`
  (the cone's side is π(R + r)·slant), is 17046.72767213926636032 mm²;
  actual 17046.72767213927363628 (4.3e-16).
- The cone meets the top face in a circle of radius R and the bore in one
  of radius r, h below.
- A blind countersink must be deeper than its cone: at 90° with D = 20 and
  d = 10 the cone is 5 mm deep, so a 5 mm blind hole is refused.

## Regeneration

**Through hole, thickness 20 → 40 mm** (the critical case,
`HoleFeature_ThroughHoleRemainsThroughAfterThicknessChange`). The hole is
on the bottom face, the pad's start plane.

| Thickness | Expected (mm³) | Actual (mm³) | Through? |
| --- | --- | --- | --- |
| 20 mm | 98429.20367320510558784 | 98429.20367320513469167 | yes: circles at z = 0 and z = 20 |
| 40 mm | 196858.40734641021117568 | 196858.40734641026938334 | yes: exit circle at z = 40; top face area 5000 − 25π = 4921.46018366025691648 mm²; no face left at z = 20 |
| 5 mm | 24607.30091830127639696 | 24607.30091830128367292 | yes: exit circle at z = 5 |
| back to 20 mm (undo, undo) | — | bit-identical to the first 20 mm result | yes |

- Changing the height regenerates Pad and Drill only.
- The through hole has no stored depth. Its cutter runs 1 mm past the body's
  bounding box each time it is built, so it follows any thickness.
- The kernel run gives 196858.40734641021 mm³ for the 40 mm block (0
  error).

**Blind hole** (bottom face, depth driven by `hole_depth`):

- thickness 20 → 30 mm: still 10 mm deep, V = 149214.60183660255 mm³
  (exact); its end stays at z = 10, and the top face is untouched
  (5000 mm²);
- depth 10 → 15 mm: only the hole regenerates, V = 98821.90275490387 mm³
  (expected 98821.90275490383).

**Diameter 5, 10, 15 mm** (`HoleFeature_RegeneratesAfterDiameterChange`):

- each change regenerates only Drill, and Pad stays up to date;
- V = 99607.30091830129, 98429.20367320513 and 96465.70826471152 mm³
  (expected 99607.30091830128, 98429.20367320511, 96465.70826471149);
- circles of radius d/2 appear on the top and bottom faces;
- returning to 10 mm gives bit-identical geometry, and undo and redo through
  all three steps are bit-identical.

**Placement hole_x 25 → 35 mm** (`HoleFeature_RegeneratesAfterPositionChange`):

- only Drill regenerates;
- the volume stays V0 − 500π (98429.20367320513 and 98429.20367320512);
- the circles move to x = 25, then x = 35, and none is left at x = 50;
- the centre of mass is where the moved hole puts it,
  x_c = (V0·50 − V_hole·x)/(V0 − V_hole), within 1e-12;
- hole_y 25 → 10 moves it along y.

## Topology Change

See the table under Face Reference. In every case:

- the hole fails with the reference and the reason in the message;
- it keeps no body;
- the upstream body is rebuilt and valid;
- the document is unchanged by regeneration;
- the model recovers when the input is restored.

No case substitutes another face.

## Hole Centre vs Face Boundary

The exact guarantee, from `cutHole()`:

1. The centre must lie on the resolved face: the classifier puts it IN or
   ON the face, within 1e-7 mm.
2. The hole's outline at the face (the counterbore or countersink, if any)
   must stay at least 0.001 mm inside the face. The distance from the centre
   to every edge of the face, measured by the kernel
   (`BRepExtrema_DistShapeShape`), must be at least the entry radius plus
   0.001 mm. That covers the outer boundary and any holes in the face (an
   earlier hole, the turned part's bore), so a centre ON the boundary is
   refused as well.

Degenerated edges are skipped. Planar faces from full revolutions have no
seam edge: `Hole_OnRevolvedEndFace` drills on the axis of a revolved end
face, which a seam through the axis would have blocked.

Tested for a 10 mm hole: 5.5 mm from an edge fits, leaving a 0.5 mm wall
and the analytic volume; 5.0005 mm, 4 mm and 3 mm are refused. The kernel
probe adds 5 mm (tangent) and 5.0000001 mm, both refused.

**Not guaranteed:** clearance beyond the entry face. The containment check
covers a blind hole breaking into a cavity (below). A through hole may pass
through cavities or faces along its axis ("through all").

## Side-Wall Breakout Policy

**Policy B: refuse.** The feature contract is a contained drilled hole. A
hole whose entry outline would cross or touch the side of its face is
refused with `FailedPrecondition` "the hole does not fit on its face: its
entry is D mm across, but the centre … is only X mm from the face's edge".
The kernel does not define this. The probe below shows that OCCT alone
builds a valid notched solid for the same input.

Tested by `Hole_RefusesHolesThatDoNotFitTheFace`,
`HoleFeature_RegeneratesAfterPositionChange` (hole_x 97 mm), the width and
bore cases above, and a hole next to a chamfer
(`HoleFeature_ComposesWithChamferAndFillet`).

**Below the entry face**, a blind hole must remove exactly its own analytic
volume, within 1e-9 of the body's volume. If it removes less, it broke into
a cavity, and it is refused (`Hole_BlindHoleThatBreaksIntoACavityIsRefused`:
a channel beside the axis).

## Kernel Probe

`kernel-probe/hole_kernel_probe.cpp` and `kernel-probe/hole-kernel-probe.log`
cover ten near-degenerate configurations, one process per case. Each case
compares OCCT's Boolean cut of a plain cylinder with no preflight against
`cutHole()` for the same hole:

| Case | OCCT alone | `cutHole()` |
| --- | --- | --- |
| through, both ends overextended | valid, 98429.2036732051 | valid, 98429.203673205106 |
| entry coplanar with the top face | valid | valid (the cutter always starts 0.01 mm outside) |
| blind bottom coplanar with the far face | valid **through** hole (98429.2036732051) | refused: "would reach the far side … make it a through hole" |
| blind bottom 1e-7 mm past the far face | valid through hole (98429.2036679691) | refused, same |
| tangent to the front face; 1e-7 mm inside it | valid | refused: does not fit |
| breaking out of the front face | valid notched solid (98652.851) | refused: does not fit |
| centred on a vertical edge | valid | refused: 0 mm from the edge |
| missing the box | the box, unchanged (100000) | refused: "not on a face of the body" |
| 1 nm radius | removes 6e-11 mm³ | refused: "removes no material" |

- **No crash** in any case (every exit code 0x00000000), so unlike Chamfer
  and Fillet no crash class protects the preflight.
- The preflight exists for the semantic guarantees. Without it the kernel
  silently makes a blind hole through, notches a side wall, or returns an
  unchanged body.

## Chamfer and Fillet Interplay

| Chain | Expected (mm³) | Actual (mm³) |
| --- | --- | --- |
| Extrude → Chamfer (top front edge, 2 mm) → Hole | 98229.20367320510558784 | 98229.20367320510558784 |
| Extrude → Fillet (top front edge, 5 mm) → Hole | 97892.69908169872360304 | 97892.69908169873815496 |
| Extrude → Hole → Chamfer of both rims, 1 mm: 2·πc²(r + c/3) removed (Pappus) | 98395.69335156682063825 | 98395.69335156684974208 |
| Extrude → Hole → Fillet of the top rim, 2 mm: 2π(r + ū)·r_f²(1 − π/4) removed, ū = r_f(10 − 3π)/(12 − 3π) | 98399.82652662889449857 | 98399.8265266289236024 |

- The hole's rim is found by a circle edge reference.
- After the chamfer the top face meets it at radius 6; after the fillet, at
  radius 7.
- A hole 4 mm from the chamfered edge is refused, since it would break into
  the chamfer face.
- A larger hole moves its rim off the chamfer's circle reference. The
  chamfer then fails with `NotFound`, the hole does not, and nothing is
  substituted.

## Extruded and Revolved Bodies

- **Extruded chain** (`HoleFeature_WorksOnAnExtrudedChain`): the P9 bracket
  is a plate with a sketched hole and a pocket cut from its top face.
  - A through hole in the pocket's floor gives V = 114429.64639517046
    (expected 114429.64639517044).
  - A blind hole in the plate's top face gives 113801.3278644525 (expected
    113801.32786445248).
  - Changing the sketched hole's radius regenerates the chain to
    111539.38115386783 (exact).
- **Revolved body** (`HoleFeature_WorksOnRevolvedBody`): the turned part is a
  revolve with a bore cut and a groove cut. A 4 mm through hole at (−10, 0)
  in its end face gives V = 23750.44046113882 (expected 23750.44046113884).
  - At a 270° sweep it gives 17687.16663971054 (exact).
  - A wider bore that reaches or swallows the hole is refused, as tabled
    above.
  - Kernel run: an axial blind bore in a revolved cylinder's end face has
    relative error 2.72e-16.

## Persistence

`HoleFeature_SaveLoadPreservesEngineeringIntent` works on `HoleVariants`:

- Drill: through, with its diameter and centre driven;
- Pocket: a blind counterbore whose depth is driven by a parameter;
- Sink: a through countersink.

The sequence is save → an empty document → load → regenerate, and it checks:

- `equivalent()` documents and identical item IDs;
- each `HoleDefinition` equal field for field: target `FeatureId`, face
  signature, centre, centre parameters, type, extent, diameter and depth
  with their parameters, and head dimensions;
- the same dependencies for every node, and the same regeneration order;
- bit-identical volume, area, centroid, bounding box and topology for Pad,
  Drill, Pocket and Sink;
- that the loaded model is still parametric. Diameter 10 → 12 mm, depth
  12 → 10 mm and hole_x 50 → 60 mm each regenerate to the analytic volume.

Also checked:

- A bottom-face through hole round-trips its reversed normal. After loading,
  a thicker block (40 mm) is still drilled through.
- A failed hole saves and loads unchanged, with the same error, and
  recovers.
- The JSON is checked text for text. A through hole stores **no depth**
  (Drill, compacted here from the pretty-printed file):
  `{"target": 6, "face": {"surface": "plane", "point": [0.0, 0.0, 0.02],
  "normal": [0.0, 0.0, 1.0]}, "center": [0.05, 0.025],
  "center_u_parameter": 7, "center_v_parameter": 8, "type": "simple",
  "extent": "through", "diameter": 0.01, "diameter_parameter": 4}`.
- Counterbore and countersink keys appear only for their types. The file
  has no negative zeros.
- Malformed data is rejected with its path, for example:
  - `objects[2].data.face.surface: unknown value 'cylinder'`;
  - `objects[2].data.face.normal: expected a unit vector`;
  - `objects[2].data.thread: unknown field` (threads are not supported, and
    unknown fields are refused, not ignored);
  - `objects[2].data: a through hole takes no depth; it goes through all
    material`.
- No kernel object is serialized; the face is stored as its plane.

## Undo / Redo

`HoleFeature_UndoRedoRestoresGeometry`:

1. **Create** "Pocket", blind 6 mm × 8 mm (`CreateHoleCommand`, "Create
   hole 'Pocket'").
2. **Diameter** → 8 mm; only Pocket regenerates.
3. **Depth** → 12 mm; the floor moves to z = 8.
4. **Placement** → (80, 25); the rim moves from x = 20 to x = 80.
5. Undo and redo through these states. Each leaves a document equivalent to
   the recorded one (definitions included) and a bit-identical volume. The
   first undo also puts the rim back at x = 20.
6. Undo everything: Pocket is gone, and Drill is the result again.
7. Redo: Pocket is recreated with the same ID and a bit-identical volume.

Commands check the feature kind. An invalid edit changes nothing and
records nothing (`HoleFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact`).
Parameter edits (diameter, position, thickness) are undone and redone
bit-identically in the Regeneration tests.

## STEP

`exportStep` of `HoleVariants` (the block with a through hole, a
counterbore and a countersink) writes one product, `PRODUCT('Sink','Sink'`.
Read back with the kernel it is 1 solid and valid. V and A are within 1e-9
of the closed forms (`kRelStep`: STEP stores decimal text).

## STL

Binary and ASCII, each with a linear deflection of 0.01 mm, parsed
independently:

- closed: every edge shared by exactly two oppositely oriented triangles;
- the enclosed volume is above the exact volume. The hole walls and cone
  are concave, so their chords lie across the hole, outside the material;
- the enclosed volume is within 0.01 mm × the surface area of the exact
  volume.

## Diagnostics

The project's convention is `Result`/`Error`: a stable `ErrorCode` and a
message naming the feature, the operation, the reference and the cause.
The suggested categories map to it as follows:

| Suggested | Code | Message (example) |
| --- | --- | --- |
| HoleMissingTarget | NotFound / FailedPrecondition | "object:9 references object:6, which does not exist"; "Drill: a hole needs the body of its target feature" |
| HoleUnsupportedPlacementSurface | InvalidArgument | "placement face: only planar faces can be referenced, not a cylinder" |
| HoleFaceNotFound | NotFound | "Drill: hole: the placement face (plane through (0, 0, 20) mm facing (0, 0, 1)) matches no face of the body" |
| HoleAmbiguousFaceReference | FailedPrecondition | "Probe: hole: the placement is ambiguous: the centre (50, 10) mm lies on 2 faces on the plane through (0, 0, 0) mm facing (0, -1, 0)" |
| HoleInvalidDiameter | InvalidArgument | "Drill: hole: the hole diameter must be positive and finite, got -2 mm" |
| HoleInvalidDepth | InvalidArgument / FailedPrecondition | "the hole depth must be positive and finite, got 0 mm"; "a through hole takes no depth; it goes through all material"; "Drill: hole: a blind hole 20 mm deep would reach the far side: there are only 20 mm of material along its axis; make it a through hole" |
| HoleInvalidDirection | — | not applicable: the direction is always into the material, never an input |
| HolePlacementOutsideBody | FailedPrecondition | "Drill: hole: the centre (150, 10) mm is not on a face of the body on the plane through (0, 0, 20) mm facing (0, 0, 1) (1 face(s) lie on that plane elsewhere)" |
| HoleNoIntersection | FailedPrecondition | "hole: the hole removes no material" |
| HoleTooLarge | FailedPrecondition | "Drill: hole: the hole does not fit on its face: its entry is 60 mm across, but the centre (50, 25) mm is only 25 mm from the face's edge (a hole must stay at least 0.001 mm inside its face)" |
| HoleBooleanFailure | as returned by `booleanDifference()`, prefixed "hole: " | — |
| HoleInvalidResult | Internal / FailedPrecondition | "hole: the kernel produced an invalid solid"; "hole: the hole splits the body into N solids"; "hole: the blind hole breaks out of the material: it removes … instead of its own …" |
| (a driving parameter of another dimension) | DimensionMismatch | validation: "Drill (object:9): the diameter is driven by tilt (object:10), which is an angle, not a length" |
| (a head at least as wide as a driven hole) | InvalidArgument | "Drill: hole: the counterbore diameter must be larger than the hole diameter (16 mm), got 16 mm" |
| (failed target) | — | the hole is blocked |

**Not exercised by a test.** Three defensive paths have no deterministic
trigger; the result validation guards them as for Chamfer and Fillet:

- a Boolean failure;
- an invalid kernel result;
- a hole that splits the body.

`bettercad-cli info` describes holes, e.g. "target Drill, counterbore blind
hole 12 mm deep, diameter 6 mm, counterbore 10 mm x 4 mm deep, centre
(20 mm, 25 mm) on plane through (0, 0, 20) mm facing (0, 0, 1)". `validate`
reports a moved face as an error and exits 1.

## Qualification

Each preset was rebuilt from clean (`cmake --build --preset <p>
--clean-first`), with warnings as errors. CTest ran only after a
successful build. The qualification ran from 04:10:10 to 04:27:21; no
source, test or CMake file changed after it started (checked by
modification time).

| Preset | Configure | Clean build | Compiler `warning:`/`error:` lines | Tests |
| --- | --- | --- | --- | --- |
| Debug | exit 0 | exit 0, 229 files compiled | 0 | **444/444 passed** |
| Release | exit 0 | exit 0, 229 files compiled | 0 | **444/444 passed** |
| Debug-shared | exit 0 | exit 0, 229 files compiled | 0 | **444/444 passed** |

**Compiler warnings: 0** in every preset. P11-FEAT-003 compiled 220 files;
the 9 new ones are:

- `Faces.cpp`, `Holes.cpp`, `OcctFaces.cpp` and `OcctHole.cpp` (geometry);
- `HoleFeature.cpp` and `HoleRegeneration.cpp` (features);
- `HoleTests.cpp`, `HoleFeatureTests.cpp` and `HoleFileTests.cpp`.

The 49 new tests (all passed in every preset):

- `tests/core/geometry/HoleTests.cpp`: 16, of which 2 test the face query;
- `tests/features/HoleFeatureTests.cpp`: 24;
- `tests/io/HoleFileTests.cpp`: 7;
- `tests/features/ValidationTests.cpp`: 1;
- `tests/cli/CliTests.cpp`: 1.

Every hole, face, fillet and chamfer test also ran 5 times in Release and
in Debug, one process per run: 595/595 passes each (`ctest-repeat-*.log`).
That is 119 tests: those 113 plus 6 older tests whose names mention holes
or faces.

## Legacy Regression

P0–P11-FEAT-003: **PASS**. All 395 tests of the P11-FEAT-003 qualification
pass in all three presets. This was checked by name against
`../P11-FEAT-003/ctest-release.log`: each of its 394 distinct names (two
older test cases share a name) appears as "Passed" in each new log.

No legacy test assertion was changed. New tests were only appended to
`ValidationTests.cpp` and `CliTests.cpp`. Chamfer and Fillet pass unchanged
after the distance helpers moved to `OcctTopology.hpp`.

## Known Limitations

- **Geometric face references, not semantic topology naming.** A face whose
  plane moves is not followed (see Face Reference).
- **Planar placement faces only.** Curved faces are refused explicitly.
- **Direction.** Always perpendicular to the face, into the material. There
  are no angled holes.
- **Ambiguity.** It is reported only when the centre lies on the edge
  between two coplanar faces. Coplanar faces elsewhere on the plane do not
  make a reference ambiguous; the face under the centre is chosen.
- **Clearance.** Checked only at the entry face, plus the blind-hole
  containment check. A through hole may cross cavities along its axis.
- **Out of scope:** threads, drill points, spotface, standards databases,
  tolerance classes, cosmetic threads.

## Evidence Files

- `README.md`: this file.
- `configure-{debug,release,debug-shared}.log`
- `build-{debug,release,debug-shared}.log`: clean rebuilds.
- `ctest-{debug,release,debug-shared}.log`
- `ctest-repeat-{release,debug}.log`: every hole, face, fillet and chamfer
  test run 5 times.
- `geometry-accuracy-release.txt`: kernel results against analytic
  solutions, including the hole cases.
- `hole-feature-values-release.txt`: measured feature-level volumes and
  areas, as printed by the tests.
- `kernel-probe/hole_kernel_probe.cpp` and
  `kernel-probe/hole-kernel-probe.log`: OCCT's Boolean cut against
  `cutHole()` in near-degenerate configurations.

## Final Result

**PASS.** P11-FEAT-004 Hole is implemented and verified in all three
presets with zero warnings:

- simple, counterbore and countersink holes, through or blind;
- placed on planar faces of extruded and revolved bodies by geometric face
  references, with the fail-never-substitute rule;
- a through hole stays through when its body gets thicker;
- analytic validation, regeneration, undo/redo, save/load, STEP and STL;
- structured diagnostics.

Threads and the other Hole Wizard extensions are not implemented.
