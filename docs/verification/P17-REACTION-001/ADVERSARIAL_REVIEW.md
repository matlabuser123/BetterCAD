# P17-REACTION-001 — adversarial review

```text
RESULT: PASS
attacks:            29
credible findings:   3
production findings: 1  (a required capability P17-BC could not support; fixed)
test defects:        2  (both fixed)
remaining blockers:  0
```

## The brief's attacks

```text
#   attack                                          answer
---------------------------------------------------------------------------
1   Can the reaction sign be flipped while the      NO. The solve is untouched
    solver residual still passes?                   and its residual is
                                                    unchanged, yet M1 (`-r`)
                                                    fails 18 of 20 tests --
                                                    led by the fully
                                                    constrained case where
                                                    R = -F holds EXACTLY. That
                                                    is the demonstration brief
                                                    157 asks for
2   Can free-DOF residuals be reported as support   NO. The mask is built from
    reactions?                                      what is CONSTRAINED, so an
                                                    unconstrained component is
                                                    never read. M2 killed, and
                                                    the test MEASURES the free
                                                    residual at those rows to
                                                    show it is non-zero
3   Can a partially restrained node show reactions  NO. Mask asserted Ux-only,
    on unconstrained components?                    force.y and force.z
                                                    EXACTLY 0.0
4   Can overlapping restraints double-count one     NO. M15, M16, M17 killed,
    physical reaction?                              and the overlap fixture
                                                    asserts listed > unique
                                                    BEFORE asserting the
                                                    balance
5   Can per-restraint summaries be summed when      THEY CAN, and the sum is
    they overlap?                                   CORRECT: owned + shared is
                                                    an exact identity by
                                                    construction. The naive
                                                    sum including both shared
                                                    fields overshoots by one
                                                    copy, which is MEASURED
6   Can a support moment be mislabeled as a         NO. SupportReaction carries
    rotational reaction DOF?                        a Force3D and no moment at
                                                    all; the compile-failure
                                                    case proves asking for one
                                                    does not compile
7   Can external loads on constrained nodes be      NO. Asserted directly: the
    omitted?                                        loaded node in the sign
                                                    fixture IS fixed, and the
                                                    external total is checked
                                                    against the applied value
8   Can pressure/traction be reintegrated           NO. Zero occurrences of any
    differently from what was solved?               load physics in this file.
                                                    The external total is read
                                                    from system.force()
9   Can current loads be used against an old        NO. M19 and M20 killed. The
    solved result?                                  authority is the assembled
                                                    F, and PreparedLoads is
                                                    only the mesh check and the
                                                    oracle -- never added to it
10  Can current remeshed node coordinates be used   NO. M21 killed; `at` and
    for old reaction moments?                       `momentAbout` refuse a mesh
                                                    they do not describe
11  Can repeating numeric NodeIds after a remesh    NO. The remesh test asserts
    falsely rebind reactions?                       the new mesh's first NodeId
                                                    really is 1 again, and both
                                                    the lookup and the recovery
                                                    are refused
12  Can F x r be used instead of r x F?             NO. M12 killed by 16 tests.
                                                    A literal F x r is not even
                                                    expressible: momentOf takes
                                                    a Translation3D and a
                                                    Force3D
13  Can the moment origin remain implicit?          NO. It is a field on
                                                    MomentBalance and a
                                                    parameter with no nullary
                                                    overload
14  Can moment equilibrium be skipped because       NO. M7 killed. The gates are
    force equilibrium passes?                       sequential and independent,
                                                    and the pure couple is the
                                                    fixture that proves it
                                                    matters
15  Can a pure couple pass force and fail moment?   It passes BOTH, and the
                                                    fixture asserts the premise:
                                                    net external force ~0 with a
                                                    participating scale > 1000 N
                                                    and a nonzero external
                                                    moment
16  Can one loose tolerance hide a sign defect?     NO. M9 (loosened to 1e-3) is
                                                    killed, and the chosen 1e-12
                                                    is 40x above the worst
                                                    measured error rather than
                                                    five orders above it
17  Can the solver residual tolerance be reused?    NO. The field is ABSENT from
                                                    EquilibriumTolerance and a
                                                    compile-failure case proves
                                                    it. The two numbers are
                                                    measured side by side and
                                                    differ by an order
18  Can 1e-9 be applied blindly to an algebraic     NO -- and the measurement
    case?                                           went the other way: the
                                                    GEOMETRIC path measured
                                                    4e-16, so no looser gate is
                                                    defined at all
19  Can force normalization divide by zero?         NO. A dimensioned floor, and
                                                    the zero case returns 0
                                                    rather than dividing
20  Can moment normalization divide by zero?        NO. Same, with its own N m
                                                    floor
21  Can aggregation depend on unordered-map         NO. The multiplicity table
    traversal?                                      is a SORTED VECTOR with a
                                                    binary search. Zero
                                                    unordered containers in the
                                                    file
22  Can thread scheduling change the totals?        NO. No parallelism. Five
                                                    repeats bitwise identical
23  Can reactions become persisted authority?       NO. Nothing here serializes
24  Can zero filtered tests be reported PASS?       NO. The selection is counted
                                                    from inside each preset's
                                                    own ctest log
25  Can a stale binary or a different tree produce  NO. Freeze, no-op rebuild,
    the evidence?                                   and the eight component
                                                    hashes read before and after
```

## The attacks I added

```text
26  Can the moment code ignore the origin and       IT COULD. Finding T1
    still pass every test?
27  Can per-restraint inspection be claimed         IT COULD NOT BE BUILT AT
    without the provenance to support it?           ALL. Finding P1
28  Can a "two-support split" be asserted as        NO, and it is NOT asserted.
    analytically equal?                             Finding T2
29  Can the equilibrium gate be a no-op because     NO. The threshold is
    nothing can ever exceed it?                     tightened below the measured
                                                    error and the recovery is
                                                    refused -- so the gate is
                                                    demonstrated to fire
```

## Production finding

### P1 — per-restraint inspection was not buildable on P17-BC's provenance

Per-restraint reaction inspection is a **required** checklist item, and brief
158 makes a broken mapping a milestone failure rather than a partial pass. But
`RestraintResolution` recorded only **counts**:

```text
restraint, facets, nodes, components, degreesOfFreedom
```

and counts cannot attribute a reaction. There were two ways forward and one of
them was unsound:

```text
re-resolve each restraint's target after the solve
    UNSOUND, and brief 112 is right to forbid it: re-running the geometry
    mapping and ASSUMING it reproduces the set the solve used is precisely how
    a source mismatch hides
retain the mapping the solve actually used
    SELECTED
```

**Fix: `RestraintResolution` gained its resolved `DofIndex` list** (ADR-041,
Decision 3). The change is additive and keeps a value **already computed** —
that loop already called `numbering.indexOf` for every degree of freedom it
resolved, verified the answer, and discarded the index.

It changes already-qualified P17-BC code, so its suite was rerun before the
freeze (32/32), runs unfiltered in all three presets, and runs 5x in two.
Recorded in
[PER_RESTRAINT_AGGREGATION.md](PER_RESTRAINT_AGGREGATION.md).

It also made a stronger attribution policy possible than the brief's fallback:
unique ownership plus one shared aggregate, which is **additive by
construction** rather than additive-with-a-warning.

## Test defects

### T1 — every equilibrium case used the origin, so ignoring the origin was a no-op

Found by a surviving mutation, which is what probes are for. Removing the
`x - O` subtraction from `assembledMomentResultant` changed nothing for any
test, because they all recovered about `(0, 0, 0)`.

```text
before   16 equilibrium assertions, all about the global origin
         M13 SURVIVED
after    a new test recovers about three origins -- global, (21,-14,33) mm and
         (1.7,-2.3,0.9) m -- with two guards: the three external moments must
         DIFFER pairwise, and the far one must exceed 10x the global one
         M13 KILLED
```

The guards matter as much as the test: without them the origin could again be
doing nothing. This is the fifth time in P17 that a probe found a test gap
rather than a production defect.

### T2 — a non-vacuity floor set above the quantity it was guarding

The gravity case asserted `|M_external| > 1.0` N·m as a "this is not a
degenerate case" guard. The block weighs 22.6 N and its centroid is 35 mm off
the axis, so the moment is **0.792 N·m** — the assertion failed on a result
that was otherwise exact to the last digit (−0.792147 expected, −0.792147
measured).

Fixed by setting the floor from the physics rather than from a round number,
and by adding what the guard was actually for: the two moment components must
**differ**, which an axis swap would not reproduce.

```text
REQUIRE(|M_x| > 0.1);
REQUIRE(|M_y| > 0.1);
REQUIRE(|M_x| != |M_y|);
```

## What this review did NOT find

```text
no second reaction definition     one source: SolvedSystem::fullResidual().
                                  Zero occurrences of `stiffness()` in this
                                  module
no second load path               zero occurrences of pressure, traction,
                                  facet or area arithmetic
no second cross product           every moment goes through core's momentOf
no hidden global state            every function is stateless or takes its
                                  inputs
no behaviour that exists only
  for tests                       `reactionProblem` and
                                  `recoverSupportReactions` share one `run`
no weakened tolerance             the thresholds were chosen AFTER the
                                  measurement and are 40x above the worst
                                  case; nothing was widened to accommodate a
                                  result
no disjunction hiding a probe     every problem-value assertion is an exact
                                  equality
no partial publication            the vectors move into SupportReactions only
                                  after both gates pass
```

## An honest limitation, recorded rather than worked around

**An analytically predicted two-support split is not claimed.** Deriving
`R_A` and `R_B` from `sum F = 0` and `sum M = 0` requires a statically
determinate idealisation; a 3D continuum with two fixed faces is
indeterminate, and the symmetric variant would need a provably **symmetric
mesh**, which P16 does not guarantee for a symmetric body.

What is claimed is what holds regardless:

```text
R_A + R_B + F_external = 0        exact statics, asserted
R = -F_external                   exactly, in the fully constrained case
the split                         MEASURED and printed, never asserted equal
```

Brief 45 and 106 both warn against forcing an analytically false expectation,
and this is that warning taken seriously rather than satisfied with a fixture
that looks symmetric.
