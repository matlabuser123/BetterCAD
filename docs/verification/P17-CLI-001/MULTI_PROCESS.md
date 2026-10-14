# P17-CLI-001 — the multi-process workflow, and the fresh-binary proof

```text
28 steps, every one its own PROCESS, every exit code asserted, every
structured payload PARSED with a real JSON parser.
```

`tests/cli/StructuralWorkflow.cmake`, registered as `cli.structural.workflow`
and driven by `-DCLI=$<TARGET_FILE:bettercad_cli>`.

## Why a separate process at all

The in-process tests call `cli::run()` directly, which proves the command
logic. They cannot prove the **executable** works: argument parsing from a real
command line, document loading from a real path, the runtime closure actually
loading, the exit code reaching the shell, and stdout being clean enough to
parse. Every one of those has broken something in this project before — that is
P16-CLI-001's recorded reason and it holds unchanged.

And the brief's §56 asks for more than that: the no-persisted-result property
can **only** be shown across processes, because a single process holding the
document in memory proves nothing about what the file carries.

## The fresh-binary proof

```text
preset                  debug-ext
absolute CLI path       C:/Users/uqhas/AppData/Local/bc-build/debug-ext/bin/
                        bettercad-cli.exe
size                    368186309 bytes
sha256                  cb158dac8919a9c605914d0cf6739c07bf8bb1911dcd21cb0b7c9262a549b332
```

Recorded in the test log by the script itself, on every run, so the evidence
names the executable that **actually ran** rather than the one that was meant
to. Re-measured per preset at the freeze; see [FREEZE.md](FREEZE.md).

**The path cannot be a stale one, and that is structural rather than
disciplined.** CMake expands `$<TARGET_FILE:bettercad_cli>` to the build-tree
binary, so:

```text
no bare name        a PATH lookup is impossible -- there is nothing to look up
no installed copy   the path is under the build root, not CMAKE_INSTALL_BINDIR
asserted to exist   the script refuses to run if it does not
```

The same mechanism covers the nine `cli.structural.*` process tests, which use
`bettercad_add_process_test(... COMMAND "$<TARGET_FILE:bettercad_cli>" ...)`.
So the brief's §114 and §163 are satisfied by the registration and not by a
convention someone has to remember.

## The sequence, as it ran

| Step | Process | Command | Exit | What it established |
| --- | --- | --- | --- | --- |
| 1 | P1 | `structural-settings --json` | 0 | the fixture's 2 loads, 1 restraint |
| 2 | P2 | `structural-load-add 4 --traction 0,0,-1.5MPa --face …` | 0 | a mutation, saved and exited |
| 3 | P3 | `structural-restraint-add 2 … uy,uz` | 0 | a second mutation, separate process |
| 4 | P4 | `structural-settings --json` | 0 | **a fresh process sees both edits** — 3 loads, 2 restraints, and `-1.5MPa` read back as `-1500000` Pa |
| 5 | P5 | `structural-validate --json` | 0 | acceptable, without solving |
| 6 | P6 | `structural-solve --json` | 0 | 81 nodes, 127 tetrahedra, 119 free equations |
| 7 | P7 | `structural-displacement --json` | 0 | max \|u\| agrees with P6 |
| 8 | P8 | `structural-stress --json` | 0 | max von Mises agrees with P6, `domain: element` |
| 9 | P9 | `structural-reactions --json` | 0 | 13218.58 N, 42 constrained nodes, unit `N` |
| 10 | P10 | `structural-solve --json` | 0 | **a second fresh process re-solved and agreed** |
| 11–16 | P11–P16 | the under-constrained fixture | 0,0,1,1,1,1 | every result command refuses it, stdout empty |
| 17–20 | P17–P20 | the unresolved-target fixture | 0,0,1,1 | preserved, reported unresolved, then refused |
| 21 | P21 | a document with no analysis | 1 | `no_structural_analysis`, stdout empty |
| 22 | P22 | a file that is not there | 1 | stdout empty |
| 23–26 | P23–P26 | four command-line faults | 2,2,2,2 | usage errors are 2, never 1 |
| 27–28 | P27–P28 | a rejected edit | 1 | the file is byte-identical afterwards |

Every mutation step writes and exits; every query step reloads from disk.
Nothing is carried in memory across a step.

## No persisted result

```text
Process A (P6) solved                                  YES
the document was saved by the workflow's edits         YES (P2, P3)
a result array reached the file                        NO
Process B (P10) loaded the same file                   YES
Process B found a current solved result                NO -- there is nowhere
                                                       for one to be
Process B had to solve again                           YES
```

This is **structural rather than defended**: a `StructuralResult` does not live
on a document object, so a loaded analysis has nowhere to carry one. The
in-process test `StructuralCli_SolvingNeverChangesTheDocument` adds the
complementary half — the saved file contains no result, checked by name for
seven derived key prefixes.

## A solve does not touch the file

```text
before the solve   7917 bytes   sha256 193eac39cfb916b872c3f8e48c11e6625e9cc171f91b5b2962d90141d895567c
after  the solve   7917 bytes   sha256 193eac39cfb916b872c3f8e48c11e6625e9cc171f91b5b2962d90141d895567c
byte-identical     YES
```

Measured with `file(SHA256)` on the real file either side of a real solve in a
real process. That is ADR-048's rule reached through the CLI — the brief's §25,
§60 and §190 — and the in-process test repeats it across all five
result-producing commands.

## The re-solve comparison

| Metric | Process 6 | Process 10 | Difference |
| --- | --- | --- | --- |
| node count | 81 | 81 | 0 |
| free equations | 119 | 119 | 0 |
| strain energy | 0.00084942003810145876 J | same | 0 |
| max \|u\| | 2.5894606930351219e-07 m | same | 0 |
| max von Mises | 2912178.3259140174 Pa | same | 0 |
| **the whole payload** | — | — | **byte-identical** |

The last row is the strong form and is what the script asserts: two fresh
processes solving the same file printed the same bytes. There was no need for a
tolerance, which is worth saying because the brief offers one — the mesher is
deterministic, the solver is direct, and the writer is insertion-ordered, so
exact equality was available and is therefore what is claimed.

## Zero-match protection

Two layers, because `ctest -R` exits 0 when a pattern matches **nothing** —
demonstrated by the guard itself, which runs an impossible filter and checks it
discovers 0.

```text
ZeroMatchGuard.cmake      unit\.StructuralCli    minimum 15
                          cli\.structural\.      minimum 8
the workflow script       asserts its own step COUNT >= 25
```

The second layer is the brief's §118 applied to a shell script rather than a
filter: a workflow that took an early return and exited 0 would be the same
defect as a filter matching nothing, so the count is asserted. Measured: **28
steps**.

| Filter | Minimum | Discovered | Executed | PASS |
| --- | --- | --- | --- | --- |
| `unit\.StructuralCli` | 15 | 19 | 19 | ✓ |
| `cli\.structural\.` | 8 | 9 | 9 | ✓ |
| an impossible filter | — | **0** | 0 | ✓ (the counter is real) |

## The exit code is never a parser's

`execute_process` gives `RESULT_VARIABLE` and `OUTPUT_VARIABLE`
independently, so there is no pipeline whose status could be the parser's
instead of BetterCAD's — the brief's §119. Both are asserted on every step,
and the two are asserted *together* where they disagree in the interesting way:
`structural-validate` on a rejected model has a **parseable payload** and a
**nonzero exit**, which is §120's exact concern.
