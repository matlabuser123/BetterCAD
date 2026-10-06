# P17-DATA-001 — result provenance, currentness and the state machine

```text
SUBJECT:  what a result records about where it came from, which changes stale
          it, and where that is decided
```

## The source stamp

```cpp
struct StructuralResultSource {
    ObjectId                 body;
    MeshControlId            control;
    meshing::GeometryRevision geometry;
    meshing::MeshStamp       mesh;
    MaterialId               material;
    std::uint64_t            materialRevision;
    AnalysisId               analysis;
    std::uint64_t            analysisRevision;
};
```

**Separate fields, not one mixed hash.** A stale result can then say *which*
dependency moved, and "the material changed" is something a user can act on
while "stale" is not.

**Field-complete equality, by `= default`.** There is no hand-written
comparison to forget to update, so adding a dependency field without comparing
it is not possible.

### Where each field comes from, and why it is separate

| Field | Source | Why it cannot be folded into another |
| --- | --- | --- |
| `geometry` | `meshing::geometryRevision` | A mix of the dependency revision counters of everything the body is built from, plus the active configuration. **A material edit does not move it** — P16-GEOM-001 narrowed it deliberately and tests it — which is exactly why material is its own field |
| `mesh` | `VolumeMesh::mesh().stamp()` | Which mesh *and which generation*. Two meshes of one body have different stamps at identical node counts |
| `material` + `materialRevision` | `requireEffectiveMaterial`, `DocumentObject::revision()` | Which material, and whether it has been edited. The revision moves only on an effective change |
| `analysis` + `analysisRevision` | `findStructuralAnalysis`, `DocumentObject::revision()` | **Covers loads, restraints and solver settings**, because all three live in one `StructuralAnalysisDefinition` |
| `body`, `control` | the validated input | Which geometry and which meshing intent, by document identity |

### One analysis revision, not three counters

The brief asks for "load / restraint revision" and leaves the representation
open, requiring that whichever is chosen be proved. One `analysisRevision`
covers all of it, because loads, restraints and solver settings are all fields
of the same definition on the same document object. Three independent counters
would be three chances to forget one.

Proved by the mechanism that will carry them:
`StructuralData_ANoOpEditDoesNotMoveTheAnalysisRevision` shows that
`setDefinition` moves the revision on an effective change and **not** on a
no-op, which is the precision that keeps a result current across a re-set of
the same value.

### What is deliberately NOT in the stamp

```text
solve duration, residual, backend name, iteration count
```

Diagnostics, not identity. Anything time-dependent would make two identical
solves produce different provenance and break determinism across presets.

## The currentness matrix

| Changed | Expected | Test |
| --- | --- | --- |
| nothing | **CURRENT** | `EveryDependencyAloneCanStaleAResult`, "nothing changed" |
| body | STALE | same, "the body" |
| mesh control | STALE | same, "the mesh control" |
| geometry revision | STALE | same, "the geometry revision" |
| mesh stamp | STALE | same, "the mesh stamp" |
| material | STALE | same, "the material" |
| material revision alone | STALE | same, "the material revision alone" |
| analysis | STALE | same, "the analysis" |
| analysis revision alone — loads, restraints, solver settings | STALE | same, "the analysis revision alone" |
| several at once | STALE, **all reported** | same, "several at once" |
| geometry edited for real | `InputsUnavailable` | `AGeometryEditStalesTheResultThroughTheInputBoundary` |
| `E` edited for real | STALE, mesh **current** | `AMaterialEditStalesTheResultAndNotTheMesh` |
| quality threshold (presentation) | **CURRENT** | `AViewerOnlyChangeDoesNotStaleAResult` |

Each row of the first block mutates **one** field alone. A comparison that
omitted any single field would pass every other row, which is why they are
separate sections rather than one combined check — the mutation test the brief
asks for, built into the structure of the test rather than bolted on.

### Three rows worth stating out loud

**A material edit stales the result and not the mesh.** Measured: after a
210 → 190 GPa edit, `Mesher::currency` still reports `Current` and
`resultCurrency` reports `Stale` with reason `material`. An architecture that
invalidated the mesh would remesh a hundred thousand elements to answer a
question about a number.

**Counts are not identity.** Remeshing the same body with the same settings
gives 9 nodes either way and a different stamp, and the result is refused.

**A threshold edit stales nothing.** It changes how an element is *classified*
and can never change the element, so P16 keeps the mesh current and a result
computed on that mesh stays current too. P17 inherits that precision by
comparing the stamp rather than the control's revision.

## Undo, and what "the same again" means

The brief asks this to be audited rather than invented, and the answer is that
**the rule is not P17's and it differs by source**. Both halves are measured in
P16's own tests:

```text
MESHING INTENT   compared BY VALUE. MeshingCommand_AThresholdEditDatesTheReport
                 edits, then undoes, then asserts currency == Current. So an
                 undone intent edit makes a result current again.
GEOMETRY         compared by a mix of REVISION COUNTERS, which only move
                 forward. MeshingCommand_AGeometryChangeIsStaleForADifferentReason
                 restores the original depth and asserts the mesh is STILL
                 StaleGeometry: "restoring 10 mm produces the original solid
                 under a new stamp".
```

P17 inherits both unchanged rather than defining a third rule:

```text
undo a load, restraint or solver-settings edit   the analysis revision... see below
undo a geometry edit                             result stays STALE until a remesh
undo a mesh-settings edit                        the mesh becomes Current again,
                                                 and a result computed on the
                                                 surviving mesh is current again
```

For P17's own analysis intent the comparison is `analysisRevision`, a
**monotonic document-object revision**. So undoing a load edit leaves the
revision moved and the result stale, and it must be re-solved. That is the
conservative half of P16's own pair, chosen knowingly: the cost of being wrong
this way is one solve that was not strictly needed, and the cost of the other
way is a result presented as describing a model it does not.

Recorded here because `P17-CMD-001` will need it, and because the honest answer
required reading two existing tests rather than reasoning from first
principles.

## Where currentness is decided

One place, and these are the only answers in P17:

```cpp
Result<StructuralResultSource> currentResultSource(document, regenerator, mesher, analysis);
ResultCurrency                 resultCurrency(result, current);
AnalysisState                  analysisState(document, regenerator, mesher, analysis,
                                             lastResult, lastFailure);
```

`currentResultSource` **calls `requireStructuralModel`**, so every gate ADR-036
established is inherited rather than re-asked: geometry eligibility including
the configuration refusal, mesh currency, and a usable material. P17-DATA adds
only what the input boundary has no reason to know — the analysis and the
material's revision.

A GUI that compared node counts, a CLI that compared an `AnalysisId` and a
post-processor that trusted its caller would be three answers that drift, and
the one that drifts towards "current" is the one that ships a wrong number.

## The state machine

Derived, never stored. Every value is a function of the document, the current
mesh state and the result in hand, so there is no cached flag to fall out of
agreement with what it describes — the choice P16 made for `MeshCurrency`.

| State | Conditions | Result present | Result current | Reached from |
| --- | --- | --- | --- | --- |
| `NoAnalysis` | no analysis with that id | — | — | — |
| `InputsUnavailable` | the analysis exists; `requireStructuralModel` refuses | maybe | no | `NoAnalysis` once created; any state on a geometry or mesh change |
| `Ready` | inputs resolve; nothing solved | no | — | `InputsUnavailable` once a mesh is current |
| `SolvedCurrent` | a result whose source matches exactly | yes | yes | `Ready` on a successful solve |
| `SolvedStale` | a result exists; ≥1 dependency moved | yes | no | `SolvedCurrent` on any dependency change |
| `SolveFailed` | the last attempt failed | maybe | no | any state on a failed solve |

Every one is reached by a section of
`StructuralData_TheAnalysisStateMachineReachesEveryStateItDeclares`. There is no
state the code cannot produce — the defect P17-ARCH-001's finding F2 corrected
in the previous milestone, applied here before it could recur.

**`SolveFailed` takes precedence over a surviving stale result**, mirroring
`MeshCurrency::GenerationFailed`: reporting that result as merely stale would
hide that the attempt to replace it failed.

**Two of the brief's suggested states were merged.** `Defined` ("the analysis
exists but prerequisites are not satisfied") and `InputStale` are the same
condition while no loads or restraints exist to be incomplete — there is
nothing yet that distinguishes "not finished being defined" from "was fine and
is now stale". `InputsUnavailable` is that single condition, and
`structuralInputProblem` names which of its seven reasons applies. Splitting
them when `P17-BC-001` makes "no restraints yet" a real state is one enum value
and one branch.

## Determinism

`StructuralData_StateAndStaleReasonsAreDeterministic` evaluates an unchanged
document 16 times and requires the same source, the same state and the same
stale-reason vector each time.

```text
no wall clock         nothing in the stamp is time-dependent
no pointer identity   comparison is by value over IDs and revisions
no unordered iteration  grep for unordered_map/unordered_set in
                        include/bettercad/structural/ and src/structural/: 0
```

`staleReasons` returns a vector in the **enum's** order, never a container's,
so the answer is byte-identical in every preset.
