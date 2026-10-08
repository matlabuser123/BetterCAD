# ADR-038 — The global stiffness matrix is a BetterCAD-owned CSR that Eigen can map

Status: Accepted
Date: 2026-10-08

## Context

`P17-ASSEMBLY-001` has to choose how the global stiffness matrix is stored.
Four things were already true when it started, and three of them are the
decision.

**Eigen 5.0.1 is already a dependency, and it is already PRIVATE.**

```text
cmake/BetterCADDependencies.cmake:50   BetterCAD::eigen, header-only,
                                       SHA256-pinned
src/sketch/CMakeLists.txt              PRIVATE_LINK BetterCAD::eigen
src/assembly/CMakeLists.txt            PRIVATE_LINK BetterCAD::eigen
tests/CMakeLists.txt                   bettercad_tests PRIVATE BetterCAD::eigen
```

So it is not a new dependency and it raises no new licence question: `TODO.md`
records that "Eigen is MPL-2.0 and passes the project's weak-copyleft admission
rule", and the open licence question is about *sparse direct solvers* — several
of which are GPL — which is `P17-SOLVE-001`'s to state.

**No public header in the repository includes Eigen.** Both modules that use it
include it only from a `.cpp` or a private `.hpp`, and only `<Eigen/Dense>`.
There is no `Eigen/Sparse`, no `Eigen::SparseMatrix` and no `Eigen::Triplet`
anywhere in the tree.

**`src/structural/CMakeLists.txt` says admitting it here is not this
milestone's call:**

> NO LINEAR ALGEBRA YET. Eigen is in the tree, pinned and already qualified,
> but it is declared private to `bettercad_sketch`. Admitting it here is
> `P17-SOLVE-001`'s decision […] so this module does not link it.

and `TODO.md` agrees: "Making it available to a structural module is an
explicit decision, not a side effect."

**ADR-037 already decided what an assembler indexes with.** `DofIndex` is a
1-based *identity*; `FreeEquationIndex` is a zero-based *position*, and its
rationale names the defect this milestone would otherwise have committed: "a
1-based handle would put a `- 1` at every assembly site, and the one that was
forgotten would be an off-by-one in the stiffness matrix".

## Constraints

```text
determinism            CLAUDE.md forbids depending on unordered iteration.
                       Many elements contribute to one global entry, so the
                       SUMMATION ORDER is a numerical contract, not a detail

layering               bettercad_structural is layer 50. A PUBLIC dependency
                       propagates to io 60, renderer 70 and scripting 70, and
                       to the app and the CLI

possession             ADR-036: a derived object that proves its inputs were
                       validated has a private constructor and one friend

no regularisation      a free body's K is singular and must stay singular;
                       nothing may add a diagonal epsilon

sparse                 production must never allocate Ndof x Ndof
```

## Options

### A — `Eigen::SparseMatrix<double>` in the public header

Admit Eigen to `bettercad_structural` as a PUBLIC dependency and let
`GlobalStructuralSystem` hold an `Eigen::SparseMatrix<double>` directly.
`P17-SOLVE-001` then factorises it with no conversion at all.

### B — `Eigen::SparseMatrix<double>` behind a pimpl

Admit Eigen PRIVATE, as sketch and assembly do, and hide the matrix behind an
opaque implementation pointer. The public header stays Eigen-free; the solver
reaches the Eigen object through a private accessor.

### C — BetterCAD owns a CSR; Eigen maps it when it is needed

`StiffnessMatrix` holds three `std::vector`s — row starts, column indices,
values — in compressed sparse row form, built by a symbolic pass and a numeric
pass. Nothing in the module links Eigen. `P17-SOLVE-001` wraps the same arrays
in `Eigen::Map<const Eigen::SparseMatrix<double>>` at zero copy, or hands them
to any other solver, when it makes its own dependency decision.

## Decision

**C.** The global stiffness matrix is a BetterCAD-owned CSR, assembled in two
passes, and `bettercad_structural` continues to link no linear algebra at all.

```text
class StiffnessMatrix        rows, cols, rowStart[rows+1], columns[nnz],
                             values[nnz] -- row-major, inner indices ascending,
                             values in SI N/m, private ctor + one friend

class ForceVector            dense, length Ndof, SI N, zero-initialised

struct GlobalStructuralSystem  StiffnessMatrix + ForceVector + the source stamp
```

The row space is the **free-equation numbering of the empty constraint set**:
`buildConstraintSet(numbering, {})` then `buildFreeEquationMap`. With nothing
constrained every degree of freedom is free, `freeCount() == dofCount()`, and
`equationOf(DofIndex)` returns the zero-based row directly — so the assembly
contains no index arithmetic whatsoever: no `- 1`, no `3 * node`.

## Rationale

**Assembly needs no linear algebra.** It needs a scatter-add and a container.
Option A and option B both link a library to obtain a *container*, which is the
tail wagging the dog — and A pays for it by making Eigen a public dependency of
every layer above 50.

**The determinism contract becomes BetterCAD's, and provable.** With a symbolic
pass there is exactly one slot per `(row, col)` and every contribution does
`+=` into it, in element-traversal order then local `(a, b)` order. Both orders
are frozen and neither comes from a container's iteration:

```text
element order   P16's own contract -- Mesh.hpp: "triangles and tetrahedra each
                in ascending ElementId […] Nothing here is an unordered
                container, so the same construction gives the same enumeration
                in Debug, Release and Debug-shared"
local order     row 0..11 then column 0..11, written out
load order      PreparedLoads::nodal(), "ascending by handle"
```

Options A and B would instead rest on `setFromTriplets`'s duplicate-summation
order, which Eigen documents as *summed* but not as summed in a specified
order. Depending on an unspecified order is exactly what the determinism rule
forbids, and the brief says so too: "Do not assume. Freeze with tests."

**It does not pre-empt an authority decision.** Both `src/structural/
CMakeLists.txt` and `TODO.md` reserve the structural Eigen admission for
`P17-SOLVE-001`. Option C leaves that decision entirely intact and hands the
solver a format it can map at zero copy, so nothing is lost by deferring.

**It is a representation, not a library.** There is no algebra here: no
factorisation, no multiplication operator, no transpose, no solve. The only
operations are the ones assembly and its own validation need —
`coeff(row, col)` by binary search within a row, a symmetry scan over stored
entries, and a finiteness scan. That is why this does not violate the brief's
"do not write a custom sparse-matrix library": nothing of the kind is written.

**What would have made A or B right.** If the solver choice were already made
and were Eigen's own `SimplicialLDLT`, option B's pimpl would avoid one map; if
BetterCAD had decided to expose Eigen types across its public API generally,
option A would be consistent rather than exceptional. Neither is true. And if
`setFromTriplets` documented its reduction order, the determinism argument
above would lose its force — it does not.

**And the cost is real but small**: ~200 lines of pattern-build and scatter-add,
against a public-dependency widening (A) or an opaque type that cannot be
compared, copied or fingerprinted without reaching through the pimpl (B).

## Consequences

Easy:

```text
+ every layer above structural stays Eigen-free, as it is today
+ determinism is a property of the code, not of a library's internals
+ nnz is a function of CONNECTIVITY ALONE, because all 144 local entries are
  emitted and none are pruned -- so the sparsity pattern is exactly "DOF pairs
  that share an element", predictable and identical in every preset
+ value semantics: the system is copyable, comparable and fingerprintable
+ Eigen::Map, SuiteSparse, MKL or a hand-written CG can all consume the same
  three arrays
```

Hard:

```text
- BetterCAD owns the CSR invariants and must test them: ascending inner
  indices, rowStart monotone, rowStart.back() == nnz
- no free matrix algebra. Anything beyond coeff/symmetry/finiteness has to be
  written or mapped, deliberately
- a future solver that wants a different storage order pays one conversion
```

Committed to:

```text
the global row space IS the free-equation numbering of the empty constraint
set, so a later reduced assembly is the same assembler with a non-empty
ConstraintSet -- and that is P17-SOLVE-001's to authorize, not this
milestone's to implement
```

## Rejected along the way

```text
std::unordered_map<pair<row,col>, double> accumulation
    hash order would decide the floating summation order. The determinism rule
    forbids it and the brief names it explicitly

triplet staging with a stable sort
    144 x Nelements triplets at 24 bytes each is 3.5 KB per element -- 345 MB
    for a 100k-element mesh, before compression. The symbolic pass costs
    O(nnz) instead, and needs no sort of values at all

pruning exact zeros
    makes nnz depend on numerical cancellation rather than connectivity, which
    would make the sparsity-pattern test fragile and the cross-preset nnz
    comparison meaningless

post-symmetrising K
    0.5 * (K + K^T) would hide a scatter bug rather than fix a numerical one.
    Assembly preserves symmetry because every Ke is symmetric and the scatter
    is symmetric; a mutation probe confirms that adding the symmetrisation
    conceals a real defect

a diagonal epsilon, a pinned node, penalty springs
    a free body's K is singular and that is correct physics. Any
    regularisation is a solver's policy
```

## Verification

```text
docs/verification/P17-ASSEMBLY-001/
    SPARSE_REPRESENTATION.md    storage, index width, duplicate semantics
    ASSEMBLY_ALGORITHM.md       the two passes and both frozen orders
    ANALYTICAL_VALIDATION.md    dense oracle, energy, internal force, rigid modes
    DETERMINISM.md              repeat and cross-preset fingerprints
    LARGE_MESH.md               the sparse-storage smoke
```

The decision's load-bearing claims are tested, not asserted: a dense oracle
built test-side from independent `Ke` values compares every entry of the
production CSR; `K r = 0` for the six rigid-body modes; nullity exactly six on
a small connected fixture; `nnz` and the ordered coordinate pattern identical
across the three presets; and the mutation that replaces `+=` with `=` is
killed.
