# ADR-042 — Mesh quality rejects on a measured accuracy law, never on a shape score, and one unbypassable entry owns every gate

Status: accepted. Milestone: `P17-VALID-001`.

## Context

P16-QUALITY-001 ships metrics and no accept/reject line, deliberately: its own
header says "`P17` owns what a structural analysis needs from a mesh", and
`reportOnlyThresholds()` classifies nothing. `StructuralModel::quality()`'s
documentation names `P17-VALID-001` as the owner of the thresholds. This
milestone is where that debt is paid.

### What the audit found already decided

The pattern of the previous eight P17 milestones held again: most of what this
milestone needed was already built and named for it.

```text
requireStructuralModel      already refuses a missing control, an ineligible
                            body, a configuration override, a missing mesh, a
                            STALE mesh, a failed generation, a missing
                            material and an unusable one -- and possession of
                            a StructuralModel is the proof (ADR-036)
resultCurrency,             already the ONE place that answers "does this
analysisState               result still describe the model", with a
                            per-dependency StaleReason set (P17-DATA-001)
solveStructuralSystem       already gates settings, source agreement,
                            finiteness, the factorisation's pivot ratio and
                            an independent residual, and publishes nothing
                            unless all of them pass (P17-SOLVE-001)
recoverSupportReactions     already gates force AND moment equilibrium at a
                            measured 1e-12 (P17-REACTION-001)
StructuralResult::create    already refuses a result whose arrays do not
                            match the mesh or whose values are not finite
MeshQualityReport           already carries every measured metric, a worst
                            element per metric, a deterministic finding
                            order, and a policy slot P16 ships empty
```

So three things were genuinely missing, and only three: the quality thresholds
themselves, a constraint-sufficiency check nobody owned, and — the largest —
**an entry point that runs the whole chain**. `solveStructuralSystem` had no
production caller. Every gate above existed and nothing called them in order,
which means the safety was available rather than enforced.

### The measurement that had to come first

The brief forbids choosing a threshold from intuition. Two sweeps were written
and run before any policy existed
([QUALITY_DISTRIBUTIONS.md](../../verification/P17-VALID-001/QUALITY_DISTRIBUTIONS.md),
[ACCURACY_LAW.md](../../verification/P17-VALID-001/ACCURACY_LAW.md)), and they
decided this ADR:

```text
1  on the ASPECT RATIO the qualified and pathological sets are INVERTED:
   a qualified thin plate scores 80.0062, the worst measurable sliver 1.73205

2  on the RADIUS RATIO and the MINIMUM DIHEDRAL they INTERLEAVE: a qualified
   cylinder (3r/R 0.00124845, 0.362683 deg) is worse than a deliberate
   1.98-degree sliver (0.00089946, 1.98399 deg)

3  P16 independently measured the same thing and traced it to a single
   number -- a bored block's worst volume dihedral 1.230616 deg equals its
   boundary's worst triangle angle 1.230616 deg. A tetrahedron conforms to
   the boundary triangle it is built on, so refusing the sliver refuses
   curved geometry

4  a FLATTENING element's recovered-strain error goes as C / sqrt(3r/R),
   with C between 2.59e-16 and 2.41e-15 across SIXTEEN orders of radius
   ratio and two fields three orders of strain apart -- while its ASPECT
   RATIO saturates at sqrt(3) and sees nothing

5  a STRETCHING element's error goes as C * aspect instead, with C between
   1.76e-16 and 4.45e-16 across SEVEN orders -- and there the RADIUS RATIO
   is the poor predictor: a needle at 3r/R 1.73e-03 errs 4.45e-13 while a
   wedge at a WORSE 9.0e-04 errs 1.17e-14, thirty-eight times less
```

Findings 4 and 5 each contradicted a draft of this milestone, and both
corrections are recorded rather than overwritten
([PRIOR_DRAFT_CORRECTION.md](../../verification/P17-VALID-001/PRIOR_DRAFT_CORRECTION.md)).
The first draft claimed Tet4 reproduces a linear field exactly at every shape
and that no accuracy threshold was derivable; the second derived ONE threshold
from Finding 4 and would have accepted a needle whose recovered strain was
wrong by 2.5e-09. **Finding 5 exists because the adversarial review asked
whether Finding 4's constant was a property of the kernel or of the wedge.**

## Decision 1 — the hard failures are derived from measured accuracy laws, one bound per degeneration family

```text
3r/R   < 1e-10   ->   HARD FAILURE, the solve is refused
aspect > 3e5     ->   HARD FAILURE, the solve is refused

no hard-fail bound on either dihedral angle
```

TWO bounds, because a tetrahedron degenerates in two independent ways and
**neither metric can see the other's family**. Each number is the inverse of
its own measured law at BetterCAD's documented 1e-9 band for geometric
accumulation:

```text
FLATTENING   err <= C / sqrt(3r/R),  C <= 2.41122e-15     measured
             =>  3r/R >= (2.41122e-06)^2 = 5.81398e-12
             chosen 1e-10, a 17x margin, bounding err at 2.41e-10 (4.1x inside)

STRETCHING   err <= C * aspect,      C <= 4.45e-16        measured
             =>  aspect <= 1e-9 / 4.45e-16 = 2.247e+06
             chosen 3e5, a 7.5x margin, bounding err at 1.34e-10 (7.5x inside)
```

**The two are not two opinions about the same thing**, which is the objection
this decision has to answer. A flattening element's aspect ratio saturates at
`sqrt(3)`, so the aspect bound is blind to the family the radius bound governs;
a stretching element's radius ratio understates its error by 38x, so the radius
bound is blind to the family the aspect bound governs. One bound alone was
measurably wrong, and that is how this decision was reached rather than argued.

### Candidates

```text
A  no hard failure at all, warnings only
   REJECTED BY FINDING 4. It was this ADR's first position, and the
   measurement refuted it: an element at 3r/R 9e-22 is reported
   structurally valid by P16, accepted by the kernel, and recovers a strain
   wrong by eighty parts per million. Nobody in the chain refused it,
   because "accurate enough to solve on" belongs to the consuming phase.
   Shipping warnings only would have left a real defect behind a measured
   excuse.

B  a shape-quality line on the dihedral angle or radius ratio, tuned to
   separate good meshes from slivers
   REJECTED BY FINDINGS 1 TO 3, and forbidden by the brief. The sets
   interleave, so every such line either refuses the cylinder BetterCAD
   produces or admits a 2-degree sliver. Tuning one would be choosing which
   qualified reference model to break.

C  the accuracy law, on the radius ratio only              CHOSEN
   The threshold is a property of the kernel and double precision, measured
   over sixteen orders, and lands 3.2e6 below the worst element of any
   qualified mesh. It therefore cannot be the shape-quality line B would
   have been: a 0.198-degree sliver passes it, correctly, because its
   measured error is 1.33e-13.

D  C, plus a second bound on the ASPECT RATIO                    CHOSEN TOO
   This was REJECTED in the first version of this ADR, on the grounds that no
   law had been measured against the aspect ratio. Finding 5 measured one.
   The objection was correct and is answered by evidence rather than
   overturned by preference: `err ~ C * aspect` holds across seven orders
   with a constant that moves by a factor of 2.5, and it is the only
   predictor of the stretching family's error.

E  C or D, plus a bound on a dihedral angle
   STILL REJECTED, and for D's original reason. No law was measured against
   either dihedral angle, and Finding 2 shows they cannot separate a
   qualified cylinder from a sliver. A third bound with nothing behind it is
   the invented number the evidence exists to avoid. The asymmetry between D
   and E is exactly the difference between a bound with a measurement and a
   bound with an intuition.
```

### Why the asymmetry is the decision, not an omission

A reader's natural objection is that rejecting on two metrics and warning on
four is inconsistent. It is the opposite: it is the only consistent position
available. **A bound is justified exactly where a measured consequence
exists** — and one exists for the radius ratio and for the aspect ratio, each
on its own family, and for neither dihedral angle.

**Hard failure means "the kernel cannot recover a strain on this element to
better than 1e-9", not "this element is badly shaped."** The two are different
claims and BetterCAD only has evidence for the first. That is why a
0.198-degree sliver passes both bounds, and why this policy must not be read as
a mesh-quality standard however much it resembles one.

## Decision 2 — the warning thresholds are the measured qualified envelope, and they reject nothing

```text
TetRadiusRatio        warning 3.0e-4     qualified worst 0.000322641
TetMinDihedralAngle   warning 6.0e-3 rad qualified worst 0.006330012 rad
TetMaxDihedralAngle   warning 3.125 rad  qualified worst 3.119060453 rad
TetAspectRatio        warning 81.0       qualified worst 80.0062
```

Each is the worst value any qualified P16 reference model exhibits, rounded
away from the qualified set so that no qualified mesh warns and so that the
comparison cannot turn on a last-bit difference between this measurement and
the next build's.

The warning says one thing and the diagnostic says it: **this element is
outside the range of every mesh BetterCAD has qualified.** It is an envelope,
not an engineering judgement, and its weakness is recorded with it — the
envelope cannot flag a 2-degree sliver, because the qualified cylinder is worse
than one. Finding 2 means no threshold could, and a policy that appeared to
would be tuned.

### Candidates

```text
A  an engineering-practice band (10 deg / 170 deg, radius ratio 0.1)
   REJECTED. Nothing in this repository can check it, so it would be an
   authority claim rather than evidence -- and it would fire on four of the
   eight qualified reference meshes, making the warning mean "BetterCAD
   produced this" rather than anything actionable.

B  no warnings; report the raw worst values and let the reader judge
   REJECTED, narrowly. It is honest, and it is what P16 does. But P17 is the
   phase that is supposed to have an opinion, and "here are some numbers"
   from the consuming phase is the debt P16 deferred, not the payment.

C  the measured qualified envelope                         CHOSEN
   A derived number with a statable meaning and no invented content. It
   rejects nothing by construction, because it is a warning bound and
   `satisfiesPolicy()` does not consider warnings.
```

## Decision 3 — too few constrained degrees of freedom is a hard failure; "enough" is the solver's to judge

```text
constrained DOFs < 6   ->   HARD FAILURE, necessarily singular
constrained DOFs >= 6  ->   NOT a sufficiency verdict; the factorisation decides
```

The brief forbids the rule `constrained >= 6 -> sufficiently constrained`, and
it is right to: six constrained degrees of freedom all in `z` remove
`z`-translation and two rotations and leave three rigid modes. **The converse
is sound and is what is implemented.** A 3D continuum's rigid-body null space is
six-dimensional, the rank of a constraint set cannot exceed the number of
degrees of freedom in it, so fewer than six constraints cannot remove six
modes. That is linear algebra, not a heuristic, and it holds whatever the mesh.

What it buys is a diagnostic. P17-SOLVE-001 already refuses an
under-constrained model through its pivot gate, and says "may be
under-constrained" because it genuinely cannot tell a mechanism from a
disconnected region from bad conditioning. When the cause is a model with two
constrained degrees of freedom, this check names it before a factorisation
spends time proving it.

The asymmetry is loud in the code and in the diagnostic, because the dangerous
reading is the one the brief forbids and a reader who skims will supply it.

## Decision 4 — one entry point runs every gate in order, and there is no way around it

```text
solveStructuralAnalysis(document, regenerator, mesher, analysis, settings)
    -> Result<StructuralSolveOutcome>
```

The gates already existed; what did not exist was anything that ran them. This
is the authoritative path, and it is the only thing in the module that produces
a `StructuralResult` from a document.

### Candidates

```text
A  leave the stages public and document the required order
   REJECTED. It is the status quo, and the status quo is that
   `solveStructuralSystem` has no production caller and nothing enforces the
   order. P16 recorded the identical failure mode for meshes -- "nothing
   FORCES the holder of a mesh to ask whether it is stale" -- and ADR-036
   answered it with possession, not documentation.

B  a solver service object holding the last result and failure
   REJECTED FOR THIS MILESTONE, and it is the obvious next step. It is a
   bigger decision than acceptance policy: it owns lifetime, invalidation
   and who may publish. `analysisState` already takes `lastResult` and
   `lastFailure` as parameters precisely so that a service can be added
   without changing the currency logic, and building one here would be
   scope the TODO does not authorize.

C  one free function that runs the chain and returns everything   CHOSEN
   It cannot be bypassed because it is the only producer; it holds no
   state, so there is no cache to go stale; and it is cheap to re-run,
   which is the property ADR-036 chose for StructuralModel for the same
   reason.
```

Validation is also available WITHOUT solving —
`validateStructuralAnalysisForSolve` — because a GUI must be able to grey out a
button, and a CLI to report why a model is not ready, without paying for a
factorisation. That function runs the pre-solve stages and nothing else; the
entry point calls it rather than repeating it, so there is one implementation
of the order.

## Decision 5 — staged, short-circuiting between dependent stages and accumulating within one

A restraint cannot be resolved without a mesh, so a mesh failure stops the
chain: continuing would report a cascade of consequences and bury the cause.
But within a stage, every finding is reported — a model with three unresolvable
loads names all three.

This is deliberately NOT what `staleReasons` does, and the difference is
principled. Stale reasons are INDEPENDENT: geometry and material can both have
moved, and a user fixing one should not have to re-run to discover the other.
Validation stages are DEPENDENT. Reporting "the restraints did not resolve"
because the mesh is stale would be a true statement that sends the reader to
the wrong place.

## Decision 6 — the report carries the measurement, and the verdict is derived from the findings

```text
ValidationStatus   Accepted | AcceptedWithWarnings | Rejected
```

The status is computed from the findings rather than stored alongside them, so
a report whose findings contain a `Failure` cannot say `Accepted`. The same
construction `MeshQualityReport::satisfiesPolicy()` uses, for the same reason,
and it makes the inconsistent state unrepresentable instead of merely unlikely.

Findings are ordered by severity (most severe first), then by code, then by
element, so a report can be diffed. No wall clock, no pointer identity, no
unordered container: the `QualityThresholds` map is `std::map`, keyed by the
metric enumeration, which P16 chose for this reason.

## Decision 7 — P16 stays report-only, and P17 does not reach into it

The thresholds are a `meshing::QualityThresholds` value returned by
`structural::structuralQualityThresholds()`. P16's default is untouched;
`reportOnlyThresholds()` still classifies nothing, and no meshing source file
changed in this milestone.

Nothing here recomputes a metric. The policy reads
`MeshQualityReport::summaries`, which carry a minimum, maximum, mean and worst
element per metric whatever policy was active, and compares them against the
bounds using P16's own `direction(metric)`. One definition of the radius ratio
exists in the repository and it is in `src/meshing/MeshQuality.cpp`.

Because the summary comparison is a second place a bound could be applied, a
test runs the same meshes through `evaluateMeshQuality` with these thresholds
and requires the per-element classification to agree with the summary verdict.
Two code paths reaching one answer is acceptable; two reaching different ones
is the defect, so it is asserted rather than assumed.

## What this does NOT decide

```text
a mesh optimiser, a repair pass, an automatic remesh
    FORBIDDEN by the brief and absent. Validation never moves a node,
    swaps a tet, takes an abs(volume), deletes an element or remeshes. It
    takes const references and returns a report.

a convergence or discretisation-error estimate
    The accuracy law measures the error in recovering a field the mesh
    represents exactly. How well a mesh approximates a field it cannot is a
    different question and no milestone owns it yet.

a hard-fail bound on a DIHEDRAL ANGLE, or on element shape as such
    Deliberately not derived; see Decision 1, candidate E. The two
    bounds that DO exist are accuracy floors with measured laws behind
    them, and they sit three to six orders clear of anything qualified.
    A shape-quality rejection line is a different thing and the
    measured distributions say it cannot be drawn.

the solver service, result caching and invalidation
    Decision 4, candidate B.

P17-VIZ, P17-CMD, P17-PERSIST, P17-CLI, P17-REFMOD, P17-QUAL
    Not authorized. Finishing this milestone is a stop condition.
```
