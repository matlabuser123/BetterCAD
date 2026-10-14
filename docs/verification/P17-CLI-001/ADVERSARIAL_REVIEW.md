# P17-CLI-001 — adversarial review

```text
RESULT:              PASS
QUESTIONS ASKED:     31 from the brief's own list, plus the reading pass
CREDIBLE FINDINGS:   8
PRODUCTION DEFECTS:  1 -- a count that meant the wrong thing, fixed
REMAINING BLOCKERS:  0
```

Eight things were wrong. **One was in the production code**; five were in the
tests, and two were in my own evidence or probe design. Three were found by
reading, five by something failing — and as in the last two milestones the two
sets are disjoint.

## The brief's attack list, answered

| Attack | Answer |
| --- | --- |
| Can CLI solve bypass P17-VALID? | No. One `solve()` helper, which calls `validateStructuralAnalysisForSolve` and then `solveStructuralAnalysis`. Probe L3. |
| Can CLI call `solvePreparedSystem` directly? | Not named anywhere: **0** occurrences. |
| Can CLI implement a second load mapper? | `GeometryMeshMap`, `prepareStructuralLoads`: **0** each. |
| Can CLI accept a `NodeId`/facet as a canonical target? | No option exists; eleven spellings tested and refused. |
| Can CLI implement its own under-constraint rule? | No restraint count anywhere; the diagnostic is `insufficient_restraint` with the core's own wording. |
| Can CLI implement its own material rule? | No material property is read in the CLI at all. |
| Can CLI set its own residual/equilibrium thresholds? | No threshold in either file; the settings come from the document. |
| Can CLI compute stress or von Mises? | `vonMises =`, `sqrt`, `eigen`: **0** in code (one comment). |
| Can CLI compute reactions? | No `K u - F`, no `cross(r, F)`. Probe L8 for the sign. |
| Can structured stdout carry a Netgen warning? | No — contained in the adapter since P16-CLI-001. Measured over nine parsing processes. Probe L4. |
| Can test scripts hide corruption with tail/grep? | The workflow uses `string(JSON)`; the only match for those words is the comment saying there are none. |
| Can stderr redirection miss stdout chatter? | It would — which is the whole lesson, and why the fix is in the adapter. |
| Can debug logging contaminate stdout? | `std::cout`/`printf`: **0** in both CLI files. Probe L4. |
| Can CLI exit 0 while the payload says error? | No. Probes L1, L2, L13. |
| Can parser success hide a nonzero exit? | `execute_process` separates them; both asserted on all 28 steps. |
| Can a warning-only solve return failure? | No — the gate is `acceptable()`, and `ValidationStatus::` has **0** occurrences. Not reachable by fixture; measured at five mesh sizes. |
| Can a stale mesh return success? | Not presentable to this CLI; see [FAILURE_MATRIX.md](FAILURE_MATRIX.md). |
| Can a missing material be filled from a CLI default? | No `--default-E`; tested. |
| Can an unresolved face be rebound by CLI? | Nothing in the CLI resolves geometry. Probe L10 for the reporting. |
| Can an under-constrained analysis be auto-pinned? | No `--auto-pin`; tested with seven absent flags. |
| Can a summary report a persisted old result? | Structurally impossible: no result is persisted. |
| Can a fresh process see a current result? | No; measured across ten processes. |
| Can CLI solve alter the persisted file? | No; sha256 identical either side, five commands. |
| Can process 2 depend on process 1's memory? | Every workflow step is its own process. |
| Can a stale binary be invoked from PATH? | `$<TARGET_FILE:>`; path and hash recorded per run. |
| Can the shared preset load an old library? | Exercised by the qualification's shared preset. |
| Can zero targeted tests report PASS? | Two layers: the guard's filter minimums and the workflow's own step count. |
| Can a pipeline return the parser's status? | No pipeline exists in the harness. |
| Can output units differ from core units silently? | Every number is a `quantity` or carries a `unit`. Probe L5. |
| Can diagnostics be reordered? | The core's order, no sort; codes compared in order. |
| Can a CLI-specific policy diverge from the core? | There is no policy in the CLI to diverge. |

## The one production defect

**`elementCount` meant the mesh total, printed beside per-tetrahedron physics.**
`Mesh::elementCount()` is `triangles_.size() + tetrahedra_.size()`, so a
12-tetrahedron block reported **24** next to a stress range over 12 elements.

Found by the core-equivalence test, which compared the printed count against
`RecoveredFields::elements().size()` and got `24.0 == 12.0`. Had the test
instead asserted "some positive number", the CLI would have shipped a report
whose element count did not match its element data.

Fixed by printing **both**, named apart: `elementCount` keeps
`mesh-info`'s meaning, `tetrahedronCount` is what the physics is over, and the
prose says "tetrahedra". Probe **L6** confirms the distinction is load-bearing.

## The three found by reading

**R1 — the forbidden-key scan would have hit the payload's own keys.** Scanning
inspection output for `strain` matches `restraints`. Caught before it could
fail, by matching the opening quote — the same trap P17-PERSIST-001 hit from
the other direction, which is why it was looked for here.

**R2 — a bare-number parser would have been a second unit semantics.** The first
draft read pressure and acceleration as plain SI doubles, because BetterCAD has
"no unit catalogue for a pressure". It does: `units::Pa`, `MPa`, `GPa`, `N`,
`m_per_s2` all exist, and `parseQuantity` is dimension-generic *precisely* so
that a new quantity "cannot acquire a second spelling, a second unit table or a
second dimension check". Rewritten to use it, which also made `2.5MPa` work.

**R3 — two probes would have been void.** L5 and L9 as first drafted needed
includes the files do not have. Rewritten before the run so each produced a
verdict rather than a compile failure — the standing lesson, applied in advance
for the second milestone running.

## The five found by something failing

**T1 — the element count** (above), the production defect.

**T2 — `strainEnergy` is a nested quantity.** The flat JSON reader grabbed `{`
and threw. A `jsonQuantity` helper now reads the nested `value` — which exists
because of the failure rather than foresight, and is noted as such.

**T3 and T4 — two of my own expected exit codes were wrong**, not the code. An
empty component string and an unknown face role are both *command-line* faults
(exit 2), not rejected edits (exit 1) — which is `fromParse`'s rule. Correcting
them surfaced something worth recording: **no CLI spelling can produce an empty
component mask**, so the core's empty-mask refusal is unreachable through this
surface. Recorded in [COMMANDS.md](COMMANDS.md) rather than left looking like a
gap.

**T5 — the prose said "elements" after the payload said "tetrahedra".** My own
test caught the inconsistency I had just introduced.

## The two found by the probes

**L5** and **L10**, both genuine test gaps, both closed and both re-killed. See
[MUTATION.md](MUTATION.md) — and note the shape of L10: an assertion that was
*true*, that *passed*, and that passed under the very defect it was written
for. That is the sixth time this project has seen a misdirected assertion, and
the first time the fixture split was the fix.

## What was looked for and not found

```text
a second command style                         none -- the edit spine took them
a second structured-output envelope            none
a second unit parser                           none, after R2
a second validation threshold                  none
a Netgen suppression in the consumer           none, and none needed
a PATH lookup for the binary                   none
a pipeline that could swallow an exit code     none
a flag that bypasses validation                none -- seven checked absent
a result written to the document               none -- sha256 identical
```

## The limits, stated

**Stale geometry and stale mesh are not reachable through the CLI**, because
every process regenerates and remeshes from current intent. The core's
refusals remain P17-VALID-001's and the CLI demonstrably uses that gate; what
the CLI presents instead is a model that will not regenerate, whose three
codes are reached by `cli.mesh.*` rather than by a P17 fixture.

**A warning-only analysis is not reachable either**, measured at five mesh
sizes. The behaviour is correct by construction — the gate is `acceptable()`
and no status enumerator is named — and that is what is claimed, rather than a
test that does not exist.

**The tie-break branch is not reachable**, with the count measured: the maximum
is unique, 1 of 9 nodes and 1 of 12 elements. The rule is encoded in the test
so it would kill a reversal the moment a tie arose.

Each of the three is a statement about what this surface can present, not a
claim that the underlying rule is untested.
