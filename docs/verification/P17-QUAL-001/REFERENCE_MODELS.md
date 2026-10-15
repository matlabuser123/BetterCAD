# P17-QUAL-001 — final reference matrix

```text
SUBJECT:  every mandatory P17 reference model, every property the final gate
          names, re-run on the frozen candidate's own binaries in all three
          presets after the qualification (qualification/postrun/)
VALUES:   PASS / FAIL / N/A only. An expected refusal is never PASS as a
          capability; a property a model does not have is N/A
EXPECTED: 10 model IDs          EXECUTED: 10          DOCUMENTS: 12
          10 solved, 2 refused, every solvable one refused as mesh_stale
          after its main dimension moved and re-solved after a remesh
```

## The matrix

```text
| Model      | Validate | Solve | Analytic | Force eq. | Moment eq. | Patch | Axial | Converge | Refusal | Stale | Save/Load | CLI  | 3 presets | PASS |
| RM-FEA-01  | PASS     | PASS  | PASS     | PASS      | PASS       | PASS  | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-02  | PASS     | PASS  | PASS     | PASS      | PASS       | N/A   | PASS  | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-03  | PASS (w) | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | PASS     | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-04  | PASS     | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-05  | PASS     | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-06  | PASS x2  | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-07  | PASS x2  | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-08  | PASS     | N/A   | N/A      | N/A       | N/A        | N/A   | N/A   | N/A      | PASS    | N/A   | PASS      | PASS | PASS      | PASS |
| RM-FEA-09  | PASS     | PASS  | PASS     | PASS      | PASS       | N/A   | N/A   | N/A      | N/A     | PASS  | PASS      | PASS | PASS      | PASS |
| RM-FEA-10  | PASS (r) | N/A   | N/A      | N/A       | N/A        | N/A   | N/A   | N/A      | PASS    | N/A   | PASS      | PASS | PASS      | PASS |

(w)  accepted_with_warnings: two element_outside_qualified_envelope warnings,
     an envelope and not a rejection (ADR-042)
(r)  rejected at the loads stage with loads_unresolvable, which IS the
     expected validation outcome for this model
```

## What each column means, and what proves it

```text
Validate    validateStructuralAnalysisForSolve on the committed document
            reaches the stage the model declares with the findings it
            declares: Complete for eight, Complete-with-8-DOFs for RM-FEA-08,
            Loads for RM-FEA-10. The runner prints the stage and the findings
            (reference-run-<preset>.txt), and StructuralReferenceSuite_*
            asserts them.
Solve       solveStructuralAnalysis publishes a result under the production
            default EquilibriumTolerance: residual 1e-17 .. 5e-17, pivot
            ratio 4.8e-4 (RM-FEA-03) .. 0.33 (RM-FEA-04).
Analytic    the closed form in tests/reference/Analytic.hpp, which includes no
            BetterCAD header: ANALYTICAL_VALIDATION.md has the gate and the
            measurement for every row.
Force eq.   the reactions balance the assembled external load, normalised by
            the magnitudes that participate: EQUILIBRIUM.md.
Moment eq.  likewise about the origin, where relevant: every solved model, with
            RM-FEA-03's 5.98e-13 the largest and F1 recorded beside it.
Patch       RM-FEA-01: an affine field reproduced to roundoff on one element
            and on a 12-element mesh with an interior node; K u equals the
            boundary tractions and is zero inside.
Axial       RM-FEA-02: delta = FL/AE, sigma = F/A, R = -F, U = F delta / 2 at
            1e-9, measured at 1e-15.
Converge    RM-FEA-03: strictly increasing counts, monotone deflection, falling
            error, 1.27x coarse to fine -- a TREND, with F2 and F3 recorded.
Refusal     RM-FEA-08 by the pivot gate with nothing published; RM-FEA-10 at
            the loads stage with nothing rebound. The same refusal through
            all four CLI result commands in fresh processes, exit 1.
Stale       the runner edits the main dimension of every solvable document:
            currency "stale: the geometry changed", validation rejected with
            mesh_stale, solve refused; then remeshed and re-solved to a
            different answer. RM-FEA-09 is the committed form of the case.
Save/Load   every document round-trips its intent, holds no mesh and no
            result, and re-solves to the same fingerprint bit for bit (or to
            the same refusal class); saving before and after a solve gives
            the same bytes (P17Qualification_SolvingLeavesEveryReferenceDocumentByteIdentical).
CLI         the 41 refmod.structural.* fresh-process tests on the runner's
            own files, in every preset.
3 presets   the same fingerprints and the same CLI payload bytes from the
            debug-ext, release-ext and debug-shared-ext binaries:
            CROSS_PRESET.md.
```

## RM-FEA-08 and RM-FEA-10: what is N/A and what is PASS

```text
N/A, because the model HAS no such property:
  Solve, Analytic, equilibrium, stale    nothing is published, so there is no
                                         result to validate, balance or stale
PASS, because the model's EXPECTED BEHAVIOUR is an explicit refusal and it
behaves that way:
  RM-FEA-08   validation accepts (8 constrained DOFs is >= 6, and six is not a
              claim of sufficiency); the solver refuses with "the system is
              singular or too ill conditioned to solve: the factorisation's
              smallest pivot is 1.1e-17 of its largest, below the floor of
              1e-12"; published_result no; the same diagnostic text over three
              attempts; exit 1 with solve_failed: ... singular from all four
              CLI result commands
  RM-FEA-10   validation rejects at the loads stage: "load:2: its target face
              names no face of the body as it is now, so the load has nothing
              to act on. Nothing nearby is substituted"; the valid restraint
              resolved first (12 constrained DOFs); published_result no;
              exit 1 with loads_unresolvable
  Save/Load   both reload to the same refusal class, so the refusal is a
              property of the document and not of the process that built it
```

## Figures

Every number this matrix summarises is in `EQUILIBRIUM.md` (the runner's
per-document table and the model-change table), `ANALYTICAL_VALIDATION.md`
(the tolerance table) and `CROSS_PRESET.md` (the fingerprints and payload
hashes), each transcribed from the frozen candidate's own output under
`qualification/postrun/`.
