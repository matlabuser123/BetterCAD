# P17-REACTION-001 — the reaction definition and sign convention

```text
RESULT: PASS
```

## The definition

```text
r_full = K u - F                     over the FULL system

r_full[free]        ~ 0              solver-quality diagnostics
r_full[constrained] = R              the support reaction

ReactionSet = the CONSTRAINED entries of r_full, and nothing else
```

**The quantity already existed.** `SolvedSystem::fullResidual()` is `K u - F`,
computed by the solve in one pass with per-entry finiteness, and its header
retained it for this milestone by name:

```text
"CONSTRAINED ENTRIES ARE THE SUPPORT REACTIONS and are correctly non-zero.
 Retained, not aggregated: P17-REACTION-001 owns reaction semantics and
 equilibrium, and discarding this now would make it recompute what is already
 known."
```

So this module computes no second residual. A grep for `stiffness()` in
`src/structural/StructuralReaction.cpp` finds **nothing**: there is no second
`K u` product, and therefore no way for two definitions to drift apart.

### What was rejected

```text
stress integration over the support faces
    rejected. It introduces stress extrapolation to a surface, facet normal
    selection and surface quadrature -- three error sources the algebra does
    not have -- to approximate a quantity the algebra gives exactly. For a
    displacement-based FE system the constrained residual IS the reaction
recomputing K u - F here
    rejected as duplication, with the drift risk above
the FREE-system residual
    rejected: it is defined on the wrong rows entirely
a penalty or pivot force
    not applicable. There is no penalty method anywhere in P17 (ADR-039)
```

## The sign convention, verified before it was documented

```text
R = the force applied BY the support ON the structure

sum(F_external) + sum(R) = 0
```

Derived from the one-degree-of-freedom case rather than chosen to make a sum
come out zero:

```text
stiffness k, the DOF constrained so u = 0, external load +F

    K u - F  =  k*0 - F  =  -F

so R = -F, and  F_external + R  =  F - F  =  0
```

and physically: push `+F` on a structure held by a support and the support
pushes back `-F`. The algebra and the physics agree, so this is the standard
finite-element convention and **no sign is flipped anywhere in the
implementation**.

### The primary sign gate has no factorisation in it

`StructuralReaction_FullyConstrainedReactionIsMinusTheAppliedLoad` constrains
every face of the block. Every degree of freedom is then constrained, `u = 0`
is known rather than solved, and

```text
r_full = K*0 - F = -F     exactly, every entry
```

so `R_total == -F_external` to the last bit — no solver error, no
conditioning, nothing to argue about.

```text
applied   (300, -500, 700) N          three DISTINCT components, so an
                                      X/Y/Z swap cannot pass
reaction  (-300, 500, -700) N         within 1e-12 relative
sign      asserted per component      x < 0, y > 0, z < 0
||e_F||2  0 N                         EXACTLY zero
eta_F     0
eta_M     0
```

A reaction defined as `F - Ku` instead of `Ku - F` fails this by a factor of
-1, which no tolerance can absorb. The mutation that makes that substitution
is killed by **18 of 20** tests.

### The load applied at a constrained node is still external

The loaded node in that fixture is a corner of the start cap, so it is
restrained. Excluding an applied load because it acts on a support would make
the external total wrong **and** the reaction total wrong by the same amount —
so equilibrium would still pass. That is why it is asserted directly:
`|F_external| == |(300, -500, 700)|` to 1e-12, with the node confirmed fixed.

## The free residual is not a reaction

`r_full` is defined over every degree of freedom. The free entries are small
and **non-zero**, and they are solver diagnostics.

This matters most at a partially restrained node. A face restrained in `Ux`
only has a reaction in `x`; the values at its `Uy` and `Uz` rows are free
residual, and reporting them would invent two support components no restraint
asked for.

```text
measured on a Ux-only side face, with a fixed start cap carrying the rest:

every DOF the Ux-only restraint resolved to     component == Ux   asserted
reported mask at such a node                    Ux only
reported force.y, force.z                       EXACTLY 0.0
largest free-residual magnitude at those rows   > 0, measured
```

The last line is the point: **if the free residual were zero there, reading it
would be harmless and the whole distinction would be untestable.** The test
measures it to show the distinction is real.

The mask is built from what is constrained, so an unconstrained component is
never read from `r_full` at all — it is not read and then zeroed, which is a
stronger property. The mutation that sets every mask to `fixed()` is killed.

## Units

```text
nodal reaction component       Force          N    (SI)
support force resultant        Force3D        N
support moment resultant       Moment3D       N m  (Torque = Energy alias)
force imbalance, norms, scale  Force          N
moment imbalance, norms, scale Torque         N m
equilibrium thresholds         double              dimensionless ratios
force floor                    Force          N
moment floor                   Torque         N m
```

No MPa, no N/mm, no display conversion. The compiler enforces it: four
compile-failure cases prove a force cannot be used as a torque, a norm does not
decay to a number, and the two floors do not interchange — which is what stops
a newton floor being used as a newton-metre one.

## There are no rotational reaction degrees of freedom

A Tet4 node has `Ux, Uy, Uz`. A support region's moment is the moment of its
**translational reaction distribution**:

```text
M_support(O) = sum (x_j - O) x R_j
```

through core's `momentOf(lever, force)` = `lever x force` — the one cross
product in the module, whose signature forces the caller to form `x - O` and so
makes the origin explicit. A grep for a hand-written cross product in
`StructuralReaction.cpp` finds nothing.

`SupportReaction` therefore carries a `Force3D` and **no moment at all**, and
the compile-failure case `nodal-reaction-has-a-moment` proves asking one for a
moment does not compile. A bending moment at a fixed face is represented by the
distribution; claiming a nodal `Mx, My, Mz` would invent a degree of freedom the
element does not have.

The cantilever case validates exactly this: the support supplies a moment of
60.2 N·m through 100-odd translational nodal reactions, and the test
accumulates `(x - O) x R` with **its own** cross product written out, so a
reversed operand order in production cannot agree with it.

## The origin is always explicit

```text
MomentBalance::origin        a field, recorded with every balance
momentAbout(mesh, origin)    a parameter, with no nullary overload
```

A moment about an unstated point is not a quantity. The qualification origin is
the global origin for the headline cases, and equilibrium is additionally
verified about a near offset `(21, -14, 33) mm` and a far one
`(1.7, -2.3, 0.9) m`.

**That non-zero-origin test exists because a mutation survived without it.**
Every other case recovers about the global origin, where `x - O == x`, so a
production path that ignored the origin entirely was a no-op for all of them.
The probe that removed the subtraction survived; the test was added; the probe
is now killed.

### The transfer relation

```text
M(O2) = M(O1) - (O2 - O1) x F
```

is tested with the test's own cross product, and the two moments are asserted
to **differ** so the relation is not being checked on an identity. A reversed
cross product can satisfy the balance about one origin and still fail this,
which is why it is a separate test — and the mutations that negate the moment,
reverse the lever, or drop the origin from either moment accumulator are all
killed.

## Reaction cardinality

```text
OPTION A: one entry per node with at least one constrained degree of freedom.
A node with no restraint is ABSENT, not present with a zero force.
```

Which is the convention `PreparedLoads::nodal()` already uses and the reason
P17-DATA documents `NodalReaction` as "SPARSE BY NATURE". A zero entry would be
indistinguishable from a support carrying no load; a missing node is a node with
no support. The test measures that unrestrained loaded nodes really are absent,
and that the reaction set is smaller than the mesh.

Ordering is the mesh's own: ascending by `NodeId`, asserted with `is_sorted` and
`adjacent_find` on real output. Components within a node are `Ux, Uy, Uz`.
One entry per node means a component cannot be added twice — the mutation that
drops the first constrained degree of freedom is killed by 17 of 20 tests.
