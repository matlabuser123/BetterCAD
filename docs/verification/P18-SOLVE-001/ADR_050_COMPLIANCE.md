# P18-SOLVE-001 — ADR-050 compliance

```text
P18 layer/order:                          thermal 45
P17 structural layer/order:               structural 50
generic solver (new) layer/order:         linalg 5

direct include P18 -> P17 solver:         NO
direct link P18 -> P17 structural target: NO
hand copy of the P17 solver into P18:     NO
P17-specific orchestration moved:         NO (it stays in structural)
reuse mechanism:                          EXTRACTION below 45 (ADR-052)
```

## The proofs, each by a test

| Claim | Proof |
| --- | --- |
| thermal includes nothing from structural | `architecture.layering` over every file under include/, src/, apps/, examples/ (525 files, 0 violations) and the standing fixture `architecture.checker.thermal-to-structural` |
| linalg includes nothing above core | fixtures `architecture.checker.linalg-to-thermal` and `linalg-to-structural`; `set(layer_linalg 5)` in the checker |
| thermal does not LINK structural | `architecture.targets.link-graph` reads `src/thermal/CMakeLists.txt` (comment lines ignored) and fails on `BetterCAD::structural` or `bettercad_structural`; the fixture `architecture.targets.thermal-links-structural` holds exactly that private link and must be refused |
| thermal reaches the solve through linalg | the same check requires `BetterCAD::linalg` in thermal's link list |
| linalg links only core and Eigen | the same check; the `targets-valid` fixture shows a comment naming structural is not a violation |
| Eigen stays out of public headers | rule 7 (`architecture.checker.eigen-public-header`); `include/bettercad/linalg/SymmetricSolve.hpp` includes no Eigen |
| the P17 solver is not copied | the thermal solve contains no factorisation: `src/thermal/ThermalSolve.cpp` calls `linalg::solveSymmetricSparse` and `linalg::evaluateResidual` and nothing else numerical (audit in ADVERSARIAL_REVIEW.md); P17's kernel exists once, in `src/linalg/SymmetricSolve.cpp` |
| no tolerances, singularity logic or diagnostics were duplicated | one pivot gate, one residual normalisation, one set of defaults (`ThermalSolverSettings`, a record of the same two ratios P18-DATA already owned) |

## What the link check cannot see, and what covers it

A link dependency can also arrive through a `target_link_libraries` outside the module's own
CMakeLists. The repository has none for thermal: the only places `BetterCAD::thermal` appears are
the module's own file and the test, CLI and compile-fail targets that consume it. The check is a
source-level audit of the two files ADR-052 constrains, and says so; it is not a graph walk of the
generated build system.

## Mutation of the gate itself

Adding `BetterCAD::structural` to thermal's `PRIVATE_LINK` makes `architecture.targets.link-graph`
fail; adding `#include <bettercad/structural/StructuralSolve.hpp>` to `ThermalSolve.cpp` makes
`architecture.layering` fail (both in MUTATIONS.md, probes ARCH1 and ARCH2).