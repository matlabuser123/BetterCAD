# P17-REACTION-001 — force equilibrium

```text
RESULT: PASS
GATE:   eta_F = ||F_external + R_total||2 / scale_F  <=  1e-12
SCALE:  scale_F = sum |F_i| over loaded nodes + sum |R_j| over constrained nodes
```

The external total is the **assembled `F` that was solved**, read back through
the qualified numbering. No load is re-integrated here, and
`PreparedLoads::resultantForce()` is an oracle rather than a second addend —
the two are compared, never summed.

## The required table

```text
fixture                     F_external (N)        R_total (N)           ||e_F||2    eta_F        threshold  PASS
-------------------------------------------------------------------------------------------------------------------
fully constrained, nodal    (300, -500, 700)      (-300, 500, -700)     0           0            1e-12      yes
cantilever, nodal +Z        (0, 0, 1000)          (0, 0, -1000)         2.61e-13    1.27e-16     1e-12      yes
cantilever, 3-component     (400, -900, 1300)     (-400, 900, -1300)    7.19e-13    2.06e-16     1e-12      yes
cantilever, 1e-3 N          (0, 0, 1e-3)          (0, 0, -1e-3)         4.47e-19    2.18e-16     1e-12      yes
cantilever, 1e6 N           (0, 0, 1e6)           (0, 0, -1e6)          3.61e-10    1.76e-16     1e-12      yes
two disjoint supports       (0, 0, 2000)          (0, 0, -2000)         --          <= 1e-12     1e-12      yes
two OVERLAPPING supports    (0, 0, 1800)          (0, 0, -1800)         --          <= 1e-12     1e-12      yes
pure couple                 (0, 0, 0)             (0, 0, 0)             --          <= 1e-12     1e-12      yes
Ux-only + fixed cap         (600, 0, 0)           (-600, 0, 0)          --          <= 1e-12     1e-12      yes
RM-MESH-01 pressure         F_z = -16800          F_z = +16800          --          <= 1e-12     1e-12      yes
RM-MESH-01 gravity          F_z = -22.6328        F_z = +22.6328        --          <= 1e-12     1e-12      yes
RM-MESH-06 base             pressure 5 MPa        opposes it            --          7.21e-17     1e-12      yes
RM-MESH-06 placed           pressure 5 MPa        opposes it            --          1.44e-16     1e-12      yes
RM-MESH-02 large            pressure 4 MPa        opposes it            --          1.48e-14     1e-12      yes
```

Component values are given where the fixture fixes them analytically; `--`
marks a case whose external total is a distributed resultant rather than a
chosen vector.

## The component check

A norm can hide a cancellation between components, so `imbalance` is kept as a
vector and **every component is asserted**, not just its magnitude:

```text
fully constrained   e_x = 0, e_y = 0, e_z = 0     exactly
cantilever cases    |e_x|, |e_y|, |e_z| each < 1e-9 N
```

and the fully constrained case additionally asserts the **sign** of each
reaction component (`x < 0`, `y > 0`, `z < 0`), because a magnitude-only check
would pass an implementation that took an absolute value somewhere.

## The three claims the fixtures are built to separate

### Exact, with no factorisation: the fully constrained case

Every degree of freedom constrained means `u = 0` is known rather than solved,
so `r_full = -F` bit-exactly and `||e_F||2 = 0`. This is the primary sign gate
and the only case where "exactly" is the right word.

### Scale invariance across nine orders

```text
load 1e-3 N   ->  ||e_F||2 = 4.47e-19 N   eta_F = 2.18e-16
load 1e6 N    ->  ||e_F||2 = 3.61e-10 N   eta_F = 1.76e-16
```

The absolute imbalance moves by nine decades and the ratio does not. That is
the evidence the gate belongs on a ratio; an absolute newton threshold would
pass one of these and fail the other for no physical reason.

### A zero net force with large participating loads: the pure couple

```text
F_external          (0, 0, 0)        to 1e-9 N
scale_F             > 1000 N         asserted
R_total             (0, 0, 0)        to 1e-9 N
eta_F               <= 1e-12
```

This is the case the normalisation was designed for. A denominator of
`||F_external||` would divide by nothing; `scale_F` is a sum of magnitudes and
stays large. The mutation that replaces the scale with the net resultant is
killed here.

## The load cross-check

Two independent routes to the applied force must agree — which triangulates
load preparation, assembly and reaction without this milestone inventing a
third load path.

```text
route                                      summation order
---------------------------------------------------------------------
assembledForceResultant(mesh, numbering,   ROW order, interleaved per node
  system.force())
PreparedLoads::resultantForce()            NodeId order
```

```text
fixture                  assembled (N)        loads (N)            agreement
----------------------------------------------------------------------------
block, 3-component       (250, -400, 650)     (250, -400, 650)     1e-12 rel
RM-MESH-01 pressure      F_z = -16800         F_z = -16800         1e-12 rel
```

Both also agree with the analytical resultant to 1e-9. And a numbering built
for another mesh is **refused** rather than used, which the test asserts.

## Zero load

```text
F_external = 0, R_total = 0, scale_F below the floor
-> eta_F is DEFINED as 0 rather than computed
```

The floor is `1e-12 N` — a dimensioned quantity, not a bare number — and it
exists only to avoid dividing by zero. It is small enough that it cannot mask a
meaningful imbalance: any real support load is many orders above it.

## Unique physical degrees of freedom only

The global total iterates the **union** — `ConstraintSet::constrained()`,
ascending and unique by construction — and each constrained degree of freedom
contributes to exactly one node entry. So a degree of freedom shared by two
restraints is counted **once**, however many restraints reached it.

```text
overlapping fixture
    listed across both restraints      > unique          asserted
    sharedDegreesOfFreedom             > 0               asserted
    global eta_F                       <= 1e-12          PASS
```

Iterating the per-restraint lists instead would overshoot by the shared edge
and break the balance by exactly its share. Three mutations attack this from
different sides (M15, M16, M17) and all three are killed.

## Failure is a failure

`recoverSupportReactions` returns `ForceImbalance` and publishes nothing when
the gate is exceeded. Demonstrated by tightening the threshold below the
measured error: the recovery is refused and the message carries the imbalance
vector, the normalized value, the threshold and the participating scale.
