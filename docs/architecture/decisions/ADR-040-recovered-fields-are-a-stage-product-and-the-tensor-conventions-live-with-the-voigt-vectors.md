# ADR-040 — Recovered fields are a stage product, and the tensor conventions live beside the Voigt vectors that need them

Status: Accepted
Date: 2026-10-09

## Context

`P17-POST-001` turns a solved displacement field into engineering answers:
nodal displacement, constant Tet4 strain and stress, principal values, von
Mises. Two questions have to be settled before any of that is written, and
both are about ownership rather than arithmetic.

**Where does the recovered result live?** `P17-DATA-001` already defined
`StructuralResult` — an immutable, factory-validated type with four channels
(displacements, reactions, strains, stresses) and a `StructuralResultSource` —
and wrote the rule this ADR has to respect:

```text
StructuralResult.hpp   "ONE PLACE DECIDES. `resultCurrency` and
                       `analysisState` are the only currentness answers in
                       P17. A GUI that compared node counts, a CLI that
                       compared an AnalysisId and a post-processor that
                       trusted its caller would be three answers that drift,
                       and the one that drifts towards 'current' is the one
                       that ships a wrong number."
```

So a second result-authority model is forbidden. The question is whether
filling `StructuralResult` is this milestone's job.

**Where do the 3x3 tensor conventions live?** `Strain6` and `Stress6` are
Voigt vectors in `TensorComponent` order. Principal values and the deviatoric
form need the symmetric 3x3 tensor, and the Voigt-to-tensor mapping is a
contract that three consumers will each want — a principal-value routine, a
von Mises routine, and eventually a GUI probe.

### What the audit found already decided

```text
Voigt order              FROZEN in P17-DATA's TensorComponent:
                         XX YY ZZ XY YZ ZX. "FROZEN HERE, ONCE, FOR EVERY P17
                         MILESTONE"
engineering shear        FROZEN in Strain6's FIELD NAMES -- gammaXy, not xy
B                        Tet4Kinematics::strainFrom, public, and the header
                         says why: "P17-POST-001 calls THIS rather than
                         rebuilding B, which is why it is public"
D                        ElasticityMatrix::stressFrom, public, likewise
local DOF order          localDofIndex(node, component), interleaved
global DOF order         ADR-037, and elementDegreesOfFreedom already maps a
                         tetrahedron's four nodes onto its twelve DofIndex
displacement field       SolvedSystem::values(), "in global DofIndex order",
                         with constrained entries EXACTLY zero
symmetric eigensolver    Eigen::SelfAdjointEigenSolver, already used by the
                         Tet4 and assembly TESTS, and Eigen is now a PRIVATE
                         link of this module (ADR-039)
```

Nothing in that list is this ADR's to revisit. Every decision below is taken
against it.

## Decision 1 — recovery produces `RecoveredFields`, a stage product; it does not construct a `StructuralResult`

`recoverFields` returns a new type carrying the `AssemblySource` of the system
it recovered from, the mesh stamp, the ordered nodal displacements and the
ordered element fields. `StructuralResult` is left for a later orchestration
milestone to assemble.

### Candidates

**A — fill `StructuralResult` here, passing an empty reactions vector.**
Rejected on two independent grounds.

The first is fabrication. `P17-REACTION-001` owns reactions and this milestone
is forbidden from computing them. An empty `reactions` channel is not "no
reactions yet"; read through the accessor it is indistinguishable from a model
with no restrained node, and `StructuralResult::create` accepts it because an
empty span is trivially ascending and unique. That is a channel that looks
computed and is not — the shape of defect the Definition of Done names.

The second is provenance. `StructuralResult` requires a
`StructuralResultSource` with eight fields; a `SolvedSystem` carries an
`AssemblySource` with six. The two missing fields are the analysis identity
and its revision, and `AssemblySource` omits them *deliberately*, as its own
header records:

```text
"analysisRevision moves on a RESTRAINT edit, which cannot change an
 unconstrained K or F at all. A system stamped with StructuralResultSource
 would be reported stale by an edit that did not touch it."
```

To fill a `StructuralResultSource` here, recovery would have to read the
analysis identity from somewhere other than what it recovered from. That is
regenerating provenance independently, and it is how a stale result acquires a
current-looking stamp.

**B — give recovery its own full result-authority model**, with its own
currentness enum and its own staleness comparison. Rejected: that is the
second answer `StructuralResult.hpp` forbids by name. `RecoveredFields`
answers no currentness question of its own — it carries the source it was
given and offers `describes(mesh)`, which is a mesh-identity check and the
same one `SolvedSystem`, `PreparedLoads` and `MeshDofMap` already offer.

**C — a stage product carrying the solved system's own source. SELECTED.**

It is the pattern the three preceding milestones established, and the chain
now reads:

```text
StructuralModel   possession proves the inputs resolved        ADR-036
  -> GlobalStructuralSystem    + AssemblySource                P17-ASSEMBLY
  -> SolvedSystem              + AssemblySource, the same one  P17-SOLVE
  -> RecoveredFields           + AssemblySource, the same one  P17-POST
  -> StructuralResult          + StructuralResultSource        later
```

Each stage takes the previous one by const reference, re-checks that it
describes the same mesh, and copies the source forward rather than deriving
one. The last arrow is the only place the analysis identity enters, and it
enters at the P17-DATA boundary built for it.

### Consequences

Recovery cannot be made to publish a current-looking result, because it cannot
name an analysis at all. The cost is that `StructuralResult` still has no
production caller after this milestone — which was already true after
`P17-SOLVE-001`, and is the honest state of a half-built pipeline rather than
a regression.

## Decision 2 — the symmetric tensor types live in the structural module beside the Voigt vectors, as aggregates with the mapping in one accessor

`StressTensor3` and `StrainTensor3` are plain structs holding the six
independent components, with a constexpr `at(row, column)` encoding the
mapping, and free `tensorOf` functions converting from `Stress6` and
`Strain6`.

### Candidates

**A — a general `SymmetricMatrix3` in `core/math`.** Rejected. `core/math` has
no matrix type today and adding one puts every module in this milestone's
blast radius for a name only `structural` uses — the reasoning `Tet4Element`
already applied to `Stiffness`:

```text
"N/m has no sibling and no second consumer in the tree, and adding an alias to
 core would put every module in this milestone's blast radius for a name only
 structural uses."
```

A dimensionless `SymmetricMatrix3` would also throw away the unit distinction
that is the point: a stress tensor's entries are `Pa` and a small-strain
tensor's are dimensionless, and one type for both needs a `double` interface
that strips `Stress` at the boundary.

**B — no tensor type; compute the invariants directly from the six Voigt
components.** Rejected, and this is the one worth stating plainly. It is
*possible* — von Mises has a closed form in the components and so does the
characteristic polynomial — but it duplicates the mapping at every site that
needs it. The brief's own list of what then goes wrong is the argument:

```text
"Do not duplicate mappings in: von Mises, principal stress, future GUI"
```

Two sites with the mapping written twice is how `ZX` becomes `XZ` in one of
them, and the two are numerically equal, so nothing catches it until a reader
indexes a tensor by hand.

**C — aggregates with the mapping in one accessor. SELECTED.**

```text
Voigt 3  XY  ->  at(0,1) and at(1,0)
Voigt 4  YZ  ->  at(1,2) and at(2,1)
Voigt 5  ZX  ->  at(2,0) and at(0,2)
```

Written once, in `at`, and every consumer goes through it. Symmetry is
structural rather than asserted: there is one `xy` field, so an asymmetric
stress tensor is unrepresentable and the symmetry test becomes a test of the
accessor rather than of the data.

`StrainTensor3` is where the engineering-shear convention is paid for. Its
`tensorOf` halves each `gamma`, so

```text
at(0,1) = gammaXy / 2
```

and the pure-shear principal-strain case — `+|gamma|/2, 0, -|gamma|/2` — is
the sharpest available test that it did.

### Why two types rather than one template

Because the second one is not a stress. A template over the component type
would compile and would still need two conversions, since the strain one
halves its off-diagonals and the stress one does not. Two aggregates of six
fields, with one honest difference between their conversions, is less code
than the abstraction that unifies them.

## Decision 3 — principal values come from Eigen's `SelfAdjointEigenSolver`, without `computeDirect`

Eigen is already a PRIVATE link of this module (ADR-039), so this adds no
dependency and no licence question.

The default tridiagonal-QL path is used rather than `computeDirect()`, which
Eigen's own documentation describes as faster and less accurate for 3x3.
Nothing here is on a hot path that would justify trading accuracy for speed,
and a hand-written closed-form cubic solver is exactly the "fragile cubic root
solver" a recorded reason is required before writing. There is no such reason.

Ordering is BetterCAD's, not the library's: the three values are explicitly
sorted descending after the solve, so no behaviour depends on the
eigensolver's output order. A repeated eigenvalue is an equality, not a
special case.

No eigenvectors are computed. They are not needed for principal magnitudes and
their sign is arbitrary, so computing them would add an output nothing can
verify.

## Decision 4 — von Mises is computed from the component formula, and the deviatoric form is the independent oracle

Production uses

```text
sigma_vm = sqrt( 0.5 ((sxx-syy)^2 + (syy-szz)^2 + (szz-sxx)^2)
                 + 3 (txy^2 + tyz^2 + tzx^2) )
```

which is a sum of squares and therefore non-negative for finite input by
construction, with no `abs` and no clamped radicand. The tests check it
through two different routes that do not share a line of code with it: the
deviatoric invariant `sqrt(3/2 s:s)` built from the tensor, and the
principal-stress form. Using the same helper for production and oracle would
make the agreement a tautology.

## Decision 5 — hydrostatic stress and principal strain are IMPLEMENTED

Both were optional. Both are implemented, and the reason is verification power
rather than completeness:

```text
hydrostaticStress   tr(S)/3, one line, and the trace identity
                    sigma_h == (sigma1+sigma2+sigma3)/3 is a genuine
                    cross-check between it and the eigensolver that neither
                    could provide alone
principalStrains    the same eigenvalue path applied to StrainTensor3, and
                    its pure-shear case directly measures the gamma/2
                    mapping -- the one convention error in this milestone
                    that produces plausible numbers
```

Hydrostatic stress keeps the **normal-stress sign convention**: compression is
negative, so a uniform `-100 MPa` state has `sigma_h = -100 MPa`. It is named
`hydrostaticStress` and not `pressure` for that reason; a `meanPressure` with
the opposite sign is a different quantity and is not defined here, so the two
cannot be conflated.
