# P17-ASSEMBLY-001 — determinism and the large mesh

## Why determinism is a numerical contract here and not a formality

Up to **22** elements write a single global entry on the unit fixture. Floating
addition is not associative, so the order those 22 contributions are summed in
decides the last bits of that entry. The traversal order is therefore part of
the answer, and it is frozen.

```text
element order   P16's own: Mesh.hpp guarantees "triangles and tetrahedra each
                in ascending ElementId [...] Nothing here is an unordered
                container, so the same construction gives the same enumeration
                in Debug, Release and Debug-shared"
local order     row 0..11 then column 0..11, written out as two loops
load order      PreparedLoads::nodal(), "ascending by handle" -- asserted
                sorted and without repeats
```

**This milestone writes no sort for any of them.** It also uses no unordered
container at all: searched over the implementation with comments stripped,
`unordered` appears zero times, and so do `thread`, `atomic`, `omp` and
`parallel`.

```text
race-sensitive assembly avoided by design: the assembly is serial.
```

A deterministic parallel merge — thread-local buffers reduced in a fixed order
— is a later performance decision with its own evidence. Nothing here claims a
performance result, so nothing here needs one.

## And no sort of values is needed at all

A triplet staging would have had to be stably sorted and then reduced, and the
reduction order would have been the thing to prove. The symbolic pass removes
the question: there is exactly **one slot per (row, column)** and every
contribution does `+=` into it, so the accumulation order IS the traversal
order and there is nothing between them.

## Repeat determinism

```text
Fixture              Runs   Ndof   nnz    K pattern   K values   F values   PASS
-------------------------------------------------------------------------------
block, 2 nodal loads    2     57   1647   identical   identical  identical  PASS
```

The fingerprints are the arrays themselves, compared element for element with
`std::ranges::equal` and **no tolerance** — there is nothing to tolerance when
the operations are the same operations in the same order. `rowStart`,
`innerIndices`, `values` and the force vector are each compared, plus
`operator==` on the matrix, the vector and the whole system.

```text
ctest -R "StructuralSystem_" --repeat until-fail:5     26/26, 130 executions
```

## The recorded element order

`GlobalStructuralSystem::elementOrder()` carries the order the stiffness was
summed in, and the test asserts it

```text
equals the mesh's own tetrahedron order, element for element   PASS
is ascending                                                    PASS
has one entry per tetrahedron                                   PASS
```

so the contract is visible in the object rather than only in a comment. The
mutation that records the order reversed is killed by four tests.

## Load-order independence

Two nodal loads given in both orders produce an identical `F`, compared
entrywise. Order is the user's and is preserved in `PreparedLoads`'
contributions; it cannot reach the vector, because the field is accumulated
into an ascending map before assembly ever sees it.

## Cross-preset

The three-preset qualification runs the full unfiltered suite in `debug-ext`,
`release-ext` and `debug-shared-ext`, each from a clean build root with its own
freshly built binaries, and the repeat selection five times in two of them. The
quantities compared are:

```text
Ndof                          integer identity
nnz                           integer identity
sparse coordinate pattern     integer identity (rowStart, innerIndices)
K values                      asserted against the independent oracle in each
                              preset, to 1e-12 relative
F values                      asserted exactly against the prepared field
symmetry                      bounded by the measured element asymmetry in
                              each preset
rigid-mode residuals          bounded at 1e-10 relative in each preset
diagnostics                   string identity
```

**No claim of bitwise equality across presets is made.** The tests assert the
properties above in each preset independently; they do not compare one
preset's `values` array against another's, so no such claim is recorded.
`-ffast-math` is not used and the arithmetic is the same source in every
preset, but establishing bitwise equality would need the arrays carried between
runs and that was not done.

## No result shopping

One traversal rule was chosen before the measurements — P16's own ascending
`ElementId` order, because it is already a qualified contract — and nothing was
re-run under a different order to pick a nicer number. The one ordering
experiment that exists is a *mutation*: recording the order reversed, which is
killed.

---

# The large mesh

## Fixture

RM-MESH-02's cylinder, which is curved: P16-SIZE-001 records that "OCCT
triangulates a PLANAR face with two triangles whatever the deflection", so only
a curved wall responds to a finer surface control. Two sizes of the same model,
because *sparse* is a statement about scaling.

```text
RM-MESH-02           nodes   tets   Ndof    nnz   nnz/row   ||K-K^T||  relative
0.25 mm / 12 mm        140    508    420  14166        33   7.63e-06   4.54e-17
0.025 mm / 6 mm        850   4210   2550 102276        40   9.16e-05   3.04e-17
```

```text
Assembly completed      YES, at both sizes
Dimensions correct      Ndof == 3 * nodes, K is Ndof x Ndof, |F| == Ndof
Finite                  every stiffness value and every force entry
Symmetric               bounded, and relative error ~3e-17
Rigid translations      all three axes below 1e-10 relative, at both sizes
Elapsed                 27.6 s for the test case, informational only
Peak memory             not instrumented; the storage figures below are the
                        measurement that was taken
```

Timing is **not a gate** and no threshold is asserted: a wall-clock bound would
be a bound on this machine.

## Sparse, as a measurement

```text
entries per row         33  ->  40        while Ndof went 420 -> 2550
nnz                 14166  ->  102276     while tets went  508 -> 4210
nnz <= 144 * tets                         102276 <= 606240        PASS
```

Entries per row is a property of connectivity, not of size, and it did not grow
when the mesh did — whereas a dense matrix's entries per row *is* `Ndof` and
would have grown six-fold. At the larger size:

```text
sparse storage (values + 64-bit indices)      1.56 MiB
a dense Ndof x Ndof of doubles               49.61 MiB
```

**The first draft of this test asserted "dense is more than fifty times
sparse".** It came out at thirty-two and failed — an arbitrary threshold
deciding the verdict, which brief section 77 warns against. It was replaced by
the scaling claim above and the exact structural bound, neither of which
contains a tuned number.

## No dense allocation in production

```text
Eigen in src/structural or include/bettercad/structural   0 occurrences
Eigen::MatrixXd in production                             0
a dense Ndof x Ndof buffer in production                  none: the only
                                                          allocations are
                                                          rowStart (Ndof+1),
                                                          inner (nnz),
                                                          values (nnz),
                                                          force (Ndof), and the
                                                          symbolic pattern,
                                                          which is O(nnz)
```

The symmetry and finiteness validations both iterate **stored entries**:
`largestSymmetryError` walks the CSR and finds each counterpart with a binary
search within its row, so neither check densifies anything. Dense matrices
appear only in the test oracles, and only on the smallest fixtures.

## Reference-model table

```text
Model                  nodes  tets   Ndof    nnz   ||K-K^T||   relative   finite  PASS
--------------------------------------------------------------------------------------
RM-MESH-01                 8     6     24    414   0           0          YES     PASS
RM-MESH-03                81   126    243   5895   7.63e-06    6.59e-17   YES     PASS
RM-MESH-04               261   808    783  24183   3.81e-06    5.95e-17   YES     PASS
RM-MESH-07                61   267    183   6543   7.63e-06    6.03e-17   YES     PASS
RM-MESH-07 @ 12 mm        14    31     42   1026   1.91e-06    2.17e-17   YES     PASS
RM-MESH-07 @  4 mm       153   769    459  18063   1.53e-05    6.72e-17   YES     PASS
RM-MESH-02 @ 0.25/12     140   508    420  14166   7.63e-06    4.54e-17   YES     PASS
RM-MESH-02 @ 0.025/6     850  4210   2550 102276   9.16e-05    3.04e-17   YES     PASS
block fixture             19    ~  57 1647          1.91e-06    5.35e-17   YES     PASS
```

RM-MESH-01's symmetry error is **exactly zero**, at six tetrahedra: with that
few contributions the element asymmetries cancel. That is why the
strictly-positive symmetry detector lives on the block fixture and the
reference models only bound the error from above — a point worth recording,
because asserting a positive error everywhere would have been wrong.

RM-MESH-03 and RM-MESH-04 carry a through-hole and a void. P17-ASSEMBLY
understands neither; it consumes valid Tet4 connectivity. That the rigid
translations remain exact null modes on RM-MESH-04 is the evidence that no
coupling was invented across the empty space.

## Local refinement

```text
RM-MESH-07     nodes   tets   Ndof    nnz
12 mm             14     31     42   1026
 4 mm            153    769    459  18063
```

Different `Ndof`, different `nnz`, different matrix dimensions — **both
valid**. Raw matrix equality is not the claim and would be wrong. The premise
is asserted first (the finer mesh really has more nodes and more elements), and
then that `Ndof` and `nnz` follow it and both systems are well formed.
