# P18-DATA-001 — the identity model

```text
SUBJECT:  three thermal document identities, what belongs in each domain, and
          why mixing them -- with each other, with a mesh handle, with an
          integer, or with P17's identities -- is a compile error rather than
          a review note
METHOD:   the existing ID system audited before anything was added; every
          mechanism below is the one already in the tree
```

## The audit, before any ID was defined

`include/bettercad/core/Id.hpp` holds one mechanism, `Id<Tag, Value>`, and
one opt-in, `isDocumentObjectTag<Tag>`. P17-DATA-001 audited it in full and
its conclusions hold unchanged; what P18 added is three tags and nothing else:

| Concern | Existing mechanism | Reusable? | P18 decision | Reason |
| --- | --- | --- | --- | --- |
| Strong, non-interchangeable IDs | `Id<Tag>`: distinct type per tag, no integer conversion, defaulted `==`/`<=>`, invalid when default-constructed | Yes | Three new tags: `ThermalAnalysisIdTag` "thermal analysis", `ThermalLoadIdTag` "thermal load", `ThermalBoundaryConditionIdTag` "thermal boundary condition" | The mechanism is what P17 and every earlier phase use; a second ID template would be the duplicate P17-DATA-001 refused |
| Which IDs are document objects | `isDocumentObjectTag<Tag> = true` widens implicitly to `ObjectId` | Yes | Only `ThermalAnalysisIdTag` opts in | An analysis is a document object; a load and a boundary condition are members of its definition, the shape `LoadId`, `RestraintId` and `BoundarySetId` established |
| ID allocation | `Document::addObject` allocates object IDs from the document's one `IdAllocator`; member IDs are chosen by the caller and checked for duplicates (`AddBoundarySetCommand`, P17's load and restraint commands) | Yes | Analysis IDs come from the document; load and BC IDs are caller-chosen and `validate(definition)` refuses a duplicate | Exact undo needs the same ID back, which a caller-chosen ID gives for free |
| Diagnostic names | `std::formatter<Id>` prints `<Tag::name>:<value>` | Yes | `"thermal analysis:3"`, `"thermal load:3"`, `"thermal boundary condition:3"` | A thermal load in a log can never read as a structural `load:3` |
| Mesh-local handles | `meshing::NodeId`, `ElementId`, `MeshStamp` -- deliberately NOT `Id<Tag>` (ADR-031) | Yes, unchanged | Nothing thermal is a mesh handle; nothing canonical holds one | ADR-032: canonical intent names CAD geometry |
| Solver-local index | `structural::DofIndex`, in `structural/` and not in core | Not reachable (layer 45 cannot include 50) and not needed yet | No thermal DOF type in this milestone | P18-DOF-001 defines the scalar numbering; defining its index type without its numbering would be a strongly-named identity nothing resolves, the `LoadCaseId` lesson |
| Face naming | `FaceName {ObjectId feature; FaceSelector face}` with `validate(FaceSelector)` in core | Yes | Every face-targeted load and BC stores exactly a `FaceName`; `validate` asks core's own selector check | One classification of geometry, P16's; no thermal notion of a face |

## The domains

| Identity | Domain | Canonical | Persisted | Lifetime | Survives remesh | Widens to `ObjectId` | Owner |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `ThermalAnalysisId` | Document | Yes | Yes (P18-PERSIST-001) | The document's | **Yes** | **Yes** | `core/Id.hpp`, used by `thermal` |
| `ThermalLoadId` | Document, member of an analysis | Yes | Yes | Its analysis's | **Yes** | No | likewise |
| `ThermalBoundaryConditionId` | Document, member of an analysis | Yes | Yes | Its analysis's | **Yes** | No | likewise |
| `FaceName` | CAD intent | Yes | Yes | The feature's | Yes, by resolution | — | `core` |
| `AnalysisId`, `LoadId`, `RestraintId` | P17's document identities | Yes | Yes | — | — | analysis only | `core/Id.hpp`, used by `structural` |
| `NodeId`, `ElementId` | One mesh generation | **No** | **No** | That mesh's | **No** | No | `meshing` |
| `MeshStamp` | One mesh generation | No | No | That mesh's | No | No | `meshing` |
| `ThermalResultSource` | Derived provenance | No | No | Its result's | No | — | `thermal` |
| a DOF index, a row, a column, a facet index | Solver- or mesh-local | No | **Never** | One preparation | No | — | not defined in this milestone |

## What the compiler refuses

`tests/compile_fail/ThermalIdMisuse.cpp`, one control that must compile plus
24 cases that must not:

```text
IDENTITY DOMAINS
  ThermalAnalysisId -> ThermalLoadId            REFUSED
  ThermalAnalysisId -> ThermalBoundaryConditionId  REFUSED
  ThermalLoadId <-> ThermalBoundaryConditionId  REFUSED, both directions
  ThermalLoadId -> ObjectId                     REFUSED  (a member, not an object)
  ThermalBoundaryConditionId -> ObjectId        REFUSED
  NodeId -> ThermalAnalysisId / ThermalLoadId   REFUSED  (ADR-032)
  ElementId -> ThermalLoadId                    REFUSED
  integer -> ThermalLoadId                      REFUSED
CROSS-PHASE
  structural LoadId -> ThermalLoadId            REFUSED
  AnalysisId <-> ThermalAnalysisId              REFUSED, both directions
CANONICAL RECORDS
  a flux load given a NodeId as its target      REFUSED
  a Temperature typed as a HeatFlux             REFUSED
  a ThermalConductivity typed as an h           REFUSED  (W/(m K) vs W/(m^2 K))
  a HeatFlux typed as a prescribed Temperature  REFUSED
  the mode assigned a bool                      REFUSED
  definition.temperatures / .conductivityMatrix / .result   NO SUCH MEMBER
RESULTS
  result.source().mesh.generation = 0           REFUSED  (read-only)
  result.setTemperatures(...)                   NO SUCH MEMBER
  result.temperatures()[0] = ...                REFUSED  (span of const)
PERMITTED, exercised by the control
  ThermalAnalysisId -> ObjectId                 compiles, and must
```

The run-time half, `ThermalData_StrongIdsAreDistinct`,
`ThermalData_OnlyTheAnalysisIsADocumentObjectIdentity` and
`ThermalData_NoMeshOrSolverHandleCanActAsAThermalIdentity`, states the same
facts as `static_assert`s on `is_convertible` and `is_constructible`, and
asserts by `sizeof` that `SurfaceHeatFluxLoad`, `PrescribedTemperature` and
`ConvectionBoundary` are exactly a `FaceName` and their values -- nowhere for
a facet, a node or a resolved set to hide.

## What is deliberately not defined

```text
a thermal DOF index         P18-DOF-001's, with its numbering
a load-case identity        one analysis is one case, as in P17
a region or body selector   ADR-049 is one solid; a selector would name
                            something that cannot yet vary
an edge or vertex target    P16 provides no canonical edge or vertex
                            reference; recorded, not worked around
```
