# P17-QUAL-001 — CLI audit

```text
SUBJECT:  whether bettercad-cli implements any structural logic of its own,
          whether its numbers are the core's, whether a refusal reaches the
          exit status, and whether a report command can write the document
METHOD:   grep for the forbidden construct; map each command to the core
          entry it calls; name the test that compares CLI and core
```

## The commands

```text
REPORTS (write nothing)
  structural-settings        the canonical analysis, and nothing derived
  structural-validate        validateStructuralAnalysisForSolve, verbatim
  structural-solve           solveStructuralAnalysis: counts, residual, pivot,
                             energy, extremes, equilibrium
  structural-displacement    the same solve; nodal extremes with deterministic
                             ascending-NodeId tie-break
  structural-stress          the same solve; element extremes, ascending ElementId
  structural-reactions       the same solve; totals per restraint and support

EDITS (write the file only if the edit succeeded)
  structural-analysis-add    structural-load-add       structural-load-remove
  structural-restraint-add   structural-restraint-remove
  batch                      the edits are scriptable as one transaction
```

## No second implementation

```text
in apps/bettercad_cli/:
  Eigen, StiffnessMatrix, computeTet4Stiffness, assembleStructuralSystem,
  solveStructuralSystem, recoverFields, isotropicElasticity,
  recoverSupportReactions, structuralQualityThresholds        0 calls
  solveStructuralAnalysis                                     1 call, in the one
                                                              solve() helper the
                                                              four result commands
                                                              share
  validateStructuralAnalysisForSolve                          2 calls: prepare,
                                                              and structural-validate
  a stress, strain, von Mises, principal or energy formula    none; every scalar
                                                              is a read of the
                                                              outcome's members
  extrema                                                     SELECTION only, with
                                                              the tie-break stated
```

The CLI cannot reach a tolerance, a threshold or a settings value except
through the document: solver settings are canonical on the analysis
(ADR-047), the quality policy is `structuralQualityThresholds()` inside the
one validation entry, and there is no flag that bypasses validation
(`StructuralCli_ThereIsNoFlagThatBypassesValidation`).

## CLI / core equivalence

```text
StructuralCli_SolveMatchesTheCore                 counts, residual, pivot,
                                                  energy, extremes equal the
                                                  in-process outcome
StructuralCli_DisplacementSummaryMatchesTheCore   the same node, the same value
StructuralCli_StressSummaryMatchesTheCore         the same element, the same value
StructuralCli_ReactionSummaryMatchesTheCore       the same totals
StructuralCli_ValidationMatchesTheCore            the same findings and stage
StructuralCli_EveryMutationMatchesTheCoreCommand  each edit command equals the
                                                  P17-CMD command on the document
StructuralCli_TheProseAndTheJsonCarryTheSameNumbers
refmod.structural.solve.axial-bar.exact           the fresh-process CLI pins
                                                  2.15354e-05 m and 5.0e7 Pa,
                                                  the closed form's numbers, on
                                                  the committed RM-FEA-02 document
```

## Refusals reach the exit status

```text
code                          where                         exit
document_not_loaded           prepare                        1
regeneration_failed           prepare                        1
dependency_cycle              prepare                        1
regeneration_blocked          prepare                        1
no_structural_analysis        prepare / settings / validate  1
ambiguous_structural_analysis prepare (two analyses)         1
mesh_generation_failed        solve helper / validate        1
<validation code>             the staged report's first      1
                              refusal, e.g. insufficient_restraint,
                              loads_unresolvable, mesh_stale
solve_failed: <core message>  the solve entry's own refusal  1
                              (singular, residual, equilibrium)
```

`StructuralCli_EveryRefusalClassReachesTheExitStatus` drives each class;
`refmod.structural.refused.under-constrained.*` and
`refmod.structural.refused.unresolved.*` prove the two committed refusal
models through all four result commands in fresh processes, matching
`solve_failed: .*singular` and `loads_unresolvable` on stderr with nothing
on stdout. Probe Q6 in `MUTATION.md` makes the solve-failure path return exit
0 and records what caught it.

## A report command never writes

```text
StructuralCli_SolvingNeverChangesTheDocument      the file's bytes before and
                                                  after every report command
StructuralCli_AFailedMutationLeavesTheFileExactlyAsItWas
StructuralCli_ARepeatedCommandGivesTheSameBytes   byte-identical stdout over
                                                  repeated runs
StructuralCli_StructuredOutputIsTheWholeOfStdout  --json is the whole of stdout;
                                                  diagnostics go to stderr
```

## Cross-preset

The three committed documents solved by each preset's own CLI gave
byte-identical payloads in P17-REFMOD-001's freeze; the same comparison is
repeated on this candidate's binaries in `CROSS_PRESET.md`.

## Verdict

```text
CLI PASS                   no structural logic, no bypass, no write on report
CLI / core equivalence     PASS, in process and in fresh processes
```
