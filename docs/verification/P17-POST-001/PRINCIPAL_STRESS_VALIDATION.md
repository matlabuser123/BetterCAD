# P17-POST-001 — principal stress and principal strain validation

```text
RESULT: PASS
```

## The algorithm, and what was rejected

```text
SELECTED       Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d>, DEFAULT path
               (tridiagonal reduction + QL iteration)
ORDERING       BetterCAD's: sigma1 >= sigma2 >= sigma3, sorted AFTER the solve
EIGENVECTORS   none computed
UNIT           Pa for stress, dimensionless for strain
```

Eigen is already a PRIVATE link of this module (ADR-039), so this adds no
dependency and no licence question.

```text
rejected                     why
---------------------------------------------------------------------------
computeDirect()              Eigen's own documentation describes it as faster
                             and LESS ACCURATE for 3x3. Nothing here is on a
                             hot path that would justify the trade
a hand-written cubic         the "fragile cubic root solver" the brief asks
  (Cardano) in production    for a recorded reason before writing. There is
                             no such reason -- but it IS used as a test
                             ORACLE, where a different algorithm is exactly
                             what is wanted
the library's own order      not relied on at all. Eigen documents ascending
                             eigenvalues; sorting descending here means no
                             behaviour depends on that, and a probe that
                             reverses the comparator is killed rather than
                             invisible
eigenvectors                 not computed: not needed for a magnitude, their
                             sign is arbitrary, and they would be an output
                             nothing could verify
```

One eigensolve exists in the file, shared by stress and strain, so the two
cannot use different paths or different orderings. Only the quantity's name in
the diagnostic differs.

## The reference table

```text
case                     input (Voigt, MPa)              expected          actual     error        PASS
---------------------------------------------------------------------------------------------------------
diagonal, GIVEN          (10, 20, 30, 0, 0, 0)           30, 20, 10        matches    < 1e-12 rel  yes
  ASCENDING
hydrostatic tension      (75, 75, 75, 0, 0, 0)           75, 75, 75        matches    < 1e-12 rel  yes
hydrostatic compression   (-100,-100,-100, 0, 0, 0)      -100,-100,-100     matches    < 1e-12 rel  yes
axisymmetric             (40, 40, 90, 0, 0, 0)           90, 40, 40        matches    < 1e-12 rel  yes
zero                     (0, 0, 0, 0, 0, 0)              0, 0, 0           EXACT      exact        yes
pure shear XY            (0,0,0, 60, 0, 0)               +60, 0, -60       matches    < 1e-10 rel  yes
pure shear YZ            (0,0,0, 0, 60, 0)               +60, 0, -60       matches    < 1e-10 rel  yes
pure shear ZX            (0,0,0, 0, 0, 60)               +60, 0, -60       matches    < 1e-10 rel  yes
pure shear XY, negative  (0,0,0, -60, 0, 0)              +60, 0, -60       matches    < 1e-10 rel  yes
mixed with shear         (100, 20, -50, 15, 0, 0)        Cardano           matches    < 1e-10 rel  yes
FULL 3D                  (120, -35, 55, 18, -27, 41)     Cardano           matches    < 1e-10 rel  yes
large finite             (1e6, -4e5, 6e5, 2e5,          finite, sorted     matches    finite       yes
                          -3e5, 5e5) x 1e6
```

### The diagonal case is GIVEN ASCENDING on purpose

`diag(10, 20, 30)` must come back `30, 20, 10`. Eigen documents ascending
output, so an implementation that passed the library's order through would
return `10, 20, 30` and fail. That is the whole content of the ordering claim,
and it is why the input is not written in the expected order.

### Pure shear fixes the off-diagonal magnitude

A pure shear state has principal stresses `+|tau|, 0, -|tau|`. If the
Voigt-to-tensor mapping halved the off-diagonal, the answer would be
`+tau/2, 0, -tau/2`. All three shear components in turn, because the sixth is
the one that gets misplaced; and the negative case, because the sign of a shear
lives in the choice of axes rather than in the principal magnitudes.

### The full-3D case uses Cardano, which is a different ALGORITHM

```text
j2 = (1/2) s:s  and  j3 = det(s)  of the deviator
theta = acos( j3 / (2 (j2/3)^{3/2}) ) / 3
sigma_i = mean + 2 sqrt(j2/3) cos(theta - 2 pi k / 3)
```

Written out in the test. Eigen reduces to tridiagonal form and iterates QL;
this solves the characteristic cubic in closed form through the invariants. So
agreement is evidence rather than a second call to the same code.

## The invariant identities

Measured on the full 3D state `(120, -35, 55, 18, -27, 41)` MPa:

```text
identity                                         bound       PASS
---------------------------------------------------------------------
sigma1 + sigma2 + sigma3  ==  tr(S)              1e-12 rel   yes
sigma_h                   ==  tr(S) / 3          1e-12 rel   yes
sigma_h                   ==  sum(principal)/3   1e-11 rel   yes
vm (components)           ==  vm (principal)     1e-10 rel   yes
```

`tr(S) = 120 - 35 + 55 = 140 MPa`, so `sigma_h = 46.6667 MPa`. The trace
identity is the mandatory principal-stress consistency check, and it is a real
one: it ties the eigensolver's three outputs to a quantity computed without it.

The determinant and second-invariant checks of the brief's sections 86 and 87
are **not** separately implemented. The trace identity plus Cardano's
closed-form agreement already pin all three roots — Cardano is *built from*
`j2` and `j3`, so agreeing with it is agreeing on the second and third
invariants. Adding `det(S) == sigma1 sigma2 sigma3` would restate that, and the
brief marks both as supplemental rather than required.

## Shift and rotation

### Adding `q I` shifts every principal stress by exactly `q`

```text
base                 (120, -35, 55, 18, -27, 41) MPa
shift                q = +250 MPa
sigma_i' - sigma_i   q, each of the three, to 1e-11 relative
ordering             still descending afterwards, asserted
```

This validates the tensor assembly and the eigensolve together: a mislaid
off-diagonal changes the eigenvalues in a way no constant shift could
reproduce.

### A proper rotation leaves all three unchanged

```text
R                    axis (0.3, -0.7, 0.65), angle 0.9 rad
S' = R S R^T         built with explicit 3x3 arithmetic in the test, not
                     through a production type
sigma1, sigma2, sigma3    unchanged to 1e-10 relative
von Mises                 unchanged to 1e-10
hydrostatic stress        unchanged to 1e-10
trace                     unchanged to 1e-10

raw components       CHANGE, and the test REQUIRES two of them to move by
                     more than 1 MPa -- otherwise every invariance assertion
                     would hold trivially on an identity rotation
```

## Degenerate and extreme states

```text
state                         requirement                       PASS
-------------------------------------------------------------------------
hydrostatic (triple root)     three equal values, finite        yes
axisymmetric (double root)    two equal, one distinct           yes
zero                          three exact zeros                 yes
~1e-20 Pa                     finite, and vm >= 0 with no NaN   yes
~1e12 Pa                      finite, sorted, vm agrees with
                              the deviatoric oracle             yes
```

A repeated eigenvalue is an **equality**, not a special case: there is no
branch for it, and no arbitrary axis labelling, because no eigenvectors are
produced.

## Over a real solved field

On the solved block fixture, for **every** element:

```text
sigma1 >= sigma2 >= sigma3                            PASS, all 58
e1 >= e2 >= e3                                        PASS, all 58
sum(principal stresses) == trace                      < 1e-8 rel
sigma_h == sum(principal)/3                           < 1e-8 rel
vm == principal form                                  < 1e-8 rel
every principal value finite                          PASS
```

And on RM-MESH-02 at 4210 elements, `sigma1 >= sigma3` and finiteness hold on
every one.

## Principal strain

Implemented, through `StrainTensor3` — so the `gamma/2` has already been
applied before the eigensolve.

```text
case                       input (Voigt)            expected              PASS
-----------------------------------------------------------------------------------
pure shear gxy = 6e-4      (0,0,0, 6e-4, 0, 0)      +3e-4, 0, -3e-4       yes
pure shear gyz = 6e-4      (0,0,0, 0, 6e-4, 0)      +3e-4, 0, -3e-4       yes
pure shear gzx = 6e-4      (0,0,0, 0, 0, 6e-4)      +3e-4, 0, -3e-4       yes
diagonal                   (1e-4, 5e-4, 3e-4,...)   5e-4, 3e-4, 1e-4      yes
mixed with all shears      (2e-4,-5e-4,7e-4,
                            3e-4,-1e-4,4e-4)        sum == tr(E)          yes
```

**The pure-shear case is the sharpest measurement in the milestone of the
engineering-shear convention.** A tensor carrying the full `gamma` in its
off-diagonals would give `+6e-4, 0, -6e-4` — exactly double — and the normal
components would stay correct, so nothing else would notice. The test asserts
`e1 != gamma` explicitly as well as `e1 == gamma/2`.

The middle value is asserted to `1e-18` absolute rather than relatively,
because its exact value is zero and a relative bound against zero is
meaningless.

## Finiteness and failure

```text
requirement                                              how
---------------------------------------------------------------------------
all three values finite, or FAIL                         checked after the
                                                         solve; a finite
                                                         symmetric tensor
                                                         cannot produce a
                                                         non-finite root, so
                                                         it is a RESULT check
                                                         and not an invented
                                                         input tolerance
no partial result                                        Result<PrincipalStresses>
                                                         -- either three sorted
                                                         finite values or a
                                                         diagnostic
eigensolve status consulted                              solver.info() must be
                                                         Success. M15 (status
                                                         ignored) is a probe
the base stress is checked FIRST                         so a NaN stress is
                                                         reported as a
                                                         NON-FINITE STRESS and
                                                         not as "principal
                                                         value failure" --
                                                         the root cause brief
                                                         section 58 asks to be
                                                         preserved
```

## Mutation protection

```text
probe                                   result
-----------------------------------------------------------
M12 the descending sort removed         KILLED
M13 sorted ascending instead            KILLED
M14 non-finite principal accepted       KILLED
M15 eigensolve status ignored           KILLED or recorded
M16 the ZX component zeroed before
    the eigensolve                      KILLED
```

See [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md) for the full set and for
the probes that could not be expressed.
