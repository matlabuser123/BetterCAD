# P18-DATA-001 — result provenance and currentness

```text
SUBJECT:  what a thermal result records about where it came from, which
          changes stale it, and where that is decided
MODEL:    P17-DATA-001's revision tuple, reused rather than reinvented, with
          one field added (the mode) and one stale reason added for it
```

## The source

```cpp
struct ThermalResultSource {
    ObjectId                  body;
    MeshControlId             control;
    meshing::GeometryRevision geometry;
    meshing::MeshStamp        mesh;
    MaterialId                material;
    std::uint64_t             materialRevision;
    ThermalAnalysisId         analysis;
    std::uint64_t             analysisRevision;
    ThermalAnalysisMode       mode;
};
```

**A revision tuple, not a hash.** Separate fields so that a stale result says
*which* dependency moved; field-complete equality by `= default`, so a field
added without being compared is not possible. Nothing in it is derived from
bytes, addresses, padding, iteration order or a clock: every field is an
identity, a monotonic revision or an enumerator, and the type is trivially
copyable with a size equal to the sum of its fields plus the one alignment pad
(asserted in `ThermalData_DeterministicCanonicalOrdering`).

| Field | Read from | Covers | Why it is separate |
| --- | --- | --- | --- |
| `body`, `control` | `ThermalModel::body()`, `control()` | which geometry, which meshing intent | by document identity |
| `geometry` | `ThermalModel::geometryRevision()` | every CAD edit under the body, the active configuration | P16 narrowed it so a material edit does NOT move it |
| `mesh` | `VolumeMesh::mesh().stamp()` | which mesh **and which generation** | the same counts and the same NodeIds can be a different mesh |
| `material`, `materialRevision` | `requireEffectiveMaterial`, `Material::revision()` | which material; k, rho, cp edited | moves only on an effective edit |
| `analysis`, `analysisRevision` | `findThermalAnalysis`, `ThermalAnalysis::revision()` | **loads, boundary conditions, initial condition, solver settings, transient settings, mode** -- every field of the one definition | one counter, not six: six would be six chances to forget one |
| `mode` | `definition().mode` | what kind of field the result is | so a consumer can tell a transient sample from a steady field without the document; redundant with `analysisRevision` for currentness and reported as its own reason when it differs |

**Deliberately not in it:** solve duration, residual, step count, backend
name, pointer to the mesh, node or element counts, array sizes, any value.

## Where it is read from, and the gates inherited

`currentResultSource(document, regenerator, mesher, analysis)` calls
`requireThermalModel`, so every gate ADR-051 established is inherited rather
than re-asked: the control exists, the body is eligible and current, a mesh is
held and current, the material resolves and yields a usable `k`. It adds only
what the input boundary has no reason to know: the analysis, its revision and
mode, and the material's identity and revision.

## The currentness matrix

| Changed | Expected | Test |
| --- | --- | --- |
| nothing | **CURRENT** | `SourceFingerprintIsSensitiveToEveryField`, "nothing changed" |
| body | STALE `body` | same |
| mesh control | STALE `mesh_control` | same |
| geometry revision | STALE `geometry` | same |
| mesh generation alone | STALE `mesh` | same |
| mesh identity alone | STALE `mesh` | same |
| material | STALE `material` | same |
| material revision alone | STALE `material` | same |
| analysis | STALE `analysis` | same |
| analysis revision alone | STALE `analysis` | same |
| mode alone | STALE `mode` | same |
| several at once | STALE, **all reported, enum order** | same |
| **same NodeIds, same counts, new mesh generation** | STALE `mesh`; `describes` false; `temperatureOf` refused | `SameIdsNewMeshGenerationIsStale` |
| **same values, new source generation** | old STALE, new CURRENT | `SameContentNewSourceGenerationIsStale` |
| different values, same source | both CURRENT | `CurrentnessComparesSourceIdentityNeverArraysOrSizes` |
| flux load added / volumetric source added | STALE `analysis`, mesh current | `EveryDefinitionEditMakesTheResultStale` |
| prescribed temperature / convection added | STALE `analysis` | same |
| a load's value edited | STALE `analysis` | same |
| solver tolerance | STALE `analysis` | same |
| mode steady -> transient / transient -> steady | STALE `analysis`, `mode` | same |
| dt, end time, output policy, initial temperature | STALE `analysis` | same, transient section |
| k edited 45 -> 50 W/(m K) | STALE `material`, **mesh current** | `AMaterialEditStalesTheResultAndNotTheMesh` |
| geometry edited for real | `InputsUnavailable` | `AGeometryEditMakesInputsUnavailable` |
| quality threshold (presentation) | **CURRENT** | `AViewerOnlyChangeDoesNotStaleAResult` |
| source populated, not defaulted | every field non-zero and equal to the document's | `TheSourceIsPopulatedFromTheDocumentAndNotDefaulted` |

The first block mutates a struct and proves the comparison is complete. The
second block edits the real document and proves the source is populated from
it -- the blind spot P17-DATA-001's review found (its F1) is covered here from
the first draft, for every definition field and for the material.

## Undo

Inherited from P16 and P17 unchanged, and the honest answer requires saying
both halves: meshing intent is compared **by value**, so an undone intent edit
restores mesh currency; geometry is compared by **monotonic revision
counters**, so an undone geometry edit leaves the mesh stale until a remesh.
The thermal analysis revision is monotonic, so undoing a load or setting edit
leaves a result stale and it must be re-solved. The conservative half, chosen
knowingly: the cost of being wrong this way is one solve that was not strictly
needed; the cost of the other way is a result presented as describing a model
it does not.

## Where currentness is decided

One place, and these are the only answers in P18:

```cpp
Result<ThermalResultSource> currentResultSource(document, regenerator, mesher, analysis);
ResultCurrency              resultCurrency(result, current);
AnalysisState               analysisState(document, regenerator, mesher, analysis,
                                          lastResult, lastFailure);
```

`resultCurrency` compares `result->source() == current` and reads nothing
else. A viewer that compared node counts and a CLI that compared an analysis
ID would be two answers that drift, and the one that drifts towards "current"
ships a wrong temperature.
