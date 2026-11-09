# ADR-076 — The CFD solve reuses the symmetric kernel, and the non-symmetric system gets a new generic kernel gated by a readable R diagonal

Status: accepted (P19-LINSOLVE-001, 2026-11-09)

## Context

`P19-PVCOUPLE-001` is qualified and consumes a caller-supplied `CfdLinearSolve`
port. This milestone supplies it. Its checklist asks for a **pressure-system
solver** and a **momentum-system solver** as two separate items, and three of its
fourteen items — "Audit existing generic solver", "Respect dependency-layer
ADRs", "Reuse generic lower-layer solver" — say the first move is to audit what
exists rather than to write something.

What exists is `bettercad::linalg` at layer 5, holding `solveSymmetricSparse`:
a direct `SimplicialLDLT` factorisation behind three gates — the pivot ratio
`min|D|/max|D|`, finiteness, and an independent residual computed from the
ORIGINAL arrays after the solve. `cfd` sits at layer 42, the rule is
strictly-lower, so `cfd` may already include it. `src/cfd/CMakeLists.txt` names
this plan outright and leaves this milestone a specific `static_assert`.

The difficulty is that **the two CFD systems have different algebra, and both
facts were already committed before this milestone began.**

```text
CfdCoupling.hpp    "THE PRESSURE-CORRECTION MATRIX IS SYMMETRIC BY
                    CONSTRUCTION ... That the momentum system is NOT symmetric
                    is equally deliberate: convection makes it asymmetric
                    (ADR-072 Decision 3), so a port must accept both and a port
                    that assumes symmetry everywhere is wrong."
CfdConvection.hpp  "THE MATRIX IS ASYMMETRIC, AND THAT IS CORRECT ... Demanding
                    symmetry of a convection operator would be wrong rather
                    than merely slack."
```

`ADR-052`, which created `linalg`, anticipated exactly this circumstance in its
own Rationale: *"What would have made Option B right: if the thermal system
needed a different algorithm (an iterative method, a non-symmetric
factorisation). It does not."* CFD's momentum system does.

## What the audit found

Two findings, both about the thing being audited rather than about CFD.

**The kernel does not check symmetry.** `src/linalg/SymmetricSolve.cpp` builds
the FULL CSR into Eigen by `setFromTriplets` and hands it to `SimplicialLDLT`,
whose default `UpLo` is `Lower`. The word "symmetric" occurs in that file only
in comments and type names, never in a check; there is no `NotSymmetric`
enumerator among the eight. The precondition is prose:
`"Solves A x = b for a symmetric A"`.

**No test has ever shown the kernel an asymmetric matrix.** Every matrix in
`tests/linalg/SymmetricSolveTests.cpp` and in `structural`'s dense helpers is
symmetric by construction. `thermal` prevents the case upstream with
`ThermalSparseMatrix::fromCsr`'s `NotSymmetric`; `cfd` prevents it upstream with
`CfdCouplingTests.cpp`'s `largestSymmetryError() == 0.0`, whose comment gives the
reason as "and `linalg::solveSymmetricSparse` requires symmetry". Both callers
guard it and point at the requirement; nothing has ever tested the requirement.
So the precondition of the kernel this milestone is told to reuse is
**unqualified**, and that is a gap in the audit's subject rather than a
precedent to follow.

## Measurements

All four rounds are in `docs/verification/P19-LINSOLVE-001/experiments/`, run
before any production code existed. Two rounds corrected the round before them,
and both defects were in the measurement rather than the production code.

**Q1 / A — a mis-routed asymmetric system is REFUSED, not silently solved.** On
a well-posed convection-diffusion chain with diffusion `1e-6`, `n = 64`:

```text
asymmetry   symmetric kernel     normalized
0           SOLVED               4.201379e-17
1e-12       residual_too_large   2.316292e-07
1e-9        residual_too_large   2.387288e-04
1e-7        singular_system      —
1e-6        singular_system      —
1e-3        singular_system      —
```

An asymmetry six orders below the diffusion coefficient is already refused. The
lower-triangle solve does not satisfy the original system, and the independent
residual — computed from the original arrays — is what catches it. At larger
asymmetry the lower triangle also loses positive-definiteness and the pivot gate
fires first. **The kernel is therefore safe to reuse, and the safety comes from
the residual gate rather than from a symmetry check.**

**Q5' — the pressure system is scale-free on the path the coupling actually
takes, and is not on the one it reports as an exception.**

```text
a_f      pin keeps its own value (THE RULE)   pin forced to 1.0 (the exception)
1e-3     SOLVED   pivotRatio 2.500000e-01     SOLVED   5.000000e-04
1e-9     SOLVED   pivotRatio 2.500000e-01     SOLVED   5.000000e-10
1e-12    SOLVED   pivotRatio 2.500000e-01     singular_system  5.000000e-13
1e-15    SOLVED   pivotRatio 2.500000e-01     singular_system  5.000000e-16
1e-20    SOLVED   pivotRatio 2.500000e-01     singular_system  5.000000e-21
```

Under the rule the ratio is `0.25` across seventeen orders of magnitude, so
`pivotFloor = 1e-12` is safe without any equilibration. Under the exception —
`pinnedDiagonalIsUnitValued`, a reference cell with no internal face and no
fixed-pressure face — the ratio is `0.5 * a_f` and a CORRECT system is refused as
singular below `a_f ~ 2e-12`. That is recorded as a limitation, not repaired
here: repairing it would edit a qualified milestone's assembly.

**Q2 / Q6 / Q7 — the reason the general kernel is QR and not LU.** A library
reporting success is not evidence, and for the non-symmetric case the measurement
shows precisely how little `info()` is worth:

```text
SparseLU, system singular to 1e-13:  info = Success, normalized residual = 0
SparseLU, exactly singular:          info = NumericalIssue, "STRUCTURALLY SINGULAR"
```

So the LU path needs a conditioning gate, and `logAbsDeterminant` cannot be one:
it reads `-5.9e+01` for a 3x3 whose entries are all `1e-9` and perfectly well
conditioned. The LDLT analogue is `min|U_ii|/max|U_ii|`, and the pinned Eigen's
`SparseLU` exposes **no accessor for U's diagonal** — `matrixL()` and `matrixU()`
return solve-only proxies. The diagonal can be recovered by solving `U` against a
dense identity, which is `O(n^2)` memory: **272 MB at `P18-SOLVE-001`'s smoke size
of 5832 unknowns.** Unusable.

`SparseQR::matrixR()` returns `const QRMatrixType&` — a real sparse matrix — so
its diagonal is read in `O(nnz)` with no dense intermediate, and the ratio
separates cleanly and scale-freely:

```text
case                  R-diagonal ratio   rank    LU U-diagonal ratio (dense route)
well conditioned      5.000000e-01       3 of 3  5.555556e-01
near-singular 1e-13   0.000000e+00       2 of 3  3.330669e-14
near-singular 1e-9    2.357023e-10       3 of 3  3.333334e-10
tiny but well cond.   5.000000e-01       3 of 3  5.555556e-01
exactly singular      0.000000e+00       2 of 3  (NumericalIssue)
```

And the cost, on rank-checked well-posed chains, with both solvers agreeing:

```text
n      LU s      QR s      QR/LU   LU normalized   QR normalized   largest rel gap
200    0.000118  0.000042  0.35    4.470226e-17    2.475578e-16    2.438e-13
1000   0.000422  0.000256  0.61    4.207224e-17    5.385403e-16    2.681e-12
4000   0.001697  0.002798  1.65    4.163406e-17    1.269122e-15    2.518e-11
```

QR is cheaper below roughly two thousand unknowns and 1.65x dearer at four
thousand, with the trend growing; its residual is 6x to 30x looser than LU's and
grows with `n`, while LU's is flat. Both stay far inside the `1e-9` gate.

## Options

### A — a second generic kernel in `linalg`, QR-based

`linalg::solveSparse` beside `solveSymmetricSparse`, sharing the residual
acceptance step verbatim. One module, one gate structure, reusable by a fourth
physics. Changes a module `structural` and `thermal` depend on, so both are
requalified by the full unfiltered run — `ADR-052`'s own stated consequence.

### B — a second generic kernel in `linalg`, LU-based

Faster at scale and more accurate, but its conditioning gate is unreachable in
sparse form. It would have to be either a dense `O(n^2)` inverse — impossible at
smoke size — or no conditioning gate at all, which the measurement shows accepts
a system singular to `1e-13` while reporting a perfect residual. That is the one
thing this project's solver philosophy forbids.

### C — a private Eigen link in `cfd` with a CFD-local non-symmetric solve

No change to `linalg`, so no requalification of structural or thermal. But it
duplicates the three-gate acceptance — the most delicate qualified idea in the
solver — inside a physics module, which is exactly the duplication `ADR-052`
chose extraction to avoid, one level further out. A fix to the residual gate
would reach two of three consumers.

### D — an iterative method (BiCGSTAB with ILU) in `linalg`

Natural for SIMPLE, where the momentum predictor need not be exact. But
`CfdLinearSolverSettings` declares ONE ratio, `relativeResidualTolerance = 1e-9`,
so the solve must reach 1e-9 regardless; an iterative method would then need a
preconditioner, an iteration cap and a convergence test, which is a second
stopping notion beside `ADR-058`'s. Iteration counts can differ between `-O0`
and `-O3`, which puts the determinism item at risk for no gain.

## Decision

**Option A.** Eight decisions, and two more added by the adversarial reviews (Decisions 9 and 10, below).

**1. The non-symmetric kernel lives in `linalg`, not in `cfd`.** `linalg`'s
charter is "the GENERIC numerical kernels more than one physics needs"; a general
sparse solve qualifies on its face, and `ADR-052`'s Consequences already say "A
third physics (CFD, optimisation) calls the same kernel." The cost — requalifying
structural and thermal — is paid by a run that happens anyway.

**2. It is `SparseQR`, chosen for a reachable conditioning gate rather than for
speed.** `min|R_ii|/max|R_ii|` is scale-free and readable in `O(nnz)`; LU's
analogue is not readable at all. That QR is 1.65x dearer at four thousand
unknowns and has a looser residual is accepted and recorded.

**3. Its gates mirror the symmetric kernel's, in the same order.**

```text
  1  info() == Success, and min|R_ii|/max|R_ii| >= rankRatioFloor
  2  every component of x is finite
  3  the independent residual r = A x - b passes the normalized gate
```

**4. `rank()` is REPORTED, never the gate.** Eigen's own documentation says the
strategy "might fail for some rank deficient problems" and that one should
"check the accuracy of the computed solution", and `rank()` is defined as
`m_nonzeropivots` under a threshold Eigen chooses. A number that depends on a
library's internal threshold is a fact worth carrying in a diagnostic and not a
condition to accept or refuse on.

**5. The residual acceptance step is SHARED, not copied.** `evaluateResidual`
takes only arrays and knows no algorithm; the general kernel calls the same code
the symmetric kernel does. This is the whole of Option C's argument against
itself, applied inside `linalg`.

**6. The symmetric kernel's precondition is QUALIFIED BY TEST, NOT CHANGED BY
CODE.** The audit found it unchecked and untested. Adding a symmetry check to
`solveSymmetricSparse` would change the behaviour of a qualified kernel — a
system with asymmetry `1e-12` solves today and would begin to be refused — and
that behaviour change would reach structural and thermal for no benefit to
either. So the kernel keeps its behaviour and acquires tests that pin it,
including the asymmetric cases measured above. The symmetry DECISION belongs to
the caller that has the information.

**7. `cfd` routes by MEASUREMENT, not by the caller's word.** `CfdSparseMatrix`
already exposes `largestSymmetryError()`. The pressure entry point requires
symmetry and refuses an asymmetric matrix in cfd's own vocabulary; the momentum
entry point accepts either. A caller cannot assert symmetry it does not have.

**8. `cfd` binds ONE ratio, and the conditioning floors come from `linalg`'s
defaults.** `ADR-058` says of `CfdLinearSolverSettings`: "ONE RATIO ... Which
solver is used is P19-LINSOLVE-001's decision and not a field." Adding a CFD
field for `pivotFloor` or `rankRatioFloor` would be the second parameter system
`ARCHITECTURE.md` rejects. Q5' is what makes the default safe for the pressure
path: the ratio is `0.25` independent of scale.

## Consequences

**Easy.** A fourth consumer gets a general sparse solve with the same three
gates. The pressure path reuses a kernel qualified across two physics and 415
structural cases. CFD's solve adds no numerical idea of its own: it routes,
translates vocabulary, and refuses.

**Hard.** `linalg` grows a second algorithm, so it carries two conditioning
notions whose names must not be confused — `pivotFloor` for the LDLT and
`rankRatioFloor` for the QR. Structural and thermal are requalified by every run
that touches `linalg`. QR's cost grows faster than LU's with system size, so a
later milestone that needs large momentum systems may want to revisit Decision 2
with a cheaper conditioning estimator rather than a dense inverse.

**Committed.** To `cfd` linking `BetterCAD::linalg` PRIVATE and never
`structural` or `thermal`; to `CfdSparseIndex` and `linalg::SparseIndex` being
the same type, now by `static_assert` rather than by construction; to the
symmetric kernel's behaviour being unchanged by this milestone.

## Known limitations

**L1** The pressure solve is refused as singular when the coupling reports
`pinnedDiagonalIsUnitValued` AND the face coefficients are below about `2e-12`.
Measured as `pivotRatio = 0.5 * a_f`. Narrow — it needs a reference cell with no
internal face and no fixed-pressure face — but real, and not repaired here
because the assembly belongs to a qualified milestone.

**L2** QR's normalized residual grows with system size (`2.5e-16` at 200
unknowns, `1.3e-15` at 4000) where LU's is flat at `4.2e-17`. Both are far inside
the `1e-9` gate at the sizes measured; the trend is recorded because it is the
one place where Decision 2's choice is worse rather than merely dearer.

**L3** QR is 1.65x the cost of LU at 4000 unknowns and the ratio is growing. No
extrapolation beyond the measured sizes is claimed.

**L4** `rank()` is threshold-dependent and reported rather than gated, so a
rank-deficient system whose deficiency Eigen's threshold misses is caught by the
R-diagonal ratio or the residual, not by the rank.

**L5** Nothing here iterates to convergence or publishes a result:
`P19-STEADY-001` owns both.

**L6** `SparseQR` requires its input in compressed mode. The kernel compresses
the matrix it builds, so no caller can observe this, but it is a precondition of
the library rather than a property of the algorithm.

## Amendments after the two adversarial reviews

Both reviews were READ-ONLY and both ran before the freeze. They produced
thirty-two findings between them. Three were substantive enough to change the
code, and **both reviews independently found the same one**, which is the
strongest signal either produced.

### Decision 10 (new) — both kernels validate the row offsets, and the omission was a memory-safety defect

`checkSystem` validated `rowStart`'s length, its first entry and its last entry,
and **nothing in between**. For a two-row system with two stored entries,
`rowStart = {0, 5, 2}` satisfies every clause: the size is `rows + 1`, the first
offset is `0`, and the last equals `inner.size()`. The build loop then runs
`slot = 0 .. 4` and reads `inner[2..4]` and `values[2..4]` past the end of their
spans. `std::span::operator[]` is unchecked, so a hardened build aborts and an
unhardened one reads adjacent memory, narrows the garbage to Eigen's index type
and hands it to `setFromTriplets`, which guards the result with `eigen_assert`
alone — Debug aborts, Release writes out of bounds.

**It was reproduced before the guard was written**, as an abort inside
`std::span::operator[]` on `values[slot]`.

Both kernels now refuse a decreasing offset, and a system whose size or
stored-entry count exceeds the range of Eigen's `int` storage index, both as
`DimensionMismatch`.

**THE FIRST FORM OF THIS GUARD ALSO BOUNDED EACH INTERIOR OFFSET, AND A
MUTATION PROBE SHOWED THAT CLAUSE COULD NOT FIRE.** The dimension check above
it already requires `rowStart.back() == inner.size()`, so if the offsets never
decrease then `rowStart[i] <= rowStart[rows] == inner.size()` for every `i` by
transitivity — an interior offset can exceed the stored entries only if the
offsets also decrease somewhere. It was removed from both kernels: a guard no
input can fire is what this project's own rule rejects, and that rule was being
cited elsewhere in this very milestone to justify omitting other checks.

The surviving guard is therefore simpler than the one the finding first
produced and provably sufficient rather than defensively redundant, and both
clauses the proof rests on — monotonicity and the back-equality it leans on —
are pinned by probes in both kernels.

**THE FIX TOUCHES `SymmetricSolve.cpp`, A KERNEL QUALIFIED UNDER P17 AND P18,
AND THE OWNER WAS ASKED BEFORE IT WAS KEPT.** One reviewer argued it belonged to
a separate milestone because `structural` and `thermal` are qualified against
that module. The question was put to the owner with the measurement below and
the alternative (revert, and file it as its own milestone) stated; the owner's
answer on 2026-11-09 was to keep it in this milestone. So the scope of
`P19-LINSOLVE-001` now includes this fix BY THE OWNER'S DECISION and not by the
author's reading of the checklist, which matters because `TODO.md` is the only
authority for scope. Three reasons were put with the question:

```text
the milestone's own name   "Audit and qualify reusable sparse linear algebra",
                           and item 1 is "Audit existing generic solver". An
                           audit that finds a memory-safety defect and leaves it
                           is not an audit
the cost is already paid   this milestone ADDS to linalg regardless, so ADR-052's
                           consequence -- "changing it requalifies both" -- is
                           already this milestone's accepted cost. The marginal
                           cost of the fix is zero
the drift guard            Linalg_BothKernelsClassifyAMalformedSystemAlike
                           asserts the two kernels classify the same malformed
                           system identically. Fixing one and not the other
                           would have made that test's claim false
```

And the fix cannot break a correct caller: it converts undefined behaviour into
a structured refusal, and every in-tree caller already validates monotonicity in
its own container first — `CfdSparseMatrix::fromCsr` reports `RowStartInvalid`,
and thermal's and structural's containers do the same. Measured after the
change, against the figures recorded before it:

```text
                before            after             
[structural]    271,414 in 415    271,414 in 415    unchanged
[thermal]       595,622 in 1168   595,622 in 1168   unchanged
```

### Decision 2, amended — the R diagonal is read by ITERATION and never by `coeff()`

The first implementation copied `matrixR()` into a column-major
`SparseMatrix<double>` and read `coeff(i, i)`. Eigen's own documentation for
that function says:

> The entries of the returned matrix are not sorted. This means that using it in
> algorithms expecting sorted entries will fail. This include random coefficient
> accesses (SpaseMatrix::coeff())

and prints the column-major copy as its counter-example. The implementation was
Eigen's counter-example verbatim.

**An honest qualifier: the wrong value could not be reproduced.** A
rank-deficient 4x4 whose dependent column is NOT last was constructed
specifically to provoke Eigen's pivot permutation — `rank` came out `3 of 4`, so
the deficiency was detected — and the column-major `coeff`, a row-major `coeff`
and an order-independent scan of the stored entries **all agreed**, reading
`0.0` at the dead position. Eigen moves a discarded column to the END, so the
position that loses its pivot is the last one either way. The review rated the
contract violation CERTAIN and the wrong number LIKELY; the measurement supports
the first and not the second.

It is changed anyway, because the previous form worked only by an undocumented
shortcut inside `CompressedStorage::atInRange` — the diagonal is inserted last
and that function tests the final entry of a column before it binary-searches —
and this project does not rest a gate on an optimisation a library does not
promise. The replacement is a single pass over the stored entries, which needs no
ordering at all, is `O(nnz)` either way, and binds a REFERENCE instead of copying
the whole factor, which the previous form did on every solve.

### Decision 9 (new) — the pressure path reports a non-positive-definite matrix as one

`cfdLinearSolve()` routes on exact symmetry, and the LDLT's precondition is
symmetric **and positive definite**. Exact symmetry is strictly weaker, so the
port handed the LDLT matrices it cannot factorise. Two measured
counter-examples, both exactly symmetric and both trivially solvable:

```text
matrix             rhs      answer    pressure path (before)   momentum path
[[-2]]             [4]      -2        system_is_singular       SOLVED, -2
[[1,2],[2,1]]      [3,3]    [1,1]     system_is_singular       SOLVED, [1,1]
```

Worse than a wrong classification: the message read *"the factorisation's
smallest pivot is 1 of its largest, below the floor of 1e-12"* for the first and
*"0.333... below the floor of 1e-12"* for the second. That wording is formatted
whenever the pivot gate fires for EITHER of its two reasons, so a refusal caused
by a non-positive pivot reports a ratio that plainly is not below the floor.

The kernel's wording belongs to a qualified milestone and is not changed. CFD
tells the difference instead: `SymmetricSolveFailure` carries
`allPivotsPositive` and `exactlyZeroPivot` as structured facts precisely so a
caller can say what happened in its own words, and the pressure path now reports
`MatrixIsNotPositiveDefinite` with a message that says the system may still be
solvable and that the general kernel accepts it.

**No fallback is added.** Both kernels' headers forbid "a fallback to a second
algorithm", so the pressure path does not quietly retry on the QR. A matrix that
reaches this refusal is either a defective assembly or a matrix that does not
belong on that path, and it is told so.

### Decision 4, restated — what `rank()` is and is not

`rank()` is still never the conditioning gate, for the reason originally given.
The restatement is narrower and was prompted by the review: the field it feeds,
previously named `structurallySingular`, was documented as "the library reported
the matrix structurally singular" and was in fact `reportedRank < rows` computed
inside the ratio-gate branch. No code path ever set it from a library verdict.
It is renamed `rankCameOutShort` and documented as what it measures — Eigen's
threshold-dependent rank falling short of the row count, alongside a small ratio
— and the header says so.

### Three claims in comments that were false, and are corrected

```text
"its pivotFloor is not read on    FALSE. evaluateResidual's first statement is
 this path"                       validate(settings), which requires pivotFloor
                                  to be finite and positive. What is true is
                                  narrower: no LDLT runs, so the floor is never
                                  COMPARED against a pivot
SingularSystem: "a structurally   FALSE. Every non-Success from the
 singular matrix (the library     factorisation becomes FactorizationFailure;
 said so)"                        nothing maps a library verdict to singularity
the back-substitution guard       NOT ONE OF THE THREE GATES. SparseQR's
 counted as a gate                _solve_impl sets m_info = Success
                                  unconditionally on its last line in the pinned
                                  Eigen, so the branch is unreachable today. It
                                  is kept as defence against a future version
                                  and is now labelled as such rather than
                                  claimed as a gate
```

## Known limitations added by the reviews

**L7** `min|R_ii|/max|R_ii|` is scale-free but NOT ordering-free. R factorises
`A·P` under COLAMD's fill-reducing permutation, so the ratio is a property of the
computed factorisation rather than of `A`. Renumbering cells — the same mesh and
the same physics, the matrix symmetrically permuted — changes the ordering,
changes R and changes the ratio, potentially across the floor. Run-to-run
determinism is unaffected, because COLAMD is deterministic for a fixed pattern,
and that is what the determinism tests measure; invariance under renumbering is
a different property and is neither claimed nor tested. Semantic-topology work
will renumber.

**L8** The `CfdLinearSolve` port returns `std::expected<CfdLinearSolveResult,
Error>`, so handing a refusal through it **discards the entire structured
failure** — the problem tag, the conditioning ratio, the residual, the symmetry
error, the index — and leaves only a message string. `CfdSparse.hpp` states the
rule this breaks: "THE TAG IS THE POINT. P19-BC-001's review found eight
refusals whose tags could be swapped with every message assertion still
passing." The signature belongs to the qualified `P19-PVCOUPLE-001`, so widening
it is not this milestone's to do; it extends that milestone's own `L4`, which
already records that the coupling cannot tell whether a port measured the
residual it reports. Callers that need the classification call the named entry
points, which carry it in full.

**L9** Eigen's default pivot threshold for `SparseQR` is `20*(m+n)*maxNorm*eps`,
which **grows linearly with the system size**. So at large `n` a legitimate pivot
can be declared dead, the factorisation reports a short rank, and a correct
system is refused. Refusing is the conservative direction — Eigen returns a
rank-truncated basic solution, which the residual gate would catch anyway — but
the threshold is the library's and not this kernel's, and no extrapolation beyond
the measured sizes is claimed.

**L10** `SparseSolveProblem::FactorizationFailure` and
`CfdLinearSolveProblem::FactorizationFailed` may be unreachable for a square
system. The only non-`Success` state `SparseQR::compute` can reach is
`InvalidInput` from an empty row, whose guard appears unreachable for a square
matrix by inspection of `SparseColEtree.h`. Both are also the DEFAULT value of
their failure struct, which is why neither is removed: an aggregate needs one,
and the alternative is a default that claims something more specific. Recorded
rather than claimed as covered, and the tension with this project's own rule —
"An enumerator no input can produce is worse than no enumerator" — is recorded
with it.

**L11** `CfdLinearSolveProblem::SolutionIsNotFinite` and `ResidualIsNotFinite`
are likewise unreached from any constructible input: they exist because the
kernels can produce them in principle and CFD translates faithfully, not because
a CFD caller has been shown producing one.

**L12** The non-finite R-diagonal branch is reachable only by overflow inside the
factorisation — entries near `1e200` square to infinity in Eigen's norm
accumulation — and it is classified `SingularSystem` for a matrix that is
badly scaled rather than singular. It now carries the rank it measured, so a
reader can see the factorisation completed, but the classification remains the
honest best available: the kernel cannot distinguish "singular" from "overflowed
while deciding".

## Verification

```text
docs/verification/P19-LINSOLVE-001/
    experiments/   four measurement rounds, two of which corrected the previous
    README.md      the evidence
```
