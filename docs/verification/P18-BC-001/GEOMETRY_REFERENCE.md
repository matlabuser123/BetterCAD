# P18-BC-001 — the geometry reference, P16's mapping, and the persistence audit

```text
SUBJECT:  what names a boundary condition's target, how it is resolved, what is
          refused, and the audit that no mesh-local identity is canonical
```

## Authority

```text
both kinds   FaceName {ObjectId feature, FaceSelector {role, entity?}}    P12-STREF-001
             the one canonical face reference in the tree; the brief's
             "GeometryReference" is this type
```

A canonical condition never holds a facet, a node, an element or an unknown. The
mirror-struct `sizeof` tests fix the members (`FaceName` and `Temperature`;
`FaceName`, `HeatTransferCoefficient` and `Temperature`), and seven compile-fail
cases name each absent member (node, facet and unknown for both kinds, and the
conductivity for the convection).
Resolution is **read-only**: the canonical conditions, the document, the analysis
and its revision, the CAD object, the mesh and the mapping are bit-for-bit
unchanged by preparing, three times over and through the definition overload
(`PreparationDoesNotMutateCanonicalBCCadOrMesh`).

## Resolution is P16's, and only P16's

`thermal::resolveFaceTarget`, the three checks P18-LOAD-001 wrote for both thermal
consumers, classify P16's answers and measure nothing:

```text
meshing::boundaryFacetsOf FAILS         only for a malformed selector
                                        -> TargetInvalid   (a Side face with no entity)
!set.fullyResolved()                    the name names no face of the body now
                                        -> TargetUnresolved
set.facets.empty()                      resolved, no facet attributed
                                        -> TargetWithoutFacets
```

This milestone changes neither P16 nor `ThermalTarget`. The third check is
reachable only through the integration seams (P16 never hands an empty set for a
face that resolved); both seams refuse an empty list (`EmptyMappedTargetFails`).

## No nearest face, no rebinding

Presented with a face the body does not have, the request is **refused**:

| Canonical target | Result | Alternative present? | Rebound? |
| --- | --- | --- | --- |
| `FaceName{block, HoleBottom}` | `TargetUnresolved`, NotFound | yes: both caps and four sides | NO |
| `FaceName{block, CounterboreFloor}` / `SpotfaceFloor` | `TargetUnresolved` | yes | NO |
| `FaceName{object 9999, EndCap}` | `TargetUnresolved` | yes | NO |
| `FaceName{block, Side}` with no entity | `TargetInvalid`, InvalidArgument | yes | NO |

for both a prescribed temperature and a convection, and the same conditions
prepared against faces that do resolve succeed, so the refusal is about the missing
face and nothing else. Mutations B41 (a genuine nearest-face fallback) and B42
(unresolved treated as resolved) are killed.

A drilled hole's cylindrical wall cannot be written as a target at all: `FaceRole`
has no role for it. That is the P16 limit P17 and P18-LOAD-001 recorded, inherited
unchanged. An edge, a vertex or a point has no `FaceRole` either, so an unsupported
topology cannot be expressed; there is no value to reinterpret.

## The persistence audit

```text
facet IDs persisted:          NO
NodeIds persisted:            NO
ThermalDofIndices persisted:  NO
Robin matrices persisted:     NO
constraint maps persisted:    NO
```

| Audit | Result |
| --- | --- |
| mesh-local type or `meshing::` name in `ThermalData.hpp` (the canonical header) | 0 matches |
| use of `PreparedThermalBoundary`, `prepareThermalBoundary` or `Tri3Convection` outside `src/thermal` and `include/bettercad/thermal` | 0 |
| mention of the prepared types in `src/io` or `include/bettercad/io` | 0 |
| a prepared type reachable from a `ThermalBoundaryCondition` | no: the variant holds a `FaceName` and physical numbers |

The prepared boundary is a derived, in-memory value. `P18-PERSIST-001` stores the
canonical conditions (id, type, face, `T_bar`, or `h` and `T_inf`) and nothing the
preparation produced; `P18-CMD-001`'s history carries the same, so no constraint map
or Robin array can enter an undo payload.

## The source audits

Run on the final tree over `ThermalBoundary.{hpp,cpp}` and
`ThermalConvection.{hpp,cpp}` (case-insensitive; comment lines excluded from the
"in code" column):

| Audit | Pattern | In code |
| --- | --- | --- |
| absolute value of a coefficient, temperature or area | `std::abs`, `fabs` | 0 |
| nearest face, centroid, distance | `nearest`, `closest`, `centroid`, `distance` | 0 (4 comment lines saying it is not done) |
| unit conversion, Celsius | `1.0e3`, `1000`, `1.0e6`, `1.0e9`, `mm`, `273.15`, `celsius` | 0 |
| unordered container | `unordered_` | 0 |
| static, mutable or thread-local state | `static` (not `constexpr`, `assert`, `cast`), `mutable`, `thread_local` | 0 |
| penalty, epsilon, anchor, a large constant | `penalt`, `epsilon`, `anchor`, `1.0e30`, `huge` | 0 |
| global assembly, sparse, factorisation | `assembl`, `SparseMatrix`, `factori`, `Eigen`, `CSR` | 0 |
| solve, reduced system | `solve` not preceded by `re`, `reduced` | 0 |
| conductivity, material lookup | `conductiv`, `MaterialRef`, `materialOf`, `findMaterial` | 1: the `static_assert` that `h` is not `k` |
| radiation | `radiat`, `emissiv`, `boltzmann`, `sigma` | 0 |
| time-varying coefficient | `transient`, `dt` | 0 |
| contact conductance | `contact` | 0 |
| random, clock | `rand`, `chrono`, `clock`, `time(` | 0 |

Includes of the four files: `core`, `meshing` and `thermal` only, which is the layer
rule of ADR-050. The layering checker reports **517 files, 0 violations**.
