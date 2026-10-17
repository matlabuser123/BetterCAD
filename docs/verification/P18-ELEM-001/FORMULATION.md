# P18-ELEM-001 — the Tet4 thermal formulation, frozen

```text
SUBJECT:  every convention the thermal element kernel uses, written down
          before the implementation so the implementation has no latitude
STATUS:   the specification. src/thermal/ThermalElement.cpp is checked
          against THIS document by the tests, not the other way round.
```

## The contracts this builds on, audited rather than assumed

```text
P16  Tetrahedron        { ElementId id; std::array<NodeId,4> nodes; RegionId region; }
                        "ORIENTATION IS MEANINGFUL AND IS NEVER CANONICALISED ...
                        the sign of the signed volume is the only evidence that
                        a generator produced an inverted element"
P16  Node               { NodeId id; Point3D position; }  -- Point3D of Length,
                        SI METRES internally; "a coordinate cannot be a bare
                        number of unknown unit"
P16  signedVolume       V = (1/6) (p2-p1) . ((p3-p1) x (p4-p1)); "No std::abs
                        anywhere: the sign IS the orientation"
P16  degeneracy rule    MeshValidation: NO TOLERANCE. not finite -> degenerate;
                        exactly zero -> degenerate; negative -> inverted; else
                        valid. "A thin tetrahedron is data-valid and is
                        P16-QUALITY-001's to complain about."
P17  Tet4Element        the structural kernel, in structural/ -- which thermal/
                        MAY NOT INCLUDE (ADR-050, layer 45 below 50). Its
                        Jacobian convention, determinant grouping, gradient
                        mapping, refusal order and no-abs / no-reorder /
                        no-symmetrise / no-regularise rules are ADOPTED here,
                        character for character where the maths is the same.
                        Its B, D, E, nu are structural and do not appear.
P18  ADR-049            Ke = V G^T k G [W/K]; Ce = integral rho cp N^T N dV
                        [J/K]; "whether the capacity matrix is consistent or
                        lumped is P18-ELEM-001's decision ... one policy,
                        stated, and C is symmetric positive definite";
                        anisotropic and temperature-dependent properties are
                        REFUSED scope, not approximated
P18  ThermalMaterial    k is ThermalConductivity [W/(m K)]; the transient pair
                        is ThermalCapacity { Density, SpecificHeatCapacity }
                        with volumetric() = rho cp derived, never stored
P18  ThermalDof         kThermalDofsPerNode = 1; elementDofs() returns the
                        element's own node order, unsorted
core Dimension          power, energy, temperature, thermalConductivity,
                        density, specificHeatCapacity, temperatureGradient
                        all exist, so W/K and J/K are expressible as
                        Quantity types and the dimensional analysis below is a
                        static_assert, not a comment
```

**Shared Tet geometry: REUSED CONVENTION, NOT SHARED CODE.** P17's Jacobian,
determinant, inverse and gradient code lives in an anonymous namespace of
`src/structural/Tet4Element.cpp`, behind a module this one cannot include.
Extracting it to a generic layer would be the right long-term shape and would
require re-qualifying P17-ELEM-001, P17-ASSEMBLY-001, P17-POST-001 and
P17-REFMOD-001 (brief §50, §148). That refactor is NOT done here: P17 is not
destabilised for P18 code reuse. What IS shared is `meshing::signedVolume`,
the one generic qualified geometry utility, and the thermal kernel's
determinant is grouped exactly as it is, so the test
`VolumeAgreesWithMeshingSignedVolumeBitwise` can assert bit-for-bit equality
on every fixture. The extraction is recorded as a candidate for a later
shared-geometry milestone, with the requalification cost named.

## Units — frozen first

```text
coordinates     m        (P16 Node::position, Length, SI)
volume          m^3
grad N          1/m
k               W/(m K)
rho             kg/m^3
cp              J/(kg K)
T               K        (absolute; Celsius is a boundary conversion, ADR-049)
grad T          K/m
Ke              W/K
Ce              J/K
Ke Te           W
```

No millimetre, no viewer coordinate, no display scale, no `* 1000`, no
`/ 1000`, no `1e-3` and no `1e3` appears in the element kernel. The source
audit is in [GEOMETRY.md](GEOMETRY.md), and the scale laws (`Ke ∝ s`,
`Ce ∝ s^3` under `x' = s x`) are the test that would expose a hidden
conversion.

## Local node order and local temperature order

```text
nodes            [n1, n2, n3, n4]   EXACTLY as the P16 Tetrahedron stores them
                                    (and exactly as P18-DOF's elementDofs()
                                    returns their unknowns)
Te               [T1, T2, T3, T4]^T
Ke, Ce rows/cols T1 T2 T3 T4
```

No sorting by NodeId, by coordinate or by unknown inside the kernel. An even
permutation of the input permutes the matrices covariantly
(`Ke' = P^T Ke P`); an odd permutation inverts the orientation and is REFUSED.

## Reference element and shape functions

```text
parent domain   xi >= 0, eta >= 0, zeta >= 0, xi + eta + zeta <= 1
node 1 (0,0,0)   node 2 (1,0,0)   node 3 (0,1,0)   node 4 (0,0,1)

N1 = 1 - xi - eta - zeta        grad_ref N1 = (-1, -1, -1)^T
N2 = xi                         grad_ref N2 = ( 1,  0,  0)^T
N3 = eta                        grad_ref N3 = ( 0,  1,  0)^T
N4 = zeta                       grad_ref N4 = ( 0,  0,  1)^T
```

Linear, so every gradient is constant over the element; `G` is constant and
the conduction integrand is constant.

## Jacobian convention — FROZEN

```text
x(xi, eta, zeta) = x1 + xi (x2 - x1) + eta (x3 - x1) + zeta (x4 - x1)

        | dx/dxi  dx/deta  dx/dzeta |     | x2-x1  x3-x1  x4-x1 |
    J = | dy/dxi  dy/deta  dy/dzeta |  =  | y2-y1  y3-y1  y4-y1 |
        | dz/dxi  dz/deta  dz/dzeta |     | z2-z1  z3-z1  z4-z1 |

    J[i][j] = d x_i / d xi_j   -- the COLUMNS are the three edge vectors
                                 from node 1. Stored row-major, so the
                                 numeric array is exactly the matrix above.
```

For the reference element `J = I`. The transpose convention would also work;
this one is chosen, it is P17's, and every formula below uses it. A
transposed `J` with an un-adjusted gradient mapping is the defect the
general-tetrahedron oracle exists to catch.

## Signed determinant and volume

```text
det J   = (p2-p1) . ((p3-p1) x (p4-p1))     columns e1, e2, e3: e1 . (e2 x e3)
V       = det J / 6
```

Computed WITHOUT absolute value, grouped exactly as `meshing::signedVolume`
groups it -- `cross` then `dot` summed left to right, read from
`src/meshing/Mesh.cpp`:

```text
cross(a,b) = { a.y b.z - a.z b.y,  a.z b.x - a.x b.z,  a.x b.y - a.y b.x }
dot(a,b)   = a.x b.x + a.y b.y + a.z b.z
```

P17 found that a cofactor expansion along the first row differs by one ulp
on a skew tetrahedron, and chose to match the association rather than relax
the bit-for-bit test, because the two are predicates on the same boundary.
The same choice is made here, for the same reason.

## Orientation and degeneracy policy — FROZEN

```text
any coordinate not finite       NonFiniteCoordinate     refused before arithmetic
det J not finite                DegenerateElement       P16's classification
|det J| <= B                    DegenerateElement       coplanar / coincident:
                                                        the value is rounding noise
det J / 6 == 0 (underflow)      DegenerateElement       P16's own volume test
det J <  -B                     InvertedElement         REFUSED, NEVER REORDERED
det J >   B                     valid                   compute
a gradient or a matrix entry
  not finite although valid     NonFiniteResult         a result check

B = 16 * eps * |e1| |e2| |e3|        eps = 2^-52, e_i = corner_{i+1} - corner_1
```

### The threshold, and where it departs from P16 and P17

**P16 and P17 use exact zero.** `MeshValidation.hpp`: "there is no tolerance
in this file"; `P17-ELEM-001/TET4_DERIVATION.md`: "there is no scale-aware
policy, because there is no tolerance at all". The brief asks for a
scale-aware criterion derived from geometric scale and floating precision if
a new one is required, and says not to loosen it to make tests pass. **A new
one IS required, and the evidence is a failing test, not a preference:**
`CoincidentNodeTetRejected` built a tetrahedron from the brief's general
fixture with two corners placed at the same point, and the exact-zero rule
ACCEPTED it for corners 2 and 3 and corners 2 and 4. With
`e1 == e2`, `e1 . fl(e1 x e3)` is not zero in floating point: it is rounding
noise of either sign, so the sign test called a zero-volume tetrahedron
"valid" or "inverted" at random, and the matrices that followed were finite
garbage (gradients of order 1/noise). Exact zero is correct only where the
arithmetic is exact, such as the integer-coordinate fixtures P17 tested. This
is a latent gap in the predecessor rule, recorded here, not fixed in P17
(brief §50, §148: P17 is not destabilised for P18).

**The bound.** `det J` is evaluated as `e1 . fl(e2 x e3)`. With
`u = 2^-53`: each cross component is a difference of two products and is
correct to `2u |e2||e3|` (Cauchy-Schwarz bounds the two product magnitudes by
`|e2||e3|`); the dot product adds `3u |e1||e2||e3|` and carries the cross
error through at most `2u |e1||e2||e3|`. The computed value differs from the
true one by at most about `8u |e1||e2||e3| = 4 eps |e1||e2||e3|`. A computed
`|det J|` at or below that is indistinguishable from the true zero, and its
sign says nothing. The factor 16 is four times that bound. Units: `m^3`
(a product of three lengths), the same as `det J`.

**Scale-aware by construction.** `B` is homogeneous of degree three in the
coordinates, as `det J` is, so scaling a tetrahedron by any `s` scales both
by `s^3` and cannot change the verdict. It is not an absolute constant, and
it is relative to the element's own edges rather than to the model.

**Not a quality rule.** `B / (|e1||e2||e3|) = 3.6e-15`. A 1000:1 sliver has
`|det J| / (|e1||e2||e3|) ~ 2e-3`, twelve orders above it, and a needle or
wedge that P17-VALID-001 rejects on measured accuracy is far above it too.
What a thermal analysis accepts as a mesh is P18-VALID-001's, and no element
a quality-gated mesh admits can be refused by this bound. Above it, the
classification is **exactly P16's**: same expression, same association, same
sign. The measured margins (largest rounding noise actually produced by exact
coplanar and coincident fixtures, against `B`) are in
[DEGENERACY.md](DEGENERACY.md) and printed by `NearDegeneratePolicy`.

**Overflow and underflow.** An infinite `|e1||e2||e3|` (edges beyond ~1e102)
gives `B = inf`, a degenerate verdict, which is correct because the volume
overflows anyway. A determinant above `B` whose `/6` underflows to zero is
degenerate by P16's own volume test. A determinant that is valid but whose
reciprocal overflows (a needle of subnormal height) passes the geometry test
and is refused by the finiteness gate as `NonFiniteResult`.

No `std::abs(det)` repairs an inversion -- `std::abs` appears once, in the
magnitude comparison against `B`, and the sign test that follows reads the
SIGNED value. No node swap, no `std::sort`. The audit of the kernel for those
tokens is in [DEGENERACY.md](DEGENERACY.md).

## Physical shape-function gradients and G

```text
grad_x Ni = J^{-T} grad_ref Ni          (J^{-1} by adjugate over det J)

G = | dN1/dx  dN2/dx  dN3/dx  dN4/dx |
    | dN1/dy  dN2/dy  dN3/dy  dN4/dy |       3 x 4; row = axis, column = node
    | dN1/dz  dN2/dz  dN3/dz  dN4/dz |

grad T = G Te                            K/m
```

All four gradients are computed by the full product; `grad N1` is NOT taken
as `-(grad N2 + grad N3 + grad N4)`, because that would make the
partition-of-unity test `G [1 1 1 1]^T = 0` hold by construction rather than
by measurement.

Identities, each a test: `sum_i grad Ni = 0`; `T_i = x_i` gives
`grad T = e_x` (and `y`, `z`); `T_i = a + b x_i + c y_i + d z_i` gives
`grad T = (b, c, d)`; a constant field gives zero.

## Conduction matrix — FROZEN

```text
Ke = V k G^T G            Ke_ij = k V (grad Ni . grad Nj)         4 x 4, W/K
```

Exact, not integrated: both factors are constant over a Tet4. Properties,
each a test rather than enforced by construction:

```text
symmetric              by construction of grad Ni . grad Nj -- NOT symmetrised
                       afterwards; there is no 0.5 (K + K^T) anywhere
row sums zero          because sum_j grad Nj = 0
constant null mode     Ke [1 1 1 1]^T = 0; only temperature DIFFERENCES conduct
positive semidefinite  x^T Ke x >= 0; rank 3, nullity 1; NOT positive definite,
                       and nothing regularises the diagonal to make it so
diagonal positive      k V |grad Ni|^2 > 0 for a valid element
energy identity        Te^T Ke Te = k V |G Te|^2
scaling                Ke(c k) = c Ke(k);  Ke(s x) = s Ke(x)
invariance             translation; proper rotation (det R = +1), because
                       G' = R G and G'^T G' = G^T R^T R G = G^T G
```

Off-diagonal signs are NOT asserted: for an obtuse tetrahedron an
off-diagonal term can be positive, so an M-matrix pattern is not a universal
invariant (brief §105).

**Conductivity contract.** `k` finite and `k > 0`, refused as
`InvalidConductivity` otherwise. P15 refuses an unusable `k` at document
entry (P18-MAT-001), so a conductivity that reached here through
`ThermalMaterial::conductivity()` cannot be out of range; the check exists
because `ThermalConductivity` is a plain `Quantity` any caller can construct,
and a negative `k` would produce a negative-semidefinite `Ke` without any
other symptom. No `abs(k)`, no `max(k, eps)`.

## Capacity matrix — FROZEN, and the policy decided

```text
P18 initial capacity policy = CONSISTENT

Ce = integral_V rho cp N^T N dV
   = rho cp V / 20 * | 2 1 1 1 |         4 x 4, J/K
                     | 1 2 1 1 |
                     | 1 1 2 1 |
                     | 1 1 1 2 |

   because, for a linear Tet4 of volume V,
   integral Ni^2 dV   = V/10          Ce_ii = rho cp V / 10
   integral Ni Nj dV  = V/20, i != j  Ce_ij = rho cp V / 20
```

Written as the exact closed form. NOT integrated numerically: `Ni Nj` is
quadratic, so a one-point centroid rule (`Ni = 1/4`) gives
`rho cp V / 16` in EVERY entry -- a rank-one matrix with the correct row
sums and the wrong diagonal -- and that is the trap the capacity tests are
written to catch (brief §84-§87). Properties, each a test:

```text
symmetric              by construction; not symmetrised
diag = 2 x off-diag    Ce_ii = 2 Ce_ij
row sum                rho cp V / 4
sum of all entries     rho cp V       (ones^T Ce ones: total heat capacity)
trace                  2 rho cp V / 5
off-diagonals          all > 0        (catches accidental lumping)
positive definite      eigenvalues rho cp V / 4 (once) and rho cp V / 20
                       (three times): the eigenvalues of [2 1 1 1; ...] are
                       5, 1, 1, 1, scaled by rho cp V / 20
rank                   4              (the centroid matrix has rank 1)
scaling                Ce(c rho) = c Ce;  Ce(c cp) = c Ce;  Ce(s x) = s^3 Ce
invariance             translation; proper rotation (V is invariant)
no null mode           Ce [1 1 1 1]^T != 0, unlike Ke
```

### Why CONSISTENT, and what ADR-049 asked for

ADR-049 left the choice to this milestone "with the measurement that decides
it". The decision is made on the following grounds, and the measurement is
the one the tests make:

1. The consistent matrix IS the Galerkin form `integral rho cp N^T N dV`
   that ADR-049 commits to; lumping (`rho cp V / 4 * I`, the row-sum
   diagonal of the same matrix) is an approximation of it, and an
   approximation is not adopted silently in the milestone whose job is the
   exact local formulation.
2. Both are symmetric positive definite, so ADR-049's one hard requirement
   on `C` is met either way; the consistent one is the one with the
   analytically known spectrum `{rho cp V / 4, rho cp V / 20 (x3)}` that the
   tests pin, and pinning the full matrix -- not only its row sums -- is
   what makes the one-point-quadrature and the silent-lumping mutations
   detectable (MUTATION_PROTECTION in ADVERSARIAL_REVIEW.md).
3. The known trade-off is recorded rather than hidden: with a consistent
   capacity and backward Euler, a time step below a mesh-dependent bound
   can produce a non-monotone first step (the discrete maximum principle is
   not guaranteed), where a lumped capacity would be monotone. That is a
   property of the TIME INTEGRATION on the assembled system and is
   P18-TRANSIENT-001's to measure against the 1D slab and the lumped-body
   benchmarks. If lumping is ever wanted it is a separate, explicit,
   separately qualified policy with its own ADR -- not a hidden global
   setting, not a second overload of `capacityMatrix()`, and not a change
   to this kernel's frozen formula.

The policy is named in code (`kTet4CapacityPolicy == "consistent"`) so that
a later assembler can record which formulation its `C` carries.

**Capacity contract.** `rho` finite and `> 0` (`InvalidDensity`), `cp`
finite and `> 0` (`InvalidSpecificHeat`), and `rho cp V` finite
(`NonFiniteResult`: two finite positives can overflow). No `abs`, no
substitution. The pair is taken as P18-MAT-001's `ThermalCapacity`, which
is the type P15's transient consumer hands over; `rho cp` is derived
arithmetic here and is not a new material authority.

## Steady needs no capacity; capacity needs no conductivity

```text
computeTet4Geometry(corners)              -> V, G              geometry only
computeTet4Conduction(geometry, k)        -> Ke                geometry + k
computeTet4Capacity(geometry, capacity)   -> Ce                geometry + rho, cp
```

Three stateless computations, as P17's kinematics / elasticity / stiffness
split and for the same reason: a steady analysis holds a `ThermalMaterial`
whose `capacity()` is empty, and it must be able to form `Ke` without one;
a transient one forms both from one geometry. There is no combined call
that could let a missing `rho` block a valid `Ke`.

## Dimensional analysis — a static_assert, not a comment

```text
Ke:   V            m^3
      G^T G        1/m^2
      k            W/(m K)
      m^3 . (1/m^2) . W/(m K)  =  W/K

Ce:   rho          kg/m^3
      cp           J/(kg K)
      V            m^3
      kg/m^3 . J/(kg K) . m^3  =  J/K

grad T:  (1/m) . K          =  K/m
Ke Te:   (W/K) . K          =  W
```

Each line is a `static_assert` in `ThermalElement.hpp` over the `Quantity`
dimensions, so the compiler checks the reasoning.

## What the kernel does not do

No global assembly, no NodeId or ThermalDofIndex arithmetic (a 4 x 4 matrix
is 4 x 4 whatever the global numbering), no boundary condition, no load, no
solve, no flux recovery, no quality judgement, no persistence, no CLI, no
anisotropy (`Ke = V G^T K G` with a tensor is DEFERRED, refused by ADR-049),
no temperature-dependent property (DEFERRED, ADR-049/ADR-027), no quadrature
loop, no cache, no static scratch state, no tolerance other than the
exact-zero determinant, and no comparison tolerance in production.
