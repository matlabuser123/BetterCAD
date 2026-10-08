# P17-ASSEMBLY-001 — Global Matrix / Vector Assembly

```text
STATUS:    PASS
MILESTONE: P17-ASSEMBLY-001, the eighth milestone of P17 — Structural FEA
SCOPE:     deterministic sparse assembly of the UNCONSTRAINED global system
           `K u = F` for the current linear-static Tet4 model. No solve, no
           constraint application, no reduced system, no regularisation of the
           free-body singularity, no post-processing, no persistence, no CLI,
           no GUI.
```

## Baseline

```text
HEAD at start      3b471a940baec875e4e7dd806cdd6efbcbc3a674
origin/main        3b471a940baec875e4e7dd806cdd6efbcbc3a674
HEAD^{tree}        fea8ed0fa7450016450592d7b7e6dce16fdea73a
working tree       clean, 0 porcelain lines
git log -1         3b471a9 BetterCAD: add structural restraint model

P17-ARCH-001  20/20   P17-DATA-001  19/19   P17-MAT-001  14/14
P17-DOF-001   12/12   P17-ELEM-001  18/18   P17-LOAD-001  18/18
P17-BC-001    16/16   P16  QUALIFIED (P16-QUAL-001, 2026-10-06)

every predecessor checked for OPEN boxes, not just for a PASS line: zero
across all seven, and every one has a README in docs/verification/
```

`TODO.md` authorized the milestone before any production file was touched:
*"P17-ASSEMBLY-001 — Global Matrix / Vector Assembly. AUTHORIZED 2026-10-08 by
an explicit scope decision. In progress."*

## The decision this milestone turned on

The brief's first step is to audit the existing sparse and linear-algebra
infrastructure before writing a container. What the audit found:

```text
Eigen 5.0.1        already a pinned, SHA256-locked dependency, already linked
                   PRIVATE into bettercad_sketch AND bettercad_assembly, and
                   into bettercad_tests
Eigen/Sparse       NOT USED ANYWHERE. Both modules include only <Eigen/Dense>;
                   there is no SparseMatrix, no Triplet and no CSR in the tree
a public header
  including Eigen  NONE, anywhere in the repository
the licence        already settled: TODO.md records "Eigen is MPL-2.0 and
                   passes the project's weak-copyleft admission rule". The open
                   question is about sparse DIRECT SOLVERS, several of which
                   are GPL, and that is P17-SOLVE-001's to state
whose decision     src/structural/CMakeLists.txt: "Admitting it here is
                   P17-SOLVE-001's decision". TODO.md: "Making it available to
                   a structural module is an explicit decision, not a side
                   effect"
```

**Decision: BetterCAD owns the CSR, and `bettercad_structural` continues to
link no linear algebra at all.** Three candidates were compared and the
rejected two are recorded, in
[ADR-038](../../architecture/decisions/ADR-038-the-global-stiffness-matrix-is-a-bettercad-owned-csr-that-eigen-can-map.md).

```text
assembly needs no algebra        it needs a scatter-add and a container.
                                 Linking a library for a CONTAINER would make
                                 Eigen a PUBLIC dependency of layer 50 and
                                 propagate it to io, renderer, scripting, the
                                 app and the CLI
determinism becomes provable     Eigen documents setFromTriplets as summing
                                 duplicates but not as summing them in a
                                 SPECIFIED ORDER. Many elements write one
                                 entry, so that order is a numerical contract,
                                 and depending on an unspecified one is what
                                 the determinism rule forbids
nothing is pre-empted            the structural Eigen admission stays
                                 P17-SOLVE-001's, and the three arrays map
                                 into Eigen::Map at zero copy, so deferring
                                 costs nothing
```

This is a representation, not a library: no factorisation, no multiplication
operator, no transpose, no solve. `coeff`, a symmetry scan and a finiteness
scan are the only operations, and each exists because assembly's own
validation needs it.

## No index arithmetic exists in this module

ADR-037 made `FreeEquationIndex` a zero-based **position** and said why:
*"a 1-based handle would put a `- 1` at every assembly site, and the one that
was forgotten would be an off-by-one in the stiffness matrix."*

So the global row space is the free-equation numbering of the **empty
constraint set**: with nothing prescribed, `freeCount() == dofCount()` and the
free numbering IS the global one.

```text
"- 1" as a row adjustment    0 occurrences
"3 * node"                   0 executable occurrences
raw NodeId as an index       0
```

The probe that substitutes `kDofsPerNode * nodeId + c` — correct only for a
dense, zero-based, gap-free handle space — is killed by **24 of 26** tests.

## What was built

```text
NEW   include/bettercad/structural/StructuralSystem.hpp
NEW   src/structural/StructuralSystem.cpp
NEW   docs/architecture/decisions/ADR-038-*.md

NEW   tests/structural/StructuralSystemTests.cpp            20 cases
NEW   tests/reference/StructuralSystemReferenceTests.cpp     6 cases
NEW   tests/compile_fail/StructuralSystemMisuse.cpp          6 cases

CHANGED  src/structural/CMakeLists.txt            +1 line
CHANGED  tests/CMakeLists.txt                     +2 lines
CHANGED  tests/compile_fail/CMakeLists.txt        the new group
```

**No predecessor production file was modified**, so brief section 146's
requalification requirement has nothing to act on — the git evidence is in
[FREEZE.md](FREEZE.md).

## The algorithm

```text
SYMBOLIC   walk the elements once; record which columns each row will
           receive; sort, unique, lay out the CSR.   O(nnz)
NUMERIC    walk again; values[slot] += Ke(a, b).
           ONE slot per (row, column), so a duplicate is a SUM BY
           CONSTRUCTION -- not by a library's reduction policy
```

A triplet staging would have cost 144 entries per element at 24 bytes — 345 MB
for a 100 000-element mesh — and would have needed a stable sort whose
reduction order then had to be proved. The symbolic pass removes the question
entirely: the accumulation order IS the traversal order.

Both orders are frozen and neither is this module's invention — P16 guarantees
`tetrahedra()` is ascending by `ElementId` and built by nothing unordered, the
local order is written out as `row 0..11` then `column 0..11`, and
`PreparedLoads::nodal()` is ascending by handle.
[ASSEMBLY_ALGORITHM.md](ASSEMBLY_ALGORITHM.md) carries it in full;
[SPARSE_REPRESENTATION.md](SPARSE_REPRESENTATION.md) the storage, the index
width and the duplicate semantics.

## The oracle is independent

```text
PRODUCTION                        ORACLE (test-side)
---------------------------------------------------------------------------
Ke via a 3x3 Jacobian and a       Ke via the 4x4 inverse of [1 x y z]: rows
cofactor determinant              1..3 of A^-1 ARE the gradients, so no
                                  Jacobian is formed
mu from P15's shearModulus        lambda and mu computed from E and nu here
row from FreeEquationMap          row computed as 3 * ordinal + component,
                                  from the node's position in mesh.nodes()
sparse CSR scatter-add            dense Eigen matrix, explicit nested scatter
```

Compared **entrywise in both directions**, so neither a missing entry nor an
extra one can hide. [ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md).

## Measured results

```text
Model                  nodes  tets   Ndof    nnz   ||K-K^T||   relative  finite
-------------------------------------------------------------------------------
RM-MESH-01                 8     6     24    414   0           0         YES
RM-MESH-03                81   126    243   5895   7.63e-06    6.59e-17  YES
RM-MESH-04               261   808    783  24183   3.81e-06    5.95e-17  YES
RM-MESH-07                61   267    183   6543   7.63e-06    6.03e-17  YES
RM-MESH-07 @ 12 mm        14    31     42   1026   1.91e-06    2.17e-17  YES
RM-MESH-07 @  4 mm       153   769    459  18063   1.53e-05    6.72e-17  YES
RM-MESH-02 @ 0.25/12     140   508    420  14166   7.63e-06    4.54e-17  YES
RM-MESH-02 @ 0.025/6     850  4210   2550 102276   9.16e-05    3.04e-17  YES
block fixture             19    --     57   1647   1.91e-06    5.35e-17  YES
```

```text
duplicate accumulation   15 contributor-count groups, 2 to 22 elements per
                         entry, every group equal to the independent sum
named two-element entry  K(0,9) = 1.69904e9 + 7.71593e8 = 2.47063e9, and
                         asserted to differ from EACH contribution alone
rigid-body modes         Tx 2.17e-17  Ty 1.55e-17  Tz 4.17e-17
                         Rx 3.24e-17  Ry 3.10e-17  Rz 1.52e-17  (relative)
nullity                  exactly 6; smallest nonzero 9.09e+08 against a
                         largest of 4.62e+10, a gap of four orders
energy                   0.5 u^T K u == sum of element energies, 1e-11
internal force           K u == the scatter of every Ke u_e
pressure                 -p A = -2100 N, prepared -2100, assembled -2100
gravity                  -rho V g = -22.6328 N, assembled -22.6328
material scaling         K(2E) == 2 K(E) entrywise, pattern unchanged
restraint edit           K, F and the source all unchanged
sparsity                 33 -> 40 entries per row while Ndof grew 420 -> 2550
                         1.56 MiB sparse against 49.61 MiB dense
```

## Five defects this milestone's own review found

All five were mine; all five are fixed. **Two were found by mutation probes
rather than by reading**, which is the clearest argument in this milestone for
running them.

```text
D1  the symmetry assertion claimed EXACT zero. It is not: P17-ELEM forms
    Ke(a,b) and Ke(b,a) as different sums of different products, so they agree
    to a few ulps. The bound is now DERIVED -- measured from the elements'
    own asymmetry times the worst contributor count -- and came out at exactly
    2x the element asymmetry against a bound of 22x

D2  the single-contributor regime does not exist on a solid Tet4 mesh, and the
    test required one. Replaced by checking the sum in EVERY contributor-count
    group: fifteen groups, 2 to 22 contributors, all correct

D3  the large-mesh test asserted "dense > 50 x sparse", got 32, and failed --
    an arbitrary threshold deciding the verdict. Replaced by a SCALING
    measurement at two sizes plus the exact bound nnz <= 144 * tets

D4  FOUND BY PROBE M8: the suite could not see a post-hoc symmetrisation,
    because averaging two values that already agree to 1e-17 changes nothing
    anyone measures. A detector was added -- a symmetrised matrix is EXACTLY
    symmetric on every mesh and an untouched one cannot be -- and M8 is now
    killed

D5  FOUND BY PROBE M14: the out-of-range read was checked on an UNLOADED
    vector, where row 0 is zero too, so a mutation that wrapped to row 0
    passed. Now checked on a vector whose row 0 is non-zero; M14 is killed
```

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md) carries all twenty-eight
attacks.

## Mutation protection

```text
14 probes, 11 KILLED, 3 SURVIVED
    M4   raw NodeId arithmetic -- 24 of 26, the broadest kill
    M7   only the upper triangle -- 12 of 26
    M10  one element skipped -- 11 of 26
    M1   += replaced by = -- 9 of 26, including BOTH rigid-mode tests
    M9   a diagonal regularisation -- 8 of 26
    M3, M11, M13  survived; each inert for a stated reason, and M11's
                  inertness is now pinned by an assertion so that if
                  PreparedLoads stopped being unique by node the probe would
                  start killing
```

One probe's first result was **void** — a collided build reported a LINKER
error, the known signature of the test binary being open while the harness
relinked it — and it was re-run on an idle build root. The void result is kept
in the log so the correction is visible.
[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

## Qualification

```text
3615/3615 in debug-ext, release-ext and debug-shared-ext, each from clean
0 warnings over 624 objects in each preset
660 x until-fail:5 in release-ext and in debug-ext
17 stages, 0 failed, qualify.cmd exit 0, 3 h 11 min
no-op rebuild: 0 Building or Linking lines in all three presets
the eight component hashes IDENTICAL before the first build and after the
last test run
```

`qualify.cmd` is byte-identical at `d313a640`, unchanged since P16-SIZE-001 and
now **eighteen milestones in a row**. [FREEZE.md](FREEZE.md).

## Known limitations

```text
no solve                       nothing is factorised or iterated. P17-SOLVE-001
no constraint application      K and F are UNCONSTRAINED; the ConstraintSet is
                               separate and is applied later
no reduced system              Kff / Ff are not produced. The assembler would
                               produce them from a non-empty ConstraintSet
                               unchanged, and authorizing that is
                               P17-SOLVE-001's
serial only                    no parallel scatter, deliberately: a
                               schedule-dependent summation order would make
                               the last bits of K depend on the machine
no performance claim           the large-mesh timing is informational and no
                               baseline was taken
six diagnostics untested       MeshHasNoDegreesOfFreedom, MeshHasNoElements,
                               ElementNodeMissing, ElementRejected,
                               LoadNodeMissing and NonFiniteSystem are
                               unreachable through the production API, because
                               possession of a StructuralModel and a
                               PreparedLoads proves they cannot happen. Kept
                               because a future input path would make them
                               live; each is named and the unreachability is
                               confirmed by a surviving probe
no bitwise cross-preset
  claim                        the properties are asserted in each preset; no
                               array was carried between runs and compared
```

## Result

```text
RESULT:   PASS
TESTS:    3615/3615 in debug-ext, release-ext and debug-shared-ext, each from
          clean; 0 warnings over 624 objects; 660 x 5 repeats in two presets;
          17 stages, 0 failed
          +32 over P17-BC-001: 20 unit, 6 reference, 6 compile-fail
MUTATION: 14 probes, 11 killed, 3 inert and explained
REVIEW:   28 attacks, 5 defects found and fixed, two of them by probes
TREE:     6d36b39e1d87bba04eee957a0b93da3ff49bf2dd, and the eight component
          hashes identical at both readings
ADR:      ADR-038, with two rejected alternatives recorded
EVIDENCE: this directory
TODO:     updated on PASS
```

## Revision

First issue, 2026-10-08.
