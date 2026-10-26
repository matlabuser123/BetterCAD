# P18-QUAL-001 - Full P18 Qualification

**Verdict: PASS, 2026-10-27. P18 - Thermal Analysis is qualified.** Eighteen milestones audited from their own recorded verdicts, 5804 of 5804 tests on each of three clean presets, 2561 tests repeated five times
in two, 0 warnings over 743 objects per preset, 17 harness stages exit 0, qualified tree == committed tree. Three defects were found at boundaries and fixed. **Three findings are open and reported, not hidden - one of
them (F1, below) matters.** P19 - CFD Integration is next and **not authorized**.

## What this milestone is

The qualification is not a re-run of the 18 suites (the three-preset regression runs all of them anyway). It is an attack on the integrated system, in the order the brief sets:

1. **Baseline and audit.** `HEAD == origin/main == ece7691`, only `.claude/` untracked. All 18 predecessor TODO blocks: 0 open boxes, a dated PASS block, `0 stage(s) failed` (`MILESTONE_AUDIT.md`, `TODO_AUDIT.md`).
2. **Three read-only audits** of the code and the tests against the brief and the TODO's 33 adversarial questions: 330 rows, each with `file:line` evidence and the exact names of its covering tests (`audit/`,
   `REQUIREMENT_MAP.md`). The first pass found **no wrong number**; it found 3 clause-level gaps and 43 partial rows and a short list of suspected defects.
3. **Fixes and tests**, by the TODO's discipline (reproduce, regression test, root cause, general fix): D-A, D-B, D-F (`FINDINGS.md`), each reproduced against the old code (`repro-old-code.txt`), plus 15 tests closing gaps and
   `tests/qualification/P18ThermalQualificationTests.cpp` (the final cross-milestone gates: the authority matrix over 20 documents, the currentness matrix in one sequence with undo in the middle, the failed regeneration, the
   manual edit-back, possession of a `ThermalModel`, the units of the transient sum).
4. **Mutation campaign**: 28 production mutants, 27 killed, 1 documented survivor (`MUTATION.md`).
5. **Independent adversarial review** by a second reviewer who was not given the audit's conclusions (`ADVERSARIAL_REVIEW.md`). It found no wrong accepted result and three major findings (F1-F3), all left to the owner.
6. **Freeze** of 18 contract fingerprints, twice, then the **clean three-preset qualification** with the unchanged harness (`FREEZE.md`, `qualification/`), then the post-run analysis (`qualification/postrun-summary.txt`).

## The numbers

| | debug-ext | release-ext | debug-shared-ext |
|---|---:|---:|---:|
| clean build, warnings as errors | 31 min, 0 warnings, 0 errors | 39 min, 0 / 0 | 31 min, 0 / 0 |
| objects built | 743 | 743 | 743 |
| no-op rebuild | nothing to do | nothing to do | nothing to do |
| full unfiltered ctest | **5804 / 5804** | **5804 / 5804** | **5804 / 5804** |
| repeat (2561 tests, until-fail:5) | pass | pass | - |
| `[refmod][thermal]` | 47 / 47 cases | 47 / 47 | 47 / 47 |
| `[p18qual]` final gates | 7 / 7 (1012 assertions) | 7 / 7 | 7 / 7 |
| independent verifier on fresh payloads | 28 / 28 | 28 / 28 | 28 / 28 |
| zero-match groups (expected, discovered, executed) | 234 groups, 0 mismatches | | |

17 stages, 0 failed; 6 h 50 min 37 s; first attempt. 234 zero-match groups each discovered at least its minimum and **executed exactly what was discovered** on every preset; an impossible filter discovers 0.
The binaries were proven fresh (newer than their preset's clean and than the newest source file; `FRESH_BINARY.md`).

## Evidence index

| topic | file |
|---|---|
| milestone and TODO audit | `MILESTONE_AUDIT.md`, `TODO_AUDIT.md` |
| ADRs | `ADR_AUDIT.md` |
| authority, currentness, DOF | `AUTHORITY_MODEL.md`, `CURRENTNESS.md`, `DOF_MODEL.md` |
| formulation, loads, assembly, solvers | `ELEMENT_FORMULATION.md`, `LOADS_BCS.md`, `ASSEMBLY.md`, `STEADY_SOLVER.md`, `TRANSIENT_SOLVER.md` |
| results, balance, validation | `POST.md`, `BALANCE.md`, `VALIDATION.md` |
| viz, commands, persistence, CLI | `VIZ.md`, `COMMANDS.md`, `PERSISTENCE.md`, `CLI.md` |
| reference models, analytical validation, tolerances | `REFERENCE_MODELS.md`, `ANALYTICAL_VALIDATION.md`, `TOLERANCES.md` |
| mutation, adversarial review | `MUTATION.md`, `ADVERSARIAL_REVIEW.md` |
| findings, limitations | `FINDINGS.md`, `LIMITATIONS.md` |
| freeze, determinism, cross-preset, fresh binary, final gate | `FREEZE.md`, `DETERMINISM.md`, `CROSS_PRESET.md`, `FRESH_BINARY.md`, `FINAL_GATE.md` |
| requirement index | `REQUIREMENT_MAP.md`, `audit/` (the three row-level audits) |
| harness and raw logs | `qualification/` (`qualify.cmd`, `verify-harness.cmd`, `launch.cmd`, `run-qualification.cmd`, `freeze-qual.ps1`, `postrun-qual.ps1`, `verify-independent.ps1`, build / clean / configure / ctest logs, the workflow ledgers and payloads of each preset, the mutation logs) |

## What is open (read this)

* **F1 - the balance gate refuses correct transient runs once the time step is small.** Reproduced: the committed `ThermalTransientSlab` (40 x 40 x 100 mm) is accepted at dt = 0.01 s and refused at dt = 0.003, 0.001, 0.0003 s
  with `balance_out_of_tolerance`, while the independent physical residual stays at 1e-15 (a 0.25 m^3 steel bar is refused at dt <= 3 s). The cause is a missing `eps |T| |C| / dt` term in the transient arithmetic scale.
  It errs on the safe side - no wrong result is accepted - but a user cannot get a valid small-step answer. **It was not fixed because the only fix loosens a tolerance in the small-step regime, and a tolerance is not
  loosened by the party qualifying it.** The exact change, its location, the test that would pin it, and its interaction with the policy fingerprint (F7) are in `ADVERSARIAL_REVIEW.md`. It should be the first item of the next
  decision about P18.
* F2 (no bound on a temperature) and F3 (no bound on the residual tolerance a user may set) are owner decisions.
* The other limitations are in `LIMITATIONS.md`.
* **`TODO.md` ordering.** The brief asks for the Future Phases section to be the absolute end of `TODO.md`. In the repository (as at HEAD and at every earlier milestone) three sections follow it - Workflow, Project Authority
  and CURRENT NEXT STEP. The order was kept and is reported, not changed; the claim "Future Phases is the final section" is therefore **not** true of the file.

## Commit

Committed as `b5dce0773b620ef4c628ae64c534e58a9f66b4ce` (`BetterCAD: qualify P18 thermal analysis`, 930 files); all eight subtree hashes equal the frozen fingerprint (`FREEZE.md`): qualified tree == committed tree.

## Next

`P19 - CFD Integration`. **Not authorized. Do not start P19.**
