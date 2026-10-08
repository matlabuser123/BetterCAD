# P17-SOLVE-001 — singularity, and why the pivot gate is not redundant

## The claim

**A library saying `Success` is not evidence, and neither is a passing
residual.** Both statements are measured here rather than argued, because
together they are the justification for a gate the library does not have.

## What Eigen does and does not detect

From `Eigen/src/SparseCholesky/SimplicialCholesky_impl.h`, the only place
`m_info` is set on the LDLT path:

```cpp
if (DoLDLT) {
  m_diag[k] = d;
  if (d == RealScalar(0)) {
    ok = false; /* failure, D(k,k) is zero */
```

An **exactly** zero pivot. Measured, both sides of that line:

```text
fixture                           pivot behaviour          Eigen info()   BetterCAD
---------------------------------------------------------------------------------------
[1 1; 1 1], rhs (1,2)             D(2,2) == 0 exactly      NumericalIssue SingularSystem
[1 1; 1 1], rhs (1,1) consistent  D(2,2) == 0 exactly      NumericalIssue SingularSystem
[1 1; 1 1+1e-13]                  D = (1, 1e-13), positive Success        SingularSystem
                                                                          (pivot gate)
[1 2; 2 1] indefinite             a negative pivot         Success        SingularSystem
                                                                          (pivot gate)
free structural body              min|D|/max|D| = 3.4e-17  Success        SingularSystem
                                  and at least one pivot                  (pivot gate)
                                  not positive
```

### A correction this milestone's own tests forced

The first draft reported Eigen's `NumericalIssue` as `FactorizationFailure`.
The `[1 1; 1 1]` fixture failed, and the failure was right: for the LDLT path
`NumericalIssue` has **exactly one cause**, and that cause is a zero pivot —
which is a statement about the physics, not about memory or a malformed matrix.
Reporting it as a factorisation fault would have sent a caller looking in the
wrong place.

It is now `SingularSystem`, with the message saying *"found an exactly zero
pivot"*, and `FactorizationFailure` is reserved for the library refusing the
matrix at all.

## The pivot gate

```text
    PASS  iff  every D(k,k) > 0  AND  min|D| / max|D| >= pivotFloor
    pivotFloor = 1e-12,  dimensionless
```

**A ratio, not an absolute pivot.** `D` carries the units of the stiffness
diagonal, so a soft material with a small `E` has small pivots and is not
singular. A raw threshold would confuse the two, which brief section 101 names
explicitly. Checked: a system whose pivots are `(1, 1e-8)` — condition number
`1e8` — solves and is *not* refused.

**The threshold does not decide the verdict**, which is the measurement that
matters:

```text
fixture                           min|D| / max|D|        floor      verdict
-------------------------------------------------------------------------------
RM-MESH-01, fixed support                 0.211          1e-12      PASS
block fixture, fixed support              0.0726         1e-12      PASS
RM-MESH-03, fixed support                 0.0427         1e-12      PASS
RM-MESH-04, fixed support                 0.0204         1e-12      PASS
RM-MESH-02 large, fixed support           2.26e-04       1e-12      PASS
ill-conditioned diag(1, 1e-8)             1e-08          1e-12      PASS
diag(1, 1e-14)                            1e-14          1e-12      REFUSED
[1 1; 1 1+1e-13]                          ~1e-13         1e-12      REFUSED
free structural body                      3.4e-17        1e-12      REFUSED
```

The nearest PASS is `2.26e-04` and the nearest REFUSE is `1e-14`: **ten orders
of separation**, with the floor sitting in the middle of it. Between the
well-restrained block (`0.0726`) and the free body (`3.4e-17`) the gap is
**fifteen orders**.

`1e-12` was chosen for a reason and not from these numbers: the error of an
LDLT solve grows with `max|D| / min|D|`, so a ratio below `1e-12` means the
component of the solution along that direction has lost twelve of a double's
sixteen digits and is noise.

And the floor is a **setting**, so a caller who knows their problem can say
what they will accept — tested by solving `diag(1, 1e-14)` with the floor at
`1e-16`.

## Why a residual check could not have replaced it

This is the load-bearing measurement of the milestone.

```text
    [ 1   1        ] [x1]   [ 1        ]
    [ 1   1+1e-13  ] [x2] = [ 1 + 1e-8 ]
```

Pivots `(1, 1e-13)`, both positive, so Eigen factorises happily. The matrix is
**nonsingular** — determinant `1e-13` — so the solve is accurate and the
residual is tiny. And the answer is `x2 = 1e-8 / 1e-13 = 1e5`: five orders
larger than any number in the problem, from an input perturbation of `1e-8`.

Measured, with the pivot gate relaxed to `denorm_min`:

```text
x                      (-100079, 100080)
pivot ratio            9.99e-14
normalized residual    1.00e-17        <-- PASSES the 1e-9 gate comfortably
```

So a residual gate alone accepts it. That is what an under-constrained
structural model looks like numerically, and it is why the two gates catch
different things and neither is redundant.

**The structural free body cannot serve as this demonstration**, and the reason
is itself worth recording: its factorisation produces a **negative** pivot, so
the positivity half of the gate refuses it however the floor is set, and the
ungated behaviour is unreachable through the public API. The synthetic system
above is controllable; the structural one is doubly caught.

## And Eigen did report success on the free body

Demonstrated without a test double, from the diagnostic itself. The free-body
refusal reads:

```text
the system is singular or too ill conditioned to solve: the factorisation's
smallest pivot is 3.391787806799153e-17 of its largest, below the floor of
1e-12, and at least one pivot is not positive. The model may be
under-constrained, disconnected or a mechanism -- this layer measures the
conditioning and does not claim to know which
```

That message is produced **only after** the `info() != Success` branch was not
taken, so the factorisation succeeded. The test asserts the message is the
pivot-gate one and is *not* the exactly-zero-pivot one and *not* the
rejected-matrix one, which pins which path was taken.

## Constraint adequacy: C0, C1, C2

Brief section 100's three stages, on one fixture.

```text
stage  restraints                     Nconstrained  expected  actual    diagnostic
--------------------------------------------------------------------------------------
C0     none                                      0  FAIL      REFUSED   SingularSystem
C1     ux on one face                            4  FAIL      REFUSED   SingularSystem
C2     fixed support on one face                12  PASS      SOLVED    --
```

`Ndof = 57` throughout. C2's free system is `45 x 45` with `nnz(Kff) = 1071`,
a pivot ratio of `0.0726`, a normalized residual of `2.07e-17` and a largest
displacement of `1.07e-07` m under a 1000 N load — microns, which is what a
40 x 30 mm steel section does.

**An empty constraint set is not refused early.** `P17-BC-001` correctly allows
a model with no restraints, so the *factorisation* is what must refuse it;
reporting it earlier would be this milestone deciding a question that belongs
to the numbers. `C0` reaches the solver and is refused there.

**And an insufficiently restrained model does not return an enormous
displacement and PASS**, which is the brief's automatic failure. It is refused.

## The diagnostic does not claim to know the cause

```text
SingularSystem    "The model may be under-constrained, disconnected or a
                  mechanism -- this layer measures the conditioning and does
                  not claim to know which"
```

Brief sections 24 and 25 ask for exactly this restraint. A singular `Kff` in a
structural context usually means unremoved rigid-body modes, but it can also
mean a disconnected unrestrained region, a mechanism, or simply bad
conditioning — and distinguishing them needs a nullspace analysis this
milestone deliberately does not perform on production-size systems. There is no
separate `UnderConstrained` value, because it could not be established, and
inventing one would have been a claim the code cannot support.

For small systems the distinction *is* available and is used in the tests:
P17-ASSEMBLY-001's eigenanalysis established that the block's free `K` has
nullity exactly six, so the C0 case is known to be rigid-body deficiency — by a
measurement in the test, not by a guess in the diagnostic.

## No regularisation, no pinning, no penalty

Searched over `src/structural/StructuralSolve.cpp` with comments stripped:

```text
1e12, 1e20, penalty           0 occurrences
epsilon added to a diagonal   0
a pinned node or a fixed
  first six DOFs              0
a weak spring                 0
stabilize / regulari[sz]e     0
```

The constraint method is **exact free-system reduction**, and only that: no
row is zeroed, no diagonal is set to one, and the two methods are not combined
— brief section 48's requirement. See
[CONSTRAINT_APPLICATION.md](CONSTRAINT_APPLICATION.md).

The mutations that add an epsilon diagonal, remove the pivot gate or ignore the
positivity check are probed, and the results are in
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).
