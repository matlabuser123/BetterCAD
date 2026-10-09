# P17-POST-001 — independent analytical validation

```text
RESULT: PASS
```

Every oracle in this file is **independent of production**: an analytic
derivative, a continuum relation, a second constitutive law written out in the
test, or a different algorithm. Nothing asks the production API what the answer
should be.

## 1. The affine field, on one tetrahedron

The mandatory case, and the one that isolates post-processing from the solver
entirely: a displacement field is written down, sampled at four corners, and
pushed through **the production per-element kernel** — the same
`recoverOneElement` that `recoverFields` calls once per tetrahedron.

```text
ux = a0 + a1 x + a2 y + a3 z        a = ( 1.0e-6,  3.0e-4, -7.0e-4,  2.0e-4)
uy = b0 + b1 x + b2 y + b3 z        b = (-2.0e-6,  5.0e-4,  1.1e-3, -4.0e-4)
uz = c0 + c1 x + c2 y + c3 z        c = ( 4.0e-6, -6.0e-4,  9.0e-4,  1.3e-3)
```

The tetrahedron is deliberately **irregular and off-origin**, in metres:

```text
n0  ( 0.011, -0.004,  0.021)
n1  ( 0.034,  0.002,  0.019)
n2  ( 0.015,  0.027,  0.023)
n3  ( 0.018,  0.001,  0.046)
```

Not the unit tetrahedron: a `B` whose rows were permuted or whose gradients
were transposed could survive on a symmetric element.

### Strain, against the analytic derivative

```text
component   analytical           recovered            relative error   PASS
-------------------------------------------------------------------------------
exx         a1       =  3.0e-4   3.0e-4               < 1e-10          yes
eyy         b2       =  1.1e-3   1.1e-3               < 1e-10          yes
ezz         c3       =  1.3e-3   1.3e-3               < 1e-10          yes
gxy         a2 + b1  = -2.0e-4  -2.0e-4               < 1e-10          yes
gyz         b3 + c2  =  5.0e-4   5.0e-4               < 1e-10          yes
gzx         c1 + a3  = -4.0e-4  -4.0e-4               < 1e-10          yes
```

The expected values come from differentiating the field by hand. **Nothing
calls `B` to produce them.**

**The six expected values are asserted DISTINCT.** If two were equal, a swap
between them would be invisible; the test sorts them and requires no adjacent
pair to match. That guard is the difference between a test of the ordering and
a test that happens to pass.

The engineering-shear convention is built into those three formulas:
`gxy = du_x/dy + du_y/dx`, not half of it.

### Stress, against an independent `Dref`

A second constitutive law, written in the test from the continuum relations
rather than obtained from `isotropicElasticity`:

```text
sii = lambda tr(eps) + 2 mu eii
tij = mu gij                        <-- mu, NOT 2 mu, because gij is
                                        engineering shear
```

with `lambda` and `mu` written out from `E = 210 GPa`, `nu = 0.3`:

```text
mu     = E / (2(1+nu))              = 8.076923076923077e+10 Pa
lambda = E nu / ((1+nu)(1-2nu))     = 1.211538461538462e+11 Pa
```

```text
component   analytical (Dref)    recovered            relative error   PASS
-------------------------------------------------------------------------------
sxx         computed             matches              < 1e-10          yes
syy         computed             matches              < 1e-10          yes
szz         computed             matches              < 1e-10          yes
txy         computed             matches              < 1e-10          yes
tyz         computed             matches              < 1e-10          yes
tzx         computed             matches              < 1e-10          yes
```

And the resulting state is **fully three-dimensional** — `szz`, `tyz` and `tzx`
are each asserted nonzero — so the derived invariants computed from it are not
being measured on a degenerate case.

### Origin independence

The same field gradient sampled on a tetrahedron translated by
`(+0.5, -0.25, +1.5)` m gives the same strain and the same von Mises to
`< 1e-8`. Strain is a derivative, so it cannot depend on where the element
sits; the field is **re-sampled** at the moved corners rather than reused, so
the test is of the recovery and not of the arithmetic.

## 2. Rigid motions

### Translation

Every corner displaced by the same `(1.3e-3, -7.0e-4, 2.9e-3)` m:

```text
all six strain components      |value| < 1e-16      PASS
```

The tetrahedron is ~2e-2 m across and the translation ~1e-3 m, so a spurious
gradient would land near 1e-1. Zero to 1e-16 is a genuine cancellation, not a
loose bound.

### Infinitesimal rotation

`u = omega x r` with `omega = (7.0e-4, -3.0e-4, 5.0e-4)`:

```text
all six strain components      |value| < 1e-16      PASS
```

**This is the stronger of the two.** A translation is annihilated by any `B`
whose rows sum to zero; a rotation is annihilated only by one whose
**symmetric part** is right — which is exactly what the engineering-shear
pairing `gxy = du_x/dy + du_y/dx` encodes. A `B` that paired the terms with a
minus sign would pass the translation case and fail this one.

**And the instrument is not vacuous:** the same displacements with one
component's sign flipped are not a rigid rotation, and the test requires the
resulting `gxy` to exceed 1e-6. Without that, the assertion above would pass on
`return Strain6{}`.

## 3. One pure component at a time

Nine cases, each a field whose only gradient is a single partial derivative.
This is what catches a permuted row: a field with only `du_x/dx` must give
`exx` and five exact zeros.

```text
field                    expected strain              recovered   PASS
-------------------------------------------------------------------------
du_x/dx = e              (e,0,0, 0,0,0)               matches     yes
du_y/dy = e              (0,e,0, 0,0,0)               matches     yes
du_z/dz = e              (0,0,e, 0,0,0)               matches     yes
du_x/dy = e              (0,0,0, e,0,0)               matches     yes
du_y/dx = e              (0,0,0, e,0,0)               matches     yes
du_y/dz = e              (0,0,0, 0,e,0)               matches     yes
du_z/dy = e              (0,0,0, 0,e,0)               matches     yes
du_z/dx = e              (0,0,0, 0,0,e)               matches     yes
du_x/dz = e              (0,0,0, 0,0,e)               matches     yes
```

`e = 2.5e-4`, all to `< 1e-14` absolute.

**The two spellings of each shear are separate cases**, and they must give the
SAME answer: `du_x/dy = e` alone and `du_y/dx = e` alone both give
`gxy = e`, because `gxy` is their sum. A tensor-shear convention would give
`e/2` for each. Each case also checks the hand-written expectation against the
field's own analytic derivative first, so a wrong case cannot silently become a
wrong test.

## 4. The constitutive relations, component by component

```text
input                         required                         measured    PASS
-----------------------------------------------------------------------------------
gxy = 1.5e-4 alone            txy = mu gxy                     < 1e-12 rel  yes
                              and NOT 2 mu gxy                 asserted     yes
                              every other component zero       < 1e-6 Pa    yes
gyz = 1.5e-4 alone            tyz = mu gyz, others zero        < 1e-12 rel  yes
gzx = 1.5e-4 alone            tzx = mu gzx, others zero        < 1e-12 rel  yes

exx = 3.0e-4 alone            sxx = (lambda + 2 mu) e          < 1e-12 rel  yes
  (UNIAXIAL STRAIN,           syy = lambda e                   < 1e-12 rel  yes
   not uniaxial stress)       szz = lambda e                   < 1e-12 rel  yes
                              shear all zero                   < 1e-6 Pa    yes
                              and sxx != syy, syy > 0          asserted     yes

exx=eyy=ezz = -2.0e-4         all three normals equal          < 1e-12 rel  yes
                              = (3 lambda + 2 mu) e            < 1e-12 rel  yes
                              shear all zero                   < 1e-6 Pa    yes
                              NEGATIVE, so the sign survives   asserted     yes
                              von Mises = 0                    < 1e-3 Pa    yes
                              sigma_h = the value itself       < 1e-12 rel  yes
```

The uniaxial case holds `eyy = ezz = 0`, which **requires lateral stress** —
so `syy` and `szz` are not zero, and the test asserts `syy > 0` and
`sxx != syy` so the claim cannot be mistaken for uniaxial *stress*.

## 5. A real solved field, against a fitted gradient

RM-MESH-01 (120 x 70 x 35 mm block, 20 mm target, **8 nodes, 6 tetrahedra**),
fixed at `z = 0`, loaded at `z = 35 mm`, solved through the whole qualified
chain. Then, for **every element**, the strain is recovered a second way:

```text
a Tet4's displacement field is LINEAR, so each component is
    u = a0 + a1 x + a2 y + a3 z
and its four nodal values determine the four coefficients EXACTLY.
Fit that 4x4 system by Gaussian elimination with partial pivoting --
written out in the test -- and read off a1, a2, a3.
```

That is a completely different route to the gradient: no `1/(6V)`, no cofactor
determinant, no reference element, no transpose. The nodal displacements are
read from **the recovered per-node channel**, which also ties the element
gather to the nodal result.

```text
quantity                         worst relative error over 6 elements   bound     PASS
----------------------------------------------------------------------------------------
strain, all six components                       2.38396e-16            1e-9      yes
stress, all six components                       2.68641e-16            1e-9      yes
von Mises                                        2.97172e-16            1e-9      yes
```

Agreement at **machine precision**, three orders tighter than the 1e-9
geometric-accumulation bound the test gates on.

**And the comparison measured something:** the test requires at least one
element with a von Mises above 1 Pa and a positive peak displacement. A
rigid-body solution would have made every error above exactly zero and proved
nothing.

## 6. The continuum relations on an axial prism

Same model, `F = 50 kN` along `+Z`, `A = 0.120 x 0.070 m`, `L = 0.035 m`,
`E = 210 GPa`.

```text
nominal sigma = F/A          = 5.952380952e+06 Pa
nominal eps   = sigma/E      = 2.834467e-05
nominal delta = F L / (A E)  = 9.920635e-07 m
```

### The volume-weighted mean axial stress is an EQUALITY, not a band

```text
measured mean sigma_zz   5.95238e+06 Pa
F/A                      5.95238e+06 Pa
ratio                    1.0, to within 1e-9 (the asserted bound)
```

This is not a fortunate agreement. For a body in equilibrium whose only axial
traction is on the two end faces,

```text
(1/V) integral sigma_zz dV  =  F L / V  =  F / A
```

holds **exactly** — it is a statement of equilibrium, which the assembled
discrete system satisfies exactly, so it does not depend on the mesh being
fine. The bound is therefore the floating-point accumulation band and not a
tuned tolerance. The first draft of this test used 10%; the measurement showed
a ratio of 1, the theorem was recognised, and the bound was tightened to 1e-9
**on the strength of the theorem rather than the observation**.

### The deflection bound is ONE-SIDED, and it is about the MEAN

```text
mean u_z over the loaded face    9.76206e-07 m
FL/(AE)                          9.920635e-07 m
ratio                            0.984015        <-- below 1, as a Tet4 must be
```

A constant-strain Tet4 cannot represent the exact linear field of this problem
on a coarse mesh and is **stiffer** than the continuum, so the computed
compliance must be smaller. That inequality is known before the measurement and
cannot be tuned, which is what makes it evidence. Demanding equality on six
tetrahedra would have been a tolerance invented to pass.

**The quantity is the MEAN of the loaded face, not a point maximum**, and that
distinction was a real defect in the first draft of this test:

```text
largest u_z     1.38406e-06 m       ratio 1.39513   <-- ABOVE FL/(AE)
mean u_z        9.76206e-07 m       ratio 0.984015  <-- below, correctly
```

The load is lumped equally onto the face's **nodes**, which on a coarse mesh is
not a uniform traction, so a single node can displace further than the
uniform-strain value. The compliance bound is a statement about the average
elongation and only the average is bounded. The first version asserted the
maximum and failed; the fix was to measure the right quantity, not to widen the
bound.

### Axial strain

```text
largest |eps_zz|   within [0.2, 5.0] x sigma/E
```

An order-of-magnitude band, and stated as one. A coarse six-element mesh has no
business reproducing `sigma/E` closely, and the band is wide enough to say so
honestly while still catching a factor-of-ten error.

## 7. Displacement magnitude

```text
input                      expected       measured       PASS
----------------------------------------------------------------
(3, 4, 0)                  5              exact to 1e-15 yes
(0, 3, 4)                  5              exact to 1e-15 yes
(4, 0, 3)                  5              exact to 1e-15 yes
(-3, -4, 0)                5              exact to 1e-15 yes
(0, 0, 0)                  0              EXACTLY 0      yes
(1, 2, 3)                  sqrt(14)       to 1e-15       yes
(1, 2, -3)                 sqrt(14)       to 1e-15       yes
(3e-6, 4e-6, 0)            5e-6           to 1e-15       yes
(3e200, 4e200, 0)          5e200          finite, 1e-14  yes
```

The `(1, 2, 3)` case is the **discriminating** one: for an axis-aligned vector
the Euclidean norm, the component maximum and `|ux+uy+uz|` all agree, so an
axis-aligned case tests none of them. Here `sqrt(14) = 3.7417`, the maximum is
`3` and the sum is `6`, and the test asserts the result is neither 3 nor 6. The
`(1, 2, -3)` case adds the mixed-sign version, where the sum would be 0.

The `3e200` case is the reason the implementation uses the three-argument
`std::hypot` rather than `sqrt(x*x + y*y + z*z)`: the naive form overflows
forming the squares, and the test requires a finite answer.

Over every solved fixture, each node's stored magnitude is checked against
`std::hypot` of its own components to `< 1e-18` absolute — so the stored value
cannot have come from a different formula than the one documented.
