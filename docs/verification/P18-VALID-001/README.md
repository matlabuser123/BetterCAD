# P18-VALID-001 — Thermal Validation and Acceptance

```text
MILESTONE:  P18-VALID-001                                  the thirteenth milestone of P18 - Thermal Analysis
BASELINE:   HEAD bc62b0ce (P18-BALANCE-001 record), origin == HEAD, working tree clean, 4927 tests
ADDS:       one authoritative acceptance decision for a thermal analysis, as a deterministic structured report
CHANGES:    nothing qualified (P16, P18-ARCH … P18-BALANCE, linalg, structural: byte-identical to HEAD, compared by blob in FREEZE.md)
NOT STARTED P18-VIZ-001, P18-CMD-001, P18-PERSIST-001, P18-CLI-001, P18-REFMOD-001, P18-QUAL-001
```

## What this milestone is

By the end of P18-BALANCE-001 every milestone decides one thing about its own output. None decides whether an ANALYSIS may be believed — and a result can be algebraically perfect and
be about a mesh the user has replaced, a material the user has edited, a model that was never well posed, or elements the gradient cannot be recovered on. `validateThermalAnalysis` is
the one place that decides, from the evidence the predecessors already own. It solves nothing, repairs nothing, adds no formula and adds no tolerance except the thermal mesh bounds,
which are derived here from a measurement because P16 measures and states no opinion.

```text
validateThermalAnalysis(document, regenerator, mesher, analysis, ThermalDerivedState) -> ThermalValidationReport
   15 ordered rules (TV_*) · findings with a code, a severity that is a function of the code, a subject and the numbers · metrics · the source it describes · the policy fingerprint
   status() = Pass | PassWithWarnings | Fail          derived every time, never stored
   accepted() · solveAccepted() · inputsAccepted()     three different questions, three different answers
```

## Documents

| Document | What it holds |
| --- | --- |
| [VALIDATION_MODEL.md](VALIDATION_MODEL.md) | the rules, the failure policy, the three acceptance questions, ownership, what the validator never does |
| [MESH_ACCEPTANCE.md](MESH_ACCEPTANCE.md) | the measured gradient-recovery laws and the thresholds inverted from them |
| [MATERIAL_VALIDATION.md](MATERIAL_VALIDATION.md) | steady needs `k`; transient needs `k`, `rho`, `cp` |
| [LOAD_BC_VALIDATION.md](LOAD_BC_VALIDATION.md) | loads and conditions, no-load and no-condition models |
| [SOURCE_CURRENTNESS.md](SOURCE_CURRENTNESS.md) | what each edit stales; same counts, same ids, new generation |
| [WELL_POSEDNESS.md](WELL_POSEDNESS.md) | pure Neumann fails in a steady and passes in a transient; Robin-only; fully constrained |
| [RESIDUAL_ACCEPTANCE.md](RESIDUAL_ACCEPTANCE.md) | the independent residual of every step, against the canonical tolerance |
| [BALANCE_ACCEPTANCE.md](BALANCE_ACCEPTANCE.md) | the balance gate and its independence of the residual |
| [RESULT_FINITENESS.md](RESULT_FINITENESS.md) | temperatures, recovered fields, cardinality |
| [WARNING_POLICY.md](WARNING_POLICY.md) | what warns, what fails, what is information |
| [STRUCTURED_REPORT.md](STRUCTURED_REPORT.md) | seven complete ordered reports, steady and transient, pass and fail |
| [DETERMINISM.md](DETERMINISM.md) | why a report is a function of the evidence only |
| [GUI_OWNERSHIP.md](GUI_OWNERSHIP.md) | the six-rule gate and three tolerances with three owners |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | the 35 attacks, the evidence, the findings |
| [MUTATIONS.md](MUTATIONS.md) | 78 probes, every one killed |
| [SCOPE.md](SCOPE.md) | what is in, what is out, what is recorded as a limitation |
| [FREEZE.md](FREEZE.md) | the candidate, the fingerprints, the commit |
| `qualification/` | the run scripts, the post-run analysis, the mutation harness and its logs, the source audit |
| ADR-053 | `docs/architecture/decisions/ADR-053-thermal-acceptance-is-decided-in-one-place-and-p16-only-measures.md` |

## Decisions

| # | Decision | Why |
| --- | --- | --- |
| D1 | One function owns acceptance; a gate fails any other owner | a second policy is a second answer, and the one that drifts toward "accepted" ships a wrong temperature |
| D2 | Status is derived; a skipped rule is never a pass | a report that stores "valid" can disagree with its own findings |
| D3 | Severity is `severityOf(code)` | a viewer has no field to change it (33 compile-fail cases) |
| D4 | The result rules are decided from plain evidence by `judgeSolveEvidence` | a typed result cannot be corrupted, so the tests change only the data |
| D5 | Tolerances are consumed, never copied | the residual tolerance is the canonical `ThermalSolverSettings`; the balance tolerance is `balanceWithinTolerance`; the mesh bounds are owned here |
| D6 | The mesh bounds are inverted from a measurement at the absolute-temperature reference condition | the structural number would have been three orders too loose for a thermal model |
| D7 | A report is current while its source equals a fresh one and its policy fingerprint is in force | a remesh with the same counts and ids is a different mesh |
| D8 | Input rules are judged independently; System, Solve, Result and Balance are blocked by an input failure | a cascade of numerical findings on a stale mesh buries the cause |
| D9 | `accepted()` needs the recovered fields and the balance; `solveAccepted()` does not | the result of a P18 analysis includes the flux and the heat it accounts for |

## The thermal mesh policy (derived in MESH_ACCEPTANCE.md)

```text
FAILURE   3r/R < 7.0e-6      (flattening law  err <= C / sqrt(3r/R), C <= 8.62e-13 at 307 K and 14 K/m)     kThermalRadiusRatioFloor
FAILURE   aspect > 700       (stretching law  err <= C * aspect,     C <= 4.28e-13, same condition)         kThermalAspectRatioCeiling
          both inverted at a 1e-9 relative gradient error with a factor of three                              kThermalGradientAccuracy
WARNING   3r/R < 3.0e-4 · aspect > 81 · min dihedral < 6.0e-3 rad · max dihedral > 3.125 rad                 the qualified envelope, rounded away
NONE      the dihedral angles carry no failure bound: no law was measured against them
```

## Tests

| Family | Cases | What |
| --- | --- | --- |
| `unit.ThermalValidation*` | **138** | hand-data seams, the policy and the report model, real documents, evidence corruption, the qualified-mesh sweep, the measured laws |
| `compile_fail.thermalvalidation.*` | **33** | the report cannot store a status, take a severity, be written through a const view; the derived state has no solver; there is no policy object |
| `architecture.validation-ownership*` | **8** | the real tree and seven fixtures, one defect each |
| **new in total** | **179** | 4927 + 179 = 5106 |

Groups (the zero-match guard holds a floor for each): policy 5 · report 4 · mesh on hand data 11 · mesh on real documents 9 · measured laws 3 · material 5 · loads and conditions 7 ·
source and currentness 9 · well-posedness 16 · residual 18 · balance 10 · finiteness and recovered fields 10 · transient setup 6 · end to end 6 · evidence corruption 11 ·
**refusals 8** (a control that does not exist, a body that is gone, a geometry edit that was not regenerated, a failed generation, no assembled system, an overflowing reduction,
a P16 invalid-element count on its own, a recovery or a balance never presented).

### The zero-match guard

`tests/cli/ZeroMatchGuard.cmake` holds 19 validation filters: the family at 138, fifteen groups, the compile-fail group at 33 and the ownership gate at 8. A filter that matches fewer
tests than its floor fails the guard, so a targeted run can never report PASS by running nothing.

### Every code has a witness

`qualification/source-audit.txt`: 50 codes in the enumeration, 0 never named by a test. Seven were reached by no scenario at the first count of this milestone
(`ControlNotFound`, `GeometryIneligible`, `MeshGenerationFailed`, `SystemMissing`, `SystemNotFinite`, and two that were only listed in a table); each now has a scenario.

## Mutation testing

78 probes of the production source, every one killed (72 by a failing test, 6 by an abort of the mutated program). Three survived the first campaign — each a test gap, each closed
with a test, each re-run and killed. Details, the brief's list mapped to probes, and what was not mutated and why: [MUTATIONS.md](MUTATIONS.md).

## Adversarial review

35 attacks, every one answered with a test, and with a mutation wherever a guard exists to remove. Four findings: one model gap (a balance that accounted for the right total and the wrong categories, now
`BalanceAttributionMismatch`) and three test gaps. No production defect changed a verdict. [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Scope audit

Overall validation implemented here: **YES**. Solving, assembling, recovering, balancing, remeshing, repairing: **NO**. GUI, visualisation, persistence, commands, CLI, reference models,
P18 qualification: **NO**. A new numeric acceptance constant: only the thermal mesh bounds. [SCOPE.md](SCOPE.md).

## Limitations recorded rather than hidden

* Real P16 meshes are single-solid, so multi-solid acceptance is not claimed; the disconnected-region rules are shown on synthetic systems.
* `h = 0` convection is not a valid condition in this version; "h > 0" is the current policy, not an extra rule.
* Prescribed temperatures are constant in time in this version, so the initial-versus-prescription override at t = 0 is information.
* A `FinalOnly` transient of more than one step can be solve-accepted and cannot be fully accepted; the report says to request `EveryStep`.
* The mesh bounds are a gradient-recovery accuracy guarantee at a stated reference condition (14 K/m on a 307 K level), not a discretisation-error bound and not a conditioning bound.
* The dihedral angles carry no failure bound.
* A balance PASS is not a solve PASS, and neither proves the physics: accuracy against analytical solutions belongs to the reference models (P18-REFMOD-001).
* No sanitizer coverage: this toolchain ships no ASan/UBSan (carried constraint 6).
* `TODO.md` ends with Future Phases in the brief; in the file as it stands, three sections follow it, as at `HEAD`. The order was kept.

## Result

```text
PASS 2026-10-23, first run on the frozen tree
QUALIFIED:   HEAD bc62b0ce plus the working tree, fingerprints at the freeze == before the first build == after the last test
             apps a95876bc · include 5eeb3868 · src 8a8bfb7a · tests b1ae3d16 · examples a955cf65 · cmake 5a382115 · CMakeLists.txt 3ec3c10f · CMakePresets.json 3229f0f8
RUN:         23:28:21 -> 04:41:08, 5h12m47s, 17 stages, 0 failed
PRESETS:     debug-ext        5106/5106   ctest 3774.59 s   0 warnings
             release-ext      5106/5106   ctest 3509.63 s   0 warnings
             debug-shared-ext 5106/5106   ctest 3769.77 s   0 warnings   (12 DLLs; libbettercad_thermal.dll exports the validator)
REPEAT:      1959 selected tests x 5, passed in release-ext (1211.75 s) and debug-ext (1348.31 s)
BUILD:       711 objects per preset, 0 warnings, 0 errors, no-op rebuild is one edge in each preset
ARCHITECTURE layering 533 files, 0 violations · validation ownership 497 files outside the thermal module, 0 violations
```

Cross-preset equivalence of the validation suite (`qualification/postrun-summary.txt`): 607 evidence lines per preset; the 32 `FP`, 28 `OVERALL`, 420 `REPORT`, 48 `FINDING`
and 11 `MATRIX` lines of debug-ext are identical to those of release-ext and of debug-shared-ext (0 differing). The four balance bit fingerprints of the neighbour milestone
are the same four values in every preset; the script's order-sensitive comparison printed False because they were printed in a different order, and the summary records the
comparison as sets (True).

Predecessor regression, in all three presets, targeted and inside the 5106: P18-BALANCE 69 cases, P18-POST 40, P18-TRANSIENT 72, P18-SOLVE 51 and Linalg 10, P18-ASSEMBLY 62,
P18-BC 92, P18-LOAD 66, P18-ELEMENT 60, P18-DOF 33, P18-MATERIAL 38, P18-DATA 36, structural 415, meshing 364: every one passes.

One infrastructure event, recorded: in the pre-freeze run of the full debug-ext suite (not the qualification) `cli.new.unicode-path` failed once under `ctest -j 8`
and passed 30 of 30 in isolation; it passed in all three qualification runs. It belongs to the CLI `new` command and shares no file with this milestone; it is the same class of
Windows file-sharing flake that P18-ELEM-001 recorded.