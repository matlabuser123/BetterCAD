# P13-CLI-001 — Headless Assembly Workflows

```text
TASK:      P13-CLI-001 — an assembly built, edited and solved without a window
STATUS:    PASS
BASELINE:  ab9c015 (authorization), on top of fcd3a81 (P13-PERSIST-001)
ADR:       ADR-009 — a CLI edit is a document transaction, and a batch is one of them
```

## Task

Turn the CLI from a **read, report and export tool** into an **editor**, without
building a second assembly implementation inside it.

The starting point, measured rather than assumed: the CLI was 1343 lines with
seven commands (`new`, `info`, `validate`, `export-step`, `export-stl`,
`version`, `help`), and the only `io::saveDocument()` call in all of it was in
`new`, which writes an empty document. Every feature CLI test in the repository
invoked `info` or `validate` and nothing else — documents were built in process
by the test and the CLI was asked to describe them.

Eight of the seventeen checklist items were commands with no ancestor anywhere
in the codebase.

## Scope

In: the CLI command contract; create/load/save; component add, remove and
placement; mate create, edit and delete across all eleven kinds;
configuration and suppression; regenerate and solve; status and diagnostics;
deterministic scripted workflows; exit codes; structured errors; the
save → load → regenerate → solve round trip; CLI/core equivalence; malformed
input; failure atomicity.

Out, and deliberately: `P13-STEP-001`, `P13-REFMOD-001`, `P13-QUAL-001`. No
undo across processes (see below). No second assembly model, no validation
restated in CLI code, no solver arithmetic in CLI code.

## Architecture

Three questions had to be answered before any command was written, and each
had a wrong answer that would have been hard to take back once files existed
that depended on it. They are settled in
[ADR-009](../../architecture/decisions/ADR-009-the-cli-edit-is-a-document-transaction.md);
what follows is what was decided and why it is safe.

### How a script names what it created two lines ago

The obvious answer — by name — is the one this project rejected in ADR-003,
and `P13-REF-001` has a qualified test called
`Reference_IdentityDoesNotFollowNames` to that effect.

But reading the document rather than assuming turned up something stronger
than "names are not identity":

```text
validateIdentifier()    a name is [A-Za-z_][A-Za-z0-9_]* , at most 64 chars
requireNameAvailable()  a name is unique across objects AND parameters
Document::rename()      re-checks both on every rename
```

So a decimal selector can only ever be an **ID**, because no name can be
decimal; and a selector starting with a letter can only ever be a **name**.
The two sets are disjoint **by construction**, not by a convention the parser
maintains — no sigil, prefix or escape is needed, and there is no ambiguous
case to decide.

Identity stays with the ID. A name is resolved at the moment of use and never
stored. Because the guarantee is load-bearing, it is pinned by its own test
(`AssemblyCli_NamesAndIdsCannotOverlapByConstruction`) rather than assumed: if
anyone relaxes the identifier rule to admit a leading digit, that fails before
a file exists that depends on it.

### One process per edit, or a batch

Per-invocation atomicity is **not** script atomicity, and that distinction is
the whole reason this milestone exists. A twenty-step script that fails at
step seven would leave the first six edits on disk — a file that is neither
the document the script describes nor the one it started from, with nothing in
it saying so.

So: both, through **one mechanism**.

```text
single-shot   load -> apply one edit  -> save
batch         load -> apply N edits   -> save
```

An edit is a function over an already-loaded document
(`EditResult (*)(Document&, Args)`), and the single-shot command is literally
the batch of one — the same `EditApply` behind a different driver. The two
cannot disagree about what a command means, because they are one path.
Atomicity then follows from the shape rather than from care taken at each call
site: **nothing is written until every edit has succeeded.**

The batch language is the CLI's own command lines, minus the document path.
No second grammar, no second parser, no second set of option names — `batch`
tokenises a line, looks it up in the same table the top-level dispatcher uses,
and calls the same function. The only syntax a batch file adds is `#` for a
comment and `"` for an argument containing spaces.

### What runs the assembly solve

`features::validateDocument()` builds a `Regenerator` and does **not**
register the assembly handlers — it cannot, because `features` is layer 2 and
`assembly` is layer 3, and ADR-006 and ADR-008 put that registration in the
caller's hands on purpose.

So `regenerate`, `solve` and `status` are where the CLI acts as the
**composition root**, building the regenerator and registering the assembly
handlers on it. It is the one component allowed to see both layers.

A consequence worth stating rather than discovering later: those three
commands **never write the file**. ADR-005 makes a transform derived, so there
is nothing for them to persist. They report, and their exit code says whether
the assembly is sound. That is asserted on the bytes, not on the message.

## Implementation

Six new files in `apps/bettercad_cli/`, and **189 inserted lines against 2
deleted** across the five existing files they are wired into.

The single largest fact about this milestone's regression risk is in the
qualified tree IDs: **`src` and `include` are byte-identical to
`P13-PERSIST-001`.** Not one library source file and not one public header was
touched. Everything is in `apps/`, `tests/` and `examples/`, so every library
the CLI drives is exactly the one already qualified.

| File | Lines | What it is |
| --- | --- | --- |
| `Selectors.hpp` / `.cpp` | 88 / 392 | The selector grammar, the mate-target grammar, and the formatter that writes a target back in the grammar the parser accepts |
| `Edits.hpp` | 74 | The edit spine: `EditFailure`, `EditResult`, `EditApply`, `EditCommand`, and the single-shot driver both forms share |
| `AssemblyEdits.cpp` | 713 | The eleven edits, and the table the dispatcher and the batch driver both look them up in |
| `AssemblyReports.cpp` | 325 | `regenerate`, `solve`, `status` -- and the composition root that joins `features` to `assembly` |
| `BatchCommand.cpp` | 186 | The tokeniser and the transaction driver |

Wired in through: `CMakeLists.txt` (+6, the new sources), `Cli.cpp` (+23, the
four report commands in the dispatch table, the edits looked up ahead of it,
and the edits listed in `--help`), `Commands.hpp` (+12, four usage strings and
four declarations), `tests/CMakeLists.txt` (+134, the new test file and the
process tests), and `tests/cmake/RunAndCheck.cmake` (+16, an optional pre-run
copy so that a process test which EDITS a document is repeatable -- see
adversarial review Finding 3).

Also added: `examples/scripts/build_assembly.txt` and
`examples/scripts/broken_assembly.txt` -- the workflows the process tests run,
committed so that a reader can see what a headless build looks like and so the
failing one is a fixture rather than a string in a test.

## The exit-code rule

Two failing codes, and a rule that says which, rather than whichever the code
path happened to produce:

```text
2   the CLI's own parser could not read the line
    an unknown word, a value that is not a quantity, a unit of the wrong
    dimension, a selector that is neither an ID nor a name

1   the line read fine, and the DOCUMENT or the MODEL rejected what it asked
    no object of that name, the wrong kind of object, a constraint the mate
    type does not take, a component still mated
```

It falls out of the error codes the layers already use: the CLI's parsing
failures are exactly the `InvalidArgument` and `DimensionMismatch` ones, while
resolution against the document answers `NotFound` or `FailedPrecondition`,
and core validation results never go through that mapping at all.

`0` is success. A solve that ends **under-** or **fully constrained** is an
answer and exits 0; **over-constrained**, **inconsistent** and **solver
failure** exit 1, and the first of those because the solver publishes no
transforms for it — there is no result to report, and the redundant mates it
names are the thing to fix.

## Commands added

Seven commands became twenty-two.

**Edits** -- each loads, applies and saves as one transaction; nothing is
written unless the edit succeeded.

```text
component-add <file> --part <selector> [--name <name>]
              [--x|--y|--z <length|parameter>] [--rx|--ry|--rz <angle|parameter>]
component-place <file> <selector> [same placement options]
component-remove <file> <selector>
mate-add <file> --type <kind> [--name <name>] [--component <selector>]
         [--a|--b|--a2|--b2 <target>] [--distance <length>] [--angle <angle>]
mate-set <file> <selector> [same, minus --name]
mate-remove <file> <selector>
suppress | unsuppress <file> <selector> [--configuration <name>]
suppress-clear <file> <selector> --configuration <name>
configuration-add <file> <name>
configuration-activate <file> <name>|--none
```

**Reports** -- these write nothing at all:

```text
regenerate <file> [--configuration <name>]
solve       <file> [--configuration <name>]
status      <file> [--configuration <name>]
```

**Scripts**:

```text
batch <file> <script> [--dry-run]
```

### The grammars

A **selector** is decimal digits (an ID) or an identifier (a name resolved
now). The two cannot overlap, because `validateIdentifier()` forbids a leading
digit.

A **mate target** is a component and its geometry, and the same text a report
prints is the text a command accepts:

```text
<component>:origin:<xy|yz|xz|x|y|z>        a principal plane or axis of the part
<component>:datum:<selector>               a datum plane or datum axis
<component>:csys:<selector>:<xy|..|z>      a plane or axis of a coordinate system
<component>:face:<selector>:<role>[:<entity>]
```

A **placement value** is a quantity or a parameter name, and those cannot be
confused either: a quantity starts with a digit, a sign or a point, and a name
never does. `--z 50mm` is a literal; `--z lift` binds the parameter, and the
binding is what the file stores.

All eleven mate kinds are reachable: `fixed`, `coincident`, `concentric`,
`parallel`, `perpendicular`, `distance`, `angle`, `revolute`, `slider`,
`cylindrical`, `planar`.

## Tests

**56 new cases** in `tests/cli/AssemblyCliTests.cpp`, and **14 new
process tests** in `tests/CMakeLists.txt` that launch the real executable. The
suite goes from 1527 to 1597.

Every in-process case checks the exit code the process would return:
`runCliCommand()` calls `cli::run()`, which is exactly the value `main()` casts
to its return value. The process tests then measure the real thing --
`execute_process`, a real exit status, a real file on disk -- because a
milestone whose subject is exit codes and files should not be measured only
through a function call.

| Group | Cases | What they pin |
| --- | --- | --- |
| The selector contract | 4 | IDs and names cannot overlap; both forms reach the same component; identity does not follow a rename |
| Create, save, load | 4 | A component survives the file; the assigned ID is usable as a selector; unnamed components are named deterministically; a sketch is refused as a part |
| Placement | 4 | Only the axes given change; a parameter binding persists and drives the solve; a wrong dimension and a missing parameter are refused differently |
| Mates | 7 | All **eleven** kinds through the CLI; an unknown kind; a slider without its roll reference; in-place editing; a value the kind does not take; removal leaving its components |
| The target grammar | 3 | Five target forms round-trip through `status`; five malformed targets; a target on a missing component |
| Configurations and suppression | 5 | Three suppression states in a configuration; base state distinct from an override; `suppress-clear` needs a configuration |
| Regenerate, solve, status | 6 | Status and DOF; an inconsistent assembly as a failure naming its conflicting mates; a configuration changing the answer; **all three writing nothing**, checked on the bytes |
| Scripted workflows | 12 | One transaction; **a failing script writing nothing at all**; `--dry-run`; comments and blank lines; quoting; an unterminated quote; an unknown edit; a report command refused as an edit; a usage error keeping its own code; **a later line seeing an earlier line edit**; removing and recreating within one batch; a script of only comments |
| Equivalence and round trip | 5 | Single-shot equals batch; CLI equals core API; save/load/regenerate/solve exactly; a name already in use; a repeated edit being byte-identical |
| Malformed input | 6 | A missing file; a corrupt file; every edit needing a document; help listing every command; an assembly whose part went missing |

### The cases that carry the most weight

`AssemblyCli_BatchThatFailsWritesNothingAtAll` is the one the milestone exists
for. Three edits succeed, the fourth fails, and the assertion is on
`readFile(path)` before and after -- not on the exit code, because a command
can report failure and still have written something. It also checks that
nothing was reported as applied, so the list cannot be mistaken for work that
happened.

`AssemblyCli_NamesAndIdsCannotOverlapByConstruction` pins the property the
whole selector grammar rests on, directly rather than through the CLI: every
decimal string is refused by `validateIdentifier()`, and every name starts with
a non-digit. If that rule is ever relaxed, this fails before a file exists that
depends on the disjointness.

`AssemblyCli_RegenerateAndSolveAndStatusWriteNothing` asserts ADR-005 from the
outside, on the bytes of the file, for all three report commands.

`AssemblyCli_IdentityIsTheIdAndDoesNotFollowARename` renames a component
through the core API and then shows the old name resolves to nothing while the
ID still reaches the same component.

## Independent validation

### The expected values are hand-derived, not read back from the solver

Two 40 x 30 x 10 mm blocks. `Base` is held by a `fixed` mate, so it contributes
no unknowns; `Arm` is free, so the system has 6.

```text
no constraint besides Ground      6 unknowns - rank 0  =  6 DOF
plane-to-plane coincidence        6 unknowns - rank 3  =  3 DOF
```

A coincidence between two planes removes one translation (along the shared
normal) and two rotations (the tilts), leaving two in-plane translations and
the spin about the normal. Both figures are asserted by name, in process and
through the real executable.

The transform is checked the same way. `Arm` is *placed* 50 mm up as intent;
the mate pulls it onto `Base`'s plane at z = 0, and `solve` prints
`origin (0, 0, 0) mm`. Suppress that mate in a configuration and the constraint
is gone, so `Arm` returns to `origin (0, 0, 50) mm` -- its placement intent,
which the solve never overwrote. That is ADR-005 observed from outside the
process, and it is asserted in both `AssemblyCliTests.cpp` and
`cli.assembly.solve.configuration`.

### CLI / core equivalence, exactly

The gate's "CLI/core equivalence" has a precise meaning since
`P13-PERSIST-001`, and it is used: the same assembly is built twice from the
same starting document -- once through the CLI, once in process through the
`P13-CMD-001` command objects -- and the two are compared with
`equivalent(Document, Document)`, which covers identity, name, metadata,
parameters, configurations and every object. A *nearly* identical document
fails there.

Then both are solved, and the transforms are compared **bit for bit**: the full
3x3 matrix by `==`, and each translation component by its SI value, with no
tolerance. `AssemblyCli_BuildsTheSameAssemblyAsTheCoreApi`.

### Atomicity, measured on the file rather than the message

Proved twice, two ways. In process, `readFile()` before and after a batch whose
fourth line fails -- the string compares equal. Through the real executable,
`cli.assembly.batch.fails` asserts exit status 1 and the diagnostic, and then a
*separate* process test asks the CLI what the document holds and requires
`Components (0)` and `Mates (0)` -- so the claim rests on the file's contents
after a real process exited, not on what the failing process said about
itself.

## Adversarial review

**Five findings: one production defect in this milestone's own new code, two
defects in its test wiring, and two wrong expectations of mine that the tests
caught.** All five are fixed. No defect was found in any previously qualified
code.

Two of the five were found by gates rather than by reading: the repeat stage
of the qualification failed and had to be diagnosed, fixed, and the whole
qualification re-run from clean. That is recorded below rather than quietly
absorbed, because the first qualification run is void and the evidence should
say so.

### Cleared

| Question | Answer |
| --- | --- |
| Can a name become the identity a file relies on? | No. A name is resolved to an `ObjectId` at the moment of use and never stored; the disjointness that makes the lookup unambiguous is a property of `validateIdentifier()` and is pinned by its own test |
| Can a selector be ambiguous between an ID and a name? | Structurally impossible: a name must start with a letter or `_`, so no decimal string is a legal name |
| Can a failed edit leave a partial document on disk? | No, and it is measured on the bytes rather than the exit code -- in process, and through the real executable by asking the CLI afterwards what the document holds |
| Can a crash during the one write that does happen corrupt the file? | No. `saveDocument()` goes through `writeFileAtomically()`, which writes a `.tmp` and renames. The transaction rests on two guarantees, not one: the driver writes nothing unless every edit succeeded, and that single write commits atomically |
| Can `regenerate`, `solve` or `status` persist derived state? | No -- asserted on the file's bytes for all three. ADR-005 |
| Can a mate target bind to raw kernel topology? | Not by check but by type: `parseMateTarget()` can only build a `PlaneReference`, an `AxisReference` or a `FaceName`. There is no field a `geometry::FaceSignature` could occupy (ADR-004) |
| Can the CLI's validation drift from the model's? | There is none to drift. Every edit goes through a `P13-CMD-001` command object or an `assembly::` entry point, and the one refusal the CLI does make -- removing a mated component -- asks `matesOf()`, which exists for that question |
| Can the single-shot and batch forms disagree? | They are one code path (`EditApply`), and a test builds the same assembly both ways and compares every component and mate definition |
| Does the CLI produce a different document from the core API? | No -- `equivalent(Document, Document)` over identity, name, metadata, parameters, configurations and objects, and then transforms compared bit for bit |
| Can a batch line fail to see an earlier line's edit? | No -- a script that creates, moves twice and then mates the same component is asserted, and so is one that removes what it created and reuses the name |
| Can an unresolved reference be reported as a solver outcome? | No. `assembly::solve()` fails structurally for one, and the CLI reports it separately from any `SolveStatus` (ADR-004) |
| Can an edit that changes nothing churn the file? | No -- repeating an edit is byte-identical, because serialization is canonical |
| Is there hidden global state? | No. The edit table is a `constexpr std::array`; each report command builds its own `Regenerator`; there are no statics and no caches |
| Did the change cross an architectural boundary? | No. The CLI is an application above every module and already linked `assembly`; `architecture.layering` runs in all three presets |
| Was any qualified code modified? | No. 152 inserted lines, 0 deleted, across four files, all of it wiring |

### Finding 1 -- `solve` reported a clean answer for a document that had not built

**A real defect in this milestone's own new code, found by reading the diff
before the tests ran.**

`runSolve()` regenerated, then solved, and never looked at whether the
regeneration had succeeded. Most mate targets -- every `origin:` one -- need no
body at all, so an assembly whose *part* failed to build would still solve, and
the command would print transforms and exit 0 for a document that cannot be
built.

`status` did not have the bug: it prints the regeneration report and fails on
it. `solve` did, and `solve` is the one a script would gate on.

Fixed: `solve` now prints the regeneration report and fails before solving
anything when the document did not regenerate, with the reason stated in the
code rather than left implicit.

### Finding 2 -- the process tests passed only because of the order they happened to run in

**A latent trap in the test wiring, found by trying to break it.**

The assembly process tests were ordered with `DEPENDS`: the batch that builds
an assembly before the `solve` that reads it. That is correct for a full run
and wrong for a filtered one, because **ctest drops a `DEPENDS` on a test the
filter excluded**, while it *does* pull in a required fixture's setup.

Deleting `built.bcad` and running `ctest -R cli.assembly.solve` reproduced it:
the copy ran, the build did not, and `solve` failed against an empty plate.
Nobody would have hit it in a full run; the first person to rerun one failing
test would have.

Fixed by making the two commands that *produce* a document part of the fixture
itself rather than merely ordered before their readers. The same filtered
command now pulls in all four setup tests and passes. They remain ordinary
tests, with their own exit codes and output asserted.

### Finding 3 -- the process tests were not repeatable

**Found by the qualification's own repeat stage, which is what it is for.**

`ctest --repeat until-fail:5` re-runs a test five times; it does **not** re-run
that test's fixture. `cli.assembly.batch` edits a document in place, so the
second pass ran `component-add --name Base` against a document that already had
a `Base`, and it failed -- correctly, for a reason that had nothing to do with
the code under test. Nine dependent tests were then `Not Run`.

```text
repeat release exit 8
    1527 - cli.assembly.batch (Failed)
    1528 - cli.assembly.solve (Not Run)
    ... 8 more
```

The standard was already there and my tests broke it: every process test this
repository had was idempotent by construction -- `cli.new` passes `--force`,
the exports overwrite, `validate` is read-only.

Fixed by giving the process-test harness an optional pre-run copy
(`COPY_FROM`/`COPY_TO` in `bettercad_add_process_test` and `RunAndCheck.cmake`),
so a test that edits a document starts from a pristine input every time. The
two tests that build a document now copy their own, which also let the two
separate setup tests be deleted -- twelve process tests instead of fourteen,
and every one of them repeatable.

Verified: the twelve pass `--repeat until-fail:5`, and all seventy-nine process
tests in the repository pass `--repeat until-fail:3`, so the shared harness
change broke nothing.

**The first qualification run is void.** It had passed all three presets
(1597/1597 each, 0 warnings) before the repeat stage failed, but the tree
changed afterwards, so the whole thing was re-run from clean. Only the second
run is cited below.

### Finding 4 -- a test asserted a capability the model deliberately refuses

I wrote a case requiring a configuration to be nameable `"Left hand"`, to
exercise the batch tokeniser's quoting. It failed, and the code was right:
`Configurations.hpp` says configuration names are identifiers **"so that they
can be typed on a command line without quoting"**.

Worth recording rather than just fixing, because it changes what the feature is
for: **no name in this model ever needs quoting.** The one thing that does is a
quantity written with a space before its unit -- `--z "50 mm"` -- which the
argument parser accepts and the tokeniser would otherwise split in two. The
corrected test pins that, and also that the unquoted form is *refused* rather
than silently misread. ADR-009 records it.

### Finding 5 -- an exact comparison on a value that cannot be exact

`CHECK(placement.rotation[2].in(units::deg) == 30.0)` failed with
`29.999999999999996`. An angle is held in radians, so a degrees round trip
passes through pi and does not return bit-exact.

The test was wrong, not the code. Replaced with `WithinAbs(30.0, 1e-12)` -- the
well-conditioned double-precision figure CLAUDE.md names -- with the reason
written beside it. The lengths in the same case keep their exact comparisons,
because millimetres do round-trip exactly through metres and the stronger
assertion is worth keeping.

### What these tests do not prove

Stated so the evidence does not overclaim.

The CLI/core equivalence case builds both documents **from the same starting
file, through the same command objects**. It proves the CLI introduces no error
of its own; it does not re-prove that those commands are correct, which is
`P13-CMD-001`'s evidence.

Nothing here measures concurrent access. Two processes editing one file will
both load, both edit and both save, and the second rename wins -- last writer
wins, silently. That is the behaviour every command in the CLI has always had,
it is not changed by this milestone, and it is recorded below rather than
implied to be safe.

## Known limitations

Recorded because they are decisions or gaps, not because anything here failed.

**No undo across processes.** The edits go through the `P13-CMD-001` command
objects for their validation and their all-or-nothing execution, but a one-shot
process has nowhere to keep a `CommandHistory` -- it would be state with no
reader. `P13-CMD-001` measured undo as deliberately transient and it stays
transient. A batch is the unit of recovery instead: it either all happened or
none of it did.

**Last writer wins.** Two processes editing one document both load, both edit
and both save, and the second rename overwrites the first's work with no
warning. No locking, no detection. This is not new -- it is true of every
command the CLI has ever had -- but this milestone is the first that makes
concurrent *writing* easy, so it is worth stating.

**A copied face cannot be named from the CLI.** The target grammar covers a
face's feature, role and profile entity. `FaceSelector::copies` -- the face a
pattern or mirror made -- is reachable through the core API and has no spelling
here. `formatMateTarget()` marks such a target with a trailing `:...` rather
than printing something that would parse back to a *different* face, which
would be worse than admitting it cannot.

**`validate` still does not run the assembly final pass.** It builds a
`Regenerator` inside `features`, which cannot register assembly handlers
(layer 2 cannot see layer 3). Changing that needs either a move the layering
forbids or an injection point in `features::validateDocument()` -- a change to
a qualified subsystem, and not authorized here. `status` is the command that
does register them, and it reports everything `validate` would plus the solve.

**An external part cannot be added from the CLI.** `component-add --part`
resolves an object in the same document. An `ObjectReference` to another
document needs that document's `DocumentId`, which ADR-003 forbids fetching
behind the caller's back. Internal references are what ADR-003 says to build
first, and the reader already handles external ones.

**A placement's parameter binding is not dimension-checked at bind time.**
`--z some_angle_parameter` is accepted and the file saves; the mismatch is
reported by `resolvePlacement()` when the placement is resolved, which is where
the model puts that rule. Restating it in the CLI would be a second place for
it to be wrong. The cost is that the failure surfaces at `solve` rather than at
the edit, and `solve` says exactly what is wrong.

**No schema or CLI-version negotiation.** A script is text and the CLI is
whatever build is on the path. Nothing records which version a script was
written against.

## Regression

Three presets, each configured, cleaned to nothing and rebuilt from scratch
before its tests ran. Every stage's exit code is in
`qualification/qualification-times.txt`; **all fourteen are 0**, and none was
skipped.

| Preset | Targets | Compiler warnings | Tests | Build | Tests |
| --- | --- | --- | --- | --- | --- |
| `debug` | 446/446 | 0 | **1595/1595** | 16 m 46 s | 352.2 s |
| `release` | 446/446 | 0 | **1595/1595** | 21 m 06 s | 275.7 s |
| `debug-shared` | 446/446 | 0 | **1595/1595** | 17 m 19 s | 289.6 s |

Then the milestone's related tests, five times over until failure -- 1027
tests selected by the CLI, command, batch, script, argument, selector,
assembly, component, mate, placement, solve, mechanical, configuration,
suppression, regeneration, reference, resolution, persistence, file, JSON,
save, load, serialization, example-model, undo, redo, document, object,
parameter, expression, sketch, datum and architecture names:

| Preset | Tests | Time |
| --- | --- | --- |
| `release` | **1027/1027 x5** | 1114.0 s |
| `debug` | **1027/1027 x5** | 981.4 s |

1595 = the 1527 of `P13-PERSIST-001` plus this milestone's 56 in-process cases
and 12 process tests. All 68 are confirmed **by name** in each of the three
ctest logs, so the count is not taken on trust.

**Qualified tree.** Recomputed from the working tree after the run, the tree
IDs are identical to the ones the harness recorded before building, so the tree
that was qualified is the tree that is committed:

```text
apps      b6f2905858c43e20eda367abe36630dc01f34f17
include   85b546819f73aa2f155e88904975d3dace4d5cd8   <- same as P13-PERSIST-001
src       22eebd5895e5be02e450dce01ef6d49c0422b0f4   <- same as P13-PERSIST-001
tests     fc0dbce113c30d00a9e8fa1b7201d62b7748ef4a
examples  0ef8346d0ff59a2101ed1fa61e452c06a849f14f
cmake     a84e909339b24bc7ffca591888e10d48a3e5296c   <- unchanged
```

`src` and `include` matching `P13-PERSIST-001` exactly is the strongest
statement available about this milestone's regression risk: **no library
source file and no public header was touched at all.** Every module the CLI
drives is bit-identical to the one already qualified, and the 1595 tests ran
against it.

**This is the second qualification run.** The first passed all three presets
and then failed its repeat stage on a real defect in the test design (Finding
3). Fixing it changed `tests/`, which voids a qualification, so the whole thing
was run again from clean. Only this run is cited; the first one's numbers
appear nowhere in this document except in that finding.

`ninja: warning: premature end of file; recovering` heads each build log. It is
ninja's own `.ninja_log`, damaged when runs were killed during
`P13-SOLVE-001`, and it makes ninja rebuild **more** rather than less -- all
446 targets were built in every preset regardless. It is not a compiler
warning, and none appears in any of the three build logs.

## Result

```text
TASK:            P13-CLI-001 -- Headless assembly workflows
IMPLEMENTATION:  15 new commands (7 -> 22) in 1778 lines of new CLI source,
                 plus 189 inserted / 2 deleted lines across 5 existing files.
                 src/ and include/ BYTE-IDENTICAL to P13-PERSIST-001.
TESTS:           56 in-process cases + 12 process tests on the real
                 executable; 1527 -> 1595
VALIDATION:      DOF and transforms hand-derived, not read back from the
                 solver; CLI vs core API compared by equivalent(Document) and
                 by BIT-IDENTICAL transforms; atomicity measured on the
                 file's bytes, including after a real process exited
REGRESSION:      1595/1595 on debug, release and debug-shared, each from
                 clean; 1027/1027 five times over in release and debug;
                 0 compiler warnings in all three builds; all 68 new tests
                 confirmed by name in every ctest log; 14/14 stages exit 0
ADVERSARIAL:     5 findings, all fixed -- 1 production defect in this
                 milestone's own new code, 2 test-wiring defects, 2 wrong
                 expectations of mine. 0 defects in previously qualified code
RESULT:          PASS
EVIDENCE:        this directory
```

What is claimed: an assembly can be built, edited, configured, regenerated and
solved entirely from a command line, through the same model, commands,
persistence, references and solver everything else uses; a script of edits is
one transaction that either all happens or none of it does, proved on the
file's bytes; exit codes follow a stated rule rather than an accident of code
path; a document built through the CLI is *indistinguishable* from one built
through the core API, and solves to the same transforms bit for bit.

What is **not** claimed: that concurrent edits are safe -- they are last writer
wins, silently, as they always have been; that undo survives a process, which
it deliberately does not; that a copied face can be named from this interface;
or that `validate` runs the assembly final pass, which it still does not and
which would take a change to a qualified subsystem that is not authorized here.

**The shape of this milestone is worth stating plainly.** It is the largest
surface addition in several milestones -- eight of its seventeen items were
commands with no ancestor in the codebase -- and it was done without touching
one line of `src/` or `include/`. The CLI became an editor by composing what
the six milestones before it had already built and qualified: the assembly
model, the command objects, the configuration system, the reference resolver,
the regenerator, the solver and the file format. The only genuinely new ideas
are two grammars whose halves cannot overlap, and a transaction shape in which
the one-line form *is* the batch of one.

## Revision

| When | What |
| --- | --- |
| 21/09 ~21:00 | ADR-009 written: the selector rule, the transaction shape, the composition root |
| 21/09 ~21:40 | implementation complete; hand-verified end to end against the real binary |
| 21/09 ~22:10 | `solve` found not to gate on regeneration (Finding 1); fixed |
| 21/09 22:30 | first qualification started |
| 21/09 23:49 | **repeat release exit 8** -- the process tests were not repeatable (Finding 3) |
| 21/09 23:53 | harness given a pre-run copy; two setup tests deleted; 79 process tests verified x3 |
| 21/09 23:56 | first run void; qualification re-run from clean |
| 22/09 01:42 | 14/14 stages exit 0; qualified tree verified against the working tree |
