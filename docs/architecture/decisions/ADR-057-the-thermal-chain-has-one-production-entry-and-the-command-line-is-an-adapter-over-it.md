# ADR-057 — The thermal chain has one production entry, and the command line is an adapter over it

```text
STATUS:    Accepted
DATE:      2026-10-25
MILESTONE: P18-CLI-001
TOUCHES:   ADR-049 (the thermal modes; a result is derived state), ADR-051 (the analysis boundary and the order of its gates), ADR-055 (the thermal commands are the only edit path),
           ADR-056 (the file holds intent only), ADR-048 and P17-CLI-001 (the structural precedent: `solveStructuralAnalysis` and an adapter over it),
           ADR-009 (the edit spine: one registration makes a verb a single-shot command and a batch verb), ADR-028 (no solver holds material data of its own)
```

## Context

The brief for this milestone is one sentence long where it matters: **the CLI is an adapter.** It may parse, load, call the existing core, format and exit; it may not own Tet4 formulation, load or boundary
integration, assembly, a solve, time integration, recovery of a gradient or a flux, a heat balance, an acceptance threshold, a currentness rule or a persistence format.

Auditing what the CLI would call found one gap. The structural analysis has a production entry (`structural::solveStructuralAnalysis`) that runs the chain and applies the validator, and the structural CLI is a
thin adapter over it. **The thermal analysis had nothing equivalent.** Every link of the chain exists and is qualified:

```text
requireThermalModel  ->  buildThermalDofMap  ->  prepareThermalLoads  ->  prepareThermalBoundary  ->  assembleThermalSystem
  ->  solveSteadyThermal | solveTransientThermal  ->  recover*Thermal  ->  recover*Balance  ->  validateThermalAnalysis
```

but **the order they run in existed in exactly one place: the test support of the viewer milestone** (`tests/renderer/ThermalVizTestSupport.hpp`, `derive`). A CLI that wanted a result would have had to write that chain
again, and a chain written twice is two answers to the question "what does a thermal solve consist of".

## Constraints

* The CLI must contain no thermal implementation, so the chain cannot live in the CLI.
* No qualified file may change; the new entry must be additive.
* The thermal module (layer 45) already links `features`, `meshing` and `core`; a new entry needs nothing else.
* The entry must not decide anything: acceptance is P18-VALID's (ADR-051), and a second opinion there is the defect the module boundary exists to prevent.
* A thermal result is derived state (ADR-049): the entry must write nothing, and must not mesh (meshing is an explicit request, as for `requireThermalModel` and `solveStructuralAnalysis`).

## Options

### A — the CLI composes the chain itself

Rejected: it is a second copy of the order, in a layer that should hold none of it, and the brief's first automatic FAIL.

### B — promote the test support's `derive` to production unchanged

Rejected: it stops at the first refusal and returns a bag of optionals with a *string*; the validator's report cannot be produced from "what exists" without a caller re-deriving which rule to blame, and the function
is named and shaped for a test.

### C — a new function in the thermal module that runs the chain and always returns a complete account (chosen)

## Decision

1. **`thermal::runThermalAnalysis(document, regenerator, mesher, analysis)` in `include/bettercad/thermal/ThermalAnalysisRun.hpp` and `src/thermal/ThermalAnalysisRun.cpp`.** It calls each link exactly once, in the
   order above, with the arguments the earlier milestones' own seams take, reading the definition from the document **now**; there is no parameter through which a caller could substitute a tolerance, an initial
   state or a time step.
2. **It always returns a `ThermalAnalysisRun`**: the derived objects that exist (each an `std::optional`, so "not built" is not a default-constructed object), the stage that stopped the chain
   (`ThermalRunStage`, a stable name), the refusal as the link reported it, and **the P18-VALID report on exactly what exists**. A caller that got only an error would have to guess which rule to blame.
3. **`accepted()` is `!stoppedAt.has_value() && validation.accepted()`**, spelled exactly so (the ownership gate checks the spelling). A solver that returned success is not an input: the validator re-judges the
   residual, the finiteness, the recovered fields and the heat balance independently of the links that produced them.
4. **`lastSolveFailure` is handed to the validator only when the SOLVE is what stopped the chain.** A refusal by the recovery or the balance leaves a solution behind, and the validator reports the missing field or
   balance under its own rule (`post_missing`, `balance_missing`) rather than blaming the solver for what it did not do. (The first version passed it for every stage and mislabelled a `final_only` transient
   as `solve_failed`; found by the CLI test that expected `balance_missing`.)
5. **It never meshes and never writes.** It reads the mesh the caller's `Mesher` holds, so a stale mesh is *reported* (`TV_MESH_CURRENT`), never repaired; every argument is a const reference.
6. **The CLI is an adapter over it** (`apps/bettercad_cli/ThermalReports.cpp`, `ThermalEdits.cpp`): reports call `runThermalAnalysis` and nothing else of the chain; edits build the P18-CMD command objects and hand
   them to the edit spine's atomic load-edit-save; the exit status is `accepted()`; and no flag, option or default exists that could bypass validation.
7. **Enforced, not described**: `tests/architecture/CheckThermalCliOwnership.cmake` (17 rules, 73 violating fixtures) fails a CLI file that includes a header of the derived chain, names a link of it, names a material
   property, holds a floating-point literal, decides on a residual or a status, corrects a value, subscripts with an id, accepts a mesh-local target, writes a file, converts a unit, writes to a process stream
   directly, does arithmetic on a quantity or lets a double bypass the finite-checking writer; and fails the entry if the chain is not called once each in order, if it does arithmetic, holds a policy, meshes or writes,
   or if `accepted()` is spelled any other way.

## Consequences

* **One new file in a qualified module.** `src/thermal` gains one source and the thermal library one object; no existing thermal source or header changes. It is additive, and it is reported as a change to the thermal
  module rather than hidden inside the CLI's qualification. The full three-preset regression covers the module either way.
* **The test support's `derive` is kept, and it is now an ORACLE.** `ThermalRun_SteadyIsTheQualifiedChainBitForBit` and `…TransientIsTheQualifiedChainBitForBit` compare every array, the recovered fields, the balance and
  the validation report of the new entry with that independently written chain; two chains written apart that agree to the bit are evidence that the order and the arguments are right, not an echo.
* **Stale geometry and a stale mesh are not presentable to a one-shot CLI**, because every process regenerates and remeshes before it asks. This is the same finding P17-CLI-001 recorded. The core's refusals are
  exercised where they can be reached (`ThermalRun_*`: an edited body on the old mesh, a remesh with the same counts and ids, an edit after a run, a failed re-run publishing nothing), and the CLI's analogue is
  `mesh_generation_failed` and `regeneration_failed`, which are tested.
* **Measured, not changed:** the core refuses a transient analysis whose output policy is `final_only` when it takes more than one step, because the heat balance of a backward-Euler step needs the states on both sides of
  it. The CLI reports that as `balance_missing` with the core's own sentence. Whether `final_only` should ever be allowed to skip the balance is an owner decision; it is not made here.
* A change to the order of the chain, to what `accepted()` means, to the stage names or to the refusal codes changes the fingerprints in `FREEZE.md` and is a new milestone.
