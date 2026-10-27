# P19-DATA-001 — CFD Analysis Data Model

```text
STATUS:    PASS
MILESTONE: P19-DATA-001, the second milestone of P19 — CFD Integration
SCOPE:     the canonical and derived CFD data types. No topology, no field
           array, no boundary-face resolution, no operator, no solve.
```

## Baseline

```text
HEAD at start      24a1496805e8059336dc5ead1e332dc6e281eabb  (P19-ARCH-001's record)
origin/main        24a1496805e8059336dc5ead1e332dc6e281eabb, verified
authorized         2026-10-27, the owner's "start P19-DATA-001", with the
                   checklist already recorded in TODO.md
```

## What was decided (ADR-061)

```text
1   the definition does NOT name the fluid: the fluid is the document's P15
    assignment (ADR-060); a material id here would be a second authority
2   the reference pressure is a value at a POINT (not a cell, not a height);
    the reference HEIGHT belongs to gravity, and a prescribed boundary pressure
    holds at that height and varies hydrostatically along its face
    a missing datum is storable but makes the analysis InputsUnavailable;
    an overdetermined one is refused by the definition
2a  a velocity inlet is a uniform normal speed, > 0, in v1
3   the initial condition exists in BOTH modes (a steady flow solve is a
    nonlinear iteration whose start matters) and has no identity, so there is
    no CfdInitialConditionId
4   every setting is canonical; there is no algorithm selector; convergence is
    not acceptance; output settings select no field (a result always has p, U, phi)
5   a semantic fingerprint (FNV-1a over a canonical text) records what intent a
    result came from; the revision still decides currency (P18-QUAL-001's D1 not taken)
6   the result holds provenance + metadata now; its fields arrive with
    P19-DOF-001; the latest failure is a structured record with nine stages
```

## The checklist

| item | answered by |
|---|---|
| `CfdAnalysisId` | core `Id.hpp`; a document object, widens to `ObjectId` |
| `CfdBoundaryConditionId` | core `Id.hpp`; a member id, does not widen |
| `CfdInitialConditionId` if needed | **not needed**: one initial condition per analysis (ADR-061 §3) |
| analysis mode | `CfdAnalysisMode` (an enum, not a bool) |
| fluid model | `CfdFluidProperties` {rho, mu}, derived; nu = mu / rho never stored |
| reference pressure policy | `CfdReferencePressure` + `pressureDatumProblem` |
| gravity policy | `CfdGravity` {vector, reference height}; absence = none |
| steady settings | `CfdSteadySettings` (iteration budget) |
| transient settings | `CfdTransientSettings` (step, end, per-step budget, sampling) |
| nonlinear-iteration settings | `CfdIterationSettings` (two under-relaxation factors) |
| convergence settings | `CfdConvergenceSettings` (two tolerances, divergence factor) |
| output settings | `CfdOutputSettings` (residual history) |
| result metadata | `CfdResultMetadata` |
| source/provenance metadata | `CfdResultSource`, `staleReasons` |
| stale/current semantics | `resultCurrency`, `AnalysisState` |
| current accepted result | `CfdResultSlot::result()`, only via `recordSuccess`, only from `CfdResult::create` |
| latest failed attempt | `CfdSolveFailure`, `CfdResultSlot::failure()` |
| prevent derived fields becoming canonical | size + count pins; six compile-fail cases; the result header not reachable from the definition's |
| deterministic semantic fingerprint | `canonicalText`, `fingerprint`; two independent goldens; 40-row truth table |

Detail: [DATA_MODEL.md](DATA_MODEL.md), [CANONICAL_DERIVED.md](CANONICAL_DERIVED.md),
[CURRENTNESS.md](CURRENTNESS.md), [FINGERPRINT.md](FINGERPRINT.md).

## Tests

```text
tests/cfd/CfdDataTests.cpp         39 cases (unit.CfdData_*)
tests/compile_fail/CfdIdMisuse.cpp 28 cases (compile_fail.cfdids.*) and a control
the P19-ARCH boundary              14 cases, unchanged, still passing
zero-match guard                   unit\.CfdData_ 39; four cases by name;
                                   compile_fail\.cfdids\. 28
```

## Adversarial review

```text
INDEPENDENT REVIEWER   0 blockers; 3 major, 10 minor, 1 suspected -- all fixed or
                       recorded; second pass verified all 15, no new code defect,
                       6 doc wordings corrected
MUTATION PROBES        20 of 20 killed, every file restored
AUTHOR'S QUESTIONS     9, each answered by a test
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

## Known limitations

```text
The result carries no field arrays until P19-DOF-001 (ADR-061 §6).
The fluid's viscosity is not resolved until P19-FLUID-001; CfdFluidProperties
  is defined, the resolution is not.
Saving a document with a CFD analysis is refused by name until P19-PERSIST-001.
Faces resolving, boundary cardinality, inflow orientation, wall tangency and
  the reference point lying in the fluid are P19-BC-001's.
The gravity reference height is inert in a domain no boundary pressure fixes,
  and defaults to 0 m (ADR-061 §2).
Results are revision-keyed: an edit typed back, or a rename, leaves a result
  stale (P18-QUAL-001 owner decision D1, untaken).
The step count of a transient analysis is bounded by P19-TRANSIENT-001, not here.
This MinGW toolchain has no ASan/UBSan.
```

## Regression

Three presets, each configured, **cleaned**, rebuilt and run unfiltered, then two
repeat stages, all in one detached run: 12:07:55 to 18:51:18, 6h43m23s, 17 stages,
`qualify.cmd exit 0`, first attempt. Because core's `Id.hpp` changed, every module
recompiled in every preset.

```text
PRESET            CLEAN  BUILD    NO-OP REBUILD  CTEST
debug-ext           0    30m34s     1 edge       5893/5893   4951.99 s
release-ext         0    36m31s     1 edge       5893/5893   4708.31 s
debug-shared-ext    0    31m03s     1 edge       5893/5893   4948.05 s

REPEAT (5x each of 2622 selected tests)
release-ext                                      2622/2622   1645.94 s
debug-ext                                        2622/2622   1989.65 s

warnings, all three clean builds   0   (-Werror, 750 objects each)
zero-match groups                  240, 0 short, 0 unexecuted, in each preset
CFD cases on each preset's binary  2244 assertions in 53 cases, identical
golden fingerprint                 cfd-definition-v1:20707079b32e4964, identical
```

[FREEZE.md](FREEZE.md).

## Result

```text
RESULT:   PASS
TESTS:    5893/5893 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 750 objects; 2622 x 5 repeats in two presets;
          17 stages, 0 failed
TREE:     include 859e5bf3, src 16c04261, tests ada28684 moved; apps, examples,
          cmake, CMakeLists.txt, CMakePresets.json identical to HEAD; identical
          before and after the run
EVIDENCE: this directory
ADR:      ADR-061
NEXT:     P19-FLUID-001 is NOT AUTHORIZED
```

## Revision

First issue, 2026-10-27.
