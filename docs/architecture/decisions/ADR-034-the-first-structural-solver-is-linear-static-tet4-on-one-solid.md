# ADR-034 — The first structural solver is linear-static Tet4 on one solid

```text
STATUS:    Accepted
DATE:      2026-10-06
MILESTONE: P17-ARCH-001
TOUCHES:   ADR-031 (Tet4 only, and why the element type is a tag from day one),
           ADR-032 (a boundary names a CAD face), ADR-028 (a solver holds no
           material data)
```

## Context

P16 delivers a validated Tet4 volume mesh and P15 delivers canonical
engineering material data. P17 is the first phase that computes an engineering
*answer* from them, and the first whose output a user could act on by changing
a part.

That makes the scope statement load-bearing in a way the earlier phases' were
not. A mesh that is too coarse is visibly too coarse. A stress number that is
wrong because the formulation does not cover the case looks exactly like a
stress number that is right. The failure mode of an under-stated scope is a
plausible wrong answer, not a visible gap.

BetterCAD also has no licence yet and no users, so there is no installed base
asking for contact or plasticity. The pressure is entirely internal: to build
one correct thing before building a general one.

## Constraints

- `ADR-031` already fixed Tet4 as the only element family and recorded that it
  is **not enough for accurate bending stress** — the linear tetrahedron has
  constant strain and converges slowly on exactly the stress concentrations
  engineers care about. That is a known limitation of this foundation, recorded
  rather than discovered.
- P16 **refuses** a body with more than one solid, explicitly and tested. A
  multi-body analysis cannot be meshed today, so it cannot be solved today.
- P16's quality thresholds are report-only by design;
  `reportOnlyThresholds()` classifies nothing. No mesh in the repository has yet
  been graded against a structural standard.
- `ADR-028` forbids a downstream solver holding material data of its own, and
  names P17.
- This toolchain has no ASan/UBSan, so the numerics carry no sanitizer
  coverage.

## Options

### A — state the supported case narrowly and refuse everything else

One analysis type, one element, one material model, one solid. Anything else is
an explicit refusal with a diagnostic.

### B — build a general element and constitutive framework first

An element interface, a constitutive interface, a multi-region assembly, and
Tet4 linear elasticity as the first implementation of each.

### C — support the narrow case but treat unsupported input as best-effort

Solve what can be solved, warn about the rest.

## Decision

**Option A.** The first supported analysis is exactly:

```text
analysis      linear static
kinematics    small displacement, small strain
material      homogeneous isotropic linear elasticity
element       Tet4, the four-node linear tetrahedron
DOF           ux, uy, uz -- three translations per node
geometry      one current regenerated CAD solid
mesh          one current validated P16 Tet4 volume mesh
```

### The mathematics this commits to

```text
K u = F

K independent of u          the stiffness does not change with deformation
loads static                no inertia, no time
material response linear    sigma = D epsilon, D constant
geometry undeformed         the mesh is not updated by the solution
```

Strain is the infinitesimal-strain tensor:

```text
epsilon = 1/2 (grad u + grad u^T)
```

No Green–Lagrange strain, no deformation-gradient formulation, no updated
geometry.

Small displacement is a **model assumption, not a verified property of a
user's load**. P17 does not claim to prove that any particular load keeps
`||u||` small relative to the characteristic geometry. If a later milestone adds
that diagnostic it will say so; until then the assumption is documented and the
user owns it.

### Unsupported, and refused rather than approximated

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

An unsupported request is an explicit diagnostic. Never a best-effort answer,
never a warning beside a number.

## Rationale

Against Option B: a general framework built before one solver works is a design
validated by nothing. `ADR-031` already made the cheap part of generality —
the element carries its type, so a second family is a new validator rather than
a new data model — and that is the right amount to pay in advance. The expensive
part, a constitutive interface with one implementation, would be designed
entirely from imagination.

Against Option C, and this is the one worth being blunt about: a best-effort
structural result is worse than a refusal, because the user cannot see the
difference. BetterCAD's stated order of priorities is correctness,
diagnostics and reproducibility ahead of features, and "solve what we can" is
the choice that inverts it.

What would make Option B right: a second element family or a second
constitutive law actually being authorized. Then the interface is designed
against two known cases instead of one imagined one.

The single-solid limit is not a preference. It follows from P16 refusing a
multi-solid body, and lifting it means changing P16 first.

## Consequences

**Easy.** Validation against closed-form answers. Every reference case in
`P17-REFMOD-001` — the patch test, the axial bar, the pressure resultant, the
reaction equilibrium — has an exact analytical value, because the formulation is
the one the textbook derivations assume.

**Hard.** Bending. `ADR-031` says so already, and a cantilever reference case
must therefore be a convergence study rather than a single comparison. The
honest statement is that coarse-mesh Tet4 bending stress is wrong by a known
mechanism, not that it is approximately right.

**Committed.** To refusing, in code, cases a commercial package would attempt.
That will look like a missing feature, and it is: it is a missing feature rather
than a wrong number, and the distinction is the point.

The element type being a tag from day one is what keeps Tet10 a new struct and a
new validator rather than a new document model. `ADR-031`'s amendment records
that the fixed-arity connectivity makes it a new struct — a cost accepted there
and inherited here.

## Verification

Scope cannot be proved by a test; it is a statement about what is *not* built.
What is verified is that the statement and the code agree:

```text
no element, no DOF, no stiffness and no solve exists in src/structural/
  -- this milestone's module is the input boundary and nothing else
the single-solid limit is P16's, already tested there
the material model is P15's LinearElasticConstants, already validated there
```

The reference models and the acceptance policy that test the formulation belong
to `P17-ELEM-001`, `P17-VALID-001` and `P17-REFMOD-001`.

Evidence: [docs/verification/P17-ARCH-001/](../../verification/P17-ARCH-001/README.md).
