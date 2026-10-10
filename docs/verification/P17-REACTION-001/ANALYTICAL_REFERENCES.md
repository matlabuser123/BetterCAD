# P17-REACTION-001 — analytical reference cases

```text
RESULT: PASS
```

Every oracle here is the **test's own**: a hand-derived partition formula, a
continuum resultant computed from the model's declared dimensions, or a cross
product written out in the test. Nothing asks the production reaction path what
the answer should be.

## 1. The partition formula, on a small system

The algebra isolated from every geometric part of the FE chain. The system is
chosen so every number is exact in binary:

```text
K = [  4   1   2 ]        F = ( 8, 3, 5 )
    [  1   8   1 ]
    [  2   1  16 ]

DOF 0 free; DOFs 1 and 2 constrained with u = 0.
```

Solved by hand:

```text
4 u0 = 8                 ->  u0 = 2            exactly
R_1 = K_10 u_0 - F_1     =  1*2 - 3  =  -1     exactly
R_2 = K_20 u_0 - F_2     =  2*2 - 5  =  -1     exactly
```

```text
claim                                              route                result
------------------------------------------------------------------------------------
u0 = 2                                             production kernel    1e-14 rel
r_full[0] (free row)                               hand-computed K u-F  0 to 1e-14
r_full[1] == -1                                    hand-computed        1e-14 rel
r_full[2] == -1                                    hand-computed        1e-14 rel
the dropped K_cc u_c term really is zero           hand-computed        exact
```

The last row is the one that makes the implementation correct rather than
merely convenient: the general formula is
`R_c = K_cf u_f + K_cc u_c - F_c`, and what licenses dropping the middle term
is that `SolvedSystem` assigns the whole displacement vector `0.0` and writes
only free rows — so `u_c` is **exactly** zero. Adding the term back against
`u_c = 0` is asserted to change nothing.

## 2. The fully constrained case — the primary sign gate

```text
every degree of freedom constrained  ->  u = 0 is KNOWN, not solved
                                     ->  r_full = K*0 - F = -F   bit-exactly
```

```text
applied   (300, -500, 700) N      three DISTINCT components
reaction  (-300, 500, -700) N     1e-12 rel, and each SIGN asserted
||e_F||2  0 N                     EXACTLY zero
eta_F     0
eta_M     0
```

No factorisation, no conditioning, nothing to argue about. A reaction defined
as `F - Ku` fails by a factor of -1, and the mutation that makes that
substitution is killed by **18 of 20** tests.

The loaded node is a **corner of the start cap**, so it is restrained — and the
external total is asserted to include it, because excluding an applied load
because it acts on a support would make both totals wrong by the same amount
and equilibrium would still pass.

## 3. The cantilever — force and a distributed moment

```text
start cap fixed, end cap loaded with 1200 N along +Z
```

```text
claim                                   oracle                        result
-------------------------------------------------------------------------------
R_total,z == -1200 N                    the applied load              1e-9 rel
M_reaction == the moment of the         the test's OWN cross product  1e-9 x scale
  reported nodal distribution             written out componentwise
|M_external| > 1 N m                    asserted, so not vacuous      PASS
|M_reaction| > 1 N m                    asserted                      PASS
eta_F, eta_M                            <= 1e-12                      PASS
```

The support supplies its moment through ~100 translational nodal reactions.
There is no nodal couple, and the test's independent accumulation is what a
reversed operand order in production cannot agree with.

## 4. A planar pressure — `F = -p A n` through the centroid

RM-MESH-01, a 120 x 70 x 35 mm block, 2 MPa on the 120 x 70 mm top face.
**Positive pressure acts inward** (`t = -p n_out`, frozen by P17-LOAD-001), so
a pressure on the `+Z` face pushes along `-Z`.

```text
analytical F_z   = -p A = -2e6 x 0.120 x 0.070 = -16800 N
reaction   F_z   = +16800 N
relative error   = 4.33093e-16
```

```text
layer                                              result
------------------------------------------------------------------------
A  reactions vs the assembled F that was solved    eta_F, eta_M <= 1e-12
B  reactions vs the ANALYTICAL continuum resultant 4.33e-16 relative
   assembled F vs PreparedLoads::resultantForce()  1e-12 rel
   both vs the analytical resultant                1e-9 rel
   moment about the face CENTROID                  1e-9 x scale
```

The centroid check is a separate statement: a uniform pressure through the
centroid produces no moment about it, so the support's moment about the same
point must also vanish.

## 5. Gravity — `W = rho V g` through the centre of mass

RM-MESH-01 again, because a **box** is tiled exactly by its tetrahedra, so the
mesh volume *is* the analytical volume.

```text
V  = 0.120 x 0.070 x 0.035          = 2.94e-4 m^3
W  = rho V g = 7850 x 2.94e-4 x 9.80665
   = 22.6328 N
```

```text
claim                                       oracle                   result
--------------------------------------------------------------------------------
R_total,z == +W                             rho V g, by hand         1e-9 rel
relative error                              --                       1.56972e-16
M_external about the global origin          c x W with c the         1e-9 x scale
  = (-0.792147, +1.357965, 0) N m             centroid (60, 35) mm
M_reaction == -M_external                   --                       1e-9 x scale
both moment components > 0.1 N m and
  DIFFERENT from each other                 asserted                 PASS
```

**The production path computes no centre of mass.** The assembled `F` carries
gravity as nodal-equivalent forces and the moment follows from their positions;
the analytical centroid is the test's oracle. That is what makes the moment
check meaningful: a gravity distribution weighted by node count instead of by
volume would give the right total force and the **wrong** moment.

## 6. Two disjoint supports

Two side faces that do not touch, so they share no degree of freedom.

```text
claim                                              result
---------------------------------------------------------------------
sharedDegreesOfFreedom == 0                        asserted FIRST
resolved[0].dof + resolved[1].dof == unique        asserted
R_A + R_B + sharedForce == R_total                 1e-9 rel
M_A + M_B + sharedMoment == M_total                1e-9 rel
R_A + R_B + F_external == 0                        eta_F <= 1e-12
each |R| > 1 N                                     asserted
```

### Why an equal split is NOT asserted

```text
R_A + R_B = -F_external     exact statics, asserted
R_A = R_B                   NOT asserted, and the magnitudes are PRINTED
```

Reaction sharing depends on stiffness, geometry and load position. Deriving a
predicted split needs a statically determinate idealisation; a 3D continuum
with two fixed faces is indeterminate, and the symmetric variant would need a
provably **symmetric mesh**, which P16 does not guarantee for a symmetric body.
Brief 45 and 106 both warn against forcing an analytically false expectation,
and this is that warning taken seriously rather than satisfied with a fixture
that merely looks symmetric.

## 7. Two overlapping supports

Two side faces that **meet** along an edge, each fully fixed, so the edge's
degrees of freedom are constrained twice in the lists and once in the union.

```text
claim                                              result
---------------------------------------------------------------------
listed > unique                                    asserted FIRST
sharedDegreesOfFreedom > 0                         asserted
listed - unique == sharedDegreesOfFreedom          asserted
global eta_F <= 1e-12                              PASS -- nothing double-counted
R_total,z == -1800 N                               1e-9 rel
owned(R1) + owned(R2) + shared == R_total          1e-9 rel
the NAIVE sum overshoots by exactly sharedForce    measured
```

The first three rows are the vacuous-instrument guard: without a real overlap
every claim below them would hold trivially.

## 8. A pure couple

```text
equal and opposite loads on two opposite faces

sum(F_external) = 0           to 1e-9 N
scale_F         > 1000 N      the loads do not cancel, only the net
sum(M_external) > 1 N m
sum(M_reaction) = -sum(M_external)   to 1e-9 x scale
```

Force equilibrium alone would pass this with any moment error, which is the
point of including it.

## 9. Linearity

```text
claim                              result
---------------------------------------------------
R(3.5 F) == 3.5 R(F)               1e-9 rel
R(-F)    == -R(F)                  1e-9 rel, and the SIGNS asserted opposite
R(F1+F2) == R(F1) + R(F2)          1e-9 rel, with both terms > 1 N
```

## 10. The origin-shift relation

```text
M(O2) = M(O1) - (O2 - O1) x F      the test's own cross product
```

asserted to 1e-9 x scale, with the two moments required to **differ** so the
relation is not checked on an identity — and a foreign mesh refused, because
`momentAbout` needs node positions.

## What is NOT claimed

```text
a predicted two-support split            see case 6
analytical agreement on a CURVED
  pressure face                          the facet tiling only approximates a
                                         cylinder's area, so the analytical
                                         comparison would measure P17-LOAD's
                                         discretisation rather than
                                         equilibrium. Layer A still holds
                                         exactly, which the large-mesh
                                         cylinder case confirms
continuum displacement accuracy          not this milestone's subject
nonzero prescribed displacement          deferred; u_c = 0 throughout
```
