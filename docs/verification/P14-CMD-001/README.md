# P14-CMD-001 — Commands / Undo / Redo

```text
STATUS:    PASS
MILESTONE: P14-CMD-001
DATE:      2026-09-24
BASELINE:  e20d6f2 (P14-REGEN-001)
```

## TASK

Express drawing edits as commands in the existing command system, with undo
that restores canonical intent **exactly** and redo that restores the
post-command state exactly — object identity, references and all.

## SCOPE

Authorized by `TODO.md`, `P14-CMD-001`. Not started here: `P14-PERSIST-001`,
`P14-CLI-001`, `P14-EXPORT-001` or anything later. No persistent command
history was implemented; see the persistence boundary below.

## BASELINE

Clean at `e20d6f2`, `HEAD == origin/main`.

The audit came first, and it decided how much needed building.

**There is one command system, and it already works.** `Command` (execute /
undo / redo, `core/document/Command.hpp`) and `CommandHistory` (two stacks,
bound to one document). Its semantics were read rather than assumed, because
four of this milestone's gates are really questions about them:

```text
a successful execute pushes to undo and CLEARS redo
a FAILED execute returns the error and pushes nothing -- redo_ is untouched,
    because redo_.clear() runs only after execute() has succeeded
a failed undo leaves the command undoable and the document unchanged
the history binds to the first document it is used with
there is NO composite/transaction command anywhere in the codebase
```

**The qualified pattern for a module's commands** is `assembly/Commands.hpp`
(`P13-CMD-001`): a create command holds the removed object between undo and
redo so redo restores the SAME object; an edit command holds the value
before; and commands wrap the module's validated functions rather than core's
generic `AddObjectCommand`/`DeleteObjectCommand`, whose job they cannot do —
*validation*.

**The four drawing kinds have one shape**, which is why the commands do too:

```text
createX(document, name, definition) -> Result<XId>
setXDefinition(document, id, definition) -> Result<bool>
removeX(document, id) -> Result<void>
```

and `ViewDefinition`, `DimensionDefinition` and `AnnotationDefinition` each
carry a `Point2D placement`.

## COMMAND CONTRACT

```text
execute   old canonical state -> validated mutation -> new canonical state
undo      new canonical state -> EXACTLY the old canonical state
redo      old canonical state -> EXACTLY the new canonical state
```

and, in every case, derived drawing state is recomputed afterwards by
regeneration rather than restored from the command.

**What a command stores.** A definition before, a definition after, or the
removed `DocumentObject` itself. **Never** a projection, a measured value, a
BOM row or an item number: those are recomputed on every call (ADR-011,
ADR-014), and a stored one would be a remembered answer to a question whose
answer has changed. Two tests exist for exactly this and are described under
INDEPENDENT VALIDATION.

**A move takes an absolute position, never a delta**, and undo restores the
position that was there. A delta-based undo drifts; this cannot.

**Exactly one validated document mutation per command.** No drawing command
touches two objects, so there is no window in which half of one is applied.
That is why no transaction was added: `CommandHistory` already discards a
command whose execute failed, and there is nothing finer to roll back.

## WHY THESE COMMANDS EXIST AT ALL

Core's generic `AddObjectCommand`/`DeleteObjectCommand` already add and remove
any `DocumentObject` with the same-ID guarantee. What they cannot do is
validate — and for views that is not a theoretical gap:

```text
removeView()          refuses while another view is projected from it
DeleteObjectCommand   calls Document::removeObject() directly, and so
                      removes the parent and ORPHANS the child
```

The evidence for that is in the suite, not in this paragraph:
`Command_DeletingAViewWithAProjectedChildIsRefusedAndChangesNothing` runs the
generic command on a clone of the same document and asserts that it
*succeeds* and leaves the child orphaned, beside the drawing command that
refuses. That is not a defect in core — it is the reason a module wraps its
own policy.

## IMPLEMENTATION

```text
include/bettercad/drawing/Commands.hpp    NEW  15 commands
src/drawing/Commands.cpp                  NEW  their implementation
tests/drawing/DrawingCommandTests.cpp     NEW  the suite
include/bettercad/drawing/Views.hpp            checkRemoveView() declared
src/drawing/Views.cpp                          its definition, extracted
src/drawing/CMakeLists.txt                     the new source
tests/CMakeLists.txt                           the new test file
```

**20 inserted lines in existing files, and one deletion.** The only change to
existing production code is the extraction of `checkRemoveView()` from
`removeView()`, which is behaviour-identical: `removeView()` now calls it.
`DeleteViewCommand` needs the same precondition but must keep the removed
object for undo, which `removeView()` discards — so the rule has one
implementation and two callers rather than two copies that could drift.

The fifteen commands:

```text
sheets       CreateSheetCommand  SetSheetDefinitionCommand  DeleteSheetCommand
views        CreateViewCommand   SetViewDefinitionCommand   MoveViewCommand
             DeleteViewCommand
dimensions   CreateDimensionCommand  SetDimensionDefinitionCommand
             MoveDimensionCommand    DeleteDimensionCommand
annotations  CreateAnnotationCommand SetAnnotationDefinitionCommand
             MoveAnnotationCommand   DeleteAnnotationCommand
```

Their bodies are four file-local templates — create, edit, move, delete — so
the fifteen cannot drift apart in what they capture or in what order. The
templates are in the `.cpp` and not the header on purpose: a class template's
members are implicitly inline, and an inline member carrying the export macro
becomes `dllimport` in `debug-shared`, which is the defect that preset has
already caught twice in P14.

## BOM AND BALLOON COMMANDS

There are no separate ones, and that is the ADR-022 answer rather than an
omission: **a BOM table and a balloon are annotation KINDS**, so
`CreateAnnotationCommand`, `SetAnnotationDefinitionCommand`,
`MoveAnnotationCommand` and `DeleteAnnotationCommand` *are* the BOM and
balloon commands.

There is deliberately **no command for a row, a quantity or an item number**.
None of the three is stored: all are computed from the active occurrence set
on every call. A command that "edited" one would be writing down an answer the
assembly is entitled to change.

A balloon's canonical target stays the **occurrence** (a `ComponentId`), never
the number it displays. Retargeting is an explicit edit through
`SetAnnotationDefinitionCommand`; nothing rebinds a reference on its own.

## TESTS

**27 new test cases**, all in `tests/drawing/DrawingCommandTests.cpp`.

```text
[cmd][p14]   1452 assertions in  27 test cases
```

Discovery confirmed: the suite grew from 2070 ctest entries to **2097**, which
is exactly 27 — every case is discovered and run, none is compiled and
forgotten.

### How "exact" is measured

Every state comparison is the document's **own serialization**
(`io::documentToJson`), compared as text. That is the right instrument and not
a convenience: ADR-011 keeps derived state out of the file, so the JSON is
precisely canonical intent — every ObjectId, every reference, every placement,
every format setting — and no projection, measured value or BOM row. Two
states that serialize identically are the same canonical state; two that draw
the same but serialize differently are not, and this suite fails them.

**Two fields are excluded, and are asserted separately instead.**

```text
last_allocated_id   the ID allocator's watermark. It MUST NOT roll back on
                    undo: a redo is holding an ID and will put it back, so a
                    rewound allocator would hand that ID to the next create
                    and the redo would collide. Asserted in the right
                    direction (monotonic) by its own test.
the document UUID   generated per document, so it differs between fixtures by
                    design. Only the determinism test compares across
                    documents.
```

This was found by a failing test, not by foresight: the first version of the
suite compared whole files and four cases failed, because `last_allocated_id`
correctly does not come back. The instrument was wrong, not the commands.

### The mapping to the checklist

| `TODO.md` item | Test |
| --- | --- |
| Sheet commands | `UndoAndRedoRestoreTheExactCanonicalStateForEveryDrawingKind`, `DeletingASheetAndUndoingItRestoresTheSheetAndWhatNamedIt` |
| View create/delete/move | `MovingAProjectedViewIsRefusedBecauseItStoresNoPlacement`, `DeletingAViewWithAProjectedChildIsRefusedAndChangesNothing`, `DeletingAViewAndUndoingItRestoresEveryReferenceToIt` |
| Dimension commands | `ARedoneDimensionShowsTheCurrentModelValueNotTheOneItWasCreatedWith`, `UndoAndRedoKeepADimensionOnTheSameFaceWithAnIdenticalFacePresent` |
| Annotation commands | `UndoAndRedoRestoreTheExactCanonicalStateForEveryDrawingKind`, `RedoRestoresTheSameObjectIdForEveryDrawingKind` |
| BOM / balloon commands | `ABomTableAndABalloonAreCreatedMovedAndDeletedAsAnnotations`, `ARedoneBalloonShowsTheCurrentItemNumberAndNeverAStoredOne`, `RetargetingABalloonIsAnExplicitEditAndKeepsTheOccurrenceIdentity` |
| Undo restores exact intent | `UndoAndRedoRestoreTheExactCanonicalStateForEveryDrawingKind`, `MultiLevelUndoAndRedoWalkEveryIntermediateStateExactly`, `AWholeDrawingScriptUndoesAndRedoesThroughEveryState` |
| Redo restores exact state | the same three, in the other direction |
| Failed commands are atomic | `EveryRefusedCommandLeavesTheDocumentAndBothStacksExactlyAsTheyWere`, `AnEditRefusedAfterTheCommandHasReadTheOldStateRollsBackCleanly`, `UndoingACommandThatWasNeverExecutedIsRefused` |
| Redo invalidation correct | `ANewCommandAfterAnUndoDropsTheRedoBranch`, `AFailedCommandLeavesTheRedoBranchIntact`, `AnEditToTheValueAlreadyThereIsAValidCommandAndIsReversible` |
| Regeneration integrates | `AfterACommandTheDrawingObjectsItReachesRegenerate`, `ACommandThatBreaksADrawingCommitsTheIntentAndRegenerationReportsIt` |
| Determinism PASS | `TheSameCommandSequenceFromTheSameBaselineGivesTheSameCanonicalState`, plus the repeat gate |

Four more carry things the checklist does not name:
`AHundredMoveUndoRedoCyclesLandOnExactlyTheSameNumbers` (drift),
`TheIdAllocatorNeverRewindsSoARedoneObjectCannotCollide`,
`UndoingACreationIsRefusedWhileSomethingIsProjectedFromIt`, and
`HistoryIsSessionStateAndIsNotPartOfTheDocument`.

### Multi-level, not single-command

Two tests walk whole scripts rather than one command:

```text
MultiLevelUndoAndRedo...   7 commands, 8 states, 7 -> 0 and 0 -> 7, with the
                           canonical state AND both stack depths checked at
                           every step
AWholeDrawingScript...     the brief's own sequence on a real assembly --
                           sheet, view, move, dimension, note, BOM table,
                           balloon, move balloon -- down and back up, then
                           regenerated and drawn again at the end
```

A command that restores its own edit but disturbs a neighbour's passes a
single-command test and fails both of these.

## INDEPENDENT VALIDATION

The reference is never the command's own bookkeeping.

**The serialization is an independent witness.** It is written by `io`, was
qualified long before this milestone, and knows nothing about commands. A
command that restored "something equivalent" would have to produce the same
bytes to pass, which is what makes ID-for-ID restoration checkable rather than
asserted.

**Derived values are checked against a CHANGED model, which is the only way to
catch a snapshot.** Two tests do this, and they are the ones that would fail
if any command had captured a result:

```text
ARedoneDimensionShows...    create a dimension at 100 mm, UNDO it, widen the
                            model to 137.5, then REDO. A command holding its
                            measured value puts 100 back. The test requires
                            137.5 -- and requires the stored reference to be
                            the same two sketch entities it always was.
ARedoneBalloonShows...      a balloon on an occurrence that is item 2; undo;
                            delete the two occurrences ahead of it so it
                            becomes item 1; redo. A command holding the
                            number draws 2. The test requires the drawn text
                            to be "1", and the stored target to be the same
                            ComponentId.
```

**The generic command is the control.**
`DeletingAViewWithAProjectedChildIsRefusedAndChangesNothing` does not merely
assert that the drawing command refuses; it runs core's `DeleteObjectCommand`
on a **clone of the same document** and asserts that it succeeds and leaves
the child orphaned. The difference between the two is the milestone's
justification, measured.

**Rebinding is tested against a deliberate look-alike.**
`UndoAndRedoKeepADimensionOnTheSameFaceWithAnIdenticalFacePresent` builds a
second, identical block before the undo, so an implementation that resolved by
resemblance would have something to grab. It is not grabbed.

## ADVERSARIAL REVIEW

Run against the final diff, on the brief's own twenty questions. **Two gaps
were found in my own work and closed; no production defect was found in the
system under review.**

### The questions, answered by tests rather than by assertion

| Question | Answer |
| --- | --- |
| Can undo restore equivalent data with different IDs? | No — `RedoRestoresTheSameObjectIdForEveryDrawingKind`, and any ID change fails the serialization comparison |
| Can redo use stale measured values? | No — `ARedoneDimensionShows...`, against a changed model |
| Can a balloon redo store an old item number? | No — `ARedoneBalloonShows...`, against a changed assembly |
| Can view-move undo drift? | No — 100 cycles, compared with `==` on the SI doubles, not a tolerance |
| Can failed commands enter history? | No — `CommandHistory` pushes only after a successful execute; asserted in the refusal battery |
| Can failed commands clear redo incorrectly? | No — `AFailedCommandLeavesTheRedoBranchIntact` |
| Can undo restore stale derived geometry? | There is none to restore; nothing derived is stored (ADR-014) |
| Can rollback leave dirty flags wrong? | Commands never touch the regenerator; dirtiness is recomputed from revisions each pass |
| Can deleting a source view silently rebind dependents? | No — `DeletingAViewAndUndoingItRestoresEveryReferenceToIt` asserts both still name the missing view, and that regeneration reports them |
| Can undo restore a target to another identical feature? | No — the look-alike test above |
| Can redo order differ between Debug and Release? | No ordering dependence; both presets are in the qualification |
| Can a late failure leave half a command applied? | Each command performs exactly ONE validated mutation, so there is no partial window; `AnEditRefusedAfterTheCommandHasReadTheOldState...` exercises the latest failure point there is |
| Can BOM quantities become command-owned state? | No — there is no BOM-row command, by ADR-022 |
| Can history contain derived scene objects? | No — the payload types are definitions and `DocumentObject`s; readable in the header's private sections |
| Can undo restore a suppressed component wrongly? | Drawing commands do not touch suppression; deletion captures and restores `ObjectOverrides` exactly as core's `DeleteObjectCommand` does |
| Can command ordering create dependency cycles? | Drawing edges run view→sheet and dimension→view, and a parent must already exist, so no cycle is reachable through these commands |
| Can deleting a sheet and undoing it renumber SheetIds? | No — same ID, and the view that names it resolves again |
| Can an invalid annotation command partially modify placement? | No — the definition is copied, modified, and offered to the validated setter; a refusal leaves the document untouched |
| Can a no-op command destroy redo history? | It does, and that is the existing rule applied consistently — see below |
| Did this accidentally implement persistent history? | No — `HistoryIsSessionStateAndIsNotPartOfTheDocument` |

### The no-op question, decided rather than dodged

An edit to the value already there is a **successful command**: it enters
history, and — like every successful command — it clears the redo branch. The
test says so explicitly, with something redoable waiting.

That is `CommandHistory`'s existing rule, and it is also what the qualified
assembly commands do (`SetComponentPlacementCommand` ignores the "did anything
change" flag its setter returns). The alternative — quietly not recording a
no-op — would make the undo stack depend on whether the user happened to type
the number that was already there.

### Gaps found in my own work, and closed

**The measuring instrument was too broad.** Comparing whole serialized files
failed four cases, because `last_allocated_id` correctly does not roll back on
undo. Fixed by excluding it and the per-document UUID, and by asserting the
allocator's real invariant — monotonicity — in a test of its own rather than
ignoring it.

**Two adversarial questions had no test.** Moving a *projected* view (which
stores no placement) and the no-op command's effect on the redo stack were
both argued in comments and not asserted. Both now have tests; the first
checks the refusal carries the qualified validator's own words ("derived from
its parent") rather than a second opinion invented in the command.

### What was deliberately not built

No transaction or composite command. The codebase has none, `CommandHistory`
already discards a failed command, and **no drawing command touches two
objects** — so there is no multi-object edit that needs one. Adding a
transaction mechanism to a system that has never needed one would be the
speculative abstraction `CLAUDE.md` forbids.

## DETERMINISM

```text
repeat release  exit 0   2096 / 2096, each test run five times (1102.79 s)
repeat debug    exit 0   2096 / 2096, each test run five times (1193.95 s)
```

`TheSameCommandSequenceFromTheSameBaselineGivesTheSameCanonicalState` runs the
same script into five fresh documents and requires five identical canonical
states — identical ObjectIds included, because IDs are allocated in a
deterministic order from a deterministic baseline. The script ends with a
round trip through the history (two undos, two redos), so what is compared is
a state *returned to*, not merely a state built.

Nothing here depends on iteration order, wall-clock time, a random seed,
thread scheduling, a temporary path or a locale. Undo and redo are two stacks
walked from the top; a command's payload is plain values; and the four
templates the commands share fix the order in which each captures what it
captures.

**The OneDrive filesystem fault did not recur**, in either repeat preset —
the second milestone running in which it has not. As before, that does not
close it: it is intermittent, and `TODO.md` still carries the decision as
due.

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2097 / 2097** (272.53 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2097 / 2097** (227.91 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2097 / 2097** (255.88 s) |

```text
qualification finished Thu 24/09/2026 11:44:02.61, 0 stage(s) failed
```

The no-op rebuild logs contain only `Checking git revision` — no compile, no
link — which is what proves the binaries tested are the ones just built.

Baseline was 2070 ctest entries; this milestone adds **exactly 27**, which is
the number of new test cases. Nothing was written and left undiscovered.

### The harness

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it reports `QUALIFICATION FAILED: 3 stage(s)
failed` and gives `qualify.cmd` exit 3.

### The qualified tree is the committed tree

Tree IDs from a scratch index, before the first build, after the last test run
and again before the commit — identical in all three:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           494a26546a2feb0c36d34f14a50ccaddcc8ed430
src               ecb006508b42832b6ae347aa4ffe82b691e57b2a
tests             a23aa4ab4af50997d7a493f63c94338fb43035e9
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-REGEN-001 baseline**: this milestone changed no
application, no example and no build configuration.

A full debug run was also made on the same tree before the freeze
(**2097/2097**), which is where the `checkRemoveView()` extraction was first
shown to disturb nothing.

## KNOWN LIMITATIONS

**Deleting a sheet does not refuse while views sit on it.** `removeSheet()`
checks only that the sheet exists — unlike `removeView()`, which refuses to
orphan a projected child. So deleting a sheet leaves its views naming a sheet
that is gone. That is not silent: since `P14-REGEN-001` the graph reports
those views as failed, which is the unresolved state the architecture defines
for this edit, and undo restores everything exactly
(`DeletingASheetAndUndoingItRestoresTheSheetAndWhatNamedIt` asserts both
halves). The asymmetry between the two policies is `P14-SHEET-001`'s to
revisit, not this milestone's: changing it would change qualified behaviour.

**A move command exists for views, dimensions and annotations, not for
sheets.** A sheet has no placement — it is the page.

**A projected, section or auxiliary view cannot be moved by
`MoveViewCommand`**, because its placement is derived from its parent
(ADR-018). It is moved by its `spacing`, through
`SetViewDefinitionCommand`. Refused with the validator's own diagnostic, and
tested.

**Command history is session state and is not persisted.** `P14-PERSIST-001`
concerns drawing *intent*; nothing here writes a history to the file and the
test asserts the file contains no trace of one. Whether undo should survive a
reload is a separate decision nobody has authorized.

**Carried from `P14-STREF-001`, unchanged:** "no silent rebinding" is still
not met for chamfer faces, which are named by position in the chamfer's edge
list. That milestone stays open and this one does not touch it.

**Carried from `P14-REGEN-001`, unchanged:** `RegenerationReport::succeeded()`
can be true for a document whose assembly did not solve. Recorded there, still
out of scope.

## RESULT

```text
TASK:            P14-CMD-001 -- Commands / Undo / Redo
IMPLEMENTATION:  15 drawing commands in the EXISTING command system -- no
                 second history, no transaction mechanism, no persistent
                 history. They wrap the drawing module's validated functions,
                 because core's generic DeleteObjectCommand walks past
                 removeView()'s refusal to orphan a projected child. The only
                 change to existing production code is the extraction of
                 checkRemoveView(), so that policy has one implementation
TESTS:           27 new; 2097/2097 in debug, release and debug-shared, each
                 from clean; 2096/2096 five times over in both repeat presets
VALIDATION:      every state comparison is the document's own serialization,
                 written by io and knowing nothing about commands; the two
                 derived-value tests change the MODEL between a command and
                 its redo, which is the only way to catch a snapshot; the
                 generic command is run on a clone as a control
ADVERSARIAL:     0 production defects found in the system under review;
                 3 gaps found in my own work and closed -- a measuring
                 instrument that was too broad, and two adversarial questions
                 that had comments instead of tests
WARNINGS:        0 in all three builds
DETERMINISM:     five identical canonical states from five fresh documents;
                 100 move/undo/redo cycles landing on the same doubles
                 exactly; repeat gate clean in both presets
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-CMD-001 marked [x], all 13 items
NEXT:            P14-PERSIST-001 -- Drawing Persistence. P14-STREF-001
                 remains OPEN on its chamfer gap.
```

## REVISION

One revision during the milestone, and it was to the instrument rather than to
the code. The first version of the suite compared whole serialized documents
and four cases failed, because `last_allocated_id` does not roll back on undo.
That is correct behaviour — a redo is holding an ID and will put it back — so
the comparison was narrowed to exclude the allocator watermark and the
per-document UUID, and the allocator's real invariant (monotonic, never
rewound) was given a test of its own. No command changed.

## FILES

```text
qualification/qualify.cmd               the harness
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree IDs
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               2097/2097 in each preset
qualification/ctest-repeat-*.log        2096/2096 five times over, debug and release
```
