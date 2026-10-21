# P18-POST-001 — the result model

```text
SUBJECT:  what a recovered thermal result is, what it holds, what keys it, and what it is not
```

## The four fields

| Field | Location | Components | Dimension | Unit | Order |
| --- | --- | --- | --- | --- | --- |
| `Temperature` | node | 1 (`T`) | temperature | K | `ThermalDofIndex` |
| `TemperatureGradient` | element | 3 (`dT/dx`, `dT/dy`, `dT/dz`) | temperature / length | K/m | `mesh.tetrahedra()` |
| `HeatFlux` | element | 3 (`qx`, `qy`, `qz`) | power / area | W/m² | `mesh.tetrahedra()` |
| `HeatFluxMagnitude` | element | 1 (`|q|`) | power / area | W/m² | `mesh.tetrahedra()` |

`descriptorOf(field)` states this table in the code (location, component count, component names, `Dimension`, unit symbol); the
dimension is the authority, taken from the quantity type itself, and the symbol is the text a report prints. A test compares every
column against the table above, and the dimension of each against the type's own (`ThermalPost_FieldDescriptorsStateLocationComponentsAndUnits`).
"Temperature magnitude" is the scalar `T`: a scalar has no vector magnitude and none is formed.

## The container

```text
ThermalPostResult                         private constructor; friends: the two solution seams
  source()        ThermalPostSource       mode, ThermalSystemSource, prescribed temperatures, solver settings,
                                          [initial temperature, transient settings]  -- the solution's own, by value
  nodeCount()     Ndof
  elementIds()    span<ElementId>         ascending, parallel to every element array
  ordinalOf(id)   optional<size_t>        binary search; an ElementId is never an index
  states()        span<ThermalPostState>  1 for steady (no time); N for transient, in the solution's order
    ThermalPostState
      time           optional<Time>       nullopt for steady
      temperatures   vector<Temperature>  K, parallel to ThermalDofIndex; the solver's values, bit for bit
      fields         ThermalElementFields gradients / heatFluxes / heatFluxMagnitudes, parallel to mesh.tetrahedra()
  temperatureOf(state, map, NodeId)       through the numbering; another numbering is refused
  gradientOf / heatFluxOf / heatFluxMagnitudeOf (state, ElementId)
```

One container serves both analyses: a steady result is the case of one state without a time, a transient result has one state per
stored time level. There is no second type, no per-mode subclass and no "current" sample: `finalState()` is the last state.

| Property | How it is enforced |
| --- | --- |
| immutable | no setter, no non-const accessor, `states()` is a `span<const>` (compile-fail `…ARE_READ_ONLY`, five cases) |
| not constructible half-built | private constructor; only the two seams build one, after every check (compile-fail `A_POST_RESULT_CONSTRUCTED_DIRECTLY`) |
| atomic | the arrays are built in temporaries and published by the one constructor call at the end (`OneInvalidTetrahedronFailsTheWholeRecovery…`, `ALateStateBelowAbsoluteZero…`) |
| keyed by ordinal | `ordinalOf` is a `lower_bound` over stored ids (`ElementResultsAreKeyedByOrdinalAndNotByRawElementId`) |
| no conductivity stored | `k` is read from the document through P18-MAT-001 at each recovery and is not a member (compile-fail `THE_RESULT_STORES_NO_CONDUCTIVITY`) |
| not a `ThermalResult` | that type also carries a heat balance (P18-BALANCE-001); compile-fail `A_POST_RESULT_IS_NOT_A_THERMAL_RESULT` |

## Why the temperature is a snapshot

`ThermalPostState::temperatures` copies the solver's vector. The solver's result remains the source of truth for it (it is bit for bit
equal, checked per unknown), and the copy is what lets a consumer hold one object that is self-sufficient for drawing and reporting
without also holding the solution. It costs `Ndof` doubles per state, bounded by what the solution already stores.

## Capacity

Storage is `O(Ndof + Ntet)` per state, whatever the identities: the largest `NodeId` or `ElementId` appears nowhere as an extent. The
sparse-identity tests use handles 7, 42, 104, 9001 (result arrays of size 4, not 9002) and ElementIds 2 and 3 (arrays of size 2).

## Derived, not canonical

| Item | Canonical? | Persisted? | In command history? | Where it lives |
| --- | --- | --- | --- | --- |
| thermal analysis definition, loads, boundary conditions, material | yes | yes (P15 / P18-DATA) | yes | the document |
| mesh | derived from canonical + meshing settings | no | no | P16 |
| nodal temperatures | derived | no | no | the solution |
| `ThermalPostResult` (T, grad T, q, \|q\|) | **derived** | **no** | **no** | memory, bound to its source |
| colour range, selected component, smoothing display | view state | not here | no | P18-VIZ-001 |

Nothing here can be saved as authority; whatever a later persistence milestone stores, this result is reproducible from what is canonical.
