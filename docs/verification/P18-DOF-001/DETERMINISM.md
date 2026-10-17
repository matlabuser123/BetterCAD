# P18-DOF-001 — determinism

```text
SUBJECT:  the same mesh gives the same numbering, the same constraint map,
          the same free map and the same element-local unknowns, in every
          preset and on every run
```

## What is ordered, and by what

| Collection | Order | Why that order |
| --- | --- | --- |
| `ThermalDofMap::nodes()` | the mesh's enumeration, copied (ascending `NodeId`, enforced by `MeshBuilder`) | one authoritative order; the same ordinal `ThermalResult::temperatures()` uses |
| `ThermalConstraintMap::constrained()` | ascending `ThermalDofIndex`, unique; ties on one unknown are resolved by value for the conflict check and then collapsed or refused | order-independent of the caller's listing |
| `ThermalFreeEquationMap::freeDofs()` | ascending `ThermalDofIndex` among the free unknowns; row e is entry e | compact, gap-free, reproducible |
| `elementDofs` | the element's own local node order | a Tet4's Jacobian and gradient are written against it |
| diagnostics | the first offender in the sorted order | the same message on every run |

## What is absent

```text
unordered_map / unordered_set   grep over include/bettercad/thermal and
                                src/thermal: 0 occurrences
coordinates in the mapping      none; identity and topology only
pointer identity                comparison is by value over stamps, counts,
                                handles and indices
floating arithmetic             none in any mapping; the only floating
                                comparison is the exact equality of two
                                prescribed temperatures, which decides
                                "the same constraint" and is meant to be exact
thread or global state          none; a map belongs to the mesh it was built
                                from
```

## Measured

```text
ThermalDof_MapConstructionIsDeterministic          16 rebuilds equal, every
                                                   node's unknown equal
ThermalDof_ConstraintMapDeterministic              6 insertion orders equal;
                                                   16 repeats equal
ThermalDof_FreeEquationOrderingDeterministic       6 orders equal; 16 repeats
                                                   equal; a fresh source
                                                   differs by stamp only
ThermalDof_TetLocalDofsPreserveNodeOrder           16 repeats equal
```

Cross-preset equivalence is measured by the qualification: the DOF suite
runs under `-O0 -g`, under the optimiser and across a DLL boundary, and the
assertion counts and message lines are compared in FREEZE.md.
