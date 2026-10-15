# P17-REFMOD-001 — the audit, written before any production code

```text
BASELINE   460321a  (P17-CLI-001), working tree clean but for the untracked
           .claude/ settings directory, HEAD == origin/main
PREDECESSORS   P17-ARCH, DATA, DOF, ELEM, LOAD, BC, ASSEMBLY, SOLVE, POST,
           REACTION, VALID, CMD, PERSIST, CLI -- each PASS in TODO.md's
           authorization table, each with a FREEZE.md in its evidence
           directory. P16-REFMOD-001 and P16-QUAL-001 PASS.
```

## 1. BetterCAD already has a reference-model system, on its sixth suite

The parts (P11/P12), the assemblies (P13), the drawings (P14), the
engineering data (P15) and the meshes (P16) each added the same five things,
and this milestone adds exactly those five and nothing else:

```text
examples/reference_models/<Suite>ReferenceModels.hpp   the declaration: a struct
                                                        of IDs per model, a
                                                        builder per model, a
                                                        catalog array
examples/reference_models/<Suite>Models.cpp            the builders, through
                                                        detail::ModelBuilder and
                                                        detail::SketchBuilder
examples/reference_models/<Suite>Catalog.cpp           kind -> document
examples/reference_models/main.cpp                     one loop per suite, which
                                                        is the CLI fixture AND the
                                                        evidence generator
tests/reference/<Suite>TestSupport.hpp                 the shared checks
tests/reference/<Suite>ModelsTests.cpp                 the cases
tests/reference/Analytic.hpp                           a section of closed forms
                                                        that includes NO
                                                        BetterCAD header
tests/CMakeLists.txt                                   the sources, and the
                                                        refmod.<suite>.* process
                                                        tests on a FIXTURES_SETUP
                                                        runner
tests/cli/ZeroMatchGuard.cmake                         two filters with asserted
                                                        minimums
```

The structural suite follows that shape exactly. `MeshTestSupport.hpp`'s
`MeshedReference` (a document with its own regenerator and mesher, keyed on
the control named "Mesh") is reused as the base of `StructuralReference`, and
`MeshReferenceModels.hpp`'s `sweptFace`, `startCapFace`, `endCapFace` and
`RigidPlacement` are reused as the one spelling of a face reference and of a
rigid transform. Nothing in the P16 files changed.

## 2. What the structural stack offers a reference suite, and at which layer

Every stage is public and every stage takes a validated type whose
possession is the evidence (ADR-036), so a reference case can stop at any
stage and compare:

```text
requireStructuralModel          StructuralModel: mesh current, map from the
                                same lookup, material linear-elastic
resolveStructuralMaterial       StructuralMaterial: E, nu, G, K; rho iff the
                                MODE asks for it
prepareStructuralLoads          PreparedLoads: nodal forces ascending by handle,
                                resultantForce(), resultantMomentAbout()
buildMeshDofMap                 MeshDofMap: the frozen 3k+c+1 numbering
prepareStructuralRestraints     PreparedRestraints: the ConstraintSet union
assembleStructuralSystem        GlobalStructuralSystem: K as CSR, F dense, both
                                SI, in the free numbering of the empty set
solveStructuralSystem           SolvedSystem: u in DofIndex order, the full
                                residual, pivot ratio, strain energy
recoverFields                   RecoveredFields: per node u and |u|, per element
                                eps, sigma, principals, von Mises, hydrostatic
recoverSupportReactions         SupportReactions: per node, per restraint,
                                force AND moment equilibrium at a measured
                                tolerance
validateStructuralAnalysisForSolve   the staged report, never an error
solveStructuralAnalysis         THE production path: every gate in order, no
                                partial outcome
```

Two public numerical kernels exist for exactly the cases a solved fixture
cannot pose: `recoverElementFields(element, corners, displacements, D)` and
`computeTet4Stiffness(nodes, constants)`. RM-FEA-01's single-tetrahedron
case goes through them, which is the path production runs per element.

## 3. What the audit found that changed the models

**A planar body cannot be refined by its global target.** P16-REFMOD-001 and
P17-ASSEMBLY-001 both recorded it: OCCT gives a planar face two triangles
whatever the deflection, so a block meshes to its corners however small the
target. Consequences taken here:

- RM-FEA-03's convergence study is a **cylinder**, whose curved wall responds
  to the deflection and the target, and the levels are asserted to differ in
  node and element count before any error is compared.
- RM-FEA-07's refinement is measured in a **volume band** with a **mirror
  document**, as P16 did, and not at the refined face's own facets.

**A slab control on a cap only takes effect when the section is wide
enough.** Measured on saved documents through the CLI and the runner rather
than reasoned: a 20 x 30 mm beam with a 5 mm control on its root cap meshed
to 8 nodes and 6 tetrahedra -- the unrefined mesh -- at 4 mm and at 10 mm
too, while 30 x 40 gave 14 nodes, 40 x 40 gave 43 and 40 x 60 gave 78. Netgen
inserts interior points at the centres of local-size tree boxes that lie
wholly inside the body, so boxes of the requested size must fit inside the
section. RM-FEA-07 is 40 x 60 x 100 for that reason, and the builder says so.
(A first reading of the CLI experiment mis-attributed 43 nodes to 30 x 40: the
variant script substituted `0.02 -> 0.03` and then `0.03 -> 0.04`, so that
variant was 40 x 40. The runner's own figure for 30 x 40 is 14.)

**Exact rigid-transform invariance needs the same connectivity.** An
80 x 50 x 30 block on a frame of this suite's own gave the SAME eight nodes
two DIFFERENT tetrahedralisations in its two placements, and a 0.4% energy
difference that no honest tolerance could absorb. RM-FEA-06 therefore uses
RM-MESH-06's block, sizing and placement -- the configuration P17-POST-001
measured to give bit-identical invariants -- through this suite's own
builder, and the test asserts element-by-element correspondence before it
claims anything.

**The CLI cannot present a stale mesh**, by architecture: every process
regenerates and meshes before it solves (P17-CLI-001's FAILURE_MATRIX.md).
RM-FEA-09's refusal is therefore an in-process case, as P17-VALID-001's is,
and the runner reproduces it for EVERY model: after each model's main
dimension is edited the runner requires `mesh_stale` from validation and a
refused solve, then remeshes and re-solves. The CLI tests for RM-FEA-09 are
of its committed, valid state.

**The equilibrium tolerance is P17-REACTION-001's and is not touched.**
`solveStructuralAnalysis` takes it as a parameter with the production default
1e-12. What the audit could not know and the suite measured is in
[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md), finding F1.

## 4. Six questions the brief asked, answered by the tree

| Question | Answer in the tree |
| --- | --- |
| How does a model carry its intent? | `MeshControl` for meshing, one `StructuralAnalysis` named "Study" for loads, restraints, mode and solver settings; the material is assigned at document level as P15 assigns one |
| Is a `NodalForceLoad` persistable? | No, by ADR-048; every load here names a `FaceName` or is gravity |
| Can a load name a face that does not exist and still save? | Yes: `validate(StructuralLoad)` checks the record, not the mesh, so RM-FEA-10 is a committed document |
| Where does "fewer than six" stop? | At validation (`InsufficientRestraint`); "six or more all in z" reaches the factorisation's pivot gate, which is RM-FEA-08's point |
| Does gravity need anything the models lack? | A density on the material and `LinearStaticWithGravity` on the analysis; RM-FEA-05 carries both |
| What does the runner print? | What P16's loop prints for a mesh, plus the solve's residual, pivot ratio, energy, extremes, reactions and both balances, in full double precision |

## 5. What this milestone does not add

No structural capability, no CLI command, no document format change, no core
change of any kind. The blast radius is `examples/`, `tests/` and this
directory.
