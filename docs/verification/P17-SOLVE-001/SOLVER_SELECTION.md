# P17-SOLVE-001 — solver selection and licence

## The audit, before any dependency was considered

```text
Library      Present?  Solver capability        Sparse format   Licence      Selected
-------------------------------------------------------------------------------------
Eigen 5.0.1  YES       SimplicialLDLT,          its own         MPL-2.0      YES
             SHA256    SimplicialLLT, CG,       SparseMatrix,   (verified
             pinned    BiCGSTAB, SparseLU,      buildable from  from the
                       SparseQR, AMD ordering   a CSR           vendored
                                                                 tree)
SuiteSparse  no        CHOLMOD, UMFPACK, SPQR   CSC             LGPL / GPL   no
CHOLMOD      no        supernodal Cholesky      CSC             LGPL 2.1+    no
UMFPACK      no        unsymmetric LU           CSC             GPL 2+       no
SuperLU      no        sparse LU                CSC/CSR         BSD-3        no
MKL          no        PARDISO, dense LAPACK    CSR             ISSL         no
LAPACK       no        dense only               --              BSD-3        no
custom       --        --                       --              --           no
```

**No new dependency was added.** Everything after the first row is absent from
the tree; three of the six carry a licence that would have decided BetterCAD's
own, which `LICENSE` has not yet chosen. Adding one would have been the
accident `TODO.md` warned about:

> Adding a copyleft solver would make that choice by accident. Eigen is
> MPL-2.0 and passes the project's weak-copyleft admission rule; several common
> sparse direct solvers do not. `P17-SOLVE-001` must state the licence of
> anything it proposes and leave a GPL/AGPL choice to the owner.

There is no such choice to leave: the selected solver is in a dependency the
project already has.

### What P17-ASSEMBLY-001 chose, and what this milestone needed from it

ADR-038 made the global stiffness a BetterCAD-owned CSR with a 64-bit unsigned
index. A zero-copy `Eigen::Map` over the global arrays is therefore **not**
available — Eigen requires its own *signed* `StorageIndex` — and that is stated
plainly here rather than claimed: the reduced system `Kff` is **built** by this
milestone, so the global index type never arises, and the one conversion that
does happen is `O(nnz)` once against a factorisation that is superlinear.

## Licence evidence

```text
library                        Eigen
version                        5.0.1
URL_HASH                       SHA256=e9c326dc8c05cd1e044c71f30f1b2e34a6161a3b6ecf445d56b53ff1669e3dec
                               (cmake/BetterCADDependencies.cmake)
licence                        MPL-2.0
use                            header-only, compiled into bettercad_structural
                               (static or shared), PRIVATE link
redistribution implication     MPL-2.0 is FILE-LEVEL weak copyleft: the
                               obligation attaches to Eigen's own files, whose
                               source is public and unmodified here. It does
                               not propagate to BetterCAD's files and does not
                               decide BetterCAD's licence
BetterCAD compatibility        admissible under the project's weak-copyleft
                               admission rule, which TODO.md already records
                               for Eigen
source of the information      the vendored tree itself, read file by file --
                               not memory, not the project's own notes
```

Read out of `_deps/eigen3-src`:

```text
COPYING.README     "Eigen is primarily MPL2 licensed [...] Some files contain
                   third-party code under BSD or other MPL2-compatible
                   licenses, whence the other COPYING.* files here."
present            COPYING.MPL2, COPYING.BSD, COPYING.APACHE, COPYING.MINPACK
ABSENT             COPYING.LGPL, COPYING.GPL
```

The absence matters. **Eigen 3.x shipped both**, because `SimplicialCholesky`
and the AMD ordering were then derived from LGPL code. 5.0.1 does not, and the
files say why:

```text
Eigen/src/SparseCholesky/SimplicialCholesky.h        Mozilla Public License v2.0
Eigen/src/SparseCholesky/SimplicialCholesky_impl.h   Mozilla Public License v2.0
Eigen/src/OrderingMethods/Ordering.h                 Mozilla Public License v2.0
Eigen/src/OrderingMethods/Amd.h                      Mozilla Public License v2.0
Eigen/src/SparseCore/SparseMatrix.h                  Mozilla Public License v2.0
```

`Amd.h` — the fill-reducing ordering `SimplicialLDLT` uses, and the file that
carried the LGPL obligation in 3.x — states it explicitly:

> The author of CSparse, Timothy A. Davis., has executed a license with Google
> LLC to permit distribution of this code and derivative works as part of Eigen
> under the Mozilla Public License v. 2.0.

The **only** two occurrences of "LGPL" or "GPL" anywhere under `Eigen/` plus
the licence files are both benign and neither is reachable from this milestone:

```text
Eigen/src/IterativeLinearSolvers/IncompleteLUT.h:93
    a historical note that the SPARSKIT-derived ILUT code was LGPL and
    "Yousef Saad gave us permission to relicense his ILUT code to MPL2".
    Not used here -- the solver is direct
LICENSE:68-70
    the MPL-2.0 text's own definition of "Secondary License". Boilerplate
```

### One thing this milestone did NOT do

`LICENSE` lists OCCT, Qt and Catch2 as third-party components and does **not**
list Eigen, Netgen or nlohmann_json. That is a pre-existing gap — Eigen has
been linked since P0–P10 through `bettercad_sketch` — and amending `LICENSE` is
a licensing decision for the owner, not this milestone's to make. It is
recorded here rather than silently edited.

## Selection

```text
Selected library     Eigen 5.0.1
Selected solver      Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>>
Direct / iterative   DIRECT
Matrix assumptions   symmetric positive definite; reads one triangle
Ordering             AMD (Eigen's default for SimplicialLDLT), SOLVER-LOCAL
Threading            single-threaded
```

Reason, in full in [ADR-039](../../architecture/decisions/ADR-039-the-structural-solver-is-eigens-simplicial-ldlt-with-a-pivot-gate-eigen-admitted-private.md):
`Kff` for a properly restrained linear-elastic model is SPD, which is what a
Cholesky-family factorisation is for; a direct solve has no tolerance, no
iteration count, no preconditioner and no initial guess, so there are four
fewer settings whose library defaults could quietly become engineering
semantics. LDLT rather than LLT because it exposes `vectorD()`, which is what
makes the pivot gate possible.

### Threading, audited rather than assumed

```text
OpenMP in the BetterCAD build     NOT ENABLED. No find_package(OpenMP), no
                                  -fopenmp anywhere in cmake/, CMakeLists.txt
                                  or any module's CMakeLists.txt
EIGEN_HAS_OPENMP                  appears only in Core/products/
                                  GeneralMatrixMatrix.h, Core/util/
                                  Parallelizer.h and SparseCore/
                                  SparseDenseProduct.h, and is gated on
                                  _OPENMP, which requires -fopenmp
SparseCholesky / OrderingMethods  contain no `pragma`, no `omp` and no
                                  `thread` (word-boundary search)
BLAS threading                    not applicable: no external BLAS is linked
```

So there is no thread count to fix and no environment variable that can change
the algorithm. The determinism measurement confirms it from the other side:
five runs of the same system are **bitwise identical**.

## Settings, with no library default inherited

```text
algorithm                      SimplicialLdlt            named, not defaulted
relativeResidualTolerance      1e-9      dimensionless   BetterCAD's own
pivotFloor                     1e-12     dimensionless   BetterCAD's own
maximum iterations             N/A -- the solver is direct, and the field is
                               ABSENT from the settings rather than present and
                               ignored. A compile-failure case proves it:
                               compile_fail.structsolve.settings-have-maximum-iterations
iterative tolerance            N/A, likewise
preconditioner                 N/A, likewise
                               compile_fail.structsolve.settings-have-preconditioner
ordering                       AMD, Eigen's default for this solver. SOLVER-
                               LOCAL: it permutes rows and columns inside the
                               factorisation and `solve()` returns the vector
                               in the order it was given, so the solution comes
                               back in FreeEquationIndex order and never in the
                               permuted one. Asserted, not trusted
initial guess                  N/A -- a direct solve has none, so there is no
                               hidden state from a previous solve
```

A mirror struct pins the settings' members, so adding an iteration count breaks
a `static_assert` rather than passing review:

```cpp
struct PermittedSettings {
    SolverAlgorithm algorithm;
    double relativeResidualTolerance;
    double pivotFloor;
};
static_assert(sizeof(SolverSettings) == sizeof(PermittedSettings), ...);
```

And a malformed setting is refused by BetterCAD, not by the library: a NaN
threshold would make `r < NaN` false and reject every solve for a reason no
diagnostic could explain, so `validate(SolverSettings)` rejects NaN, infinity,
zero and negative values for both tolerances, and the solve reports
`InvalidSettings` before any factorisation.

## Why the library's status is not the gate

Read out of `Eigen/src/SparseCholesky/SimplicialCholesky_impl.h`:

```cpp
if (DoLDLT) {
  m_diag[k] = d;
  if (d == RealScalar(0)) {
    ok = false; /* failure, D(k,k) is zero */
    break;
  }
}
...
m_info = ok ? Success : NumericalIssue;
```

An **exactly** zero pivot. So:

```text
an EXACTLY singular matrix       gives an exactly zero pivot, and Eigen sees
                                 it. Measured: [1 1; 1 1] returns
                                 NumericalIssue
a NUMERICALLY singular matrix    gives a tiny or negative pivot, and Eigen
                                 does NOT. Measured: a free structural body
                                 factorises with info() == Success and a pivot
                                 ratio of 3.4e-17
```

Because `NumericalIssue` from `compute()` has exactly one cause on this path,
it is reported as `SingularSystem` rather than `FactorizationFailure` — a
correction this milestone's own tests forced, recorded in
[SINGULARITY_VALIDATION.md](SINGULARITY_VALIDATION.md).

The remaining case — numerically singular, library happy — is what the pivot
gate exists for, and [SINGULARITY_VALIDATION.md](SINGULARITY_VALIDATION.md)
shows that a residual check could not have replaced it.

## Eigen's admission, and the invariant that now has a test

`bettercad_structural` links `BetterCAD::eigen` **PRIVATE**, as
`bettercad_sketch` and `bettercad_assembly` already did. No public header
includes it, so nothing above layer 50 sees it.

That invariant held by practice across the whole repository and was load-bearing
for this decision, so it is now **enforced**:

```text
tests/architecture/CheckLayering.cmake   rule 7: Eigen headers may be included
                                         from src/ only, never from a public
                                         header
architecture.checker.eigen-public-header a fixture holding BOTH directions -- a
                                         public header that includes
                                         <Eigen/SparseCholesky> and a src/ file
                                         that includes <Eigen/SparseCore> -- so
                                         the expected count of ONE proves the
                                         rule fires on the violation and not on
                                         the permitted use
```

Rule 1 could not have covered it: it keys on the `.hxx` extension, and Eigen's
entry headers have no extension at all. Writing the self-test immediately found
two defects in the rule — a message split across two list elements, and a
semicolon inside it that CMake treated as a list separator — which is why the
fixture exists rather than a review.
