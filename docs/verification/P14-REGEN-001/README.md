# P14-REGEN-001 — Drawing Regeneration

```text
STATUS:    PASS
MILESTONE: P14-REGEN-001
DATE:      2026-09-24
BASELINE:  ddb9bfd (P14-STREF-001 audit)
ADR:       ADR-023 — a drawing handler resolves references, never the drawing
```

## TASK

Put drawing objects into the dependency graph: a model change must reach the
drawing state it affects, an unresolved reference must be reported rather than
passed over, a failure must commit nothing, and stale drawing geometry must be
impossible.

## SCOPE

Authorized by `TODO.md`, `P14-REGEN-001`. Not started here: `P14-CMD-001`,
`P14-PERSIST-001`, `P14-CLI-001` or anything later.

## BASELINE

The audit came first, and it decided the shape of the work.

**ADR-014 mandated drawing regeneration handlers, and they were never built.**
Its decision section says `Sheet`, `View`, `Dimension` and `Annotation` "each
get a `RegenerationHandler` registered by the drawing module", that one
"resolves the object's references and **fails** if one does not resolve", and
that this "is not optional politeness: an object with no handler is silently
marked `UpToDate` and never validated". Measured:

```text
grep -rn "registerHandler" include/bettercad/drawing/ src/drawing/   -> nothing
grep -rn "registerHandlers" include/ src/                           -> assembly only
```

So every sheet, view, dimension and annotation took the unhandled branch at
`src/features/Regenerator.cpp:344-347` and regenerated as a success whatever
had happened to what it names.

**Nothing in the drawing layer caches derived state.** Confirmed by reading,
not assumed: `Sheet.hpp:30`, `Views.hpp:21`, `Dimensions.hpp:19,66` and
`Annotations.hpp:18` each say so, and there is no `mutable`, no cache member
and no stored projection anywhere under `src/drawing/`. So the "invalidate
what went stale" half of the milestone has nothing to invalidate, and the work
is the other half: validation, and the dirty set.

## ARCHITECTURE

`ADR-023`, with three candidates compared: the handler runs the object's full
resolver; the handler runs the reference-resolving prefix of that resolver; a
drawing final pass after `assembly.solve`.

The full resolver is not merely expensive, it is **wrong during the object
phase**. The assembly solve is a final pass (`ADR-008`), so while objects are
being built `Regenerator::transforms()` still holds the previous pass's map —
and `draw()` moves a balloon's anchor by its occurrence's solved transform
(`Annotations.cpp:476`), while `measure()` does the same for a view of a
component (`Dimensions.cpp:337`). A handler calling them would read a stale
transform, or, with an empty lookup, report "the assembly did not solve" on
every pass of a healthy document.

Chosen: the prefix. The cut is an existing line inside `viewGeometry()` —
after `effectiveSubject`, `effectiveSource`, `effectiveBasis`,
`effectiveScale`, `effectivePlacement` and the body resolution, before the
section cut and the HLR.

## BLAST RADIUS

Two changes reach outside the new files, and they have very different reach.

**`View::dependencies()`** is read by the dependency graph, so it reaches
everything built on the graph whether or not it draws: regeneration order,
the dirty set, the missing-reference check, `features::validateDocument`,
`regenerateResultBodies`, and the STEP/STL export path through
`io::placedAssembly`. It is the reason the repeat selection includes
`[Vv]alidat` and `[Ee]xport` as well as the drawing tests, and the reason the
reference models are in scope.

The change can only ever REMOVE an edge, and only one that named a
non-existent object, so nothing that worked before can lose an edge it was
using.

**`resolveDimensionTargets` / `resolveAnnotationTarget`** are additions. They
share `resolveTarget` with `measure()` and `draw()` but do not modify them, so
no existing caller changes behaviour. `src/drawing/Annotations.cpp` gained an
include of `assembly/Configurations.hpp`, which `drawing` already links.

**`drawing` now links `BetterCAD::features` explicitly.** It already had it
transitively through `assembly`, so no link line changes in practice; the
direct dependency is now declared directly. Layer 4 including layer 2 is what
`ARCHITECTURE.md` allows, and `architecture.layering` is in the regression.

Not reached: no serialized format changed, no ID scheme changed, no public
behaviour of `measure()`, `draw()` or `projectedGeometry()` changed, and no
existing test was edited.

## IMPLEMENTATION

```text
include/bettercad/drawing/Regeneration.hpp   NEW  registerHandlers()
src/drawing/Regeneration.cpp                 NEW  the four handlers
src/drawing/View.cpp                              the ObjectId{0} edge, removed
include/bettercad/drawing/Dimensions.hpp          resolveDimensionTargets()
src/drawing/Dimensions.cpp                        its definition
include/bettercad/drawing/Annotations.hpp         resolveAnnotationTarget()
src/drawing/Annotations.cpp                       its definition
src/drawing/CMakeLists.txt                        the new source, and features
tests/drawing/DrawingRegenerationTests.cpp   NEW  the suite
tests/CMakeLists.txt                              registers it
docs/architecture/decisions/ADR-023-*.md     NEW  the decision
```

137 lines of production change in existing files. **No existing test was
edited, and no existing production behaviour was changed** other than the
removal of the bogus dependency edge.

**What each handler asks.**

```text
sheet        validate(definition) -- a sheet names nothing outside itself
view         effectiveSubject; then for an object view the source must be
             local and valid, and for an assembly view drawnOccurrences()
             must find something to draw; then effectiveBasis,
             effectiveScale, effectivePlacement, which walk the parent chain
dimension    resolveDimensionTargets: measure()'s own opening
annotation   resolveAnnotationTarget: draw()'s branches, plus whether a
             balloon's occurrence is in force
```

Each returns `std::nullopt`: a drawing object owns no geometry, so the
regenerator publishes nothing for it and there is nothing that could go
stale. Each forwards the resolver's own `ErrorCode`, which is what carries
`P14-STREF-001`'s Resolved / Unresolved / Invalid split into
`Regenerator::error()`.

## TESTS

**23 new test cases**, all in `tests/drawing/DrawingRegenerationTests.cpp`.

```text
[regen][p14]   759 assertions in  23 test cases
[drawing]    10126 assertions in 372 test cases   (was 9367 in 349)
```

Discovery confirmed: the suite grew from 2047 ctest entries to **2070** in
every preset.

One test per checklist item, and the mapping is deliberate:

| `TODO.md` item | Test |
| --- | --- |
| Define dirty-propagation triggers | `WithoutAHandlerABrokenDimensionRegeneratesAsUpToDate`, `EveryDrawingKindIsValidatedRatherThanAssumed` |
| Rebuild views after model changes | `AViewRedrawsAtTheModelsNewSize` |
| Update dimensions after model changes | `ADimensionFollowsTheModelItNames` |
| Update annotations/BOM where required | `AddingAnOccurrenceMovesTheBomAndTheBalloonNumbers`, `AConfigurationChangesWhatTheAssemblyViewDrawsAndTheBomSays` |
| React to configuration changes | `ABalloonOnASuppressedOccurrenceFailsAndComesBack`, `AnAssemblyViewWithNothingActiveFails` |
| Regenerate only affected state | `AModelEditReachesOnlyTheDrawingObjectsThatNameIt`, `AnAssemblyDrawingSettlesInsteadOfRebuildingEveryPass` |
| Preserve canonical drawing intent | `AFailedDrawingRegenerationCommitsNothing` |
| Handle unresolved references explicitly | `TheFailureCodeSaysUnresolvedOrInvalid`, `AViewWhoseSourceIsRemovedFailsAndBlocksItsDimensions`, `AViewOfAnExternalPartFailsRatherThanPassingSilently`, `ABalloonIsCheckedForBeingInForceNotJustForResolving` |
| Validate failure atomicity | `AFailedRegenerationCannotBeDrawnWithTheOldGeometry`, `AFailedDrawingRegenerationCommitsNothing` |
| Validate deterministic regeneration | `RepeatedPassesGiveTheSameReportAndTheSameDrawing`, plus the repeat gate |

Three more carry the findings rather than a checklist item:
`AnAssemblyViewDeclaresNoEdgeToAnAbsentSource` (the defect found here),
`ThePrefixAndTheFullResolverAgreeOnEveryBrokenReference` (the one way this
design could fail quietly), and `UndoingTheDeletionOfWhatAViewDrawsRestoresIt`.

## INDEPENDENT VALIDATION

There is no analytic reference for "was this object validated", so validation
here is of three other kinds, none of which reads the implementation for its
expected answer.

**The before-and-after, in one process.** `WithoutAHandlerABrokenDimension
RegeneratesAsUpToDate` builds one document, breaks one face name, and
regenerates it twice: once through a bare `features::Regenerator`, once
through one with the drawing handlers. The bare run reports `UpToDate`, no
error and no entry in `failed`; the other reports `Failed` with a diagnostic.
The claim that this milestone changes anything is therefore measured rather
than asserted from the commit history — and the old behaviour stays reachable,
because registration is the caller's.

**The two resolvers, checked against each other.** The handlers run
hand-written prefixes of `measure()` and `draw()`, which is the one way this
design could fail quietly. `ThePrefixAndTheFullResolverAgreeOnEveryBroken
Reference` asserts, in both directions and over every breakage the fixtures
can produce, that a prefix never passes what the whole fails, and never fails
what the whole passes; where both fail, the error CODES must match too. The
full resolvers are the ones `P14-STREF-001` qualified, so they are an
independent reference and not a restatement.

**Analytic geometry, where a number is involved.** Every dimension asserted
comes from the fixture's own construction and not from the code: a 100 mm
profile widened to 137.5 mm reads 137.5; a view of it spans 180 mm after
widening to 180; the same view at 1:2 spans 50. The block volumes and BOM
quantities are counted from what the fixture places.

## ADVERSARIAL REVIEW

Run against the final diff, on `docs/engineering/ADVERSARIAL_REVIEW.md`'s
questions. **Three defects of my own were found and fixed here; two in the
system under review were found, one fixed and one recorded.**

### What was assumed, and how each assumption was checked

| Assumption | How it was checked |
| --- | --- |
| No drawing derived state is cached anywhere | every file under `src/drawing/` read for a cache member or `mutable`; then TESTED, by changing a view's scale with NO regeneration pass and getting the new projection — a cache keyed on the model would still have been "valid" and still wrong |
| A drawing object's dependencies include everything it names, so the body lookup is ordering-safe | all three `dependencies()` read; `Dimension` and `Annotation` add `referencedObjects(target)`, `View` its sheet, source and parent |
| During the object phase, `transforms()` holds the PREVIOUS pass's map | `src/features/Regenerator.cpp:356` — `transforms_.clear()` runs after the object loop, and the final passes fill it |
| `measure()` and `draw()` need those transforms | `Dimensions.cpp:337`, `Annotations.cpp:476` — both fail "the assembly did not solve" without one |

### Defects found in my own work, and fixed

**A test claimed more than it proved.** `...IsComputedNotStored` asserted that
a dimension still read 100 mm after the sketch was edited but before
regeneration. That is true of a *cache* as well, so it proved nothing about
storage. It now also halves the view's scale with no regeneration and requires
the projection to come back at the new scale.

**The view handler would have mislabelled an absent source.** An Object-subject
view with an empty source would have been reported as "in another document",
which it is not. `createView()` refuses such a view so it is unreachable
today, but a file written by something else is not. It now says "draws one
object but names none", with `InvalidArgument`.

**The prefix was weaker than the whole, in a way that mattered.** A balloon's
occurrence resolves through its PART, which a configuration does not touch — so
`resolveTarget` anchored a balloon on a suppressed component and the handler
passed it, while `draw()` fails it. That is the milestone's own defect wearing
a different hat. `resolveAnnotationTarget` now asks
`assembly::activeComponents()`, and
`ThePrefixAndTheFullResolverAgreeOnEveryBrokenReference` asserts the two never
disagree about a failure, in both directions, over every breakage the fixtures
can produce.

### Defects found in the system under review

**The missing handlers** — the milestone's subject, measured in one process by
`WithoutAHandlerABrokenDimensionRegeneratesAsUpToDate`.

**`View::dependencies()` declaring `ObjectId{0}`** — fixed. The regression runs
with **no drawing handlers registered at all**, because it is the graph's own
check and must hold without anything this milestone adds.

**`RegenerationReport::succeeded()` can be true when a final pass failed** — a
failing pass records its error under `ObjectId{}` and never appends to
`report.failed` (`Regenerator.cpp:360-364`). Recorded, NOT fixed: it is in
`features`, it changes what `succeeded()` means for every caller, and it is
outside this milestone.

### The rest of the checklist

```text
weakened test or moved tolerance   no. kMm = 1e-9, as in the P14-STREF-001
                                   suite. No existing test was edited --
                                   git diff touches no file under tests/
                                   except the CMakeLists that registers the
                                   new one
independent expected values        the dimensions (100, 137.5, 150, 180, and
                                   50 at 1:2) come from the fixture's own
                                   construction, not from the implementation
hidden global state                none; a handler captures only what it is
                                   passed
save/load                          tested, both the values and the states
undo/redo                          tested, through removeObject/insertObject,
                                   which restores the original ObjectId
rebinding                          the recovery test puts the rectangle back
                                   with a NEW entity id, and requires the
                                   reference NOT to adopt it
Debug vs Release                   no new floating-point logic; both presets
                                   are in the qualification
order of operations                handlers are keyed by type name, so
                                   registration order cannot matter; the
                                   object loop is the graph's topological
                                   order; final passes run in name order
partial state on failure           a handler is a pure read; asserted over
                                   three consecutive failing passes that the
                                   definition, the revision and the object
                                   count do not move
architectural boundary             drawing (4) -> features (2), which
                                   ARCHITECTURE.md allows and
                                   architecture.layering enforces in the suite
scope                              nothing from P14-CMD-001 or later was
                                   started
```

### Coverage not extended

The blocked-by-parent path is tested for a dimension under a failed view, and
not separately for a PROJECTED view under a failed parent. It is the same
mechanism — the parent is a declared dependency, so the graph blocks it — and
`removeView()` already refuses to orphan a child (`P14-STREF-001`). Recorded
as a coverage gap rather than dressed up as tested.

## FAILURE PATHS

| What breaks | Who notices | State |
| --- | --- | --- |
| A view's source object is deleted | the graph's missing-reference check | view `Failed`, its dimensions `Blocked` |
| A view's source is in another document | the view handler | `Failed`, `FailedPrecondition` |
| An assembly view draws nothing in this configuration | the view handler | `Failed`, `FailedPrecondition` |
| A dimension's named face stops existing | the dimension handler | `Failed`, `NotFound` |
| A dimension names a role its feature cannot produce | the dimension handler | `Failed`, `InvalidArgument` |
| A balloon's occurrence is suppressed here | the annotation handler | `Failed`, `FailedPrecondition` |
| A BOM table's view stops being an assembly view | the annotation handler | `Failed` |
| The feature a dimension names fails to build | the graph | `Blocked` |
| The assembly does not solve | the assembly final pass | reported there, not duplicated |
| A projection fails numerically | `projectedGeometry`, when drawn | not a regeneration failure — a stated limitation |

Every one of these was, before this milestone, `UpToDate` and silent.

## DETERMINISM

```text
repeat release  exit 0   2069 / 2069, each test run five times (992.61 s)
repeat debug    exit 0   2069 / 2069, each test run five times (993.00 s)
```

`RepeatedPassesGiveTheSameReportAndTheSameDrawing` also regenerates one
assembly drawing six times from a regenerator that forgets everything each
pass, and requires the annotation summary, the dimension text and the drawn
edge count to be identical every time.

Nothing here depends on iteration order, wall-clock time, a random seed,
thread scheduling, a temporary path or a locale. Handlers are stored in a
`std::map` keyed by type name, so registration order cannot reach the result;
objects are regenerated in the graph's topological order; final passes run in
name order.

**The OneDrive filesystem fault did not recur**, in either repeat preset. As
in the last three milestones, that does not close it: it is intermittent, and
`TODO.md` still carries the decision as due.

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2070 / 2070** (198.47 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2070 / 2070** (179.04 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2070 / 2070** (197.08 s) |

```text
qualification finished Thu 24/09/2026 9:24:55.89, 0 stage(s) failed
```

The no-op rebuild logs contain only `Checking git revision` — no compile, no
link — which is what proves the binaries tested are the ones just built.

### The harness

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it reports `QUALIFICATION FAILED: 3 stage(s)
failed` and gives `qualify.cmd` exit 3.

### The qualified tree is the committed tree

Tree IDs from a scratch index, before the first build and after the last test
run — identical:

```text
apps              b32ce14e7be30b1c05432f740c2d25607be73b39
include           fcef48629341db14e2d3e42f60b58522ee15127a
src               359c4fa4a41b013c1b80ba19b2ce7865024cc7c6
tests             b17de1300554c4a68f6a951cfd036534f860d22e
examples          d0d2ae4277ba99b46ff1384725292deb3519c199
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

`apps`, `examples`, `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
**byte-identical to the P14-STREF-001 baseline**: this milestone changed no
application, no example, and no build configuration.

A full debug run was also made on the same tree before the freeze
(**2068/2068** at that point, before the last two tests were added), which is
where the `View::dependencies()` change was first shown to disturb nothing —
the reference models included.

## KNOWN LIMITATIONS

**Nothing in production registers the drawing handlers yet.** They are
registered the way `assembly::registerHandlers` is — by whoever builds a
regenerator at layer 4 or above — and today no production code regenerates a
document for its drawings: `features::validateDocument` and
`regenerateResultBodies` are layer 2 and structurally cannot call layer 4, and
the CLI's drawing commands are `P14-CLI-001`. The tests are therefore the only
caller, and that is stated rather than dressed up. The `View::dependencies()`
fix, by contrast, is unconditional and reaches every regeneration in the
codebase.

**A handler does not catch a projection that fails numerically.** It resolves
references; the drawing is built on demand and fails there. A view can be
`Regenerated` and still refuse to draw. This is `ADR-023`'s stated trade.

**A sheet handler cannot fail through the public API.** `Sheet::create()`
validates and the load path goes through it, so re-validating a stored
definition is defensive rather than load-bearing. It is registered for
uniformity, at the cost of one struct check.

**Carried from `P14-STREF-001`, unchanged:** "no silent rebinding" is not met
for chamfer faces, which are named by position in the chamfer's edge list.
That milestone stays open, and this one does not touch it.

**Observed, not this milestone's to fix.** A final pass that fails records its
error under `ObjectId{}` and never appends to `report.failed`
(`src/features/Regenerator.cpp:360-364`), so `RegenerationReport::succeeded()`
can be `true` for a document whose assembly did not solve. It is in `features`
and `assembly`, both qualified, and fixing it changes what `succeeded()` means
for every caller. Recorded here because this milestone's audit found it.

## RESULT

```text
TASK:            P14-REGEN-001 -- Drawing regeneration
IMPLEMENTATION:  drawing::registerHandlers() -- the RegenerationHandlers
                 ADR-014 mandated and nobody built -- resolving what each
                 object NAMES and never what it DRAWS (ADR-023); plus the
                 removal of a dependency edge to ObjectId{0} that had failed
                 every assembly view since P14-ASM-001
TESTS:           23 new; 2070/2070 in debug, release and debug-shared, each
                 from clean; 2069/2069 five times over in both repeat presets
VALIDATION:      the before/after measured in ONE process on ONE document;
                 the prefix asserted to agree with the qualified full
                 resolver in both directions; every number analytic from the
                 fixture's own construction
ADVERSARIAL:     2 defects found in the system under review (1 fixed, 1
                 recorded as out of scope); 3 found in my own work and fixed
WARNINGS:        0 in all three builds
DETERMINISM:     six passes identical; repeat gate clean in both presets; the
                 OneDrive fault did not recur
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-REGEN-001 marked [x], all 13 items
NEXT:            P14-CMD-001 -- drawing commands. P14-STREF-001 remains OPEN
                 on its chamfer gap and comes first if authorized.
```

## FILES

```text
qualification/qualify.cmd               the harness
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree IDs
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               2070/2070 in each preset
qualification/ctest-repeat-*.log        2069/2069 five times over, debug and release
```
