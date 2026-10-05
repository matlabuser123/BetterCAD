# P16-CLI-001 — Headless Meshing Workflows

```text
STATUS:   PASS
DATE:     2026-10-05
GATE:     CLI uses core meshing APIs + core/CLI mesh results equivalent
          + quality results equivalent + diagnostics structured
          + failures propagate + no CLI-only meshing implementation
```

## Baseline

```text
HEAD:          7024649cda863c36a0c4cb044afdd1469af58624
TREE:          859b03b62640aeef8c6cf22e55d60a8b4e7d75eb
origin/main:   7024649cda863c36a0c4cb044afdd1469af58624  (equal)
log:           7024649 BetterCAD: persist canonical meshing intent
working tree:  clean at start of milestone
compiler:      GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, WinLibs r4)
cmake:         4.4.1, Ninja
build root:    C:/Users/uqhas/AppData/Local/bc-build  (outside OneDrive, -ext presets)
CLI:           bettercad-cli, apps/bettercad_cli/, bettercad_cli_lib + main.cpp
```

## Prerequisites

Checked by reading each milestone's recorded RESULT, not its checkbox:

```text
P16-ARCH-001      PASS      P16-MAP-001       PASS
P16-DATA-001      PASS      P16-VIZ-001       PASS
P16-GEOM-001      PASS      P16-CMD-001       PASS
P16-SURF-001      PASS      P16-PERSIST-001   PASS
P16-VOL-001       PASS      INFRA-NETGEN-001  PASS
P16-SIZE-001      PASS
P16-QUALITY-001   PASS
```

So the milestone is **not blocked**.

## Scope

```text
IN     six mesh reports and four mesh edits, as adapters over existing core
IN     a small general structured writer, with --json on the reports
IN     parseNamedFace exported from Selectors.hpp, so the face grammar is
       shared with drawings and mates rather than copied
IN     BetterCAD::meshing on the CLI target
IN     targeted tests, process tests on the real executable, a scripted
       end-to-end workflow (valid and negative), core/CLI equivalence, and a
       zero-match filter guard that proves its own counter

OUT    converting the existing commands to --json (a known limitation)
OUT    `mesh local edit`: remove + add is the same canonical result through
       commands that already exist, and `batch` makes it one transaction.
       Decision recorded, per the brief's §17
OUT    any new meshing semantics, defaults, tolerances or thresholds
OUT    a stale-mesh workflow, which a one-shot process cannot reach
```

## CLI architecture audit

In full in [CLI_AUDIT.md](CLI_AUDIT.md). Most of this milestone's
architecture questions were already answered by what was there.

```text
Entry point        cli::run(args, out, err), noexcept
Command parser     constexpr kCommands of {name, usage, summary, handler}
Subcommands        TWO kinds: reports (handlers) and edits (EditCommand)
Open/save          reports load and never write; edits go through runEdit,
                   which saves ONLY if the edit succeeded
Structured output  none existed -- the one real decision (C4)
Diagnostics        failure(command, code, problem, err), code a STABLE name
Exit codes         Success 0, Failure 1, UsageError 2
Reusable for P16?  entirely
```

The **edit spine** is the answer to half the milestone. Its own header says
*"the single-shot command is literally the batch of one"* and *"nothing is
written until every edit has succeeded"* — so registering four meshing verbs
in a table gave atomicity, save semantics and `batch` scriptability with no
driver code.

## Core API routing, and no CLI-only semantics

Every command and the existing entry point behind it is tabulated in
[COMMAND_CONTRACT.md](COMMAND_CONTRACT.md) §1. The CLI computes nothing:

```text
volume     VolumeMesh::tetrahedralVolume
quality    MeshQualityReport, under the control's own thresholds
structure  meshing::validate(const Mesh&)
mapping    meshing::resolveBoundarySet
mutation   the P16-CMD-001 command objects, via EditSupport::execute
```

Searched case-sensitively over `apps/bettercad_cli/`:

```text
nglib 0   Ng_[A-Z] 0   Netgen 0   radiusRatio 0   dihedral 0
aspectRatio 0   determinant 0   nearest 0   std::sqrt 0
```

and `meshing` links Netgen **PRIVATE**, so the CLI cannot reach the backend
even transitively.

## Command surface

```text
mesh-control-add       mesh-settings      mesh-quality
mesh-set-global-size   mesh-generate      mesh-validate
mesh-local-add         mesh-info          mesh-boundaries
mesh-local-remove
```

Four edits (atomic, saving, scriptable through `batch`) and six reports
(loading, computing, printing, writing nothing).

## Units and geometry references

**In:** `parseLength(text, units::mm)`, the CLI's own parser. `10mm`, `0.01m`
and `1cm` are the same `Length`, asserted together.

**Out:** a value and its unit, never a bare number —
`{"value": 0.008, "unit": "m"}`. An absent global size prints `null` and
"BetterCAD default", never an invented number.

**References:** `face:<feature>:<role>[:<entity>]`, through the grammar
drawings and mates already use. Output is re-feedable
(`face:Extrude001:end_cap`). A `NodeId` has no syntax and `node:4` exits 2.

## Exit codes and diagnostics

The full matrix is in [COMMAND_CONTRACT.md](COMMAND_CONTRACT.md) §3. The
distinction that matters:

```text
exit 1   the core refused the value, and its diagnostic is the core's
exit 2   the command line never became a quantity or a reference
```

Measured:

```text
mesh-set-global-size -- -1mm    1   "size is -0.001 m, which is not positive"
mesh-set-global-size -- 0mm     1   "size is 0 m, which is not positive"
mesh-set-global-size -- nanmm   2   "'nanmm' is not a number"
mesh-set-global-size -- 1zz     2   "unknown unit 'zz' in '1zz'"
```

A failing query prints **nothing** to stdout, which is the stream discipline
P15-CLI-001 established and what lets a pipeline trust what it reads.

## Core / CLI equivalence

In full in [CORE_EQUIVALENCE.md](CORE_EQUIVALENCE.md). The same document
through the core API and through the CLI, field by field, on a cylinder with
>100 elements:

```text
node count, element count, boundary triangles   exact
mesh volume                                     WithinRel 1e-12
invalid / warning / failure element counts       exact
data validity                                    exact
```

The volume tolerance is round-trip noise, not an engineering tolerance: both
paths read the same `tetrahedralVolume()`, so anything larger would mean the
CLI had recomputed something.

Out of process, on the real executable, eight tests including the 21-step
workflow — each taking the binary from `$<TARGET_FILE:bettercad_cli>`, an
absolute path, so a preset tests the binary it built.

## End-to-end workflow

In full in [E2E_WORKFLOW.md](E2E_WORKFLOW.md).

```text
21 steps, every exit code asserted, output parsed, fails fast
valid:     settings -> control-add -> set-global -> local-add -> settings
           -> generate -> info -> quality -> validate -> boundaries
           -> fresh copy reports the IDENTICAL payload
           -> local-remove CHANGES the mesh
negative:  invalid size (1), unreadable size (2), node reference (2),
           missing control (1), missing document (1), unknown command (2)
           -> and the document is byte-identical after all six
```

## Zero-match filter protection

`ctest -R` exits **0** when a pattern matches nothing — the guard records that
by running an impossible filter — so the guard asserts a discovered **count**
per filter and proves its own counter:

```text
unit\.MeshingCli            17 tests (minimum 10)
cli\.mesh\.                  8 tests (minimum 5)
unit\.MeshingPersistence    26 tests (minimum 10)
unit\.MeshingCommand        33 tests (minimum 10)
an impossible filter         0 tests, and ctest exits 0 on it
```

## Adversarial review

29 attack questions and ten findings in
[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
questions:              29
findings:               10
production defects:     1  (C1: Netgen writes to stdout, which made --json
                            unparseable; fixed in the BACKEND ADAPTER, because
                            stdout is shared and the CLI must not compensate
                            for a core defect)
defects in new code:    2  (C2 stray commas in the JSON; C3 face references
                            were not re-feedable)
test defects:           2  (C9 the workflow fixture named a feature that does
                            not exist -- caught by the script's own fail-fast;
                            C10 two regexes spanned newlines)
decisions recorded:     4  (C4 no structured framework existed; C5 staleness
                            is unreachable headlessly; C6 a negative value
                            needs --; C8 `mesh local edit` not exposed)
reuse instead of copy:  1  (C7 parseNamedFace exported, not duplicated)
remaining open:         0
```

## Mutation testing

```text
8 mutations of the CLI
6 killed
2 equivalent mutants, each PROVEN rather than asserted
1 PRODUCTION DEFECT found, fixed, re-expressed and killed
```

| # | Mutation | Verdict |
| --- | --- | --- |
| M1 | the CLI substitutes its own default target size | killed, 2 |
| M2 | the CLI validates the size itself and bypasses the core | killed, 4 |
| M3 | a failed core mutation is reported as success | killed, 4 |
| M4 | a failed mesh generation exits zero | killed, 1 |
| M5 | a structurally invalid mesh does not gate `mesh-validate` | equivalent mutant |
| M6 | the CLI ignores a feature that failed to regenerate | **found a defect**; killed, 7 |
| M7 | local controls listed in stored order | equivalent mutant |
| M8 | the JSON writes a bare number with no unit | killed, 9 |

**M6 found what no other gate had.** It removed the regeneration check and
survived, because no fixture had a model that fails to build. Writing one
exposed a misreading of the core: `Regenerator::regenerate()` returns an error
only for a *document-level* fault, while a FEATURE that fails to build is a
**success** carrying `failed`, `blocked` or `cycles` in its report. The CLI
checked only the `Result`, so a broken model passed the precondition and the
command then reported `no_mesh_control` — true, and not the reason, which is
exactly what §44 forbids. Fixed to inspect the report and keep the core's
distinctions.

**Both survivors are proven, not excused.** M5's branch cannot be reached:
`volumeMeshFor` validates before returning, so no `VolumeMesh` a caller can
obtain is ever invalid, and `mesh-validate`'s gate is defence in depth rather
than a tested gate. M7 cannot be observed through a file: P16-PERSIST-001's
serializer writes the canonical order, measured —
`end_cap` added first still appears after `start_cap` in the file. Detail in
[qualification/mutation/README.md](qualification/mutation/README.md).

## Full regression

```text
PRESET            FULL SUITE     WARNINGS  OBJECTS  NO-OP REBUILD
debug-ext         3328 / 3328       0        592    0 compiles, 0 links
release-ext       3328 / 3328       0        592    0 compiles, 0 links
debug-shared-ext  3328 / 3328       0        592    0 compiles, 0 links

REPEAT (5x each of 442 selected tests, back to back)
release-ext       442 / 442                          155.24 s
debug-ext         442 / 442                          181.55 s
```

Unfiltered, after a fresh configure and a clean build in each preset. Every
no-op rebuild did nothing, so the binaries tested are the ones just built.

Pre-freeze, both full suites showed one failure, `cli.new.unicode-path`, a
code-page artefact: those runs were made without `chcp 65001`, which
`qualify.cmd` sets as its first act. The qualifying runs are 3328/3328.

## Fresh-binary proof and runtime closure

Every CLI process test and the workflow script take the executable from
`$<TARGET_FILE:bettercad_cli>`, which resolves **per preset** — so each preset
ran the binary it had just built, and nothing came from `PATH`. The workflow
script prints the absolute path and size of the executable it ran.

The shared preset linked **9 DLLs** including `libbettercad_meshing.dll`, so
the CLI process tests under that preset had to resolve the whole closure at
load time. That is the runtime proof: a missing or mismatched DLL would fail
the process, not a link step.

Runtime provenance beyond that is **inherited rather than re-proved**: the CLI
links the same `BetterCAD::meshing` the tests do and runs under CTest's
environment, exactly as the existing `cli.*` tests always have, and
INFRA-NETGEN-001 qualified that closure. Saying so is more honest than
claiming a fresh audit this milestone did not perform.

## Determinism and cross-preset equivalence

The repeat set is **442 of 3328** tests: the CLI suite, every `cli.` test (the
new commands share `kCommands`, the edit registry, the argument parser and
`Selectors.cpp` with all of them), the whole meshing and mesh-renderer surface
— because one core file changed — plus `Document`, the face-reference tests,
`gui.` and `architecture`.

`compile_fail` is excluded on purpose: this milestone adds no compile-fail
case and nothing in `apps/bettercad_cli/` is included by one. Each of those
cases is a real compilation, so five rounds of roughly two hundred would cost
about two hours and report nothing about the CLI. The reasoning is written
into
[qualification/run-qualification.cmd](qualification/run-qualification.cmd).

The equivalence, determinism, human-vs-JSON, workflow and zero-match gates all
pass under `-O0 -g`, under the optimiser, and across a DLL boundary.

## Result

```text
RESULT:    PASS
GATE:      met -- the CLI routes every operation to an existing core API and
           computes no mesh quantity of its own; core and CLI agree field for
           field including in a separate process; quality and validation are
           the core's reports; diagnostics carry stable machine codes and the
           streams are disciplined; failures propagate with the core's own
           distinctions; and there is no CLI-only default, tolerance,
           threshold, validation or mapping
EVIDENCE:  this directory; qualification/qualification-times.txt for the run,
           qualification/mutation/results.txt for the mutations
TREE:      f58a1e64cbff9ec9f2d6f7c3d0816bace905843e, qualified and committed
```

## Known limitations

1. **Structured output is meshing-only.** The other commands keep their prose.
   Converting them would be rewriting three qualified output surfaces inside a
   feature milestone; adopting `--json` elsewhere is future work.
2. **`mesh-info` always reports `current`.** A one-shot process cannot hold a
   mesh while its intent moves, so the stale states are unreachable headlessly.
   They exist and are tested in core.
3. **A negative size needs `--`** to reach the core validator; without it the
   argument parser refuses it first, at exit 2.
4. **One control and one body per document.** Both are reported by name
   (`ambiguous_mesh_control`, `ambiguous_body`) rather than guessed at, and
   choosing between several needs a selector this version does not have — the
   same limit the GUI records.
5. **`mesh local edit` is not exposed**, by decision: remove + add, in a
   `batch` when atomicity matters.
6. **Runtime closure is inherited, not re-proved.** The CLI links the same
   `BetterCAD::meshing` the tests do and runs under CTest's environment, as
   the existing `cli.*` tests always have; INFRA-NETGEN-001 qualified that
   closure and this milestone does not re-qualify it.
