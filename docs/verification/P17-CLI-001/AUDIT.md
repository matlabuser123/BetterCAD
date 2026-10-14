# P17-CLI-001 — the CLI architecture audit

```text
SUBJECT:  what bettercad-cli already is, what P17 may reuse, and the six
          questions the brief asked me to decide that the project had
          already decided
RAN:      before any production code was written, and before any command
          was named
```

**The brief says "audit actual P16 naming and choose names consistent with
it" and "do not invent names before this audit".** This is that audit. Its
main finding is the same as the last two milestones': nearly every question
the brief raises already has an answer in the tree with a recorded reason, and
one answer differs from what the brief suggests.

## The capability table

| Capability | Existing type / API | Where | Reusable for P17? | Decision |
| --- | --- | --- | --- | --- |
| dispatcher | `cli::run` then `dispatch`, a `constexpr std::array` of `Command` | `Cli.cpp` | yes | add six report rows |
| exit codes | `ExitCode{Success=0, Failure=1, UsageError=2}` | `Cli.hpp` | yes | **three, not six** — see below |
| argument parsing | `parseArguments(args, {{"--flag", takesValue}})` | `Arguments.hpp` | yes | reuse |
| the edit spine | `EditCommand`, `EditApply`, `runEdit`, `editCommands()` | `Edits.hpp`, `EditRegistry.cpp` | **yes** | add a `structuralEditCommands()` table |
| edit failure | `EditFailure{usage, code, message}` | `Edits.hpp` | yes | `rejected`/`malformed`/`coded`/`fromParse` |
| command execution | `execute(document, command)` | `EditSupport.hpp` | yes | inherits P17-CMD validation and atomicity |
| structured output | `JsonValue`, `--json`, `quantity(value, unit)` | `StructuredOutput.hpp` | yes | reuse unchanged |
| face grammar | `parseFaceReference(document, "face:<feature>:<role>[:<entity>]")` | `Selectors.hpp` | **yes** | the canonical target syntax; no second grammar |
| quantity parsing | `parseLength(text, defaultUnit)` | `Selectors.hpp` | yes | reuse |
| object resolution | `resolveObject(document, "<id> or <name>")` | `Selectors.hpp` | yes | reuse |
| "exactly one, or diagnose" | `theControl` / `theBody` with named codes | `MeshingEdits.cpp` | yes | the pattern for `theAnalysis` |
| backend stdout suppression | `SilentBackendScope` | `src/meshing/netgen/NetgenBackend.cpp` | **yes, already done** | no change — see below |
| fresh-process tests | `bettercad_add_process_test(... "$<TARGET_FILE:bettercad_cli>" ...)` | `tests/CMakeLists.txt` | yes | fresh-binary proof is **structural** |
| in-process tests | `runCliCommand({...})` giving `{exitCode, out, err}` | `tests/cli/CliRunner.hpp` | yes | reuse |
| scripted workflow | `MeshingWorkflow.cmake`, driven by `-DCLI=$<TARGET_FILE:...>` | `tests/cli/` | yes | the P17 sibling, with one improvement |
| zero-match guard | `ZeroMatchGuard.cmake`, with its own self-proof | `tests/cli/` | yes | extend its filter table |

## The six questions the project had already answered

### 1. How many exit codes? THREE. The brief's six classes do not exist.

```cpp
enum class ExitCode : int {
    Success = 0,
    Failure = 1,    ///< The command ran but reported a problem.
    UsageError = 2, ///< The command line was invalid.
};
```

`help` prints the contract: "Exit status: 0 success, 1 failure, 2 invalid
command line." The brief's §50 asks to distinguish six classes **"only if
current CLI contract already permits this granularity"**. It does not, and
inventing P17-specific numbers would make one diagnostic class exit 3 for a
structural command and 1 for every other command — which §97 forbids.

**The granularity lives in the diagnostic CODE, not the exit code.** That is
P15-CLI-001's recorded design: `failure(command, code, problem, err)` prints
`bettercad-cli <command>: <code>: <message>`, and the comment says why — "the
code is the thing a pipeline matches, and only the code is an interface".

### 2. Are mutations top-level commands or batch verbs? BOTH, from one function.

The edit spine (ADR-009), and `Edits.hpp` states it better than this file
could:

```text
Every document-changing command is a function over an ALREADY-LOADED
document. Two drivers use the same functions:

    single-shot   load -> apply one edit  -> save
    batch         load -> apply N edits   -> save

so the one-line form and the scripted form cannot disagree about what a
command means -- the single-shot command is literally the batch of one.
```

So an edit registered in `structuralEditCommands()` becomes a top-level
command *and* a `batch` verb, with nothing added to `BatchCommand.cpp`.

This also answers §21 (save after mutation) and §54 (atomic mutation) without
a decision: `runEdit` "loads it, applies the command to it, and saves only if
the edit succeeded" — in place, no `--save` flag — and atomicity is "a
property of that shape rather than of care taken at each call site".

### 3. What is the naming pattern? Module name first, verb last.

Measured, not guessed. The P16 set is **ten** `mesh-*` commands, which is what
`TODO.md` meant:

```text
six reports      mesh-settings  mesh-generate  mesh-info
                 mesh-quality   mesh-validate  mesh-boundaries
four edits       mesh-control-add      mesh-set-global-size
                 mesh-local-add        mesh-local-remove
```

and P15's is `material-list`, `material-show`, `material-effective`,
`material-completeness` plus `mass-properties`. The module name is the prefix;
the verb, where there is one, comes last.

The structural module is `structural`, so the prefix is `structural-`. Final
names are in [COMMANDS.md](COMMANDS.md).

### 4. What does a report do on failure? It depends which kind — and this is the one place the brief's suggestion does not fit.

BetterCAD distinguishes two things the brief's single `{"status": "error"}`
envelope merges:

```text
"COULD NOT ANSWER"           stdout is EMPTY. The code and message go to
  a missing file, no            STDERR as "bettercad-cli <cmd>: <code>: <msg>".
  analysis, a load failure      Exit 1, or 2 for a usage error.

"THE ANSWER IS A FAILURE"    the JSON IS written to stdout, in full, AND
  a structurally invalid         the exit status is 1.
  mesh; a Rejected
  validation report
```

`mesh-validate` is the precedent for the second, and its comment gives the
reason: "a report that gates is only usable from a script if the gate reaches
the exit status." `cli.mesh.settings.no-control` is the precedent for the
first, and it asserts `EXIT_CODE 1 STDERR_REGEX "no_mesh_control"` with
nothing on stdout.

The brief's §4 and §48 both say to follow the existing channel contract, so
P17 follows this one rather than introducing a stdout error envelope. It also
happens to satisfy §120 — "a JSON error payload may parse while the CLI exits
nonzero" — more strongly than the brief's shape does, because a refusal that
cannot answer writes **nothing** to stdout and so cannot be mistaken for an
answer.

### 5. Where is backend chatter suppressed? In the adapter, already.

`src/meshing/netgen/NetgenBackend.cpp` carries `SilentBackendScope`, and its
comment is the brief's own §76 written out before the brief existed:

> NETGEN WRITES TO STDOUT. ... It has to be contained HERE, in the adapter
> that owns the backend's quirks, because stdout is a shared resource and no
> consumer can be asked to tolerate a library writing to it. **P16-CLI-001
> found this the hard way**: a structured `mesh-info --json` was unparseable,
> because the first line a script read was Netgen's warning rather than the
> opening brace. Fixing it in the CLI would have left the GUI, the tests and
> any future consumer with the same corrupted stream and the CLI compensating
> for it.

It swaps `std::cout.rdbuf` under the backend mutex that every generation
already holds. **So no P16 change is needed, and no P16 requalification
follows from this milestone.** P17 inherits clean stdout, and
[BACKEND_STDOUT.md](BACKEND_STDOUT.md) records the measurement rather than
trusting the inheritance.

### 6. Do summary commands solve? YES — and the sibling module settled it.

The brief's §195 asks for this to be "decided deliberately" and recommends the
summary command invoking the normal solve pipeline. The P16 reports already do
exactly that, four times over, and for the identical reason:

```text
mesh-info        "Mesh the document and summarise it"
mesh-quality     "Mesh the document and report element quality"
mesh-validate    "Mesh the document and check it structurally"
mesh-boundaries  "Mesh the document and report the named boundary sets"
```

Each builds the derived state it needs from canonical intent, in its own
process, and exits — because **a generated mesh is not persisted**, exactly as
a structural result is not. `MeshingReports.cpp` says so: "in a fresh process
`mesh-info` says there is no mesh until something asks for one — which is
correct and is tested, not worked around."

So each P17 result command solves through `solveStructuralAnalysis` and
reports. Two consequences, both required by §196 and both tested: a summary
command inherits **every** P17-VALID refusal, and there is no
`NoCurrentResult` state to report, because no command ever looks for a stored
result.

> The brief's §89 describes the alternative model, in which a summary command
> finds no current result and says so. That model needs a result to be
> findable across processes, which P17-PERSIST-001 forbids. Following it would
> have made every summary command permanently useless from a fresh process.

## What the audit found that is NOT there

```text
a higher-level core orchestration (runStructuralAnalysis)   does not exist
```

`solveStructuralAnalysis(document, regenerator, mesher, analysis)` is the
production entry and its own header states it "never calls
`Mesher::generate`". So the CLI must load, regenerate, generate the mesh and
then solve — the same four steps `MeshingReports.cpp` already does in its
`prepare()` and `generate()` helpers. Per §197 that sequence is written
**once**, in one helper, and shared by all four result commands rather than
repeated.

That helper is orchestration, not engineering: it calls qualified entries in
order and owns no rule of its own.

## Scope decision recorded: one command the checklist does not list

The checklist asks for inspect, add/remove load, add/remove restraint, solve,
three summaries and validation. It does not ask for a way to **create** an
analysis — but without one the CLI cannot produce a structural analysis at
all, and every other command would require a hand-written file.

`structural-analysis-add` is included, because:

```text
mesh-control-add is its exact sibling and P16 included it for the same reason
it is one thin adapter over CreateStructuralAnalysisCommand, which
  P17-CMD-001 already built and qualified
without it, the multi-process workflow's "create/inspect analysis if
  necessary" (§57) could only ever be "inspect"
```

Nothing else was added. There is no edit command for loads or restraints
beyond add and remove (§20: editing stays available through the core command
API and is not required here), no interactive shell, no `--force`, no
`--ignore-validation`, and no export format.

## One improvement over the P16 precedent

`MeshingWorkflow.cmake` asserts JSON fields with `string(MATCHES)` — a regex
over the payload text. The brief's §122 and §161 require the output to be
genuinely **parsed**, not grepped.

CMake 3.28, which this project already requires, has `string(JSON ... GET ...)`
— a real JSON parser that fails on malformed input. The P17 workflow uses it,
so every structured payload is parsed before any field is read, and any
contamination is a fatal error rather than a regex that happens to still
match. P16's script is not changed; this is additive.
