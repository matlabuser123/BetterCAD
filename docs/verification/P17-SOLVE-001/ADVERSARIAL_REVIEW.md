# P17-SOLVE-001 — adversarial review

```text
RESULT: PASS
```

Twenty-nine attacks, from brief section 133, asked against the final diff.
**Four** found real defects; one was in production and three were in my own
tests or in the infrastructure I added. All four are fixed.

## The four defects this review found

### D1 — Eigen's `NumericalIssue` was reported as the wrong kind of failure

A production defect, found by the exactly-singular `[1 1; 1 1]` fixture.

The first draft mapped any non-`Success` from `compute()` to
`FactorizationFailure`. But for the LDLT path `m_info` is assigned in exactly
one place — `ok = false` when `d == RealScalar(0)` — so `NumericalIssue` has
exactly one cause and that cause is a **zero pivot**, which is a statement
about the physics. Calling it a factorisation fault would have sent a caller
looking for memory or a malformed matrix.

Fixed: `NumericalIssue` from `compute()` is now `SingularSystem`, with the
message saying *"found an exactly zero pivot"*, and `FactorizationFailure` is
reserved for the library refusing the matrix outright. The division of labour
is now explicit in a test: Eigen catches the exact zero, the pivot gate catches
everything else.

### D2 — the near-singular demonstration was accidentally exactly singular

`[[1, 1], [1, 1 + 1e-16]]` is `[[1, 1], [1, 1]]` in double precision: `1e-16`
is below the ulp of 1, which is about `2.22e-16`. So the fixture meant to show
"library success with a passing residual" instead produced an exactly zero
pivot and was caught by Eigen — the opposite of the point.

Fixed with `1e-13`, which is about 450 ulps and genuinely distinct. The
demonstration now reads: `x = (-100079, 100080)`, pivot ratio `9.99e-14`,
**normalized residual `1.00e-17`, which passes the `1e-9` gate**. That is the
measurement the milestone needed.

### D3 — the free body could not be used for that demonstration at all

The draft tried to show "without the pivot gate the library would succeed" by
relaxing `pivotFloor` on a free structural body. It failed: the free body's
factorisation produces a **negative** pivot, so the positivity half of the gate
refuses it however the floor is set, and the ungated behaviour is unreachable
through the public API.

That is better news than the draft assumed — the free body is caught by two
independent signals — but it meant the demonstration had to move. It is now on
the synthetic system of D2, where everything is controllable, and the free-body
test instead proves Eigen reported success **from the diagnostic itself**: the
pivot-gate message can only be produced after the `info() != Success` branch
was not taken, so the test asserts the message is that one and is neither the
exactly-zero-pivot message nor the rejected-matrix message.

### D4 — the new architecture rule's diagnostic was mangled, twice

Infrastructure, found by writing the rule's own fixture self-test rather than
by reading.

Rule 7 (Eigen containment) reported **three** violations on a fixture holding
one. Two causes, both CMake list semantics:

```text
list(APPEND violations "first string" "second string")   appends TWO elements
"... in a public header; it is PRIVATE ..."              the SEMICOLON is a
                                                         CMake list separator,
                                                         so one string became
                                                         two elements again
```

So a single violation counted as three, then as two, and the message printed
across broken lines. Fixed by concatenating into one string and replacing the
semicolon with a dash. Without the self-test the rule would have shipped with a
mangled diagnostic and an inflated count — and the fixture also proves the rule
**fires on the public header and not on the permitted `src/` use**, because it
holds both and the expected count is one.

## The twenty-nine attacks

### Can solver success be accepted without independent residual validation?

No. Three gates are required and the library supplies one. The residual is a
loop over BetterCAD's own CSR copy of `Kff` against its own `Ff`, computed
after the solve returned; `solver.error()` has **zero occurrences** in the
file, and the only Eigen members used at all are `compute`, `info`, `solve` and
`vectorD`. With the threshold set below what double precision can deliver, the
solver reports success and BetterCAD refuses — reached through the public API
with no test double. The probe that removes the gate is killed.

### Can the residual be checked on the wrong full system where reactions make it non-zero?

No, and this is the trap the brief calls the most important. The gate is
`Kff uf - Ff`. Measured on the block fixture: the largest free entry of
`K u - F` is `2.56e-13` N and the largest constrained entry is `273.83` N,
summing to `-1000` N against a `+1000` N applied load. Both halves are
asserted, including that the constrained entries are **not** zero — which is
the premise that makes the distinction necessary. The probe that computes the
residual over the full system is killed by **twelve** tests.

### Can a singular free body be regularised and reported solved?

No. `epsilon`, `1e12`, `1e20`, `penalty`, `stabilize` and `regularise` have
zero occurrences. The C0 fixture is refused, and the probe that adds `1e-6` to
the diagonal is killed by six tests.

### Can an arbitrary node be pinned automatically?

No. `pin` and `spring` have zero occurrences, and C0 and C1 both fail rather
than being made solvable.

### Can a penalty factor alter the engineering solution?

No penalty exists. The constraint method is exact reduction and the two methods
are not combined — nothing zeroes a row or sets a diagonal to one.

### Can insufficient restraints produce enormous finite displacements and still PASS?

No. C1 — one component on one face — is refused with `SingularSystem`. The
synthetic analogue, where that *would* happen, is exactly the D2 measurement:
a solution five orders larger than its data with a passing residual, refused by
the pivot gate.

### Can a disconnected free component survive?

An unrestrained disconnected component contributes its own rigid-body modes, so
`Kff` is singular and the solve fails — the same mechanism as C0. The
diagnostic deliberately says *"may be under-constrained, disconnected or a
mechanism"* rather than claiming to know which, because distinguishing them
needs a nullspace analysis this milestone does not perform. P16 produces one
solid, so there is no production fixture; the honesty of the diagnostic is what
is recorded instead of a claim.

### Can `Kff` use the wrong row/column ordering?

No. Every reduced entry is compared against `K(dofOf(i), dofOf(j))` with a
tolerance of **zero**, in both directions, and every reduced row's inner
indices are asserted strictly ascending. The probes that keep the `Kfc`
columns and that take the force from the wrong row are both probed.

### Can constraint insertion order change the solution?

No. The free set comes from `FreeEquationMap::freeDofs()`, which P17-DOF
documents as ascending and which `ConstraintSet` sorts — so the reduced system
is a function of the constrained *set*, not of the order it was built in.
P17-BC-001 already proved its `ConstraintSet` is insertion-order independent,
and this milestone consumes that rather than re-deriving it.

### Can constrained DOFs become tiny non-zero values instead of exact zero?

No. The full vector is allocated zero-filled and only free rows are written, so
a constrained entry is exactly `0.0`. Asserted over every constrained degree of
freedom with an exact comparison, and the probe that initialises the vector to
`1e-9` is killed by four tests.

### Can a solver-internal permutation leak into P17 DOF identity?

No. AMD permutes inside the factorisation and `solve()` returns the vector in
the order it was given. The test checks `displacementOf(dof)` and
`values()[dof.value() - 1]` agree for every free degree of freedom, so a
reordering would break one of them, and the probe that shifts the mapping by
one is probed.

### Can M1 `K`/`F` be solved with M2 constraints because dimensions match?

No, and the fixture is built so dimensions *do* match: the same model remeshed
gives the same degree-of-freedom count and a different `MeshStamp`.
`buildFreeEquationMap` refuses it, `extractFreeSystem` refuses it
independently, and there is no other way to obtain a `FreeEquationMap`. The
probe that disables the pairing check is probed.

### Can old `K` after an `E` change be solved as current?

The solve itself cannot tell — `K` is an input — so the answer is in the
provenance: `SolvedSystem` carries `AssemblySource`, which carries the material
identity **and revision**, and the test asserts the source moves on a modulus
edit. So an old solution is distinguishable from a current one by comparison,
which is P17-DATA-001's existing mechanism rather than a new one.

### Can old `F` after a load edit be solved as current?

Same answer and the same mechanism. `F` is assembled from the prepared loads,
and the analysis revision that covers loads lives in
`StructuralResultSource`, which a solve consumer compares at the P17-DATA
boundary. This milestone adds no second currentness path — deliberately, and
recorded.

### Can stale geometry or a stale mesh still produce a nominally current result?

No. A `GlobalStructuralSystem` can only exist for a `StructuralModel`, whose
possession is ADR-036's evidence that the geometry and the mesh are current.
The solver inherits that gate rather than re-asking it, and `SolvedSystem`
carries the mesh stamp so `describes()` refuses a mesh it does not belong to —
asserted after a remesh.

### Can NaN/Inf solution pass because solver status says Success?

No. Every component of `uf` is checked finite after the solve, every residual
component is checked finite, and the strain energy is checked finite. The probe
that accepts a non-finite solution is probed.

### Can residual normalization divide by zero for `F = 0`?

No, and there is no floor either. The denominator vanishes only when `uf` and
`Ff` are both exactly zero, and then `r` is exactly zero, so the honest value
is `0` — and the impossible case (zero denominator, non-zero residual) is
*refused* rather than flattered. Measured for `F = 0`, `F ~ 1e-30` and
`F ~ 1e20`.

### Can a raw residual threshold incorrectly depend on force scale?

No. The gate is on the dimensionless ratio and the raw norms are recorded
alongside. `||r|| = 1e-6` N would pass for `F ~ 1e9` N and fail for `F ~ 1` N,
which is the point of normalising.

### Can solver defaults change with library version?

Nothing material is inherited. The algorithm is named, both tolerances are
BetterCAD's own with recorded reasons, and the fields a direct solver does not
have are **absent** rather than defaulted — two compile-failure cases prove
that referring to `maximumIterations` or a preconditioner does not compile. The
one Eigen default used is the AMD ordering, which is recorded and is
solver-local.

### Can an iterative solver silently use unlimited or default iterations?

Not applicable: the solver is direct. Recorded as `N/A` rather than invented as
a field, and the absence is compiler-enforced.

### Can internal library threading change deterministic behaviour?

No. OpenMP is not enabled in the build, `EIGEN_HAS_OPENMP` is gated on
`_OPENMP`, `SparseCholesky` and `OrderingMethods` contain no `pragma`, `omp` or
`thread`, and no external BLAS is linked. Measured from the other side too:
five runs are bitwise identical, which is the canary that would notice if any
of that changed.

### Can a 0x0 fully constrained problem crash?

No. `Nfree == 0` short-circuits to a successful `u = 0` with a pivot ratio of
`1.0` and nothing handed to the library, so no `0 x 0` factorisation is
attempted. The probe that removes the short-circuit is probed.

### Can an SPD solver accept an indefinite matrix?

No. `[1 2; 2 1]` has eigenvalues `-1` and `3`; LDLT produces a negative pivot
and the positivity half of the gate refuses it. The probe that ignores
positivity is killed.

### Can a production solve densify `Kff`?

No. The reduced system is a CSR, the factorisation is Eigen's sparse
`SimplicialLDLT`, and the residual is a sparse row walk. Measured on the large
fixture: `Kff` is `2250 x 2250` with `82584` non-zeros occupying **1.26 MiB**
against **38.62 MiB** for a dense equivalent, and entries per row stays bounded
as the mesh grows. Dense matrices appear only in the test oracle, on a
`12 x 12` reduced system.

### Can a failed solve overwrite an older valid result as current?

No. `SolvedSystem` has no default constructor and one friend, so a solution
exists only if all three gates passed; a failure returns `std::unexpected` and
touches nothing. This milestone holds no result store, so there is no older
result to overwrite — `P17-DATA-001` owns result retention and
`P17-POST-001` will publish into it.

### Can solver settings change without invalidating an old result?

No. `SolvedSystem` carries the `SolverSettings` it was produced under, and two
solutions with different tolerances compare unequal even when the numbers
agree — asserted.

### Can test expected solutions be generated by the same production solver?

No, and this was designed against. The 2x2 and 3x3 expectations are closed-form
fractions written out by hand; the structural expectation is dense Gaussian
elimination **with partial pivoting**, which LDLT does not do, written out in
the test file. Measured agreement: `2.87e-16` relative.

### Can zero filtered tests be reported as PASS?

No. The selection was counted before it was trusted:

```text
ctest -N -R "StructuralSolve_|structsolve"    Total Tests: 30
    24 unit + reference + 6 compile-fail
```

and the final regression is unfiltered in all three presets.

### Can stale binaries or a different source tree generate qualification evidence?

No. Each preset is configured, has every build output removed, is rebuilt, and
is tested only after a successful build; the no-op rebuild check proves the
binaries CTest ran are the ones just produced, and the harness reads the eight
component hashes before the first build and again after the last test run. See
[FREEZE.md](FREEZE.md).

## What was NOT found

No hidden global state; no behaviour that exists only for tests; no GUI or CLI
dependency — `src/structural/` includes no Qt, no CLI and no io header, and the
architecture test enforces it; no persistence — nothing saves `K`, `Kff`, `F`,
`Ff`, a factorisation or `u`; no post-processing — `strain`, `stress`,
`reaction` and `vonMises` have zero executable occurrences, and the only
"strain" in the file is the strain-energy diagnostic string.

**And no predecessor production file was modified.** The one shared file this
milestone touched is `tests/architecture/CheckLayering.cmake`, which is test
infrastructure, extended with rule 7 to enforce this milestone's own invariant
— with its own fixture self-test, and with all fifteen architecture tests
passing. `src/structural/CMakeLists.txt` gained the Eigen link and the new
source; no predecessor's code changed, so brief section 150's requalification
requirement has nothing to act on beyond the full run.
