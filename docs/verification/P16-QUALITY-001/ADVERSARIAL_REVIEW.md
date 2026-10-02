# P16-QUALITY-001 — adversarial review

```text
SUBJECT:  an attempt to disprove the milestone before calling it complete
DATE:     2026-10-02
METHOD:   read the final diff against the lifecycle's questions; then try to
          break the implementation by mutation and see whether the suite
          notices.
```

**Three defects found, all fixed and re-verified. Four limitations carried.**

The three were found in this order, and the third was found by mutation
testing after the first two had already been fixed — which is the argument for
doing both.

---

## Finding 1 — a fabricated metric and value on a structural finding

**Severity: real. Fixed.**

`QualityFinding` carried `QualityMetric metric` and `double value` as plain
fields. For an element refused on structural grounds — an inverted
tetrahedron, say — no metric was computed at all, so the finding was emitted
as

```text
metric = QualityMetric::TetVolume     value = 0.0
```

Both wrong. The element's signed volume is **negative**, not zero, and the
finding is not about the volume metric. The message was accurate; the
structured payload was not. ARCHITECTURE.md requires errors to be structured
precisely so that a GUI or a CLI can present them **without parsing text** —
so a consumer doing the right thing would have been told the element's volume
was zero.

**Fix.** Both fields are now `std::optional`, absent when no value was
produced:

```cpp
std::optional<QualityMetric> metric{};
std::optional<double> value{};
```

Asserted by
`QualityReport_CountsAnInvalidElementAsInvalidAndGivesItNoMetrics`, and the
mutation that puts the old fabricated pair back is killed.

---

## Finding 2 — a contradictory policy was silently half-ignored

**Severity: real. Fixed.**

`validate(QualityThresholds)` existed and refused a threshold on a
dimensioned size — but **`evaluateMeshQuality` never called it**. A caller
could pass

```cpp
limits[TetVolume]      = {.warning = 1.0};    // refused by validate()
limits[TetRadiusRatio] = {.failure = 0.9};    // perfectly usable
```

straight to `evaluateMeshQuality`, and get a report in which the radius-ratio
bound was honoured and the volume bound was **never consulted** — because
nothing classifies a `ContextOnly` metric. No diagnostic, no indication, and
`satisfiesPolicy()` returning a verdict computed under half a policy.

This is the exact shape of the defect `P16-SIZE-001`'s audit found four times
over: **a control that quietly does nothing.**

**Fix.** `evaluateMeshQuality` validates the policy itself. On failure it
classifies nothing — every metric is still measured and reported, as under the
report-only default — records the diagnostic in
`MeshQualityReport::thresholdPolicyError`, and `satisfiesPolicy()` returns
false, because a verdict that was never reached is not a pass.

"Half a policy is not a policy": the usable half is **not** applied either.
Applying it would make the report's verdict depend on which of a caller's
bounds happened to be well formed.

Asserted by `QualityPolicy_AContradictoryPolicyIsReportedAndClassifiesNothing`,
which also checks that the metrics are identical to those measured without any
policy — a bad policy cannot change a number.

---

## Finding 3 — an unreachable finiteness check, found by mutation

**Severity: real, and it is the one the review nearly missed. Fixed.**

`classify()` begins by refusing a non-finite value:

```cpp
if (!std::isfinite(value)) { ...Invalid... }
```

with the comment that a NaN makes every comparison false, so a threshold check
would quietly call it `Valid`. Correct reasoning — and the branch **could not
fire**. Mutating it to `if (false)` left the whole suite green.

The reason is instructive. `classify()` was called only for the four tet shape
metrics of an element whose `defined` flag was already true, and `defined`
means "every metric finite". So the one guard against a NaN reaching a
threshold comparison was dead code, and nothing in the suite could tell.

Worse, the quantity that actually goes non-finite was **unnameable**. For a
near-degenerate element the circumradius overflows — `R = 0.173/h` for a
wedge of unit base — but `inradius` and `circumradius` were bare fields of
`TetQuality` with no `QualityMetric` names. So the report could say an element
was undefined but not which number failed, and its finding said only "a shape
metric could not be computed".

**Fix**, and it is a design change rather than a patch:

```text
1  TetInradius and TetCircumradius are now QualityMetrics (ContextOnly, m).
   Every measured quantity of an element has a name a report can print and a
   finding can point at. Those two were the only ones without one.

2  EVERY measured quantity of an element is routed through classify(), not
   just the shape metrics. A ContextOnly metric is checked for finiteness and
   never compared against a bound.

3  So the circumradius overflow now produces a finding that NAMES
   tet_circumradius, and the finiteness branch is reachable and tested.

4  Samples are still taken only from a fully measured element -- an infinity
   fed into an aggregate poisons every number it touches, and an aggregate
   over an element the report has already called Invalid would be describing
   something it refused.
```

Asserted by
`QualityTet_MarksAnElementUndefinedWhenItsCircumradiusOverflows`, which now
requires the finding to name `tet_circumradius`. The mutation that deletes the
check is killed.

**What this says about the method.** Finding 3 was invisible to reading the
diff, because the code is correct in isolation and its comment is true. It was
invisible to the tests, because they asserted the right behaviour and got it.
Only deleting the branch and finding that nothing complained exposed that the
behaviour was coming from somewhere else, and that a real failure mode had no
name.

---

## Mutation testing

Twenty-four plausible implementation mistakes, applied one at a time to
`src/meshing/MeshQuality.cpp`, each followed by a rebuild and the full
`[quality]` suite. Harness, mutation list and raw log in
`qualification/mutation/`.

A mutation **killed by the compiler** is weaker evidence than one killed by a
test — it shows the code would not build, not that the suite would notice — so
the two are distinguished, and in this run there are none: every one of the 24
was killed by a failing test.

One mutation needed rewriting to get there. Its first form replaced the
bound-ordering check's condition with `if (false)`, which left `ordered`
unused and so failed to compile under `-Werror` -- a kill, but the wrong kind.
The form in the list sets `const bool ordered = true` instead, compiles
cleanly, and is killed by
`QualityPolicy_RefusesAFailureBoundOnTheGoodSideOfItsWarning`.

```text
MUTATION                                                                  VERDICT
dihedral: the angle between the outward normals, not the internal one      killed
radius ratio: forget the factor of three, r/R instead of 3r/R              killed
inradius: r = V/A instead of 3V/A                                          killed
validity: take the absolute value of the signed volume                     killed
aspect ratio: inverted, l_min / l_max                                      killed
threshold: non-strict, so a value ON the bound is already a warning        killed
threshold: one direction for both, so lower-is-better classifies backwards killed
triangle shape quality: 4 sqrt(2) instead of 4 sqrt(3)                     killed
worst element: by value alone, ignoring the metric's direction             killed
policy: accept a threshold on a dimensioned size                           killed
policy: never report that the ordering of two bounds is wrong              killed
policy: a contradictory policy applied anyway, its error unrecorded        killed
policy: a contradictory policy's usable half applied regardless            killed
structure: skip the structural verdict and call every mesh valid           killed
findings: reversed order, so the least severe comes first                  killed
mean: divide by a fixed six instead of the sample count                    killed
undefined: a non-finite value classified instead of called Invalid         killed
undefined: an unmeasurable element's infinities fed into the summaries     killed
face winding: one face of the four wound the other way                     killed
circumcentre: a sign slip in one of the three cross products               killed
circumcentre: denominator 6V instead of 12V                                killed
edges: one of the six tetrahedron edges duplicated and one lost            killed
triangle angles: the law-of-cosines sign dropped                           killed
structural refusal: a fabricated metric and value on the finding           killed

24 applied, 24 KILLED BY A FAILING TEST, 0 killed only by the compiler,
0 survived.
```

Raw log: `qualification/mutation/results-final.txt`.

**Every mutation killed.** The run reported here is against the committed
implementation, after the three fixes above.

The earlier run, against the pre-fix tree, is what produced finding 3: one
mutation survived it. **That run's raw log was overwritten by this one**, since
the harness writes to a fixed path. Said plainly rather than reconstructed — a
log typed out from memory is not a log. What survives of it is this finding and
the mutation itself, which is in the list above and is killed by the run
recorded here.

---

## The lifecycle's questions

**What did we assume?** That a `Mesh` holds finite coordinates. Verified
rather than assumed: `MeshBuilder::addNode` refuses a non-finite coordinate at
entry, so a per-coordinate check in the quality layer would have been a branch
nothing could reach — and it was removed for exactly that reason, leaving the
determinant's own finiteness check, which *is* reachable by overflow and is
tested.

**What case is missing?** Tet10, Hex8 and Wedge6 — which do not exist in
BetterCAD, deliberately (P16-DATA-001: "an enumerator with no arity, no
orientation convention and no validation would be a promise the code does not
keep"). Nothing else: the element vocabulary is Triangle3 and Tetrahedron4,
and both are covered, together and separately.

**Could this pass its tests and still be geometrically wrong?** That is what
the two independent reference tetrahedra, the production-path closed form and
the mutation run are for. The regular tetrahedron alone would not have been
enough — too many of its metrics are 1 — which is why the cube corner, whose
dihedral range straddles the regular one's single value, is asserted beside it.

**Are the expected values really independent?** Yes. Every one was derived by
hand from the formulas in METRIC_DEFINITIONS.md and confirmed by a separate
double-precision evaluation that never calls BetterCAD. The tests carry the
closed forms as algebra, not as recorded decimals. The forbidden pattern
(`expected = productionMetric(x); EXPECT_EQ(productionMetric(x), expected)`)
appears nowhere; the single place a measured value is an input is the
threshold-strictness test, which uses the element's own radius ratio as the
*bound* and asserts nothing about its value.

**Did we weaken a test or move a tolerance?** No tolerance was changed to make
anything pass. Three bounds are used, each derived and each recorded:

```text
1e-12  the synthetic references, whose coordinates are exact
1e-11  translation invariance: eps * 100 m / 10 mm = 2.2e-12, a property of
       representing the input that no implementation can beat
1e-7   the 40 mm production block: Netgen places its interior node ~6e-11 m
       from the geometric centre, measured, which is 4.5e-9 on the radius
       ratio after the circumcentre solve
```

The 1e-7 figure is the accuracy of the **mesh**, not a loosened version of
1e-12 — the synthetic references keep 1e-12. That test exists to catch a wrong
formula, and a wrong formula is out by order one: the supplement-instead-of-
dihedral error is 0.68 rad, seven orders of magnitude outside the bound.

**Is there hidden global state?** No. Every entry point is a pure function of
its arguments; there is no cache, no static, no singleton and no
configuration read from anywhere.

**Can save/load or undo/redo change the result?** Nothing here is persisted. A
quality report is derived from a mesh, and a mesh is itself derived state that
is never written to a document file (`VolumeMesh_IsNeverWrittenToTheDocument`
File, P16-VOL-001). `P16-PERSIST-001` will persist meshing *intent*; a policy
is a value with no pointers, backend tags or mesh-local ids, so it will not
need this type to change.

**Can a parameter change leave stale geometry?** Not reachable from here.
`evaluateMeshQuality` takes a plain `Mesh`, which by ADR-031 carries no CAD
reference at all — so currency is the caller's question, answered by
`VolumeMesh::isStale` and `P16-GEOM-001`'s revision check, both already
qualified. This layer adds no second notion of staleness and no back door
around the first.

**Can a stable reference bind to the wrong face?** Not applicable: this
milestone names no faces.

**Could Debug and Release differ?** The full three-preset qualification is the
answer. Nothing here depends on an unordered container, wall-clock time, a
random seed, thread scheduling, path order or locale; elements are visited in
ascending `ElementId`, summaries are keyed in a `std::map`, findings are
sorted by a total order, and ties in the worst-element search are broken by
the lower `ElementId`.

**Could order of operations matter?** The one place it could is the
worst-element search, and the tie rule removes it. `std::accumulate` over a
deterministic order gives a deterministic mean; compensated summation is not
used, because nothing has shown it is needed and adding it on suspicion would
be optimising without evidence.

**Can a failure leave partial state committed?** No. Every function returns a
value or a `Result`; nothing mutates anything the caller owns, and the
compile-failure cases show that no implementation behind this API could.

**Did we cross an architectural boundary or widen the scope?**
`src/meshing/MeshQuality.cpp` includes only its own header, which reaches
`core` and its own module. No OCCT, no Qt, no backend header, no new
dependency, and `architecture.layering` passes. Scope: metrics,
classification and a report. No optimiser, no repair, no solver requirement,
no geometry/mesh correspondence (`P16-MAP-001`, not started and not
authorized by this document).

---

## Carried limitations

```text
BETTERCAD STATES NO QUALITY REQUIREMENT
    The shipped default is report-only, with no thresholds at all. Deliberate:
    quality thresholds are solver requirements and P17 owns those. The
    consequence is that satisfiesPolicy() reduces to structural validity until
    someone sets a policy -- which is the honest answer, not a gap.

CURVED BODIES CARRY NEAR-DEGENERATE ELEMENTS
    Measured, reported and not hidden: the cylinder at a 3 mm target has an
    element with a 0.26 deg dihedral and a radius ratio of 0.000451. The
    origin is largely the surface triangulation a tetrahedron must conform to
    -- for the bored block the volume mesh's worst dihedral and the boundary's
    worst triangle angle are the SAME number, 1.230616 deg. The owner is
    P16-SURF-001's deflection controls and, for a requirement, P17. This
    milestone's job was to find it, and no threshold was chosen to make it
    disappear.

THE CIRCUMRADIUS NORM OVERFLOWS EARLIER THAN IT NEED
    |c| is computed as sqrt(c.c), so it reports infinity once |c| exceeds
    1.3e154 even though a double reaches 1.8e308. A hypot-style norm would
    survive further. Not changed: an element flat enough to reach that has no
    useful radius ratio anyway, the behaviour is correct (report undefined,
    never clamp), and a numerical-robustness rewrite is not this milestone's
    scope.

TWO GUARDS REMAIN UNREACHABLE BY CONSTRUCTION
    A zero-length edge and a zero-area face cannot occur alongside a positive
    signed volume -- either would force the determinant to zero. The checks
    stay so that the division and the normalisation below them cannot be the
    thing that discovers a violated invariant, and they are labelled as such
    in the source rather than presented as live paths. The NON-FINITE half of
    the face check is reachable and is tested; the zero half is not.

CROSS-PRESET MESH DETERMINISM UNASSERTED
    Carried unchanged from P16-VOL-001 and P16-SIZE-001: nothing exports a
    mesh to compare between presets. A quality report is a pure function of a
    mesh, so it inherits exactly this limitation and adds none.

NO SANITIZER COVERAGE
    This MinGW ships no libasan or libubsan. Carried.
```
