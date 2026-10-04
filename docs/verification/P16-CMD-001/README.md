# P16-CMD-001 — Commands / Undo / Redo

```text
STATUS:   PASS
DATE:     2026-10-05
GATE:     commands mutate canonical meshing intent + undo exact + redo exact
          + failed mutations atomic + generated mesh excluded as authority
```

## Baseline

```text
HEAD:          0b13e20de9578214f333da750570c3997daf2afc
TREE:          e5d271a3d9bb7fad32f1d4b08fd20b5e1845467b
origin/main:   0b13e20de9578214f333da750570c3997daf2afc  (equal)
log:           0b13e20 BetterCAD: add engineering mesh inspection UI
working tree:  clean at start of milestone
compiler:      GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, WinLibs r4)
cmake:         4.4.1, Ninja
build root:    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive, -ext presets)
```

## Prerequisites

Every predecessor this milestone depends on is qualified, checked by reading
its evidence rather than its checkbox:

```text
P16-ARCH-001      PASS     ADR-030 to ADR-033
P16-DATA-001      PASS     mesh data model, identity, units
P16-GEOM-001      PASS     geometry preparation, GeometryRevision
P16-SURF-001      PASS     engineering surface mesh
P16-VOL-001       PASS     Tet4 volume mesh
P16-SIZE-001      PASS     global / local sizing, SizingValidationReport
P16-QUALITY-001   PASS     quality metrics, QualityThresholds
P16-MAP-001       PASS     geometry <-> mesh correspondence, NamedBoundarySet
P16-VIZ-001       PASS     mesh visualisation / inspection
INFRA-NETGEN-001  PASS     Netgen toolchain
INFRA-VIEWER-001  PASS     OCCT visualization toolchain
```

So the milestone is **not blocked**.

## Scope

What this milestone builds, and the one thing it deliberately does not.

```text
IN     MeshControl, the canonical meshing intent, as a document object
IN     ten commands over it, on the existing CommandHistory
IN     Mesher, the derived meshes, keyed by control, with a currency contract
IN     the tests, the invalidation matrix and the payload evidence

OUT    persistence of MeshControl                  -> P16-PERSIST-001
OUT    CLI meshing workflows                       -> P16-CLI-001
OUT    command-history persistence                 -> BetterCAD persists no
                                                      undo stack; not this
                                                      milestone's to introduce
OUT    wiring the GUI to a MeshControl             -> needs GUI undo, which
                                                      TODO.md does not
                                                      authorize here; see
                                                      ADVERSARIAL_REVIEW A3
```

## The audit that decided the milestone was buildable

Written before any production code, in
[COMMAND_AUDIT.md](COMMAND_AUDIT.md). Two findings mattered.

**A general command framework exists and P16 must use it.** `Command` and
`CommandHistory` already provide exactly the contract this milestone needs,
including "redo reproduces the state execute produced, INCLUDING THE SAME
IDs". So there is no `MeshUndoManager` here, and the create and delete
commands are thin wrappers over the generic `AddObjectCommand` and
`DeleteObjectCommand`.

**P16's canonical meshing intent did not exist.** `MeshSizingControls`,
`QualityThresholds` and `NamedBoundarySet` were value types passed as
arguments to `volumeMeshFor` — nothing in the document owned them, so there
was nothing for a command to mutate.

That could have been a stop condition, as the absent viewport was for
P16-VIZ-001. It is not, because ADR-030 already specifies the object —

> "A `MeshControl` is a document object ... created and edited only through
> commands, and it is persisted."

— and P16-SIZE-001 explicitly deferred building it to this milestone. So
`MeshControl` is in scope here rather than missing.

## Canonical meshing intent

```cpp
struct MeshControlDefinition {
    ObjectId body{};                              // what is meshed
    VolumeMeshControls mesh{};                     // surface + sizing REQUEST
    QualityThresholds quality{};                   // the policy, not a report
    std::vector<NamedBoundarySet> boundarySets{};  // CAD faces, not facets
};
```

`MeshControl` is a `DocumentObject` holding one of these. It is validated
through the owning validators — `validate(MeshSizingControls)`,
`validate(QualityThresholds)`, `validate(NamedBoundarySet)` — so the command
layer restates none of P16-SIZE's or P16-QUALITY's rules, and carries the
sizing report's own first-issue message through rather than flattening it.

`dependencies()` returns the body, so the document's dependency graph knows a
control depends on what it meshes.

## The command authority boundary

In full in [HISTORY_CONTRACT.md](HISTORY_CONTRACT.md). In short: the history
holds a body handle, unit-bearing quantities, stable typed IDs and semantic
face references. It holds no nodes, elements, connectivity, facet identities,
quality report, mapping, render buffer or backend handle — and **cannot**, as
the compile-fail cases below show.

## The commands

| Command | Execute | Undo | Redo | Failed atomic | Geometry ref preserved | Invalidation |
| --- | --- | --- | --- | --- | --- | --- |
| `CreateMeshControlCommand` | creates, validated | removes, keeps the object | re-inserts, **same ID** | yes | n/a | n/a |
| `DeleteMeshControlCommand` | removes | restores ID, name, definition | removes again | yes | yes | mesh forgotten by owner |
| `SetMeshControlDefinitionCommand` | whole definition | before-state | after-state | yes | yes | per field (see matrix) |
| `SetGlobalMeshSizeCommand` | target or default | before-state | after-state | yes | n/a | StaleIntent |
| `AddLocalMeshSizingCommand` | insert by `FaceName` | before-state | after-state | yes | yes | StaleIntent |
| `EditLocalMeshSizingCommand` | change size on a face | before-state | after-state | yes | yes | StaleIntent |
| `RemoveLocalMeshSizingCommand` | remove by `FaceName` | before-state | after-state | yes | yes | StaleIntent |
| `AddBoundarySetCommand` | insert by `BoundarySetId` | before-state | after-state | yes | yes | none |
| `EditBoundarySetCommand` | name and selection, same ID | before-state | after-state | yes | yes | none |
| `RemoveBoundarySetCommand` | remove by ID | before-state | after-state | yes | yes | none |

**A create settings command is implemented, not N/A**: a document has no
meshing control until one is created, and a control is per body, so creation
is a user action with its own undo step.

**Every edit carries a before/after pair of one small value** rather than
inverting fields. Inverting is a list of things to forget, and it grows every
time the definition gains a field; a pair of definitions is proportional to
the number of controls and measured at 431 B each.

**A local control's identity is its `FaceName`**, which is P16-SIZE-001's
contract and not a choice made here: it refuses two controls on one face and
declares the stored order meaningless. So there is no synthetic ID to mint, no
way for a redo to allocate one differently, and no way for an undone add to
collide with a later one.

## Framework questions the brief asks, answered by reading

**Command merging / coalescing.** The framework has none: there is no `merge`,
`coalesce` or `combine` anywhere in `Command.hpp` or `CommandHistory.cpp`. So a
slider dragged 10 → 9 → 8 → 7 mm would record four steps, and this milestone
introduces no special-case coalescing — the brief forbids inventing one where
the standard infrastructure does not support it.

**Compound commands.** No compound-command type exists either. Where a dialog
edits several fields as one user action,
`SetMeshControlDefinitionCommand` carries the whole definition: one command,
one undo step. That is the material precedent, and it is why no edit here
inverts fields individually.

**Command history persistence.** BetterCAD persists no undo stack — a `.bcad`
file holds document objects, and a `CommandHistory` is constructed empty beside
a loaded document. Nothing here changes that.

**Headless use.** The commands are in `src/meshing/`, layer 4, with no Qt and
no renderer dependency, so P16-CLI-001 can drive exactly the same types. There
is no GUI-only path to canonical meshing intent.

**Threading.** The history holds values, not task handles or futures, so a
later asynchronous mesher changes nothing about it: `Mesher::generate` is the
only thing that would move off the calling thread, and it is not a command.

## Mesh invalidation

The matrix, with the two rows that required a decision, is in
[INVALIDATION_MATRIX.md](INVALIDATION_MATRIX.md).

Invalidation originates from canonical intent, in the core. There is no
`markMeshStale()` and no GUI flag: `Mesher` holds, beside each mesh, the
body, the `VolumeMeshControls`, the `QualityThresholds` and the
`GeometryRevision` it was built from, and `currency` answers by comparison.

```text
NoMesh | Current | StaleIntent | StaleGeometry | GenerationFailed
```

`VolumeMeshControls` is compared **by value, not by the control's revision**,
and that is the whole of the milestone's invalidation precision: a revision
bumps for any effective edit, including a boundary-set rename, which cannot
change a single tetrahedron.

## Tests

```text
tests/meshing/MeshControlTests.cpp        8 cases   [meshcontrol]
tests/meshing/MeshingCommandTests.cpp    33 cases   [meshcmd]
tests/compile_fail/MeshingCommandMisuse.cpp
                                         13 cases + a control that must compile

targeted run:  41 cases, 1390 assertions, 0 failures   (debug-ext)
repeats:       ctest --repeat until-fail:5 over all 41, 0 failures, 11.92 s
compile_fail:  13 of 13 pass (each must fail to compile with the expected
               GCC diagnostic; the control must compile)
```

The brief's §87 list, against what is here:

| Brief's name | Test |
| --- | --- |
| `CreateSettings.ExecuteUndoRedo` | `..._CreateExecutesUndoesAndRedoesWithTheSameIdentity` |
| `GlobalSize.ExecuteUndoRedo` | `..._GlobalSizeIsExactThroughUndoAndRedo` |
| `GlobalSize.InvalidAtomic` | `..._AnInvalidSizeFailsBeforeTouchingAnything` |
| `GlobalSize.NoOp` | `..._ANoOpEditCostsNothingButAHistoryEntry`, `..._AUnitEquivalentEditChangesNothing` |
| `LocalSize.Add/Edit/RemoveUndoRedo` | `..._LocalSizingAddEditRemoveAreExactAndKeyedByFace` |
| `LocalSize.GeometryReferencePreserved` | `..._AGeometryReferenceSurvivesUndoAndRedoUnchanged` |
| `LocalSize.UnresolvedReferencePreserved` | `..._AnUnresolvedFaceReferenceSurvivesUndoAndRedoUnrebound` |
| `BoundarySet.Add/Edit/RemoveUndoRedo` | `..._BoundarySetAddEditRemoveKeepItsIdentity` |
| `Command.FailureAtomic` | `..._AnInvalidSizeFailsBeforeTouchingAnything`, `..._AFailedGenerationLeavesTheIntentAndTheHistoryIntact`, `..._AFailedRegenerationKeepsTheMeshItCouldNotReplace` |
| `Command.RedoInvalidation` | `..._ANewCommandDiscardsTheRedoBranch` |
| `Command.MeshInvalidation` | `..._ASizeEditMakesTheHeldMeshStaleAndNothingElse`, `..._ALocalSizeEditInvalidatesAndARenameDoesNot`, `..._AThresholdEditDatesTheReportAndNotTheMesh`, `..._AGeometryChangeIsStaleForADifferentReason`, `..._DeletingAControlLeavesNoMeshBehindIt` |
| `Command.RecomputeAfterUndoRedo` | `..._RemeshingAfterAnUndoRedoChainFollowsTheRestoredIntent` |
| `Command.NoMeshPayload` | `..._AHistoryStepCostsTheIntentAndNotTheMesh`, `..._NoCommandIsLargeEnoughToHoldAMesh`, `..._ALongHistoryStaysProportionalToTheEdits`, and the 13 compile-fail cases |
| `Command.MultiStepUndoRedo` | `..._AMultiStepSequenceUndoesAndRedoesExactly` |
| `Command.Deterministic` | `..._TheSameSequenceGivesTheSameCanonicalState` |

Beyond the list: the mesh-originated command path and the interior-face
refusal (`..._AMeshOriginatedEditStoresTheSameReferenceAsACadOriginatedOne`,
`..._AnInteriorMeshFaceCannotNameASizingTarget`), stored-order restoration,
the wrong-kind-of-ID refusal, two controls on one body, ID non-reuse after an
undone create, and a history that outlives three generated meshes.

## The multi-step fingerprint sequence

The brief's mandatory sequence. The fingerprint reads **only** canonical
intent — body, global target, deflections, ordered local controls, ordered
boundary sets, threshold policy — and no generated anything, so a match after
an undo cannot be a cached mesh matching.

```text
S0  create, global 10 mm
S1  set global 7 mm
S2  add local on the end cap, 3 mm
S3  add local on a side face, 2 mm
S4  edit the end cap's control to 1 mm
S5  remove the side face's control

undo x5   ->  S4 S3 S2 S1 S0, each compared exactly
            undoCount() == 1 afterwards: the fixture's own create, and
            nothing else -- so the five edits consumed exactly five entries
redo x5   ->  S1 S2 S3 S4 S5, each compared exactly
            canRedo() == false afterwards
```

Every adjacent pair is additionally required to **differ**, or the sequence
would not be testing five distinct edits.

## Redo invalidation

```text
S0   create, global 10 mm
A    set global 8 mm          -> S1
B    add local on the end cap -> S2
undo B                        -> S1        canRedo() == true
C    set global 6 mm          -> S3

redo B available?   NO        canRedo() == false, redoCount() == 0
final state         S3        and S3 != S2, asserted
then undo x2        S1, S0    the surviving history still walks to the start
```

`MeshingCommand_ANewCommandDiscardsTheRedoBranch`. The `S3 != S2` assertion
matters: without it the test would pass even if C had somehow produced B's
state.

## Failure atomicity

Four invalid global sizes, each checked against four separate things that a
broken implementation could move independently:

```text
before      fingerprint F, undoCount U, redoCount R, document revision V
            (the control already carries a local control, so F is non-trivial)

0 m         InvalidArgument   F unchanged   U unchanged   R unchanged   V unchanged
-1e-3 m     InvalidArgument   F unchanged   U unchanged   R unchanged   V unchanged
NaN         InvalidArgument   F unchanged   U unchanged   R unchanged   V unchanged
infinity    InvalidArgument   F unchanged   U unchanged   R unchanged   V unchanged

local size -1 m   InvalidArgument   F unchanged   U unchanged
```

`MeshingCommand_AnInvalidSizeFailsBeforeTouchingAnything`. That these
assertions bite was confirmed by mutation M5, whose corrected form leaves the
invalid definition in place: the test then fails five times, and its output
shows the fingerprint had become

```text
body=2|global=0|def...        body=2|global=-0.00...
body=2|global=nan|d...        body=2|global=inf|d...
```

so the fingerprint genuinely observes the field the refusal must protect.

## History payload

```text
COMMAND                           STORES                                    MESH?
CreateMeshControlCommand          name + MeshControlDefinition + the         NO
                                  wrapped AddObjectCommand
DeleteMeshControlCommand          MeshControlId + the wrapped
                                  DeleteObjectCommand (which holds the
                                  removed DocumentObject)                    NO
SetGlobalMeshSizeCommand          MeshControlId + optional<Length>
                                  + before/after MeshControlDefinition       NO
Add/Edit/RemoveLocalMeshSizing    MeshControlId + FaceName (+ Length)
                                  + before/after MeshControlDefinition       NO
Add/EditBoundarySetCommand        MeshControlId + NamedBoundarySet
                                  + before/after MeshControlDefinition       NO
RemoveBoundarySetCommand          MeshControlId + BoundarySetId
                                  + before/after MeshControlDefinition       NO
SetMeshControlDefinitionCommand   MeshControlId + MeshControlDefinition
                                  + before/after MeshControlDefinition       NO

Generated VolumeMesh              NOT STORED
Node arrays                       NOT STORED
Element arrays                    NOT STORED
GeometryMeshMap / quality report  NOT STORED
Netgen handles / render buffers   NOT STORED
```

Enforced rather than asserted: `VolumeMesh` has a **private default
constructor**, so it cannot be a member of the aggregate commands copy, and 13
compile-fail cases pin the absences — including that the mesher hands out only
a `const VolumeMesh*`, so a caller cannot take one and change it.

## Regeneration after undo and redo

```text
S1  global 10 mm                 generate -> M1   504 elements   Current
edit to local 2 mm on the cap    M1 -> StaleIntent (M1 untouched, still 504)
                                 generate -> M2   more than 504  Current
undo  -> canonical S1            M2 -> Current again, WITHOUT a remesh:
                                 currency is a comparison, not a stored flag
                                 generate -> M3   504 elements, == M1
redo  -> canonical S2            generate -> M4   == M2's count
```

`MeshingCommand_RemeshingAfterAnUndoRedoChainFollowsTheRestoredIntent`, which
additionally **requires** the refined mesh to be strictly larger before
comparing anything — otherwise the test would be comparing two identical
numbers and proving nothing.

The provenance is in `Mesher::Held`: each held mesh keeps the `ObjectId`, the
`VolumeMeshControls`, the `QualityThresholds` and the `GeometryRevision` it was
built from, so "was M3 generated from S1?" is answered by comparison and not by
trusting the caller.

## Mutation testing

```text
12 mutations, each a single substitution in production code

11 killed
 1 equivalent mutant (M3), proven so and re-run under the wider filter
 0 genuine survivors
```

One mutation (M5) was **faulty as first written** — it assigned the invalid
definition and then rolled it back, which is identical to validating first.
Corrected to leave it in place, it was killed by five assertions in
`..._AnInvalidSizeFailsBeforeTouchingAnything`, whose failure output shows the
fingerprint had taken on `global=0`, `global=-0.001`, `global=nan` and
`global=inf`.

Full detail, including the harness hazard that produced two void verdicts on
the first re-run attempt, in
[qualification/mutation/README.md](qualification/mutation/README.md).

## Adversarial review

The 24 attack questions, the findings and the mutation table are in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
questions:            24
findings:             8
production defects:   1  (A1, clone() dropped the ID -- found by probe,
                          fixed before any command was written)
test/evidence defects: 4 (A2 fingerprint ignored four selector fields;
                          A6 invented size figures; A7 two wrong assertions)
known limitations:    3  (A3 two staleness authorities, disjoint by
                          reachability; A4 conservative geometry stamp;
                          A5 per-sketch EntityId allows a rebind, pre-existing)
contract clarified:   1  (A8 an unresolved control makes meshing refuse)
remaining open:       0 production defects
```

## Determinism and cross-preset equivalence

The canonical state has nothing in it that could vary: no unordered
iteration, no timing, no randomness, no locale. Local controls and boundary
sets are enumerated through sorted accessors, and the threshold map is ordered
by `QualityMetric`. `MeshingCommand_TheSameSequenceGivesTheSameCanonicalState`
plays a whole execute/undo/redo trace twice and compares every fingerprint.

```text
PRESET            FULL SUITE     REPEAT (5x, 436 tests)
debug-ext         3273 / 3273    436 / 436   4362.84 s
release-ext       3273 / 3273    436 / 436   4203.47 s
debug-shared-ext  3273 / 3273    --
```

Identical results under `-O0 -g`, under the optimiser, and across a DLL
boundary — which answers attack question 24 by measurement rather than by
argument.

The repeat set is the blast radius, not the changed files: 436 tests covering
every meshing and mesh-renderer test, the shared `CommandHistory` contract,
the generic add/delete object commands, the **material** commands (so a
framework regression shows up outside the new code too), `Document`, ID
allocation, the dependency graph, the GUI smoke tests, `compile_fail` and
`architecture`.

## Full regression

```text
debug-ext         3273 / 3273   100%    983.85 s    0 warnings   587 objects
release-ext       3273 / 3273   100%    983.39 s    0 warnings   587 objects
debug-shared-ext  3273 / 3273   100%   1031.23 s    0 warnings   587 objects
```

Unfiltered, after a fresh configure and a clean build in each preset. Each
preset's no-op rebuild did **0 compiles and 0 links**, so the binaries tested
are the ones just built; the shared build linked 9 DLLs including
`libbettercad_meshing.dll`.

The pre-freeze runs had one failure each, `cli.new.unicode-path`, which is a
code-page artefact: `qualify.cmd` sets `chcp 65001` as its first act and the
qualifying runs are 3273/3273. Diagnosed by re-running that one test under
65001, not assumed.

## GUI regression

The application is unchanged by this milestone — `apps` has the same tree hash
as at P16-VIZ-001's freeze — so the GUI tests are a regression check rather
than a new claim. They are in the `gui\.` portion of the repeat set and pass
5x in both Debug and Release, covering the launch smoke test, selection,
visibility, the mesh stale-state display, boundary highlighting and quality
inspection.

## Warnings

```text
0, in all three builds, with -Werror and the project's full warning set
```

## Result

```text
RESULT:    PASS
GATE:      met -- commands mutate canonical meshing intent; undo and redo are
           exact by canonical fingerprint; failed mutations change nothing,
           including both stack depths and the document revision; and no
           generated mesh is canonical command state, which 13 compile-fail
           cases make structurally impossible rather than merely absent
EVIDENCE:  this directory; qualification/qualification-times.txt for the run,
           qualification/mutation/results.txt for the mutations
TREE:      b9593bbc9276e6dbd809a93945152386837c0061, qualified and committed
```

## Known limitations

1. **The application is not wired to a `MeshControl`.** It meshes with default
   controls, so `renderer::statusOf`'s geometry-only staleness is correct for
   every mesh it can see — but it would be wrong for a mesh generated from a
   control. Named at the risk site and in finding A3. Wiring it needs GUI undo,
   which is a scope decision and not this milestone's.
2. **`MeshControl` is not persisted.** P16-PERSIST-001 owns that.
   `MeshControl_CannotYetBeSavedAndSaysSo` records the gap as a test so it is
   visible rather than assumed.
3. **Restoring a geometric parameter does not restore the geometry stamp**, so
   a mesh stays `StaleGeometry` after an identical shape returns. Conservative
   in the safe direction; finding A4.
4. **A no-op edit still records a history entry.** It mutates nothing and
   invalidates nothing; suppressing the entry would mean changing shared
   framework behaviour. Finding, with its reasoning, in the review's §2.
5. **The local-sizing refusals do not name the face in their message.** "there
   is no local mesh sizing control for that face" says what failed and why but
   not which face, where the project's diagnostic standard asks for the object
   too. It is minor — the caller has just supplied the face, and P16-SIZE's own
   validator names it when the refusal surfaces from there — and it was noticed
   after the freeze. Changing a string in a frozen tree voids a three-preset
   qualification, which is not a proportionate price for a message
   improvement, so it is recorded here rather than silently fixed or silently
   ignored.
