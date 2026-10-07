# P17-ELEM-001 — the independent reference implementation

```text
SUBJECT:  how the expected values were produced without using the code under
          test, and how that independence is enforced rather than promised
```

The numbers are in `NUMERICAL_VALIDATION.md`. This document is about
methodology: an "independent" reference that quietly shares the production
algorithm proves nothing, and the brief lists exactly that as an automatic
failure.

## Two kinds of expectation, and both are used

```text
ANALYTICAL        derived by hand and written out as exact values. Available
                  only for the reference tetrahedron, where J = I and every
                  gradient is a small integer, and for the constitutive law,
                  where lambda and mu have closed forms
INDEPENDENT       computed in the test by a different algorithm, for geometry
                  nobody can do by hand: skew, thin, translated, scaled and
                  twelve deterministic pseudo-random tetrahedra
```

Neither replaces the other. The analytical case is the only one that can catch
an error the two implementations might *share*; the independent one is the only
one that reaches geometry where a transpose or a cofactor sign actually shows.

## The different route

Production:

```text
form the 3x3 Jacobian J, columns = edge vectors from node 0
det J  = e1 . (e2 x e3)
V      = det J / 6
grad_x N = J^-T grad_xi N        with grad_xi N the constant reference table
mu     = taken from P15's LinearElasticConstants::shearModulus
products = hand-written triple loops over std::array
```

Reference (`tests/structural/Tet4ElementTests.cpp`, function `reference`):

```text
form the 4x4 A whose rows are [1 xj yj zj]
V      = det(A) / 6                             Eigen 4x4 determinant
                                                (LU, not a cofactor expansion)
C      = A^-1                                   Eigen 4x4 inverse
grad Ni = rows 1..3 of C, column i              read DIRECTLY -- no Jacobian
                                                is ever formed, so no transpose
                                                exists to get wrong
lambda, mu = recomputed from E and nu in the test
products = Eigen fixed-size matrix expressions
```

**Why that route is genuinely independent.** `Ni(x,y,z) = ai + bi x + ci y +
di z` with `Ni(node j) = delta_ij` means `A c_i = e_i`, so column `i` of
`A^-1` holds node `i`'s coefficients and its last three entries *are* the
gradient. The reference therefore never maps anything through a Jacobian. The
single most likely defect in the milestone — `J^-1` where `J^-T` belongs —
cannot be mirrored, because there is nothing to mirror it in. Mutation **M2**
confirms it: swapping the index order in production's gradient loop is killed
by the reference comparison, among six others.

It is also independent in the two places that matter besides the geometry:

```text
mu            production takes P15's derived shearModulus; the reference
              computes E/(2(1+nu)) itself. Both are the same EXPRESSION, so
              they agree bit for bit -- stated plainly in NUMERICAL_VALIDATION
              rather than presented as a stronger result than it is. What the
              comparison still catches is a transposed block or a wrong shear
              diagonal, which M3 confirms
arithmetic    Eigen's products against hand loops, so a transcription error in
              either triple loop shows
```

## How the independence is enforced

Not by a promise in a comment. Three mechanisms:

```text
1. THE REFERENCE FUNCTION'S ONLY PRODUCTION DEPENDENCY IS ITS ARGUMENT.
   `reference(const Nodes&, double, double)` reads `Point3D::x/y/z` and
   nothing else. It calls no function from `bettercad::structural`, and it
   cannot: it is declared above the production readers in the file's anonymous
   namespace and uses none of them.

2. THE COMPARISON IS ONE-DIRECTIONAL. `productionB`, `productionD` and
   `productionKe` copy production output into Eigen matrices and are used only
   on the left of a subtraction. Nothing flows the other way.

3. THE ANALYTICAL CASE IS BUILT A THIRD TIME. The unit-tetrahedron test does
   not call `reference` either: it writes the four gradients out as integer
   literals, writes the `B` block out again, builds `D` as
   `diag(1,1,1,1/2,1/2,1/2)` by hand, and multiplies. So the unit case has
   three independent descriptions -- production, the reference, and hand
   arithmetic -- and all three agree.
```

What this does **not** claim: the reference was not derived symbolically with
an external tool. The brief allows that and says not to add a runtime Python
dependency for it. The reference tetrahedron's values were derived by hand and
are checkable by a reader in a few lines (`J = I`, so `J^-T = I`, so the
physical gradients are the reference gradients); the skew and random cases rest
on the algorithmic independence above rather than on a third symbolic
derivation. That is recorded as the limit of the evidence rather than glossed.

## Breadth, and why it is not the whole story

Twelve deterministic pseudo-random tetrahedra are compared as well, with:

```text
fixed seed            std::mt19937{20261007}
positive volume       enforced at construction -- a generated tetrahedron with
                      negative volume has one node pair swapped, and one with
                      |V| < 0.02 is rejected and regenerated
bounded conditioning  coordinates in [-1, 1] with a volume floor, so the
                      fixtures are well conditioned by construction
no result shopping    the seed and the acceptance rule are in the source; no
                      case is skipped on its result
```

They are breadth **on top of** the fixed fixtures, never instead of them. A
suite of only random cases could pass while the reference element — the one
case a reader can verify — was wrong, and it would give a different set of
geometry on a different platform if the engine ever changed.

## The reference-mesh pass is a different question

`tests/reference/Tet4ElementReferenceTests.cpp` runs every element of
RM-MESH-01, RM-MESH-03 and RM-MESH-07 through the kernel. It does **not** use
the independent reference — a 4x4 inverse per element over three meshes would
be measuring Eigen — and it is not asking whether the formula is right. It
asks four different things:

```text
does a QUALIFIED mesh contain an element this kernel refuses?       none do
does P17's volume equal P16's, element by element?                  exactly
is every Ke finite and symmetric at production scale?               yes
is the whole batch reproducible?                                    yes
```

plus one genuinely independent whole-mesh check: the element volumes **sum** to
`VolumeMesh::tetrahedralVolume()`, which P16 computed by its own route over its
own iteration order. Per-element agreement cannot make that claim — a kernel
handed the wrong connectivity would pass element by element and fail the sum.
