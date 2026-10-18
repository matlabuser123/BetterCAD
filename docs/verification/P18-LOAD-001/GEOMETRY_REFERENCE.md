# P18-LOAD-001 — the geometry reference, P16's mapping, and the source audits

```text
SUBJECT:  what names a load's target, how it is resolved, what is refused, and
          the audit that no mesh-local identity is canonical
```

## Authority

```text
surface load   FaceName {ObjectId feature, FaceSelector {role, entity?}}    P12-STREF-001
               the one canonical face reference in the tree; the brief's
               "GeometryReference" is this type
volume load    none -- the whole analysed solid (ADR-049: one solid); resolved by
               meshing::regionOf(map, mesh), whose `source` is the body
```

A canonical load never holds a facet, a node, an element or an unknown. The
mirror-struct `sizeof` test asserts the fields (`FaceName` and `HeatFlux`;
`VolumetricHeatSource`), and five compile-fail cases assert that the surface and the volumetric load have no node, facet,
unknown or element member. Resolution is **read-only**: the canonical `FaceName` and the
load list compare equal before and after preparing (`PreparationDoesNot...`,
`RemeshReResolves...`), and the document, the analysis and its revision, the
CAD object, the mesh and the mapping are bit-for-bit unchanged.

## Resolution is P16's, and only P16's

`thermal::resolveFaceTarget` makes three calls into P16 and classifies the
answers; it measures nothing:

```text
meshing::boundaryFacetsOf FAILS         only for a malformed selector
                                        -> TargetInvalid   (observed for a Side with no entity)
!set.fullyResolved()                    the name names no face of the body now
                                        -> TargetUnresolved
set.facets.empty()                      resolved, no facet attributed
                                        -> TargetWithoutFacets
```

Reached through the integration seams instead, because P16 never hands an empty
set for a face that resolved: an empty facet list is `TargetWithoutFacets` and an
empty element list `BodyWithoutElements`, both tested. A `ThermalModel` cannot
exist for a mesh with no tetrahedra, so `BodyWithoutElements` is reachable only
through the seam; that is recorded as a defensive value, not claimed as a
user-reachable one.

## No nearest face, no rebinding

Presented with a face the body does not have, the request is **refused**, not
bound to a neighbour:

| Canonical target | P16 says | Result | Alternative present? | Rebound? | Diagnostic | Prepared state accepted? |
| --- | --- | --- | --- | --- | --- | --- |
| `FaceName{object 9999, EndCap}` | not fully resolved | `TargetUnresolved`, NotFound | yes: both caps | NO | "Nothing nearby is substituted" | NO |
| `FaceName{block, HoleBottom}` | not fully resolved, 0 facets | `TargetUnresolved` | yes: both caps | NO | likewise | NO |
| `FaceName{block, CounterboreFloor}` / `SpotfaceFloor` | not fully resolved | `TargetUnresolved` | yes | NO | likewise | NO |
| `FaceName{block, Side}` with no entity | selector invalid | `TargetInvalid` | yes | NO | core's validate message | NO |
| zero-flux load on `HoleBottom` | not fully resolved | `TargetUnresolved` | yes | NO | likewise | NO |

Mutation L37 inserts a genuine nearest-face fallback (an unresolved name retried
as the end cap) and is killed by the first two rows. L38 and L39 (unresolved
reported as "without facets"; unresolved treated as resolved) are killed.

## A drilled hole's cylindrical wall is not targetable, and that is recorded

`FaceRole` is `StartCap, EndCap, Side, HoleBottom, CounterboreFloor, Chamfer,
SpotfaceFloor`. There is **no** role for a hole's cylindrical wall: `cutHole`
names a hole's flat faces and not the wall. A reference to the wall therefore
cannot be written, and a load cannot target it. Nothing in this milestone works
around that, and in particular nothing persists a mesh facet to reach it. It is
the same P16 limit P17 recorded, inherited unchanged; permanent semantic
topology is P21's.

## Unsupported targets

Not expressible: an edge, a vertex, a point, an interior face, a sub-volume, a
mesh facet identity. The canonical load carries a `FaceName` or nothing, so
there is no value to be "unsupported". Internal faces cannot be named either:
P16's map attributes boundary triangles to CAD faces only, so a heat flux cannot
be applied to both sides of an interior partition.

## The source audits

Run on the final tree (case-insensitive, over `ThermalLoadVector.{hpp,cpp}` and
`ThermalTarget.{hpp,cpp}`, the four files this milestone adds to `src` and
`include`):

| Audit | Pattern | Matches in code | Matches in comments |
| --- | --- | --- | --- |
| absolute value of a load, area or volume | `std::abs`, `fabs` | 0 | 0 |
| nearest face, centroid, distance, tolerance search | `nearest`, `closest`, `centroid`, `tolerance`, `distance` | 0 | 8, each saying the code does NOT do it |
| unit conversion | `1.0e3`, `1000`, `1.0e6`, `1.0e9`, `mm` | 0 | 0 |
| unordered container | `unordered_` | 0 | 0 |
| static, mutable or thread-local state | `static` (not `constexpr`, `assert`, `cast`), `mutable`, `thread_local` | 0 | 1 (the "no static state" statement) |
| convection, Robin, `h T_inf`, ambient | `convect`, `robin`, `film`, `ambient` | 0 | 3 (scope statements) |
| prescribed temperature | `prescribed`, `dirichlet`, `TemperatureConstraint` | 0 | 4 (scope statements) |
| material lookup | `conductiv`, `MaterialRef`, `materialOf` | 0 | 1 (scope statement) |
| solver or global assembly | `solve` not preceded by `re`, `assembl`, `factori`, `SparseMatrix` | 0 | 4 (scope statements) |
| randomness, time, clock | `rand`, `chrono`, `clock`, `time(` | 0 | 0 |

The same audit with `solve` unrestricted matches 51 lines, every one a
`resolve...` identifier or comment; the strict row above removes them.

Includes of the four files: `core`, `meshing`, and `thermal` only, which is the
layer rule of ADR-050. The layering checker reports **513 files, 0 violations**
on the final tree.

Mesh-local identities in canonical types:

```text
ThermalData.hpp  (SurfaceHeatFlux, VolumetricHeatSource, ThermalLoad)   no NodeId, ElementId,
                                                                        ThermalDofIndex or meshing:: type
ThermalAnalysis.hpp  names meshing:: types only in ThermalModel, the runtime
                     object that borrows the current mesh; not a definition type
PreparedThermalLoads members   ThermalLoadId, the load kind, MeshStamp and
                               GeometryRevision (provenance), the canonical
                               load list by value, and vectors indexed by
                               ThermalDofIndex ordinal -- derived, never persisted
serialisation of any load type  0 matches under src/io and include/bettercad/io
use of PreparedThermalLoads outside src/thermal       0
```

Nothing writes a facet, node, element or degree-of-freedom number anywhere that
outlives the mesh: the prepared loads are a derived, in-memory value that
`isCurrent` refuses after a remesh.
