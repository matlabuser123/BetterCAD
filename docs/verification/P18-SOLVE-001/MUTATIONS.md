# P18-SOLVE-001 — mutation probes

```text
SUBJECT:  deliberate defects applied to the production source, each run against the milestone's
          tests, to show the tests can fail
RESULT:   43 distinct mutations, 43 killed, 0 survived, 0 void; 2 test gaps found and closed
```

## Method

Each probe is a textual replacement in one production file, a build of `bettercad_tests` in
`debug-ext`, a run of the tests, and an unconditional restore. The SHA-256 of the three files probed
(`ThermalSolve.cpp`, `SymmetricSolve.cpp`, `StructuralSolve.cpp`) before the first probe and after the
last were equal in both batches. Probes S, L and W ran the milestone's own tests (`[thermal][solve]`
and `[linalg]`, 60 then 61 cases); the two W probes, which mutate the structural wrapper around the
extracted kernel, ran **all 415 structural cases**. ARCH1 and ARCH2 mutate the architecture gate and
run its script, not a build. A probe is **killed** if the run exits non-zero. One was killed by an
abnormal exit (S23 aborted the test binary): a kill, but a blunter signal than an assertion, and marked.

Four probes (S01, S02, S15, S19) did not compile under `-Werror` (an unused variable made by the mutation
itself); they were reformulated with a multiplication by zero or a dead conditional so that the
code under test is still disabled, and the reformulated forms are the ones counted.

## Results

| ID | Mutation | Verdict | Test cases failing |
| --- | --- | --- | --- || S01b | beff = Qf (the Kfc Tc term omitted) | KILLED (first form did not compile under -Werror; reformulated) | 21 of 61 |
| S02b | prescribed temperatures forced to zero | KILLED (first form did not compile under -Werror; reformulated) | 22 of 61 |
| S03 | Kfc Tc added instead of subtracted | KILLED | 21 of 60 |
| S04 | free equations assumed to be the first Nfree unknowns | KILLED | 34 of 60 |
| S05 | prescribed ordinal reversed in Kfc | KILLED | 14 of 60 |
| S06 | Nfree = 0 reported as singular | KILLED | 1 of 60 |
| S07 | at least one prescribed temperature required | KILLED | 9 of 60 |
| S08 | epsilon added to the reduced diagonal | KILLED | 29 of 60 |
| S09 | first free unknown pinned by a large diagonal | KILLED | 36 of 60 |
| S10 | anchoring analysis does not refuse an unanchored region | KILLED | 7 of 60 |
| S11 | full-system residual (reactions) treated as a free-equation failure | KILLED | 31 of 60 |
| S12 | stale assembled system accepted | KILLED | 2 of 60 |
| S13 | stale prepared boundary accepted | KILLED | 1 of 60 |
| S14 | solver settings dropped from the solution source | KILLED | 1 of 60 |
| S15b | prescribed conditions dropped from the solution source | KILLED (first form did not compile under -Werror; reformulated) | 4 of 61 |
| S16 | settings ignored by the staleness comparison | KILLED | 2 of 60 |
| S17 | prescribed temperatures ignored by the staleness comparison | KILLED | 3 of 60 |
| S18 | a constrained temperature perturbed after reconstruction | KILLED | 33 of 60 |
| S19b | rounding noise counts as an anchor (tolerance zero) | KILLED (first form did not compile under -Werror; reformulated) | 2 of 61 |
| S20 | a weak real anchor is not recognised (tolerance 1e-3) | KILLED | 1 of 60 |
| S21 | stored zeros connect regions | KILLED | 1 of 60 |
| S22 | one anchored row anchors every region | KILLED | 2 of 60 |
| S23 | every disconnected model rejected | KILLED | abnormal exit (-1073740791): the binary aborted |
| S24 | missing datum and unanchored region swapped | KILLED | 7 of 60 |
| S25 | transient system accepted by a steady solve | KILLED | 1 of 60 |
| S26 | numbering of another mesh accepted | KILLED | 1 of 60 |
| S27 | constraint-held postcondition removed from the candidate check | KILLED | 1 of 60 |
| S28 | candidate finiteness check removed | KILLED | 1 of 60 |
| S29 | kernel refusals reclassified: singular as factorisation failure | KILLED | 1 of 60 |
| S30 | non-finite reduced right-hand side accepted | KILLED (survived the first pass; closed by a new test) | 1 of 61 |
| L01 | pivot gate removed from the kernel | KILLED | 2 of 60 |
| L02 | residual threshold not applied | KILLED | 3 of 60 |
| L03 | solution finiteness check removed | KILLED | 1 of 60 |
| L04 | residual finiteness check removed | KILLED (survived the first pass; closed by a new test) | 1 of 61 |
| L05 | raw absolute residual used instead of the normalized one | KILLED | 5 of 60 |
| L06 | indefinite pivots accepted (positivity check removed) | KILLED | 2 of 60 |
| L07 | exact zero pivot reported as a factorisation failure | KILLED | 3 of 60 |
| L08 | right-hand side finiteness not checked | KILLED | 1 of 60 |
| L09 | residual evaluation ignores the matrix scale | KILLED | 2 of 60 |
| W01 | structural wrapper: singular classified as factorisation failure | KILLED | 9 of 415 |
| W02 | structural wrapper: the singular message loses the exactly-zero-pivot wording | KILLED | 1 of 415 |
| ARCH1 | `BetterCAD::structural` added to thermal's `PRIVATE_LINK` | KILLED | `architecture.targets.link-graph` exits 1 |
| ARCH2 | `#include <bettercad/structural/StructuralSolve.hpp>` added to `ThermalSolve.cpp` | KILLED | `architecture.layering` exits 1 |

## The brief's list, mapped

| Brief item | Probe |
| --- | --- |
| include the P17 structural solver directly | ARCH2 (the layering check refuses it) |
| copy the P17 solver body | not a code mutation: the thermal solve contains no factorisation (audit, ADVERSARIAL_REVIEW.md) |
| `beff = Qf` | S01 (and S03 for the wrong sign) |
| `Tc` forced to zero | S02 |
| free equations assumed to be the first `Nfree` unknowns | S04 |
| `Nfree = 0` treated as singular | S06 |
| require at least one Dirichlet | S07 |
| add `eps I` to `Kff` | S08 |
| pin the first free unknown | S09 |
| accept a pseudo-inverse result | not a mutation: there is no code path that returns a result for a singular model; S10 and L01 remove the two refusals and the tests fail |
| accept solver Success without the residual check | L02, L05, L09 |
| skip `isfinite(T)` | L03, S28 |
| skip `isfinite(residual)` | L04 |
| check the full residual including prescribed unknowns | S11 |
| accept a stale assembled system by dimension equality | S12, S26 |
| accept a stale constraint map by count equality | the stamp comparison lives in P18-DOF's `buildThermalFreeEquationMap`; the seam test with an old map against a new system reports `constraint_mismatch` and the P18-DOF mutation set covers its check |
| convert `Kff` to dense | not mutable by a line edit; audited (no `n * n` allocation, no `toDense`; compile-fail ..._NO_DENSE_FORM; the smoke solves compare `nnz(Kff)` with `Nfree^2`) |
| raw absolute residual only | L05 |
| drop solver settings from the result provenance | S14, S16 |

## What the probes found: two test gaps, both closed

| Survivor (first pass) | Why it survived | New assertion that kills it |
| --- | --- | --- |
| S30 the reduction's own finiteness check | the kernel's input check refuses the same overflow one step later, and the classification is the same, so no test could tell which layer refused | `NonFiniteReducedRightHandSideIsRefused` now calls `reduceThermalSystem` directly and requires the refusal and its wording ("Qf - Kfc Tc overflowed") |
| L04 the per-row finiteness check in the residual loop | the norm check after the loop refuses the same overflow, again with the same classification | `Linalg_ResidualOverflowNamesTheRow` requires the row index and the "residual component 1" wording |

Neither was a defect: each check had a second line of defence behind it. The tests now pin the first
line, so a refactor that deleted it would be seen.

## Equivalent and defensive checks, stated honestly

- `ConstraintNotPreserved` is not reachable through the solve, whose reconstruction assigns the prescribed
  value. It is reachable through `validateSteadySolution` (a candidate with a prescribed unknown one ulp
  off), and S18 perturbs the reconstruction inside the solve and is killed by it: so the postcondition is
  live, not decorative.
- `InvalidSettings` is checked by the thermal entry points and again by the kernel; the second is
  reachable only through the generic API, where it has its own test (`Linalg_ValidatesItsInput`).
