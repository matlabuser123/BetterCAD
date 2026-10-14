# P17-CLI-001 — exit codes

```text
Success    = 0
Failure    = 1    the command ran but reported a problem
UsageError = 2    the command line was invalid
```

**Three, not six — and that is the audit's finding rather than a choice.**
`Cli.hpp` has defined exactly these three since P10, and `help` prints the
contract: "Exit status: 0 success, 1 failure, 2 invalid command line."

The brief's §50 asks to distinguish six classes **"only if current CLI
contract already permits this granularity"**. It does not. Inventing
P17-specific numbers would make one diagnostic class exit 3 for a structural
command and 1 for every other command, which §97 forbids in the same breath.

**The granularity lives in the diagnostic CODE.** That is P15-CLI-001's design,
and its comment states the division: "the code is the thing a pipeline
matches, and only the code is an interface. The message stays the thing an
engineer reads."

So a script branches on the code, never on the English and never on a number
this milestone would have had to invent:

```text
bettercad-cli structural-solve: insufficient_restraint: only 0 degree(s) of
freedom are constrained, and a three-dimensional body has six rigid-body
modes, so at least 6 are necessary. ...
                └─ exit 1    └─ the interface
```

## The table

Every row measured by running the command, in process and again as a separate
process.

| Case | Diagnostic code | stdout | Expected | Actual | PASS |
| --- | --- | --- | --- | --- | --- |
| a valid solve | — | payload | 0 | 0 | ✓ |
| a valid validation (acceptable) | — | payload | 0 | 0 | ✓ |
| a valid inspection | — | payload | 0 | 0 | ✓ |
| an inspection whose targets do not resolve | — | payload, `faceResolves: false` | **0** | 0 | ✓ |
| no command | — | empty | 2 | 2 | ✓ |
| an unknown option | — | empty | 2 | 2 | ✓ |
| two document paths | — | empty | 2 | 2 | ✓ |
| `--pressure abc` | — | empty | 2 | 2 | ✓ |
| `--pressure nan` | — | empty | 2 | 2 | ✓ |
| an unknown component (`rx`) | — | empty | 2 | 2 | ✓ |
| an empty component mask | — | empty | 2 | 2 | ✓ |
| a face role this build does not know | — | empty | 2 | 2 | ✓ |
| two load kinds at once | — | empty | 2 | 2 | ✓ |
| no load kind at all | — | empty | 2 | 2 | ✓ |
| a gravity load given a `--face` | — | empty | 2 | 2 | ✓ |
| a traction with two components | — | empty | 2 | 2 | ✓ |
| a load id that is not a number | — | empty | 2 | 2 | ✓ |
| the file does not exist | `document_not_loaded` | empty | 1 | 1 | ✓ |
| a feature the document does not have | `bad_face_reference` | empty | 1 | 1 | ✓ |
| no structural analysis | `no_structural_analysis` | empty | 1 | 1 | ✓ |
| two structural analyses | `ambiguous_structural_analysis` | empty | 1 | 1 | ✓ |
| no meshing control to analyse | `no_mesh_control` | empty | 1 | 1 | ✓ |
| a model that will not regenerate | `regeneration_failed` | empty | 1 | — *(see below)* | — |
| a model blocked by an earlier failure | `regeneration_blocked` | empty | 1 | — *(see below)* | — |
| a dependency cycle | `dependency_cycle` | empty | 1 | — *(see below)* | — |
| a mesh that will not generate | `mesh_generation_failed` | empty | 1 | 1 | ✓ |
| a missing material | the core's material code | empty | 1 | 1 | ✓ |
| an under-constrained analysis | `insufficient_restraint` | empty | 1 | 1 | ✓ |
| an unresolved load target | `loads_unresolvable` | empty | 1 | 1 | ✓ |
| an unresolved restraint target | `restraints_unresolvable` | empty | 1 | 1 | ✓ |
| a rejected validation report | the finding's own code | **payload** | 1 | 1 | ✓ |
| a duplicate `LoadId` | the command's own message | empty | 1 | 1 | ✓ |
| a `LoadId` that is not there | the command's own message | empty | 1 | 1 | ✓ |
| a duplicate `RestraintId` | the command's own message | empty | 1 | 1 | ✓ |
| a `RestraintId` that is not there | the command's own message | empty | 1 | 1 | ✓ |
| a second analysis | `structural_analysis_exists` | empty | 1 | 1 | ✓ |

The three regeneration rows are implemented and carry their own codes, taken
from P16-CLI-001's `prepare()` where mutation M6 established that the
*report* must be checked and not only the `Result`. They are **not reached by a
P17 fixture**: producing a model that regenerates badly needs a broken feature,
which is P12's territory and already covered by `cli.mesh.*`. The code is
shared, so the paths are exercised there; nothing here claims a P17 test for
them.

## The rule, stated once

**Exit 2 means "the CLI could not read what it was given". Exit 1 means "the
model refused what it was asked".** That is `fromParse`'s rule, and the
mapping is `InvalidArgument` or `DimensionMismatch` → 2, everything else → 1.
It is decided in exactly one place for every edit in the program, which is
`EditSupport.hpp`'s stated purpose: "the difference between exit 1 and exit 2
... is precisely the thing that must not be decided twice."

A consequence worth naming: a machine code is **dropped** when the failure is a
usage error, "because a usage error prints the usage and exits 2, and a machine
code on it would suggest a document state that was never reached". So the
code column above is empty for every exit-2 row, and that is deliberate.

## The exit code is never derived from text

```text
if (message.contains("stale")) ...     does not appear anywhere
```

`structural-solve` returns `solved.error()` — an `ExitCode` the helper already
chose. `structural-validate` returns `report.acceptable() ? Success : Failure`
— the core's own accessor. **The CLI never names a `ValidationStatus`
enumerator**, checked by grep: zero occurrences of `ValidationStatus::` in the
reports file. So it cannot have a status-based gate, and a warning-only report
exits 0 by construction.

Probes **L1** (a swallowed solve failure), **L2** (validate always succeeding)
and **L13** (a usage error reported as a failure) are each killed.

## Warning-only analyses: correct by construction, unreachable by fixture

A `AcceptedWithWarnings` report is acceptable and must exit 0 (§52, §149). The
CLI gates on `acceptable()`, which returns true for it, so the behaviour is
right by construction.

**It is not reachable through the CLI**, and that was measured rather than
assumed: validation was run on the fixture at five global mesh sizes — 30 mm,
20 mm, 8 mm, 5 mm, 3 mm — and every one returned `accepted` with zero
findings. P17-VALID-001 explains why, having measured it: BetterCAD's own
meshes sit comfortably inside the qualified accuracy envelope, and a warning
needs a hand-built degenerate element that Netgen will not produce.

So what is proved is the structural property — the gate is `acceptable()` and
never a status comparison — and the unreachability is recorded rather than
faked with a hand-assembled report, which would have tested the core and not
the CLI.
