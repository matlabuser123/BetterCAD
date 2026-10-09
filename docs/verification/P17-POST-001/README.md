# P17-POST-001 — Displacement / Strain / Stress Recovery

```text
TASK:            P17-POST-001
RESULT:          PASS
DATE:            2026-10-10
```

Takes the qualified solved displacement field and recovers deterministic nodal
displacement, constant Tet4 strain and stress, and the derived invariants. It
**re-derives nothing**: `B` and `D` are P17-ELEM-001's, reached through the two
functions that header made public for this milestone by name.

## Baseline

```text
git status --short      clean
git rev-parse HEAD      5e06595a2151869b855fe57e1ae3c54fad063a4d
git rev-parse origin/main
                        5e06595a2151869b855fe57e1ae3c54fad063a4d
git rev-parse HEAD^{tree}
                        4f9448a91e7d4f686782b8bb0c3013f30a2537f0
git log -1 --oneline    5e06595 BetterCAD: add linear static structural solver
```

## Prerequisites

Every one `[x]` with evidence, and no open checkbox anywhere above this
milestone's own section in `TODO.md`.

```text
P17-ARCH-001      [x]   536e14b5
P17-DATA-001      [x]   fcf2ea16
P17-MAT-001       [x]   4d4698b3
P17-DOF-001       [x]   9301b643
P17-ELEM-001      [x]   845b118c
P17-LOAD-001      [x]   27afe8d3
P17-BC-001        [x]   60e01a98
P17-ASSEMBLY-001  [x]   6d36b39e
P17-SOLVE-001     [x]   4e41fdd2
P16               QUALIFIED (P16-QUAL-001, 2026-10-06)
```

## What the audit found already decided

This is the seventh milestone running where the audit's main result is that the
work was already laid out. Recorded because it keeps being the cheapest hour of
the milestone.

```text
question                       answer, and where it already was
---------------------------------------------------------------------------
Voigt order                    FROZEN in P17-DATA's TensorComponent, whose
                               header says "FROZEN HERE, ONCE, FOR EVERY P17
                               MILESTONE" and names this one
engineering shear              FROZEN in Strain6's FIELD NAMES -- gammaXy, not
                               xy -- so a reader cannot mistake the convention
B                              Tet4Kinematics::strainFrom, already PUBLIC, and
                               P17-ELEM's header says why: "P17-POST-001
                               recovers strain as eps = B u_e [...] and if B
                               were buried in this translation unit that
                               milestone would reimplement it"
D                              ElasticityMatrix::stressFrom, likewise
local DOF order                localDofIndex, and elementDegreesOfFreedom
                               already maps a tetrahedron's four nodes onto its
                               twelve DofIndex
displacement field             SolvedSystem::values(), "in global DofIndex
                               order", constrained entries EXACTLY zero
symmetric eigensolver          Eigen::SelfAdjointEigenSolver, already used by
                               the Tet4 and assembly TESTS, and Eigen already
                               a PRIVATE link of this module (ADR-039)
the result type                StructuralResult already exists, validated, with
                               four channels -- which turned out to be the
                               reason NOT to use it here
```

So the milestone's own work was the tensor mapping, the invariants, the
traversal, the source gates, and one architectural decision.

## Architecture

[ADR-040](../../architecture/decisions/ADR-040-recovered-fields-are-a-stage-product-and-the-tensor-conventions-live-with-the-voigt-vectors.md),
five decisions with the candidates compared:

```text
1  recovery produces RecoveredFields, a STAGE PRODUCT; it does NOT construct
   a StructuralResult. Three candidates; the selected one continues the chain
   StructuralModel -> GlobalStructuralSystem -> SolvedSystem -> RecoveredFields,
   each carrying the SAME AssemblySource forward rather than deriving one.
   Filling StructuralResult here would have needed an empty reaction channel
   (a channel that looks computed and is not) and an analysis identity
   recovery cannot see (regenerating provenance independently)
2  the symmetric tensor types live in this module beside the Voigt vectors, as
   aggregates with the mapping in ONE accessor. Three candidates
3  principal values come from SelfAdjointEigenSolver's DEFAULT path, not
   computeDirect, and not a hand-written cubic
4  von Mises from the component formula, with the deviatoric form as the
   INDEPENDENT oracle
5  hydrostatic stress and principal strain are IMPLEMENTED, for verification
   power rather than completeness
```

Architecture rules preserved: the Document still owns canonical state; no OCCT
and no Qt anywhere near this module; `structural` at layer 50 includes only
lower layers; **no Eigen in any public header** (rule 7, which this milestone
is the first new public header to be checked against); stable typed IDs; units
SI throughout; errors structured; no GUI state.

## Implementation

```text
NEW   include/bettercad/structural/StructuralPost.hpp
      src/structural/StructuralPost.cpp
      tests/structural/StructuralPostTests.cpp          35 cases
      tests/reference/StructuralPostReferenceTests.cpp   6 cases
      tests/compile_fail/StructuralPostMisuse.cpp        6 cases
      docs/architecture/decisions/ADR-040-*.md

MOD   src/structural/CMakeLists.txt        the new source, nothing else
      tests/CMakeLists.txt                 two registrations
      tests/compile_fail/CMakeLists.txt    one group, six cases
      src/structural/StructuralSolve.cpp   A COMMENT ONLY -- it claimed Eigen
                                           was used "NOWHERE ELSE IN THIS
                                           MODULE", which this milestone made
                                           false
```

### The public surface

```text
displacementMagnitude(Translation3D)            -> Length        (std::hypot)
tensorOf(Stress6)  -> StressTensor3                              a relabelling
tensorOf(Strain6)  -> StrainTensor3                              gamma HALVED
hydrostaticStress(Stress6)                      -> Stress        tr(S)/3
vonMisesStress(Stress6)                         -> Stress        full 3D
principalStresses(Stress6)        -> Result<PrincipalStresses>   descending
principalStrains(Strain6)         -> Result<PrincipalStrains>    descending
recoverElementFields(id, corners, u_e, D)    -> Result<ElementFields>
elementRecoveryProblem(corners, u_e, D)      -> optional<RecoveryProblem>
elementDisplacements(numbering, solution, nodes)
                                  -> Result<array<Translation3D, 4>>
recoverFields(model, material, system, solution) -> Result<RecoveredFields>
fieldRecoveryProblem(...)                        -> optional<RecoveryProblem>
```

`recoverElementFields` is the **public numerical kernel**, exposed for the
reason `solveSymmetricSparse` is P17-SOLVE's and recorded under adversarial
finding P1: no validated mesh can carry a non-finite displacement and no
qualified solve can produce one, so six refusals were unreachable and
therefore unchecked. `recoverFields` calls it once per tetrahedron, so the
tested path is the production path.

## Conventions, units and ownership

[RESULT_CONVENTIONS.md](RESULT_CONVENTIONS.md). The short form:

```text
strain     [ exx eyy ezz gxy gyz gzx ]    dimensionless, ENGINEERING shear
stress     [ sxx syy szz txy tyz tzx ]    Pa
tensor     XY -> (0,1)   YZ -> (1,2)   ZX -> (2,0)
strain tensor off-diagonals               gamma / 2
principal order                           sigma1 >= sigma2 >= sigma3
displacement, magnitude                   m
principal strain                          dimensionless
display conversion in the core            NONE, and the compiler enforces it
```

Reuse counted rather than claimed: **one** `computeTet4Kinematics`, **one**
`strainFrom`, **one** `stressFrom`, **one** `isotropicElasticity`, **one**
`elementDegreesOfFreedom`; **zero** occurrences of a second shape gradient, a
`1/(6V)`, a cofactor determinant, `lambda`, `E * nu`, `poissonRatio`, a second
Voigt table, `3 * node` or `dof % 3`.

## Validation

[ANALYTICAL_VALIDATION.md](ANALYTICAL_VALIDATION.md),
[VON_MISES_VALIDATION.md](VON_MISES_VALIDATION.md),
[PRINCIPAL_STRESS_VALIDATION.md](PRINCIPAL_STRESS_VALIDATION.md),
[FINITENESS_AND_SOURCE.md](FINITENESS_AND_SOURCE.md).

The headline measurements:

```text
affine strain, all six components, vs the ANALYTIC derivative
                                            < 1e-10 relative
stress, vs an independent Dref              < 1e-10 relative
rigid translation strain                    < 1e-16
rigid rotation strain                       < 1e-16
nine pure-component fields                  < 1e-14 absolute

RM-MESH-01, 6 elements, strain vs an INDEPENDENTLY FITTED gradient
                                            worst 2.38396e-16
                            stress          worst 2.68641e-16
                            von Mises       worst 2.97172e-16

axial volume-weighted mean sigma_zz         5.95238e+06 Pa
F/A                                         5.95238e+06 Pa
ratio                                       1.0, bound 1e-9 BY EQUILIBRIUM

axial mean u_z                              9.76206e-07 m
F L / (A E)                                 9.92063e-07 m
ratio                                       0.984015, one-sided as a Tet4 must

full-3D von Mises, production                1.62410e+08 Pa
                   deviatoric oracle         1.62410e+08 Pa
                   PLANE-STRESS, for contrast 1.44212e+08 Pa
                   relative difference        0.112052  <-- ASSERTED > 5%

RM-MESH-02 large, level 0                   140 nodes, 508 tets
                  level 1                   850 nodes, 4210 tets
                  result counts             exactly equal to both, at both
                  von Mises vs deviatoric   < 1e-9 on every element

RM-MESH-06 base vs rotated, pressure load   peak vm, peak |u|, sigma1, sigma3
                                            all within 1e-15 (bound 25%)
determinism fingerprint, RM-MESH-01         0xc30f7c078f350b3e, 5 repeats
                                            bitwise identical
```

### The optional fields, decided

```text
Hydrostatic stress   IMPLEMENTED. tr(S)/3, Pa, NORMAL-STRESS sign convention
                     (compression negative). Named hydrostaticStress and not
                     pressure for that reason, and a meanPressure with the
                     opposite sign is deliberately NOT defined, so the two
                     cannot be conflated.
Principal strain     IMPLEMENTED. From StrainTensor3, so the gamma/2 is
                     already applied; a pure engineering shear gives
                     +|gamma|/2, 0, -|gamma|/2, which is the sharpest
                     available measurement of the halving.
```

## Adversarial review

[ADVERSARIAL_REVIEW.md](ADVERSARIAL_REVIEW.md).

```text
attacks              31  (26 from the brief, 5 added)
credible findings     9
production findings   2  -- both fixed
test defects          6  -- all fixed
infrastructure        1  -- fixed, and recorded as a memory rule
remaining blockers    0
```

```text
P1  SIX REFUSALS WERE UNREACHABLE, so nobody had checked them, and the
    header claimed otherwise. Fixed by exposing the per-element kernel; six
    probes now bite
P2  A LOOKUP COULD SUCCEED AGAINST STALE FIELDS. displacementOf(NodeId)
    answered from the recorded channel without the caller proving the fields
    describe the mesh in hand -- and P16 restarts its handles at 1 after every
    remesh. Fixed: both accessors now take the mesh and refuse, which is the
    contract StructuralResult::displacementOf already had
```

## Mutation protection

[MUTATION_PROTECTION.md](MUTATION_PROTECTION.md).

```text
30 probes, 27 KILLED, 3 SURVIVED
```

All three survivals are guards on branches no input can reach — a non-finite
eigenvalue of a finite symmetric 3x3, a failure of an eigensolver that cannot
fail, and a non-finite displacement a `SolvedSystem` cannot contain. They are
reported as survivals and kept, with the reason, rather than deleted to reach
30 of 30.

The plane-stress substitution is killed by **13 of 41** tests, the largest in
the set.

## Determinism

[DETERMINISM.md](DETERMINISM.md). Five repeats bitwise identical within a run;
a fingerprint over every ordered channel, and a guard proving the fingerprint
is not a constant. **Bitwise cross-preset equality is not claimed** — the
properties are asserted in each preset independently against the same
independent oracles.

## What is NOT claimed, and NOT built

```text
reactions                   P17-REACTION-001. SolvedSystem::fullResidual()
                            retains them; nothing here reads it
nodal / smoothed stress     not authorized. Recovery is element-constant
deformed geometry           P17-VIZ-001, display-only. Every input here is a
                            const reference
display units               P17-VIZ-001 / the CLI. The core stays SI
mesh-quality acceptance     P17-VALID-001
a safety or yield verdict   not computed. This milestone produces numbers and
                            decides nothing
persistence                 recovered fields are derived and are never
                            serialized
continuum convergence       not claimed beyond the equilibrium equality and
                            the one-sided compliance bound. P17-REFMOD-001
                            owns it
StructuralResult            still has no production caller, which was already
                            true after P17-SOLVE-001 and is the honest state
                            of a half-built pipeline
performance                 not measured as a gate. No baseline, no speedup
                            claimed
P17 as a whole              not qualified. P17-QUAL-001 is a separate
                            milestone
```

## Regression

See [FREEZE.md](FREEZE.md) for the frozen candidate, the stage table, the
warning audit and the qualified-equals-committed proof.

## Known limitations

```text
four RecoveryProblem values are unreachable by any input today
    MeshHasNoElements, ElementNodeMissing, DegreeOfFreedomMissing,
    InvalidMaterial. Kept because a later milestone that builds a model from a
    file or a script opens each path, and a silently skipped element would
    publish a softer body with nothing reporting it. Named as unreachable in
    the header rather than claimed as tested

two more are unreachable because the check before them is sufficient
    PrincipalValueFailure, NonFiniteDerivedResult. Two probes record this by
    SURVIVING

the per-element kernel takes an ElementId it only uses for diagnostics
    a kernel caller with no mesh passes an invalid handle and the message says
    "the element". Slightly awkward, and the alternative -- two message tables
    -- is worse

a prior-milestone evidence defect was found, reported, and FIXED on an
explicit instruction
    P17-ASSEMBLY-001's and P17-SOLVE-001's committed
    qualification/run-qualification.cmd carried one and two bare OLD_END
    lines -- heredoc terminators leaked by the sessions that wrote them -- and
    cmd.exe reported "'OLD_END' is not recognized as an internal or external
    command" into each run-qualification.err. See
    [PRIOR_EVIDENCE_CORRECTION.md](PRIOR_EVIDENCE_CORRECTION.md).
    Neither run was affected and no qualification result moves. The three
    lines are removed and each file now carries a rem note recording the
    correction; the .err files are NOT edited, because they are the record of
    what the runs actually produced. This milestone's own copy was written in
    one pass and grepped for any line not beginning with an expected token
```

## Acceptance gate

```text
TASK:            P17-POST-001
IMPLEMENTATION:  one new public header, one new source, three new test files,
                 ADR-040, and a comment correction in a qualified file
TESTS:           41 unit + reference, 6 compile-failure, inside the full
                 unfiltered suite in three presets
VALIDATION:      analytic derivatives, an independent Dref, an independently
                 fitted gradient, Cardano's closed form, the deviatoric
                 invariant, the principal-stress form, and the axial
                 equilibrium equality
RESULT:          PASS
EVIDENCE:        docs/verification/P17-POST-001/
TODO:            updated only on PASS
```
