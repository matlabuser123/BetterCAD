# P18-SOLVE-001 — solver architecture and the linear-algebra audit

```text
SUBJECT:  what linear algebra the repository already owned, who owned it, whether thermal
          could use it, and what was done about it
RESULT:   P17's symmetric sparse solve was EXTRACTED, mechanically, to a new module `linalg`
          at layer 5; structural now calls it through a wrapper, thermal calls it through its
          own; nothing was copied
```

## Baseline capture

```text
HEAD                 241c13aba2701e661e355c5a310d3d04c06709b4   (P18-ASSEMBLY-001's record commit)
HEAD tree            9324d82b91f93eca49ad802989d6505af07236a8
origin/main          241c13a...  (the owner's push of both P18-ASSEMBLY-001 commits, verified)
working tree         clean apart from the session's untracked .claude/
git diff / --cached  empty; git diff --check exit 0
predecessor suite    4546 of 4546, three presets (P18-ASSEMBLY-001)
```

Prerequisites read before any code: the committed evidence of P18-DATA, P18-DOF, P18-BC and
P18-ASSEMBLY, P17-SOLVE-001's header and implementation (`StructuralSolve.hpp/.cpp`), ADR-039
(the solver and Eigen), ADR-050 (the layer decision and the two admissible answers) and the layering
checker. TODO.md ticks all eight P18 predecessors and P16/P17 as qualified.

## The audit

| Concern | Existing owner | Layer | Reusable by P18? | Action | Reason |
| --- | --- | --- | --- | --- | --- |
| sparse matrix type | `thermal::ThermalSparseMatrix` (P18-ASSEMBLY); `structural::StiffnessMatrix` (P17) | 45; 50 | thermal's own is what the solve receives; the CSR layout is identical to P17's | none | the solver takes three arrays, not a matrix class, so the two types need no common base |
| direct solve algorithm | `structural::solveSymmetricSparse` (`runKernel`) | 50 | **yes, by extraction only** | moved to `linalg::solveSymmetricSparse` | thermal (45) cannot include structural (50); a copy is refused by ADR-050 |
| factorisation backend | Eigen 5.0.1 `SimplicialLDLT`, private to structural (ADR-039); also private to sketch and assembly | — | yes | `linalg` links Eigen PRIVATE | one more private link on a pinned, MPL-2.0 library already in three modules; rule 7 unchanged |
| singularity detection | P17's three-condition gate (pivot ratio, finiteness, independent residual) | 50 | yes | moved with the kernel, unchanged | the library reports success on a singular matrix; the gate is the delicate qualified idea |
| rank / factorisation status | `solver.info()` and `vectorD()` read inside the kernel | 50 | yes | moved with the kernel | NumericalIssue means an exactly zero pivot; the ratio covers the rest |
| tolerances | `structural::SolverSettings` (canonical, ADR-047): residual 1e-9, pivot floor 1e-12 | 50 | the VALUES are right for thermal, the RECORD is structural | thermal keeps its own canonical record, `ThermalSolverSettings` (P18-DATA), same two ratios | measured in `RESIDUAL_VALIDATION.md`; one source for each |
| diagnostics | `structural::SolveProblem` and P17's wording | 50 | the classes, not the words | the kernel returns a structured failure; each physics words it | a generic layer must not learn "under-constrained" or "newtons" |
| determinism | serial CSR extraction, setFromTriplets over a CSR with no duplicates, AMD ordering inside SimplicialLDLT | — | yes | measured here | see `DETERMINISM.md` |
| residual implementation | a loop over the original CSR, computed by BetterCAD | 50 | yes | moved, and exposed as `evaluateResidual` | so a wrong finite answer can be shown to the gate |
| free-system reduction | `structural::extractFreeSystem` | 50 | NO: it reads P17's constraint set and has no `- Kfc Tc` term | thermal writes its own, over P18-DOF's maps | the reduction is the physics-specific half; the partition is stated once per physics |

**Who owned the generic sparse solve?** `structural`, layer 50. **Can thermal legally depend on it?** No
(ADR-050; `architecture.checker.thermal-to-structural`). **Does P17 already wrap a lower-level generic
solver?** No: Eigen was the lowest level, called from inside `structural`. Hence the extraction.

## What was done

```text
include/bettercad/linalg/SymmetricSolve.hpp   NEW  the public surface: settings, ResidualNorms, the failure, solve, evaluateResidual
src/linalg/SymmetricSolve.cpp                 NEW  P17's runKernel, moved; structured failure instead of P17's prose
src/linalg/CMakeLists.txt                     NEW  bettercad_linalg (core PUBLIC, Eigen PRIVATE)
src/structural/StructuralSolve.cpp            EDIT the kernel replaced by a wrapper: units on the numbers, P17's three
                                                   messages rebuilt byte for byte; extractFreeSystem etc. untouched
src/structural/CMakeLists.txt                 EDIT + PRIVATE_LINK BetterCAD::linalg
src/thermal/CMakeLists.txt, ThermalSolve.*    NEW  thermal's reduction, anchoring analysis, solve, provenance
tests/architecture/CheckLayering.cmake        EDIT layer_linalg 5
```

No public structural header changed, no structural test changed, and the structural suite
(415 cases, 271 414 assertions) passes unchanged against the extracted kernel. ADR-052 records the decision.

## The dependency path

```text
bettercad_thermal  --PRIVATE-->  bettercad_linalg  --PRIVATE-->  Eigen (header-only, MPL-2.0)
                                                   --PUBLIC--->   bettercad_core
bettercad_structural --PRIVATE--> bettercad_linalg                 (the same kernel)
bettercad_thermal  -X->  bettercad_structural                      (no include, no link)
```