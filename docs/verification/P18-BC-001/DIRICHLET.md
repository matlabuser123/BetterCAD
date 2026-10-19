# P18-BC-001 — prescribed temperature

```text
SUBJECT:  GeometryReference + T_bar  ->  current boundary facets  ->  unique
          NodeIds  ->  ThermalDofIndices  ->  ThermalConstraintMap
```

## The pipeline

```text
FaceName, T_bar (canonical)
  -> thermal::faceTargetProblem / resolveFaceTarget          P16 boundaryFacetsOf, nothing else
  -> uniqueFacetDofs(mesh, map, facets)                      corners of the facets, once each, ascending ThermalDofIndex
  -> TemperatureRequest { condition, dof, T_bar }            one per unknown
  -> resolveTemperatureConstraints(map, requests)            P18-DOF-001's buildThermalConstraintMap + attribution
  -> ThermalConstraintMap + sources                          sorted, deduplicated, bound to the numbering's stamp
```

## It is a value, not a bit

P17 restraints hold a displacement at zero, so `ConstraintSet` records only which
unknowns are fixed. A temperature is arbitrary, so the constraint carries it:
`ThermalTemperatureConstraint { ThermalDofIndex, Temperature }`. A prescribed
350 K is 350 K at the end of the chain (`PrescribedTemperatureStoresNonzeroKelvin`),
not zero and not 76.85. A FREE node has **no** prescribed temperature
(`prescribedTemperature(dof)` is `nullopt`), not 0 K.

## One constraint per unknown

A mapped face is many triangles sharing nodes:

```text
facet 1: n1 n2 n3
facet 2: n1 n3 n4        six corners, four unknowns -> four constraints
```

`uniqueFacetDofs` sorts the corner unknowns by `ThermalDofIndex` and removes
repeats. The real block's end cap is two triangles: six corners, four nodes, four
constraints, and everything else on the body stays free
(`MappedFaceDirichletMapsUniqueNodesOnce`).

### On the brief's synthetic numbering

The brief's fixture put `NodeId` 11, 42, 900, 7 at unknowns 0, 1, 2, 3, to show
that order is by unknown and not by handle. That numbering cannot be built: P16's
`MeshBuilder` refuses a node whose handle is not greater than the last one
("node handles are strictly increasing"), and `ThermalDofMap` numbers nodes in
mesh order, so an unknown is always the RANK of its handle. The tests use the same
four handles in the only order the builder accepts, 7, 11, 42, 900, which still
shows the property that matters: no unknown equals its handle
(`7 -> 0, 11 -> 1, 42 -> 2, 900 -> 3`), `NodeId.value()` is never an index, and
the order is read from the unknown. The seam
`resolveTemperatureConstraints` takes arbitrary unknowns, so order independence
there is tested over all 24 and all 120 permutations of the request list.

## Duplicates and conflicts

```text
same unknown, same value, two conditions     one constraint; both ids recorded as having asked
same unknown, different values               REFUSED, nothing accepted
```

- **Exact equality.** `300` and the next double above it conflict. So do `300` and
  `300.000001`. No solver tolerance decides whether two user commands are "the
  same".
- **Not first, not last, not the mean.** B04 (first wins), B05 (last wins) and B06
  (mean) are mutations the suite kills.
- **Partial overlap.** The end cap and a side wall meet along an edge. Prescribed
  350 K and 300 K they disagree at exactly the edge's nodes, and the report counts
  every one (`PartialSurfaceOverlapConflictRejected`). Prescribed 300 K and 300 K
  they overlap cleanly: the union of the node sets, shared nodes attributed to
  both (`IdenticalOverlapAlongAnEdgeDeduplicates`). Faces that do not meet never
  conflict, whatever their values.
- **A summary, not a flood.** One `TemperatureConflict` per PAIR of conditions
  with the number of unknowns they disagree on and the lowest one as a
  representative, ordered by condition id. Every unknown is still examined, and a
  pair is counted once per unknown however many requests meet there
  (`ConflictCountsEachUnknownOncePerPair`). The report is identical under every
  permutation of the input (`ConflictReportIsSummarisedAndOrderIndependent`,
  120 permutations).

## What is valid at this layer

| Situation | Result |
| --- | --- |
| no prescribed temperature at all | valid: `constraints()` is empty |
| every unknown prescribed (`Nfree = 0`) | valid: `buildThermalFreeEquationMap` reports `freeCount() == 0` |
| a prescription of 0 K | valid, and stored as 0 K |
| below 0 K, NaN, infinity | refused (`TemperatureInvalid`), by the canonical rule |
| a face that resolves to no facet | refused (`TargetWithoutFacets`), not a successful empty condition |

Nothing is added to a diagonal or a right-hand side. There is no penalty factor, no
epsilon anchor and no reduced system in this milestone; P18-SOLVE-001 eliminates
the constrained unknowns exactly.
