# P17-VALID-001 — the acceptance policy, stated once

What a structural analysis requires of its input, and which layer decides each
part. Every number here is derived in
[QUALITY_DISTRIBUTIONS.md](QUALITY_DISTRIBUTIONS.md) or
[ACCURACY_LAW.md](ACCURACY_LAW.md); the reasoning is ADR-042's.

## The policy in one table

```text
                                  verdict      decided by              refuses?
-- input identity -------------------------------------------------------------
the analysis exists               hard fail    this milestone          yes
its definition validates          hard fail    P17-DATA-001            yes

-- currency, the gate P16 could not close ------------------------------------
the control exists                hard fail    P17-ARCH-001            yes
the body is eligible              hard fail    P16 geometryIneligibility yes
  (incl. a configuration override)
a mesh is held                    hard fail    P17-ARCH-001            yes
THE MESH IS CURRENT               hard fail    P16 MeshCurrency        yes
the last generation did not fail  hard fail    P16 Mesher              yes

-- the mesh itself -----------------------------------------------------------
structurally valid                hard fail    P16-DATA-001            yes
no element Invalid                hard fail    P16-QUALITY-001         yes
  (incl. a non-finite metric)
the control's own policy is       warning     THIS MILESTONE          no
  self-contradictory
3r/R   >= 1e-10                   HARD FAIL    THIS MILESTONE          yes
aspect <= 3e5                     HARD FAIL    THIS MILESTONE          yes
all four metrics vs the            warning     THIS MILESTONE          no
  qualified envelope

-- the material --------------------------------------------------------------
linear-elastic constants complete hard fail    P15 / P17-ARCH-001      yes
what the analysis MODE needs      hard fail    P17-MAT-001             yes

-- supports and loads --------------------------------------------------------
restraints resolve                hard fail    P17-BC-001              yes
constrained DOFs >= 6             hard fail    THIS MILESTONE          yes
  (NECESSARY, never sufficient)
loads resolve                     hard fail    P17-LOAD-001            yes
any load at all                    warning     THIS MILESTONE          no

-- after the solve -----------------------------------------------------------
the settings are usable           hard fail    P17-SOLVE-001           yes
every source agrees               hard fail    P17-SOLVE-001           yes
the factorisation succeeded       hard fail    P17-SOLVE-001           yes
min|D|/max|D| above the floor     hard fail    P17-SOLVE-001           yes
  (THE sufficiency verdict)
u and the residual are finite     hard fail    P17-SOLVE-001           yes
the residual is within tolerance  hard fail    P17-SOLVE-001           yes
recovered fields are finite       hard fail    P17-POST-001            yes
FORCE equilibrium                 hard fail    P17-REACTION-001        yes
MOMENT equilibrium                hard fail    P17-REACTION-001        yes
every published value is finite   hard fail    P17-DATA-001            yes
```

Three rows say THIS MILESTONE. Everything else was already a gate, and this
milestone's larger contribution is that **something now runs them in order**:
`solveStructuralSystem` had no production caller before
`solveStructuralAnalysis` existed, so the safety was available rather than
enforced.

## The four checkboxes about quality, answered exactly

The brief asks for each, and asks that "quality failure policy if used" be
recorded as `NONE` or `IMPLEMENTED with reason` rather than papered over.

### Define the FEA mesh-acceptance policy — IMPLEMENTED

Acceptance is the table above. The decisive design point is that quality
contributes exactly two refusals -- one per degeneration family, each
derived from its own measured accuracy law -- and that structural
VALIDITY, currency, material completeness, constraint sufficiency, the
residual, equilibrium and finiteness all refuse independently of any
shape score.

### Define structural-invalid mesh rejection — IMPLEMENTED, hard failure

An inverted, degenerate, repeated-handle or non-finite element is not a badly
shaped tetrahedron, it is not a tetrahedron. P16 classifies it `Invalid`, which
no threshold can produce, and P17 refuses.

Reachable only through `validateStructuralMeshQuality`, because
`requireStructuralModel` guarantees a HELD mesh is structurally valid --
`Mesher::generate` refuses one that is not. The function is public and must not
let the library become the validator, so the refusal is tested there.

### Define the quality warning policy — IMPLEMENTED, envelope-relative

```text
TetRadiusRatio         warning 3.0e-4       qualified worst 0.000322641
TetMinDihedralAngle    warning 6.0e-3 rad   qualified worst 0.006330012 rad
TetMaxDihedralAngle    warning 3.125 rad    qualified worst 3.119060453 rad
TetAspectRatio         warning 81.0         qualified worst 80.0062
```

Each is the worst value any qualified P16 reference model exhibits, rounded
away from the qualified set. The warning means one thing and says it: **this
element is outside the range of every mesh BetterCAD has qualified.** It is an
envelope, not an engineering judgement, and it refuses nothing.

Two assertions keep it honest in both directions, and neither is decoration:

```text
AcceptsEveryQualifiedReferenceMesh
    every qualified model comes back Accepted -- not merely acceptable, but
    with no warnings either, because a bound below something BetterCAD
    produces would make the warning mean "BetterCAD made this"

TheEnvelopeBoundsReallyAreTheMeasuredWorstCase
    each bound is within a FACTOR OF TWO of the measured worst. Without this
    a bound could be loosened by orders and every other test would still
    pass, with the warnings quietly meaning nothing. Measured at 1.01x to
    1.08x.
```

### Define the quality failure policy — IMPLEMENTED, one bound per degeneration family, both derived

```text
3r/R   < 1e-10   ->   HARD FAILURE
aspect > 3e5     ->   HARD FAILURE
```

Neither is chosen. Each is the inverse of its own MEASURED law, evaluated at
BetterCAD's 1e-9 band for geometric accumulation:

```text
FLATTENING   err <= C / sqrt(3r/R)   C <= 2.41122e-15   -> 3r/R   >= 1e-10
STRETCHING   err <= C * aspect       C <= 4.45e-16      -> aspect <= 3e5
```

with a 17x margin on the first crossing and 7.5x on the second. Full derivation
and the two-sided verification in [ACCURACY_LAW.md](ACCURACY_LAW.md).

**TWO BOUNDS AND NOT ONE, because neither metric can see the other's family.**
A flattening element's aspect ratio saturates at `sqrt(3)`; a stretching
element's radius ratio understates its error by 38x. This milestone first
shipped a single radius-ratio bound and it was measurably wrong — it accepted a
needle whose recovered strain was off by 2.5e-09, which adversarial review
found ([PRIOR_DRAFT_CORRECTION.md](PRIOR_DRAFT_CORRECTION.md)).

**And there is no hard-fail bound on either dihedral angle.** That asymmetry is
the policy, not an omission: no accuracy law was measured against them, and
they cannot separate a qualified cylinder from a 1.98-degree sliver, so a bound
there would refuse BetterCAD's own output. A 0.198-degree sliver passes BOTH
hard failures, correctly, because its measured recovered-strain error is
1.33e-13.

Hard failure means **"the kernel cannot recover a strain on this element to
better than 1e-9"**, never "this element is badly shaped".

## Under-constrained detection, and the rule that is NOT used

```text
constrained DOFs <  6   ->   NECESSARILY singular. Refused here.
constrained DOFs >= 6   ->   SAYS NOTHING. The factorisation decides.
```

The brief forbids `constrained >= 6 -> sufficiently constrained`, and it is
right to: six constrained degrees of freedom all in `z` remove z-translation
and two rotations and leave three rigid modes.

The converse is sound and is what is implemented. A 3D continuum's rigid-body
null space is six-dimensional, and the rank of a constraint set cannot exceed
the number of degrees of freedom in it, so fewer than six cannot remove six.
That is linear algebra and holds whatever the mesh.

What it buys is a NAME. P17-SOLVE-001 already refuses an under-constrained
model through its pivot gate and says "may be under-constrained", because it
genuinely cannot distinguish a mechanism from a disconnected region from bad
conditioning. When the cause is a model with four constrained degrees of
freedom, this check says so before a factorisation spends time proving it.

`DoesNotClaimSixDegreesOfFreedomIsSufficient` proves the asymmetry rather than
asserting it: a model restrained in `z` on three faces has far more than six
constrained degrees of freedom, validation ACCEPTS it, and the factorisation
refuses it. Both halves are checked in one test, because either alone would
look like the forbidden rule.

The refusal's own message carries "necessary condition" and "not a sufficient
one", and a test asserts those words are in it — a reader who skims will supply
the converse otherwise.

## Over- and conflicting-constraint behaviour — INHERITED, not re-decided

P17-BC-001 settled it: a constrained degree of freedom is a SET membership, so
two restraints naming the same face, or overlapping faces, or the same
component twice, produce one constrained degree of freedom and not a conflict.
There is no over-constraint error to raise because the representation cannot
express one — `ConstraintSet::constrained()` is ascending and without repeats
by construction.

P17-REACTION-001 then settled the reporting half: a shared degree of freedom is
counted ONCE globally, with per-restraint summaries that are additive by
construction.

This milestone adds nothing here, and that is the correct outcome. A second
conflict check would be a second answer that could drift.

## Stale-input checks — INHERITED and finally CALLED

P16 recorded a limitation it could not close on its own: nothing FORCES the
holder of a mesh to ask whether it is stale. `Mesher::mesh()` hands back a
stale mesh deliberately, because P16-VIZ inspects them.

`requireStructuralModel` was built to ask (ADR-036). What was missing was a
production caller. `validateStructuralAnalysisForSolve` is that caller, and
`solveStructuralAnalysis` cannot reach a result without it.

`RefusesAStaleMeshThroughThePublicEntry` asserts all three premises rather than
just the refusal: the mesh is still held, still internally valid and still
handed out, its currency is `StaleGeometry`, AND the solve publishes nothing.
Without the first three the test would prove only that something failed.

## Solver-residual, equilibrium and result-finiteness acceptance

All three were already gates that REFUSE rather than warn, so acceptance here
is defined as "that gate passed, and this is the number it passed with".
`StructuralSolveOutcome` records `residual`, `pivotRatio`, `strainEnergy`,
`forceBalance` and `momentBalance` from the stage that owns each.

**No second threshold is defined beside any of them, and none is loosened.**
A second tolerance beside a measured one is a second answer that can drift, and
`PublishesNothingWhenAGateRefuses` reaches both the residual and the
equilibrium refusals by TIGHTENING each gate below its measured value — which
also proves the gates are live rather than nominal.

## What validation never does

Checkable by grep, and checked:

```text
no node is moved            no element is deleted
no tet's nodes are swapped  no nodes are merged
no abs() on a volume        nothing is remeshed
no tolerance is loosened    no support is repaired
no material is invented     no load target is adjusted
```

Every entry point takes const references and returns a report. P16 remains
report-only: `reportOnlyThresholds()` still classifies nothing and no meshing
source file changed in this milestone.
