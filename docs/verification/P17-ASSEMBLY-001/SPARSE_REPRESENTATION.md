# P17-ASSEMBLY-001 — the sparse representation

## The audit that came first

The brief's step 1 is to inspect what already exists before writing a matrix
container. What exists:

```text
Capability              Existing implementation          Suitable?  Visibility
-----------------------------------------------------------------------------------
dense linear algebra    Eigen 5.0.1, SHA256-pinned       yes        PRIVATE to
                        cmake/BetterCADDependencies:50              bettercad_sketch
                                                                    and
                                                                    bettercad_assembly
sparse matrix           NONE. No Eigen/Sparse, no        --         --
                        Eigen::SparseMatrix and no
                        Eigen::Triplet appears anywhere
                        in the tree; both modules that
                        use Eigen include only
                        <Eigen/Dense>
CSR / compressed        NONE                             --         --
sparse solver wrapper   NONE                             --         --
matrix/vector aliases   sketch and assembly use          no         private .hpp
                        Eigen types directly in their               and .cpp only
                        private solver headers
deterministic sparse    NONE                             --         --
assembly
Eigen in a PUBLIC       NONE. Not one public header in   --         --
header                  the repository includes it
```

Decision, and the alternatives, are recorded as
[ADR-038](../../architecture/decisions/ADR-038-the-global-stiffness-matrix-is-a-bettercad-owned-csr-that-eigen-can-map.md):
**BetterCAD owns the CSR and `bettercad_structural` links no linear algebra at
all.** Three facts drove it.

```text
assembly needs no algebra        it needs a scatter-add and a container.
                                 Linking a library to obtain a container
                                 would make Eigen a PUBLIC dependency of
                                 layer 50 and propagate it to io 60,
                                 renderer 70, scripting 70, the app and the
                                 CLI

determinism must be BetterCAD's  many elements write one entry, so the
                                 SUMMATION ORDER is a numerical contract.
                                 Eigen documents setFromTriplets as summing
                                 duplicates but not as summing them in a
                                 specified order, and CLAUDE.md forbids
                                 depending on an unspecified order

the decision is not this
milestone's to make             src/structural/CMakeLists.txt: "Admitting it
                                 here is P17-SOLVE-001's decision". TODO.md:
                                 "Making it available to a structural module
                                 is an explicit decision, not a side effect"
```

And the licence question is already settled for Eigen, so nothing was decided
by accident: `TODO.md` records that "Eigen is MPL-2.0 and passes the project's
weak-copyleft admission rule". The open question is about sparse DIRECT
SOLVERS, several of which are GPL, and that is `P17-SOLVE-001`'s to state.

**Verified, not asserted** — searched over
`src/structural/StructuralSystem.cpp` and
`include/bettercad/structural/StructuralSystem.hpp`:

```text
Eigen          0 occurrences
SparseLU, LDLT, BiCGSTAB, ConjugateGradient   only in a comment saying there
                                              are none
unordered      only in two comments, about P16's ordering guarantee
```

## The format

```text
Sparse type              bettercad::structural::StiffnessMatrix
Storage                  compressed sparse ROW
                             rowStart  size rows() + 1, monotone,
                                       rowStart[0] == 0,
                                       rowStart[rows()] == nonZeros()
                             inner     size nonZeros(), STRICTLY ASCENDING
                                       within each row
                             values    size nonZeros(), parallel to inner
Index type               std::uint64_t  (StiffnessMatrix::Index)
Index width              64-bit, deliberately
Value type               double, SI, N/m
Typed accessor           coeff(row, column) -> Stiffness
Vector type              bettercad::structural::ForceVector, dense
                             std::vector<double>, SI, N, length Ndof,
                             EXPLICITLY zero-initialised
Typed accessor           operator[](row) -> Force
Compression              none needed: the symbolic pass lays out the final CSR
                         directly, so there is no uncompressed phase and no
                         finalisation step to forget
Duplicate semantics      UNREPRESENTABLE. There is exactly ONE slot per
                         (row, column) and every contribution does `+=` into
                         it, so a duplicate is a sum by construction rather
                         than by a reduction policy
Zero entries             all 144 local entries are emitted and NONE are
                         pruned, so the pattern is a function of CONNECTIVITY
                         ALONE. An absent entry reads as zero; a stored
                         explicit zero is permitted
Mappable                 the three arrays are laid out for
                         Eigen::Map<const Eigen::SparseMatrix<double>>,
                         SuiteSparse or anything else, at zero copy
```

### Why 64-bit, and the bound proved rather than hoped for

A 32-bit inner index is the classic sparse overflow. Measured here, `nnz` runs
at 33 to 40 entries per row, so a 32-bit entry count would cap a model at about
50 million degrees of freedom — plausible for a real analysis, and the kind of
limit that is discovered by a wrong answer. The header carries the proof:

```cpp
static_assert(static_cast<StiffnessMatrix::Index>(kDofsPerNode) *
                  static_cast<StiffnessMatrix::Index>(
                      std::numeric_limits<meshing::NodeId::ValueType>::max()) <
              std::numeric_limits<StiffnessMatrix::Index>::max() / 2);
```

A `NodeId` is 32-bit and the mesh's handles are strictly increasing, so a mesh
holds at most `2^32 - 1` nodes; `3N` fits in a 64-bit index with four orders of
magnitude to spare. No row index and no count derived from one can overflow.

**No silent narrowing.** `-Wconversion -Wsign-conversion -Werror` are on and
the build carries zero warnings, so a `std::size_t`-to-`int` narrowing could
not have slipped in.

### The pattern is connectivity, and it is checked both ways

Measured on the unit fixture: 19 nodes, 57 degrees of freedom, **1647**
stored entries. The test builds the expected set independently — walking the
elements and recording every `(row, column)` pair — and compares in both
directions:

```text
nonZeros() == the number of distinct pairs that share an element     PASS
every stored pair is one of them                                     PASS
every one of them is stored                                          PASS
```

So the sparsity pattern cannot contain an invented coupling (a spatial
neighbour rather than a shared node) and cannot be missing a real one. That is
also what makes `nnz` identical in every preset: it depends on no numerical
value.

### Sparse, measured as a scaling property

```text
RM-MESH-02           nodes   tets   Ndof    nnz     nnz/row
0.25 mm / 12 mm        140    508    420   14166        33
0.025 mm / 6 mm        850   4210   2550  102276        40
```

Entries per row is a property of the connectivity, not of the size, and it
stayed at 33 to 40 while `Ndof` grew six-fold — whereas a dense matrix's
entries per row IS `Ndof` and would have grown with it. At the larger size:

```text
sparse storage (values + indices)     1.56 MiB
a dense Ndof x Ndof of doubles       49.61 MiB
```

The first draft of that test asserted "dense is more than fifty times sparse",
which came out at thirty-two — an arbitrary threshold deciding the verdict,
which brief section 77 warns against. It was replaced by the scaling claim
above plus the exact structural bound `nnz <= 144 * tets`.

## Units, frozen

```text
K   N/m        u   m        K u   N        F   N
```

so `K u = F` is an equation between forces. `coeff()` returns `Stiffness` and
`operator[]` returns `Force`; neither converts to the other and neither decays
to a `double`, which three build-failure cases enforce:

```text
compile_fail.structasm.stiffness-as-force
compile_fail.structasm.force-as-stiffness
compile_fail.structasm.stiffness-as-double
```

`values()` is the one deliberate SI escape hatch, documented as the buffer a
solver maps, and the reason the stored type is `double` rather than `Stiffness`
— the same choice `Tet4Stiffness` made for the same reason. There is no
internal conversion to `N/mm` for anyone's convenience.

## Possession is the evidence

`StiffnessMatrix`, `ForceVector` and `GlobalStructuralSystem` each have a
private constructor and exactly one friend (ADR-036), and the system has **no
default constructor at all** — it cannot exist without a matrix, a vector and a
numbering that were each built by the function entitled to build them. Three
more build-failure cases enforce it:

```text
compile_fail.structasm.stiffness-matrix-constructed-directly
compile_fail.structasm.force-vector-constructed-directly
compile_fail.structasm.global-system-constructed-directly
```

So a caller cannot hand a solver a matrix whose pattern was never validated,
whose dimensions do not match a numbering, or which holds a non-finite value.

## Derived, never persisted

```text
K, F, rowStart, inner, values, the pattern, the element order
```

are all rebuilt from the model. Searched: nothing in `io`, in any command, in
any `DocumentObject` or in any serializer holds one, and there is no
`GlobalStructuralSystem` field anywhere outside this module's own test files.
