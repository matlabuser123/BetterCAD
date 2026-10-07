# P17-ELEM-001 — numerical validation

```text
SUBJECT:  the measured behaviour of the element kernel, in the tables the
          brief asks for. Every number here was printed by a test; none was
          chosen to look good.
```

Values are quoted as the tests report them, via `INFO` under
`bettercad_tests "[elem]" -s`.

## The analytical unit tetrahedron

```text
nodes            (0,0,0) (1,0,0) (0,1,0) (0,0,1)    metres
det J            1                                   EXACT
V                1/6                                 EXACT

grad N1          (-1, -1, -1)                         EXACT
grad N2          ( 1,  0,  0)                         EXACT
grad N3          ( 0,  1,  0)                         EXACT
grad N4          ( 0,  0,  1)                         EXACT
```

`EXACT` means asserted with `==` and not with a tolerance: for the reference
element `J = I`, so `J^-T` is the identity and every gradient is a small
integer. A tolerance there would have hidden a wrong transpose.

With `E = 1 Pa`, `nu = 0`:

```text
lambda           0           EXACT  (1 * 0 / ((1)(1)))
mu               0.5         EXACT
D                diag(1, 1, 1, 1/2, 1/2, 1/2)         EXACT, entry by entry

max |B  - B_analytical|      0
max |D  - D_analytical|      0
max |Ke - Ke_analytical|     <= 1e-15 (asserted); measured 0
```

The expected `Ke` is formed in the test from the **analytical** gradients and
the **analytical** `D`, with the `B` block written out a second time, and
multiplied with Eigen. Nothing in the expectation comes from production: a
pasted production matrix is not analytical evidence.

Two entries are additionally written as rationals so the chain can be checked
by hand:

```text
Ke(3,3)   node 1 Ux against itself.  grad N2 = (1,0,0), so only the exx row
          contributes: V * D(0,0) * 1 * 1 = (1/6)(1) = 1/6
Ke(3,7)   node 1 Ux against node 2 Uy.  Only the gxy row couples them, with
          c1 * b2 = 0 * 1 = 0, so the entry is 0
```

## Independent reference implementation

A different route, described in `INDEPENDENT_REFERENCE.md`: a 4x4
coordinate-matrix inverse instead of a 3x3 Jacobian, `lambda` and `mu`
recomputed from `E` and `nu` instead of taken from P15, Eigen products instead
of hand loops. Relative errors, worst entry:

| Fixture | V | gradients | B | D | Ke |
| --- | --- | --- | --- | --- | --- |
| unit, `E=1 nu=0` | 0 | 0 | 0 | 0 | 0 |
| unit, steel | 0 | 0 | 0 | 0 | 0 |
| skew, steel | 0 | 1.2e-16 | 1.2e-16 | **0** | 1.2e-16 |
| skew, auxetic `nu=-0.2` | 0 | 1.2e-16 | 1.2e-16 | **0** | 1.8e-16 |
| skew, `nu=0.25` | 0 | 1.2e-16 | 1.2e-16 | **0** | 1.2e-16 |
| skew, `nu=0.45` | 0 | 1.2e-16 | 1.2e-16 | **0** | — |
| skew, `nu=0.499` | 0 | 1.2e-16 | 1.2e-16 | **0** | — |
| thin, steel | 0 | — | — | **0** | — |
| translated skew | 0 | — | — | **0** | — |
| scaled skew (1e-3) | 1.2e-16 | 6.4e-17 | 6.4e-17 | **0** | ~1e-16 |
| 12 random, fixed seed | 0 … 7.5e-16 | 1.6e-16 … 1.6e-15 | same | **0** | 1.4e-16 … 2.3e-15 |

Asserted bounds: `V <= 1e-13`, gradients and `B <= 1e-11`, `D <= 1e-14`,
`Ke <= 1e-11`. Measured worst case is **2.3e-15**, four orders inside the
bound, so the thresholds are not doing any work.

**`D` agrees bit for bit on every fixture**, and that is explained rather than
lucky: production takes `mu` from P15's `shearModulus`, which is
`E/(2(1+nu))`, and the reference computes `E/(2(1+nu))` itself — the same
expression, so the same double. `lambda` is the same expression in both too.
It is still a real check, because a `2 mu` on the shear diagonal or a
transposed block would show immediately.

**`V` agrees exactly on almost every fixture** although the two routes are a
3x3 cofactor-free triple product and a 4x4 Eigen determinant. The scaled
fixture differs by one ulp, which is the expected behaviour of two different
expansions.

## P16 / P17 volume cross-check

| Mesh | Elements | Worst relative disagreement |
| --- | --- | --- |
| three chosen fixtures (unit, skew, thin) | 3 | **0** |
| RM-MESH-01 | every element | **0** |
| RM-MESH-03 | every element | **0** |
| RM-MESH-07 | every element | **0** |

Bit-for-bit, and asserted as `== 0.0` rather than with a tolerance. That is a
decision with a reason: the two functions are predicates on the *same*
boundary, so a tetrahedron whose signed volume rounds to within an ulp of zero
must not be data-valid for `MeshValidation` and inverted for this kernel. It is
exact because the determinant is grouped exactly as `meshing::signedVolume`
groups it — `cross` then `dot`, summed left to right. The first draft expanded
cofactors along the first row and was **one ulp out**; see
`ADVERSARIAL_REVIEW.md` F1.

A separate whole-mesh check, which per-element agreement cannot make: the
element volumes **sum** to `VolumeMesh::tetrahedralVolume()` within 1e-12
relative, over RM-MESH-03's elements. That would catch a kernel that was right
element by element and was handed the wrong connectivity.

## Rigid-body modes

Unit tetrahedron, steel, `||Ke||inf = 7.40e10`:

| Mode | `||Ke r||inf` | relative | strain energy `U` | PASS |
| --- | --- | --- | --- | --- |
| Tx | 3.81e-06 | 2.58e-17 | **0** | yes |
| Ty | 3.81e-06 | 2.58e-17 | **0** | yes |
| Tz | 3.81e-06 | 2.58e-17 | **0** | yes |
| Rx | 9.54e-07 | 1.05e-17 | **0** | yes |
| Ry | 9.54e-07 | 1.05e-17 | **0** | yes |
| Rz | 1.43e-06 | 1.58e-17 | **0** | yes |

Skew tetrahedron, steel, `||Ke||inf = 4.36e11`:

| Mode | `||Ke r||inf` | relative | `U` (J) | PASS |
| --- | --- | --- | --- | --- |
| Tx | 2.48e-05 | 5.69e-17 | 1.1e-05 | yes |
| Ty | 2.29e-05 | 5.25e-17 | -1.9e-05 | yes |

The energies on the skew element are roundoff at the `1e-17` relative level
against a `1e11` stiffness, and one is **negative** — which is exactly why the
assertion is `|U| <= 1e-12 ||Ke|| ||r||^2` and not `U >= 0`. A rigid mode's
energy is zero, and zero computed in floating point can land on either side.

The rotations are built about the element's **own centroid**, so they are
genuine infinitesimal rigid rotations of that element rather than rotations
about the global origin with a translation mixed in.

The six modes are tested **explicitly**, by their analytically constructed
vectors, before any eigenvalue is looked at — a null space of the right
dimension could still be the wrong null space.

## Eigenvalues and rank

Unit tetrahedron, steel. Sorted, `largest = 2.1114e+11`, zero band
`1e-10 x largest = 21.114`:

```text
 1   -7.46976e-06      \
 2   -2.26411e-06       |
 3    2.19310e-08       |  six, all inside the zero band by 15 orders of
 4    9.09140e-07       |  magnitude. Two are negative: roundoff, not
 5    3.28775e-06       |  instability
 6    1.50014e-05      /
 7    2.69231e+10      \
 8    2.69231e+10       |
 9    4.46296e+10       |  six deformational, all positive
10    6.73077e+10       |
11    6.73077e+10       |
12    2.11140e+11      /
```

```text
zeros counted            6
positives counted        6
materially negative      0      (a negative outside the band FAILS the test)
numerical rank, by SVD   6      singular values 2.11e11 ... 2.69e10, then
                                six below 1e-10 of the largest
```

The separation is fifteen orders of magnitude, so the `1e-10` relative band is
not a tuned threshold — any band between `1e-15` and `1e-6` of the largest
eigenvalue gives the same answer. Rank is checked a second time by **SVD**,
which is an independent decomposition of the same claim.

Nothing regularises the diagonal. A free Tet4 is *meant* to be singular; the
six zero modes are the physics, and `M9` below confirms that adding anything to
the diagonal is caught.

## Symmetry

| Fixture | `||Ke||inf` | `max |Ke_ij - Ke_ji|` | relative |
| --- | --- | --- | --- |
| unit, `E=1 nu=0` | 0.333333 | **0** | **0** |
| unit, steel | 7.40385e+10 | **0** | **0** |
| skew, steel | — | — | within 1e-12 (asserted) |
| thin, steel | — | — | within 1e-12 (asserted) |
| skew, `nu=0.499` | — | — | within 1e-12 (asserted) |
| RM-MESH-01/03/07, every element | — | — | within 1e-12 (asserted) |

Exactly zero on the chosen fixtures, which is what `B^T D B` with symmetric `D`
should give. The tolerance is **relative to the matrix norm**, because an
absolute bound cannot serve both a `0.33 N/m` unit-material element and a
`7.4e10 N/m` steel one.

Nothing is symmetrised afterwards. `M8` confirms that adding
`0.5 (K + K^T)` would hide a formula error rather than fix a numerical one.

## Scale laws

Geometry: `x' = s x`, material fixed.

| `s` | V ratio expected | actual | B ratio expected | actual | Ke ratio expected | actual | PASS |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1e-3 | 1e-9 | within 1e-12 rel | 1e3 | within 1e-12 rel | 1e-3 | within 1e-12 rel | yes |
| 1e-1 | 1e-3 | " | 10 | " | 1e-1 | " | yes |
| 1 | 1 | " | 1 | " | 1 | " | yes |
| 1e2 | 1e6 | " | 1e-2 | " | 1e2 | " | yes |
| 1e3 | 1e9 | " | 1e-3 | " | 1e3 | " | yes |

`Ke ∝ s` because `s^3 · (1/s) · (1/s) = s`. This is the sharpest units check in
the milestone: dropping `V` (M6) or taking `|V|` changes the exponent, and both
probes are killed here.

Material: `E' = c E`, geometry and `nu` fixed.

| `E` factor | D ratio expected | actual | Ke ratio expected | actual | PASS |
| --- | --- | --- | --- | --- | --- |
| 1e-3 | 1e-3 | within 1e-12 rel | 1e-3 | within 1e-12 rel | yes |
| 1 | 1 | " | 1 | " | yes |
| 7 | 7 | " | 7 | " | yes |
| 1000 | 1000 | " | 1000 | " | yes |

## Translation invariance

Gradients are derivatives, so the origin cannot matter — but subtractive
cancellation can, so the tolerance is stated per offset rather than assumed
uniform.

| Offset (m) | V | B | Ke | tolerance |
| --- | --- | --- | --- | --- |
| (13.7, -4.2, 8.9) | invariant | invariant | invariant | 1e-14 rel |
| (1e3, -2e3, 5e3) | invariant | invariant | invariant | 1e-12 rel |
| (1e6, -1e6, 1e6) | invariant | invariant | invariant | 1e-9 rel |

The loosening with distance is cancellation, measured rather than wished away:
a 1 m element at 1e6 m loses about six digits in the coordinate differences,
and 1e-9 is the honest bound there. That is within CAD's working range and is
recorded as a limitation rather than a result.

## Rotation

A proper rotation (`det R = +1`, axis `(1,2,-3)/|...|`, angle 0.7 rad) turns the
global component basis with the geometry, so the correct statement is
**covariance**, not invariance:

```text
||Ke_rot - T Ke T^T|| / ||Ke||      <= 1e-12        T = diag(R, R, R, R)
volume                               invariant to 1e-13 rel
eigenvalues                          invariant to 1e-11 rel
strain energy under R u              invariant to 1e-10 rel
```

Requiring `Ke_rot == Ke` would be wrong, and the test says so in its own
comment. A **reflection** (`det R = -1`) with the node order unchanged flips
the signed volume and is **rejected** — and swapping a node pair restores
validity, which is what makes that a statement about orientation rather than
about reflection.

## Invalid geometry

| Case | Signed volume | Expected | Actual | Diagnostic | PASS |
| --- | --- | --- | --- | --- | --- |
| inverted (one swap) | < 0 | reject | reject | `inverted_element` | yes |
| three odd permutations | < 0 | reject | reject | `inverted_element` | yes |
| reflected, order kept | < 0 | reject | reject | `inverted_element` | yes |
| four coplanar nodes | `== 0` | reject | reject | `degenerate_element` | yes |
| two coincident nodes | `== 0` | reject | reject | `degenerate_element` | yes |
| three collinear + one off | `== 0` | reject | reject | `degenerate_element` | yes |
| NaN coordinate | — | reject | reject | `non_finite_coordinate` | yes |
| `+Inf` coordinate | — | reject | reject | `non_finite_coordinate` | yes |
| `-Inf` coordinate | — | reject | reject | `non_finite_coordinate` | yes |
| thin, 1000:1, positive V | > 0 | **accept** | **accept** | — | yes |

The last row is the one that matters most for scope: `invalid` is not `poor
quality`. P16's degeneracy rule has **no tolerance**, so a thin tetrahedron is
data-valid and refusing it here would make a qualified mesh unsolvable.
`P17-VALID-001` owns acceptance policy.

An inverted element is **never reordered**. The test swaps the pair back
afterwards and the element becomes valid, which proves the refusal was about
the order and not about the points.

## Invalid material

| Case | Expected | Actual | PASS |
| --- | --- | --- | --- |
| `nu = 0.5` | reject | reject, `invalid_material` | yes |
| `nu = -1` | reject | reject, `invalid_material` | yes |
| `nu = NaN` | reject | reject, `invalid_material` | yes |
| `nu = 0.499` | accept | accept, finite D and Ke | yes |
| `nu = -0.2` (auxetic) | accept | accept, matches reference | yes |

The admissible **range** is not re-checked here, and that is stated as a
decision: P15 refuses an unusable `E` or `nu` at the point of entry, which
P17-MAT-001 established and proved, so a material resolved through
`StructuralMaterial::elastic()` cannot be out of range. What is checked is
**finiteness**, because `LinearElasticConstants` is a plain struct a caller can
fill by hand, and a NaN must not reach `Ke` silently. See `KNOWN LIMITATIONS`
in `README.md`.

`nu = 0.499` is accepted with worsening conditioning and no complaint, because
P15 allows anything strictly below 0.5 and a solver-quality policy is a later
milestone's.

## Energy

```text
U from Ke  = (1/2) u^T Ke u
U from eps = (1/2) V eps^T D eps
```

agree to **1e-10 relative** over three affine fields on the skew element
(pure extension, pure shear, and a mixed field), and are strictly positive for
each. That is the end-to-end formulation check: the quadratic form in nodal
displacements and the strain-energy density integral are computed by different
routes through different public APIs.

Supporting checks:

```text
u^T Ke u >= -1e-12 ||Ke|| |u|^2   over 64 deterministic pseudo-random vectors
v^T Ke u == u^T Ke v              to 1e-12 rel, through forceFrom()
rigid translation -> zero force   to 1e-12 ||Ke|| |u|
```

## Unit equivalence

The same physical element described two ways:

```text
nodes  (0,0,0) (0.1,0,0) (0,0.1,0) (0,0,0.1) metres
       (0,0,0) (100,0,0) (0,100,0) (0,0,100) millimetres
E      2e11 Pa   and   200 GPa

relative difference in Ke       <= 1e-15 (asserted); measured 0
```

Exactly equal, because `Quantity` converts at construction and stores SI — the
conversion happens once, at the boundary, which is the project's rule.

## Determinism

Within one build, repeated evaluation is asserted **bit-identical** — `==` on
the whole value, not a tolerance — because the computation is a fixed sequence
of scalar arithmetic over its arguments with no state:

```text
kinematics, D and Ke equal over 16 repeats, on unit, skew and thin
batch fingerprint equal over 4 repeats of every element of RM-MESH-01,
  together with element count, min and max volume and worst asymmetry
```

The fingerprint is a test-side checksum over the ordered element handles,
volumes and four fixed stiffness entries. It is determinism evidence only and
is not an engineering authority: it says two passes produced the same numbers
and nothing about whether they are right.

Searched, and zero occurrences in `src/structural/Tet4Element.cpp`:
`static` storage, `mutable`, `thread_local`, `cache`, and any unordered
container.
