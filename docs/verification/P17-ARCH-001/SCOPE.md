# P17-ARCH-001 — the supported analysis, and the refusals

```text
SUBJECT:  exactly what the first structural solver computes, the assumptions
          that make it meaningful, and what it refuses instead of approximating
DECISION: ADR-034
```

## Supported

```text
analysis      linear static
kinematics    small displacement, small strain
material      homogeneous isotropic linear elasticity
element       Tet4, the four-node linear tetrahedron
DOF           ux, uy, uz -- three translations per node
geometry      one current regenerated CAD solid
mesh          one current validated P16 Tet4 volume mesh
loads         geometry-based intent; the types are P17-LOAD-001's
restraints    geometry-based intent; the types are P17-BC-001's
solve         CPU, double precision
output        displacement, strain, stress, von Mises, reactions -- all derived
```

This is **not** a general FEA solver, and the documentation does not call it
one. It is one analysis type on one element on one solid.

## The assumptions, stated as mathematics

### Linear static

```text
K u = F

K independent of u          the stiffness does not change with deformation
loads static                no inertia, no time dependence
material response linear    sigma = D epsilon with D constant
geometry undeformed         the mesh is not updated by the solution
```

No iteration on geometric nonlinearity.

### Small strain

Infinitesimal-strain theory:

```text
epsilon = 1/2 (grad u + grad u^T)
```

No Green–Lagrange strain, no deformation-gradient formulation, no updated
geometry.

### Small displacement

```text
||u|| sufficiently small relative to the characteristic geometry
```

**A model assumption, not a verified property of any particular load.** P17 does
not claim to prove that a user's load satisfies it. If a later milestone adds
that diagnostic it will say so; until then the assumption is documented and the
user owns it. Writing it down is the difference between a limitation and a
surprise.

### Isotropic linear elasticity

```text
sigma = D epsilon

lambda = E nu / ((1 + nu)(1 - 2 nu))
mu     = E / (2 (1 + nu))
```

`E` and `nu` come from P15 through `requireLinearElasticConstants`, which also
supplies `G` and `K` derived from them (ADR-027). Nothing in `src/structural/`
stores a modulus.

The Voigt ordering and the engineering-vs-tensor shear convention are
`P17-ELEM-001`'s to fix and write down **once**, shared by assembly and
post-processing. This milestone states the requirement and sets none of the
numbers.

## Refused, not approximated

```text
geometric nonlinearity   large strain            plasticity
hyperelasticity          viscoelasticity         creep
contact                  friction                buckling
modal analysis           harmonic analysis       transient dynamics
fatigue                  fracture                cohesive elements
shells                   beams                   trusses
Tet10                    Hex8                    mixed elements
composites               anisotropic elasticity  orthotropic elasticity
thermal stress           prestress               multiple-solid contact
adaptive FEA refinement
```

An unsupported request is an explicit diagnostic. Never a best-effort number,
never a warning printed beside one.

The reasoning is in ADR-034 and is worth repeating because it is the whole
argument: **a best-effort structural result is worse than a refusal, because the
user cannot see the difference.** A mesh that is too coarse looks too coarse. A
stress that is wrong because the formulation does not cover the case looks
exactly like a stress that is right.

## Limits inherited, not chosen

| Limit | Whose | Why P17 cannot lift it |
| --- | --- | --- |
| One solid | P16 | A multi-solid body is **refused at mesh time**, explicitly and tested. It cannot be meshed, so it cannot be solved. Lifting it means changing P16 first |
| Tet4 only | ADR-031 | And ADR-031 already records that Tet4 is *not enough for accurate bending stress* — constant strain, slow convergence on stress concentrations. A known limitation of the foundation, recorded rather than discovered |
| Configuration overrides refused | carried defect 2 | An active parameter override can change the effective parameter without rebuilding the geometry. P16 refuses to mesh in that state; P17 inherits the refusal by reusing the check |
| A drilled hole's wall has no `FaceName` | P16-MAP-001 | So it cannot be a load or restraint target. Not to be faked for unsupported topology |
| No ASan/UBSan | this toolchain | The numerics will carry no sanitizer coverage. Recorded; not pretended otherwise |

## What a later milestone must still decide

Named here so that none of it is quietly assumed to be settled:

```text
the Voigt ordering and shear convention        P17-ELEM-001
the DOF numbering order                        P17-DOF-001
whether gravity / self-weight is in scope      P17-LOAD-001 (P15 already has
                                               the density path and a separate
                                               FeaLinearStaticWithGravity
                                               consumer, so the data is ready
                                               and only the decision is not)
the structural mesh-acceptance thresholds      P17-VALID-001
the sparse representation                      P17-ASSEMBLY-001
the linear solver and its licence              P17-SOLVE-001
the per-body material story                    P17-MAT-001 (P15 assigns a
                                               material per DOCUMENT today,
                                               with no inheritance)
```

## How scope is verified

Scope is a statement about what is *not* built, so it cannot be proved by a
test. What is verified is that the statement and the code agree:

```text
src/structural/ holds one header and one source, 2 files
no element, no DOF, no stiffness, no vector, no solve exists in it
no linear algebra is linked: BetterCAD::eigen is not in its PUBLIC_LINK
the single-solid limit is P16's and is tested there
the material model is P15's and is validated there
```

Measured after the milestone's own build: the structural library is
`StructuralAnalysis.cpp` alone.
