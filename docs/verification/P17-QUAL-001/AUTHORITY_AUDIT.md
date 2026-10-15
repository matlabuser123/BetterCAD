# P17-QUAL-001 — architecture, authority and identity audit

```text
SUBJECT:  whether the final P17 architecture is the one the ADRs decided, and
          whether the CAD / material / mesh / solver authority boundary can be
          reversed anywhere
METHOD:   searched for the FAILURE MODE rather than the promise. A grep that
          finds the reassuring comment proves nothing; a grep that finds no
          occurrence of the forbidden construct does. Then one executable
          statement per boundary, which a table alone cannot be.
```

## The pipeline, as the committed tree implements it

```text
authoritative CAD model          Document: features, parameters, sketches
        |
        v  Regenerator
regenerated geometry             requireMeshableGeometry -- P16's ONE geometry
        |                        boundary: stale, failed, blocked, non-solid,
        |                        zero-volume, configuration override, refused
        v  Mesher::generate
derived P16 Tet4 mesh            held by the Mesher service, stamped, never a
        |                        document object
        v  requireStructuralModel          (ADR-036)
validated structural input       control -> body eligible -> mesh HELD and
        |                        CURRENT -> map + quality -> material resolves
        |                        with every constant the MODE needs (P15)
        v  validateStructuralAnalysisForSolve   (ADR-042)
staged acceptance                analysis -> inputs -> quality -> material ->
        |                        restraints -> loads, stopping at the first
        |                        refusing stage
        v  prepare / assemble / solve / recover / reactions
derived result                   StructuralResult, keyed to the MeshStamp it
        |                        was computed on, built only by
        |                        solveStructuralAnalysis on success
        v
renderer (read-only adapter) / CLI (reports) / persistence (intent only)
```

**The single approved path is measured, not asserted.**

```text
callers of solveStructuralAnalysis                   2: the CLI's one solve()
                                                      helper, and the test tree
callers of validateStructuralAnalysisForSolve        3: the entry itself, the
                                                      CLI's prepare and
                                                      structural-validate
constructors of StructuralResult                     1: the private one, reached
                                                      only by create(), called
                                                      only in solveStructuralAnalysis
Eigen headers included outside src/structural        sketch/solver, assembly/solver
                                                      -- the two admissions that
                                                      predate P17; none in apps/,
                                                      include/, examples/, renderer
```

## The authority matrix

```text
| DATA                                  | CANONICAL? | WHERE IT LIVES / WHY NOT              |
| CAD geometry                          | YES        | Document: features, sketches, params  |
| P15 material and its assignment       | YES        | features::Material; document-level     |
|                                       |            | assignment; revision moves on edit    |
| P16 meshing intent                    | YES        | MeshControl::definition()             |
| structural analysis definition        | YES        | StructuralAnalysis: mesh control id,  |
|   mode, loads, restraints, solver     |            | mode, loads, restraints, SolverSettings|
|   settings (ADR-047)                  |            | -- five members, none derived          |
|                                       |            |                                        |
| volume nodes, Tet4 connectivity       | NO         | Mesher::Held, keyed by control         |
| DOF numbering (MeshDofMap)            | NO         | built per solve from the mesh          |
|                                       |            | enumeration; stamp + count bound       |
| constraint set, free-equation map     | NO         | likewise; one friend each              |
| prepared nodal forces                 | NO         | PreparedLoads, mesh-stamped; NodalForce|
|                                       |            | load is mesh-local and REFUSES the save|
| stiffness matrix, force vector        | NO         | GlobalStructuralSystem, AssemblySource |
| solution, residual, pivot ratio       | NO         | SolvedSystem                           |
| displacements, strains, stresses,     | NO         | StructuralResult -- no document object,|
|   principal values, von Mises         |            | no setter, no provenance rewrite       |
| reactions, equilibrium verdict        | NO         | SupportReactions, derived from the     |
|                                       |            | constrained residual                   |
| result currentness / analysis state   | NO         | DERIVED by comparison, never stored    |
| contour bands, deformed positions,    | NO         | renderer::StructuralResultView, stamp- |
|   overlay segments                    |            | keyed; refuses another mesh's result   |
| CLI JSON payloads                     | NO         | computed per invocation; the file is   |
|                                       |            | never written by a report command      |
```

### The matrix as one executable statement

A table is a claim. `P17Qualification_SolvingLeavesEveryReferenceDocumentByteIdentical`
is the check: save each of the twelve committed reference documents, mesh and
solve it through the production entry, save again, and require the bytes to be
**identical** — with the object count and the document revision unchanged,
so neither meshing nor solving nor a REFUSAL is an edit.

```text
12 reference documents     10 solved, 2 refused (RM-FEA-08 by the pivot gate,
                           RM-FEA-10 at the load stage)
document bytes             before == after, all twelve
object count, revision     unchanged, all twelve
derived arrays in the file "displacements" "stresses" "strains" "reactions"
                           "stiffness" "nodes" "tetrahedra": 0 occurrences
```

P17-PERSIST-001 proved the same on one hand-built part in four states
(`StructuralPersistence_TheFileIsIdenticalWhateverTheSolveHasDone`) and the
density form (`..._TheFileDoesNotFollowTheMeshOrTheResultSize`); the final
gate adds the breadth.

## Identity: no solver handle is a CAD identity

```text
NodeId / ElementId in src/io or include/bettercad/io   2 lines, both comments
                                                        saying they are NOT there
DofIndex / FreeEquationIndex in core/Id.hpp            0
DofIndex in any document object                        0 (RestraintResolution's
                                                        list is a derived object)
nearest / closest / rebind in src/structural           2 lines, both comments
                                                        saying there is none
unordered_map / unordered_set in structural             0 (2 comments explaining
                                                        why not)
std::chrono, time(nullptr), rand in structural          0
std::abs / fabs on a Jacobian determinant or volume    0
structural compile-fail files                           9, every one a case that
                                                        must NOT compile
```

The compile-fail cases are the sharpest form: `StructuralIdMisuse`,
`StructuralDofMisuse`, `StructuralRestraintMisuse`, `StructuralSystemMisuse`,
`StructuralSolveMisuse`, `StructuralPostMisuse`, `StructuralReactionMisuse`,
`StructuralValidationMisuse`, `StructuralCommandMisuse` — a `DofIndex` used
as a `FreeEquationIndex`, a numbering constructed directly, a restraint that
targets a node, a command keyed on a DOF index, a definition that carries a
stiffness matrix, a command constructible from a result. Each is a real
compilation that must fail with the stated diagnostic.

## Stale-input protection, in one sequence

`P17Qualification_AFailedRegenerationNeverYieldsACurrentResult` runs the whole
contract on RM-FEA-09: current result, an edit that makes the body impossible
(depth 0), a regeneration that FAILS, and then —

```text
mesh currency              does not describe the model
validation                 Rejected at the Inputs stage, geometry_ineligible,
                           0 nodes / 0 DOFs / 0 loaded nodes measured past it
solve                      refused; the old mesh still HELD (stale is old,
                           not deleted); generate() refused too
analysis state             inputs_unavailable
recovery at the ORIGINAL   same fingerprint to the last bit, the first result
  depth                    still stale with reasons {geometry, mesh}
```

And `P17Qualification_EveryDependencyEditStalesTheResultInOneSequence` runs
every dependency edit on one document with a fresh solve after each:
geometry (inputs unavailable; after a remesh stale for {geometry, mesh}),
meshing intent ({mesh}), material ({material}, mesh current), load
({analysis}), restraint ({analysis}), undo (still stale; the fresh solve
agrees with the pre-restraint solve bit for bit), solver settings
({analysis}, identical numbers). Five solves on one mesh stamp, so every
verdict was the edit's and not a remesh's. Detail:
[CURRENTNESS_AUDIT.md](CURRENTNESS_AUDIT.md).

## What the GUI owns

`apps/bettercad/` contains **zero** references to the structural module. The
display surface P17-VIZ-001 built lives in `src/renderer/` as a headless,
read-only adapter plus the OCCT viewer's presentation classes:
`StructuralResultView` reads stored members and computes nothing a solver
computes (`StructuralViz_EveryChannelsValuesAreItsSourcesValues`,
`..._RenderingChangesNothingItReads`), refuses a result that is not this
mesh's (`..._RefusesAResultFromADifferentMesh`, `..._AStaleDofNumberingIsRefusedNotTrusted`),
and never draws a stale result as current (`..._StaleResultIsNeverDrawableAsCurrent`,
`..._AStaleContourIsNotDrawnAtAll`). No stress formula, eigensolver, nodal
average, node move, `abs()` on a signed channel, remesh or tolerance exists
in `src/renderer/` (0 matches for `computeTet4Stiffness`, `isotropicElasticity`,
`recoverFields(`, `vonMises(`, `StiffnessMatrix`, `Eigen`). The Qt inspector
surface is not wired, which P17-VIZ-001 records as out of scope; a GUI that
displays nothing cannot own a second authority.

## Verdict

```text
CAD geometry remains authority           PASS  -- no mesh, numbering, result or
                                                  reaction is a document object
P15 material remains canonical           PASS  -- resolved per solve; an edit
                                                  stales the result, not the mesh
P16 mesh remains derived                 PASS  -- re-checked at the solver entry
                                                  through requireStructuralModel
P17 results remain derived               PASS  -- byte-identical documents, all 12
stale geometry rejection                 PASS  -- including a FAILED regeneration
stale mesh rejection                     PASS  -- RM-FEA-09, and the intent row
```
