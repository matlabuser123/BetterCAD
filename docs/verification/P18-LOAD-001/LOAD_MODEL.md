# P18-LOAD-001 — the thermal load model, frozen

```text
SUBJECT:  canonical heat-load intent, its resolution through P16, its
          conversion to an equivalent nodal heat input, and the provenance
          that makes a prepared load detectably stale
STATUS:   the specification. src/thermal/ThermalLoadVector.cpp and
          ThermalTarget.cpp are checked against THIS document by the tests,
          not the other way round.
```

## The audit, before any design (brief section 4)

| Concern | Existing mechanism | Reusable? | P18 decision | Reason |
| --- | --- | --- | --- | --- |
| Canonical load identity | `ThermalLoadId` (P18-DATA-001) | YES, unchanged | use it; add none | "no second load identifier in the tree" |
| Canonical load types | `SurfaceHeatFluxLoad {FaceName, HeatFlux}`, `VolumetricHeatSourceLoad {VolumetricHeatSource}`, `ThermalLoad` variant, `validate(ThermalLoad)` (P18-DATA-001) | YES, unchanged | **the canonical model already exists**; this milestone defines nothing canonical anew | the P17 split (intent in the object's header, derived machinery in a separate one) was adopted by P18-DATA; a second definition would be a parallel system |
| Geometry-reference authority | `FaceName {feature, FaceSelector}` (core/document/References, P12-STREF-001) | YES | the surface target; the brief's "GeometryReference" is this type | it is the one canonical face reference in the tree |
| Volume target | none; `VolumetricHeatSourceLoad` has no target field | YES, as it stands | the whole analysed solid, resolved through `meshing::regionOf` | ADR-049: one solid; a region selector would name something that cannot vary |
| Boundary mapping | `meshing::GeometryMeshMap`, `boundaryFacetsOf` (P16-MAP-001) | YES, called | all facet attribution; no classification in P18 | "no distance-to-plane, no nearest face, no centroid test" |
| Target triage (invalid / unresolved / no facets) | `structural::resolveFaceTarget` in `StructuralTarget` | **NO — cannot include structural/ (ADR-050)** | `thermal/ThermalTarget.{hpp,cpp}`, the same three checks over the same P16 calls | P17 extracted this later, for BC, and had to requalify LOAD; thermal does it now so P18-BC-001 shares it without touching a qualified file |
| Source/currentness gate | `ThermalModel` possession (P18-ARCH-001) | YES | `prepareThermalLoads` takes a `ThermalModel` | a stale mesh or mapping cannot be presented |
| Mesh-bound derived value | `ThermalDofMap` stamp + count (P18-DOF-001) | YES | index space of the prepared vector; bound by `describes` | `NodeId.value()` is never an index |
| Tet volume and validity | `computeTet4Geometry` (P18-ELEM-001) | YES, generic part only | volume and orientation/degeneracy gates | one volume formula and one sign policy; `Ke`/`Ce` are NOT called |
| Triangle area | `meshing::triangleArea` (P16) | YES | facet area | "unsigned, because a triangle has no sign" |
| Nodal-force preparation | `structural::prepareStructuralLoads`, `PreparedLoads` | shape yes, code no | atomic, ordered, mesh-bound, per-load attribution | force, traction, pressure, normal sign and 3 DOF/node semantics are structural and are NOT carried over |
| Persistence of prepared state | none | — | none | P18-PERSIST-001 stores canonical loads only |

## Canonical versus derived

```text
CANONICAL (P18-DATA-001, unchanged)           DERIVED (this milestone, disposable)
ThermalLoadId                                  resolved boundary facets (ElementIds of THIS mesh)
load kind                                      resolved Tet4 elements (ElementIds of THIS mesh)
FaceName   (surface target)                    NodeIds
HeatFlux q'' [W/m^2]  /  generation q''' [W/m^3]   ThermalDofIndex
                                               nodal heat input Q [W], one per unknown
                                               per-load area / volume / total heat rate
                                               the provenance record
```

`PreparedThermalLoads` is derived: it has no public constructor, one friend,
and is never persisted, never a member of anything canonical, and never undo
payload. The canonical `ThermalLoad` list is read-only to preparation.

## Sign convention — FROZEN (P18-DATA-001's, obeyed exactly)

```text
POSITIVE q''   heat INTO the body through the face (the face is heated)
NEGATIVE q''   heat leaving the body through the face (extraction)
POSITIVE q'''  heat generated in the body
NEGATIVE q'''  a volumetric sink
ZERO           a valid load; a zero contribution (the target must still resolve)
```

`HeatBalance` uses the same convention, so a load's sign and its appearance
in the balance agree. Because the sign is a property of the SCALAR and not of
a direction, **no normal is computed and reversing a triangle's winding cannot
change a contribution**; this is the difference from a pressure.
Signed finite values are accepted exactly as given: no `abs`, no clamp, no
warning for a negative value.

## Units — FROZEN

```text
q''              W/m^2      HeatFlux
q'''             W/m^3      VolumetricHeatSource
facet area       m^2        Area             (P16 coordinates are SI metres)
tet volume       m^3        Volume
nodal heat input W          Power            one per ThermalDofIndex
total heat rate  W          Power

q'' . A       W/m^2 . m^2 = W          static_assert in ThermalLoadVector.hpp
q''' . V      W/m^3 . m^3 = W          static_assert in ThermalLoadVector.hpp
```

No W/mm^2, no W/cm^2, no display conversion, no factor of 1000 in the kernel.
The prepared `Q` is the right-hand side the solve forms: `K [W/K] T [K] = Q [W]`
and `C [J/K] dT/dt [K/s] = W`.

## The two integrals, exact

```text
SURFACE   Qe = integral_A N^T q'' dA ,  integral_A Ni dA = A/3 for a linear triangle
          Q1 = Q2 = Q3 = q'' A / 3                  sum = q'' A         per facet
VOLUME    Qe = integral_V N^T q''' dV , integral_V Ni dV = V/4 for a linear Tet4
          Q1 = Q2 = Q3 = Q4 = q''' V / 4             sum = q''' V        per element
```

Constant `q''` and `q'''` are the whole contract: no spatial function, no
per-node value, no interpolation, no time dependence, no callback. The
formulas are exact, so there is no quadrature. NOT `q'' A` per node, NOT `/4`
on a triangle, NOT `/3` on a Tet.

## Resolution — read-only, atomic, ordered

```text
inputs      ThermalModel (current mesh, current P16 map), ThermalDofMap, loads
order       loads in the order given (the definition's canonical order);
            facets ascending ElementId (P16's deduplicated set); corners in the
            stored topology order; one serial accumulation into a dense Ndof
            vector. No unordered container, no parallelism.
surface     boundaryFacetsOf(map, face) -> Unresolved is a STATUS, not an
            error; invalid selector / unresolved / resolved-without-facets are
            three distinct refusals. Nothing nearby is substituted.
volume      meshing::regionOf(map, mesh) -> every Tet4 of the mapped body
accumulate  Q[dof] += share ; NEVER assigned
atomic      one refusal refuses the whole set; no partial vector is published
checks      duplicate ThermalLoadId, non-finite value, facet area finite and
            > 0, element valid by computeTet4Geometry, every entry of Q finite
```

## Provenance — what makes a prepared load stale

```text
PreparedThermalLoadsSource {
    body        which solid                              (ObjectId)
    mesh        WHICH MESH AND WHICH GENERATION          (MeshStamp)
    nodeCount   a stamp identifies the BUILDER, not the snapshot (P18-DOF)
    geometry    the geometry revision the mapping resolved against
    loads       the canonical loads it was prepared FROM, by value
}
```

A VALUE, NOT A HASH, and no pointer. Equality is field-complete by
`= default`, and `staleReasons` names which dependency moved (Body, Mesh,
Geometry, Loads).

**What it deliberately does NOT contain:** the material (prescribed `q''` and
`q'''` need no `k`, `rho` or `cp`), the solver settings, the time step, the
mode, any boundary condition, or the analysis revision. The analysis revision
moves on ANY edit to the definition, so binding to it would stale a prepared
load for a convection or tolerance edit that cannot change it. Binding to the
LOAD LIST by value gives exactly the dependency the physics has: a load edit
(value, target, order, addition, removal) stales it; a material, solver, mode
or BC edit does not. The thermal RESULT still goes stale on a material edit
through `ThermalResultSource`, which is a separate gate.

## Failure vocabulary (each value reachable, each tested)

```text
DuplicateLoadId      two records share a ThermalLoadId
NonFiniteValue       q'' or q''' is NaN or infinite
TargetInvalid        the FaceSelector is malformed (core's validate)
TargetUnresolved     the FaceName names no face of the body now -- including a
                     drilled hole's unnamed wall. Nothing is substituted.
TargetWithoutFacets  resolved, and P16 attributed no boundary facet to it
BodyWithoutElements  a volumetric load and the mapped body has no Tet4
DegenerateFacet      a facet's area is zero, negative or not finite
InvalidElement       a Tet4 is inverted, degenerate or non-finite (ELEM's gate)
EntityNotInMesh      a facet, element or node the mesh does not have
EntitiesNotUnique    a target list repeats an entity (would double-count it)
DofMapMismatch       the numbering is not this mesh's, or the vector is not Ndof long
NonFiniteResult      a share or a sum overflows
```

## Scope boundary

No convection (`h`, `T_inf`, Robin) — P18-BC-001. No prescribed temperature —
P18-BC-001. No global assembly, solve, transient policy, flux recovery,
persistence, CLI or visualisation. No material lookup. No time dependence.
No heat balance (the per-load, per-kind totals are kept so P18-BALANCE-001 can
attribute, but no balance is computed). `Q` is returned as a vector for
P18-ASSEMBLY-001 to consume; nothing here mutates a global system.

## Known unsupported targets, recorded not worked around

```text
a cylindrical hole wall   cutHole names a hole's flat faces and not its wall, so
                          the wall has no FaceName and cannot be targeted (P16
                          limit, same as P17). A reference that does not resolve
                          is refused.
an edge, a vertex, a point, an interior face, a sub-volume, a mesh facet ID
                          NOT EXPRESSIBLE: the canonical load carries a
                          FaceName or nothing, and compile-fail cases prove it.
```
