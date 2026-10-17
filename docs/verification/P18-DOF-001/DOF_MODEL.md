# P18-DOF-001 — the temperature DOF model

```text
thermal DOFs per node     1   the scalar temperature; no component, no
                              rotation, no flux unknown (kThermalDofsPerNode)
Ndof                      Ndof = Nnode, for the current P16 volume mesh
DOF meaning / unit        one temperature unknown [K]
DOF ordering              ThermalDofIndex d  =  the node's ordinal in the
                          mesh's own enumeration, which P16 guarantees is
                          ascending NodeId. Zero-based. d indexes
                          ThermalResult::temperatures() directly.
```

## The audit that preceded it

| Concern | Existing mechanism | Reusable? | P18 decision | Reason |
| --- | --- | --- | --- | --- |
| Node identity | `meshing::NodeId`, 32-bit, 1-based, sparse, mesh-local, never persisted (ADR-031); `Mesh::findNode` is a binary search, never `nodes_[id.value()]` | Yes, unchanged | the key of the forward lookup, and nothing else | "handles may be sparse (1, 4, 10)" is P16's own text |
| Node order | `Mesh`: "nodes are stored and enumerated in ascending NodeId ... nothing here is an unordered container"; `MeshBuilder::addNode(NodeId, ...)` refuses a handle not greater than the last | Yes | the DOF index IS the ordinal in that enumeration; the map COPIES it, never sorts | one authoritative order, the same one `ThermalResult::temperatures()` is parallel to |
| Mesh identity | `MeshStamp {MeshId, generation}`: fresh `MeshId` per builder from a process-local counter; `Mesh::owns(stamp)` | Yes | every map carries the stamp; `describes()` asks `owns()` AND compares the node count | a stamp names the builder, not the snapshot (P17-DOF-001 F1) |
| Structural `DofIndex` | `structural/StructuralData.hpp`: 1-based, 0 invalid, three per node, interleaved | **No**: layer 45 cannot include 50, and the shape is wrong for one scalar per node | a distinct `ThermalDofIndex`, zero-based, no invalid value | a thermal unknown is a position into the nodal array; a 1-based handle would put a "- 1" at every site |
| Structural `FreeEquationIndex` | zero-based position, no default, absence is `optional` | shape yes, type no | `ThermalFreeEquationIndex`, same shape | the row of the reduced system |
| Constraint pattern | `structural::ConstraintSet`: WHICH DOFs, no value, duplicates refused | shape yes, content no | `ThermalConstraintMap`: which AND at what temperature; exact duplicates collapsed; conflicts refused | a Dirichlet temperature is arbitrary; `uc = 0` is not copied |
| Free-equation pattern | `structural::FreeEquationMap`: compact rows over ascending free DOFs, stamp + count binding | Yes, mirrored | `ThermalFreeEquationMap`, plus `agreesWith(constraints)` | the complement is checked, not assumed |
| Absolute temperature | `thermal::isAbsoluteTemperature` (P18-DATA-001) | Yes | every prescribed value passes it | one rule, not two |
| Nodal result array | `ThermalResult::temperatures()` parallel to `mesh.nodes()` (P18-DATA-001) | Yes | `temperatures()[dof.value()]` is the node's temperature; tested | one convention, not two |
| Compile-fail infrastructure | `bettercad_compile_fail_group/case` | Yes | 15 cases | — |

**Reuse versus a parallel framework.** No generic index template exists in
the tree (P16 wrote its three handles out by hand and said why); P17's two
index types live in `structural/`, which `thermal/` may not include
(ADR-050), and carry 3-per-node arithmetic. Two small distinct types in
`thermal/` are therefore the consistent choice, not a parallel framework:
they are the same two ideas P17 has, written for one unknown per node, and
their incompatibility with P17's is the point rather than a cost -- a
structural row and a thermal row index different matrices.

## The three types, and their dependencies

```text
P16 mesh                       ->  ThermalDofMap            (numbering)
ThermalDofMap + resolved BCs   ->  ThermalConstraintMap     (which, and at what T)
ThermalDofMap + constraints    ->  ThermalFreeEquationMap   (which rows)
```

| Type | Depends on | Does NOT depend on |
| --- | --- | --- |
| `ThermalDofMap` | the mesh (stamp, node enumeration) | material, loads, BCs, solver settings, initial condition, analysis identity |
| `ThermalConstraintMap` | the DOF map; the resolved prescribed temperatures (P18-BC-001's input; synthetic here) | material, loads, solver settings |
| `ThermalFreeEquationMap` | the DOF map; WHICH unknowns are prescribed | the prescribed VALUES (same topology), material, loads |

A `ThermalDofMap` is not bound to a `ThermalAnalysisId`: two analyses on one
mesh may derive equal maps; their constraint and free maps may differ.

## Full and reduced index spaces, on one fixture

Mesh nodes 7, 104, 9001, 500000, 500001; prescribed: unknown 1 at 300 K,
unknown 4 at 350 K.

| NodeId | ThermalDofIndex | Constrained? | Prescribed T | ThermalFreeEquationIndex |
| --- | --- | --- | --- | --- |
| 7 | 0 | no | — | 0 |
| 104 | 1 | **yes** | 300 K | none |
| 9001 | 2 | no | — | 1 |
| 500000 | 3 | no | — | 2 |
| 500001 | 4 | **yes** | 350 K | none |

`Ndof 5, Nconstrained 2, Nfree 3`. Reconstruction from the reduced solution
[310, 320, 330] and the prescribed [300, 350]: [310, 300, 320, 330, 350],
parallel to the mesh's nodes, which is what `ThermalResult::create` takes
(`ThermalDof_FullReconstructionOracle`).

## Canonical / derived

```text
ThermalDofIndex            DERIVED   solver-local; not in core/Id.hpp
ThermalDofMap              DERIVED   rebuilt from the mesh; O(N) handles
ThermalConstraintMap       DERIVED   rebuilt from the DOF map and the resolved BCs
ThermalFreeEquationIndex   DERIVED
ThermalFreeEquationMap     DERIVED

persisted as thermal intent        NO
member of ThermalAnalysisDefinition NO   (static_assert, compile-fail cases; the
                                         definition's size mirror from
                                         P18-DATA-001 would break)
in a command-history payload        NO   the history stores commands; nothing
                                         here is reachable from a command
stored in a canonical BC            NO   PrescribedTemperature is {FaceName,
                                         Temperature}; not constructible from a
                                         ThermalDofIndex or a NodeId (asserted)
```

## Architecture audit, stated

```text
NodeId as permanent thermal CAD identity    NO   mesh-local; it is the forward
                                                 lookup's key at runtime and
                                                 never a field of canonical intent
ElementId as thermal DOF identity           NO
nearest-node rebinding                      NO   an absent node is NotFound; a
                                                 foreign mesh is refused by stamp
raw NodeId arithmetic                       NO   the one `nodes_[...]` in the
                                                 module is indexed by the DOF
                                                 ordinal, which is dense by
                                                 construction
coordinates in the mapping                  NO   identity and topology only
global or static map                        NO   a map belongs to the mesh it was
                                                 built from; two documents' maps
                                                 are independent (tested)
cache                                       NO   construction is one O(N) copy
```
