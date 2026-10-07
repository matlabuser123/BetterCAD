# P17-ELEM-001 — the Tet4 derivation, frozen

```text
SUBJECT:  every convention the element kernel uses, written down before the
          implementation so the implementation has no latitude
```

This document is the specification. It was written before
`src/structural/Tet4Element.cpp` and the tests check the implementation against
**these** statements, not the other way round.

## The P16 contract this builds on, audited rather than assumed

```text
Tetrahedron           { ElementId id; std::array<NodeId,4> nodes; RegionId region; }
                      "ORIENTATION IS MEANINGFUL AND IS NEVER CANONICALISED ...
                      the sign of the signed volume is the only evidence that a
                      generator produced an inverted element"

signedVolume(p1..p4)  V = (1/6) (p2-p1) . ((p3-p1) x (p4-p1))
                      "No std::abs anywhere: the sign IS the orientation"
                      "Not finite in, not finite out: the caller validates"

node coordinates      Point3D of Length, SI metres, in the body's own frame.
                      "A mesh is in its body's coordinate frame and carries no
                      transform" (ADR-032)

connectivity order    NOT a textbook ordering claim. P16 stores the four
                      handles as the generator produced them and refuses a
                      negative signed volume, so the only guarantee is
                      V > 0 for the stored order -- which is exactly what the
                      orientation convention below needs
```

**The degeneracy criterion has NO TOLERANCE, and that is deliberate.**
`MeshValidation.hpp` says so in as many words:

> there is no tolerance in this file. An element is refused for being
> degenerate when its volume or area is exactly zero, or not finite — not for
> being thin. A thin tetrahedron is data-valid and is P16-QUALITY-001's to
> complain about.

and the predicate, read from `src/meshing/MeshValidation.cpp`, is exactly:

```cpp
if      (!isFinite(volume))  -> DegenerateTetrahedron   // "signed volume is not finite"
else if (volume.si() == 0.0) -> DegenerateTetrahedron   // "four nodes are coplanar"
else if (volume.si() <  0.0) -> InvertedTetrahedron
else                            valid
```

with its own reasoning for the first branch: *"Finite coordinates can still
overflow the triple product. A non-finite determinant is never evidence of a
valid element, so it is refused rather than compared against zero — a
comparison an infinity would answer 'greater'."*

So the brief's instruction to "audit P16's scale-aware degeneracy policy" has a
different answer than it anticipates: **there is no scale-aware policy, because
there is no tolerance at all.** P17-ELEM therefore applies the *same* rule, and
inventing a tolerance here would do two things the project forbids: create a
second definition of element validity, and refuse elements a qualified P16 mesh
publishes. A near-degenerate tetrahedron with positive volume is **accepted**,
and its conditioning is reported rather than refused — which is also what the
brief's own §49 and §50 require (`invalid` is not `poor quality`).

## Reference element

```text
parent domain     xi >= 0, eta >= 0, zeta >= 0, xi + eta + zeta <= 1

node 1            (0, 0, 0)
node 2            (1, 0, 0)
node 3            (0, 1, 0)
node 4            (0, 0, 1)
```

## Shape functions

```text
N1 = 1 - xi - eta - zeta
N2 = xi
N3 = eta
N4 = zeta
```

Properties, each a test:

```text
sum Ni = 1                       partition of unity
Ni(node j) = delta_ij            nodal Kronecker property
```

## Reference gradients

```text
          d/dxi   d/deta  d/dzeta
N1         -1       -1       -1
N2          1        0        0
N3          0        1        0
N4          0        0        1
```

Constant: a Tet4's shape functions are linear, so every derivative is a
constant over the whole element. Hence `B` is constant and **no quadrature loop
is needed** — `Ke = V B^T D B` exactly, not approximately.

## Jacobian convention — FROZEN

With `x(xi, eta, zeta) = sum Ni xi_node`:

```text
        | dx/dxi   dx/deta   dx/dzeta |     | x2-x1  x3-x1  x4-x1 |
    J = | dy/dxi   dy/deta   dy/dzeta |  =  | y2-y1  y3-y1  y4-y1 |
        | dz/dxi   dz/deta   dz/dzeta |     | z2-z1  z3-z1  z4-z1 |
```

**`J[i][j] = d x_i / d xi_j`. Its COLUMNS are the three edge vectors from node
1.** The transpose convention would also work; this one is chosen and every
formula below uses it. For the reference element `J = I`.

### Determinant and volume

A determinant whose columns are `e1, e2, e3` is `e1 . (e2 x e3)`, so

```text
    det J = (p2-p1) . ((p3-p1) x (p4-p1))
    V     = det J / 6
```

which is the same triple product `meshing::signedVolume` computes. There is no
`std::abs` anywhere — the sign is the orientation.

**And the ASSOCIATION matters, not only the algebra.** The first
implementation expanded cofactors along `J`'s first row. That is the same
number mathematically and came out **one ulp different** on a skew
tetrahedron, because floating-point addition is not associative — the test
asserting bit-for-bit agreement caught it. The determinant is therefore
grouped exactly as P16 groups it, `cross` then `dot` summed left to right, read
from `src/meshing/Mesh.cpp`:

```text
cross(a,b) = { a.y b.z - a.z b.y,  a.z b.x - a.x b.z,  a.x b.y - a.y b.x }
dot(a,b)   = a.x b.x + a.y b.y + a.z b.z
```

Matching the association was chosen over relaxing the test because the two
functions are predicates on the same boundary: a tetrahedron whose volume
rounds to within an ulp of zero must not be data-valid for `MeshValidation`
and inverted for this kernel, or a qualified mesh would contain an element the
solver refuses. Exact agreement makes that impossible rather than unlikely.

### Physical gradients

```text
    grad_x N = J^-T grad_xi N
```

Derivation, so the transpose is not a guess:

```text
    dN/dx_i = sum_j (dN/dxi_j)(dxi_j/dx_i)
            = sum_j (J^-1)_{j i} (dN/dxi_j)
            = (J^-T grad_xi N)_i
```

`J^-1` rather than `J^-T` here is the single most likely transposition error in
the milestone, so it has its own mutation probe and its own independent
reference check. For the reference element `J^-T = I` and

```text
    grad N1 = (-1, -1, -1)      grad N3 = ( 0,  1,  0)
    grad N2 = ( 1,  0,  0)      grad N4 = ( 0,  0,  1)
```

which is asserted exactly.

Because `sum Ni = 1` for all `x`, `sum grad Ni = 0` — tested on every fixture
as a geometry-gradient sanity check that needs no reference values.

## Voigt ordering — inherited, not chosen

From `structural::TensorComponent`, frozen by **P17-DATA-001**:

```text
strain   [ exx  eyy  ezz  gxy  gyz  gzx ]^T       ENGINEERING shear
stress   [ sxx  syy  szz  txy  tyz  tzx ]^T

gxy = dux/dy + duy/dx = 2 exy
gyz = duy/dz + duz/dy = 2 eyz
gzx = duz/dx + dux/dz = 2 ezx
```

This milestone defines **no** ordering of its own. `TensorComponent` and
`Strain6` / `Stress6` already exist, and `Strain6` carries the convention in
its FIELD NAMES — `gammaXy`, not `xy` — precisely so that the factor of two
cannot be lost between two milestones. `P17-POST-001` will read the same
types. The shared definition is the single source, and a test asserts that
`B`'s row order agrees with `TensorComponent`'s integer values.

## B — FROZEN

For node `i` with `grad Ni = (bi, ci, di)`:

```text
          | bi   0    0  |
          | 0    ci   0  |
    Bi =  | 0    0    di |          B = [ B1 B2 B3 B4 ]      6 x 12
          | ci   bi   0  |
          | 0    di   ci |
          | di   0    bi |
```

Row order is `exx eyy ezz gxy gyz gzx`. The shear rows are the error-prone
part, so each of the three has its own pure-shear test and the `gyz`/`gzx` swap
has its own mutation probe.

Units: `1/m`.

## Local DOF ordering — inherited from P17-DOF-001

```text
    local index = kDofsPerNode * node + offsetOf(component)

    0  node1 Ux      3  node2 Ux      6  node3 Ux      9  node4 Ux
    1  node1 Uy      4  node2 Uy      7  node3 Uy     10  node4 Uy
    2  node1 Uz      5  node2 Uz      8  node3 Uz     11  node4 Uz
```

Interleaved per node, which is ADR-037's frozen convention and the reason it is
frozen: an element's twelve indices come from four short runs. The helper is
written **in terms of `kDofsPerNode` and `offsetOf`**, which are P17-DOF's own,
so the local order cannot drift from the global one. A test asserts the
correspondence index by index.

## D — FROZEN

```text
    lambda = E nu / ((1 + nu)(1 - 2 nu))
    mu     = E / (2 (1 + nu))
```

```text
        | l+2m   l     l     0   0   0 |
        | l     l+2m   l     0   0   0 |
    D = | l     l     l+2m   0   0   0 |
        | 0     0     0      m   0   0 |
        | 0     0     0      0   m   0 |
        | 0     0     0      0   0   m |
```

**`mu` on the shear diagonal, not `2 mu`.** With engineering shear the factor
of two is already in `gamma`, so `tau_xy = mu gamma_xy`. Putting `2 mu` there
would double every shear stress — the single most common Voigt-convention
defect — so it has a dedicated regression test and a dedicated mutation probe.

### mu is NOT recomputed

**`materials::LinearElasticConstants::shearModulus` already IS `mu`.** Read
from `src/core/materials/MechanicalProperties.cpp`:

```cpp
derivedShearModulus:  modulus / (2.0 * (1.0 + ratio))
derivedBulkModulus:   modulus / (3.0 * (1.0 - 2.0 * ratio))
```

The first is character-for-character the brief's `mu`. So P17-ELEM **uses
P15's value** rather than computing its own — the fourth milestone running in
which the audit found the quantity already built. `lambda` is not provided and
is computed from the frozen formula, and the identity

```text
    lambda = K - 2 mu / 3
```

is an independent cross-check against P15's *other* derived constant. Verified
algebraically:

```text
K - 2mu/3 = E/(3(1-2nu)) - E/(3(1+nu))
          = (E/3) [ (1+nu) - (1-2nu) ] / ((1-2nu)(1+nu))
          = (E/3) (3 nu) / ((1-2nu)(1+nu))
          = E nu / ((1+nu)(1-2nu))                            = lambda
```

Units: `Pa`.

## Ke — FROZEN

```text
    Ke = V B^T D B                12 x 12
```

Exact, not quadrature: `B` and `D` are both constant over a Tet4.

### Dimensions, proved by the compiler

```text
    V        m^3
    B        1/m
    D        Pa = N/m^2

    V B^T D B    m^3 . (1/m) . (N/m^2) . (1/m)  =  N/m
```

This is not left to a comment. The header carries

```cpp
static_assert(decltype(Volume{} * InverseLength{} * Stress{} * InverseLength{})::dimension
              == dimensions::force / dimensions::length);
```

so the dimensional reasoning is checked by the type system at compile time. The
matrices themselves store `double` in SI — see `README.md` on why — and the
accessors are typed, so `stiffness(i, j)` returns a `Stiffness`, which is
`Quantity<force/length>`.

## Validity — P17 checks it itself

P16's adapter already corrects Netgen's inverted node order with one swap, so a
mesh arriving here has positive signed volume. **P17 verifies it anyway**, and
the brief is right that it must: the guarantee protects meshes that came through
P16, not synthetic test tetrahedra, future callers, or an upstream regression.

```text
a node coordinate is not finite      -> NonFiniteCoordinate
the signed volume is not finite      -> DegenerateElement
the signed volume is exactly zero    -> DegenerateElement
the signed volume is negative        -> InvertedElement
a computed gradient is not finite    -> NonFiniteResult
otherwise                               accept
```

The first four are P16's rule, in P16's order, with P16's classification —
including putting a non-finite volume under *degenerate* rather than inventing
a fifth kind. The fifth is a **result** check rather than an invented input
tolerance: it catches a positive but tiny determinant whose reciprocal
overflows, which no P16 mesh can produce but a synthetic fixture can.

**An inverted tetrahedron is REJECTED, never reordered.** P16 owns mesh
correction; P17 rejects invalid solver input. A silent reorder here would
destroy the evidence that something upstream was wrong, which is the same
argument `Mesh` makes for never canonicalising orientation.

## What this milestone does not do

```text
no global assembly        P17-ASSEMBLY-001
no loads, no restraints   P17-LOAD-001, P17-BC-001
no solve                  P17-SOLVE-001
no stress recovery        P17-POST-001, which will CONSUME the B and D defined
                          here rather than reimplement them
no quality policy         P16-QUALITY-001 measures, P17-VALID-001 decides
no persistence            B, D and Ke are derived state
no material database      P15 owns it; the element takes resolved constants
```
