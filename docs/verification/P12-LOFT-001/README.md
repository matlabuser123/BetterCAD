# P12-LOFT-001 — Differing Section Shapes, Smooth Interpolation, End Conditions

```text
TASK:            P12-LOFT-001
SCOPE:           sections that are not the same shape; interpolation that runs
                 continuously across the intermediate sections; end conditions
IMPLEMENTATION:  geometry::detail::matchChainByArcLength() and the closed-form
                 mixed area behind it, geometry::LoftStyle,
                 features::LoftInterpolation::Smooth
TESTS:           1174 tests per preset, 100% passed in Debug, Release and
                 Debug-shared; 28 new cases; 950 x 5 repeats in Release and
                 Debug; 0 compiler warnings in 372 translation units
VALIDATION:      prismatoid volumes from a mixed area derived and integrated by
                 hand, frustum and quadratic-of-revolution volumes, cap and
                 cut-section areas, bounds and centroids -- none read back from
                 the loft; and an independent OCCT run, outside BetterCAD, that
                 built the same sections with the kernel's own matcher
RESULT:          PASS for differing shapes and smooth interpolation;
                 BLOCKED for end conditions, with the measurement that shows why
EVIDENCE:        this directory
REVISION:        qualified on the tree committed as this milestone; the eight
                 tree IDs are listed under Final Result
```

The three things `P11-FEAT-009` left out. Two are implemented and qualified;
the third cannot be built on the kernel BetterCAD has, and is reported rather
than faked.

Nothing here was decided from the kernel's defaults. The probe
(`kernel-probe/loft_shapes_probe.cpp`, log in
`kernel-probe/loft-shapes-probe.log`) is an OCCT program outside BetterCAD
that measures what `BRepOffsetAPI_ThruSections` actually does, and its
negative results are as load-bearing as the positive ones.

## Section Correspondence for Differing Shapes

### What `P11-FEAT-009` did, and why it refused

A loft joins matching points of consecutive sections. `P11-FEAT-009` matched
two sections only when they were already **the same shape** — the same lines
and arcs, in the same cyclic order, arcs of equal sweep — and then chose which
corner met which by a **least-twist** rule: of the rotations that line the
segments up, the one whose corresponding corners move least sideways, measured
relative to each section's centroid. Anything else was refused:

```text
makeLoft: sections 1 and 2 cannot be matched: section 1 is a circle and
section 2 is 4 lines; lofts between different shapes are not supported
```

That refusal was honest, not lazy: without a correspondence of its own,
BetterCAD would have had to hand the sections to the kernel and accept
whatever pairing came back.

### Why the kernel's own matcher is not enough

`BRepOffsetAPI_ThruSections` will match differing sections itself, with
`CheckCompatibility(true)`. Probe case A shows it works; case B shows the
problem with relying on it:

| Sections | Matcher | Volume (mm³) | Against the prismatoid |
| --- | --- | --- | --- |
| square R10 → circle r5 | kernel (`CheckCompatibility(true)`) | 4058.637708 | ratio **1.004934** |
| square R10 → circle r5, circle split into 4 aligned quarters | BetterCAD's (`CheckCompatibility(false)`) | 4058.637708 | ratio **1.000000099** |
| the same, circle split 45° round instead | BetterCAD's | **3685.714479** | ratio 0.908116 |
| square → square (control) | either | 3500.000000 | ratio 1.000000000 |

Two things follow. The kernel's answer and BetterCAD's **agree to the last
digit** when BetterCAD chooses the same alignment, so the matching itself is
not in dispute. But rotating the seam by 45° gives a **different solid** — 9 %
less — while the kernel reports success either way. The alignment is therefore
engineering intent, and BetterCAD must choose it rather than discover it. With
the kernel matching, the prismatoid ratio is also 1.0049, not 1.0: the exact
volume check that guards every other loft would have to be loosened to accept
it. That is the one thing this project does not do.

### The scheme

Both loops are measured by **normalized arc length** and split wherever either
has a corner, so that they end with the same number of segments and segment
*k* of one spans the stretch that segment *k* of the other spans:

- `cornerParameters()` — where a loop's corners lie, 0 ≤ *u* < 1.
- `splitAt()` — the loop split at a set of parameters. Lines split into lines
  and arcs into arcs, so the curve itself is unchanged; a circle, which has no
  corners of its own, becomes one arc per parameter.
- `alignmentCandidates()` — the offsets worth trying: every corner of one
  against every corner of the other. That set is exactly the cyclic rotations
  `P11-FEAT-009` searches, written as a parameter shift, so the two schemes
  agree wherever both apply.
- `matchChainByArcLength()` — every section split at the union of the chain's
  corners and rotated so that segment *k* of each spans the same stretch.

The offset is chosen by the **same least-twist rule** `P11-FEAT-009` uses,
scored the same way, with ties going to the first candidate so that rounding
cannot flip a match. `LoftPlan.cpp` then hands the kernel the correspondence
with `CheckCompatibility(false)`: **BetterCAD decides, the kernel builds.**

### A circle has no corners

A circle contributes no corner parameters, so corner-to-corner offsets cannot
place its seam — every candidate differs only by the *other* loop's own
symmetry, and a square lofted to a circle came out 12117.28 mm³ against a
hand-derived 6586.03. Where one loop is a single circle the candidates
therefore also include the **angles about the centroid** of the other loop's
corners, which is the thing a circle does have.

### Chains: a section in the middle belongs to two pairs

Matching is pairwise, but a loft is a chain. Matching (i−1, i) and then
(i, i+1) splits section *i* **twice, differently**, and the kernel is then
handed sections with different numbers of edges — the first pair's
correspondence is lost, silently. Square R10 → circle r5 → hexagon R8
reproduces it (`LoftShapes_ChainsOfThreeDifferentShapesStayMatched`, written
before the fix and failing on it).

So a chain is matched **in one go**: every consecutive pair's alignment is
chosen first, from the sections as they were read, and only then is the whole
chain split, at the union of every section's corners mapped into each
section's own parameter. One pair that needs arc length pulls in the whole
chain, including any circle-to-circle pair in it.

`LoftShapes_ChainsAreIndependentOfTheirDirection` checks that the same chain
read upside down encloses the same volume, which a segmentation depending on
visiting order would not.

## The Prismatoid Check, Generalized

Every loft in BetterCAD is checked against a volume computed **without the
kernel**: between matched sections whose points move in straight lines the
cross-section is a quadratic in the height, so

```text
V = h/6 (A0 + 4 Am + A1)
```

is exact. `P11-FEAT-009` got `Am` by building the halfway section — every
point the average of its two matching points — and measuring its area. That
works while matching pairs average to a segment of their own kind. Matching a
line to an arc, which arc-length matching produces, has no such average.

The general form was derived by hand for this milestone. With **M** the mixed
area of the two matched loops,

```text
M = ∮ q × dp,     A(t) = (1−t)² A_p + t(1−t) M + t² A_q,
Am = A(1/2) = (A_p + M + A_q) / 4
```

and `M(p, p) = 2 A_p` recovers the equal-section case as a sanity check.
`mixedArea()` computes M in **closed form** for every pair of lines and arcs —
no sampling, no kernel. For a regular *n*-gon of circumradius *R* lofted to a
circle of radius *r*, matched corner to equal arc, that gives

```text
M = (2 n² r R / π) sin²(π/n)
```

For *n* = 4, *R* = 10, *r* = 5: M = 800/π = 254.647908947033 mm², and
Am = 133.296931321694 mm². An independent 4096-point sampling of the averaged
curve in the kernel probe gives **133.296911** — agreement to 1.5e-7, which is
the sampling's own error, not the formula's.

`areaMidway()` keeps `P11-FEAT-009`'s construction where it applies, so lofts
that built before this milestone are checked by exactly the same arithmetic as
before, and uses the closed form only where the old one has no answer.

### Measured against the closed forms

Release build of the qualified tree; every "expected" is computed in the test
from the formulas above, never read back from the loft.

| Loft | Measured (mm³) | Expected (mm³) | Relative |
| --- | --- | --- | --- |
| square R10 → circle r5, h 30 | 4058.637707965895 | 4058.637708132611 | 4.1e-11 |
| triangle R10 → circle r5, h 30 | 3158.732135286189 | 3158.732134944400 | 1.1e-10 |
| 4-gon → 6-gon | 4815.868861619930 | 4815.868862577821 | 2.0e-10 |
| square R10 → circle r5 → hexagon R8 | 6377.266862941225 | 6377.266864214856 | 2.0e-10 |

The independent OCCT probe, which built the first of these outside BetterCAD
with the kernel's own matcher, measured **4058.6373** — the same solid to 1e-7.

## Smooth Interpolation

### What BetterCAD means by it

> **Smooth**: the sides run continuously across the intermediate sections
> instead of being split into a band per interval. The surface passes through
> the **end** sections exactly — they are the solid's caps — and through the
> **intermediate** ones to within the kernel's approximation.

That second sentence is a measurement, not a hedge, and the milestone's first
draft got it wrong. "The surface passes through every section" was written
first and then tested, by cutting the three-circle spool on its middle
section's plane and measuring the face:

| Loft | Cut section at z = 25 (mm²) | Exact 25π (mm²) | Relative |
| --- | --- | --- | --- |
| **ruled** (the control) | 78.53981633974483 | 78.53981633974483 | **< 1e-15** |
| **smooth** | 78.54028940291627 | 78.53981633974483 | **6.0e-6** |

The ruled loft's waist is the middle section itself, lying on the crease
between two exact cones, so that row measures the *cut* rather than the loft
and comes out exact to rounding. The smooth loft's is **6.0e-6 out in area**,
a radius of 5.0000150 mm against 5 — **1.5e-5 mm**. The miss is the surface's,
not the cut's, and the claim was corrected to match.

Its volume nevertheless matches the exact quadratic integral to 3.8e-11, so
the miss is local and largely cancels when integrated — an approximating
spline oscillating about the curve it was fitted through. Both facts are
recorded; neither is allowed to hide the other.
`LoftSmooth_PassesThroughEverySection` asserts the measured bound **and** that
an order tighter fails, so the bound cannot quietly rot into a loose one.

The phrase is **not** used to mean "the kernel returned a B-spline". A ruled
loft's sides are B-spline surfaces too — that is `P11-FEAT-009`'s standing
limitation, and it is unchanged here.

### What can be predicted, and what cannot

Probe cases C, F and G measured the kernel's interpolation against every law
that might describe it:

| Sections | Measured (mm³) | Law | Agreement |
| --- | --- | --- | --- |
| 2 circles | 5497.787144 | **the ruled loft** | exact, to the last digit |
| 3 circles, equally spaced | 7330.382866 | **the quadratic through the radii**, 7330.382858 | 1.1e-9 |
| 3 squares, equally spaced | 4666.666667 | 0.8 × ruled, as the circles | exact |
| 4 circles, equally spaced | 8567.727065 | the cubic, 8594.051103 | **0.3 % out** |
| 3 circles, unequal spacing | 5037.207036 | uniform-in-index 7330.38; in-z 4458.77 | **neither** (31 % out) |

So there is **no closed form BetterCAD can state** for a smooth loft in
general. Two cases can be pinned exactly, and both are tested against a
closed form rather than against each other:

- `LoftSmooth_WithTwoSectionsIsTheRuledLoft` — the ruled and the smooth loft
  of the same two circles each meet the **frustum's** exact volume
  π h/3 (r₁² + r₁r₂ + r₂²) to `kRelTight` (1e-12), with identical topology.
- `LoftSmooth_ThroughThreeSectionsFollowsTheQuadratic` — 7330.382858652078
  against the quadratic's 7330.382858376183 (3.8e-11), and the ratio to the
  ruled loft 0.80000000003 against exactly 0.8.

### How a smooth loft is guarded, then

The exact prismatoid check cannot be applied to it, and **it was not loosened
to make one pass**. What can go wrong and be caught is the *correspondence*,
so `makeLoft(sections, Smooth, namer)`:

1. builds the **ruled** loft of the same matched sections and checks *that*
   against the prismatoid volume, exactly as before — if the matching is
   wrong, this fails;
2. requires the smooth solid to be one valid solid, free of self-interference
   (`BRepAlgoAPI_Check` with `bTestSI`), of finite positive volume;
3. requires it to lie within a **stated envelope** of the ruled loft, 0.5× to
   2.0×. This is an envelope, not a law, and it is documented as one: the
   shapes measured sit at 0.80 and 0.98 of the ruled volume, and the envelope
   is there to catch a solid built from the wrong sections, not to certify the
   interpolation.

## End Conditions — BLOCKED

**Not implemented.** `BRepOffsetAPI_ThruSections` offers no end condition, and
nothing in it can be made into one.

Probe case D set every knob the class has and measured the solid each time:

| Setting | Volume (mm³) | Faces |
| --- | --- | --- |
| default | 5497.787144 | 3 |
| `SetContinuity` C0 / C1 / C2 | 5497.787144 (each) | 3 |
| `SetParType` Centripetal / ChordLength / IsoParametric | 5497.787144 (each) | 3 |
| `SetMaxDegree` 3 / 8 | 5497.787144 (each) | 3 |
| `SetSmoothing` on | 5497.787144 | 3 |

**Identical to the last digit in every case.** Those settings choose how the
surface is approximated, not how it leaves a section.

The usual workaround — repeating a section a little way along its normal, so
the surface is forced to leave flat — was measured too (case E), and it is not
a tangent condition:

| Ghost offset | Volume (mm³) | Against the frustum | Centroid z |
| --- | --- | --- | --- |
| 0.01 mm | 6783.428691 | **1.2338** | 12.2305 |
| 0.10 mm | 6787.533524 | 1.2346 | 12.2318 |
| 1.00 mm | 6830.030956 | 1.2423 | 12.2448 |

A ghost section adds a straight collar of its own length and moves the volume
**23 % away from the frustum even at 0.01 mm**, and it does not converge as
the offset shrinks. Shipping that as "tangent to the end face" would be a
false claim, and persisting it in a document would persist a kernel workaround
as engineering intent.

A real end condition needs a surface builder that takes boundary derivatives —
`GeomFill`/`BRepFill` with prescribed tangents, or an approximation BetterCAD
drives itself. That is a new subsystem, and it is not authorized by this
milestone. **Reported, not invented.**

## Stable References

`P11-FEAT-009` names a loft's **end caps** (`FaceRole::StartCap`,
`FaceRole::EndCap`) and not its sides. Matching differing shapes does not
change that, and `LoftShapes_CapsStayNamedAcrossShapes` pins it: both caps are
found, there is exactly one of each, the start cap measures 400 mm² (the
square) and the end cap 78.5398 mm² (the circle), and a side reference is
still refused —

```text
... is a loft, whose sides are not planes and are not named
```

**Why the sides still cannot be named, now stated by construction rather than
by the kernel's opacity.** Arc-length matching does give BetterCAD a stable
index of its own — segment *k* of the matched chain is BetterCAD's
construction, not kernel traversal order — but a matched segment generally
spans *part* of one sketch entity in one section and *part* of another in the
next. There is no single `EntityId` that names it, so there is nothing
persistent to record. Naming sides here would mean inventing a face numbering,
which is exactly what must never be persisted. The limitation is therefore
**retained deliberately**, not by omission.

Nothing in this milestone persists a kernel index, a `TopoDS` identity, a
traversal order or an address. What a loft stores is what it stored before:
sketch IDs, offsets, an operation, a target — and now an interpolation.

## The B-Spline Sides Limitation, Quantified

`P11-FEAT-009` records that a loft's sides are B-spline surfaces rather than
the exact planes, cylinders or cones the same shape would have if it were
extruded or revolved. **This milestone does not lift it**, and it now has
numbers.

A smooth loft through three circles of the *same* radius (r 10, over 50 mm) is
geometrically a cylinder, so π r² h is exact:

```text
measured  15707.96326854017 mm^3
exact     15707.963267948966 mm^3     (5000 pi)
relative  3.8e-11
```

That is the cost of the approximation, measured on a case with an exact
answer: about 4e-11 relative on a 10 mm body. It is why the differing-shape
tests use `kRelApproximatedIntersection` (1e-9) rather than `kRelTight`
(1e-12), and the reason is recorded at each use rather than assumed.
Equal-shape lofts still meet `kRelTight`
(`LoftShapes_EqualShapesAreMatchedAsBefore`), so the P11 path is measurably
untouched. The cut-section measurement above puts the *pointwise* cost, which
is much larger than the integrated one, at 1.5e-5 mm.

The same approximation shows in the bounding box: the kernel bounds a B-spline
face numerically (`BRepBndLib::AddOptimal`) and pads it outwards by its
confusion tolerance. Measured: −10.0000001 for an exact −10, 30.0000001 for an
exact 30. The tests assert the bound **contains** the exact one and exceeds it
by at most 1e-7 mm — a one-sided check, not a loosened symmetric tolerance.

## Backward Compatibility

- **Same-shape sections keep `P11-FEAT-009`'s matching entirely.** The
  arc-length path is entered only when some consecutive pair is not the same
  shape. Every loft that built before this milestone is built by the same code
  and is bit-for-bit what it was (`LoftShapes_EqualShapesAreMatchedAsBefore`,
  and the existing frustum, three-section, rectangular, leaning and
  tapered-hole models unchanged).
- **`interpolation` still defaults to `ruled`**, and a file written before
  this milestone says `ruled` and still means exactly the ruled loft:
  `LoftShapes_FilesWrittenBeforeThisMilestoneStillLoadAsRuled` reloads one and
  compares volume, area, centroid, bounds and topology **by bits**.
- `"smooth"` is a new accepted value of an existing field; an unknown value is
  still rejected with its JSON path
  (`objects[4].data.interpolation: unknown value 'curved'`).

## Deliberate Regression-Test Changes

Four existing tests asserted that something **fails**, and this milestone
makes it succeed. Each is turned round deliberately, and none is deleted:

1. `tests/core/geometry/LoftTests.cpp` — `Loft_RejectsSectionsItCannotLoft`
   asserted four refusal messages: circle vs 4 lines, 4 lines vs 6, arcs of
   unequal sweep, and lines and arcs in another order. All four now **build
   one valid solid**, so the test asserts that instead; the volumes are
   checked against closed forms in `LoftShapeTests.cpp`. **Why:** those four
   cases are precisely the milestone's subject, and keeping the refusals would
   mean the feature had not been implemented.
2. `tests/features/LoftFeatureTests.cpp` —
   `LoftFeature_RejectsIncompatibleSections`, section "a circle to a
   rectangle", for the same reason.
3. `tests/features/LoftFeatureTests.cpp` —
   `LoftFeature_FailuresLeaveTheDocumentAndUpstreamBodiesIntact` needs an edit
   the geometry genuinely cannot take, to test atomicity. It used "a rectangle
   against a circle"; it now moves a section onto the other's own plane, which
   is still refused. **Why:** the test is about atomic failure, not about
   which input fails, so it keeps testing what it was written to test. The
   same substitution is made in `tests/features/ValidationTests.cpp` and
   `tests/cli/CliTests.cpp` for the same reason.

No tolerance was loosened anywhere to obtain a pass, and no reference model
was deleted.

## Tests

28 new cases, in four layers, all tagged `[loft][p12]`:

| File | Cases | What they cover |
| --- | --- | --- |
| `tests/core/geometry/LoftShapeTests.cpp` | 14 | the closed forms, chains, the refusals that remain, equal shapes unchanged, the smooth cases the probe pins, the cut section at the waist |
| `tests/features/LoftShapeFeatureTests.cpp` | 9 | parameters, face names, determinism, undo, atomic failure |
| `tests/io/LoftFileTests.cpp` | +4 | save → destroy → load → regenerate by bits, transparent JSON, old files, STEP read-back |
| `tests/cli/CliTests.cpp` | +1 | `info` and `validate` through the public CLI |

Determinism (`LoftShapes_AreDeterministic`): regenerating the same document
twice, and building a fresh document from the same definition, give the same
volume, area and centroid **by bits**, for both differing shapes and smooth
interpolation.

## Final Result

Qualified by `qualification/run-qualification.cmd`, which for each preset
configures, removes every build output, rebuilds with warnings as errors, and
runs CTest only after a successful build; then repeats this milestone's
related tests five times in Release and Debug. Logs in `qualification/`.

| Preset | Configure | Clean | Build | Tests |
| --- | --- | --- | --- | --- |
| Debug | 0 | 0 | 0, 372 TUs, **0 warnings** | **1174 / 1174** |
| Release | 0 | 0 | 0, 372 TUs, **0 warnings** | **1174 / 1174** |
| Debug-shared | 0 | 0 | 0, 372 TUs, **0 warnings** | **1174 / 1174** |
| repeat, Release | — | — | — | **950 x 5, 100 %** |
| repeat, Debug | — | — | — | **950 x 5, 100 %** |

**Nothing earlier moved.** `qualification/all-values-comparison.txt`: every
measured value of every test that existed before this milestone, from a
Release build of `P12-SWEEP-001`'s own commit (`abb9215`, built in a separate
detached worktree of that revision) and from this milestone's qualified
Release build — **48978 lines each, MD5 `0faf68a485bcdfdb5eb4411dd8d91284`
for both.** Not one volume, area, centroid, bounding box or diagnostic of a
pre-existing test changed. `qualification/regression-comparison.txt`: all
**1145** baseline test names present and passed in all three presets, 27 new.

**Across configurations** (`qualification/values-determinism.txt`): every
value measured from the kernel is identical in Debug, Release and
Debug-shared, and Debug and Debug-shared agree byte for byte. Release differs
on eight lines, and on each the differing number is a constant the *test*
computes — all downstream of `std::pow(std::sin(pi/n), 2.0)` in
`mixedPolygonCircle()`, which `-O2` evaluates two units in the last place
from `-g` (254.64790894703247659 against 254.64790894703259028, either side
of the exact 800/π). On the four entries that read a solid back, the measured
number is identical in all three; only the expectation beside it moves.

**Tolerances** (`qualification/deviations.txt`): every tolerance is met with
headroom and the tightest are met at rounding. None was widened for this
milestone; the single 1e-5 bound is the smooth loft's cut section, and its
test also asserts that an order tighter fails.

**The qualified tree is the committed tree.** The eight Git tree IDs
`qualify.cmd` recorded from a scratch index before building were compared
with the working tree after the run and are identical:

```text
  apps              0247759f0a34433331618ee594e874b4d0f3c5e3
  include           0aa008074636d613303fd07556176e54eb2815d9
  src               fd6c0c37afd337898a36a6c09a068c3671b0d06e
  tests             834e6b7b7a9d1141d56d02cfceb368e2e609d702
  examples          07f6ea93fbb93e97a4e6e989ef7636a612d26848
  cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
  CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
  CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

**Two defects were found by writing the tests these claims needed, and both
are fixed in this tree:**

1. A chain of three differing shapes split its middle section twice and lost
   the first pair's correspondence silently (above). The reproducer was
   written before the fix and failed on it.
2. `LoftSmooth_WithTwoSectionsIsTheRuledLoft` was used as a test name in two
   files, so CTest registered two tests under one name and `ctest -R` could
   not tell them apart, which would make a failure report ambiguous. The
   feature-level one is now
   `LoftSmooth_TwoSectionFeatureMatchesTheRuledFeature`. The qualification
   was re-run from clean on the corrected tree; these are its results.

**Left undone, deliberately:** the suite still has one duplicate CTest name,
`Dependency cycles are reported and block their dependents`, which predates
this milestone and belongs to the regeneration tests. Fixing it is outside
this milestone's scope.

```text
TASK:            P12-LOFT-001
IMPLEMENTATION:  arc-length section matching for differing shapes with a
                 closed-form mixed-area prismatoid check; smooth
                 interpolation guarded by the ruled loft of the same matched
                 sections
TESTS:           1174 per preset, 100 % in Debug, Release and Debug-shared
                 after a clean rebuild of each; 28 new cases; 950 x 5 repeats
                 in Release and Debug; 0 warnings in 372 translation units
VALIDATION:      closed forms derived by hand (mixed area, prismatoid,
                 frustum, quadratic of revolution), an independent OCCT probe
                 outside BetterCAD, and the kernel's own STEP reader
RESULT:          PASS for differing section shapes and smooth interpolation;
                 BLOCKED for end conditions -- the kernel offers none, and
                 the ghost-section workaround is 23 % wrong at 0.01 mm
EVIDENCE:        this directory
TODO:            P12-LOFT-001 ticked (differing shapes, smooth
                 interpolation); end conditions recorded as blocked
```

