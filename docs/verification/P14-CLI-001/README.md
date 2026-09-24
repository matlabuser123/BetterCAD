# P14-CLI-001 — Headless Drawing Workflows

```text
STATUS:    PASS
MILESTONE: P14-CLI-001
DATE:      2026-09-25
BASELINE:  42df0cb (P14-PERSIST-001)
```

## TASK

Give the drawing model a command line: the same qualified operations, reached
without a window. The CLI is an adapter, never a second CAD engine.

## SCOPE

Authorized by `TODO.md`, `P14-CLI-001`. Not started here: `P14-EXPORT-001`,
`P14-REFMOD-001`, `P14-QUAL-001` or anything later.

## CLI / CORE ARCHITECTURE

**Nothing in `src/` or `include/` changed.** Every library API the command
line needed already existed, and the qualified tree IDs say so: `include` and
`src` are byte-identical to the trees `P14-PERSIST-001` qualified. The whole
milestone is in `apps/` and `tests/`.

That is the strongest statement available that the CLI is an adapter. It could
not have reimplemented a drawing semantic even by accident, because it added no
place to put one.

### The spine it plugs into

`P13-CLI-001` (ADR-009) already built the shape, and this milestone adds verbs
to it rather than a second mechanism:

```text
single-shot   load -> apply ONE edit  -> save
batch         load -> apply N edits   -> save
```

Both drive the same `EditApply` functions, so the one-line form and the
scripted form cannot disagree about what a command means. Atomicity is a
property of that shape: nothing is written until every edit has succeeded.

Each drawing edit is one `P14-CMD-001` command object executed against the
loaded document, so the CLI inherits that milestone's validation and its
all-or-nothing execution instead of restating either.

```text
CLI parse -> resolve selectors -> build a definition
          -> drawing::<X>Command::execute()  <- the qualified core
          -> save, only if every edit succeeded
```

## COMMAND SURFACE

Thirteen edit verbs, in the naming style the assembly verbs already use:

```text
sheet-add       sheet-set        sheet-remove
view-add        view-set         view-move        view-remove
dimension-add   dimension-set    dimension-remove
annotation-add  annotation-set   annotation-remove
```

and one report:

```text
drawing <file.bcad> [--configuration <name>]
```

**There are no BOM or balloon verbs, and that is `ADR-022`'s answer rather
than an omission**: a BOM table and a balloon are annotation KINDS, so
`annotation-add --type bom_table` and `annotation-add --type balloon --target
object:<occurrence>` are them. There is deliberately no verb for a row, a
quantity or an item number — none is stored, all are derived, and a CLI that
"edited" one would be writing down an answer the assembly is entitled to
change.

## REFERENCE ARGUMENTS

A drawing target is written in `ADR-012`'s vocabulary, and the grammar has
**no positional form at all**:

```text
origin:<xy|yz|xz|x|y|z>                a principal plane or axis
datum:<selector>                       a datum plane or datum axis
csys:<selector>:<xy|..|z>              a plane or axis of a coordinate system
face:<selector>:<role>[:<entity>]      a named PLANAR face
cylinder:<selector>:<role>[:<entity>]  a named CYLINDRICAL face
object:<selector>                      a hole feature or an occurrence
                                       (annotations only)
```

The first three are parsed by the mate grammar's own geometry half, so a datum
is a datum whichever subsystem names it. The last three are not the same as a
mate's `face`, and the difference is the model's: a mate names a face as a
`FaceName` whatever its surface, while a drawing must tell a planar face (a
`PlaneReference` it can measure to) from a cylindrical one (a `FaceName` it can
take a radius of).

A drawing target names **no component**: a drawing dimensions the part and a
view places it, which is `P14-STREF-001`'s finding that a dimension cannot
measure a component's faces because a component has none of its own.

`object:` is accepted for an annotation and **refused for a dimension**, with
its own message, because a hole feature names its floors and never its bore
(`P14-DIM-001`) — so there is nothing there for a dimension to measure.

## REGENERATION — AND THE GAP THIS MILESTONE CLOSED

`P14-REGEN-001` built the drawing regeneration handlers and recorded that
**nothing in production registered them**: `features::validateDocument` is
layer 2 and structurally cannot reach layer 4, so a drawing object was still
silently marked `UpToDate` in the one production path that regenerates.

This milestone is where that ends. `AssemblyReports.cpp` and
`DrawingReports.cpp` both register both modules' handlers, so
`regenerate`, `solve`, `status` and `drawing` all report a broken drawing
instead of passing over it. It is one line, and it is the line that makes
`P14-REGEN-001` reachable.

## TESTS

**27 new ctest entries**: 17 in-process cases in `tests/cli/DrawingCliTests.cpp`
and 10 process tests in `tests/CMakeLists.txt`.

```text
[cli][drawing][p14]   724 assertions in 17 new cases
```

The split is deliberate and is the answer to "can the workflow pass only
because state survived in one process":

```text
in-process   drives cli::run directly, so a failure is reported with the
             diagnostic that caused it
process      drives the BUILT EXECUTABLE, so the batch writes a file and a
             SEPARATE invocation reads it, with nothing shared but bytes on
             disk
```

### The committed workflow

`examples/scripts/build_drawing.txt` is an ordinary script a person can run,
and is what the process tests execute:

```text
sheet-add      --name Sheet1 --format A3 --scale 1:1
view-add       --name Front --sheet Sheet1 --source Extrude001 --x 150mm --y 150mm
view-add       --name Top --sheet Sheet1 --parent Front --direction top --spacing 90mm
dimension-add  --name Thickness --view Front --type linear
               --from face:Extrude001:start_cap --to face:Extrude001:end_cap --decimals 2
annotation-add --name GeneralNote --view Front --type note --text BREAK_SHARP_EDGES
```

applied to a copy of the committed `plate.bcad` as ONE transaction, and then
read back by `bettercad-cli drawing` in a new process. The expected output is
asserted in full, including the number: the plate's `thickness` parameter is
20 mm, so `Thickness` reads **20.00** — derived from the model on demand, and
nowhere in the file.

`examples/scripts/broken_drawing.txt` is its companion, and the one that
matters: three good lines, a fourth naming a view that does not exist, and a
fifth that would have worked. Exit 1, the line named, and — asserted on the
FILE rather than on the message — nothing written.

### The mapping to the checklist

| `TODO.md` item | Where |
| --- | --- |
| CLI create/load/save drawing | every edit is load-apply-save through the spine; `cli.drawing.batch` and `.report` prove it across processes |
| CLI add/remove sheets | `sheet-add`, `sheet-set`, `sheet-remove` |
| CLI create/edit views | `view-add` (object, assembly, projected), `view-set`, `view-move`, `view-remove` |
| CLI add/edit dimensions | `dimension-add`, `dimension-set`, `dimension-remove` |
| CLI annotations | `annotation-add`, `annotation-set`, `annotation-remove` |
| CLI regenerate | the existing `regenerate`/`status`/`solve`, now with the drawing handlers registered |
| CLI BOM generation | `annotation-add --type bom_table`/`--type balloon`, and `drawing` reports the rows |
| CLI export | the boundary, stated and tested — see EXPORT COMMAND BOUNDARY |
| Structured diagnostics | the existing `EditFailure`/`Error` path, with the usage-vs-rejection split extracted so it is decided once |
| Correct process exit codes | a 21-case battery in-process and 6 process tests |
| CLI/core equivalence | `EveryEditLeavesTheSameCanonicalStateAsTheCoreCommand` |
| End-to-end scripted workflow | `AWholeDrawingAppliesAsOneBatchAndTheReportAgrees`, plus `cli.drawing.batch` + `cli.drawing.report` on the executable |

## CLI / CORE EQUIVALENCE

The milestone's central claim, tested one row at a time. Two copies of one
baseline: one edited through the command line, one through the `P14-CMD-001`
command objects directly, in the same order. The canonical states are then
compared as the document's own serialization — **the same bytes**.

| Operation | CLI | core | equal |
| --- | --- | --- | --- |
| create sheet | `sheet-add --format A3 --scale 1:1` | `CreateSheetCommand` | YES |
| create view | `view-add --sheet .. --source .. --x .. --y ..` | `CreateViewCommand` | YES |
| move view | `view-move .. --x .. --y ..` | `MoveViewCommand` | YES |
| create dimension | `dimension-add --view .. --type linear --from .. --to ..` | `CreateDimensionCommand` | YES |
| create annotation | `annotation-add --view .. --type note --text ..` | `CreateAnnotationCommand` | YES |

Anything the CLI decided for itself — a default it invented, a field it filled
in differently, an ID it allocated out of order — is a difference in those
bytes and fails the test.

**And the derived results are compared too**, which byte equality does not by
itself prove: both documents are regenerated through the production path and
the measured dimension must come out the same, and equal to **40.00** — the
block's thickness, fixed by the fixture and computed by neither path.

The remaining rows are covered elsewhere rather than in that table:
`create BOM`/`create balloon` by the BOM cases (which change the assembly and
re-ask), `regenerate` by the report command sharing `features::Regenerator`
with everything else, and `save/load` by the process tests.

## EXIT-CODE CONTRACT

Unchanged from the rest of the CLI, and followed by every drawing verb:

```text
0   the operation succeeded
1   the command was read, and the document or the model refused it
2   the command line could not be read
```

The distinction is made in one place (`EditSupport.hpp`'s `fromParse`), which
this milestone extracted from `AssemblyEdits.cpp` so the two edit files cannot
come to disagree about it: `InvalidArgument` and `DimensionMismatch` mean the
CLI could not read what it was given; anything else means the model said no.

## EXPORT COMMAND BOUNDARY

**There is no drawing exporter, and nothing here pretends there is.**
`P14-EXPORT-001` owns PDF, SVG and DXF. There is also no sheet-level scene API
to write one against yet: the drawing module builds a view's projection and an
annotation's items, and `ADR-016`'s full scene — frame, layers, hatch, line
weights — is that milestone's.

So no command offers a drawing format. Asking for one is an unknown command
(exit 2) rather than a file full of nothing, and a test asserts exactly that
for `export-pdf`, `export-svg`, `export-dxf` and `export-drawing`. The MODEL
exporters (`export-step`, `export-stl`) still work on a document that carries a
drawing, which is the part of "CLI export" that exists today.

## ADVERSARIAL REVIEW

Run against the brief's twenty-one questions. **One finding changed a test
rather than the code; no production defect was found.**

| Question | Answer |
| --- | --- |
| Can the CLI mutate drawing state differently from core? | No — every edit is a `P14-CMD-001` command object, and the equivalence test compares bytes |
| Can a failed command return 0? | No — 21 in-process cases and 6 process tests, each checking the code AND that stdout is empty |
| Can batch hide a failed inner operation? | No — exit 1, the line named, and the FILE unchanged |
| Can the CLI save stale derived geometry? | There is none in the format to save (`P14-PERSIST-001`), and the CLI writes through `saveDocument` |
| Can a dimension use an unstable topology index? | The grammar has no index form: `face:3`, `index:3`, `edge:7`, `nearest:..` and `screen:..` are all unreadable targets, not fragile ones |
| Can a balloon target a part instead of an occurrence? | It is accepted and then DIAGNOSED — see below |
| Can the CLI regenerate by a separate path? | No — `features::Regenerator` with both modules' handlers |
| Can the CLI count a BOM itself? | No — `drawing::billOfMaterials()`; the test changes the assembly and re-asks |
| Can unsupported export return success? | No — unknown command, exit 2, no file |
| Can a malformed ID fall back to object 0? | No — `0`, empty, `-1`, `1.5`, `999999` and a name with a space are each refused, and the view is asserted not to have moved |
| Can a missing reference silently rebind? | No — the configuration case suppresses and restores, and the stored target never moves |
| Can a crash half-write a file? | No — `writeFileAtomically` (temp, then rename) |
| Can stdout say success while the exit code says failure? | No — every failure case also asserts stdout is empty |
| Can shell quoting corrupt a path? | A path with a space AND a non-ASCII name round-trips; the process tests invoke the executable directly rather than through a nested shell |
| Can Debug and Release print differently? | Output order is the document's ascending ID order; both presets run the same assertions |
| Can the workflow pass only inside one process? | No — the batch and the report are separate invocations of the built executable |
| Can configuration switching leave stale drawing state? | No — the report is recomputed per invocation |
| Can a command bypass the transaction rules? | No — the spine writes only after every edit has succeeded |
| Did this implement `P14-EXPORT-001`? | No — and a test asserts the absence |

### The finding: where a wrong balloon target is caught

I expected `annotation-add --target object:PartA` — a part DEFINITION where an
occurrence belongs — to be refused at create time. **It is accepted**, and my
test was wrong rather than the code.

`checkAnnotation()` says in as many words that it validates that the object a
target names EXISTS, and leaves what the object IS to the resolver, which
needs the regenerated bodies. So the command succeeds and regeneration reports
it — exactly as a view cycle in a file is reported (`P14-PERSIST-001`), and for
the same reason: create-time validation cannot do everything, and a CLI that
invented a stricter rule of its own would be a second opinion about what a
balloon may name.

What matters is that it is never silent, and that the state is the right one of
`P14-STREF-001`'s three. It is **Invalid**, not Unresolved — correct, because a
part definition cannot become a balloon target in any configuration:

```text
  Wrong (object:14)   balloon   on Assembly (object:13)   invalid
      Wrong (annotation:14) cannot be drawn: object:8 is not a hole or a
      component; an annotation can point at an object only when the object
      knows where it is
Regeneration: FAILED
```

and `bettercad-cli drawing` exits 1. The test now asserts that, and that
retargeting to the occurrence makes the same drawing sound.

### One methodological note

While checking that finding I read an exit code out of a shell pipeline
(`cli drawing m.bcad | tail -6; echo $?`), which reports **`tail`'s** status
and not the CLI's. Re-measured without the pipeline, the command exits 1 as it
should. It is the same trap recorded in `P14-BOM-001`, and it is worth
restating: a pipeline's exit status is the last command's.

## DETERMINISM

```text
repeat release  exit 0   2141 / 2141, each test run five times (987.24 s)
repeat debug    exit 0   2141 / 2141, each test run five times (1180.32 s)
```

The process tests are in that gate, which is what makes it a real one for this
milestone: each copies a pristine `plate.bcad` before running, so applying the
same script five times over is idempotent. A test that edited a committed
document in place would fail its second pass on a name the first pass took.

`TheSameScriptTwiceGivesTheSameFileAndTheSameReport` runs the same batch into
three fresh documents and requires the three reports to be identical, which
covers the object IDs, the BOM numbering and the measured values at once.

Nothing in the output depends on iteration order, an address, a timestamp or a
temporary path: every listing is in the document's ascending ID order.

**The OneDrive filesystem fault did not recur**, in either repeat preset — the
fourth milestone running. As before that does not close it: it is intermittent,
and `TODO.md` still carries the decision as due.

## FULL REGRESSION

Three presets, each configured and **built from clean**, on the frozen tree:

| Preset | Build | Warnings | No-op rebuild | Tests |
| --- | --- | --- | --- | --- |
| `debug` | exit 0 | 0 | compiled 0, linked 0 | **2142 / 2142** (291.21 s) |
| `release` | exit 0 | 0 | compiled 0, linked 0 | **2142 / 2142** (224.65 s) |
| `debug-shared` | exit 0 | 0 | compiled 0, linked 0 | **2142 / 2142** (248.37 s) |

```text
qualification finished Fri 25/09/2026 4:25:19.84, 0 stage(s) failed
```

Baseline was 2115 ctest entries; this adds **exactly 27** — 17 in-process cases
and 10 process tests. Nothing was written and left undiscovered.

CLI failures propagate the whole way: a drawing command's non-zero exit fails
its ctest entry, which fails the preset's `ctest` stage, which increments the
harness's failed-stage count, which becomes its exit code.

### The harness

`verify-harness.cmd` was run before the qualification and passes: pointed at a
preset that does not exist it reports `QUALIFICATION FAILED: 3 stage(s) failed`
and gives `qualify.cmd` exit 3.

Worth recording: a bare `ctest --preset debug` fails `cli.new.unicode-path`,
and the harness's run does not. The harness sets `chcp 65001`, which that test
needs — which is why the code page is set there rather than assumed. The
failure was reproduced, explained and re-run under the harness rather than
dismissed.

### The qualified tree is the committed tree — and proves the central claim

Tree IDs from a scratch index, before the first build, after the last test run
and again before the commit — identical in all three:

```text
apps              651980365937f53cdb3119c6031863f695adc762
include           494a26546a2feb0c36d34f14a50ccaddcc8ed430
src               ecb006508b42832b6ae347aa4ffe82b691e57b2a
tests             0130eb9d8dfc68ae0a75a3ae7992d1a7b4361d83
examples          e7aa13bd4ee36e22dc31786daa2d9afc4580b2e9
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

**`include` and `src` are byte-identical to the trees `P14-PERSIST-001`
qualified** (`494a2654…`, `ecb00650…`), which are in turn the trees
`P14-CMD-001` qualified. Three milestones now share them. That is not a claim
in prose that the CLI added no library code — it is the same hash,
independently computed, across three qualifications.

What changed: `apps` (the verbs), `tests` (the suite) and `examples` (the two
committed scripts). `cmake`, `CMakeLists.txt` and `CMakePresets.json` are
unchanged back to `P14-REGEN-001`.

## KNOWN LIMITATIONS

**`view-add` makes base, projected and assembly views; not section, detail or
auxiliary.** Those need a cutting plane, a region or a direction pair on the
command line, which is a vocabulary design of its own rather than three more
options, and the brief's "where practical" is the room this takes. They are
reachable through the core commands and through a file; only the command line
is short of them.

**`sheet-set` sets one margin for all four edges.** The model holds four, and a
per-edge spelling is a small extension nobody has needed yet.

**There is no `drawing-export`.** `P14-EXPORT-001` owns it, and there is no
sheet-level scene API to write one against; see EXPORT COMMAND BOUNDARY.

**`validate` still does not see drawings.** It goes through
`features::validateDocument`, which is layer 2 and cannot reach layer 4 —
`regenerate`, `solve`, `status` and `drawing` all do. Closing that means
moving validation up a layer, which is not this milestone's.

**The CLI does not use `CommandHistory`.** That is `ADR-009`'s design, not an
oversight: the transaction is the load-apply-save shape, and a session's undo
stack has no meaning across processes. `P14-CMD-001` records the same boundary.

**Carried from `P14-STREF-001`, unchanged:** "no silent rebinding" is still not
met for chamfer faces, named by position in the chamfer's edge list. The CLI
can name such a face (`face:<feature>:chamfer:<n>`) and inherits the gap
exactly; it does not widen it.

## RESULT

```text
TASK:            P14-CLI-001 -- Headless drawing workflows
IMPLEMENTATION:  13 drawing edit verbs and one report, added to P13-CLI-001's
                 existing edit spine. Each verb is one P14-CMD-001 command
                 object, so the CLI inherits that milestone's validation and
                 atomicity rather than restating either. NO LIBRARY CODE
                 CHANGED: include and src are byte-identical to the trees the
                 last two milestones qualified
TESTS:           27 new ctest entries (17 in-process, 10 on the built
                 executable); 2142/2142 in debug, release and debug-shared,
                 each from clean; 2141/2141 five times over in both repeat
                 presets
VALIDATION:      CLI/core equivalence compared as the document's own
                 serialization, byte for byte, across five operations, with
                 the derived measurement compared too; the end-to-end workflow
                 run through the BUILT EXECUTABLE in two separate processes
ADVERSARIAL:     0 production defects; 1 finding that corrected a test rather
                 than the code; 1 methodological note (a pipeline's exit
                 status is the last command's)
WARNINGS:        0 in all three builds
DETERMINISM:     three runs of one script give one report; repeat gate clean
                 in both presets
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P14-CLI-001 marked [x], all 15 items
NEXT:            P14-EXPORT-001 -- PDF / SVG / DXF Export. P14-STREF-001
                 remains OPEN on its chamfer gap.
```

## REVISION

One revision, and it was to a test rather than to the code: I expected a
balloon aimed at a part definition to be refused when the command ran, and it
is accepted and then diagnosed at regeneration, because `checkAnnotation()`
validates that the named object EXISTS and leaves what it IS to the resolver.
The test now asserts the real contract — Invalid, reported, exit 1 — and that
retargeting to the occurrence makes the drawing sound.

## FILES

```text
qualification/qualify.cmd               the harness
qualification/verify-harness.cmd        its exit-code regression; run and passing
qualification/run-qualification.cmd     the entry point and its repeat selection
qualification/qualification-times.txt   every stage, its exit code, and the tree IDs
qualification/build-*.log               three presets, 0 warnings each
qualification/rebuild-*.log             the fresh-binary proof
qualification/ctest-*.log               2142/2142 in each preset
qualification/ctest-repeat-*.log        2141/2141 five times over, debug and release
```

The two committed scripts live with the examples rather than with the tests,
because they are meant to be run by a person:

```text
examples/scripts/build_drawing.txt      the workflow
examples/scripts/broken_drawing.txt     the one that stops, and writes nothing
```
