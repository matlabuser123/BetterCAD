# P17-LOAD-001 — Structural Loads

```text
STATUS:   PASS
MILESTONE: P17-LOAD-001, the sixth milestone of P17 — Structural FEA
SCOPE:    canonical load intent and its conversion into a current-mesh nodal
          force field. No global right-hand side, no restraints, no solver, no
          stress recovery, no persistence, no CLI, no GUI.
```

## Baseline

```text
HEAD at start      dd2aca6a74d3042e3ba4ab692f6d052cc5ec387c
origin/main        dd2aca6a74d3042e3ba4ab692f6d052cc5ec387c
working tree       clean, 0 porcelain lines
P17-ARCH-001  20/20   P17-DATA-001  19/19   P17-MAT-001  14/14
P17-DOF-001   12/12   P17-ELEM-001  18/18   P16  QUALIFIED
and the eight paths still fingerprinted P17-ELEM-001's qualified component
list, so every predecessor was demonstrably qualified on THIS tree
```

## What the audit found, and what it removed from the brief

Three of the brief's requirements describe states that **do not exist**, and
establishing that was most of the design work.

```text
MappingState::Ambiguous     ABSENT, deliberately. P16's header: a FaceName
                            "can be ambiguous, which P12-STREF-001 documents,
                            and that is exactly why this layer does not map
                            through one"
MappingState::Unsupported   ABSENT. "attribution needs no surface kind at all,
                            so a cone, a sphere, a torus and a B-spline map
                            exactly as a plane does"
a stale-mapping check       UNREACHABLE here. requireStructuralModel already
                            refuses a stale mesh and proves the map came from
                            the same lookup (ADR-036), so a second gate would
                            be a branch nothing can take
```

`MappingState` has exactly two values. So `LoadProblem` has **eight**, not the
eleven the brief sketches, and the three omissions are recorded with P16's own
words rather than shipped as placeholders — the discipline P17-ARCH-001
established when it deleted three unreachable values from its own first draft.

[MAPPING_CONTRACT.md](MAPPING_CONTRACT.md) carries the full authority matrix.

## The authority chain

```text
canonical load intent      LoadId + FaceName + a physical value
         |
P16's GeometryMeshMap      meshing::boundaryFacetsOf(map, FaceName)
         |
current boundary facets    ElementIds of THIS mesh, derived, discarded
         |
equivalent nodal forces    NodeId -> Force3D, derived, disposable
```

**A facet handle is never load authority**, and it is a compile-time assertion
rather than a review: mirror structs fix the permitted members of
`SurfaceTractionLoad` and `PressureLoad`, so an added field changes the
`sizeof`, and neither is constructible from an `ElementId`. `grep` confirms the
canonical header contains no `ElementId` outside comments, and exactly one
`NodeId` — `NodalForceLoad::node`, the explicitly mesh-local load.

**And P17 classifies no geometry.** Searched over the implementation: no
`tolerance`, no `nearest`, no `distance`, no `centroid`, no normal-similarity
test. Facet geometry is read only after P16 has answered, and then only to
integrate over it.

## What this milestone adds

```text
StructuralLoad.hpp         canonical intent: NodalForceLoad, SurfaceTractionLoad,
                           PressureLoad, GravityLoad, and the variant over them
StructuralLoadVector.hpp   the derived field: PreparedLoads,
                           prepareStructuralLoads, and the facet integration
core/math/Vector.hpp       Traction3D, Moment3D, momentOf
core/units/Units.hpp       the Torque alias
```

**Two headers because the constraint forced it.** The loads live in
`StructuralAnalysisDefinition`, so the document-object header includes the
canonical one — and if the derived machinery were there too, every document
object would pull in the `Mesher`, the `GeometryMeshMap` and the whole input
boundary. It is also the separation the physics has: intent survives a remesh
and a nodal force field does not.

**The loads went where P17-DATA-001 left room for them.** The definition
carried `// Loads -- P17-LOAD-001` as a placeholder, and its own note said why:
"an edit to any of them moves the owning object's revision ... Three
independent counters would give three chances to forget one." So load-change
invalidation needed **no new mechanism** — the one requirement in this
milestone that took no code at all.

That broke two assertions **P17-MAT-001 predicted it would break**, and whose
comment named its own replacement: "a compile-fail case naming the types that
may not appear would survive it." The replacement is a mirror struct fixing the
three members the definition may hold.

## Conventions — FROZEN

```text
traction   Pa, a GLOBAL vector. Rotating the body does NOT rotate the load
pressure   Pa, a SIGNED SCALAR acting along the current outward normal.
           Positive acts INWARD:  t = -p n_out
gravity    m/s^2, explicit direction. kStandardGravity is offered, never a
           default; a default-constructed GravityLoad has zero acceleration
nodal      N, MESH-LOCAL by declaration, carrying its MeshStamp
```

The pressure convention is demonstrated rather than asserted: the block's end
cap (normal `+Z`) and start cap (normal `-Z`) under **one** positive scalar give
opposite forces, which a global-direction pressure cannot produce. And the
transform test separates the two conventions on RM-MESH-06:

```text
PRESSURE   resultant rotates with the face:  F_placed = R F_base
TRACTION   resultant does NOT:               F_placed = F_base, component-wise
```

Requiring either behaviour of the other would be wrong, and the test says which
is which. [LOAD_SCHEMA.md](LOAD_SCHEMA.md).

## Integration

```text
surface, per facet   F = A t,  each corner A t / 3
                     A_vec = (1/2)(p2-p1) x (p3-p1) -- the HALF is not optional
body, per Tet4       F = rho V g, each node rho V g / 4
accumulation         ADDS, never overwrites. Superposition is the semantics
```

`V` is P16's **signed** volume with no absolute value anywhere: a
`StructuralModel` proves every element is positively oriented, so a magnitude
could only hide a violation of that.

**Facet orientation is traced, not trusted.** `Mesh.hpp` is explicit that the
data layer does not canonicalise winding, so the outward guarantee had to be
established from four separate facts — `kTetFaces`' windings "give outward
normals", `tetrahedralBoundary` keeps them, `generateVolumeMesh` stores them,
and P16 refuses a non-positive tetrahedron. Because it is derived rather than
promised, it is **checked**: a test walks every boundary facet of a meshed
block, finds the tetrahedron that owns it, and requires the area vector to
point away from the material. If that chain ever breaks, every pressure has the
wrong sign and the failure is visible.

## Gravity: IMPLEMENTED

Not deferred. P17-MAT-001 had already made the density available for this
consumer by name — `StructuralMaterial::density()` is present exactly when the
mode is `LinearStaticWithGravity`, because that milestone reads P15's
requirement table — so the only thing missing was the integral.

```text
the load carries an ACCELERATION and no density    ADR-028, asserted at
                                                   compile time
no density, gravity asked                          DensityMissing. There is no
                                                   7850 anywhere in the module
no gravity in the set                              the density is never consulted
```

[GRAVITY_DECISION.md](GRAVITY_DECISION.md).

## Tests

```text
tests/structural/StructuralLoadTests.cpp              16 ctest entries
  the canonical face loads carry no mesh handle, by sizeof and constructibility
  a gravity load carries no density; a nodal load carries its stamp
  each kind reports itself and its target
  the area vector is HALF the edge cross product -- and asserted not double
  a reversed winding reverses the normal and keeps the area
  a degenerate triangle has zero area and no direction; a skew one by hand
  A t / 3 per corner, exactly; and asserted NOT A t / 2, NOT A t
  traction resultant force AND MOMENT, about an off-centre origin
  the facet areas sum to the analytical CAD face area
  positive pressure acts inward; the opposite face is pushed the other way
  negative pressure is suction and is not clamped; the area is applied ONCE
  pressure on a closed surface has no net force
  traction ignores facet winding and pressure does not
  every boundary facet winds outward of its owning tetrahedron
  an unresolved target, a malformed selector and a role the body lacks refused
  a nodal force is mesh-local: applied exactly, refused after a remesh,
    rebindable to the new stamp
  NaN and Inf refused in all four payloads; a duplicate LoadId refused;
    two DIFFERENT ids on one face superpose; one bad load publishes nothing
  superposition in force and moment; reversing the list; removing a load
  bit-identical over 8 repeats
  gravity weighs the body against its ANALYTIC volume, with the moment at the
    centre of volume, a sideways g, a missing density, and a density edit
  the surface and body scale laws at three scales

tests/reference/StructuralLoadReferenceTests.cpp       7 ctest entries
  RM-MESH-01's end cap: facet area == analytic, force == t A, moment exact
  a remesh re-resolves the canonical target; no facet identity carries over
  convergence on RM-MESH-02's cylindrical wall: 72 -> 100 -> 200 facets
  RM-MESH-06: pressure rotates as R F, global traction does not
  RM-MESH-04's bore: facet area approaches 2 pi r h from below, and every
    loaded node is pushed outward
  the drilled-hole wall is refused, with the named bore as its control
  RM-MESH-01 weighed against its analytic volume
```

## Zero-match protection

Counted with `-N` before every run: `StructuralLoad_` selects **23**. The
final regression is unfiltered in all three presets.

## Mutation protection

```text
PROBES    13
KILLED    12
SURVIVED   1   M13, inert for a stated structural reason
```

Every error the brief names has a probe. The most informative result:
**M2 — the whole facet force on one corner — preserves the total force
exactly** and is caught only through the moment. That is the measured
justification for every moment assertion in the milestone.
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Determinism

No unordered container; accumulation is a `std::map` keyed on `NodeId`, so the
emitted field is ascending and the floating-point sums are formed in the same
order everywhere. Repeated preparation is asserted **bit-identical** within a
build.

**Cross-preset equivalence**, which matters most here because every claim is a
floating-point comparison over a mesh:

```text
[load]         debug-ext         All tests passed (3534 assertions in 23 cases)
               release-ext       All tests passed (3534 assertions in 23 cases)
               debug-shared-ext  All tests passed (3534 assertions in 23 cases)

[structural]   all three         All tests passed (34680 assertions in 133 cases)
```

Identical, including the closed-surface cancellation, the convergence
monotonicity and the `R F` transform comparison.

## Adversarial review

```text
QUESTIONS                       34  (30 from the brief, 4 of my own)
FINDINGS                         4
PRODUCTION DEFECTS               0
TEST DEFECTS OF MINE             2  F1, vacuous twice and PASSING the second
                                    time; F4, two compile errors from writing
                                    against remembered APIs
DIAGNOSTIC DEFECTS               1  F2, "load load:4"
UNREACHABLE BRANCHES KEPT        2  F3, both justified and both recorded as
                                    UNTESTED rather than counted as covered
GATE-BLOCKING                    0
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then
two repeat stages. One uninterrupted detached run, 03:20:01 to 06:15:42,
**2 h 55 min 41 s**, 17 stages, `qualify.cmd exit 0`.

```text
PRESET            CONFIGURE  CLEAN  BUILD  NO-OP REBUILD  CTEST
debug-ext              0       0      0         0           0   3545/3545
release-ext            0       0      0         0           0   3545/3545
debug-shared-ext       0       0      0         0           0   3545/3545

REPEAT (5x each of 820 selected tests, back to back)
release-ext            0                            820/820   747.73 s
debug-ext              0                            820/820   789.46 s

warnings, all three clean builds   0   (-Werror and 22 warning flags,
                                        615 objects each)
no-op rebuilds                     0 compiles, 0 links in every preset
shared build                       10 DLLs
```

**3545** is P17-ELEM-001's 3522 plus this milestone's 23; both counts were
taken independently and reconciled rather than one derived from the other.

**615** objects is 612 plus exactly three new translation units, confirmed by
name in the build log: `StructuralLoad.cpp`, `StructuralLoadTests.cpp` and
`StructuralLoadReferenceTests.cpp`.

**820** repeated tests is P17-ELEM-001's 797 plus 23. No new filter term was
needed — `unit\.Structural` already matches `StructuralLoad_*` — but that was
**verified by counting the milestone's own tests inside the selection**, not
inferred from the pattern. The repeat set is the broad one because this
milestone touches `core`, and the driver's comment says so.

## Known limitations

```text
NamedBoundarySet is NOT used. P16 provides it, and a load could target one --
  but a named set lives in MeshControlDefinition::boundarySets, which is
  MESHING intent, so a load targeting one would make the structural analysis
  depend on a collection the mesh control owns. P17-BC-001 has the natural
  need and can decide where the canonical set should live. Nothing is
  unreachable without it: a set is a union of FaceNames and several loads
  superpose.

LoadProblem::TargetWithoutFacets has NO TEST. It fires when a FaceName resolves
  and the mapping attributed no facet to it -- a state P16 names and reports,
  but which I could not construct, because the mapping is complete for every
  committed reference model. The branch was KEPT rather than deleted, because
  the fallthrough would be a silent zero load, which the brief forbids. Seven
  of the eight LoadProblem values have a test; this one does not.

The no-target guard in prepareStructuralLoads is likewise unreachable today,
  since the nodal and gravity branches return before it and both remaining
  payloads always carry a face. Kept because target() returns an optional and a
  fifth payload without a target would otherwise fall through silently.

A durable POINT load is not supported. NodalForceLoad is mesh-local by
  declaration and dies with its mesh; a point load on the model would need a
  canonical CAD VERTEX reference, which P16 does not provide.

A drilled hole's cylindrical wall cannot be a load target, because cutHole
  names a hole's flat faces and not its wall. This is P16-MAP-001's recorded
  limitation and the refusal is what passes -- tested against its control, the
  named bore of RM-MESH-03, which succeeds.

The derived nodal field is NOT a global right-hand side. Mapping it into
  equation space is P17-ASSEMBLY-001's, through P17-DOF's MeshDofMap.

Density is assigned per DOCUMENT in P15, so a graded or per-body body force is
  not representable. Inherited from P17-MAT-001's recorded limitation.

This MinGW toolchain has no ASan/UBSan. Inherited and recorded.
```

## The qualified tree is the committed tree

```text
| WHEN                               | WHOLE FINGERPRINT                        |
| frozen, before the first configure  | 27afe8d34f444b303940dc1ad9b3c51d8476f731 |
| recorded by the harness after the   | 27afe8d34f444b303940dc1ad9b3c51d8476f731 |
|   last test of the last preset      |                                          |
| recomputed before the commit        | 27afe8d34f444b303940dc1ad9b3c51d8476f731 |
```

Component for component at all three readings:

```text
apps               b49722470580d4fe6c2bb2b3594bb3885eee10db
include            38b76d284081e0dc9d4a910db3d23196206a7186
src                4d5eb402931aafc4351e51386d6a0a4602d72842
tests              25903e4f6439599e67fc5e3d1df52c93ac802dc1
examples           75ce1c6852e03011f350d6806f4e8d5f3207d63e
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e
```

Three paths moved; five are byte-identical to P17-ELEM-001's. The whole value
is a function of the eight paths **plus the base tree HEAD pointed at**, which
was `dd2aca6` throughout. The invariant that survives a moving HEAD is the
component list:

```bash
git fetch origin && for p in apps include src tests examples cmake CMakeLists.txt CMakePresets.json; do echo "$p $(git rev-parse "origin/main^{tree}:$p")"; done
```

What moved after the freeze: `docs/verification/P17-LOAD-001/`, `TODO.md`,
`ROADMAP.md` and `README.md` — all documentation, none inside the fingerprint.

[FREEZE.md](FREEZE.md) records the four pre-freeze checks, the mutation
harness's verified restoration, and the harness provenance: `qualify.cmd` is
byte-identical at `d313a64070718c44fae290ac042fe259d1a03c8b`, unchanged since
P16-SIZE-001 and now sixteen milestones in a row.

## Result

```text
RESULT:   PASS
TESTS:    3545/3545 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 615 objects; 820 x 5 repeats in two presets;
          17 stages, 0 failed
MUTATION: 13 probes, 12 killed, 1 inert
TREE:     27afe8d34f444b303940dc1ad9b3c51d8476f731, identical at all three
          readings
EVIDENCE: this directory
```

## Revision

First issue, 2026-10-08.
