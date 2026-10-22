# ADR-053 — Thermal acceptance is decided in one place, and P16 only measures

```text
STATUS:    Accepted
DATE:      2026-10-22
MILESTONE: P18-VALID-001
TOUCHES:   ADR-042 (where P17's staged acceptance landed), ADR-051 (the boundary this one consumes), ADR-050 (the layer it lives in),
           ADR-036 (possession is the proof), ADR-028 (no material data in a solver), ADR-030 (derived state carries its source)
```

## Context

By the end of P18-BALANCE-001 every milestone decides one thing about its own output. The assembler refuses an inconsistent system, the solve refuses a residual above the
tolerance the user set, the recovery refuses a non-finite field, the balance reports whether its books close. None of them decides whether an ANALYSIS may be believed — and
that is a different question, because a result can be algebraically perfect and be about a mesh the user has replaced, about a material the user has edited, about a model that was
never well posed, or on elements whose gradient cannot be recovered to a useful accuracy. A solver returning success is not that.

P16 measures a mesh and states no opinion (`reportOnlyThresholds()` classifies nothing, by design: "P17 owns what a structural analysis needs from a mesh"). P17-VALID-001 paid that debt
for the structural kernel and recorded the method: derive each hard threshold from a MEASURED law, keep a margin, set the warnings at the qualified envelope, and never choose a number
from a fixture comparison. P18 inherited the debt for the thermal kernel (`TODO.md`, carried constraint 3).

## Constraints

* `thermal` (45) cannot include `structural` (50), so P17's validation cannot be reused; its method and its report discipline can.
* A typed result (a solution, a recovery, a balance) has no public constructor and cannot be made invalid; the decision about a result must nevertheless be testable on a corrupted one.
* P16 stays generic mesh infrastructure and its metric computation may not change.
* There must be ONE tolerance for the free residual (the canonical `ThermalSolverSettings`, user intent) and ONE for the heat balance (P18-BALANCE-001's measured policy); a third copy is a
  second answer.
* Thermal quantities are ABSOLUTE temperatures. Nothing in the structural measurement carries that.
* Nothing in the validator may solve, repair, remesh or loosen.

## Options

### A — each milestone keeps its own gate and a consumer aggregates

The viewer or the CLI would combine "solver succeeded", "post is finite" and "balance within tolerance". Rejected: the combination IS the policy, and a GUI that holds it is a second owner whose
version can differ from a CLI's; severities would be reinterpreted per consumer; and no gate would prevent it.

### B — one function in the thermal module that produces a structured report, over the predecessors' own functions

`validateThermalAnalysis(document, regenerator, mesher, analysis, derived-state)`: ordered rules with stable ids, severities that are a function of the code, a status derived from the
findings, gating that skips (never passes) what it cannot judge, and the result rules decided by a function of plain evidence so a corrupted result can be shown to the SAME decision.

### C — put the thermal thresholds in P16's `QualityThresholds` default

Rejected: P16 would then own an application policy it cannot justify, every P16 consumer would inherit it, and P16 would need requalification. P16 reports; the consumer decides.

## Decision

**Option B**, with these clauses.

1. **One owner.** `validateThermalAnalysis` is the only place a thermal analysis is accepted. An architecture gate (`CheckValidationOwnership.cmake`) fails any file outside the thermal module that
   names a function or constant that decides, any thermal file that defines a competing acceptance API, a validator that solves, assembles, recovers, balances, integrates, computes a Jacobian or a
   mesh metric, regenerates or remeshes, a validator that carries a tolerance literal, one that does not read each owner's published function, and one that is non-deterministic.
2. **The report is derived.** `status()`, `accepted()`, `solveAccepted()`, `inputsAccepted()` and the counts are computed from the ordered checks and findings every time; nothing stores "valid".
   `accepted()` and `solveAccepted()` are different questions and the report answers both.
3. **Severity is a function of the code.** `severityOf(code)`; a finding carries it; a viewer has no field to change it.
4. **Skipped is never pass.** A rule whose prerequisite is missing or invalid is skipped and names the earlier rule that blocked it; a skipped rule counts against acceptance.
5. **The result rules are decided from plain evidence.** `judgeSolveEvidence` is public so the tests can show it a perturbed free temperature, a NaN flux, a balance edited in place or a history
   one state short, without weakening any production type.
6. **Tolerances are consumed, not copied.** The residual is judged by `validateSteadySolution` / `validateTransientStep` with the analysis's own settings; the balance by
   `balanceWithinTolerance`; the validator adds no numeric acceptance constant except the thermal mesh bounds.
7. **The thermal mesh bounds are derived from a measurement at the thermal reference condition.** The recovered-gradient error is `≤ C / sqrt(3r/R)` (flattening, C ≤ 8.62e-13) and `≤ C · aspect`
   (stretching, C ≤ 4.28e-13) at a 307 K level and 14 K/m — the ABSOLUTE-temperature condition that makes the thermal constants three orders larger than the isolating ones. Inverting each at the
   1e-9 relative band with a factor of three gives `3r/R ≥ 7.0e-6` and `aspect ≤ 700`. The four envelope warnings are the qualified worst case rounded away. The dihedral angles carry no failure bound.
8. **Report currentness.** A report is current exactly while its `ThermalResultSource` equals a freshly computed one AND the policy fingerprint is the one in force.

## Rationale

The alternative that looks cheaper — reuse P17's numbers — would have shipped a stretching bound three orders too loose for a thermal model, because the structural measurement had no
absolute offset in its field and therefore saw no degradation along the stretching family at all. The thermal measurement is the one that does. The alternative of aggregating in a consumer fails
the same way every ownership argument in this project has: the policy exists somewhere, and a gate that cannot see it cannot enforce it. Skipping instead of passing is what keeps a report
honest about a model that is not ready: a cascade of numerical findings on a stale mesh would bury the cause.

What would have made a different option right: if more than one acceptance policy were ever wanted at once (a lenient preview and a strict release), the policy would have to become a
parameter. It is deliberately not one now — thresholds are application policy, not canonical user intent — and the fingerprint exists so that when it does become one, a stored report
can say which it was produced under.

## Consequences

* A CLI and a GUI print and consume the same report; neither can disagree with the other about acceptance.
* The mesh policy is a short list of named constants with a derivation a reviewer can re-run (`ThermalValidationLawTests.cpp`).
* A new derived object (a future coupled field) must be added to the source-compatibility matrix and to the evidence, or the gate for its owner fails.
* P18-VIZ-001, P18-CMD-001, P18-PERSIST-001 and P18-CLI-001 consume the report; none may add a rule, a threshold or a severity.
* The thermal bounds are tied to a stated reference gradient. They are an accuracy guarantee for recovery at that condition, not a discretisation-error bound and not a conditioning bound; the
  solver's pivot and residual gates remain the guard of the latter two.
