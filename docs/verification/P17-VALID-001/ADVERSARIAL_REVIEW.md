# P17-VALID-001 — adversarial review

The gate CLAUDE.md describes: read the final diff and try to disprove the
milestone. **This one found three credible defects and all three were fixed
before `[x]`.** Two changed the shipped policy, and the third was found only on
a SECOND pass, after the qualification had already started -- which voided a
30-minute run and was the right call.

## 1. What did we assume?

```text
ASSUMED   the kernel's recovered-strain error is a function of the radius
          ratio, because that is the metric it was measured against
FOUND     FALSE, and this was the serious finding. A needle at 3r/R
          1.73e-03 errs 4.45e-13 while a wedge at a WORSE 9.0e-04 errs
          1.17e-14 -- 38x less. The radius ratio does not determine the
          error; each degeneration family has its own law.
RESOLVED  a second hard-fail bound, aspect > 3e5, derived from the needle
          family's own measured law. ADR-042 Decision 1 rewritten, candidate
          D reversed from REJECTED to CHOSEN with the measurement that
          caused it. PRIOR_DRAFT_CORRECTION.md Correction 2.

ASSUMED   `worstByMetric` is a list of measurements, so reusing
          `ValidationFinding` for its entries is harmless
FOUND     FALSE. Every entry carried
          `code = ElementOutsideQualifiedEnvelope` whether or not the metric
          had warned, so a consumer reading the structured payload rather
          than the message was told a metric warned when it had not. That is
          fabricated data in a structured field -- the exact defect P16's own
          `QualityFinding` documentation warns about when it refuses to name
          `TetVolume` with a value of 0 for an inverted element.
RESOLVED  a dedicated `MetricObservation` type with no code and no severity.
          A `static_assert` on its size keeps the two types distinct, and
          probe M26 confirms the element handle is load-bearing.

ASSUMED   P16's structural validity plus the kernel's own finiteness check
          together cover "is this element usable"
FOUND     FALSE, and this is the gap the milestone exists to close. An
          element at 3r/R 9.0e-22 is reported structurally valid with
          `invalidElements 0`, the kernel returns finite numbers, and the
          recovered strain is wrong by eighty parts per million. Nobody
          refused it.
RESOLVED  this milestone is the somebody. Both bounds are derived from
          measurement rather than from either layer's existing opinion.
```

```text
ASSUMED   `model->quality()` is "the mesh's quality", so reading every field
          of it is safe
FOUND     FALSE, and this one was missed on the first pass of this review.
          It is the MESHER's report, produced under the MESH CONTROL's own
          `QualityThresholds` -- a `MeshControl` carries them and
          `Mesher::generate` evaluates the held report with them. So reading
          `thresholdPolicyError` and refusing on it meant a user's broken
          REPORTING preferences blocked a structurally sound solve. P16
          still measures every metric in that case; only its own
          classification is skipped, and this module reads `summaries`.
RESOLVED  `QualityPolicyUnusable` downgraded to a WARNING, the mesh stage's
          short-circuit narrowed from "any finding" to "a refusal", and the
          diagnostic rewritten to say the structural verdict is unaffected.
          PRIOR_DRAFT_CORRECTION.md Correction 3.

          THIS REVIEW'S OWN MISS IS THE INTERESTING PART. Section 10 below
          asks "did we cross an architectural boundary", and the answer first
          recorded was "P16 untouched -- zero meshing source files changed".
          That is true, and it is not the whole question: nothing was written
          to P16, but P17 was READING P16's policy verdict and treating it as
          its own. A boundary can be crossed by reading, and a file-change
          count cannot detect it. The question that found it was narrower --
          "what, exactly, does this accessor return, and who decided it".
```

## 2. What case is missing?

```text
CHECKED   a third degeneration family
          A tetrahedron's shape can collapse toward a PLANE or toward a
          LINE, and both are measured. A "cap" or "sliver-needle hybrid" is
          a combination of the two and is bounded by whichever bound it
          crosses first -- but that is an ARGUMENT, not a measurement, and
          it is recorded as a limitation rather than as a result
          (KNOWN_LIMITATIONS.md).

CHECKED   an element worse than the worst qualified mesh but better than
          both floors
          That is the WARNING band, and it is exercised: the 0.01 mm wedge
          warns on two metrics and is accepted.

CHECKED   the exact boundary of every comparison
          No measured mesh can land exactly on a bound, so the report is
          SYNTHESISED for that case --
          `ComparesStrictlySoAValueOnTheBoundIsOnTheGoodSide` sets a summary
          to the bound and to `nextafter` past it, for a HigherIsBetter and
          a LowerIsBetter metric, and for both the warning and the failure
          bound. Without it `<` could silently become `<=`.

CHECKED   a metric with no measured elements
          `count == 0` carries a default 0.0, which against a
          HigherIsBetter bound would refuse an empty metric as
          catastrophically bad. Skipped explicitly, and tested.

CHECKED   fewer than six constrained DOFs, NON-ZERO
          The empty-restraint case alone would let a mutation narrow the
          bound to `< 1` and survive. The start cap resolves to FOUR nodes,
          measured, so a one-component restraint on it gives 4 -- and probe
          M12 is killed by it.

NOT DONE  a configuration-override refusal driven from this entry point
          `InputProblem::GeometryIneligible` carries
          `ConfigurationOverrideActive` and the path is inherited whole from
          `requireStructuralModel`, whose own suite tests it
          (P17-ARCH-001). This milestone tests the two staleness paths and
          relies on that. Recorded rather than claimed.
```

## 3. Could this pass its tests and still be geometrically wrong?

```text
The thresholds are not geometric claims, they are accuracy claims, and each
is verified in BOTH directions against measured error:

  an ACCEPTED element near the bound has a measured error inside 1e-9
  a REFUSED element far from the bound has a measured error outside it

So a bound moved without re-measuring breaks a test. That is what
`and the aspect ceiling lands where the measurement puts it, on both sides`
is for, and it asserts the error as well as the verdict.

The LAWS themselves are pinned as bands, not values -- and Correction 1's law
is asserted to FAIL on the needle family, which is what stops the two-family
finding from silently collapsing back into one law.
```

## 4. Are the expected values really independent?

```text
YES, for the accuracy laws. The oracle is the analytic gradient of a field
written down by hand; nothing in it calls `B`, `strainFrom` or any
kinematics. Two fields three orders of strain apart are used, so a constant
that was really a property of one field would show up.

YES, for the quality distributions. Every number is read from
`MeshQualityReport::summaries` -- P16's own measurement -- and the wedge
table REPRODUCES P16-QUALITY-001's published figures at the three apex
heights it recorded (0.977 / 0.0849 / 0.000899). Two independent cylinder
measurements agree within a factor of three.

PARTLY, for the envelope bounds. They are derived FROM the reference models
and then asserted AGAINST them, which is circular in form. It is not
circular in substance -- the claim is only "this is the range BetterCAD has
qualified", which is what the measurement says -- but the tightness
assertion (each bound within a factor of two of the measured worst) is what
keeps the circularity from becoming vacuous. Recorded as a limitation.
```

## 5. Did we weaken a test or move a tolerance?

```text
MOVED, ONCE, AND UPWARD IN STRICTNESS
    the aspect ceiling from 1e5 to 3e5. Not a loosening of a gate to obtain
    a pass: at 1e5 the bound sat 0.2 units from the aspect ratio of the very
    fixture that measures its law, so the verdict on its own evidence turned
    on rounding. The test caught it. 3e5 lies cleanly between two fixture
    points and carries 7.5x on the derived crossing.

LOOSENED, ONCE, DELIBERATELY AND RECORDED
    the growth-ratio bound in the wedge test, 1000x -> 1e4. The measured
    ratio is 491x, so 1000 left 2x on a ratio of two machine-epsilon-scale
    quantities -- the least stable number in the file. The real gate on that
    case is the absolute `kBound`, and the comment says so. This is the
    "weakened assertion loses its probe" hazard, checked: the upper ratio
    bound catches nothing the absolute bound misses, because `mild` cannot
    get much smaller than epsilon.

NOT WEAKENED ANYWHERE ELSE. No disjunction was added to a failing
assertion, and no tolerance was widened to make a test pass.
```

## 6. Is there hidden global state? Can save/load or undo/redo change the result?

```text
NO STATE. Every entry point is a free function over const references. There
is no cache, no service object, no static, no mutable member.

PERSISTENCE IS NOT THIS MILESTONE'S, and that is deliberate: a validation
report is derived state, recomputed from the document on demand, and
`P17-PERSIST-001` is not authorized. Nothing here is written to a file.

UNDO/REDO reaches this correctly BECAUSE nothing is cached. A restraint edit
moves the analysis object's revision, which `currentResultSource` reads, and
`resultCurrency` reports the result stale -- P17-DATA-001's machinery,
unchanged. `SolvesAWellPosedAnalysisEndToEnd` asserts the published result is
`Current` through that path rather than through a flag of its own.
```

## 7. Can a parameter change leave stale geometry? Can a stable reference bind to the wrong face?

```text
STALENESS IS THE MILESTONE'S CENTRAL TEST, not an afterthought.
`RefusesAStaleMeshThroughThePublicEntry` changes the extrude depth,
regenerates, and asserts the mesh is STILL HELD, still internally valid and
still handed out -- then that validation refuses it as `MeshStale` and the
solve publishes nothing. The intent-staleness path is tested separately.

FACE BINDING is P17-BC's and P17-LOAD's. Nothing here holds a NodeId, a
DofIndex or a boundary facet; targets are resolved against the CURRENT mesh
on every call, and `missingFace()` exercises the refusal.
```

## 8. Could Debug and Release differ? Could order of operations matter?

```text
The full suite runs unfiltered in all three presets and this milestone's
filters run under QUALIFY_REPEAT in two. The measured numbers are reported
as debug-ext measurements, which is why every law is pinned as a BAND and
every threshold carries margin rather than sitting at a crossing.

ORDER: std::map everywhere a verdict iterates, std::stable_sort for the
finding order, and the published arrays walk the mesh's own ascending
enumeration. DETERMINISM.md has the full list. Two solves are compared with
EXACT equality on every displacement, not a tolerance -- a tolerance there
would pass an order-dependent solve.
```

## 9. Can a failure leave partial state committed?

```text
NO. `solveStructuralAnalysis` returns `Result<StructuralSolveOutcome>` and
there is no outcome on any failure path. A `StructuralSolveOutcome` cannot
even be default-constructed -- `StructuralResult` has no public constructor
-- which a compile-failure case proves. So "a result that claims to be a
solution and is not" is unrepresentable rather than merely avoided.

`PublishesNothingWhenAGateRefuses` reaches the residual and equilibrium
refusals by tightening each gate below its measured value, and asserts no
outcome in both cases.
```

## 10. Did we cross an architectural boundary or widen the scope?

```text
LAYER        structural (50) including meshing (40) and core (0). No new
             module, no renumber, no OCCT, no Qt. `architecture.layering`
             passes.

P16 UNTOUCHED. Zero meshing source files changed. `reportOnlyThresholds()`
still classifies nothing, and the P17 policy is a VALUE handed to
`evaluateMeshQuality` rather than a change to it.

NO SECOND METRIC DEFINITION. grep for `radiusRatio =`, a dihedral formula or
a cross product in this milestone's production files finds nothing. Every
number is read from `summaries`.

NO REPAIR. grep for a node move, a tet swap, `abs(`, an element erase, a
merge or a remesh in the production files finds nothing. Validation takes
const references and returns a report.

SCOPE: one new header, one new source, three new test files, two
registrations. P17-BC's `RestraintResolution` was NOT touched this time --
the only predecessor file this milestone needed was already public.
```

## Residual concerns, carried rather than closed

Each is in [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md) with its reasoning:

```text
the two-family argument for completeness is an argument, not a measurement
the envelope bounds are derived from and asserted against the same fixtures
only the WORST element per metric is reported, not every offending element
the configuration-override refusal is inherited and tested upstream, not here
the laws' constants are debug-ext measurements, mitigated by bands and margin
```

## Verdict

```text
THREE CREDIBLE DEFECTS FOUND, ALL RESOLVED BEFORE [x]

  1  the accuracy law was generalised from one degeneration family, and the
     shipped policy guaranteed an accuracy it did not deliver
     -> a second derived bound, ADR-042 rewritten, correction recorded

  2  measurements were carried in a finding type and reported a warning code
     for metrics that had not warned
     -> a dedicated MetricObservation type

  3  a user's contradictory MESH CONTROL policy refused a structurally
     sound solve, because this module read P16's policy verdict and treated
     it as its own
     -> downgraded to a warning, the short-circuit narrowed, a test that
        also proves the warning cannot become an excuse

NONE WAS FOUND BY READING THE REASONING. The first came from asking whether a
measured constant belonged to the kernel or to the fixture; the second from
asking what a consumer reading the payload would be told; the third from asking
what one accessor actually returns and who decided it. All three became tests.

AND THE THIRD COST A QUALIFICATION. It was found 30 minutes into the
three-preset run, which was stopped and voided, because source changed after
the freeze. That is the rule working, not a mishap: the alternative was
shipping a known false rejection.
```
