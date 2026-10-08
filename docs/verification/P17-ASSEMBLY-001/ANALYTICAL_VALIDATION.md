# P17-ASSEMBLY-001 — analytical and independent validation

## The oracle is independent, and that is the design

```text
PRODUCTION                          ORACLE (test-side)
-----------------------------------------------------------------------------
Ke from P17-ELEM: a 3x3 Jacobian,   Ke from the 4x4 inverse of [1 x y z];
cofactor determinant, gradients     rows 1..3 of A^-1 ARE the gradients, so no
through J^-T                        Jacobian is formed and no transposition in
                                    production can be mirrored
mu from P15's derived shearModulus  lambda and mu computed from E and nu here
hand-written triple-product loops   Eigen does the multiplication
row from FreeEquationMap            row computed HERE as 3 * ordinal +
                                    component, from the node's position in
                                    mesh.nodes()
sparse CSR, scatter-add             dense Eigen::MatrixXd, explicit nested
                                    scatter
```

Taking production's numbering for the oracle would have made the comparison a
tautology; computing `3 * ordinal + component` in the test cross-checks the
documented interleaving at the same time.

## Entrywise, both directions

```text
StructuralSystem_MatchesAnIndependentDenseScatterOfEveryElement
```

Every cell of the production matrix against the oracle, and every cell of the
oracle against production — so neither a missing entry nor an extra one can
hide behind a one-directional comparison.

```text
Ndof                      57
nnz                     1647
max |K| (SI)        4.62e+10
largest entrywise difference   < 1e-12 * max|K|        PASS
```

The tolerance has a reason: the two routes do the same algebra with different
groupings and different operation counts, so they agree to accumulated
double-precision rounding and not to the last bit.

## Why there is no one-element fixture, and what replaces it

`assembleStructuralSystem` takes a `StructuralModel`, whose possession is
ADR-036's evidence that the mesh came from the mesher. A hand-built
one-element `Mesh` cannot become one, and the mesher will not produce a
single-tetrahedron mesh of a real solid. The alternative was a test-only back
door, which CLAUDE.md forbids.

What replaces it is stronger: the test counts, independently, how many elements
reach each global entry, and checks the sum **in every contributor-count
group**.

```text
contributors : entries
    2:90   3:162   4:702   5:342   6:180   8:36   10:18   11:18
   12:45  13:9    14:9    15:9    16:9    18:9   22:9
                                                  (15 groups, 1647 entries)

largest disagreement in each group        < 1e-12 * max|K|        PASS
```

**And the single-contributor regime does not exist here** — measured, not
assumed. On a Tet4 mesh of a solid, every pair of nodes that shares a
tetrahedron shares more than one, so the minimum is two. The first draft of
this test *required* a single-contributor entry and failed at `0 > 0`; the
requirement was wrong, not the code. Recorded rather than engineered around.

An overwrite instead of an accumulation would differ from the sum by a factor
of roughly `count` — up to 22 here — which is not a tolerance question. The
probe confirms it: `+=` replaced by `=` is killed by nine tests.

## The named two-element entry

The table brief section 135 asks for, read off real connectivity. Each
element's contribution is computed by the independent route.

```text
Global entry   Element   Contribution (N/m)   Expected sum      Actual
------------------------------------------------------------------------
K(0, 9)          40          1.69904e+09
                 58          7.71593e+08
                                               2.47063e+09   2.47063e+09
```

Both contributions are asserted non-zero first, and the assembled value is
asserted to differ from *each one alone* by more than 1e-6 of the sum — so
"the last contribution won" is excluded explicitly and not merely by a
tolerance.

## Symmetry, with the tolerance derived rather than tuned

The first draft asserted the global symmetry error is exactly zero. **It is
not**, and the reason is in P17-ELEM rather than here:
`computeTet4Stiffness` forms `Ke(a,b) = sum_k B[k][a] * (DB)[k][b]`, so
`Ke(a,b)` and `Ke(b,a)` are *different sums of different products* —
mathematically equal because `D` is symmetric, equal to within rounding in
floating point. The assembly inherits exactly that and adds nothing.

So the bound is measured from the elements and applied to the matrix:

```text
element asymmetry   max |Ke(a,b) - Ke(b,a)|            9.537e-07
element scale       max |Ke(a,b)|                      1.131e+10
                    9.537e-07 <= 8 * eps * 1.131e+10 = 2.01e-05    PASS

most contributors to one entry                                22
bound = 22 * 9.537e-07                                   2.098e-05
global error        max |K(i,j) - K(j,i)|                1.907e-06
                    1.907e-06 <= 2.098e-05                        PASS
                    1.907e-06 <  1e-14 * 4.62e+10 = 4.6e-04       PASS
```

The global error came out at exactly **twice** the element asymmetry, against a
bound of twenty-two times it.

**And it is asserted to be strictly positive.** That is the detector for the
brief's automatic failure "K is symmetrised after assembly to hide error": a
mutation probe showed the rest of the suite cannot see such a step, because
averaging two values that already agree to 1e-17 changes nothing anyone
measures. But a symmetrised matrix comes out *exactly* symmetric on every mesh,
and an untouched one cannot when its elements are asymmetric and several of
them write each entry. Both premises are asserted before the claim. With the
detector in place the probe is killed.

The energy form is checked too: `v^T K u == u^T K v` to 1e-12, which would
catch an asymmetry in the pattern rather than the values.

## Energy

Brief section 70, on a deterministic non-rigid displacement field (a fixed
quadratic in the node coordinate, so every element deforms):

```text
0.5 u^T K u                    from the sparse matrix
sum over elements 0.5 ue^T Ke ue   from the independent Ke and an explicit gather
agreement                      within 1e-11 relative                  PASS
```

## Internal force

Brief section 71, which catches a row/column scatter mistake that energy
alone would not:

```text
K u                            from the sparse matrix
scatter of every Ke ue         into a dense reference vector
||difference||                 < 1e-11 * scale * sqrt(Ndof)           PASS
```

## Rigid-body modes

Brief sections 31 to 34. A free connected body's `K` is **singular and stays
singular**: nothing regularises the diagonal, pins a node or adds a penalty
spring. The residuals are scale-aware — compared against `|K|` times the size
of the motion, the only dimensionally meaningful yardstick.

```text
Mode   ||K r||       relative     PASS
Tx     5.85007e-06   2.17e-17     PASS
Ty     4.16362e-06   1.55e-17     PASS
Tz     1.12346e-05   4.17e-17     PASS
Rx     2.61265e-07   3.24e-17     PASS
Ry     3.34109e-07   3.10e-17     PASS
Rz     1.63716e-07   1.52e-17     PASS
```

The rotations are `u_i = omega x x_i` built from the mesh's own coordinates.
That they are null modes is the evidence that element assembly did not destroy
the local null spaces — a scatter error would break the rotations while leaving
the translations intact, which is why both are checked.

On RM-MESH-04, a body with a **void**, the translations hold too:

```text
axis 0   4.17638e-05   2.33e-17     PASS
axis 1   3.33492e-05   1.86e-17     PASS
axis 2   1.69166e-05   9.43e-18     PASS
```

If assembly had invented coupling across the hole — a spatial neighbour rather
than a shared node — the invented stiffness would be balanced by no element and
the translations would stop being null.

## Nullity and positive semidefiniteness

A full eigendecomposition, on the smallest fixture because a large mesh must
not be densified.

```text
Ndof                                 57
near-zero eigenvalues                 6        PASS
smallest nonzero eigenvalue   9.09427e+08
largest eigenvalue            4.61977e+10
every nonzero eigenvalue > 0                   PASS
```

**The connectivity is asserted first.** Brief section 37 is explicit that
nullity six must not be hardcoded for an arbitrary mesh, so the test performs
its own flood fill over shared nodes and requires that every element is
reachable from element zero and every node belongs to an element. Six is then
the right answer for one connected body with no internal mechanism.

**And the threshold does not decide the answer.** The sixth and seventh
eigenvalues are asserted to be more than four orders of magnitude apart, so the
count is a property of the matrix: `|lambda_6|` sits at the rounding level and
`lambda_7 = 9.09e8`.

Energy of a deformation is positive, and every diagonal entry is positive on
every active degree of freedom — supplemental checks, not the primary proof.

## The load vector

```text
Fixture                  Expected              Prepared        Assembled    PASS
--------------------------------------------------------------------------------
nodal, 2 nodes           1,2,3 / -4,5,6        identical       identical    PASS
two loads on one DOF     +10 and -3  ->  7     7               7            PASS
superposition            F(L1+L2) = F(L1)+F(L2)  entrywise                  PASS
no loads                 F = 0                 --              every entry 0 PASS
pressure, RM-MESH-01     -p A = -2100 N        -2100 N         -2100 N      PASS
gravity,  RM-MESH-01     -rho V g = -22.6328 N -22.6328 N      -22.6328 N   PASS
```

The pressure's expected value is `p` times the analytical face area
`0.120 x 0.070` from RM-MESH-01's own declared dimensions, and acts **inward**
against the end cap's `+Z` outward normal. The gravity expectation is
`rho V g` with `V = 0.120 x 0.070 x 0.035`, also from the declared dimensions
and not from the mesh.

**Entry by entry, not just the resultant.** For the pressure case all four
prepared nodal forces are located in `F` through independently derived rows and
compared exactly, so a scatter that put the right total in the wrong rows would
fail.

**Where the two-load addition happens is stated precisely**, because a probe
showed the obvious test does not measure the assembly: `PreparedLoads::nodal()`
holds *one* entry per node, so P17-LOAD does the addition and the assembly
scatters it once. The `+=` in the assembly is therefore defensive, the probe is
inert, and the contract it relies on — `nodal()` unique and ascending by node —
is now pinned by its own assertion, so if that ever changed the probe would
start killing.

## Dependency separation

```text
Change                     K             F             PASS
----------------------------------------------------------
different load case        unchanged     changed       PASS
E -> 2E                    exactly 2K    unchanged     PASS
rho 7850 -> 2700           unchanged     scales by     PASS
  (gravity load)                         2700/7850
restraint added            unchanged     unchanged     PASS
```

The material-scaling check is entrywise over all 1647 values and also asserts
the *pattern* is unchanged — `rowStart` and `innerIndices` compared element for
element — because the pattern depends on connectivity alone.

## Diagnostics matrix

```text
Scenario                      Expected problem              Published?  PASS
-----------------------------------------------------------------------------
valid model                   none                          yes         PASS
loads from another mesh       LoadSourceMismatch            no          PASS
mesh with no nodes            MeshHasNoDegreesOfFreedom     no          UNTESTED
mesh with no tetrahedron      MeshHasNoElements             no          UNTESTED
element names a missing node  ElementNodeMissing            no          UNTESTED
P17-ELEM refuses an element   ElementRejected               no          UNTESTED
load names a missing node     LoadNodeMissing               no          UNTESTED
a non-finite entry            NonFiniteSystem               no          UNTESTED
```

**The six untested values are untested for one reason, and it is the same
reason in each case: possession of a `StructuralModel` and a `PreparedLoads`
proves they cannot happen.** The model proves the mesh came from the mesher, so
it has nodes, has tetrahedra, has no element naming a missing node and no
degenerate or inverted element; the prepared loads prove every load resolved
against that mesh. Constructing any of those states would need a hand-built
mesh, which cannot become a `StructuralModel`, or a test-only back door.

They are **kept rather than deleted** because each is a real failure of a
future input path — a mesh arriving from `io`, a mesher backend change, a
pathological geometry — and an assembly that quietly skipped a bad element
would publish a softer body with nothing reporting it. Every value's
`toString` is asserted, so each is named rather than merely present, and
`structuralAssemblyProblem` agrees with `assembleStructuralSystem` on every
reachable case.

The mutation that disables the element refusal survives, and that survival is
the measurement confirming the branch is unreachable — recorded in
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md) rather than hidden.
