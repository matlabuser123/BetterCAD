# P18-QUAL-001 - currentness audit and matrix (brief sections 16-18, 60-61)

Row-by-row audit: `audit/AUDIT_A_adr_authority_currentness_dof.md` section A3 (15 rows: 11 COVERED, 4 PARTIAL, 0 GAP).
The matrix below is **executed**, not described: `P18Qualification_EveryDependencyEditStalesExactlyTheObjectsItReaches` and
`P18Qualification_TheTransientEditsAndTheCapacityStaleTheirOwnObjects` run the rows in sequence on one document, each followed by a
fresh run, and ask the production validator (`validateThermalAnalysis` with an older run's derived state) which derived objects the edit
staled. The set is the `ValidationSubject` of each `SourceStale` finding; nothing is read from a stored flag.

## What each link compares

```text
CAD feature  -> GeometryRevision -> P16 MeshStamp {MeshId, generation}
  -> ThermalModel (snapshot, borrowed)  -> ThermalDofMap {stamp, nodeCount}
  -> PreparedThermalLoads / PreparedThermalBoundary {body, stamp, nodeCount, geometry, canonical list BY VALUE}
  -> ThermalSystem {+ mode, material id + revision, loads, convections}
  -> SteadySolution / TransientSolution {+ prescribed temperatures, settings, initial, step, end, output}
  -> ThermalPostResult, ThermalBalanceResult {the same source record}
  -> ThermalValidationReport {ThermalResultSource incl. analysis id + analysis REVISION, policy fingerprint}
  -> VIZ display state (recomputed from the above on every call; no state of its own)
```

Every derived object stores its source as a **value** (no pointer, address, timestamp or byte hash); equality is `= default`, so a new
field cannot be added without being compared; `staleReasons` names the field that moved. Counts are only an additional guard next to
the stamp (`a stamp identifies the builder, not the snapshot`).

## The matrix, steady (document `ThermalSurfaceFlux`, six edits, seven runs)

| # | edit | stale after the edit | NOT stale |
|---|---|---|---|
| 1 | remesh (a new mesh object, same geometry) | DOF map, loads, boundary, system, solution, POST, BALANCE | - |
| 2 | conductivity (P15 material) | system, solution, POST, BALANCE | DOF map, loads, boundary; the mesh stays Current |
| 3 | surface heat flux (command layer, real history) | loads, system, solution, POST, BALANCE | DOF map, boundary |
| 4 | prescribed temperature | boundary, solution, POST, BALANCE | DOF map, loads, **system** |
| 5 | undo of 4 (see "two regimes") | the edited run (computed for the edited content) | the pre-edit run's content-keyed objects |
| 6 | solver settings (residual tolerance halved) | solution, POST, BALANCE | DOF map, loads, boundary, system |

Row 4: the assembled system is **not** stale after a prescribed-temperature edit. Base assembly performs no Dirichlet elimination
(ADR-049), so K and Q do not depend on a held value; the constraint map and the solve do. A convection edit does enter K and Q and
does stale the system (transient case, below). The first version of this test expected the system to go stale, the run showed
`{boundary, solution, POST, BALANCE}`, and the expectation was corrected only after confirming the design in the code - not to make
the test pass.

Each row also asserts that a fresh run is accepted and current (`judge(run, fresh).accepted()`), that the answer differs from the
previous run's when the physics changed (`fingerprint != previous`) and is bit-identical where it must be (a remesh, a tolerance
change: same temperatures, same bits), and that the first result never becomes current again however many times the document returns
to numbers it once had.

## The matrix, transient (document `ThermalTransientSlab`)

| edit | stale after the edit |
|---|---|
| initial temperature | solution, POST, BALANCE |
| time step 50 s -> 25 s | solution, POST, BALANCE (the history now has 21 states) |
| prescribed temperature | boundary, solution, POST, BALANCE |
| convection coefficient | boundary, **system**, solution, POST, BALANCE |
| specific heat, density (P15) | system, solution, POST, BALANCE |

## Failure never makes a result current

`P18Qualification_AFailedRegenerationNeverYieldsACurrentResult`: a regeneration that fails after a result was current (the extrusion
depth set to zero, which the parameter accepts and the extrusion refuses - a `REQUIRE`, so the failure branch cannot silently stop
being exercised). The run is refused at the model stage with nothing published past it (no solution, no POST, no balance), the old
result is stale. The model is restored, regenerated and remeshed: the new run is accepted and reproduces the original temperatures to
the last bit, and **the old result stays stale** because the mesh object moved (`staleSubjects == everything`).

## The two regimes (the one place the brief's wording is not uniformly true)

The brief: "no numerical-equality resurrection - edit, result stale, undo or recreate equal values: the source-generation policy
decides." The code, recorded by ADR-055 and pinned by tests, has **two regimes**:

1. **Revision-keyed**: `ThermalResultSource` and `ThermalValidationReport` carry the analysis object's **revision**. `Document::modifyObject`
   bumps it on every effective change including undo and redo, so an undo moves the revision forward and these **never revive**.
   Nothing is *accepted* on the strength of an old verdict.
2. **Content-keyed**: loads, boundary, system, solution, POST and BALANCE compare the recorded canonical intent **by value** (plus mesh
   stamp, geometry, material id and revision) and carry no analysis identity. After an undo - or after typing the old value back - they
   describe the document again. This is sound, not a guess: a fresh solve of that content gives the same bits (asserted: `fingerprint(r5) ==
   fingerprint(r3)`), and what is never allowed is the reverse - a run computed for the *edited* content is not current under the restored one.

Consequences recorded, not hidden: after an undo, VIZ shows a Current result beside a Stale validation badge (ADR-055); results of one
analysis are "current" for another analysis with an equal definition on the same mesh and material (no analysis identity in the chain
sources); and the comment in `ThermalValidationMappedTests.cpp` that currentness "is identity, not 'the same values again'" describes
the report only. This is an **owner decision**, `D1` in `FINDINGS.md` and `LIMITATIONS.md`. The final-gate test pins the behaviour so it
cannot change silently, including the manual edit-back path that no earlier test covered
(`P18Qualification_AManualEditBackFollowsTheSameTwoRegimesAsUndo`).

## Failure modes the matrix rules out, with the test that would catch each

| the failure | caught by |
|---|---|
| a remesh with the same node and element counts, or even the same ids, leaves an old result current | `ThermalViz_SameCountsNewMeshStillStale`, `ThermalViz_SameIdsNewMeshStillStale`, `ThermalRun_ARemeshWithTheSameCountsAndIdsStillMakesTheOldRunStale`, `ThermalReference_RM10_SameCountsAndSameIdsStillStale`, the `Remesh...` case of every stage; mutation QP13 (a stamp not compared) |
| a material edit leaves an old result current | `ThermalMaterial_*`, `ThermalAssembly_RejectsStaleMaterialSource`; mutation QP18 |
| a load or boundary edit leaves an old result current | row 3 and 4 above; `ThermalLoad_EditStalesThermalResult`, `ThermalBC_EditStalesThermalResult`; mutation QP25 |
| a failed solve resurrects an old result | `ThermalSolve_FailedResolveDoesNotCreateACurrentResult`, `ThermalViz_FailedRecomputeDoesNotMakeOldResultCurrent`, the failed-regeneration case |
| the validator reports a stale object as fine | mutation QP12 (the stale branch removed) |
