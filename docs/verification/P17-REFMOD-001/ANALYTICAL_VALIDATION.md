# P17-REFMOD-001 — analytical oracles, tolerances and the three findings

```text
SUBJECT:  where every expected number comes from, why none of it comes from
          the code under test, and what the measurements say beside each gate
SOURCE:   tests/reference/Analytic.hpp -- which opens with "Nothing here uses
          BetterCAD or the kernel", and whose structural section says
          "There is no B matrix, no D matrix, no von Mises from
          StructuralPost.hpp, no facet integration from
          StructuralLoadVector.hpp and no Lame constant read from an
          ElasticityMatrix"
PRESET:   every figure below is from debug-ext, 2026-10-15; the suite prints
          each one with WARN so the tables are transcriptions
```

## The rule, and how it is enforced rather than promised

`Analytic.hpp` includes no BetterCAD header, so it *cannot* call a production
formula. The chain for every model:

```text
the model's DIMENSIONS, read from its own parameters
the document's MATERIAL, resolved through P15 and compared with the header's
   declared constants EXACTLY (E == 210e9, nu == 0.3, rho == 7850)
the analysis's own LOAD VALUE, read from the definition
    |
    v  a closed form that knows nothing about BetterCAD
INDEPENDENT EXPECTATION
    |
    +--> compared with the PRODUCTION field, stage by stage
```

Three oracles deserve a sentence each, because they are exact where a reader
would expect a tolerance:

- **The axial bar is affine.** With rollers instead of a fixed base the
  Poisson contraction is free, the exact solution `u = (-nu eps x, -nu eps y,
  eps z)` is linear in position, and a Tet4 reproduces it to roundoff. So
  `delta = FL/AE`, `sigma = F/A` and `R = -F` are compared at 1e-9 and
  measured at 1e-15.
- **The mean axial stress is a statement of discrete equilibrium.** Virtual
  work of the affine field `u = z e_z`: the internal work is
  `sum_e sigma_zz,e V_e`, the external work is the top traction's `-p A H`
  (RM-FEA-04) or the body force's `-rho g V z_bar` (RM-FEA-05), the base
  reactions do no work at `z = 0`. So `(1/V) sum sigma_zz V = -p` and
  `= -rho g H / 2` EXACTLY, whatever the mesh. Measured at 0 and 1.8e-15.
- **The patch test's force check uses the mesh's own outward winding.** The
  suite's divergence sum over the boundary triangles is required positive
  first, so `sigma S / 3` per corner is known to point out of the material.

## The tolerance table

Every gate beside what it measured. `kExact = 1e-9` is BetterCAD's band for
floating-point accumulation; `kAlgebraic = 1e-12` for a sum with no
factorisation in it.

| Model | Quantity | Oracle | Gate | Measured |
| --- | --- | --- | --- | --- |
| RM-FEA-01 single tet | strain, 6 components | `sym(A)` | 1e-12 rel | 1.51e-15 |
| | stress | Hooke from `lambda, mu` | 1e-12 rel | 4.67e-16 |
| | von Mises, hydrostatic, principal sums | deviatoric contraction, `tr/3`, `tr` | 1e-12 rel | pass |
| | `Ke u` vs `sigma S / 3` on 4 faces | test-side area vectors | 1e-10 rel | 1.79e-15 |
| | net internal force | 0 | 1e-10 of scale | pass |
| | energy vs `(1/2) sigma:eps V` | | 1e-12 rel | pass |
| | rigid rotation: strain, stress, energy | 0 | 1e-12 of scale | pass |
| RM-FEA-01 meshed, 12 elements, 1 interior node | strain / stress / von Mises per element | the same affine field | 1e-11 rel | 1.51e-15 / 1.40e-15 / 5.38e-16 |
| | `K u` at boundary nodes vs `sigma S / 3` | | 1e-10 rel | 8.70e-16 |
| | `K u` at the interior node | 0 | 1e-10 of scale | 2.13e-15 |
| | net force | 0 | 1e-10 of scale | 1.26e-15 |
| RM-FEA-02 | every nodal displacement vs the affine state | `(-nu eps x, -nu eps y, eps z)` | 1e-9 of delta | 4.74e-16 |
| | tip `u_z`, every tip node | `FL/AE = 2.142857e-5 m` | 1e-9 rel | pass |
| | largest `|u|` | `hypot(nu eps a, nu eps b, delta) = 2.15354e-5 m` | 1e-9 rel | pass |
| | strain / stress / von Mises per element | `eps, -nu eps; F/A = 50 MPa` | 1e-9 rel | 3.70e-15 / 3.13e-15 / 1.49e-15 |
| | total reaction | `-F = -21600 N` | 1e-9 rel | 6.74e-16 |
| | reaction split | Uz roller carries `-F`, Ux and Uy rollers carry 0 | 1e-9 of F | pass |
| | strain energy | `(1/2) F delta = 0.231429 J` | 1e-9 rel | pass |
| RM-FEA-04 | applied resultant | `-pA = -5400 N` | 1e-12 rel | 1.68e-16 |
| | applied moment about the face centroid | 0 | 1e-12 of `F a` | pass |
| | total reaction | `+5400 N` | 1e-9 rel | pass; eta_F 5.0e-18, eta_M 1.5e-17 |
| | reaction moment about the face centroid | 0 | 1e-9 of `F a` | pass |
| | mean `sigma_zz` | `-p = -2 MPa` | 1e-9 rel | 0 |
| RM-FEA-05 | applied resultant | `-rho V g = -10.7775 N` | 1e-11 rel | 0 |
| | applied moment about the origin | `(-b/2 W, +a/2 W, 0)` | 1e-9 rel | pass |
| | total reaction | `+W` | 1e-9 rel | pass; eta_F 6.2e-16, eta_M 5.6e-16 |
| | mean `sigma_zz` | `-rho g H / 2 = -3079.29 Pa` | 1e-9 rel | 1.77e-15 |
| RM-FEA-03, every level | reaction vs applied | equal | 1e-9 rel | pass |
| | applied vs `t pi r^2` | within `(1 - d/r)^2` | derived | 198.986, 198.986, 199.351 N of 200 |
| | root moment | `F_applied L` | 1e-9 rel | pass |
| RM-FEA-06 | boundary node gap under `R x + t` | 0 | 1e-9 m | 1.56e-17 m |
| | interior node gap | recorded | 1e-4 m | 2.66e-07 m, 1 node |
| | connectivity | every base tet has an image | exact | 12 of 12 |
| | strain energy, max `|u|`, max von Mises | equal | 1e-5 rel | equal to 6 digits |
| | element von Mises / hydrostatic | equal through centroid correspondence | 1e-5 of max vm | 5.15e-07 / 9.23e-07 |
| | `u_placed` vs `R u_base` | | 1e-5 of max `|u|` | 1.09e-06 boundary, 1.59e-06 interior |
| | total reaction vs `R F_base` | | 1e-9 rel | pass |
| | nodal reactions vs `R r_base` | | 1e-5 rel | 1.10e-07 |

## RM-FEA-03: the convergence study, and finding F2

```text
level   global  defl.   nodes  tets   tip u_y (m)   2U/F (m)     ratio to   pivot     eta_M
        (mm)    (mm)                                             Timoshenko
coarse  10      0.25     138    489   1.55825e-06   1.55855e-06  0.0377     4.8e-04   5.98e-13
medium   5      0.10     332   1626   1.95922e-06   1.96083e-06  0.0474     5.3e-03   1.59e-13
fine     2.5    0.05     494   2465   1.98538e-06   1.98738e-06  0.0481     6.6e-04   1.15e-12

Euler-Bernoulli  F L^3 / 3EI          4.04203e-05 m
Timoshenko       + F L / kappa G A    4.13095e-05 m     kappa = 6(1+nu)/(7+6nu)
```

The levels differ structurally -- strictly increasing nodes, tetrahedra and
free equations, asserted before any error is compared -- the deflection rises
monotonically on both measures, the distance to the Timoshenko value falls at
every step, and the finest level deflects 1.27x the coarsest (gated at 1.2x).
**No equality and no band is claimed at any level.**

**F2.** A linear tetrahedron locks in bending, and on these meshes it deflects
a few per cent of beam theory. That is the element the P17 scope chose
(ADR-034), measured rather than excused -- and the reason it is THIS slow is
F3.

## Finding F3: a developable wall is never subdivided along its ruling

The instrument is asserted in the test so it stays measured:

```text
level    longest boundary edge / L    fraction of tets with an edge >= L/2
coarse   1.00015                      0.401
medium   1.00015                      0.125
fine     1.0001                       0.104
```

OCCT triangulates the cylinder's wall as strips the FULL LENGTH of the body at
every deflection: the deflection governs the chord angle, the ruling direction
is straight and gets no subdivision. Every tetrahedron owning a wall triangle
therefore carries a 100 mm edge, at the outer fibre where the bending stress
lives, and refinement through the global target -- which adds interior points
and cannot shorten a boundary edge -- leaves that layer in place. This is
P16-REFMOD-001's planar-face limitation ("two triangles whatever the
deflection") in its curved form, and it bounds what any bending analysis
BetterCAD runs on a prismatic body can resolve until the surface pipeline can
subdivide along a ruling. It is the mid-span moment instrument's noise too
(0.44, 0.54, 0.44 N m against 10): element-constant stresses on needles do
not integrate a bending moment.

Recorded for the owner. Nothing in this suite works around it.

## Finding F1: the moment-equilibrium threshold on a bending solve

P17-REACTION-001 measured its worst normalized moment imbalance at 2.5e-14 on
a well-conditioned 850-node cylinder under a cap pressure and set the
production threshold at 1e-12, noting that "a mesh an order larger ... could
cross it without anything being wrong". A bending cantilever of needle
elements is the first model to do so:

```text
committed cantilever, 489 tets, L = 100 mm     eta_M 5.98e-13   passes
the same at L = 120 mm (the runner's first      eta_M 1.27e-12   REFUSED by
  model-change value)                                            solveStructuralAnalysis
fine convergence level, 2465 tets               eta_M 1.15e-12   REFUSED under the
                                                                 default
```

The imbalance is the forward error of the factorisation reaching the
constrained rows of `K u - F`: pivot ratios of 5e-4 to 7e-4 against 0.03 to
0.3 for every block in the suite. The reactions still balance the applied load
to 1e-9 at every level (asserted), so nothing is out of balance; the gate's
margin is what ran out.

What this suite does about it, and does not: the committed cantilever is
solved under the production default everywhere -- the suite, the runner, the
CLI -- and passes it; its model-change edit SHORTENS it to 80 mm so the
runner's re-solve stays under the default; the convergence levels are solved
under an explicitly stated `EquilibriumTolerance{1e-9, 1e-9}` with every
level's `eta_M` recorded and asserted under that band. No production threshold
was touched: changing P17-REACTION-001's measured value is an ADR-level
decision for the owner, and the measurement that would inform it is above.

## pi

`analytic::pi = std::numbers::pi`; the cantilever's traction is formed in the
builder from `reference::kPi = 3.14159265358979323846`, the same double, and
the test derives `F = t pi r^2` from the declared traction and radius and
requires it to equal `kCantileverTipForceN` to 1e-12.
