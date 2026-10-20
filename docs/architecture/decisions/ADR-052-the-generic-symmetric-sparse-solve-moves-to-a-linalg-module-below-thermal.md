# ADR-052 — The generic symmetric sparse solve moves to a `linalg` module below `thermal`

```text
STATUS:    Accepted
DATE:      2026-10-20
MILESTONE: P18-SOLVE-001
TOUCHES:   ADR-050 (the two admissible answers it left open), ADR-039 (the
           solver, its pivot gate and Eigen private to the modules that link
           it), ADR-035 (the layer table respaced in tens), ADR-038 (BetterCAD
           owns the sparse containers)
```

## Context

ADR-050 put `thermal` at layer 45, below `structural` (50), so that the one
coupling the roadmap names -- a temperature field driving thermal strain --
flows downward without a renumber. Its cost was stated in the same document:
P17's sparse solver, `structural::solveSymmetricSparse`, cannot be reached from
`thermal` by include. ADR-050 named two honest answers and refused a third,
and left the choice to the milestone whose boxes say "audit available linear
algebra" and "reuse qualified compatible solver infrastructure".

The audit (P18-SOLVE-001, `SOLVER_ARCHITECTURE.md`) found that the part of
`StructuralSolve.cpp` worth reusing is a self-contained kernel: about two
hundred lines taking a square CSR as three arrays, a right-hand side and two
ratios, and returning a solution, the independent residual and a
classification. It uses no structural type. The only structural things around it
are the units on its output (newtons, metres, N/m), the wording of three
diagnostics, and the reduction of a structural system to its free part -- and
those stay in `structural`.

## Constraints

- ADR-050: `thermal` may not include `structural`, and may not link it.
- Hand-copying the kernel into `thermal` is refused by ADR-050 and by the
  milestone brief: a second authoritative copy of a qualified gate.
- ADR-050 requires the extraction to be MECHANICAL: the same code, the same
  tests passing unchanged.
- Eigen stays private and never appears in a public header (rule 7).
- No qualified P17 behaviour, message or test may change.

## Options

### A — extraction to a module below 45

A new module that both physics call. One kernel, one set of gates. Changes a
qualified module, so P17's solve, reaction, validation and reference models are
requalified by the full unfiltered run.

### B — an independent private Eigen link in `thermal`

No change to P17. A second pivot gate and a second residual, which the two
modules could then let drift apart: a fix to a singularity case in one would not
reach the other. This is the duplication ADR-050 warned about, one level up.

### C — include `structural` from `thermal`

Forbidden by ADR-050 and by the layering check.

## Decision

**Option A.** A module `linalg` at layer **5**, holding exactly the generic
symmetric sparse solve:

```text
core 0   linalg 5   sketch 10   features 20   assembly 30   drawing 40
meshing 40   thermal 45   structural 50   io 60   renderer 70   scripting 70
```

Layer 5 is the free integer under `sketch`: the module needs only core's
`Result` and `Error`, and strictly-lower means it can include nothing else, which
is the property that makes it generic. It owns:

```text
solveSymmetricSparse     the three-condition solve, moved from structural
symmetricSolveProblem    the problem it would report
evaluateResidual         the acceptance step WITHOUT a solve, public so that a
                         candidate solution -- including a deliberately wrong
                         one -- can be accepted or refused on the same terms
ResidualNorms, SymmetricSolveFailure, SymmetricSolveSettings
```

`structural::solveSymmetricSparse` is now a thin wrapper: it validates P17's
canonical `SolverSettings`, calls the kernel, puts P17's units on the numbers and
rebuilds P17's three unit- or structure-bearing messages from the kernel's
structured failure. Every P17 test passes unchanged, and the messages are the
ones P17 qualified.

## Rationale

Option B is cheaper today and costlier for the life of the project: the pivot
gate is the most delicate qualified idea in the solver (the library reports
success on a singular matrix), and two copies of it are two chances to fix a bug
in one. Option A pays the requalification once, with a test suite that already
exists and exercises the kernel hard -- P17's solve suite alone is 21 cases,
including singular, under-constrained, rigid-body and mechanism systems, and the whole
structural suite (415 cases) passes unchanged against the extracted kernel.

The kernel returns STRUCTURED facts (the pivot ratio, whether every pivot was
positive, whether the zero pivot was exact, the residual norms) rather than text,
so each physics says them in its own words without the lower layer learning
either vocabulary.

What would have made Option B right: if the thermal system needed a different
algorithm (an iterative method, a non-symmetric factorisation). It does not; the
reduced thermal conduction matrix is symmetric and, when well posed, positive
definite, the same shape P17 solves.

## Consequences

**Easy.** A third physics (CFD, optimisation) calls the same kernel. A fix to the
singularity gate reaches every consumer.

**Hard.** `linalg` is now a qualified module whose behaviour structural and
thermal both depend on; changing it requalifies both.

**Committed.** To `linalg` including nothing above core, enforced by a fixture;
to `thermal` and `linalg` never linking `structural`, enforced by a CMake target
audit; to Eigen staying private.

## Verification

```text
architecture.layering                            521+ files, 0 violations
architecture.checker.linalg-to-thermal           linalg -> thermal REFUSED
architecture.checker.linalg-to-structural        linalg -> structural REFUSED
architecture.checker.thermal-to-structural       thermal -> structural REFUSED (ADR-050)
architecture.targets.thermal-link-graph          bettercad_thermal links no structural
architecture.targets.linalg-link-graph           bettercad_linalg links core and Eigen only
P17 structural solve / reaction / validation / reference models      unchanged, passing
evidence                                         docs/verification/P18-SOLVE-001/
```
