# ADR-039 — The structural solver is Eigen's SimplicialLDLT with a pivot gate, and Eigen is admitted PRIVATE to the structural module

Status: Accepted
Date: 2026-10-08

## Context

`P17-SOLVE-001` has to choose a sparse linear solver, and it is the milestone
two earlier documents reserved that choice for.

```text
src/structural/CMakeLists.txt   "NO LINEAR ALGEBRA YET. Eigen is in the tree,
                                pinned and already qualified, but it is
                                declared private to bettercad_sketch.
                                Admitting it here is P17-SOLVE-001's
                                decision [...] so this module does not link
                                it."

TODO.md                         "Eigen is MPL-2.0 and passes the project's
                                weak-copyleft admission rule; several common
                                sparse direct solvers do not. P17-SOLVE-001
                                must state the licence of anything it proposes
                                and leave a GPL/AGPL choice to the owner."

ADR-038                         BetterCAD owns the global CSR; a solver maps
                                or rebuilds it, and the structural Eigen
                                admission stays P17-SOLVE-001's
```

So two decisions are due: **which solver**, and **whether Eigen may be linked
into `bettercad_structural`.**

### What the audit found

```text
Library      Present?   Capability              Licence         Selected?
--------------------------------------------------------------------------
Eigen 5.0.1  YES, SHA   SimplicialLDLT/LLT,     MPL-2.0         YES
             pinned     CG, BiCGSTAB, SparseLU, (verified from
                        SparseQR, AMD ordering   the vendored
                                                 tree)
SuiteSparse  no         CHOLMOD, UMFPACK        LGPL / GPL      no
CHOLMOD      no         supernodal Cholesky     LGPL 2.1+       no
UMFPACK      no         unsymmetric LU          GPL 2+          no
SuperLU      no         sparse LU               BSD-3           no
MKL / LAPACK no         dense + PARDISO         ISSL / BSD      no
custom       --         --                      --              no
```

**No new dependency is needed and none was added.** Everything after the first
row is absent from the tree, and three of the five carry a licence that would
have decided BetterCAD's own — which `LICENSE` has not yet chosen. Adding one
would be the accident `TODO.md` warned about.

### The licence, verified from the vendored source rather than from memory

```text
library       Eigen
version       5.0.1, URL_HASH SHA256=e9c326dc8c05cd1e... (pinned in
              cmake/BetterCADDependencies.cmake)
licence       MPL-2.0
use           header-only, compiled into the static or shared
              bettercad_structural
```

Evidence, read out of the fetched tree:

```text
COPYING.README    "Eigen is primarily MPL2 licensed [...] Some files contain
                  third-party code under BSD or other MPL2-compatible
                  licenses, whence the other COPYING.* files here."
present           COPYING.MPL2, COPYING.BSD, COPYING.APACHE, COPYING.MINPACK
ABSENT            COPYING.LGPL and COPYING.GPL -- which Eigen 3.x DID ship,
                  because SimplicialCholesky and the AMD ordering were then
                  derived from LGPL code
```

Every file the chosen solver needs carries the Mozilla header:
`SparseCholesky/SimplicialCholesky.h`,
`SparseCholesky/SimplicialCholesky_impl.h`, `OrderingMethods/Ordering.h`,
`OrderingMethods/Amd.h`, `SparseCore/SparseMatrix.h`. `Amd.h` states it
explicitly:

> The author of CSparse, Timothy A. Davis., has executed a license with Google
> LLC to permit distribution of this code and derivative works as part of Eigen
> under the Mozilla Public License v. 2.0.

The only two occurrences of "LGPL" or "GPL" anywhere under `Eigen/` plus the
licence files are both benign: a historical note in `IncompleteLUT.h` saying
the SPARSKIT-derived ILUT code **was relicensed to MPL2 with the author's
permission**, and the MPL-2.0 text's own definition of "Secondary License".
Neither file is used by this milestone.

**MPL-2.0 is file-level weak copyleft.** The obligation attaches to Eigen's own
files, not to files that merely use them, so linking it does not propagate a
licence to BetterCAD's sources and does not decide the project's licence. That
is the admission rule `TODO.md` records, and it is why no owner decision is
required here.

## Constraints

```text
no penalty method        Kii += 1e20 is forbidden: it distorts conditioning
                         and reactions
no arbitrary pinning     an under-constrained model must FAIL, not be made
                         solvable by fixing node 0
no regularisation        Kff += eps*I would hide a singularity, which is
                         diagnostic information
determinism              measured, not assumed; no unordered iteration and no
                         environment-dependent threading
no Eigen in a PUBLIC
  header                 not one public header in the repository includes it,
                         and layer 50 is below io 60, renderer 70 and
                         scripting 70
library success is
  never sufficient       the solution must independently satisfy the equations
```

## Options

### Solver A — `SimplicialLDLT`, direct, SPD-oriented

`L D L^T` with an AMD fill-reducing ordering, no pivoting. Reports
`NumericalIssue` only when a pivot is **exactly** zero.

### Solver B — `SimplicialLLT`, direct, strictly SPD

`L L^T`. Fails when a pivot is `<= 0`, so it rejects indefinite matrices — but
not near-singular ones with tiny positive pivots.

### Solver C — `ConjugateGradient`, iterative

Needs a preconditioner, a relative tolerance, a maximum iteration count and an
initial-guess policy, all of which then become engineering semantics that must
be frozen and measured. Convergence on an ill-conditioned elastic system is
not guaranteed, and the iteration count is another thing to prove
deterministic.

### Solver D — `SparseLU`, direct, unsymmetric

Pivots, so it detects singularity more robustly, but discards the symmetry
that halves the work and the storage, and solves a harder problem than the one
BetterCAD has.

## Decision

**Solver A, `Eigen::SimplicialLDLT`, DIRECT — with an explicit pivot gate that
does not exist in the library.** Eigen is admitted to `bettercad_structural` as
a **PRIVATE** link, exactly as `bettercad_sketch` and `bettercad_assembly`
already have it, and no public header includes it.

A successful solve requires **three independent conditions**, and the library
supplies only the first:

```text
1  solver.info() == Success  AND  every D(k,k) > 0
                             AND  min|D| / max|D| >= pivotFloor
2  every component of uf is finite
3  the INDEPENDENT residual r = Kff uf - Ff passes the normalized gate
```

## Rationale

**Why direct.** `Kff` for a properly restrained linear-elastic model is
symmetric positive definite, which is the case a Cholesky-family factorisation
is for. A direct solve has no tolerance, no iteration count, no preconditioner
and no initial guess, so there are four fewer settings whose library defaults
could silently become engineering semantics — and the brief's own instruction
is to prefer the simplest robust solver and leave scale work to P26.

```text
maximum iterations         N/A, and deliberately absent from the settings
                           rather than present and ignored
iterative tolerance        N/A
preconditioner             N/A
ordering                   AMD (Eigen's default for SimplicialLDLT), recorded
                           because it is solver-local: it permutes rows and
                           columns internally and the solution is returned in
                           FreeEquationIndex order, never in the permuted one
```

**Why LDLT rather than LLT.** LDLT exposes `vectorD()`, which is what makes the
pivot gate possible — the diagonal of the factorisation is the measurement.
LLT hides it behind `sqrt(d)` and fails hard on `d <= 0`, which sounds stronger
and is not: it still accepts a tiny positive pivot, so it has the same blind
spot with less information.

**The pivot gate is the load-bearing part of this decision, and it exists
because the library's status is demonstrably insufficient.** Read out of
`SimplicialCholesky_impl.h`:

```cpp
if (DoLDLT) {
  m_diag[k] = d;
  if (d == RealScalar(0)) {
    ok = false; /* failure, D(k,k) is zero */
```

An exact zero. A singular matrix in floating point almost never produces one —
it produces a tiny or negative pivot — so **a free body factorises with
`info() == Success` and `solve()` returns an enormous but finite
displacement.** That is the brief's own automatic failure, delivered by the
library's happy path.

Worse, and this is why a residual check alone cannot replace the pivot check:
the garbage in that solution lies **in the null space**, so `Kff uf` is still
close to `Ff` and **the residual passes**. The two gates catch different
things, and neither is redundant. The measurements are in
`SINGULARITY_VALIDATION.md`.

`min|D| / max|D|` is the right form because it is **dimensionless**: `D` carries
the units of the stiffness diagonal, so the ratio is scale-free and a soft
material with a small `E` is not mistaken for a singular one. A raw diagonal
threshold would make exactly that mistake, which the brief names.

**Why Eigen may be admitted here.** It is already in the tree, already pinned
by hash, already MPL-2.0, and already linked into the same binaries through
`sketch` and `assembly`. Admitting it PRIVATE to a third module adds no
dependency, no licence obligation and no public-header exposure. The decision
was reserved for this milestone, and this is the milestone making it
deliberately — which is what both reserving documents asked for.

**What would have made a different option right.** If `Kff` were unsymmetric,
D would be wrong and `SparseLU` right. If the models were large enough that
fill-in dominated, an iterative method with a good preconditioner would win —
and P26 owns that. If Eigen had still shipped `COPYING.LGPL` as 3.x did, the
AMD ordering would have carried an LGPL obligation and the owner would have had
to choose; the vendored tree says it does not.

## Consequences

Easy:

```text
+ no new dependency, no licence decision made by accident
+ no tolerance, iteration count, preconditioner or initial guess to freeze
+ symmetry is exploited: SimplicialLDLT reads one triangle
+ vectorD() gives a principled, scale-free singularity measure the library
  does not itself act on
+ single-threaded: OpenMP is not enabled anywhere in the BetterCAD build and
  SparseCholesky contains no thread and no pragma, so determinism does not
  depend on a thread count
+ Kff stays sparse; nothing densifies
```

Hard:

```text
- fill-in. A direct factorisation of a 3D elastic system fills in, and that
  cost is not addressed here. P26 owns scale
- no factorization reuse. A load-only change could reuse the factorisation and
  deliberately does not: correctness first, and the API does not prevent
  adding it later
- near-incompressible materials (nu -> 0.5) condition badly, and the pivot gate
  will refuse what it cannot solve accurately. Reported honestly rather than
  accepted with a loosened tolerance
```

Committed to:

```text
Eigen is now linked (PRIVATE) by bettercad_structural, and the note in
src/structural/CMakeLists.txt that said otherwise is replaced by the record of
this decision. No public header includes it, so nothing above layer 50 sees it
```

## Rejected along the way

```text
a penalty method (Kii += 1e20)
    distorts conditioning and makes reactions meaningless. Searched: no
    1e12, 1e20 or penalty factor exists in the implementation

pinning node 0 or the first six DOFs
    silently changes the engineering model, which is the worst kind of defect:
    it produces a plausible answer to a question nobody asked

Kff += eps * I
    hides the singularity that is the diagnostic

trusting solver.info() alone
    measurably insufficient, as above. This is the single most important
    rejection in this ADR

requiring K u - F = 0 over the FULL system
    the constrained entries are the support REACTIONS and are correctly
    nonzero. The residual gate is on the free system; the full residual is
    retained for P17-REACTION-001

adding SuiteSparse / CHOLMOD / UMFPACK
    LGPL or GPL, absent from the tree, and would decide BetterCAD's licence

upgrading Eigen
    the pinned 5.0.1 is adequate; an upgrade inside this milestone would
    expand the regression surface for no gain
```

## Verification

```text
docs/verification/P17-SOLVE-001/
    SOLVER_SELECTION.md       the audit, the licence evidence, the settings
    CONSTRAINT_APPLICATION.md free-system extraction and reconstruction
    RESIDUAL_VALIDATION.md    the formula, the threshold and the measurements
    SINGULARITY_VALIDATION.md why the pivot gate is not redundant, measured
    DETERMINISM.md            repeat and cross-preset
```

The load-bearing claims are measured, not asserted: a 2x2 and a 3x3 SPD system
against closed-form solutions; a free body that the residual gate **passes**
and the pivot gate refuses; `min|D|/max|D|` orders apart between the
well-restrained and the under-restrained case, so the threshold does not decide
the verdict; constrained displacements exactly zero; and the mutation that
removes the residual gate killed.
