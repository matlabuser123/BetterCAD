# P18-QUAL-001 - milestone audit (brief sections 6-8)

Method: `milestone-audit.ps1` (kept beside the other scripts in the session scratchpad; the output is reproduced here) reads `TODO.md`
and `docs/verification/P18-*-001/` and reports, per milestone, the number of ticked and unticked boxes, whether a dated `PASS` block
exists, the last line of its `qualification-times.txt`, the feature commit and the record commit. The baseline was
`HEAD == origin/main == ece7691063531cf033403829fa70ab40233c3673` (tree `fcce738bc6d7c8c6eb3ebcd15d16f8a3680803d3`), with only the
untracked `.claude/` directory present.

| milestone | `[x]` | `[ ]` | PASS block | qualification finished | stages failed | feature commit | record commit |
|---|---:|---:|---|---|---:|---|---|
| P18-ARCH-001 | 19 | 0 | yes | Fri 2026-10-16 05:26 | 0 | 7e7578f | e6fded3 |
| P18-DATA-001 | 19 | 0 | yes | Sat 2026-10-17 01:35 | 0 | 314166e | 843469a |
| P18-MAT-001 | 19 | 0 | yes | Sat 2026-10-17 07:29 | 0 | 448eb5e | d40001c |
| P18-DOF-001 | 12 | 0 | yes | Sat 2026-10-17 20:13 | 0 | daea78b | d4800f7 |
| P18-ELEM-001 | 21 | 0 | yes | Sun 2026-10-18 08:08 | 0 | 72872de | f10f5f0 |
| P18-LOAD-001 | 18 | 0 | yes | Sun 2026-10-18 22:13 | 0 | a96cedae | b4714a4 |
| P18-BC-001 | 17 | 0 | yes | Mon 2026-10-19 12:42 | 0 | a0745de3 | a5b8cc2 |
| P18-ASSEMBLY-001 | 20 | 0 | yes | Tue 2026-10-20 02:36 | 0 | c0ca6321 | 241c13a |
| P18-SOLVE-001 | 15 | 0 | yes | Tue 2026-10-20 20:22 | 0 | 8fdcb0c | 147d355 |
| P18-TRANSIENT-001 | 21 | 0 | yes | Wed 2026-10-21 10:43 | 0 | 693a950c | 7a9ec06 |
| P18-POST-001 | 16 | 0 | yes | Wed 2026-10-21 22:51 | 0 | 35839d3f | fa15d36f |
| P18-BALANCE-001 | 14 | 0 | yes | Thu 2026-10-22 10:40 | 0 | 60ce0a3e | bc62b0ce |
| P18-VALID-001 | 18 | 0 | yes | Fri 2026-10-23 04:41 | 0 | 15ad364d | c56aef05 |
| P18-VIZ-001 | 21 | 0 | yes | Fri 2026-10-23 21:25 | 0 | 7ba06a7 | 6b675ec |
| P18-CMD-001 | 22 | 0 | yes | Sat 2026-10-24 17:51 | 0 | fe16f9d | 975efa6 |
| P18-PERSIST-001 | 22 | 0 | yes | Sun 2026-10-25 07:32 | 0 | 3f00ca7 | 2ffba66 |
| P18-CLI-001 | 27 | 0 | yes | Mon 2026-10-26 02:06 | 0 | ebca776 | b274df2 |
| P18-REFMOD-001 | 24 | 0 | yes | Mon 2026-10-26 12:30 | 0 | 04751f6 | ece7691 |

(The feature-commit column for the first milestones is the hash their TODO blocks and evidence READMEs record; the record commit is the
`record the P18-xxx-001 commit` follow-up. All eighteen milestones were pushed and verified `origin/main == HEAD` at the time they closed.)

All 18 predecessor blocks have **0 open boxes**, a dated PASS block, and a `qualification-times.txt` whose last line is
`0 stage(s) failed`. There is no hidden unchecked item, no `PASS_WITH_WARNINGS`-style qualification and no milestone
qualified on fewer than three presets.

What each milestone qualified (one line each; the clause-by-clause audit is in `audit/`):

| milestone | what it qualified |
|---|---|
| ARCH | the thermal module at layer 45 (ADR-049/050/051), `requireThermalModel`, the checker rules and fixtures |
| DATA | canonical thermal analysis object, loads, boundary conditions, units, ids, the dead-but-tested `ThermalResult` state machine |
| MAT | thermal material resolution (k always; rho, cp only for a transient), no defaults |
| DOF | one scalar temperature per node, dense compact numbering, constraint and free-equation maps |
| ELEM | Tet4 conduction matrix, consistent capacity, shape gradients, degeneracy policy |
| LOAD | surface flux and volumetric source vectors, GeometryReference resolution, prepared loads |
| BC | prescribed temperature, convection (Robin), prepared boundary |
| ASSEMBLY | sparse symmetric K and C, heat vector, per-contribution accounting, stale reasons |
| SOLVE | steady reduced solve, anchoring analysis, residual gate, `linalg` extraction (ADR-052) |
| TRANSIENT | backward Euler on a fixed grid, resolved initial state, per-step evidence |
| POST | gradient and heat-flux recovery (q = -k grad T), source identity |
| BALANCE | steady and transient heat balance with the gross-scale tolerance policy |
| VALID | `validateThermalAnalysis`, 21 rules, mesh bounds from measured laws (ADR-053) |
| VIZ | read-only contours, vectors, boundary glyphs, display state, inspection (ADR-054) |
| CMD | commands editing one analysis definition, history, undo/redo (ADR-055) |
| PERSIST | one more object type in `.bcad`, intent only (ADR-056) |
| CLI | single production entry `runThermalAnalysis` and eight `thermal-*` commands (ADR-057) |
| REFMOD | ten reference models with an independent oracle, 47 in-process cases, a 58-process workflow |

P18-QUAL-001 itself changes only what its own audit proved necessary: three production fixes (`FINDINGS.md`), the tests that pin them,
the final-gate cases in `tests/qualification/P18ThermalQualificationTests.cpp`, and the gap-closing tests listed in `REQUIREMENT_MAP.md`.
