# P17-CLI-001 — CLI against core

```text
THE CLAIM: for one document, the CLI reports what the core reports, and fails
where the core fails. Nothing is compared against a literal that a change to
the physics could not move.
```

Every engineering assertion in `tests/cli/StructuralCliTests.cpp` is an
equivalence: the core is run in process through its own public entries, its
result is read through its own accessors, and the number the CLI printed is
compared against it. There is no expected-value table in the test file, which
is deliberate — a hard-coded `2912178.3` would pass against a CLI that had
grown its own von Mises formula and happened to agree on one mesh.

## Mutations: the canonical fingerprint

Two documents start identical. One is edited through the CLI, the other through
the P17-CMD command the CLI is supposed to be calling. **Both are reloaded from
disk** and their canonical fingerprints compared — so a CLI that built its own
record, allocated its own id or skipped the command differs here even when its
own output looks right.

| Case | Path A | Path B | Fingerprints equal? |
| --- | --- | --- | --- |
| add a load | `structural-load-add 4 --pressure 750000 --face …` | `AddStructuralLoadCommand` | **yes** |
| remove a load | `structural-load-remove 1` | `RemoveStructuralLoadCommand` | **yes** |
| add a restraint | `structural-restraint-add 3 … ux,uz` | `AddStructuralRestraintCommand` | **yes** |
| remove a restraint | `structural-restraint-remove 1` | `RemoveStructuralRestraintCommand` | **yes** |
| the same mask, written `uz,ux` | `structural-restraint-add 3 … uz,ux` | `… ux,uz` | **yes** — a bitmask has no order to lose |

The removal case additionally asserts the load is *gone* from the fingerprint,
so it cannot pass by both sides failing to change anything.

Probe **L11** replaces `AddStructuralLoadCommand` with a direct
`SetStructuralAnalysisDefinitionCommand` that appends the load itself — which
is what "the CLI bypasses the command" looks like — and is **killed**, because
the duplicate-id and finite-value checks belong to the command it skipped.

## Results, scalar by scalar

Fixture: the 40 × 30 × 20 mm steel block, 20 mm global mesh, 2.5 MPa on the end
cap, fixed start cap → **9 nodes, 12 tetrahedra, 15 free equations**.

| Metric | Core accessor | Tolerance | PASS |
| --- | --- | --- | --- |
| free equations | `outcome.freeEquations` | **exact** | ✓ |
| normalized residual | `outcome.residual.normalized` | **exact** | ✓ |
| pivot ratio | `outcome.pivotRatio` | **exact** | ✓ |
| strain energy | `outcome.strainEnergy` | 1e-12 rel | ✓ |
| max \|u\| | `outcome.largestDisplacement` | 1e-12 rel | ✓ |
| max von Mises | `outcome.largestVonMises` | 1e-12 rel | ✓ |

Three of those are **exact equalities**, not tolerances: the CLI reloaded the
same file, regenerated the same geometry and meshed it with the same
deterministic mesher, so any difference at all would mean it did something
else.

## Displacement summary

| Metric | Core | CLI | PASS |
| --- | --- | --- | --- |
| node count | `fields.displacements().size()` | `nodeCount` | ✓ |
| max \|u\| | the worst entry's `magnitude` | `magnitude.max` | ✓ |
| **the value in `largest`** | the same | `largest.magnitude.value` | ✓ |
| the vector there | the worst entry's `displacement` | `largest.displacement.{x,y,z}` | ✓ |
| node of the max | the worst entry's `node` | `largest.node` | ✓ |
| `ux`, `uy`, `uz` ranges | a span over `displacements()` | the three range objects | ✓ |
| unit | — | `"m"` | ✓ |

## Stress summary

| Metric | Core | CLI | PASS |
| --- | --- | --- | --- |
| tetrahedron count | `fields.elements().size()` | `tetrahedronCount` | ✓ |
| mesh element count | `Mesh::elementCount()` | `elementCount` (≥ the above) | ✓ |
| max von Mises | the worst entry's `vonMises` | `vonMises.max` | ✓ |
| **the value in `largest`** | the same | `largest.vonMises.value` | ✓ |
| σ₁ there | the worst entry's `principalStress.sigma1` | `largest.sigma1.value` | ✓ |
| element of the max | the worst entry's `element` | `largest.element` | ✓ |
| σxx σyy σzz τxy τyz τzx ranges | six spans over `elements()` | the six range objects | ✓ |
| domain | — | `"element"` | ✓ |
| unit | — | `"Pa"` | ✓ |

**The `largest` values and the six component ranges were added after probe L5
survived.** The first version compared the extremum's *id* and the unit but not
its *value*, so a number corrupted inside `largest` was invisible. See
[MUTATION.md](MUTATION.md).

## Reaction summary

| Metric | Core | CLI | PASS |
| --- | --- | --- | --- |
| constrained nodes | `result.reactions().size()` | `constrainedNodes` | ✓ |
| total force z | `reactions.totalForce().z` | `totalForce.z` | ✓ |
| per-restraint id | `reactions.restraints().front().restraint` | `restraints[0].restraintId` | ✓ |
| sign convention | — | **relation**, not a literal | ✓ |
| unit | — | `"N"`, `"N m"` | ✓ |

**The sign is checked as a relation and not a literal**: the reported total plus
the external resultant must sum to zero within 1e-6 of the magnitude, so a flip
anywhere in the chain fails. Probe **L8**, which negates all three components,
is **killed**.

An independent arithmetic check that the chain is physically right, from the
hand-written fixture: a 1234.5 Pa traction on a 0.1 × 0.02 m face is 2.469 N,
and the measured reaction x was **−2.469 N** — equal and opposite, to the digit.

## Validation

| Case | Core | CLI | exit | PASS |
| --- | --- | --- | --- | --- |
| acceptable | `status()`, `reachedStage`, `acceptable()` | the same three | 0 | ✓ |
| an unresolved restraint | `findings.front().code` | `findings[0].code`, same order | **1** | ✓ |

The finding **codes** are compared, in the core's own staged order, not a
count and not a boolean. Probes **L2** (validate always succeeding) and **L3**
(skipping the staged pre-check) are both killed.

## Failures, class by class

See [FAILURE_MATRIX.md](FAILURE_MATRIX.md) for the full matrix. The equivalence
claim there is the same one: the CLI's refusal set equals the core's on every
reachable fixture, and the diagnostic is the core's own code rather than a CLI
reinterpretation.

## And nothing in the CLI could have computed any of it

The brief's §66–71, answered by grep over both CLI source files:

```text
solvePreparedSystem          0        vonMises =        0
GeometryMeshMap              0        sqrt              0
prepareStructuralLoads       0        Eigen             0
prepareStructuralRestraints  0        Simplicial        0
MeshDofMap                   0        SparseLU          0
ConstraintSet                0        ConjugateGradient 0
FreeEquationMap              0
GlobalStructuralSystem       0
```

And the four names that do appear — `Tet4`, `eigen`, `DofIndex`, `NodeId`,
`ElementId` — appear **only in comments**, which was checked line by line. The
code reads `worst->node` and `worst->element`, member accesses on a core
struct; it never names a mesh-local id type.

There is likewise no threshold, no restraint count, no material check and no
residual formula: `ValidationStatus::` has **zero** occurrences, so the CLI
cannot have a status-based gate and branches only on the core's
`acceptable()`.
