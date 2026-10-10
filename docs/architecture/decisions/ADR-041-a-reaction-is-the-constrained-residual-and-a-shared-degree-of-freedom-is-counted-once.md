# ADR-041 — A reaction is the constrained residual, and a degree of freedom shared by two restraints is counted once

Status: Accepted
Date: 2026-10-10

## Context

`P17-REACTION-001` recovers support reactions and proves static equilibrium.
Four questions have to be settled before any of it is written, and three of
them are about honesty rather than arithmetic.

### What the audit found already decided

```text
the reaction quantity    SolvedSystem::fullResidual() is ALREADY `K u - F`
                         over the full system, computed in one pass with
                         per-entry finiteness, and its header says why it
                         exists: "CONSTRAINED ENTRIES ARE THE SUPPORT
                         REACTIONS [...] Retained, not aggregated:
                         P17-REACTION-001 owns reaction semantics and
                         equilibrium, and discarding this now would make it
                         recompute what is already known"
u_c is EXACTLY zero      the solver assigns the full vector 0.0 and writes
                         only free rows, so the partition R_c = K_cf u_f - F_c
                         holds with no u_c term
the constrained set      ConstraintSet::constrained(), ascending and unique
                         "by construction"
the applied force        ForceVector::resultantForce() for the assembled F,
                         and PreparedLoads::resultantForce() plus
                         resultantMomentAbout(mesh, origin) as an independent
                         oracle -- WITH the origin already explicit and a
                         mesh refusal already in place
the moment of a force    core's momentOf(lever, force) = lever x force, whose
                         header already insists the caller forms the
                         difference "because a moment about an unstated origin
                         is not a quantity"
an empty free system     SOLVES rather than factorising, with pivotRatio 1.0 --
                         so a fully constrained model is a usable fixture
```

So the numerical source exists and the load oracles exist. What is missing is
identity: **which restraint produced which constrained degree of freedom.**

## Decision 1 — the reaction is the constrained component of `K u - F`, and nothing else

```text
R[dof] = fullResidual()[dof]     for dof in constraints().constrained()
```

No second residual definition, no re-solve, no penalty term, no stress
integration, and explicitly **not** the free-system residual.

### Candidates

**A — integrate element stress over the support faces.** Rejected. It
introduces stress extrapolation to a surface, facet normal selection and
surface quadrature — three error sources the algebraic residual does not have
— to compute a quantity the algebra already gives exactly. For this
displacement-based FE system the constrained residual *is* the reaction; a
stress integral is an approximation of it. It remains available as a
supplemental check for a later milestone.

**B — recompute `K u - F` here.** Rejected as duplication. The solver already
computes it, checks every entry finite, and retains it for this milestone by
name. A second implementation could drift from the first, and the drift would
be invisible because both would look plausible.

**C — consume `fullResidual()`. SELECTED.** The partition identity
`R_c = K_cf u_f - F_c` is checked independently, on a small hand-built system,
against a test-side dense computation — which is where the algebra is verified
rather than assumed.

### The free residual is not a reaction

`fullResidual()` is defined over **all** degrees of freedom, and its free
entries are small but not zero. They are solver-quality diagnostics and they
are not support reactions:

```text
ReactionSet = the CONSTRAINED entries of r_full
```

This matters most at a partially restrained node. If a node is constrained in
`Ux` only, then `Rx` is a reaction and the tiny `r_full` values at its `Uy` and
`Uz` rows are **not** — they are the free residual, and reporting them would
invent two support components that no restraint asked for.

## Decision 2 — the sign convention, verified before it was documented

```text
R = (K u - F) on constrained DOFs
  = the force applied BY the support ON the structure

equilibrium:  sum(F_external) + sum(R) = 0
```

Checked against the one-degree-of-freedom case before being written down,
rather than chosen to make a sum come out zero:

```text
stiffness k, the DOF constrained so u = 0, external load +F
    K u - F = 0 - F = -F
so R = -F, and F + (-F) = 0
```

and physically: push `+F` on a structure held by a support, and the support
pushes back `-F`. The two agree, so the convention is the standard
finite-element one and no sign is flipped anywhere.

The fully constrained case is the same statement with no factorisation in it
at all: every DOF constrained means `u = 0`, so `r_full = -F` everywhere and
`R_total = -F_external` exactly. That is the primary sign gate, and a
sign-flip mutation must fail it.

## Decision 3 — `RestraintResolution` gains the degrees of freedom it resolved to, which changes qualified P17-BC code

Per-restraint inspectability is a **required** checklist item. P17-BC retained
only counts:

```text
struct RestraintResolution {
    RestraintId restraint;
    std::size_t facets;
    std::size_t nodes;
    RestraintComponents components;
    std::size_t degreesOfFreedom;   // nodes * components.count()
};
```

Counts cannot attribute a reaction. The two ways to get identity:

**A — re-resolve each restraint's target after the solve.** Rejected, and the
brief is right to forbid it: re-running the geometry mapping and *assuming* it
reproduces the set the solve used is precisely how a source mismatch hides. If
the mesh or the mapping moved, the re-resolution would quietly attribute
reactions to the wrong degrees of freedom.

**B — retain the mapping the solve actually used. SELECTED.** The per-restraint
loop in `prepareStructuralRestraints` already calls
`numbering.indexOf(NodalDof{node, component})` for every degree of freedom it
resolves, verifies it, and then **discards the index**, keeping only the
`NodalDof` in a global union. The change is to keep what is already computed:

```text
RestraintResolution gains
    std::vector<DofIndex> constrained;   ascending, unique within the restraint
invariant
    constrained.size() == degreesOfFreedom
```

This is additive, costs one `push_back` of a value already in hand, and
introduces no new resolution path. It is **derived state and is never
persisted** — the same status the rest of `PreparedRestraints` has.

It does change already-qualified shared code, so P17-BC-001's suite is rerun
and the requalification is recorded. That is the honest price of the
alternative being unsound.

## Decision 4 — a shared degree of freedom is counted once globally, and per-restraint summaries are additive by construction

Two restraints can lawfully reach the same degree of freedom: overlapping
faces share edge nodes, and `PreparedRestraints` already documents the
constrained set as "a UNION, NOT A LIST" for that reason. The physical
reaction at a shared DOF exists **once**, and it cannot be partitioned between
the restraints without inventing a convention.

### Candidates

**A — split a shared reaction between its restraints.** Rejected outright. Any
split is arbitrary; a 50/50 split is arbitrary *and* looks principled, which is
worse.

**B — report the shared DOF under every contributing restraint, and warn that
the summaries are not additive.** This is the brief's Policy B. Rejected as the
primary model: a structure whose correct use depends on a reader noticing a
warning will eventually be summed by a CLI or a GUI, and the result will be
wrong by exactly the overlap.

**C — unique ownership plus one shared aggregate. SELECTED.** Each summary
reports the reaction on the DOFs **it alone owns**. The DOFs owned by more than
one restraint are aggregated once, separately. Then

```text
sum over restraints of ownedForce  +  sharedForce  ==  globalReactionForce
```

**exactly**, and the same for moments about one origin. Additivity is a
property of the decomposition rather than a rule a caller must remember, and
it is asserted as an identity in the tests.

For traceability each summary also carries the reaction on the DOFs it
*shares* — which is what a reader inspecting one restraint wants — but that
field is named `sharedForce`, is documented as not additive across restraints,
and is excluded from the identity above. `sharedDegreesOfFreedom` being
non-zero is the explicit signal that a summary has a non-additive part.

## Decision 5 — equilibrium is normalised by the magnitudes that participate, not by the net resultant

```text
e_F = F_external + R_total
e_M = M_external(O) + M_R(O)
```

Reported as `||.||_2`, `||.||_inf`, **each component**, and a normalized
metric. The denominators are sums of individual magnitudes:

```text
scale_F = sum |F_i|                 over loaded nodes
        + sum |R_j|                 over constrained DOFs, grouped per node
scale_M = sum |(x_i - O) x F_i|
        + sum |(x_j - O) x R_j|
```

Normalising by `||F_external||` would be wrong whenever large loads cancel: a
couple has zero net force and a zero net external moment is reachable by
cancellation too, so a ratio against the net would divide a real imbalance by
almost nothing and report a spurious failure — or, with the sign the other way,
hide one. Both scales are reported in the evidence so the tolerance is
auditable.

Each has an absolute floor carrying its own dimension — a force floor in `N`
and a moment floor in `N·m`, never one dimensionless number for both — used
only to keep the zero-load case from dividing by zero.

**The thresholds are measured, not chosen here.** A solver residual tolerance
is not an equilibrium tolerance: the solver's gate is on the free equations and
this one is on the whole body's applied and constrained forces. The measurement
and the resulting thresholds are recorded in
`docs/verification/P17-REACTION-001/EQUILIBRIUM_TOLERANCE.md`, separately for
the algebraic path (assembled `F` against reactions) and the geometric path
(an analytical continuum resultant against reactions).

## Decision 6 — a support moment is derived, and there are no rotational reaction degrees of freedom

A Tet4 node has `Ux, Uy, Uz` and nothing else, so there is no nodal couple to
report. A support region's resultant moment is the moment of its translational
reaction distribution:

```text
M_support(O) = sum (x_j - O) x R_j
```

through core's `momentOf`, which is the one cross product in the path. The
result schema therefore has **no** nodal `Mx, My, Mz`: a bending moment at a
fixed face is represented by the distribution of translational nodal
reactions, and claiming otherwise would invent a degree of freedom the element
does not have.

Because a moment depends on its origin, it is computed on request with the
origin as a parameter and never stored as an independent solved quantity. The
transfer relation

```text
M(O2) = M(O1) - (O2 - O1) x F_resultant
```

is tested, which is what freezes the convention and catches a reversed cross
product.

## Status of what this does NOT decide

```text
nonzero prescribed displacement   deferred. The algebra is written so that a
                                  u_c term would extend it, and no partial
                                  support is implemented
a reaction application point      not computed. Force and moment resultants
                                  per support region are what this milestone
                                  owns; a centre of pressure is not
mesh-quality acceptance           P17-VALID-001
display, arrows, scaling          P17-VIZ-001. Presentation state must never
                                  enter the result's source fingerprint
persistence                       never: reactions are derived (ADR-030's rule
                                  for a mesh, applied to a result by
                                  P17-DATA-001)
```
