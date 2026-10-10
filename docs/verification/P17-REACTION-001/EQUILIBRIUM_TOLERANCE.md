# P17-REACTION-001 — the equilibrium tolerance, derived from measurement

```text
RESULT: PASS

SELECTED  force equilibrium   1e-12 normalized
          moment equilibrium  1e-12 normalized
          force floor         1e-12 N
          moment floor        1e-12 N m

MEASURED  worst normalized force imbalance   1.48e-14   (large mesh)
          worst normalized moment imbalance  2.50e-14   (large mesh)
          margin                             ~40x
```

The brief is emphatic that the threshold must not be predeclared, and it was
not: the numbers below were measured first, on every fixture, and the
thresholds chosen afterwards.

## What the metric is

```text
e_F = F_external + R_total
e_M = M_external(O) + M_R(O)
```

reported as `||.||_2`, `||.||_inf`, **each component**, and normalized by a
sum of participating magnitudes:

```text
scale_F = sum |F_i|              over loaded nodes
        + sum |R_j|              over constrained nodes
scale_M = sum |(x_i - O) x F_i|
        + sum |(x_j - O) x R_j|

eta_F = ||e_F||_2 / scale_F      (or 0 when scale_F is below the floor)
eta_M = ||e_M||_2 / scale_M
```

**Not normalized by the net resultant**, and that choice is load-bearing. A
pure couple has `F_external = 0` by construction, and a net external moment is
reachable by cancellation too; a ratio against the net would divide a real
imbalance by almost nothing. `scale` reflects what participates. Both scales
are reported in every measurement below so the ratio is auditable rather than
asserted.

**The floors carry their own dimension** — newtons and newton-metres, never one
dimensionless number for both. They exist only so a zero-load system does not
divide by zero, and a mutation that replaced the scale with the net resultant
is killed.

## A solver residual tolerance is NOT an equilibrium tolerance

This is recorded because the brief asks for it and because the measurement
shows the two are genuinely different numbers.

```text
                          solver normalized      equilibrium eta_F
                          residual (free eqns)   (whole body)
----------------------------------------------------------------------
cantilever, nodal +Z      2.07357e-17            1.27233e-16
cantilever, 3-component   2.24690e-17            2.06177e-16
cantilever, 1e-3 N        2.76589e-17            2.18041e-16
cantilever, 1e6 N         2.67654e-17            1.76225e-16
fully constrained         0                      0
```

The solver's gate is `||Kff uf - Ff|| / (||Kff||inf ||uf|| + ||Ff||)` over the
**free** equations. This one is over the **whole body's** applied and
constrained forces, and it additionally involves the external-load
reconstruction and, for moments, every node position. They are consistently
about an order apart, in the same direction, and neither is a substitute for
the other. P17-SOLVE-001's `relativeResidualTolerance` is not referenced
anywhere in this module — a compile-failure case proves the field is absent
from `EquilibriumTolerance` rather than present and ignored.

## Layer A — the algebraic path

The assembled `F` that was solved, against the reactions recovered from the
same system. Everything came out of one linear system, so the only error is
floating-point accumulation.

```text
fixture                     |F| (N)      scale_F (N)   ||e_F||2 (N)   eta_F
----------------------------------------------------------------------------------
cantilever, nodal +Z        1000         2051.14       2.60973e-13    1.27233e-16
cantilever, 3-component     1630.95      3487.39       7.19019e-13    2.06177e-16
cantilever, 1e-3 N          0.001        0.00205114    4.47233e-19    2.18041e-16
cantilever, 1e6 N           1e+06        2.05114e+06   3.61462e-10    1.76225e-16
fully constrained, nodal    911.043      1822.09       0              0
```

```text
fixture                     scale_M (N m)  ||e_M||2 (N m)  eta_M
--------------------------------------------------------------------------
cantilever, nodal +Z        60.1584        2.22045e-15     3.69100e-17
cantilever, 3-component     115.314        3.85923e-14     3.34671e-16
cantilever, 1e-3 N          6.01584e-05    6.53712e-21     1.08665e-16
cantilever, 1e6 N           60158.4       4.41480e-12     7.33863e-17
fully constrained, nodal    61.8379        0               0
```

### Two things worth reading off that table

**The fully constrained case is EXACTLY zero.** Every degree of freedom is
constrained, so `u = 0` is known rather than solved, the free system is empty,
and `r_full = K*0 - F = -F` bit-exactly. There is no factorisation in it at
all. That is why it is the primary sign gate.

**The normalized error is scale-invariant across nine orders of load.** From
`1e-3 N` to `1e6 N` the absolute imbalance moves by nine decades
(4.47e-19 N to 3.61e-10 N) and `eta_F` stays at ~2e-16. That is the evidence
that a RATIO is the right gate and an absolute newton threshold would be
arbitrary.

## Layer B — the geometric path

Reactions against an **analytical continuum resultant**, computed on the test
side from the model's declared dimensions. This comparison carries P17-LOAD's
own integration error, so it is a different numerical question and is measured
separately.

```text
fixture                  analytical                 reaction        relative
---------------------------------------------------------------------------------
RM-MESH-01 pressure      F_z = -p A                 +16800 N        4.33093e-16
                             = -2 MPa x 0.120 x 0.070
                             = -16800 N
RM-MESH-01 gravity       W = rho V g                22.6328 N       1.56972e-16
                             = 7850 x 2.94e-4 x 9.80665
                             = 22.6328 N
```

### The measured finding that contradicts the expectation

The brief anticipated that the geometric path would need the looser `1e-9`
band. **It does not.** Measured at 4.33e-16 and 1.57e-16 — the same order as
the algebraic path — and the reason is specific:

```text
a PLANAR pressure face       the boundary facets tile the face exactly, and a
                             uniform pressure over exact triangles sums to
                             exactly p A. There is no geometric approximation
                             to accumulate
a BOX under gravity          the tetrahedra tile the body exactly, so the mesh
                             volume IS rho V and the weight is exact
```

So **no separate looser geometric gate is defined**, and the brief's section 38
hierarchy is answered with one threshold rather than two — on the strength of
the measurement, not for convenience.

**Where a geometric penalty WOULD appear** is a curved face, where the facet
tiling only approximates the true surface and the analytical area differs from
the integrated one. That is a load-discretisation difference and P17-LOAD-001's
subject, not an equilibrium error: the reactions still balance the assembled
`F` exactly (Layer A), which the large-mesh cylinder case below confirms. No
claim is made here about analytical agreement on a curved face.

## The far-origin case

A moment about the global origin is a sum of `(x - O) x F` terms; with the
geometry far from `O` every term is large and the sum is small, which is where
cancellation would show. RM-MESH-06's placed model sits on a rotated and
translated frame, in the coordinate range real CAD models occupy.

```text
fixture                 farthest node   scale_M (N m)   eta_F         eta_M
-------------------------------------------------------------------------------
RM-MESH-06 base         0.108171 m      3334.52         7.20621e-17   4.82161e-17
RM-MESH-06 placed       0.126098 m      2972.53         1.44195e-16   8.67145e-17
```

No degradation. The moment scale is in the thousands of newton-metres and the
imbalance stays at 1e-13 N·m, so the cancellation is benign at this coordinate
magnitude. Force equilibrium is unaffected by construction — it has no lever
arm in it.

## The large mesh, which sets the threshold

```text
RM-MESH-02, cylinder, 2.5e-5 deflection over a 6 mm target

nodes                  850
degrees of freedom     2550
constrained            300
reaction entries       100
scale_F                23094.2 N
eta_F                  1.48215e-14      <-- the worst force case
scale_M                485.204 N m
eta_M                  2.49895e-14      <-- the worst moment case
```

Two orders worse than the small fixtures, which is expected: the imbalance is a
sum over 100 reaction entries and 850 load entries rather than over a handful,
and floating-point accumulation grows with the term count.

## The decision

```text
worst measured         eta_F 1.48e-14, eta_M 2.50e-14   (large mesh)
chosen threshold       1e-12 for both
margin                 ~40x on the worst case, ~4000x on the small ones
```

**Why 1e-12 and not 1e-13.** A 1e-13 gate leaves only 4x on the worst measured
case, and the error grows with the number of accumulated terms — so a mesh an
order larger than RM-MESH-02 could cross it without anything being wrong. 1e-12
keeps 40x on the largest fixture measured while staying inside BetterCAD's
documented band for "well-conditioned double-precision algebra", which is what
this is.

**Why not 1e-9.** Nothing measured needs it. The guidance offers 1e-9 for
geometric accumulation and the geometric path measured 4e-16, so using 1e-9
would be inheriting a band for a reason that does not apply here — and it would
accept a 1e-10 imbalance, five orders above anything observed, which is exactly
the room a sign or mapping defect would hide in.

**No threshold above 1e-9 is needed, and none is used.** Both thresholds are
1e-12 and both floors are 1e-12 in their own units.

### The threshold is not tuned to a failing implementation

Every measurement above comes from an implementation that already passed. No
threshold was widened to accommodate a result; the only movement was from the
first draft's conservative placeholder to the measured value.

## The gate can fail, demonstrated

A threshold that cannot fail is not a gate. The imbalance cannot be injected
from outside — it comes from the solved residual — so the demonstration runs
the other way: `StructuralReaction_RefusesAnImbalanceLargerThanTheThreshold`
tightens the threshold **below** the measured error and requires the recovery
to be refused.

```text
force threshold = measured eta_F / 10    -> ForceImbalance, recovery refused
moment threshold = measured eta_M / 10   -> MomentImbalance, recovery refused
```

Both messages carry the imbalance vector, the normalized value, the threshold
and the participating scale — and the moment message names its origin.

And the mutation that **loosens** the gate to `1e-3` is killed, so the
threshold cannot be inflated without a test noticing.

## Equilibrium failure is a FAILURE, not a warning

`recoverSupportReactions` returns an error and publishes nothing. A solve can
satisfy its own residual gate and still have a reaction sign, mapping or
load-accounting defect — the mutation table shows a sign flip failing 18 of 20
tests while the solve itself is untouched — and this is the only check that
sees it.
