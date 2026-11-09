# P19-LINSOLVE-001 — CFD Linear Solver Infrastructure

RESULT: PASS — three presets built from clean and two repeated five times, 17 stages all exit 0, the qualified tree unmoved; four defects in this milestone's own evidence found afterwards and corrected
COMMIT: e63b19db2d4f48c3e73812eeb66784d71a7ce805

**The eight frozen identities were read back OUT OF THAT COMMIT OBJECT**, with
`git rev-parse e63b19d:<path>`, which is a check that cannot be run before the
commit exists and is the reason it is recorded in a second commit rather than
predicted in the first:

```text
apps               fc398a3d2e524178cf38e8b667236379d8381086   MATCH
include            d52820c56a1ae388f2a4f12b126b45c103308bca   MATCH
src                56358d8c8c6c97f55ded83b74e5d36e0759281ed   MATCH
tests              ba7cf940350cf23c61563eb51aef65f379f75b98   MATCH
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958   MATCH
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7   MATCH
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd   MATCH
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e   MATCH
```

So the tree that was qualified is the tree that is committed, by four readings:
the harness before its first build, the harness after its last test, a
recomputation from the working tree after the gate, and this one from the commit
itself. 66 files: 8 new and 6 modified across the frozen surface, `TODO.md` and
`ROADMAP.md`, `ADR-076`, and the 49 files of this evidence directory.

NOT PUSHED. The owner pushes.

## TASK

`P19-LINSOLVE-001` — CFD Linear Solver Infrastructure. Authorized 2026-11-09 by
the owner's "implement and fully verify" over the fourteen-item checklist, which
arrived pasted into the instruction as the eight before it did.

**THE PASTED TEXT WAS DIFFED AGAINST `TODO.md`'S OWN SECTION BEFORE ANY WORK.**
Byte-for-byte identical across all 23 lines, 14 unticked items and 0 ticked —
counted rather than assumed — sha256 `86a54ddba6c61ce2` over both after CR
normalization. The authorization and its six scope notes were written into
`TODO.md` before any implementation, and the diff is
[experiments/diff-checklist.py](experiments/diff-checklist.py).

## SCOPE

Three of the fourteen items say the first move is to audit rather than to build:
"Audit existing generic solver", "Respect dependency-layer ADRs", "Reuse generic
lower-layer solver". They were right to.

### What the repository already decided, before this milestone began

```text
tests/architecture/CheckLayering.cmake   layer_linalg 5, layer_cfd 42, and the
                                         rule is strictly-lower -- so cfd was
                                         ALREADY permitted to include linalg
                                         and no renumbering was needed
src/cfd/CMakeLists.txt                   "the SOLVE -- still P19-LINSOLVE-001's
                                         decision, unchanged. The generic kernel
                                         it would reuse is BetterCAD::linalg
                                         (layer 5, ADR-052), linked PRIVATE,
                                         never an include of structural or
                                         thermal and never a hand copy"
src/cfd/CMakeLists.txt                   left this milestone a static_assert BY
                                         NAME: "the static_assert that turns
                                         'same by construction' into 'same,
                                         enforced' belongs to the milestone that
                                         may legitimately include that header"
CfdData.hpp (ADR-058)                    CfdLinearSolverSettings already
                                         declares the ONE ratio, 1e-9, and says
                                         "Which solver is used is
                                         P19-LINSOLVE-001's decision and not a
                                         field"
CfdCoupling.hpp (ADR-075)                the pressure matrix is symmetric by
                                         construction; "That the momentum system
                                         is NOT symmetric is equally deliberate
                                         ... a port that assumes symmetry
                                         everywhere is wrong"
CfdConvection.hpp (ADR-072 Decision 3)   "THE MATRIX IS ASYMMETRIC, AND THAT IS
                                         CORRECT"
```

So the milestone's shape was not a choice: link `linalg` PRIVATE, reuse the LDLT
for pressure, and supply something else for momentum, because an LDLT cannot
take an asymmetric matrix. `ADR-052`'s own Rationale had already named the
condition — *"What would have made Option B right: if the thermal system needed a
different algorithm (an iterative method, a non-symmetric factorisation). It does
not"* — and CFD's momentum system does.

### What the audit found in the thing it was told to reuse

**The symmetric kernel does not check symmetry.** `src/linalg/SymmetricSolve.cpp`
builds the FULL CSR into Eigen and hands it to a `SimplicialLDLT` whose default
`UpLo` is `Lower`. The word "symmetric" occurs in that file only in comments and
type names, never in a check; there is no `NotSymmetric` among its eight
enumerators. The precondition is prose.

**And no test had ever shown it an asymmetric matrix.** Every matrix in
`tests/linalg/SymmetricSolveTests.cpp` and in `structural`'s dense helpers is
symmetric by construction. `thermal` prevents the case upstream with
`ThermalSparseMatrix::fromCsr`'s `NotSymmetric`; `cfd` prevents it upstream with
`CfdCouplingTests.cpp`'s `largestSymmetryError() == 0.0`, whose comment gives the
reason as "and `linalg::solveSymmetricSparse` requires symmetry". Both callers
guard it and point at the requirement. **Nothing qualified the requirement.** That
is a gap in the audit's subject rather than a precedent to follow, and closing it
is item 1's real deliverable.

### What is NOT this milestone's

`P19-STEADY-001` owns the outer loop and the atomic result publication, so
nothing here iterates to convergence or publishes a result. `P19-PVCOUPLE-001`
owns the coupling and is qualified: its `CfdLinearSolve` port is the shape this
milestone must satisfy, and changing that port would requalify a passed
milestone — so it was not changed.

## BASELINE

```text
HEAD at the start      30c04cc  (P19-PVCOUPLE-001 recorded, pushed)
full suite             6802 tests in three presets
cfd subsystem          728,802 assertions in 623 cases
linalg subsystem       171 assertions in 10 cases
structural             271,414 assertions in 415 cases
thermal                595,622 assertions in 1168 cases
```

The last three matter because this milestone changes `linalg`, which
`structural` and `thermal` both link. `ADR-052`'s Consequences say so outright:
"`linalg` is now a qualified module whose behaviour structural and thermal both
depend on; changing it requalifies both."

## ARCHITECTURE

[ADR-076](../../architecture/decisions/ADR-076-the-cfd-solve-reuses-the-symmetric-kernel-and-a-new-general-kernel-gated-by-a-readable-r-diagonal.md)
holds the decision, four candidates and the measurements. The short form:

```text
pressure correction   symmetric by construction  ->  linalg::solveSymmetricSparse
momentum              asymmetric and correct     ->  linalg::solveSparse (NEW)
```

### The measurements, made before any production code existed

Four rounds, in [experiments/](experiments/). **Two of the four corrected the
round before them, and both defects were in the measurement rather than in the
production code.**

**A mis-routed asymmetric system is REFUSED, not silently solved.** On a
well-posed convection-diffusion chain, diffusion `1e-6`, 64 cells:

```text
asymmetry   symmetric kernel     normalized
0           SOLVED               4.201379e-17
1e-12       residual_too_large   2.316292e-07
1e-9        residual_too_large   2.387288e-04
1e-7        singular_system      —
1e-6        singular_system      —
1e-3        singular_system      —
```

The safety comes from the residual gate, computed from the ORIGINAL arrays, and
not from a symmetry check. An asymmetry six orders below the diffusion
coefficient is already refused.

**Why QR and not the faster LU.** A library reporting success is not evidence,
and the measurement shows how little `info()` is worth here:

```text
SparseLU, singular to 1e-13:  info = Success, normalized residual = 0
SparseLU, exactly singular:   info = NumericalIssue, "STRUCTURALLY SINGULAR"
```

So the LU path needs a conditioning gate, and `logAbsDeterminant` cannot be one
— it reads `-5.9e+01` for a 3x3 whose entries are all `1e-9` and which is
perfectly well conditioned. The LDLT analogue is `min|U_ii|/max|U_ii|`, and the
pinned Eigen exposes **no accessor for U's diagonal**: `matrixL()` and
`matrixU()` return solve-only proxies, verified against
`Eigen/src/SparseLU/SparseLU.h`. The diagonal can be recovered by solving `U`
against a dense identity, which is `O(n^2)` memory — **272 MB at the 5832
unknowns of `P18-SOLVE-001`'s smoke test.** `SparseQR::matrixR()` returns
`const QRMatrixType&`, a real sparse matrix, so its diagonal is read in
`O(nnz)`:

```text
case                  R-diagonal ratio   rank     LU U-diagonal (dense route)
well conditioned      5.000000e-01       3 of 3   5.555556e-01
near-singular 1e-13   0.000000e+00       2 of 3   3.330669e-14
near-singular 1e-9    2.357023e-10       3 of 3   3.333334e-10
tiny but well cond.   5.000000e-01       3 of 3   5.555556e-01
exactly singular      0.000000e+00       2 of 3   (NumericalIssue)
```

And the cost, on rank-checked well-posed systems where both solvers agree:

```text
n      LU s      QR s      QR/LU   LU normalized   QR normalized   largest rel gap
200    0.000118  0.000042  0.35    4.470226e-17    2.475578e-16    2.438e-13
1000   0.000422  0.000256  0.61    4.207224e-17    5.385403e-16    2.681e-12
4000   0.001697  0.002798  1.65    4.163406e-17    1.269122e-15    2.518e-11
```

QR is dearer from about two thousand unknowns and its residual is looser and
grows with `n`. Both facts are recorded as `ADR-076` `L2` and `L3` rather than
left out of the comparison that chose it.

**The pressure system is scale-free on the path the coupling takes.** Under the
rule the assembly follows — the pin keeps its own diagonal — the LDLT's pivot
ratio is `0.25` across seventeen orders of magnitude of face coefficient, which
is what lets CFD pass ONE ratio across the boundary and leave the conditioning
floor to the kernel's default. Under the exception the coupling REPORTS,
`pinnedDiagonalIsUnitValued`, the ratio is `0.5 * a_f` and a correct system is
refused below `a_f ~ 2e-12`; recorded as `L1` and not repaired here, because the
assembly belongs to a qualified milestone.

### Two measurement defects, published rather than quietly fixed

**Round two modelled the exception as the rule.** It pinned the reference cell
with a diagonal of `1.0` and called that what the qualified coupling writes.
`CfdCoupling.hpp` says the opposite: the rule is "the diagonal keeps its own
value", and the unit value is a fallback for a reference cell with no internal
face and no fixed-pressure face. The corrected round separates both paths, and
the narrow hazard survives as `L1` — so the round was not wasted, only
mislabelled.

**Round three's momentum fixture was singular, and its own output said so.** It
printed the same largest LU-vs-QR gap, `1.574122e+12`, at `n = 200`, `1000` and
`4000`. One number for three different systems cannot be a property of the
systems. The chain was anchored at one end only; a singular system has a family
of solutions, the two solvers picked different members, and both reported a tiny
residual — which is why the gap was enormous while every residual looked
perfect. Round four asserts `rank == n` BEFORE believing any other number.

## BLAST RADIUS

```text
changed            src/linalg/   +1 header +1 source +1 CMake line
                   src/cfd/      +1 header +1 source +1 source line
                                 +1 PRIVATE link, 1 comment amended
direct dependents  structural and thermal both link BetterCAD::linalg
indirect           everything that links either: the reference models, the CLI,
                   the desktop app
CHANGED, by the     src/linalg/SymmetricSolve.cpp gains the SAME row-offset
owner's decision    guard as the new kernel. The audit found a reachable
                   out-of-bounds read there, the question of whether repairing
                   it belonged to this milestone went to the owner, and the
                   answer on 2026-11-09 was to keep it here (ADR-076
                   Decision 10, and r18). THIS LINE PREVIOUSLY SAID THE FILE
                   WAS NOT CHANGED, which was true when it was written and
                   false by the time the tree was frozen
NOT changed        the symmetric kernel's BEHAVIOUR on any well-formed input.
                   ADR-076 Decision 6 still holds: the asymmetry it tolerates
                   and the pivot gate it applies are untouched, and the only
                   difference is that a malformed CSR is now refused instead of
                   read past. Measured: [structural] 271,414 in 415 and
                   [thermal] 595,622 in 1168, both identical to the baseline
persistence        nothing. No serialized format, no stable reference, no
                   document state. A solver has no persistent representation
the PVCOUPLE port  CfdLinearSolve is consumed, never changed
```

The regression set therefore is not "the cfd tests": it is the full unfiltered
suite in three presets, because a change to `linalg` reaches two qualified
physics.

## IMPLEMENTATION

```text
include/bettercad/linalg/SparseSolve.hpp   the general kernel's API
src/linalg/SparseSolve.cpp                 SparseQR behind three gates
include/bettercad/cfd/CfdLinearSolve.hpp   CFD's two entry points and the port
src/cfd/CfdLinearSolve.cpp                 routing, precondition, translation
```

**The general kernel's three gates mirror the symmetric kernel's, in the same
order**, with `min|R_ii|/max|R_ii|` standing where `min|D|/max|D|` stood.
`rank()` is REPORTED and never gated: Eigen's own documentation says the strategy
"might fail for some rank deficient problems" and that one should "check the
accuracy of the computed solution", and `rank()` is `m_nonzeropivots` under a
threshold Eigen chooses. A number that depends on a library's internal threshold
belongs in a diagnostic, not in a condition.

**There is ONE residual gate in BetterCAD and both kernels are behind it.**
`evaluateResidual` takes only arrays and knows no algorithm, so `solveSparse`
calls it and translates its classification rather than computing a second
residual. That is the whole of the argument `ADR-052` used against duplicating
the gate, applied inside `linalg`.

**The system check is written twice, and a test is what makes that safe.** The
two kernels are separate translation units and neither may reach into the
other's anonymous namespace; extracting the forty-line check would edit a file
qualified across two physics.
`Linalg_BothKernelsClassifyAMalformedSystemAlike` feeds the same malformed
systems to both and requires the same problem, the same index and the same row
count, so a divergence is a failure rather than a drift.

**CFD supplies no numerical idea of its own.** It routes, it refuses, it
translates. The pressure entry point requires EXACT symmetry and refuses
anything else in CFD's own words — the kernel would catch it, but it would say
"residual_too_large", which names a numerical difficulty rather than the
structural mistake that was made.

**The port routes on exact symmetry**, because `P19-PVCOUPLE-001` takes ONE port
and uses it for both the momentum components and the pressure correction, so the
port must decide from the matrix alone. The consequence is stated rather than
hidden: the kernel is a function of the matrix, so a momentum system at exactly
zero mass flux is exactly symmetric and takes the LDLT — the right kernel for it,
since at zero flux the operator IS symmetric positive definite — and nothing
anywhere may assert bitwise equality across the two kernels.

**The `static_assert` this milestone was owed** sits in `src/cfd/CfdLinearSolve.cpp`
rather than a header, because `BetterCAD::linalg` is linked PRIVATE and a
consumer of `BetterCAD::cfd` does not inherit the link. The CMake comment that
said "BetterCAD::linalg is NOT linked here" is amended in place rather than
deleted, as `P19-DIFF-001` amended its predecessor.

**No finiteness loop over the matrix.** `CfdSparseMatrix::fromCsr` is the only
producer of the type and already refuses `NonFiniteEntry`, so such a loop could
not fire for any input a caller can construct. `CfdBoundary.hpp` states the rule
this follows — "An enumerator no input can produce is worse than no enumerator" —
and a guard no input can fire is that rule's other face. The right-hand side
arrives as a raw span with no gate of its own, and that is what is checked.

## TESTS

```text
tests/linalg/SparseSolveTests.cpp         19 cases    2,389 assertions
tests/cfd/CfdLinearSolveTests.cpp         20 cases    2,563 assertions
tests/cfd/CfdCouplingTests.cpp            +1 case    20,406 assertions
tests/compile_fail/LinalgSparseMisuse.cpp    14 gates
tests/compile_fail/CfdLinearSolveMisuse.cpp  14 gates

full suite   6870 = 6802 + 19 + 20 + 1 + 28          EXACTLY, no fudge term
repeat set   3474 = 3434 + 19 + 20 + 1               EXACTLY, compile_fail excluded
cfd        751,771 in 644 = 728,802 + 22,969 and 623 + 21   EXACTLY
linalg       2,560 in  29 =     171 +  2,389 and  10 + 19   EXACTLY
```

**THIS BLOCK HELD THE PRE-REVIEW SNAPSHOT UNTIL THE POST-GATE RECONCILIATION
READ IT AGAINST THE GATE'S OWN LOGS.** It said 12 and 11 cases, a 6854-test
suite and a 3458-test repeat set: the tree as it stood before the two
adversarial reviews closed seventeen test gaps. Those figures were correct when
they were written and were never revisited, so the one section whose job is to
say what was tested described a tree the gate did not run — and it carried
`EXACTLY` three times while doing so. The figures above are read from the three
qualified binaries and from the gate's own `ctest` logs, and `FREEZE.md` holds
the derivation of each.

The per-file split is a measurement too, not a division: `[linsolve]` reads
22,969 in 21 cases, the appended coupling case alone reads 20,406 in 1, and the
remainder is the new CFD suite's 2,563 in 20.

### The independent reference

**The strongest check in this milestone is not a residual.**
`CfdCouplingTests.cpp` already contained `denseSolve`, a dense Gaussian
elimination written so that the coupling could be qualified before any solver
existed. It shares no code with `BetterCAD::linalg`, with Eigen, or with either
factorisation. So the added case runs the SIMPLE loop twice over the same
benchmark — once through `cfdLinearSolve()` and once through that reference — and
compares:

```text
velocity   2.442e-16 relative
pressure   3.499e-16 relative
iterations 118 vs 118
```

Two unrelated implementations driving the same operator to the same field at
machine precision, in identical iteration counts. The fixture is the benchmark's
own, whose inlet speed is derived from the cell Péclet number rather than picked,
because upwind convection drives the momentum diagonal negative above `Pe ~ 1`
and the operator refuses that.

### What the audit gap cost, and what closed it

`Linalg_TheSymmetricKernelRefusesAnAsymmetricSystem` pins the measured
boundaries of the precondition nothing had ever tested, in all three directions:
an exactly symmetric system is solved, every asymmetry from `1e-12` up is
refused by one gate or the other, and the general kernel solves every one of
those same systems.

### A test defect of the author's own, caught by a failing assertion

`Linalg_SparseSingularSystemCarriesStructuredFacts` first claimed the
conditioning floor is a setting by LOWERING it, copying the symmetric suite's
shape — where `{{1, 0}, {0, 1e-14}}` has a pivot ratio of exactly `1e-14` and a
lower floor admits it. It failed, and `ADR-076`'s own Q7 had already said why:
QR's column pivoting treats a numerically dependent column as zero and moves it
to the end, so the ratio is EXACTLY `0.0` and no positive floor admits it. The
measured behaviour:

```text
matrix                floor 1e-12        floor 1e-9         floor 1e-16
near-singular 1e-13   singular, 0.0      singular, 0.0      singular, 0.0
near-singular 1e-9    SOLVED, 2.357e-10  singular, same     SOLVED, same
near-singular 1e-6    SOLVED, 2.357e-07  SOLVED, same       SOLVED, same
```

So the demonstration had to run the other way: a system the DEFAULT floor admits
is refused once the floor is RAISED above its measured ratio, with the same ratio
reported in both and full rank found in both. The contrast with the LDLT is now
asserted in one place rather than inferred from two suites.

### Determinism, with a structural basis and not only a repeat count

Following `P17-SOLVE-001`'s and `P18-SOLVE-001`'s method: repeated solves
compared with exact `==` and no tolerance, "because there is nothing to
tolerance: the same operations in the same order on the same data", a difference
counter checked against zero, and an FNV-1a bit fingerprint the test asserts only
to be reproducible in one process.

**AND THE CROSS-PRESET HALF IS A POST-RUN MEASUREMENT, NOT A CLAIM ABOUT A
LOG.** An earlier form of this section said the identity was "established by
diffing the three presets' logs". It could not be: `ctest --preset` captures no
stdout on a PASSING run, so a `WARN` never reaches `ctest-<preset>.log`, and
`grep -c "bit fingerprint"` over the committed logs of the two previous
milestones returns 0 and 0. The fingerprints are read from the three qualified
binaries after the gate and recorded under REGRESSION, the same way the
assertion counts are.

The audit behind the claim, by `P17`'s own method:

```text
Eigen/src/SparseQR/          pragma omp 0   std::thread 0   std::execution 0
Eigen/src/OrderingMethods/   pragma omp 0   std::thread 0   std::execution 0
Eigen/src/SparseCore/        pragma omp 2   std::thread 0   std::execution 0
OpenMP in this build         not enabled: 0 hits for fopenmp in the cache or cmake
unordered CONTAINERS         none. Every "unordered" hit in OrderingMethods and
                             SparseCore is the ENGLISH WORD in a comment
                             ("p has no unordered children left", "place
                             unordered nodes in lists", "unordered insertion")
the two new sources          0 occurrences of "unordered"
```

The two `pragma omp` are in `SparseDenseProduct.h`, a sparse-times-dense product
rather than a factorisation, and are inert because OpenMP is not enabled.

### The compile-fail gates, and what they are for

`ADR-076`'s Consequences name one hazard above the others: `linalg` now carries
TWO conditioning notions of the same shape that are not the same quantity. The
gates make the type system refuse the confusion — `pivotFloor` on a
`SparseSolveSettings`, `rankRatioFloor` on a `SymmetricSolveSettings`, either
settings type substituted for the other, either result type, either problem
enumeration.

One gate is architectural rather than typed:
`THE_CFD_HEADER_DOES_NOT_EXPOSE_LINALG` names a linalg type after including only
`CfdLinearSolve.hpp`. Because `linalg` is PRIVATE, that must not compile; if a
linalg type ever leaks into a cfd public header it starts to compile and the gate
fails. The layering check enforces containment of the include; this enforces
containment of the vocabulary.

**And these gates are attributable, which the inherited convention was not.**
Every case in the existing groups is a bare `void f()` at namespace scope, which
trips `-Werror=missing-declarations` as well as its intended error — so the
compile exits 1 partly for a reason unrelated to the defect, and only the regex
discriminates. The new cases are `[[maybe_unused]] static`, so the ONLY
diagnostic is the intended one, verified by reading a gate's output:

```text
error: conversion from 'bettercad::linalg::SparseSolveSettings' to non-scalar
       type 'const bettercad::linalg::SymmetricSolveSettings' requested
```

Six of the twenty-eight gates failed on the first run because the regex omitted
that very wording — `to non-scalar type` — which is the `P19-GRAD-001` lesson the
block's own comment cites, reproduced by the author who wrote the comment.

## ADVERSARIAL REVIEW

Two independent reviews, both **READ-ONLY by construction**, both before the
freeze. The constraint is structural rather than procedural, and it is there
because `P19-CONT-001`'s reviewer left a `CfdContinuity.cpp.adv-review-backup`
inside the frozen surface where `git add -A src` would have committed it. One
review attacked production correctness, the other asked only what could be wrong
and still pass the tests. Between them, thirty-two findings.

**THE STRONGEST SIGNAL EITHER PRODUCED IS THAT THEY FOUND THE SAME DEFECT.** The
two had different briefs, read different things and were given no access to each
other, and both landed on the unvalidated row offsets.

### The audit's most serious finding: a reachable out-of-bounds read in BOTH kernels

`checkSystem` validated `rowStart`'s length, its first entry and its last entry,
and nothing in between. For a two-row system with two stored entries,
`rowStart = {0, 5, 2}` satisfies every clause it tested, and the build loop then
read `inner[2..4]` and `values[2..4]` past the end of their spans.

**Reproduced before anything was changed**, as an abort inside
`std::span::operator[]` on `values[slot]`. Unhardened, it reads adjacent memory,
narrows the garbage to Eigen's index type and hands it to `setFromTriplets`,
which guards the result with `eigen_assert` alone — so Debug aborts and Release
writes out of bounds. That is the "could Debug and Release differ" item on this
project's own review checklist, as a memory-safety defect rather than a
diagnostic one.

The fix is in both kernels, and `ADR-076` Decision 10 records why that scope
judgement was made rather than deferred — the milestone's own name and item 1,
the fact that `linalg` is being requalified by this run regardless, and the
drift-guard test whose claim would otherwise have become false. Verified
behaviour-preserving against the figures recorded before the change:

```text
                before            after
[structural]    271,414 in 415    271,414 in 415    unchanged
[thermal]       595,622 in 1168   595,622 in 1168   unchanged
```

### A contract violation in this milestone's own code, and the honest limit of the evidence

The conditioning gate read `matrixR()`'s diagonal with `coeff(i, i)` on a
column-major copy. Eigen's documentation for that very function says its entries
"are not sorted … This include random coefficient accesses
(SpaseMatrix::coeff())", and prints the column-major copy as the counter-example.
The implementation was that counter-example verbatim.

**The wrong value could not be reproduced, and that is recorded rather than
smoothed over.** A rank-deficient 4x4 whose dependent column is NOT last was
built specifically to provoke Eigen's pivot permutation; `rank` came out `3 of
4`, so the deficiency was detected, and the column-major `coeff`, a row-major
`coeff` and an order-independent scan all agreed. Eigen moves a discarded column
to the END, so the position that loses its pivot is the last one either way.

It was changed anyway. The previous form worked only because of an undocumented
shortcut inside `CompressedStorage::atInRange`, and a gate resting on an
optimisation a library does not promise is not a gate this project will claim.
The replacement needs no ordering, costs the same, and binds a reference instead
of copying the whole factor on every solve.

### A routing condition weaker than the callee's precondition

The port routes on exact symmetry; the LDLT requires symmetric **and positive
definite**. Measured:

```text
matrix          rhs     answer   pressure path (before)   momentum path
[[-2]]          [4]     -2       system_is_singular       SOLVED, -2
[[1,2],[2,1]]   [3,3]   [1,1]    system_is_singular       SOLVED, [1,1]
```

And the diagnostic was not merely wrong but self-contradictory: *"the
factorisation's smallest pivot is 1 of its largest, below the floor of 1e-12"*.
That wording is formatted whenever the pivot gate fires for either of its two
reasons. CFD now reports `MatrixIsNotPositiveDefinite` from the
`allPivotsPositive` fact the kernel already carries, and adds no fallback,
because both kernels forbid one.

### Three comments that claimed more than the code did

`"its pivotFloor is not read on this path"` was false — `evaluateResidual`
validates its settings first, and `validate` reads the floor.
`SingularSystem`'s doc claimed a library verdict no code path produces. And the
back-substitution guard was counted among three gates when `SparseQR`'s
`_solve_impl` sets `Success` unconditionally, so it cannot fire. All three are
corrected in place, and the guard is now labelled unreachable-and-kept rather
than claimed.

### A false claim in THIS evidence, found by fact-check rather than by argument

The determinism section said the cross-preset fingerprint identity is
"established by diffing the three presets' logs". It is not, and the committed
logs prove it:

```text
grep -c "bit fingerprint" docs/verification/P19-PVCOUPLE-001/qualification/ctest-debug-ext.log   0
grep -c "bit fingerprint" docs/verification/P19-CONT-001/qualification/ctest-debug-ext.log       0
```

`ctest --preset` captures no stdout on a PASSING run, so a `WARN` never reaches
the log. The claim had no log behind it, which is the shape of failure
`CLAUDE.md` names outright. The fingerprints are therefore read from the three
qualified binaries after the gate and recorded under REGRESSION, exactly as the
assertion counts already are — a post-run measurement rather than a claim about
a file.

### What the test-adequacy review bought: twenty predicted mutations

The second review was asked for concrete one-line edits it believed no test
would catch, and produced twenty, ranked. Seventeen are now closed by tests. The
three left are documented-unreachable branches (`FactorizationFailure` on both
paths, the non-finite R diagonal) and are recorded in `ADR-076` as `L10`-`L12`
rather than claimed as covered.

The sharpest of the seventeen, because it invalidated a result this evidence had
already boasted about:

**The integration test's bound WAS the convergence tolerance.**
`CfdConvergenceSettings` stops at `1e-6`, and the test asserted the two runs
agreed to `1e-6` — so both halted inside the same neighbourhood of the fixed
point and agreement was close to automatic. It discriminated gross solver error,
not the accuracy the gates promise. A single iteration from one shared state
takes the loop out of the comparison:

```text
whole loop, to convergence   velocity 2.442e-16   pressure 3.499e-16
ONE iteration, same state    velocity 1.383e-16   pressure 2.190e-16
```

The second pair is the real measurement, and the bound asserted on it is
`1e-10` rather than `1e-6`.

Others worth naming, each a defect that would have survived:

```text
a 1x1 system          `if (rows == 0)` -> `if (rows <= 1)` returned an EMPTY
                      solution and reported success. Nothing anywhere was
                      smaller than 2x2
non-square on the     the momentum path would have solved the leading square
momentum path         block and discarded a column in silence -- and linalg
                      cannot detect it, because every clause it tests passes
a singular momentum   no test ever provoked one through CFD, so translate()'s
system                SingularSystem arm was unreached: the commonest real
                      failure of a momentum solve could have been reported as a
                      library fault
the conditioning      asserted only as `> 1e-3`, so a hard-coded 1.0 passed --
ratio                 and a constant is perfectly scale-free, so the scale
                      sweep could not tell. Now pinned to the measured 0.25
                      (pressure) and 0.5 (general)
the recomputed        an ABSOLUTE 1e-18 bound on a residual of magnitude 4e-17
residual              is a 2.4% relative tolerance. A 0.1% corruption passed.
                      Now exact equality, with a positive premise
the port's exactness  the header argues at length for an exact test; the port
                      had only been shown asymmetries of 0 and 1e-6, so
                      `== 0.0` could become `< 1e-15`. A one-bit asymmetry now
                      goes through the port and must take the QR
ADR-076 L1            the unit-pin regime was prose. `pressureShape` gained a
                      unit-pin mode, and the ratio is pinned at 0.5*a_f across
                      three decades with the refusal below the floor asserted,
                      which also makes the pressure floor's VALUE load-bearing
```

### One finding argued and declined

The test review judged the `linalg` memory-safety fix out of scope and asked for
it to be reported rather than fixed. The reasoning against is in `ADR-076`
Decision 10 and above; the short form is that leaving a reproduced
out-of-bounds read inside a milestone whose title is "Audit and qualify reusable
sparse linear algebra" would be the clearest possible case of confusing
implementation with completion, and that the requalification it costs is a cost
this milestone was already paying.

## REGRESSION

```text
full suite, each of three presets   6870 tests, 6870 Passed, 0 failures
repeat, release-ext and debug-ext   3474 selected, 17,370 executions, 0 failures
stages                              17, every one exit 0
compiler diagnostics                0 `error:`, 0 `warning:` under -Werror
```

Every figure was reconciled against the logs rather than read off a summary
line, and the reconciliation is the point: a passing suite cannot produce these
numbers by accident, and a suite that silently stopped selecting tests would
still say `100% tests passed`.

```text
per single-run log    Start markers 6870 = result lines 6870 = progress-prefixed
                      6870 = Passed 6870, failure markers 0
per repeat log        Start 17,370 = result lines 17,370 = Passed 17,370,
                      progress-prefixed 3,474, ratio EXACTLY 5.000
this milestone's own  14 compile_fail.linsolve + 14 compile_fail.cfdlinsolve,
tests, in each preset 19 new linalg, 20 new CfdLinearSolve, 1 appended coupling
                      case -- all Passed, all three presets identical
compile-fail gates    28 guards in the two sources, 28 cases registered and run
                      by the gate; set difference EMPTY BOTH WAYS, taken from the
                      gate's own log rather than by parsing CMake
architecture          205 of 205, including `architecture.layering`
```

**The progress counter is not the execution count.** Counting it as one would
have reported 3,474 where 17,370 ran. A first pass at this reconciliation also
counted 999 progress lines in a 6870-test log, which is `1..999`: the regex
required leading whitespace, and `ctest` right-pads the counter to the width of
the total, so a four-digit counter has none. 999 is not a measurement of
anything, and the tell was that it was suspiciously round.

### The assertion identity, read from the three qualified binaries

```text
[linalg]           2,560 assertions in    29 cases
[cfd]            751,771 assertions in   644 cases
[structural]     271,414 assertions in   415 cases
[thermal]        595,622 assertions in 1,168 cases
```

Identical in all three presets: debug-ext, release-ext, debug-shared-ext. `ctest --preset` captures no stdout from a
passing test, so these cannot be read from the gate's logs and were read from
the binaries the gate built.

`[structural]` and `[thermal]` matching their pre-change baselines exactly is
the measurement that matters most here, because this is the first P19 milestone
to modify a module both of those physics link. A pass/fail count cannot show
that: a deleted test and an added test net to nothing in it, and do not net to
nothing here.

### Cross-preset determinism

```text
24-cell asymmetric chain                     045b00105d248b79
24-cell momentum system                      045b00105d248b79
24-cell pressure system                      9c29663b4850a63c
```

The same values in all three presets — Debug static, Release static and Debug
shared — which is a stronger statement than the five identical executions each
repeat preset already recorded, because it crosses optimization level and
linkage rather than repeating one binary.

**TWO OF THE THREE ARE THE SAME VALUE, AND THAT WAS CHECKED RATHER THAN
ACCEPTED.** A 64-bit collision does not happen by chance, so either the two
fingerprints compute the same thing or one is not hashing what it claims. They
compute the same thing, and the chain of equalities is exact:

```text
both hashes       FNV-1a, offset 14695981039346656037, prime 1099511628211,
                  byte-wise over std::bit_cast<uint64_t> of each solution value
                  -- the two functions are identical
linalg's system   chain(24, 1.0e-6, 1.0e-6), rhs[0] = 1.0e-9, rest 0
cfd's momentum    momentumShape(24, 1.0e-6, 1.0e-6), rhs[0] = 1.0e-9, rest 0
the two fixtures  IDENTICAL line for line, same accumulation order
the path          solveCfdMomentumSystem routes to linalg::solveSparse with the
                  conditioning floor taken from the kernel's own default
```

So this is not a coincidence to explain away but the milestone's thesis in one
number: the CFD momentum path reproduces the raw kernel **bit for bit**, which
is what reusing a kernel rather than reimplementing it is supposed to mean. The
pressure system differs because it is a different matrix solved by a different
kernel.

One imprecision in a test comment is recorded rather than fixed, because fixing
it would void the qualification over a comment. `SparseSolveTests.cpp` says the
cross-preset claim is made "by diffing the three presets' logs". The method is
right and the logs are the wrong ones: `ctest --preset` captures no stdout from
a passing test, so the values must be read by running the binaries, which is
what was done here. The comment is inside the frozen surface, so correcting it
now would make the committed tree differ from the qualified tree — the one thing
a freeze exists to prevent — and it is left for the next milestone that touches
that file.

**These were read from the binaries, and the claim they replace had no log
behind it.** An earlier form of this README said the identity was established by
diffing the presets' logs. It cannot be: `grep -c "bit fingerprint"` over the
committed `ctest` logs of the two previous milestones returns 0 and 0, because
Catch2 writes `WARN` to the test's stdout and `ctest` discards that for a
passing test. The claim was removed before the freeze and is replaced here by a
measurement.

## QUALIFICATION

Three presets configured and built from clean, then two repeated five times, from
the tree frozen at `30c04cc` plus this milestone's eight new and six modified
files. `FREEZE.md` holds the frozen identities, the figure derivations and the
post-gate reconciliation.

```text
verdict        run-qualification.out  "Qualification passed: every stage exited 0."
               qualification-times.txt  "0 stage(s) failed"
stages         17, all exit 0
wall clock     07:54:33 -> 11:41:35
```

**The verdict is read from the harness's own two statements, never from the
launcher's exit code.** `P19-MOM-001`'s first attempt was reported as
"completed (exit code 0)" while the harness itself had written
`QUALIFICATION FAILED: 2 stage(s) failed`. Here both sources agree, and the
grep that first looked for them found nothing because it assumed upper-case
wording — the file was then read whole rather than trusted to a pattern.

### The qualified tree is the committed tree

```text
                   before the first build                 after the last test
apps               fc398a3d2e524178cf38e8b667236379d8381086        MATCH
include            d52820c56a1ae388f2a4f12b126b45c103308bca        MATCH
src                56358d8c8c6c97f55ded83b74e5d36e0759281ed        MATCH
tests              ba7cf940350cf23c61563eb51aef65f379f75b98        MATCH
examples           4da234ebf4779f7c16d5df0075f106f7e3d4c958        MATCH
cmake              5a382115d7ed7775189d40b55e40f318eaae6cc7        MATCH
CMakeLists.txt     3ec3c10f2a4c522f36c60b1ce1a58e99c4db1ecd        MATCH
CMakePresets.json  3229f0f89b236744d7d5c082bed47c28b718288e        MATCH
```

The harness read all eight twice, and they were recomputed from the working tree
a third time after the gate: unmoved. `git status` over the same eight paths
shows exactly 8 added and 6 modified files and nothing else.

### Freshness, and the check that had to be re-scoped first

```text
debug-ext          5899 ninja outputs declared, 1145 on disk, 0 newer than build exit
release-ext        5899 declared, 1145 on disk, 0 newer than build exit
debug-shared-ext   5912 declared, 1158 on disk, 0 newer than build exit
binary mtimes      08:11:33 / 09:08:38 / 10:10:04, each BEFORE its own ctest start
                   and before both repeat runs (10:58 and 11:22)
no-op rebuilds     one line each, exit 0, identical in shape to the three
                   previously committed milestones' copies
```

A first attempt asked whether a file had a build-artifact extension and sat
inside the build tree, which is a different question, and it reported a FAIL on
8 files per preset. All of them were zero-byte stubs and mismatched copies that
the deployment TESTS author in their own `ctest` scratch directories. Asking
`ninja -t targets all` instead — the build tool's own list of what it produces —
gives the answer above. `FREEZE.md`'s addendum has the detail, including why
`ninja -n` is not a staleness check and why no build was run during the
reconciliation.

### The caches

All three resolve the pinned WinLibs toolchain and exactly one dependency
prefix, with zero `msys64` references: `CMAKE_COMMAND` and `CMAKE_CXX_COMPILER`
under the WinLibs package path, `BETTERCAD_DEPS_PREFIX` at
`bettercad-deps/gnu-16-mingw-amd64`, and `BUILD_SHARED_LIBS` OFF/OFF/ON with
`CMAKE_BUILD_TYPE` Debug/Release/Debug.

## RESULT

**PASS.**

All fourteen `TODO.md` items are discharged, and the three the checklist put
first — audit, respect the layer ADRs, reuse the lower-layer kernel — are why
this milestone added no second factorisation. The layering table already placed
`linalg` at 5 and `cfd` at 42 under a strictly-lower rule, so `cfd` was already
permitted to include it; `src/cfd/CMakeLists.txt` had already named the kernel to
reuse and the `static_assert` that was owed. Reading those three records first is
what turned a solver-writing task into an audit, a `PRIVATE` link and a call.

What the audit found is the milestone's most useful result: `solveSymmetricSparse`
**performs no symmetry check**, and no test anywhere had ever shown it an
asymmetric matrix. It is not a defect — the independent residual computed from
the original arrays is what protects it — but it was an untested precondition
under a name that promises otherwise, and it is now pinned by tests in both
kernels.

```text
new general kernel   linalg::solveSparse, SparseQR, gated on a READABLE R
                     diagonal. SparseLU was measured and rejected: it reports
                     Success with normalized residual exactly 0 on a system
                     singular to 1e-13, and the pinned Eigen exposes no accessor
                     for U's diagonal, so recovery would mean a dense inverse --
                     272 MB at P18's smoke size
three gates          info() == Success and min|R_ii|/max|R_ii| >= floor; every
                     component finite; the independent residual passes. rank()
                     is REPORTED, never gated
one residual gate    solveSparse calls linalg::evaluateResidual and translates
                     its verdict rather than computing a second residual
routing              exact symmetry, largestSymmetryError() == 0.0, because the
                     coupling takes ONE port for both systems
memory safety        a reachable out-of-bounds read in the ALREADY-QUALIFIED
                     symmetric kernel, repaired in both kernels by the owner's
                     decision; [structural] and [thermal] measured identical to
                     their baselines afterwards
```

**Three probe rounds found what two independent reviews could not, because all
three survivors were in the fixes rather than in the original code.** `M4c`
showed a two-part guard whose tests all exercised the wrong half; `M9a` showed a
CFD guard the kernel below made invisible, catchable only through check order;
and `M4e` was not a missing test but a proof that one clause could not fire,
so it was removed from both kernels. The guard that ships is simpler than the one
the audit's finding first produced, and provably sufficient rather than
defensively redundant.

**The post-gate reconciliation found four defects in this milestone's own
evidence**, none in the frozen surface, all corrected and recorded rather than
quietly rewritten: a harness paragraph that asserted a file was unmodified four
lines above its own correction; a count of "six" new files where there are eight;
this README's `TESTS` block still holding the pre-review snapshot and calling it
`EXACTLY`; and a `cfd` assertion figure four short, which survived because
728,802 + 22,965 = 751,767 made it internally consistent and so unfalsifiable
from inside the document. Only reading `[cfd]` off the built binary exposed it.

One finding is reported and deliberately NOT fixed: `verify-harness.cmd` carries
a literal `0x08` byte where `\bc-build` was meant, in 72 of 96 committed copies,
entered 2026-09-30. It fires only when `BETTERCAD_BUILD_ROOT` is undefined, which
`run-qualification.cmd` always defines, so no gate ever evaluated it. Repairing
it across the lineage is outside this milestone's authorization.

```text
TASK:            P19-LINSOLVE-001
IMPLEMENTATION:  8 new files, 6 modified; linalg::solveSparse and the CFD solve
TESTS:           6870 per preset x 3; 3474 x 5 x 2; 28 compile-fail gates
VALIDATION:      independent residuals from the original arrays; the coupling
                 suite's own dense Gaussian reference, agreeing to 2.4e-16
RESULT:          PASS
EVIDENCE:        docs/verification/P19-LINSOLVE-001/
TODO:            14 items ticked, 705 -> 719
```

## REVISION

**r1** — the pasted checklist was diffed against `TODO.md`'s own section before
any work, byte-for-byte across 23 lines, 14 unticked and 0 ticked counted rather
than assumed, sha256 `86a54ddba6c61ce2`; and the authorization plus six scope
notes were written into `TODO.md` BEFORE any implementation.

**r2** — the UNDERSTAND phase found that the repository had already decided most
of this milestone's shape, in three independent committed places, and that `cfd`
was ALREADY permitted to include `linalg` by the layering table. Without that
phase this milestone would have written a second symmetric factorisation inside
`cfd` — the duplication `ADR-052` chose extraction to avoid.

**r3** — the same phase found that `ADR-058` already declares the one solver
ratio and says which decisions are not its own, so "Define tolerances" meant bind
the declared ratio and decide where the conditioning floors come from. This is
the `P19-CONT-001` and `P19-PVCOUPLE-001` lesson a third time.

**r4** — the audit found the symmetric kernel's symmetry precondition
UNQUALIFIED: no check in the code, no `NotSymmetric` enumerator, and no test
anywhere that had ever shown it an asymmetric matrix. `ADR-076` Decision 6 chose
to keep the behaviour and pin it with tests rather than change a kernel two
qualified physics depend on.

**r5** — `ADR-076` Q5's premise was false and the header it was about said so. It
pinned the pressure reference cell with a unit diagonal and called that the
qualified assembly's behaviour; the rule is "the diagonal keeps its own value"
and the unit value is a reported exception. Re-measured, the rule's path is
scale-free at `0.25` and the exception's is `0.5 * a_f`. The narrow hazard
survives as `L1`.

**r6** — Q7's momentum fixture was SINGULAR and its own output announced it: the
same largest LU-vs-QR gap, `1.574122e+12`, for three different system sizes. One
number for three different systems is a property of the fixture, not the systems.
Round four asserts `rank == n` before believing any other number. This is the
second measurement defect of the milestone and the second to be caught by a
constant that could not be a measurement.

**r7** — `SparseLU` was ruled out on a library-API claim, and the claim was
VERIFIED against Eigen's own source rather than asserted:
`Eigen/src/SparseLU/SparseLU.h` exposes `matrixL`, `matrixU`, the permutations,
the determinant family and `nnzL`/`nnzU`, and no accessor for U's diagonal.
Ruling a candidate out on an unchecked claim about a library is the error this
project keeps catching.

**r8** — the first form of `Linalg_SparseProblemMatchesTheFailure` did not
compile: a braced initializer cannot bind to `std::span<const double>`. Recorded
because it is the compiler doing a reviewer's work.

**r9** — the "floor is a setting" section asserted the wrong direction and
failed, and `ADR-076` Q7 had already measured why. QR's column pivoting makes the
ratio exactly `0.0` for a column it discards, so no floor admits it; the
demonstration had to raise the floor rather than lower it. The contrast with the
LDLT is now asserted explicitly.

**r10** — six of the twenty-eight compile-fail gates failed on the first run
because the regex omitted GCC's actual wording, `to non-scalar type`. The block's
own comment cites `P19-GRAD-001` for exactly this, so the author reproduced the
defect the comment warns about. The same run revealed that every case in the
INHERITED convention also trips `-Werror=missing-declarations`, so a gate's exit
code is only attributable because the regex discriminates; the new cases were
made `[[maybe_unused]] static` so the intended diagnostic is the only one.

**r11** — THE AUDIT'S MOST SERIOUS FINDING WAS A MEMORY-SAFETY DEFECT, IT WAS
PRE-EXISTING, AND BOTH REVIEWS FOUND IT INDEPENDENTLY. Neither kernel validated
that `rowStart` is non-decreasing or that an interior offset lies within the
stored entries, so `rowStart = {0, 5, 2}` for a two-row system with two entries
passed every check and the build loop read past its spans. Reproduced as an
abort inside `std::span::operator[]` before anything was changed. Fixed in both
kernels; `ADR-076` Decision 10 records the scope judgement, and `[structural]`
and `[thermal]` are unchanged at 271,414 in 415 and 595,622 in 1168.

**r12** — THE CONDITIONING GATE VIOLATED EIGEN'S DOCUMENTED CONTRACT, AND THE
WRONG VALUE COULD NOT BE REPRODUCED. `matrixR()` is documented as unsorted and
its documentation names `coeff()` specifically; the first implementation was
Eigen's own counter-example verbatim. A rank-deficient fixture with a non-last
dependent column was built to provoke the permuted branch — `rank` came out
`3 of 4` — and all three read forms agreed. Changed anyway, because the previous
form worked only through an undocumented shortcut in
`CompressedStorage::atInRange`. Recorded as a contract violation with an honest
"could not reproduce a wrong number", not as a near-miss that was caught.

**r13** — THE PORT ROUTED ON A CONDITION WEAKER THAN THE KERNEL'S PRECONDITION.
Exact symmetry is not symmetric-positive-definite. `[[-2]] x = [4]` and
`[[1,2],[2,1]] x = [3,3]` are both exactly symmetric and trivially solvable, and
both were refused as `system_is_singular` with a message reporting a pivot ratio
"below the floor of 1e-12" when the ratios were 1 and 0.333 — self-contradictory,
because the kernel formats that sentence whenever its pivot gate fires for either
reason. A new enumerator reports what happened, built from the
`allPivotsPositive` fact the kernel already carried. No fallback was added:
both kernels forbid one.

**r14** — THREE COMMENTS CLAIMED MORE THAN THE CODE DID, in a milestone whose
own evidence twice criticises inherited prose for the same fault. "Its
`pivotFloor` is not read on this path" was false — `evaluateResidual` validates
its settings first. `SingularSystem`'s doc claimed a library verdict no path
produces. And the back-substitution guard was counted as one of three gates when
`SparseQR` cannot leave a non-`Success` state after a solve. All corrected in
place; the guard is now labelled unreachable-and-kept.

**r15** — A FALSE CLAIM IN THIS EVIDENCE, FOUND BY FACT-CHECK. The determinism
section said the cross-preset fingerprint identity was "established by diffing
the three presets' logs". `grep -c "bit fingerprint"` over the committed
`ctest-debug-ext.log` of both `P19-PVCOUPLE-001` and `P19-CONT-001` returns 0:
`ctest --preset` captures no stdout on a passing run. The claim had no log behind
it. It is replaced by a post-run measurement from the three qualified binaries,
the same way the assertion counts already were — and it is worth noting that the
inherited practice, not just this milestone's wording, was wrong.

**r16** — THE INTEGRATION TEST'S HEADLINE NUMBER WAS NEARLY AUTOMATIC. Its bound
was `1e-6` and `CfdConvergenceSettings` stops at `1e-6`, so both runs halted
inside the same neighbourhood of the fixed point and agreement to `1e-6` proved
little beyond the absence of gross error — while this evidence had already
quoted the result as machine-precision agreement. A single iteration from one
shared state removes the loop, and the real figures are velocity `1.383e-16`
and pressure `2.190e-16` against a `1e-10` bound. The loop comparison is kept
because convergence to the same fixed point is its own claim, with its bound
labelled for what it is.

**r17** — SEVENTEEN OF TWENTY PREDICTED MUTATIONS WERE REAL GAPS. The
test-adequacy review was asked for one-line edits no test would catch and gave
twenty, ranked. Among them: a 1x1 system returned an empty solution and reported
success under `rows <= 1`, because nothing anywhere was smaller than 2x2; a
non-square matrix on the momentum path would have had its leading square block
solved and a column discarded in silence; no test provoked a singular system
through the general path, so the commonest real failure of a momentum solve
could have been classified as a library fault; the conditioning ratio was
asserted only as `> 1e-3`, which a hard-coded constant satisfies and which no
scale sweep can distinguish; and the recomputed-residual check was an ABSOLUTE
`1e-18` bound on a quantity of magnitude `4e-17`, a 2.4% relative tolerance that
a 0.1% corruption passes. The three not closed are documented-unreachable
branches recorded as `L10`-`L12`.

**r18** — ONE REVIEW FINDING WAS A SCOPE QUESTION, AND IT WENT TO THE OWNER.
The test review judged the `linalg` memory-safety fix a decision for the owner
rather than something to fix here, and on that it was RIGHT: `TODO.md` is the
only authority for scope, and the reviewer had identified the one finding whose
repair reached outside this milestone's obvious boundary. The author's three
reasons for keeping it are in `ADR-076` Decision 10, but they are an argument
and not an authorization.

So the question was put to the owner with the measurement attached — both
dependent physics unchanged at 271,414 in 415 and 595,622 in 1168 — and with the
alternative stated plainly, which was to revert it and file it as its own
milestone. **The owner's answer on 2026-11-09 was to keep it.** The fix is
therefore in scope by decision rather than by inference, and the reviewer's
instinct to escalate rather than to let the author decide is recorded as
correct.

**r19** — A SHELL DEFECT CORRUPTED THIS FILE WHILE THE CORRECTION IN `r15` WAS
BEING WRITTEN. Backticks inside a `python -c` string passed through `bash` were
command-substituted, so three inline code spans were replaced by the output of
`ctest --preset`, `WARN` and `grep`. Caught by a scan for lines with an odd
backtick count, and repaired. The fifth tooling defect of this lineage's
`cmd.exe`/heredoc family, and the reason this session writes scripts with a file
tool rather than through a shell string.

**r20** — A FORTY-MINUTE FULL-SUITE RUN WAS INVALIDATED BY THE AUTHOR
REBUILDING UNDERNEATH IT. A background `ctest -j 8` over `debug-ext` was
launched before the review findings were fixed, and the twelve rebuilds the
fixes needed — and then the mutation probes — relinked the very binary it was
executing. The timestamps settle it: the test binary was relinked at 07:02:46
and the log closed at 07:02:31. The run therefore executed a MIXTURE of
binaries and is void. Its single reported failure, `cli.new.unicode-path`, was
recorded here as uninterpretable rather than benign and then checked on a quiet
tree — see `r21`, which found a cause that was neither the rebuild nor a defect.

The lesson is not "do not run the suite": it is that the only full-suite
evidence that counts is the QUALIFICATION GATE, which configures and builds
from clean and runs with the tree frozen and nothing else touching it. A
convenience run during active editing proves nothing, and its PASS would have
been just as worthless as its FAIL — which is the more dangerous half, because
a pass would not have prompted anyone to check the timestamps.

**r21** — THE ONE TEST FAILURE SEEN IN THIS MILESTONE WAS THE AUTHOR'S SHELL,
AND IT WAS CHASED DOWN RATHER THAN ATTRIBUTED. `cli.new.unicode-path` failed on a
quiet tree as well as in the void run, which ruled out the concurrent rebuild and
made it a stop condition: a required gate failing. It also passed in all three
presets at `30c04cc`, so something had changed. The diagnosis:

```text
run from Git Bash (the Bash tool)   FAILED
run from cmd.exe at code page 65001 PASSED
```

The test writes a document named `Plåt ✓` and matches the CLI's confirmation
line against a UTF-8 regex; its verdict depends on the console code page of the
shell that launches `ctest`. The harness always launches from `cmd.exe`, which is
the qualified path and the one the gate uses, so the tree is not at fault and
neither is this milestone — the CLI was never touched.

Two things are nonetheless worth recording. The failure is a real if narrow
property of the suite: a test whose result depends on the invocation
environment is not environment-independent, and nothing in the suite says so.
And `r20`'s guess was wrong — it leaned toward the concurrent rebuild as the
likely cause while declining to hand-wave it, and the real cause was neither
that nor a defect. Declining to hand-wave it is what produced the right answer;
the guess would not have.

**r22** — THREE PROBE ROUNDS, AND ALL THREE SURVIVORS WERE IN THE FIXES RATHER
THAN IN THE ORIGINAL CODE. `M4c` showed a two-part guard whose tests all
exercised the wrong half; `M9a` showed a CFD guard made invisible by the kernel
repeating it, catchable only through the check ORDER; and `M4e` was not a missing
test at all but a PROOF that one clause of the new guard could not fire, so it
was removed and monotonicity now carries the whole weight. The guard that ships
is simpler than the one the audit's finding first produced. Twenty-nine
behavioural probes are now killed, the no-op control survived all three rounds,
and every revert was verified by hash across all four watched files.

Details in [experiments/mutations-summary.md](experiments/mutations-summary.md).
