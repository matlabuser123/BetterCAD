# P12-PATTERN-001 — Symmetric, Total-Length, Suppressed Instances, Patterns of Patterns

```text
TASK:            P12-PATTERN-001
IMPLEMENTATION:  symmetric and total-length pattern directions, suppressed
                 instances, and patterns whose source is another pattern
TESTS:           1111 tests per preset, 100% passed in Debug, Release and
                 Debug-shared; 41 new cases; 766 x 5 repeats in Release and
                 Debug; 0 compiler warnings in 364 translation units
VALIDATION:      closed-form transforms, instance counts, volumes, centroids
                 and bounds computed in the tests without the pattern code
RESULT:          PASS
EVIDENCE:        this directory
TODO:            P12-PATTERN-001 marked done
```

The four capabilities `P11-FEAT-005` (linear pattern) and `P11-FEAT-006`
(circular pattern) left out. Each is engineering intent the definition
stores, not a shape the regeneration infers.

## Implementation

### What a direction's length means

`PatternDistribution` (`Spacing`, `TotalLength`) says whether a direction's
`spacing` is the distance from one instance to the next or the whole row's
length, from the first instance to the last. `resolveStep()` divides a total
length once, `s = L / (N − 1)`, so the step is one value every offset is
computed from.

**The seed is instance 0 and is counted in N.** Five instances over 80 mm are
the source plus copies at 20, 40, 60 and 80 mm, spanning exactly 80 mm. A
total length therefore needs `N ≥ 2`, refused with
`a total length needs at least 2 instances to divide it between, got 1` —
at `create`/`setDefinition` for a literal count, and at regeneration for a
driven one, which is only known there.

### Symmetric directions

`PatternDirection::symmetric` puts the copies on both sides of the source,
which is then the middle instance. `patternStepMultiple(i, symmetric)` gives
the multiple of the step instance i sits at:

```text
plain      m(i) = 0, 1, 2, 3, 4, …
symmetric  m(i) = 0, +1, −1, +2, −2, …
```

Every offset is `m(i)·s·d̂`, computed from the source. No instance's position
depends on the ones before it, so no iteration order and no floating-point
accumulation can change it. The copies are numbered outward, the positive
side of the direction first, so raising the count leaves what an existing
index means unchanged: instance 3 is +2 steps whether the pattern has 5
instances or 9 (`LinearPattern_SymmetricKeepsWhatAnIndexMeansWhenTheCountGrows`).

**Odd and even counts.** A symmetric direction needs an odd count and refuses
an even one:
`a symmetric direction needs an odd count, so the source is its middle instance, got 4`.
The reason is architectural, and the message says it: instance 0 *is* the
source's own body, which a pattern never moves. An even symmetric pattern
(offsets ±s/2, ±3s/2, …) would have to move the source, which this
architecture does not do. This is a deliberate semantic decision, not an
omission; it is listed again under Known Limitations.

A symmetric circular pattern is the same rule with an angle in place of a
length. A full circle takes no symmetry — its instances already go all the
way round — and is refused:
`a full-circle pattern takes no symmetry: its instances already go all the way round`.

### Suppressed instances

`LinearPatternDefinition::suppressed` and
`CircularPatternDefinition::suppressed` hold the indices that make no
geometry. They are persistent intent, checked in two places:

- `validateSuppressed()`, on the list alone, at `create`/`setDefinition`:
  never index 0, which is the source itself, and strictly increasing, so the
  list is one set written one way;
- `checkSuppressedAgainstCount()`, once the count is resolved: every index
  must be one the pattern has, and the copies must not all be suppressed.

`patternInstances()` and `circularPatternInstances()` **mark** the suppressed
instances rather than leaving them out, so nothing is renumbered: a pattern
of 8 with 2 and 5 suppressed still has 8 indexed positions, and instance 6 is
still 6 steps from the source. Regeneration skips the marked ones when it
builds placements.

Because the index is unchanged, so is the name of every face:

- a face of instance 6 keeps its name and its place when instance 2 is
  suppressed;
- the name of instance 2 then matches **no** face. `findNamedFaces()` returns
  an empty list and `resolveFacePlane()` fails with `NotFound`. It is never
  answered with instance 3, which is still instance 3 and 60 mm from the
  source (`LinearPattern_SuppressingAnInstanceDoesNotRenameTheOthers`);
- unsuppressing restores the same index and the same geometry: volume,
  surface area, centroid and topology compare bit for bit with the pattern
  that was never suppressed
  (`LinearPattern_ReEnablingAnInstanceRestoresTheSameIdentity`).

### Patterns of patterns

`instanceOperation()` now accepts a pattern as a source.
`nestedPatternOperation()` resolves the inner pattern's own source operation
once and replays the inner pattern's instances at each outer instance,
composing the motions with the new `RigidTransform3D::after()`:

```text
outer instance k, inner instance j  →  motion(k) ∘ motion(j)
```

The copy chain a face carries is the inner pattern's step and then the
outer's, in the order the copies were made, so a nested copy names the
feature that made the face, the inner pattern and its instance, and the outer
pattern and its instance. Inner instance 0 is the inner source's own
geometry, so it carries only the outer step. The chain is the existing
`FaceSelector::copies` (P12-SKETCH-003); no transient kernel index is
involved at any level.

`instanceCountOf()` returns the instances a source really builds — the
product down the whole chain, with each level's suppressed instances left out
— and `checkNestedCount()` refuses a combination that exceeds
`kMaxPatternInstances` (500) before the kernel runs:
`a linear pattern may have at most 500 instances, got 600 (30 x the 20 instances of Row)`.
A chain deeper than `kMaxPatternNesting` (8) is refused as a cycle, as is a
pattern that names itself; a genuine dependency cycle is caught by the
regenerator before either pattern is regenerated.

`InstanceOperation` now takes the whole chain (`const std::vector<FaceCopy>&`)
instead of a single step, and `detail::appendCopies()` appends it. A mirror
still refuses a pattern source (`a mirror cannot repeat a linear_pattern`):
its image is not a motion of the source's operation, so it has no instance to
repeat, and its body scope mirrors the whole pattern instead.

### Files changed

| File | Change |
| --- | --- |
| `include/bettercad/features/LinearPatternFeature.hpp` | `PatternDistribution`, `PatternDirection::distribution` and `::symmetric`, `patternStepMultiple()`, `LinearPatternDefinition::suppressed`, `validateSuppressed()`, `checkSuppressedAgainstCount()`, `PatternStep::symmetric`, `PatternInstance::suppressed` |
| `src/features/pattern/LinearPatternFeature.cpp` | the above, and the instance list that marks suppressed instances |
| `src/features/pattern/LinearPatternRegeneration.cpp` | the total-length step, the odd-count and suppression checks on resolved counts, suppressed placements skipped, the nested cap |
| `include/bettercad/features/CircularPatternFeature.hpp`, `src/features/pattern/CircularPatternFeature.cpp`, `src/features/pattern/CircularPatternRegeneration.cpp` | the same for circular patterns |
| `include/bettercad/core/math/RigidTransform.hpp` | `after()`, the composition of two rigid motions |
| `src/features/pattern/PatternSupport.hpp`, `.cpp` | `InstanceOperation` takes the copy chain; `nestedPatternOperation()`, `instanceCountOf()`, `checkNestedCount()`, `kMaxPatternNesting` |
| `src/features/pattern/MirrorRegeneration.cpp` | the new operation signature |
| `src/features/SolidSupport.hpp`, `.cpp` | `appendCopies()` |
| `src/io/json/FeatureJson.cpp` | the `distribution`, `symmetric` and `suppressed` keys |
| `apps/bettercad_cli/DocumentCommands.cpp` | the CLI descriptions |

## Tests

41 new test cases, 3830 assertions under the milestone's own selector, all
passing in Debug, Release and Debug-shared.

| File | Cases | What they cover |
| --- | ---: | --- |
| `tests/features/PatternDistributionTests.cpp` | 17 | Symmetric offsets, bounds and centroids; the odd-count rule for literal and driven counts; total-length steps and the counts they refuse; a driven total length; suppressed instances, their indices, their names and their return; suppression of the first, last and interior copies; circular symmetry and suppression; every invalid suppression list; touching, overlapping and separate instances; undo and redo; determinism across four distribution and symmetry combinations |
| `tests/features/PatternNestingTests.cpp` | 13 | A linear pattern of a linear pattern, a circular of a linear, a linear of a circular, a circular of a circular, and three levels; the copy chain and what it must not name; suppression inside and outside a nested pattern; parameter changes reaching through; the instance cap on the product; failure propagation and atomicity; self-reference and a two-pattern cycle; determinism |
| `tests/io/PatternInstanceFileTests.cpp` | 6 | Round trip of the distribution, the symmetry, the suppression set and a nested pattern's source chain; a file written before this milestone read and written back byte for byte; malformed values rejected with the JSON path; bit-for-bit rebuilds from file across four combinations |
| `tests/cli/PatternInstanceCliTests.cpp` | 4 | `info` for a spacing, a total length, symmetry, suppression and a grid; the circular description; `validate` on a pattern of a pattern; `validate` on a suppression index the pattern has not |
| `tests/core/math/RigidTransformTests.cpp` | 1 | `RigidTransform3D::after()` against the two motions applied in sequence, for 25 pairs of motions |

Two `P11` test sections were changed deliberately, because the behaviour they
asserted is what this milestone replaces:
`LinearPattern_RefusesUnsupportedSources` and
`CircularPattern_RefusesUnsupportedSources` asserted that a pattern refuses a
pattern source. They now assert that a **mirror** source is refused, which is
still true; nesting is covered by `PatternNestingTests.cpp`. No other existing
test was changed.

## Independent Validation

Every expected position, count, volume, centroid and bound in the tests is
worked out from the definition by hand and written as a literal. The
production code is never asked what it thinks the answer is.

| Check | Reference | Where |
| --- | --- | --- |
| Symmetric offsets | m(i)·s for m = 0, +1, −1, +2, −2: `{0, 20, −20, 40, −40}` mm | `LinearPattern_SymmetricInstancesSitEitherSideOfTheSource` |
| Symmetric bounds and centroid | cubes at −40…+40 plus the seed's own 10 mm: bounds (−40, 0, 0)–(50, 10, 10); centroid (5, 5, 5), the source's own centre, because the offsets are symmetric | same |
| Total length | s = L/(N−1): 80/4 = 20 mm, 80/2 = 40 mm, 120/4 = 30 mm, 60/4 = 15 mm | `LinearPattern_TotalLengthDividesTheRowBetweenItsInstances`, `…NeedsSomethingToDivide` |
| Total length = spacing | the same offsets as a spacing of L/(N−1), compared element by element | `…TotalLengthDividesTheRowBetweenItsInstances` |
| Non-overlapping volume | V = n_active × V_seed: 5, 6, 12, 18 and 450 cubes of 1000 mm³ | the distribution, nesting and file tests |
| Overlapping volume | the union, not the sum: 5 cubes 4 mm apart span 26 mm → 26 × 10 × 10 mm³, not 5000 | `LinearPattern_TouchingAndOverlappingSymmetricInstancesFuse` |
| Touching volume | 5 cubes 10 mm apart fuse into one 50 × 10 × 10 mm bar, 1 solid | same |
| Circular angles | m(i)·step for a 30° step: `{0, +30, −30, +60, −60}`°, each bolt found by a circle signature at 40 mm radius and that angle | `CircularPattern_SymmetricTurnsItsCopiesBothWays` |
| Flange volume | V = πH(R² − n d²/4), n = 4, 5, 8 and 12 | `BoltCircleModel::expectedVolume`, used by the suppression and nesting tests |
| Plate volume | V = LWH − n π(d/2)²H, n = 6 | `HoleRowModel::expectedVolume`, `CircularPattern_RepeatsALinearPattern` |
| Nested positions | the product of the two definitions: 3 × 4 corners at (20i, 30j) mm; 8 corners of a 20 × 30 × 40 mm box | `LinearPattern_RepeatsAnotherLinearPattern`, `…RepeatsAPatternOfAPattern` |
| Nested centroid | the mean of equal cubes' centres: (25, 50, 5) mm and (15, 20, 25) mm | same |
| Half-turn images | x ↦ 2a − x about (a, 25): 20, 40, 60 ↦ 120, 100, 80 | `CircularPattern_RepeatsALinearPattern` |
| `after()` | the two motions applied in sequence, for 5 motions × 5 motions × 3 points and 3 directions, plus the orientation rule det(AB) sign | `RigidTransform_AfterAppliesTheInnerMotionFirst` |

## Instance Identity

| Change | What must not happen | Test |
| --- | --- | --- |
| count 3 → 6, 6 → 3 | an instance moving to another index | `LinearPattern_NestedPatternsFollowTheirSourceParameters`, `LinearPattern_RegeneratesWhenCountChanges` (P11) |
| spacing changed | ditto | `LinearPattern_NestedPatternsFollowTheirSourceParameters` |
| total length changed | ditto | `LinearPattern_TotalLengthDividesTheRowBetweenItsInstances` |
| symmetric ↔ plain | an index meaning a different copy | `LinearPattern_SymmetricKeepsWhatAnIndexMeansWhenTheCountGrows` |
| suppress / unsuppress | renumbering; a different body on return | `LinearPattern_ReEnablingAnInstanceRestoresTheSameIdentity` |
| several suppressed | ditto | `LinearPattern_SuppressedInstancesMakeNoGeometry` |
| first, last, interior suppressed | ditto | `LinearPattern_SuppressesTheFirstCopyTheLastAndOnesBetween` |
| a reference to a suppressed instance | resolving to the next surviving instance | `LinearPattern_SuppressingAnInstanceDoesNotRenameTheOthers` |
| pattern of a pattern | the two steps being confused, or flattened | `LinearPattern_NestedCopiesKeepTheProvenanceOfBothPatterns` |
| upstream parameter changed | the pattern losing its indices | `LinearPattern_NestedPatternsFollowTheirSourceParameters` |
| save / load / regenerate | a different definition or a different body | `LinearPattern_SaveLoadPreserves…`, `…RebuildsIdenticallyFromAFile` |
| undo / redo | a new index instead of the old one | `LinearPattern_SuppressionAndDistributionAreUndoable` |
| fresh document | a different result from the same inputs | the determinism tests |

The provenance test asserts all four cases on one grid: no copy step (the
seed), the inner step alone, the outer step alone, and both — and that the
two steps **in the other order** name nothing, as does an instance neither
pattern has.

## Failure Paths

Every one fails with a structured `Error`, keeps no body, and leaves the
document unchanged.

| Input | Code | Message |
| --- | --- | --- |
| count 0 | `InvalidArgument` | `the count must be at least 1, got 0` (P11) |
| driven count not a whole number, or over the cap | `InvalidArgument` | `the count must be a whole number from 1 to 500, got …` (P11) |
| non-finite or non-positive spacing | `InvalidArgument` | `the spacing must be positive and finite, got …` |
| non-finite, zero or negative total length | `InvalidArgument` | `the total length must be positive and finite, got …` |
| total length with count 1 (literal or driven) | `InvalidArgument` | `a total length needs at least 2 instances to divide it between, got 1` |
| symmetric with an even count (literal or driven) | `InvalidArgument` | `a symmetric direction needs an odd count, so the source is its middle instance, got 6` |
| symmetric full circle | `InvalidArgument` | `a full-circle pattern takes no symmetry: …` |
| invalid axis or direction | `InvalidArgument` | `the direction must be a finite, non-zero vector` (P11) |
| suppressing instance 0 | `InvalidArgument` | `a linear pattern cannot suppress instance 0: it is the source itself` |
| duplicate suppression index | `InvalidArgument` | `… suppressed instances are listed once, by increasing index, got 2 after 2` |
| unsorted suppression list | `InvalidArgument` | `… got 1 after 3` |
| suppression index ≥ count | `InvalidArgument` | `a linear pattern of 5 instances has no instance 5 to suppress` |
| a count that falls below a suppressed index | `InvalidArgument` | `a linear pattern of 3 instances has no instance 4 to suppress` |
| suppressing every copy | `InvalidArgument` | `a linear pattern cannot suppress every copy: 4 of 5 instances leaves the source alone` |
| missing source | `NotFound` | `object:6 references object:5, which does not exist` (P11) |
| a pattern that names itself | — | the regenerator reports a dependency cycle; neither pattern builds |
| a cycle of two patterns | — | ditto |
| the source pattern fails | the source's code | the outer pattern is blocked and keeps no body |
| the nested product over the cap | `InvalidArgument` | `a linear pattern may have at most 500 instances, got 600 (30 x the 20 instances of Row)` |
| a kernel operation that fails at one instance | the operation's own code | `instance 1 at 180 deg: Holes instance 2: hole: …` — both levels named |

**Atomicity.** `buildPattern()` builds into a local body and returns it only
when every instance has succeeded and the result is a valid solid of finite
positive volume. The first failure returns the error, so no partially
regenerated body can become the document's result; the failing feature keeps
no body and the features below it keep theirs. The nesting tests assert this
for a failure raised inside the inner pattern and for one raised by the outer
pattern's own kernel call.

## Persistence

New keys, written only when they are not their default, so a file written
before this milestone is written back **byte for byte** and still means what
it meant (`LinearPattern_AFileWithoutTheNewFieldsIsAPlainPattern`):

```json
"first": { …, "distribution": "total_length", "symmetric": true },
"suppressed": [ 2, 5, 17 ]
```

A nested pattern needs no new field: the outer pattern's `source` is the
inner pattern's ID, which the dependency graph already carries.

`LinearPattern_SaveLoadPreservesDistributionSymmetryAndSuppression` builds a
7 × 3 grid whose first direction is symmetric and given as a total length,
with three suppressed instances, then saves, destroys, loads and regenerates:
the definition compares equal field by field, the instance list is identical,
the regeneration order is the same, and the volume, surface area, centroid,
bounding box and topology compare **bit for bit**.
`…SaveLoadPreservesANestedPattern` does the same for a pattern of a pattern
and checks the nested face name still finds exactly one face, at the same
centroid. Malformed values are rejected with the JSON path
(`…MalformedInstanceFieldsAreRejectedWithTheJsonPath`).

## Determinism

Three kinds of evidence, all from the qualified tree.

**Across configurations.** The milestone's tests ran in all three presets with
`-s --rng-seed 1`; `values.py` kept every measured value and
`compare-values.py` compared them (`qualification/values-determinism.txt`):

```text
release:      2135 non-empty lines, MD5 3f4d8a8fbb9c0a38424208510b3b6c68
debug:        2135 non-empty lines, MD5 3f4d8a8fbb9c0a38424208510b3b6c68
debug-shared: 2135 non-empty lines, MD5 3f4d8a8fbb9c0a38424208510b3b6c68
RESULT: measured values identical
```

**Within a run.** `LinearPattern_DistributionAndSuppressionAreDeterministic`
builds the same pattern in four combinations of symmetry and distribution and
compares the volume, surface area and three centroid components of: the
document regenerated once, the same document regenerated again from scratch,
and a second document built the same way. `…NestedPatternsAreDeterministic`
does the same for a pattern of a pattern. The comparison is `==` on the SI
doubles, not a tolerance.

**Repeats.** The 766 tests the milestone's selector covers ran five times each
in Release and in Debug (`--repeat until-fail:5`): 3830 runs each, 100 %
passed.

**Through the file.** `LinearPattern_RebuildsIdenticallyFromAFile` builds,
saves, destroys, loads and regenerates each of the four combinations together
with a nested pattern, and compares the volume, surface area and centroid as
raw bits (`std::bit_cast<std::uint64_t>`), with the bounding box and the
topology.

## Performance

Measured, not claimed. `performance/pattern_instances_timing.cpp` builds each
case through the public API and times one full regeneration of a fresh
document, checking the result against the analytic volume so that a fast but
wrong answer would show. Release build of this tree, GCC 16.1.0 `-O2`,
OCCT 8.0.1, AMD Ryzen 7 5800H; full log in
`performance/pattern-instances-release.log`. The seed is a 5 mm cube 10 mm
apart, as in `P11-FEAT-005`'s cubes case, so the plain row is directly
comparable.

| Instances | plain | symmetric | suppressed (half built) | nested | two-way grid |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 10 | 0.229 s | 0.158 s (N=9) | 0.066 s | 0.186 s | 0.188 s |
| 50 | 3.463 s | 3.013 s (N=49) | 0.893 s | 3.353 s | 3.329 s |
| 100 | 14.806 s | 12.980 s (N=99) | 3.334 s | 12.795 s | 12.503 s |
| 250 | 95.955 s | 80.725 s (N=249) | 19.997 s | 79.756 s | 79.489 s |
| 500 | 344.655 s | 325.067 s (N=499) | 79.020 s | 326.762 s | 325.826 s |

Every row's volume matched the analytic value to a relative error of at most
4.3e−14.

What the numbers say:

- **The existing ~O(n²) cost is unchanged, and is not claimed to be fixed.**
  Doubling the count from 250 to 500 multiplies the time by 3.6 (plain) and
  4.1 (nested); n² would give 4.0. The 500-instance cap remains what keeps a
  runaway input out of the kernel. No optimization was attempted: the
  milestone's goal is correctness.
- **Nesting costs what a flat pattern of the same instance count costs.** At
  500 instances, 25 × 20 nested takes 326.8 s against 325.8 s for the same
  500 as one two-way grid and 344.7 s for a plain row of 500. Replaying the
  inner pattern's instances adds no measurable overhead.
- **Suppression costs what the instances actually built cost**, not what the
  count would cost: 500 instances with half suppressed take 79.0 s, close to
  a plain row of 250 (96.0 s), not to a plain row of 500.
- **Symmetry is free**: the same work, one instance fewer (the count is made
  odd).
- **Against the `P11-FEAT-005` baseline** on the same machine (cubes: 10 →
  0.217 s, 50 → 3.110 s, 100 → 12.448 s, 200 → 50.164 s), the plain row is
  within the run-to-run variation this machine shows. That variation is
  bounded by this run itself: plain, nested and two-way at 100 instances do
  exactly the same kernel work and differ by 18 % (12.503 s to 14.806 s).
  **No speed change is claimed in either direction.**

## Regression

**Every earlier test, by name, in all three presets**
(`qualification/regression-comparison.txt`, `-hole001.txt`):

| Baseline | Names | Missing | Not passed | New |
| --- | ---: | ---: | ---: | ---: |
| `P11-QUAL-001` Release | 737 | 0 | 0 | 373 |
| `P12-HOLE-001` Release | 1069 | 0 | 0 | 41 |

Both `RESULT: PASS`, for `ctest-debug.log`, `ctest-release.log` and
`ctest-debug-shared.log` alike. No test was removed or renamed.

**Every measured value of every earlier test**
(`qualification/all-values-comparison.txt`). `P12-HOLE-001`'s commit
(`8ee2c28`) was built in a separate clean worktree of that exact revision and
its whole suite run with the same options, then compared with this
milestone's qualified Release run:

```text
hole001-release:    46919 non-empty lines, MD5 4f1d21536de311c7510820b3fcf1a9f3
pattern001-release: 46919 non-empty lines, MD5 4f1d21536de311c7510820b3fcf1a9f3
differing lines: 0 expected only, 0 measured
RESULT: measured values identical
```

Three sets of entries were excluded, each for a stated reason, and the
comparison file records them:

- the 41 test cases this milestone adds, which have no counterpart;
- `LinearPattern_RefusesUnsupportedSources` and
  `CircularPattern_RefusesUnsupportedSources`, whose assertions this milestone
  deliberately changed: they asserted that a pattern refuses a pattern source,
  and now assert that a *mirror* source is refused;
- `Failures are reported and block dependents until fixed`, whose entries are
  pointer comparisons. `values.py` drops an entry repeated verbatim within a
  test case, so whether the second `regenerator.body(…) != nullptr` survives
  depends on whether the heap reused the same address. It did in one run and
  not the other. The assertion passed in both, and no engineering value is
  involved.

One substitution was applied to both files alike: the build-info string says
`(modified)` when the build was made from a working tree with uncommitted
changes, which the qualified build necessarily was, since the commit follows
the qualification.

**Old files keep their meaning.**
`LinearPattern_AFileWithoutTheNewFieldsIsAPlainPattern` saves a pattern from
before this milestone, checks the file has no `distribution`, `symmetric` or
`suppressed` key, loads it and checks the definition reads as `Spacing`, not
symmetric, nothing suppressed, then writes it back and compares the bytes.
Nothing reinterprets an old pattern as a total-length or symmetric one.

**Largest deviations** (`qualification/deviations.txt`). Across the
milestone's own checks the worst absolute deviation is 9.95e-14 mm against a
1e-9 tolerance (a nested cube's bound at 50 mm), and the worst relative one
3.30e-16 against 1e-12. No tolerance was loosened anywhere.

**Timing** (`qualification/timing-comparison.txt`). The 41 new tests add
30.71 s to the Release suite. The existing tests' summed time fell by 534 s
against the `P12-HOLE-001` qualification log, with a median new/old ratio of
0.542 and none slower. That is **not** a speedup from this milestone: the
`P12-HOLE-001` qualification is recorded in its own evidence as having run
under machine load, with two re-runs summing 810 s and 827 s. This run's
827.46 s matches those re-runs. **No speed change is claimed.**

## Known Limitations

- **A symmetric direction needs an odd count.** Instance 0 is the source's
  own body, which a pattern never moves, so there is always an instance at
  the centre of a symmetric span. An even symmetric pattern, whose instances
  would sit at ±s/2, ±3s/2, …, would have to move the source. This is
  refused with a message that says why, not silently rounded to an odd count.
- **A total length is the span from the first instance to the last**, and the
  seed is one of the N. A count of 1 has no span to divide and is refused.
- **Suppressing every copy is refused.** A pattern that produced only its
  source would be a pattern in name only; removing it is an edit, not a
  suppression.
- **The instance cap is 500, and the cost still grows with the square of the
  count.** For a pattern of a pattern the cap is on the product, so 25 × 20
  is allowed and 30 × 20 is not. This milestone measured that cost; it did
  not change it.
- **A mirror cannot repeat a pattern** (feature scope). A mirror's image is a
  reflection of the source's *result*, not a motion of its operation, so
  there is no instance to replay. The body scope mirrors the whole pattern's
  body instead, which is the supported way to do it.
- **A variable-radius fillet still cannot be patterned** (`P12-FEAT-006`):
  its stations run along each edge's canonical direction, which a copy's edge
  may reverse. Unchanged by this milestone.
- **Patterns nested more than 8 deep are refused** as a cycle. The dependency
  graph catches real cycles first; the depth limit is a second line of
  defence, and no reachable model needs more (the instance cap allows at most
  a chain of single-instance patterns beyond about 9 levels).
- **Suppression is by index, not by position.** Changing the count or the
  spacing keeps the suppressed indices, so instance 3 stays suppressed even
  though it has moved. That is the intent the definition records; it is not
  a geometric predicate.

## Evidence Files

| File | What |
| --- | --- |
| `performance/pattern_instances_timing.cpp`, `build-and-run.cmd` | The timing harness and how it was built and run. |
| `performance/pattern-instances-release.log` | Its run: 25 cases, every volume checked against the analytic value. |
| `qualification/qualify.cmd`, `run-qualification.cmd` | The qualification: clean rebuild and tests in each preset, then the repeats. |
| `qualification/configure-*.log`, `clean-*.log`, `build-*.log`, `ctest-*.log` | Each preset's configure, clean, build and test output. |
| `qualification/ctest-repeat-*.log` | The five-times repeats in Release and Debug. |
| `qualification/qualification-times.txt` | Times, exit codes and the Git tree IDs of what was built. |
| `qualification/reference-values-release.txt` | Every value the milestone's tests measured (Release, `-s`). |
| `qualification/values-determinism.txt` | The same values from all three presets, and their MD5. |
| `qualification/all-values-comparison.txt` | Every assertion of the whole suite, compared with `P12-HOLE-001`. |
| `qualification/regression-comparison.txt` | Every `P11` test, by name, in all three presets. |
| `qualification/deviations.txt` | The largest deviation measured in each group of checks. |
| `qualification/timing-comparison.txt` | Per-test times against `P12-HOLE-001`, and the split by new and existing tests. |
| `qualification/values.py`, `compare-values.py`, `compare-regression.py`, `compare-times.py`, `deviations.py`, `timing-split.py`, `collect-evidence.cmd` | The comparison tools. |

## Final Result

```text
RESULT: PASS
```

| Gate | Result |
| --- | --- |
| Debug | 1111 / 1111 passed |
| Release | 1111 / 1111 passed |
| Debug-shared | 1111 / 1111 passed |
| Unexpected compiler warnings | 0, in 364 translation units per preset, `-Werror` |
| Clean rebuild | every preset cleaned (`ninja -t clean`) and rebuilt from nothing before its tests |
| Legacy tests (`P11`) | 737 / 737 names present and passed, all three presets |
| Earlier `P12` tests | 1069 / 1069 names present and passed, all three presets |
| `P12-PATTERN-001` tests | 41 / 41 cases passed, all three presets |
| Repeats | 766 tests x 5 in Release and in Debug, 100 % passed |
| Independent validation | closed-form transforms, counts, volumes, centroids and bounds, none taken from the implementation |
| Failure-path tests | every row of the table above, each keeping no body |
| Serialization tests | round trip of every new field, bit-for-bit rebuilds, old files unchanged |
| Determinism | identical values in all three presets (MD5 `3f4d8a8f…`); repeated, fresh and reloaded builds identical |
| CLI tests | 4 cases, `info` and `validate` |
| Existing measured values | identical to `P12-HOLE-001`'s (MD5 `4f1d2153…`), with the three excluded sets explained |
| Performance | measured at 10, 50, 100, 250 and 500 instances; the O(n²) cost recorded, not claimed fixed |

**Environment.** GCC 16.1.0 (MinGW-W64 x86_64-ucrt-posix-seh, Brecht Sanders
r4), C++23, `-Werror`; CMake 4.4.2, Ninja 1.13.2; OCCT 8.0.1; Catch2 3.16.0;
Qt 6.11.2; Windows 11 Pro 10.0.26200, AMD Ryzen 7 5800H.

**The tree that was qualified.** `qualification/qualification-times.txt`
records the Git tree IDs of the sources built, taken from a scratch index
before the first configure:

```text
apps              3304ef81c72054eb04a9e997c6f0dc5d3fdc8fd5
include           288072ef21867301924450657fadb93e8e935de8
src               aac9ecf6550272eae1f0d678b1457f8e4207fe2f
tests             bdf753b44d8b74f4799fe2c9e35158da04424db4
examples          07f6ea93fbb93e97a4e6e989ef7636a612d26848
cmake             a84e909339b24bc7ffca591888e10d48a3e5296c
CMakeLists.txt    a0adbb9c1d3583aab4e294fd41837537e53e4764
CMakePresets.json 951b53d6b7412e6057c188dd41e40cb9e57c6aa3
```

The milestone's commit carries these same trees; nothing under them changed
between the qualification and the commit. The performance measurement was
built against the Release libraries of this `src` and `include` tree.
