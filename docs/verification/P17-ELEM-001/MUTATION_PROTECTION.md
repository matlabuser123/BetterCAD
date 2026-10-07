# P17-ELEM-001 — mutation protection

```text
SUBJECT:  the highest-risk FEA formulation errors, introduced deliberately, to
          establish that the suite would CATCH them rather than that it passes
PROBES:   13   (11 distinct defects, 2 of them re-run after a useless kill)
KILLED BY TESTS:            11
SURVIVED, STRUCTURALLY:      2   both no-ops; see "The two survivors"
KILLED ONLY BY THE COMPILER: 0   after the two re-runs
```

Harness: `elemmut/run.sh`, `run2.sh` and `run3.sh` in the session scratchpad.
Each probe restores the pristine source first, applies **one** literal
substitution verified to be present exactly once (an ambiguous or missing
anchor is an error, not a silent no-op), rebuilds `bettercad_tests`, runs the
29 element tests, and restores. Every run ends with a **restore build** and the
last one re-runs the suite afterwards — `post-restore: 100% tests passed out of
29` — because a restored source is not a restored binary.

The ten probes the brief names are M1–M10. M11–M13 were added because two of
those turned out to be unkillable (see below) and because the three validity
branches deserved probes of their own.

## Results

| Probe | Mutation | Verdict | Tests failed |
| --- | --- | --- | --- |
| M1 | `std::abs(detJ)` inside the validity predicate | **KILLED** | 3 of 29 |
| M2 | `J^-1` where `J^-T` belongs (index order swapped) | **KILLED** | 7 of 29 |
| M3 | `2 mu` on the `D` shear diagonal | **KILLED** | 5 of 29 |
| M4 | `gyz` and `gzx` rows of `B` swapped | **KILLED** | 3 of 29 |
| M5 | local DOF ordering switched to component-blocked | **KILLED** | 8 of 29 |
| M6 | `V` dropped from `Ke` | *compiler* | — |
| M6b | `V` dropped, `volume` still referenced so it compiles | **KILLED** | 4 of 29 |
| M7 | `std::abs(V)` scaling `Ke`, after the gate | *SURVIVED* | — |
| M8 | `Ke = 0.5 (Ke + Ke^T)` after the triple product | *SURVIVED* | — |
| M9 | `1e-3 V` added to the `Ke` diagonal | **KILLED** | 4 of 29 |
| M10 | `B`'s `gxy` row halved: tensor shear against engineering `D` | **KILLED** | 5 of 29 |
| M11 | the negative-volume branch removed | **KILLED** | 3 of 29 |
| M12 | the exactly-zero-volume branch removed | **KILLED** | 1 of 29 |
| M13 | the non-finite-coordinate check removed | *compiler* | — |
| M13b | the same check disabled by a condition that compiles | **KILLED** | 1 of 29 |

The named failures are in `elemmut/results*.txt`. Four are worth quoting
because they say which test is load-bearing:

```text
M2  J^-1 instead of J^-T
      Tet4Element_MatchesTheIndependentReferenceImplementation
      Tet4Element_HasSixRigidBodyModesAndNoMore
      Tet4Element_BGivesZeroStrainForEveryRigidBodyMode
      Tet4Element_IsCovariantUnderAProperRotation
      Tet4Element_IsEquivalentUnderAnOrientationPreservingNodePermutation
      Tet4Element_EvaluatesEveryElementOfTheMeshingReferenceModels
      ... 7 in all

M3  2mu on the D shear diagonal
      Tet4Element_EngineeringShearGivesTauEqualsMuGamma      <- written for it
      Tet4Element_ElasticityMatrixIsSymmetricAndMatchesTheLameFormulas
      Tet4Element_StiffnessMatchesTheAnalyticalUnitTetrahedron
      Tet4Element_MatchesTheIndependentReferenceImplementation
      Tet4Element_IsCovariantUnderAProperRotation

M5  component-blocked local DOFs
      Tet4Element_BRowOrderMatchesTheSharedVoigtOrdering
      Tet4Element_HasNoDimensionalAmbiguity
      Tet4Element_IsPositiveSemidefiniteAndStoresRealEnergy
      ... 8 in all

M9  diagonal regularisation
      Tet4Element_FollowsTheGeometryScaleLaw
      Tet4Element_FollowsTheMaterialScaleLaw
      Tet4Element_MatchesTheIndependentReferenceImplementation
      Tet4Element_StiffnessMatchesTheAnalyticalUnitTetrahedron
```

M2 is the transposition defect this milestone was most at risk from, and the
independent reference catches it — which is the whole reason that reference
takes a route with no Jacobian in it.

**M9 is the most informative kill.** Adding `1e-3 V` to the diagonal is *not*
caught by the rigid-body test, because `1e-3` relative sits inside that test's
own scale-aware band. It is caught by the two **scale laws** and the
independent reference. That matters: it shows the suite's coverage of
"something was added to Ke" does not rest on the one assertion a reader would
expect it to, and it is the reason the scale laws are tested at five
magnitudes rather than one.

**M12 and M13b each failed exactly one test**, which is thin — but the right
one, and by design: `Tet4Element_RejectsDegenerateAndNonFiniteGeometry` is a
single `TEST_CASE` with six sections covering coplanar, coincident, collinear,
NaN, `+Inf` and `-Inf`. One ctest entry, six independent assertions.

## The two survivors

Neither is a test gap, and neither was explained away.

### M7 — `abs(V)` after the gate is a no-op

By the time `computeTet4Stiffness` runs, its argument is a `Tet4Kinematics`:
a type with a private constructor and exactly one friend, so **possessing one
is the evidence** that the volume is positive and finite (ADR-036's idiom).
`std::abs` of a positive number is that number.

The thing M7 was aiming at *is* tested — by **M1**, which puts `std::abs` on
the determinant inside the predicate where orientation is actually decided, and
is killed by three tests. The brief's rule is "`abs` is forbidden in element
**acceptance** … using absolute magnitude after positive-orientation validation
for scale calculations may be okay, but must not mask inversion", and that is
exactly the division the two probes demonstrate: killed where it would mask an
inversion, no-op where it cannot.

M11 reinforces it from the other side: removing the orientation branch
altogether is killed by the same three tests.

### M8 — symmetrising an already-symmetric matrix is a no-op

The measured symmetry residual of `Ke` is **exactly zero** on the chosen
fixtures (`max |Ke_ij - Ke_ji| == 0` for the unit tetrahedron at both
materials). Averaging a matrix with its own transpose when the two are
bit-identical changes nothing.

So the probe's survival **is** the evidence the brief asks for in §85 — that
there is no formula error being masked, because there is nothing to mask. A
test contrived to kill M8 would be asserting that a no-op is a no-op.

Searched to confirm the production code contains no symmetrisation: the only
occurrence of `0.5 * (` in `src/structural/Tet4Element.cpp` is a comment
saying why there is none.

## Two useless kills, and what was done about them

M6 and M13 both came back `KILLED-BY-COMPILER`:

```text
M6   error: unused variable 'volume' [-Werror=unused-variable]
M13  error: 'isFinite(const Point3D&)' defined but not used [-Werror=unused-function]
```

True kills and worthless ones: they say `-Werror` noticed an unused local or
function, not that any test can see a missing volume factor or an accepted NaN.
Both were rewritten to compile — `sum + 0.0 * volume`, and a condition that is
always false but keeps `isFinite` called — and both are then killed by the
suite on its own account.

Recorded because a compiler kill is easy to accept as a pass. It answers a
different question from the one a mutation probe is asking.

## Coverage of the claims

```text
orientation is checked locally, not inherited     M1, M11
the degeneracy rule is P16's, applied here        M12
non-finite coordinates are refused                M13b
J^-T, not J^-1                                    M2
engineering shear, mu not 2mu                     M3, M10
the Voigt row order                               M4
the local DOF order matches the global one        M5
V multiplies the triple product exactly once      M6b, M9
nothing is symmetrised                            M8 (no-op, by measurement)
nothing is regularised                            M9
abs() cannot mask an inversion                    M1 kills it where it could,
                                                  M7 is a no-op where it cannot
```

Eleven claims, thirteen probes, eleven killed by tests and two no-ops with
their reasons measured rather than argued.
