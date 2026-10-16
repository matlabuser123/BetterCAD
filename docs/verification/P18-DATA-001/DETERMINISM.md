# P18-DATA-001 — determinism

```text
SUBJECT:  the same document gives the same source, the same state, the same
          stale reasons and the same roster, in every preset and on every run
```

## What is ordered, and by what

| Collection | Order | Why that order |
| --- | --- | --- |
| `thermalAnalysisIds(document)` | ascending `ThermalAnalysisId` | the one canonical roster, whatever order the objects were added or iterated in |
| `ThermalAnalysisDefinition::loads`, `::boundaryConditions` | **the user's order, kept** | the order is canonical intent, compared by `operator==` and persisted as given; two definitions with the same records in a different order are different intent (asserted) |
| `staleReasons(result, current)` | the enum's order: Body, Control, Geometry, Mesh, Material, Analysis, Mode | the answer is byte-identical in every preset; never a container's order |
| `ThermalResult::temperatures()` | parallel to `mesh.nodes()`, which `Mesh` documents as ascending `NodeId` | the mesh's own enumeration; no second index, no assumption of dense IDs |
| `ThermalResult::gradients()`, `::heatFluxes()` | parallel to `mesh.tetrahedra()` | likewise |
| `kVectorComponents` | X, Y, Z | frozen; see UNITS.md |

## What is absent

```text
unordered_map / unordered_set   grep over include/bettercad/thermal and
                                src/thermal: 0 occurrences
wall clock, timestamps          nothing in ThermalResultSource, ThermalResult,
                                ThermalResultSlot or the definition
pointer identity                comparison is by value over IDs, revisions and
                                enumerators; ThermalResultSource is trivially
                                copyable with sizeof equal to its fields plus
                                one alignment pad (asserted)
byte hashing                    there is no hash of anything; equality is
                                field-by-field and defaulted
floating-point tolerance        no approximate comparison anywhere in
                                currentness; a source either equals or it does
                                not
```

## Measured

`ThermalData_DeterministicCanonicalOrdering`: an unchanged document evaluated
16 times gives the same `ThermalResultSource`, the same `SolvedCurrent`, the
same three-element stale-reason vector `{Geometry, Material, Mode}` for the
same mutation, and the same four-element roster; the roster is sorted
ascending after analyses named "B", "A", "C" are added in that order.

Cross-preset equivalence is measured by the qualification: the data-model
suite runs under `-O0 -g` (debug-ext), under the optimiser (release-ext) and
across a DLL boundary (debug-shared-ext), and the assertion counts and message
lines are compared in FREEZE.md.
