# P17-QUAL-001 — independent analytical validation and the tolerance audit

```text
SUBJECT:  every closed-form oracle the P17 reference suite holds the solver
          to, re-run on the frozen candidate, with the gate beside the
          measurement; and the audit that no tolerance was loosened to pass
SOURCE:   tests/reference/Analytic.hpp, which includes no BetterCAD header
          and cannot call a production formula; the suite's own WARN lines
          from qualification/postrun/suite-<preset>.txt
AGAINST:  P17-REFMOD-001's run of the same suite on byte-identical
          production code: every measurement line identical (the 337
          message lines of the debug-ext run compared one by one)
```

## The oracles

```text
patch test (RM-FEA-01)    an affine field u = A x: strain sym(A), stress by
                          Hooke's law from lambda and mu, von Mises by the
                          DEVIATORIC contraction; K u equals sigma S / 3 per
                          corner from the suite's own area vectors and is
                          zero at the interior node
axial bar (RM-FEA-02)     on ROLLERS, so the exact solution is affine and a
                          Tet4 reproduces it: delta = FL/AE, sigma = F/A,
                          R = -F, U = F delta / 2
pressure (RM-FEA-04)      resultant -pA; mean sigma_zz = -p EXACTLY by the
                          virtual work of u = z e_z
self-weight (RM-FEA-05)   resultant -rho V g at the centre of volume; mean
                          sigma_zz = -rho g H / 2 exactly
cantilever (RM-FEA-03)    Euler-Bernoulli and Timoshenko (kappa = 6(1+nu)/(7+6nu))
                          as the TARGET of a convergence trend, never as an
                          equality; applied load within (1 - d/r)^2 of t pi r^2
rigid transform (06)      R x + t on every boundary node; invariants equal;
                          vectors rotate; connectivity corresponds
local refinement (07)     a volume band 0.3 L deep at each cap, and a MIRROR
                          document, so refinement is proved by where the
                          elements went and not by their count
```

## The tolerance table, re-measured

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
| | largest `\|u\|` | `hypot(nu eps a, nu eps b, delta) = 2.15354e-5 m` | 1e-9 rel | pass |
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
| | strain energy, max `\|u\|`, max von Mises | equal | 1e-5 rel | equal to 6 digits |
| | element von Mises / hydrostatic | equal through centroid correspondence | 1e-5 of max vm | 5.15e-07 / 9.23e-07 |
| | `u_placed` vs `R u_base` | | 1e-5 of max `\|u\|` | 1.09e-06 boundary, 1.59e-06 interior |
| | total reaction vs `R F_base` | | 1e-9 rel | pass |
| | nodal reactions vs `R r_base` | | 1e-5 rel | 1.10e-07 |
| RM-FEA-07 | root band elements, root control vs none | > 10x | 315 vs 2 | pass |
| | root band median edge, root control vs none | < 0.25x | 9.01 vs 61.6 mm | pass |
| | tip band, mirror document | > 10x and < 0.25x | 190 vs 2; 11.6 vs 61.6 mm | pass |
| | energy, tip deflection, peak von Mises | greater than unrefined | | pass |

Every gate is at least three orders of magnitude above its measurement
except the rigid-transform rows, where the 1e-5 band covers the interior
node the mesher places on its own and the measured value is 1e-6 (recorded
by P17-REFMOD-001 with the reason). `kExact = 1e-9` is BetterCAD's band for
floating-point accumulation; `kAlgebraic = 1e-12` for a sum with no
factorisation in it.

## The convergence study (RM-FEA-03) and findings F2, F3

```text
level   global  defl.   nodes  tets   tip u_y (m)   2U/F (m)     ratio to   pivot     eta_M
        (mm)    (mm)                                             Timoshenko
coarse  10      0.25     138    489   1.55825e-06   1.55855e-06  0.0377     4.8e-04   5.98e-13
medium   5      0.10     332   1626   1.95922e-06   1.96083e-06  0.0474     5.3e-03   1.59e-13
fine     2.5    0.05     494   2465   1.98538e-06   1.98738e-06  0.0481     6.6e-04   1.15e-12

Euler-Bernoulli  F L^3 / 3EI          4.04203e-05 m
Timoshenko       + F L / kappa G A    4.13095e-05 m
```

Strictly increasing nodes, tetrahedra and free equations asserted before any
error is compared; the deflection rises monotonically on both measures; the
distance to Timoshenko falls at every step; the finest level deflects 1.27x
the coarsest (gated at 1.2x). **No equality and no band is claimed at any
level.** The "Cantilever convergence PASS" gate is passed as a convergence
study, which is what the brief asks, and the result it converges toward on
these meshes is a few per cent of beam theory: F2 (linear Tet4 locks in
bending) with its measured cause F3 (every wall triangle spans the full
100 mm at every deflection; needle fraction 0.40, 0.13, 0.10). Both recorded
for the owner by P17-REFMOD-001, unchanged here.

## Finding F1, restated at this gate

The convergence levels are solved under an explicitly stated
`EquilibriumTolerance{1e-9, 1e-9}` because the fine level's `eta_M`
(1.15e-12) crosses the production 1e-12 moment threshold; the committed
cantilever (5.98e-13) and every other reference model pass the production
default everywhere. The reactions balance the applied load to 1e-9 at every
level, so nothing is out of balance: the gate's margin ran out on a
factorisation with pivot ratios of 5e-4 to 7e-4. **No production threshold
was touched at this gate either.** Revising P17-REACTION-001's measured
value is the owner's ADR-level decision, and the measurement is on file.

## The tolerance audit: nothing was loosened

```text
tolerances in the three new final gates          byte identity (x12), set
                                                 equality, bit-exact
                                                 fingerprint equality (x3),
                                                 one 1e-12 relative check on
                                                 linearity of an 8-node solve
                                                 whose residual is 4e-17
tolerances changed in any existing test          0 -- tests/ moved by one new
                                                 file, one registration line
                                                 and one zero-match filter
                                                 (git diff --stat of the
                                                 committed tree)
production thresholds changed                    0 -- src/ is byte-identical
                                                 to the P17-REFMOD-001 freeze
                                                 (fingerprint f3c1245d)
thresholds inherited and re-read                 pivotFloor 1e-12,
                                                 relativeResidualTolerance
                                                 1e-9, EquilibriumTolerance
                                                 1e-12 / 1e-12, radius-ratio
                                                 floor 1e-10, aspect ceiling
                                                 3e5, warning envelope 3e-4 /
                                                 6e-3 / 3.125 / 81.0 -- each
                                                 at the line ADR_AUDIT_CLAUSES.md
                                                 cites
```
