# P17-POST-001 — von Mises validation

```text
RESULT: PASS
```

The brief's closing note names this as the most important check in the
milestone, and it is right to: **almost every reasonable test passes with a
plane-stress formula.** A 2D von Mises drops `szz`, `tyz` and `tzx`, and it
agrees with the correct answer on

```text
every uniaxial state along x or y
every pure-XY-shear state
every state where the out-of-plane terms happen to vanish
```

which is most of what a test author writes first. So the discriminating
fixture is the whole point of this file.

## The production formula

```text
sigma_vm = sqrt( 0.5 ( (sxx-syy)^2 + (syy-szz)^2 + (szz-sxx)^2 )
                 + 3 ( txy^2 + tyz^2 + tzx^2 ) )
```

All six components. **Non-negative by construction**: the radicand is a sum of
squares with positive coefficients, so for finite input it cannot be negative.
There is no `std::abs` and no clamped radicand anywhere -- a repaired radicand
would hide a formula error rather than a rounding one.

`noexcept` and total, returning `Stress`. The finiteness gate is upstream, in
the element kernel, so a non-finite stress is reported as a **non-finite
stress** rather than as "von Mises failed" -- the root cause the brief's
section 58 asks to be preserved.

## The mandatory full-3D fixture

```text
sxx = +120 MPa      txy = +18 MPa
syy =  -35 MPa      tyz = -27 MPa
szz =  +55 MPa      tzx = +41 MPa
```

All three of the components a 2D formula drops are **nonzero**, and the test
asserts that they are before comparing anything.

```text
route                                      value (Pa)      relative difference
---------------------------------------------------------------------------------
production, component formula              1.62410e+08     --
independent oracle, deviatoric invariant   1.62410e+08     within 1e-12
                sqrt(3/2 s:s)
independent oracle, principal-stress form   1.62410e+08    within 1e-10
                from production principals
independent oracle, principal-stress form   1.62410e+08    within 1e-10
                from CARDANO's principals
PLANE-STRESS formula, for contrast         1.44212e+08     0.112052
```

**The last row is the proof the fixture discriminates**, and it is an
assertion rather than a remark: the test computes the common plane-stress form

```text
sqrt( sxx^2 - sxx syy + syy^2 + 3 txy^2 )
```

and requires the relative difference to exceed 5%. Measured: **11.2%**. Without
that assertion the fixture could have passed a 2D implementation by
coincidence and nothing would have said so.

### The three oracles are genuinely independent

```text
deviatoric invariant    s = S - tr(S)/3 I, then sqrt(3/2 s:s). A different
                        algebraic route, and it has its OWN convention trap --
                        the double contraction counts each off-diagonal TWICE
                        -- so agreement is evidence rather than rearrangement
principal-stress form   sqrt( ((s1-s2)^2 + (s2-s3)^2 + (s3-s1)^2) / 2 )
Cardano's solution      the characteristic cubic solved in closed form through
                        the invariants, written out in the test. Eigen reduces
                        to tridiagonal form and iterates QL; this does neither,
                        so it is a second ALGORITHM and not a second call
```

Production uses the component formula and no test oracle shares a line with
it.

## The reference table

```text
case                        expected          production      oracle          error        PASS
------------------------------------------------------------------------------------------------
zero stress                 0                0               0               exact        yes
hydrostatic +1 kPa          0                0               0               < 1e-3 Pa    yes
hydrostatic -100 MPa        0                0               0               < 1e-3 Pa    yes
hydrostatic +500 MPa        0                0               0               < 1e-3 Pa    yes
hydrostatic -1 GPa          0                0               0               < 1e-3 Pa    yes
uniaxial sxx = 250 MPa      250 MPa          250 MPa         --              < 1e-12 rel  yes
uniaxial syy = 250 MPa      250 MPa          250 MPa         --              < 1e-12 rel  yes
uniaxial szz = 250 MPa      250 MPa          250 MPa         --              < 1e-12 rel  yes
uniaxial sxx = -250 MPa     250 MPa          250 MPa         --              < 1e-12 rel  yes
pure shear txy = 80 MPa     sqrt(3)*80 MPa   138.564 MPa     --              < 1e-12 rel  yes
pure shear tyz = 80 MPa     sqrt(3)*80 MPa   138.564 MPa     --              < 1e-12 rel  yes
pure shear tzx = 80 MPa     sqrt(3)*80 MPa   138.564 MPa     --              < 1e-12 rel  yes
pure shear tyz = -80 MPa    sqrt(3)*80 MPa   138.564 MPa     --              < 1e-12 rel  yes
FULL 3D (above)             --               162.410 MPa     162.410 MPa     < 1e-12 rel  yes
tiny finite (~1e-20 Pa)     --               finite, >= 0    agrees          < 1e-10 rel  yes
large finite (~1e12 Pa)     --               finite, > 0     agrees          < 1e-12 rel  yes
```

### `uniaxial szz` is the cheapest possible 2D detector

A plane-stress formula gives **exactly zero** for a state whose only nonzero
component is `szz`. One assertion, and it catches the whole class.

### Pure shear fixes the coefficient

`vm = sqrt(3)|tau|`, not `|tau|` and not `2|tau|`. All three shear components in
turn, because `tyz` and `tzx` are precisely what a 2D formula omits. The test
also asserts `sqrt(3) * tau != tau`, so the comparison cannot be vacuous.

## Hydrostatic-shift invariance

Von Mises depends on the deviator alone, so adding `q I` for **any** finite `q`
must not move it. This is the strongest single property available, because it
holds for every `q` rather than at a point.

```text
q (Pa)        vm(S + qI)      vm(S)           relative change   sigma_h moved by q?
------------------------------------------------------------------------------------
-1.0e9        1.62410e+08     1.62410e+08     < 1e-11           yes, asserted
-2.5e8        1.62410e+08     1.62410e+08     < 1e-11           yes, asserted
+1.0e3        1.62410e+08     1.62410e+08     < 1e-11           yes, asserted
+7.5e8        1.62410e+08     1.62410e+08     < 1e-11           yes, asserted
+2.0e9        1.62410e+08     1.62410e+08     < 1e-11           yes, asserted
```

**Both halves are asserted.** Von Mises must not move AND the hydrostatic
stress must move by exactly `q`. Checking only the first would pass an
implementation that ignored the normal components altogether.

## Rotation invariance

Under a proper rotation `S' = R S R^T`, built in the test with explicit 3x3
arithmetic rather than through a production type:

```text
quantity               before          after           relative change
------------------------------------------------------------------------
von Mises              1.62410e+08     1.62410e+08     < 1e-10
sigma1                 measured        unchanged       < 1e-10
sigma2                 measured        unchanged       < 1e-10
sigma3                 measured        unchanged       < 1e-10
hydrostatic stress     4.66667e+07     unchanged       < 1e-10
trace                  1.40000e+08     unchanged       < 1e-10

raw components         CHANGE, and the test REQUIRES they change by more
                       than 1 MPa -- otherwise the whole invariance claim
                       would hold trivially on an identity rotation
```

Axis `(0.3, -0.7, 0.65)`, angle 0.9 rad.

## Over a real solved field

Not only on written-down tensors. On the solved block fixture, **every element**
is cross-checked against the deviatoric oracle and the principal-stress form:

```text
elements                                        58
with all of szz, tyz and tzx nonzero            58  (asserted > half)
worst |vm - deviatoric| / deviatoric            < 1e-9
worst |vm - principal form| / vm                < 1e-8
worst |sum(principals) - trace| / trace         < 1e-8
worst |sigma_h - sum(principals)/3| / sigma_h   < 1e-8
```

And on RM-MESH-02 at 4210 elements, the deviatoric cross-check holds to
`< 1e-9` on every one.

**The count of fully-3D elements is asserted**, because agreement measured on a
degenerate set would prove nothing: a solved structural field really does
produce stress states with all six components populated, and the test requires
that more than half of them do.

## Mutation protection

```text
probe                                        result
--------------------------------------------------------------------
plane-stress formula substituted             KILLED
szz omitted                                  KILLED, 11 of 41 tests
tyz omitted                                  KILLED,  8 of 41 tests
tzx omitted                                  KILLED,  9 of 41 tests
shear coefficient 3 -> 1                     KILLED,  9 of 41 tests
hydrostatic stress allowed to leak in
  (sxx - syy  ->  sxx + syy)                 KILLED
```

See [MUTATION_PROTECTION.md](MUTATION_PROTECTION.md) for the whole set.

## What is NOT claimed

```text
continuum accuracy of the stress FIELD      not claimed. A constant-strain Tet4
                                            is stiffer than the continuum and
                                            its stress peaks grow with
                                            refinement; P17-REFMOD-001 owns
                                            convergence
a yield or safety verdict                   not computed. P17-POST produces
                                            numbers and decides nothing
bitwise agreement between presets           not claimed. The properties are
                                            asserted in each preset
                                            independently
```
