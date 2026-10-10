# P17-VALID-001 — Structural Validation / Acceptance

**This milestone owns the P17 acceptance policy.** P16-QUALITY-001 ships mesh
metrics and states no opinion — `reportOnlyThresholds()` classifies nothing, and
its own header says "`P17` owns what a structural analysis needs from a mesh".
`StructuralModel::quality()` names this milestone as the owner. This is where
that debt is paid.

```text
TASK:            P17-VALID-001
RESULT:          PASS 2026-10-11
                 3759/3759 in debug-ext, release-ext and debug-shared-ext,
                 836 tests x5 in two presets, 640 objects per preset with
                 0 warnings, 28 mutation probes with 27 killed.
                 Qualified at include 34b3ec8d / src b6cddc44 / tests
                 81b34bee -- identical before the first build and after the
                 last test run.
```

## What was built

```text
NEW   include/bettercad/structural/StructuralValidation.hpp
      src/structural/StructuralValidation.cpp
      tests/structural/StructuralValidationTests.cpp            27 cases
      tests/reference/StructuralValidationSweepTests.cpp         5 cases
      tests/compile_fail/StructuralValidationMisuse.cpp          6 cases

MOD   src/structural/CMakeLists.txt        the new source, nothing else
      tests/CMakeLists.txt                 two registrations
      tests/compile_fail/CMakeLists.txt    one group, six cases

NO PREDECESSOR PRODUCTION FILE WAS MODIFIED. Unlike P17-REACTION-001, which
needed a field added to qualified P17-BC code, everything this milestone
consumes was already public.
```

Three things were genuinely missing before this milestone, and only three:

```text
1  the quality thresholds themselves      P16 deferred them by design
2  a constraint-sufficiency check          nobody owned the necessary condition
3  AN ENTRY POINT THAT RUNS THE GATES      the largest of the three
```

On (3): every gate a solve needs already existed and was correct, and
`solveStructuralSystem` had **no production caller**. The safety was available
rather than enforced — the same shape as the limitation P16 recorded for meshes
("nothing FORCES the holder of a mesh to ask whether it is stale") one layer up.
`solveStructuralAnalysis` is now the only production path from a `Document` to a
`StructuralResult`, and it validates first.

## The policy, in one block

```text
TWO HARD-FAIL QUALITY BOUNDS, one per degeneration family, both DERIVED

    3r/R   < 1e-10   ->  REFUSED      flattening family
    aspect > 3e5     ->  REFUSED      stretching family

FOUR WARNING BOUNDS, the measured edge of the qualified envelope

    3r/R 3.0e-4   aspect 81.0   minDih 6.0e-3 rad   maxDih 3.125 rad

NO HARD-FAIL BOUND ON EITHER DIHEDRAL ANGLE
NO AUTOMATIC REPAIR, REMESH OR TOLERANCE LOOSENING ANYWHERE
```

Neither failure bound is chosen. Each is the inverse of a **measured** accuracy
law at BetterCAD's documented 1e-9 band for geometric accumulation:

```text
FLATTENING   err <= C / sqrt(3r/R)   C <= 2.41e-15   16 orders, 2 fields
STRETCHING   err <= C * aspect       C <= 4.45e-16    7 orders, 2 fields
```

Both sit three to six orders clear of the worst element of any qualified P16
reference mesh, so neither refuses anything BetterCAD produces — and a
**0.198-degree sliver passes both**, correctly, because its measured
recovered-strain error is 1.33e-13.

## Evidence

| document | what it holds |
| --- | --- |
| [QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md) | the measured sweep over every qualified reference model and the pathological fixtures, run BEFORE any policy existed |
| [ACCURACY_LAW.md](ACCURACY_LAW.md) | the two measured laws and the two derivations, each verified on both sides |
| [ACCEPTANCE_POLICY.md](ACCEPTANCE_POLICY.md) | the policy stated once, with every checkbox answered and its owner named |
| [SOLVE_ENTRY.md](SOLVE_ENTRY.md) | the orchestration, and the honest limit of "cannot be bypassed" |
| [DETERMINISM.md](DETERMINISM.md) | what could have been non-deterministic and what is used instead |
| [ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) | **three credible defects found and fixed before `[x]`** |
| [PRIOR_DRAFT_CORRECTION.md](PRIOR_DRAFT_CORRECTION.md) | two claims this milestone made and then disproved |
| [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md) | 28 probes, 27 killed, 1 unreachable |
| [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) | eight limitations carried rather than closed |
| [FREEZE.md](FREEZE.md) | the qualified tree and the three-preset run |

Decision: [ADR-042](../../architecture/decisions/ADR-042-mesh-quality-rejects-on-measured-accuracy-laws-one-bound-per-degeneration-family-and-one-unbypassable-solve-entry.md).

Raw logs: [logs/](logs/).

## The measurement came first, and it was not what was expected

The brief forbids choosing a threshold from intuition. So two sweeps were
written and run before any policy existed, and the policy was written from what
they printed. **The sweep confirmed one conclusion and refuted two**, and a
later code review refuted a third — all three corrections are recorded in
[PRIOR_DRAFT_CORRECTION.md](PRIOR_DRAFT_CORRECTION.md) rather than quietly
overwritten.

```text
CONFIRMED, and more strongly than expected
    "no shape metric separates good meshes from bad". A QUALIFIED CYLINDER
    (3r/R 0.00124845, minimum dihedral 0.362683 deg) is worse than a
    deliberate 1.98-degree sliver (0.00089946, 1.98399 deg). On the aspect
    ratio the two sets are INVERTED: a qualified thin plate scores 80.0 while
    the worst measurable sliver scores 1.73.

REFUTED 1 -- "so no hard-fail threshold is derivable"
    That followed from a second claim, that Tet4 reproduces a linear field
    exactly at every shape, which is true in exact arithmetic and false in
    double precision. Measured, the recovered-strain error moves through
    ELEVEN orders of magnitude, and at the bottom of the range P16 still
    reports the mesh valid while the strain is wrong by 80 ppm.

REFUTED 2 -- "so one bound on the radius ratio is enough"
    Refuted by the adversarial review asking whether the fitted constant
    belonged to the kernel or to the one fixture family that measured it. It
    belonged to the family. A needle at 3r/R 1.73e-03 errs 4.45e-13 while a
    wedge at a WORSE 9.0e-04 errs 1.17e-14 -- 38x less. The one-bound policy
    accepted a needle whose recovered strain was wrong by 2.5e-09, outside
    its own stated guarantee.

REFUTED 3 -- "a contradictory quality policy should refuse the solve"
    Found by re-reading the staged chain and asking what `model->quality()`
    actually returns: the MESHER's report, produced under the MESH CONTROL's
    own thresholds. A `MeshControl` carries them, so a user's broken
    REPORTING preferences would have blocked a structurally sound solve --
    while P16 still measures every metric in that case and this module reads
    the measurements. Downgraded to a warning.

    THIS ONE COST A QUALIFICATION. It was found 30 minutes into the
    three-preset run, which was stopped and voided because source changed
    after the freeze. The alternative was shipping a known false rejection.
```

## Tests

```text
27  unit         tests/structural/StructuralValidationTests.cpp
 5  reference    tests/reference/StructuralValidationSweepTests.cpp
 6  compile-fail tests/compile_fail/StructuralValidationMisuse.cpp
--
38  total, verified by counting TEST_CASE in each file AND by the 32 that
    `ctest -R StructuralValidation_` selects -- the compile-failure cases are
    NOT in that filter and run unfiltered
```

The ones that carry the most weight:

```text
AcceptsEveryQualifiedReferenceMesh
    all seven qualified P16 reference models come back `Accepted` -- not
    merely acceptable, but with NO warnings either -- and each clears the
    accuracy floor by at least five orders. A policy that refused one of
    these would be refusing BetterCAD's own output.

TheEnvelopeBoundsReallyAreTheMeasuredWorstCase
    each warning bound is within a FACTOR OF TWO of the measured worst
    qualified value (1.01x to 1.08x). Without this a bound could be loosened
    by orders and every other test would still pass, with the warnings
    quietly meaning nothing.

MeasuresHowKernelAccuracyDegradesWithElementShape
    both laws, over sixteen and seven orders, two fields each, PINNED as
    bands -- and the flattening law asserted to FAIL on the needle family,
    so the two-family finding cannot regress into one law.

AcceptsASliverBecauseTheKernelHandlesIt
    a 0.198-degree sliver is ACCEPTED, with both premises asserted: it really
    is a sliver, and the aspect ratio really cannot see it.

RefusesAStaleMeshThroughThePublicEntry
    the gap P16 recorded and could not close. All three premises asserted --
    the mesh is still held, still valid, still handed out -- then refused as
    `MeshStale`, and the solve publishes nothing.

DoesNotClaimSixDegreesOfFreedomIsSufficient
    validation ACCEPTS a model restrained in z on three faces, and the
    FACTORISATION refuses it. The asymmetry in one test, because either half
    alone reads as the rule the brief forbids.

ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide
    the exact boundary, which no measured mesh can pose, so the report is
    synthesised -- on the bound and at `nextafter` past it, for both
    directions and both severities.

PublishesNothingWhenAGateRefuses
    the residual and equilibrium gates, each reached by TIGHTENING it below
    its measured value, each producing no result at all.
```

## Validation

```text
INDEPENDENT ORACLE       the analytic gradient of a linear displacement
                         field, written down by hand. Nothing in it calls
                         `B`, `strainFrom` or any kinematics.

INDEPENDENT CONFIRMATION the wedge table REPRODUCES P16-QUALITY-001's
                         published figures at the three apex heights it
                         recorded (0.977 / 0.0849 / 0.000899, aspect
                         1.1547 / 1.7066 / 1.7318), and two independent
                         cylinder measurements agree within a factor of
                         three.

AGREEMENT                `AgreesWithP16sOwnPerElementClassification` runs the
                         same meshes through P16's per-element classifier
                         under these thresholds and requires the same
                         verdict as the summary read -- two code paths, one
                         answer, asserted rather than assumed.

NO SECOND DEFINITION     grep for a radius-ratio, aspect-ratio, dihedral or
                         cross-product formula in this milestone's
                         production files, comments stripped, finds NOTHING.
                         One definition of every metric exists and it is in
                         `src/meshing/MeshQuality.cpp`.
```

## RESULT

**PASS**, 2026-10-11. The qualified tree, the stage-by-stage timings, the
voided first attempt and the full acceptance gate are in
[FREEZE.md](FREEZE.md).
