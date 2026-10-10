# P17-REACTION-001 — moment equilibrium

```text
RESULT: PASS
GATE:   eta_M = ||M_external(O) + M_R(O)||2 / scale_M  <=  1e-12
SCALE:  scale_M = sum |(x_i - O) x F_i| + sum |(x_j - O) x R_j|
ORIGIN: explicit, always. A field on MomentBalance and a parameter with no
        nullary overload.
```

A moment about an unstated point is not a quantity, which is what core's
`momentOf(lever, force)` already insists on by taking a **relative** lever —
so the caller must form `x - O` and the operand order is fixed by the
signature.

## There are no rotational reaction degrees of freedom

A Tet4 node has `Ux, Uy, Uz`. A support region's moment is the moment of its
**translational reaction distribution**:

```text
M_support(O) = sum (x_j - O) x R_j
```

`SupportReaction` carries a `Force3D` and no moment, and the compile-failure
case `nodal-reaction-has-a-moment` proves asking one for a moment does not
compile. A bending moment at a fixed face is represented by the distribution;
claiming a nodal `Mx, My, Mz` would invent a degree of freedom the element does
not have.

## The required table

```text
fixture                    origin              |M_external|   |M_reaction|   ||e_M||2     eta_M       PASS
-------------------------------------------------------------------------------------------------------------
fully constrained          global              61.84 scale    opposes        0            0           yes
cantilever, nodal +Z       global              60.16 scale    opposes        2.22e-15     3.69e-17    yes
cantilever, 3-component    global             115.31 scale    opposes        3.86e-14     3.35e-16    yes
cantilever, 1e-3 N         global            6.02e-5 scale    opposes        6.54e-21     1.09e-16    yes
cantilever, 1e6 N          global              60158 scale    opposes        4.41e-12     7.34e-17    yes
cantilever, non-zero O     (21,-14,33) mm      differs        opposes        --           <= 1e-12    yes
cantilever, far O          (1.7,-2.3,0.9) m    > 10x global   opposes        --           <= 1e-12    yes
pure couple                global              > 1 N m        > 1 N m        --           <= 1e-12    yes
RM-MESH-01 pressure        global              --             opposes        --           <= 1e-12    yes
RM-MESH-01 pressure        face centroid       ~0             ~0             --           1e-9 rel    yes
RM-MESH-01 gravity         global          (-0.792, 1.358, 0) opposes        --           <= 1e-12    yes
RM-MESH-06 base            global            3334.52 scale    opposes        --           4.82e-17    yes
RM-MESH-06 placed          global            2972.53 scale    opposes        --           8.67e-17    yes
RM-MESH-02 large           global             485.20 scale    opposes        --           2.50e-14    yes
```

`scale` is the participating magnitude sum; `--` marks a value the fixture
does not fix analytically. The component imbalance `(M_x, M_y, M_z)` is kept
as a vector and reported, because a norm can hide a cancellation.

## The nonzero-moment cases, which is what makes the gate informative

Brief 34 warns that a moment check can be uninformative if all forces act
through the origin or the moment scale is ~0. **Every case above has a
nonzero moment scale**, and the headline cases assert it:

```text
cantilever          |M_external| > 1 N m and |M_reaction| > 1 N m, asserted --
                    a vacuous-instrument guard, because a zero-moment case
                    would make the balance hold trivially
gravity             M_external = (-0.792147, 1.357965, 0) N m, matched to
                    1e-9 x scale, with both components > 0.1 N m and
                    ASSERTED DIFFERENT from each other so an axis swap could
                    not reproduce them
pure couple         |M_external| > 1 N m with ZERO net force
```

## The pure couple: force balance alone would miss a defect

```text
equal and opposite loads on two opposite side faces

sum(F_external) = 0        to 1e-9 N, asserted
scale_F         > 1000 N   asserted -- the loads do not cancel, only the net
sum(M_external) > 1 N m    asserted
sum(M_reaction) = -sum(M_external)   to 1e-9 x scale
eta_M           <= 1e-12
```

An implementation that checked only force equilibrium would pass this with
**any** moment error at all. That is why the moment gate is sequential and
independent, and why the mutation that removes it is killed.

## The cantilever: a distributed support moment

The start cap is fixed and the end cap loaded transversely, so the support must
supply a moment — through roughly a hundred translational nodal reactions.

```text
applied        1200 N along +Z on the end cap
R_total        -1200 N, to 1e-9 rel
M_reaction     accumulated INDEPENDENTLY in the test with its own cross
               product written out, and compared to the reported value within
               1e-9 x scale
```

The independent accumulation is what a reversed operand order in production
cannot agree with. M12 — the reversed lever — is killed by 16 tests.

## The origin, and why one test exists at all

```text
M(O2) = M(O1) - (O2 - O1) x F
```

is asserted with the test's own cross product, and the two moments are required
to **differ** so the relation is not checked on an identity. A reversed cross
product can satisfy the balance about one origin and fail this.

**And equilibrium is verified about three origins**, because a mutation proved
one was not enough:

```text
global (0,0,0)              x - O == x, so a path that IGNORED the origin was
                            a no-op and the probe SURVIVED
(21, -14, 33) mm            near offset
(1.7, -2.3, 0.9) m          far offset, > 10x the global moment
```

with two guards: the three external moments must differ pairwise, and the far
one must exceed ten times the global one. See
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md) finding M13.

### The consistency brief 98 asks for

```text
M'_imbalance = M_imbalance - dO x F_imbalance
```

With `F_imbalance` already ~0, the second term vanishes and the moment
imbalance must stay ~0 under any change of origin. Measured at three origins
spanning four orders of lever arm: it does.

## The far-origin cancellation case

A moment about a distant origin is a sum of large terms whose total is small —
where catastrophic cancellation would appear.

```text
fixture                 farthest node    scale_M (N m)   eta_M
-------------------------------------------------------------------
RM-MESH-06 base         0.108171 m       3334.52         4.82e-17
RM-MESH-06 placed       0.126098 m       2972.53         8.67e-17
```

No degradation at the coordinate magnitudes real CAD models occupy. Force
equilibrium is unaffected by construction, having no lever arm in it; the test
asserts both anyway.

## The pressure case about its own centroid

A uniform pressure on a planar face acts through the face centroid, so it
produces **no moment about that centroid** — and the support's moment about the
same point must therefore also be ~zero. That is a different statement from
balancing about the origin, and one a reversed cross product would not satisfy.

```text
origin = (60, 35, 35) mm, the centroid of the 120 x 70 mm top face
M_reaction about it  ==  -M_external about it,  within 1e-9 x scale
```

## Zero and near-zero moment scale

```text
scale_M below the 1e-12 N m floor  ->  eta_M is DEFINED as 0 rather than
                                       computed
```

The floor carries its own dimension — newton-metres, not the force floor's
newtons, and not one bare number for both. A compile-failure case proves the
two do not interchange.
