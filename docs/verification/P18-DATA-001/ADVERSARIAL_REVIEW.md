# P18-DATA-001 — adversarial review

```text
SUBJECT:   an attempt to disprove the thermal data model -- identity,
           canonical/derived line, provenance, currentness, lifecycle --
           before any thermal physics is built on it
QUESTIONS: 34 from the phase plan's final list, read against what exists
           today, plus 8 of the reviewer's own
MUTATIONS: 24 run-time probes on the production tree, plus the compile-time
           ones the compile-fail group runs on every build
FINDINGS:  2 -- 1 test-method defect fixed before the freeze, 1 recorded
           inheritance
PRODUCTION DEFECTS: 0
GATE-BLOCKING: 0
```

Each answer below was obtained by reading the code or running something.
Where the answer was "yes, it could", the fix came before the box was ticked.
Where a question is about a type that does not exist yet, the answer says
which milestone owns it and what THIS milestone fixes about it.

## Findings

### F1 — the size-independence test assumed a flat block could be refined (TEST METHOD, fixed)

The first draft of `ThermalData_AResultDoesNotCopyTheMeshAndItsSizeIsIndependentOfTheMesh`
put a second mesh control with a 6 mm global target on the same 40 x 30 x 20
block and required more nodes than the 9 of the default mesh. It got 9. P16
had already measured why (`SizeGlobal_CannotBeFinerThanTheBoundaryItMustConformTo`):
OCCT triangulates a planar face with two triangles whatever the deflection,
so a block's boundary is irreducibly 12 triangles and a volume target has
nowhere to act; the fixture that can show sizing is a cylinder. The test now
adds a 6 mm-radius, 20 mm cylinder to the same document and meshes it at a
3 mm target.

```text
measured   coarse block 9 nodes; cylinder 178 nodes, 741 tetrahedra;
           sizeof(ThermalResult) = 216 bytes either way
```

Nothing in production was wrong; the assumption in the test was, and the
honest record is that P16's own evidence answered it. The build also caught a
test authoring slip (`2_minute` is not a literal; `2.0 * units::minute` is),
corrected in the same pass.

### F2 — one effective material per document (RECORDED, inherited)

`currentResultSource` reads the material through
`features::requireEffectiveMaterial(document)`, exactly as P17 does: P15's
model is one effective material per document, and two bodies in one document
share it. The size-independence test has two analysed bodies and both carry
the same `material` and `materialRevision` in their sources, which is correct
under that model. A per-body material assignment would be a P15 change and is
not P18's to make; recorded so a later reader does not take the shared field
for a provenance bug.

## The brief's mutation probes, and what kills each

| Attack | Expected protection | Observed | Regression test | PASS |
| --- | --- | --- | --- | --- |
| alias `ThermalLoadId` to `ThermalAnalysisId` (or to `LoadId`) | the compiler | `static_assert(!is_same_v<...>)` fails; 7 compile-fail cases pass only because the types differ | `StrongIdsAreDistinct`; `compile_fail.thermalids.*-id-as-*` | ✓ |
| a `NodeId` inside a load record | the compiler | `SurfaceHeatFluxLoad{node, flux}` refused; `sizeof` mirror would break | `a-heat-flux-load-does-not-target-a-node`; `NoMeshOrSolverHandleCanActAsAThermalIdentity` | ✓ |
| a temperature array inside the definition | the compiler | no member; size mirror breaks; not constructible | `the-definition-has-no-temperatures`; `TheDefinitionHoldsOnlyIntent` | ✓ |
| omit the mesh from provenance comparison | truth table | M01 | `SourceFingerprintIsSensitiveToEveryField` "mesh generation alone"; `SameIdsNewMeshGenerationIsStale` | see below |
| omit the material revision from the source | document-edit test | M02 | `AMaterialEditStalesTheResultAndNotTheMesh`; `TheSourceIsPopulated...` | see below |
| omit the mode from the source | document-edit test | M03 | `TheSourceIsPopulated...`; `EveryDefinitionEditMakesTheResultStale` "mode" | see below |
| omit BC / dt / initial-condition edits from provenance (analysis revision) | document-edit test | M04 | `EveryDefinitionEditMakesTheResultStale`, every section | see below |
| failed solve reported as current | state test | M09, M11 | `FailedAttemptIsNotCurrent`; `FailedAttemptDoesNotResurrectStaleResult` | see below |
| failure discards or resurrects the stale result | lifecycle test | M10 | `FailedAttemptDoesNotResurrectStaleResult` | see below |
| size-based currentness | same-IDs remesh test | M06, M07 | `SameIdsNewMeshGenerationIsStale`; `CurrentnessComparesSourceIdentityNeverArraysOrSizes` | see below |
| byte hashing of provenance | by construction | no hash exists; `ThermalResultSource` is a trivially copyable tuple of IDs, revisions and an enum with `sizeof` asserted | `DeterministicCanonicalOrdering` | ✓ |
| unordered iteration in the roster | ordering test | M23 | `DeterministicCanonicalOrdering`; `AddUndoRedoRestoresTheAnalysisExactly` | see below |
| Celsius anywhere | catalog scan | M24 | `TemperaturesAreKelvinAndNoCelsiusUnitExists` | see below |
| swapped gradient / flux components | frozen order | M19 | `VectorComponentOrderIsFrozen` | see below |
| approximate equality in currentness | by construction | no floating comparison exists in `resultCurrency`, `staleReasons` or `analysisState`; every field is integral or an enum | `SourceFingerprintIsSensitiveToEveryField` | ✓ |

### The run-time probes, as run

Each probe is one textual mutation of a production source, a rebuild of the
debug-ext test binary, one run of `bettercad_tests "[thermal][data]"` (plus
`[units][catalog]` for the Celsius probes), a verdict, and a byte-identical
restore. Scripts and per-probe logs are in the session scratchpad; the tree
was verified free of every mutation before the pre-freeze run.

| # | Mutation | File | Verdict | Killed by |
| --- | --- | --- | --- | --- |
| M01 | `staleReasons` omits the mesh comparison | ThermalAnalysisObject.cpp | **KILLED** 3 cases / 4 assertions | truth table "mesh generation alone", "mesh identity alone"; `SameIdsNewMeshGenerationIsStale` |
| M02 | source `materialRevision = 0` | ThermalResult.cpp | **KILLED** 2 / 6 | `AMaterialEditStalesTheResultAndNotTheMesh`; `TheSourceIsPopulated...` |
| M03 | source `mode` always Steady | ThermalResult.cpp | **KILLED** 2 / 3 | `TheSourceIsPopulated...`; `EveryDefinitionEditMakesTheResultStale` "mode" |
| M04 | source `analysisRevision = 1` | ThermalResult.cpp | **KILLED** 2 / 34 | every section of `EveryDefinitionEditMakesTheResultStale` |
| M05 | `staleReasons` omits the mode comparison | ThermalAnalysisObject.cpp | **KILLED** 3 / 5 | "mode alone"; "several at once"; determinism |
| M06 | currency by analysis id only | ThermalResult.cpp | **KILLED** 7 / 33 | `SameIdsNewMeshGenerationIsStale`, `SameContentNewSourceGenerationIsStale`, every stale-edit case |
| M07 | `describes()` compares node count | ThermalResult.cpp | **KILLED** 1 / 2 | `SameIdsNewMeshGenerationIsStale` |
| M08 | `create` skips the stamp check | ThermalResult.cpp | **KILLED** 1 / 1 | `AResultMustMatchItsMeshExactly` "the source must name this mesh" |
| M09 | failure does not take precedence (block deleted) | ThermalResult.cpp | VOID, did not compile: unused parameter under -Werror | — |
| M09b | same, with the parameter kept used | ThermalResult.cpp | **KILLED** 3 / 6 | `FailedAttemptIsNotCurrent`; `FailedAttemptDoesNotResurrectStaleResult`; state machine |
| M10 | `recordFailure` discards the result | ThermalResult.cpp | **KILLED** 1 / 1 | `FailedAttemptDoesNotResurrectStaleResult` |
| M11 | `recordFailure` recorded as success | ThermalResult.cpp | **KILLED** 2 / 3 | `FailedAttemptIsNotCurrent`; `...DoesNotResurrect...` |
| M12 | steady accepts an initial condition | ThermalAnalysisObject.cpp | **KILLED** 1 / 1 | `TheDefinitionIsValidatedPerMode` |
| M13 | transient needs no initial condition | ThermalAnalysisObject.cpp | **KILLED** 1 / 2 | same |
| M14 | a no-op edit moves the revision | ThermalAnalysisObject.cpp | **KILLED** 1 / 3 | `ANoOpEditDoesNotMoveTheRevision` |
| M15 | duplicate load ids accepted | ThermalAnalysisObject.cpp | **KILLED** 1 / 1 | `TheDefinitionIsValidatedPerMode` |
| M16 | `dt = 0` accepted | ThermalData.cpp | **KILLED** 2 / 3 | `TransientSettingsRequire...`; per-mode validation |
| M17 | `h = 0` accepted | ThermalData.cpp | **KILLED** 1 / 1 | `BoundaryConditionsAreValidatedOnTheirOwnTerms` |
| M18 | temperatures below 0 K accepted | ThermalData.hpp | **KILLED** 5 / 6 | initial, prescribed, ambient, result, `isAbsoluteTemperature` cases |
| M19 | flux `component()` swaps Y and Z | ThermalData.hpp | **KILLED** 1 / 2 | `VectorComponentOrderIsFrozen` |
| M20 | steady result accepts a stored-energy rate | ThermalResult.cpp | **KILLED** 1 / 1 | `AResultMustMatchItsMeshExactly` "steady ... no stored-energy rate" |
| M21 | `normalizedImbalance()` returns 0 for no heat | ThermalData.hpp | **KILLED** 1 / 1 | `HeatBalanceSignConventionAndNormalization` |
| M22 | roster sort removed | ThermalAnalysisObject.cpp | **SURVIVED -- equivalent** | `Document` stores objects in `std::map<ObjectId, ...>`, so iteration is ascending by construction; the sort is defence against a container change and no test can tell them apart |
| M23 | roster through `unordered_set` | ThermalAnalysisObject.cpp | VOID, did not compile: missing include | — |
| M23c | same, with the include | ThermalAnalysisObject.cpp | **KILLED** 2 / 4 | `DeterministicCanonicalOrdering`; `AddUndoRedoRestoresTheAnalysisExactly` (the hashed order was not ascending) |
| M24 | a `degC` Unit constant, no catalog entry | Units.hpp | **SURVIVED -- recorded** | nothing enumerates a namespace's constants; a non-catalog unit cannot reach a document, parameter or file (those resolve by catalog symbol); the exposure is code that spells it, see README limitations |
| M24b | a `degC` unit AND its catalog entry | Units.hpp + UnitCatalog.cpp | **KILLED** 2 / 2 | `TemperaturesAreKelvinAndNoCelsiusUnitExists`; the catalog count |

```text
probes with a verdict   25
killed                  23
survived, equivalent     1   M22
survived, recorded       1   M24, with its realistic form M24b killed
void (rerun)             2   M09 -> M09b, M23 -> M23c
```

## The phase plan's questions, at this gate

**Can P18 solve a stale mesh? geometry that failed regeneration?** Not
through this model: `currentResultSource` calls `requireThermalModel`, so a
stale or failed mesh has no current source and `analysisState` answers
`InputsUnavailable` with the old result unoffered
(`AGeometryEditMakesInputsUnavailable`, state machine "defined"). No function
in the data model takes a `VolumeMesh` except `ThermalResult::create` and the
two readers, and each compares the stamp.

**Can a conductivity edit leave an old result current?** No: `k` 45 -> 50
W/(m K) moves the material revision, the result is stale for reason
`material`, and the mesh stays current (`AMaterialEditStalesTheResultAndNotTheMesh`).
**Density / cp edits?** The same field, the same revision, the same path;
`setMaterialThermal` and the mechanical setter both move `Material::revision()`.
Steady results are staled by a `rho`/`cp` edit too, conservatively -- the
choice P18-ARCH-001 recorded for this milestone to make knowingly, and it is
made: one material revision, no per-property split.

**Can a thermal-load edit leave an old result current? a thermal-BC edit? a
time-step change?** No: every one is a field of the one definition on the one
document object, every effective `setDefinition` moves its revision, and
`EveryDefinitionEditMakesTheResultStale` edits the real document one field at
a time -- flux load added, volumetric source added, prescribed temperature
added, convection added, a load's value changed, solver tolerance, mode,
dt, end time, output policy, initial temperature -- and finds the result
stale and the mesh current after each.

**Can NodeId become permanent thermal-load identity? ElementId permanent
thermal intent?** No: the compiler refuses both in nine compile-fail cases,
the canonical records are exactly a `FaceName` and their values by `sizeof`,
and the definition cannot be constructed from a `MeshStamp`.

**Can a thermal boundary silently move to another CAD face? an unresolved BC
bind to a nearby face?** No resolution code exists yet (P18-LOAD-001,
P18-BC-001). What this milestone fixes is that the record holds a `FaceName`
and nothing resolved, so there is nothing stale to rebind FROM.

**Can an inverted or zero-volume Tet enter thermal assembly? Can K lose
symmetry? duplicate contributions overwrite? a free null mode appear solved?
the solver return success with a large residual? heat balance fail while the
solve is accepted? flux or generation fail to preserve the total? convection
use the wrong sign? a rigid transform change T or |q|?** No element, matrix,
load integration, solver or balance computation exists. What this milestone
fixes: the sign convention every one of those will be measured against (heat
into the body positive; `q = -k grad T`), the balance's terms and its derived
imbalance, the absolute-temperature rule a result must pass, and the
`ThermalSolverSettings` tolerances a solve will be judged by.

**Can thermal conductivity units be confused?** Less than before: the four
field and boundary quantities now exist as dimensioned types, Fourier's law
and convection are dimensionally checked at compile time
(`UnitsAreExplicitSi`), and `a-conductivity-is-not-a-convection-coefficient`
refuses W/(m K) where W/(m^2 K) is wanted.

**Can GUI visualisation recalculate heat flux? visualisation units mutate
model coordinates?** Nothing downstream exists. What is fixed: a result's
arrays are spans of const; `|q|` is derived by one function on the type; a
viewer that computed its own would be visibly a second implementation.

**Can undo restore thermal matrices or results instead of analysis intent?**
No: the history stores commands, `AddObjectCommand` clones the object, and a
`ThermalAnalysis` holds only a definition -- there is no result member to
clone (`AddUndoRedoRestoresTheAnalysisExactly`; `CANONICAL_DERIVED.md`).

**Can save/load trust old temperature or heat-flux arrays? Can
save-before-solve and save-after-solve differ?** Not yet possible in either
direction: `documentToJson` refuses a document holding a thermal analysis by
name (`SavingADocumentWithAThermalAnalysisIsRefusedUntilPersistenceExists`).
P18-PERSIST-001 must preserve both halves -- the definition is written, no
result is -- and inherits P16's and P17's tests for the second.

**Can the CLI implement separate Tet4 thermal logic? return zero on a
singular solve? Can thresholds differ between GUI and CLI?** Nothing
downstream exists; the authority matrix is P18-ARCH-001's and unchanged.

**Can transient time-step changes fail to stale the old result?** No:
measured, dt 0.5 -> 0.25 s stales with reason `analysis`.

**Can stale binaries produce qualification evidence? a zero-test filter pass?
source change after final qualification?** Answered by the harness, unchanged
from P17: clean builds per preset, `ZeroMatchGuard` with two new filters at
asserted minimums (36 >= 20, 24 >= 12), the eight-path fingerprint at both
ends of the run and compared with the commit.

## The reviewer's own eight

**1. Can a steady result be told from a transient sample without the
document?** Yes: `source().mode`, and `create` enforces that a steady result
has no time and no stored-energy rate while a transient one has both. A
transient sample accidentally stamped `Steady` is refused for carrying a
time.

**2. Is "same IDs, new generation" really the case the remesh produces?**
Measured: 9 nodes with identical `NodeId` values and 12 tetrahedra either
way, different stamp, result stale, `describes` false, `temperatureOf`
refused. The test asserts the IDs are equal before it asserts the stamps
differ, so it cannot pass by the IDs happening to change.

**3. Could a mutation to `currentResultSource` survive the struct-level
truth table?** That was P17-DATA-001's F1, and it is closed here from the
first draft: `TheSourceIsPopulatedFromTheDocumentAndNotDefaulted` compares
every field to the document's own values, and `EveryDefinitionEditMakesTheResultStale`
edits the real document per field. M02, M03 and M04 are the probes.

**4. Does strict mode-completeness cost the user an extra edit?** No.
`setDefinition` replaces the whole definition, so steady -> transient is one
edit that supplies the initial condition and the time settings together, and
transient -> steady is one edit that drops them. What it buys: no inert field
whose edit stales a result for nothing, no field the solver has to default.

**5. Is 0 K accepted on purpose?** Yes, and it is written down once
(`isAbsoluteTemperature`): the limit of the scale, mathematically ordinary in
linear conduction, and a threshold above it would have no physical basis.
Below it is refused everywhere, including in results.

**6. Why refuse a result with a sub-zero temperature rather than leave it to
validation?** Because a field with a node below 0 K is not a solution of a
problem whose every datum was absolute; refusing turns a wrong solve into a
reported `SolveFailed` rather than a displayed temperature. The cost -- a
mathematically valid linear solution with a nonphysical node is refused with
a diagnostic naming the node -- is the conservative side, and P18-VALID-001
can only tighten it.

**7. Why is `ThermalResultSlot` in a DATA milestone?** Because the brief
asks the lifecycle -- failed with no result, failed while a stale result
remains, failure never resurrecting -- to be proved now, and proving it needs
the holder with its transitions. Forty lines, no document access, no cached
currentness; P18-SOLVE-001 owns one per analysis.

**8. Does the `[thermal][data]` tag collide with anything?** No: 36
discovered, 36 are this file's; the `[thermal]` collision P18-ARCH-001
recorded (F2) does not reach the compound filter, and the zero-match guard
keys on the ctest prefix `unit\.ThermalData` anyway.

## Verdict

```text
adversarial review     PASS -- 0 production defects; 1 test-method defect fixed
                       before the freeze; 1 inheritance recorded; every
                       forward question named to the milestone that owns it
```
