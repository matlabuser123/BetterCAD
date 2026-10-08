# P17-SOLVE-001 — mutation protection

```text
14 probes applied, 14 KILLED, 0 SURVIVED
```

The residual gate is first because brief section 134 makes it a **required
gate** rather than optional diagnostics, and the evidence it asks for is
exactly this: proof that tests fail if the gate is removed or bypassed.

## Method

Each probe restores the two production files from hash-verified pristine
copies, applies **one** verified literal substitution (the harness dies if the
anchor is missing or ambiguous), rebuilds, runs the solve selection, and
restores. The run ends with a restore build, and the harness distinguishes a
collided build from a real compiler kill by looking for `ld returned` — the
signature that cost P17-ASSEMBLY-001 a void result.

```text
filter      StructuralSolve_      24 tests, counted with ctest -N first
                                  (25 after the fixture added below)
harness     .../scratchpad/solvemut/run.sh
logs        results.txt, results-m11.txt, results-m11b.txt
```

Nothing touched the build root while it ran.

## Results

```text
#    Mutation                                        Result   Killed by
--------------------------------------------------------------------------
M1   the independent residual gate removed           KILLED   1 of 24
M2   the normalized residual forced to zero          KILLED   1 of 24
M3   the pivot gate removed                          KILLED   7 of 24
M4   the pivot positivity check ignored              KILLED   1 of 24
M5   1e-6 added to the diagonal                      KILLED   6 of 24
M6   the residual taken over the FULL system          KILLED  12 of 24
M7   constrained DOFs initialised to 1e-9            KILLED   4 of 24
M8   the free-equation mapping shifted by one        KILLED   4 of 24
M9   the Kfc columns kept in the reduced rows        KILLED  14 of 24
M10  the reduced force taken from the wrong row      KILLED   2 of 24
M11  a non-finite solution accepted                  KILLED   1 of 25
                                                     (after a fixture and a
                                                     precise assertion; see
                                                     below)
M12  the mesh pairing not checked                     KILLED   1 of 24
M13  the settings not validated                       KILLED   1 of 24
M14  the zero-row short-circuit removed               KILLED   1 of 24
```

## The gates the brief names

```text
M1   if (!(normalized <= tolerance))  ->  if (false && ...)
     THE REQUIRED GATE OF BRIEF SECTION 134, removed. Killed by
     StructuralSolve_ReportsEveryProblemThroughTheSameOrderedChecks, whose
     impossible-threshold section is the adversarial path of section 38: the
     solver reports success and BetterCAD must still refuse. With the gate
     gone, it does not.

M2   normalized = euclidean / denominator  ->  normalized = 0.0
     THE SAME GATE BYPASSED RATHER THAN REMOVED, which section 134 asks for
     separately. A residual that always reports zero passes every threshold.
     Killed.

M6   the kernel called with the GLOBAL system instead of the reduced one
     TWELVE KILLS, the second largest in the set. This is the trap the brief
     calls the most important: `K u - F` is non-zero at the constrained
     degrees of freedom because those entries are the support REACTIONS, so a
     solver that checked the full residual would fail every correct solution
     of a restrained model. It does, loudly.

M3   the pivot gate removed
     Seven kills. The free body, the insufficiently restrained body, the
     indefinite matrix, the exactly singular matrix and the near-singular
     demonstration all stop being refused.

M4   the positivity half of the pivot gate ignored
     One kill: the indefinite 2x2, whose negative pivot is the only thing that
     distinguishes it. Worth keeping separate from M3 -- the two halves of the
     gate catch different matrices.

M5   1e-6 added to the diagonal before factorising
     Six kills. The brief's automatic failure "epsilon diagonal is added to
     make a free body nonsingular", and the rigid-mode-deficient fixtures are
     what notice.

M9   the constrained columns kept in the reduced rows
     FOURTEEN KILLS, the largest. Keeping `Kfc` makes the reduced matrix
     non-square in content -- its column indices run past `Nfree` -- so the
     dimension check, the extraction comparison, every solve and the residual
     all fail.
```

## The one that needed work

### M11 — a non-finite solution accepted

The probe disables the finiteness check on `uf`. It **survived twice**, for two
different reasons, and both are worth recording.

**First survival: the branch was unreachable.** No fixture in the file produced
a non-finite solution. A solve that gets as far as back-substitution has a
finite system and a conditioned matrix, so overflow does not happen by
accident.

The branch was unreachable, not untestable, and a fixture was added that
reaches it:

```text
    [ 1  0      ] [x1]   [ 1     ]
    [ 0  1e-300 ] [x2] = [ 1e300 ]
```

Finite input, both pivots positive, so the factorisation succeeds — and
`x2 = 1e300 / 1e-300 = 1e600`, which overflows a double to infinity. The pivot
gate catches it at the default floor (the ratio is `1e-300`), so the floor is
relaxed to reach the gate under test — which is the two gates being
independent, demonstrated by having to disable one to exercise the other.

**Second survival: my assertion was too weak.** The first version of the new
test allowed either `NonFiniteSolution` or `NonFiniteResidual`, reasoning that
"which operation overflows first is not a BetterCAD contract". That was wrong:
the **check order is documented in the header**, so an infinity in `uf` must be
reported as the solution being non-finite. The disjunction let the mutation
survive, because with the solution check disabled the residual check catches
the same infinity and reports the other value.

Tightened to assert `NonFiniteSolution` exactly. M11 is now **killed**.

This is the third time in P17 that an assertion weakened for safety lost its
power — P17-BC-001's D4 and P17-ASSEMBLY-001's D5 were the same shape — and it
is the clearest argument in this milestone for probing rather than reading.

## What the probes could not express

```text
"use solver.error() instead of the independent residual"
    not expressible: there is no solver.error() call to substitute FOR. The
    only Eigen members used anywhere in the file are compute, info, solve and
    vectorD, which a grep confirms. Writing one in would be adding code, not
    mutating it -- and M1 and M2 already remove and bypass the gate it would
    replace

"pin an arbitrary DOF" / "apply a huge penalty"
    not expressible for the same reason: there is no pinning and no penalty to
    disable. M5 is the closest expressible form -- adding a regularisation --
    and it is killed by six tests

"reuse a reduced factorisation after a restraint change"
    not expressible: there is no factorisation cache. The milestone
    deliberately has none, so there is nothing to invalidate incorrectly

"accept NoConvergence"
    not applicable: the solver is direct, so there is no convergence status
    and no iteration count. Recorded as N/A rather than invented

"expose the internal solver permutation as the displacement order"
    M8 is the expressible form -- shifting the free-equation mapping -- and it
    is killed by four tests. Substituting Eigen's own internal permutation
    array is not reachable: SimplicialLDLT does not expose it, which is itself
    why the permutation cannot leak
```

And two are covered under different names:

```text
"forget one constrained DOF"                  M7, M9
"swap free-equation mapping"                  M8
"reconstruct constrained displacement from
 uninitialised memory"                        M7 (the vector is allocated
                                              zero-filled; the probe fills it
                                              with 1e-9 instead)
"ignore mesh/source mismatch"                 M12
"compute residual on full constrained system
 and demand zero"                             M6
"skip residual validation"                    M1, M2
"add epsilon diagonal"                        M5
"accept solver status despite NaN u"          M11
```

## Restoration verified

```text
source hashes after the run match pristine, both files
    src/structural/StructuralSolve.cpp              5ffa41a8
    include/bettercad/structural/StructuralSolve.hpp 665a41fe
restore build OK
100% tests passed out of 25, 75.91 s
```

The changes made after the probes were the fixture and the assertion described
under M11, both in the test file; M11 was re-run against each, and the full
three-preset qualification ran after both.
