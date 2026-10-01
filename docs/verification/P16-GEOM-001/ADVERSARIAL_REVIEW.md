# P16-GEOM-001 — adversarial review

Every attack the milestone brief lists, answered against the implementation. An attack answered
by a test names it; one answered by reading says what was read; one answered by a mutation says
what broke.

```text
ATTACKS              21 from the brief, plus 4 raised here
FINDINGS             6
PRODUCTION DEFECTS   1 -- in existing code, found by extraction (F2)
FIXED                6
OPEN                 1 recorded and out of scope (F6)
MUTATIONS            3 applied, 3 caught
```

Five of the six were in this milestone's own work. The sixth is a real defect in code that was
already qualified, and it only surfaced because the brief insisted on reuse rather than a second
rule.

---

## Findings

### F1 — I duplicated the existing definition of valid geometry

**Attack.** Does this layer define "meshable" in a way that could diverge from what the rest of
BetterCAD calls valid? The brief forbids it: *"Do not create a weaker 'meshing valid' test that
accepts shapes core considers invalid."*

It did. `features::Validation::checkBody` already ran exactly these checks —
`isEmpty` → `solids == 0` → `!isValid()` → no mass properties → volume not positive — and
composed them into `BodySummary::valid`. My first implementation wrote the same four into
`meshing`, in a different order: validity before closure instead of after. Two definitions of
valid geometry, differing in a way nobody would notice until a shape passed one and failed the
other.

**Resolution.** Extracted `features::bodyDefect(const Body&)` as the one definition.
`validateDocument()` keeps the sentences for a person; `meshing` maps the typed reason. One
rule, two presentations — the same shape as `dirtySources()` for staleness.

### F2 — PRODUCTION DEFECT: an infinite volume counted as a positive one

**Attack.** Found while extracting F1: is the existing positivity test sound?

```cpp
} else if (!(properties->volume > Volume{})) {   // the old test
```

**`Inf > 0` is true.** So a body whose volume integrated to infinity passed as positive, and
`BodySummary::valid` was `true` for it. `validateDocument()` would have called such a body
valid, and any consumer that trusted that would have gone on to use it.

This is a defect in code qualified before P16, and it was invisible while the rule lived in one
place that nobody had reason to re-read. Extraction is what exposed it.

**Resolution.** `bodyDefect` requires `volume > 0` **and** `isFinite(volume)`. That strictly
strengthens `validateDocument()` as well as the meshing boundary. Nothing could have depended on
the old behaviour: an infinite volume is not a shape.

### F3 — `state()` is not a currency check, and nothing exposed one

**Attack.** Can a body that no longer follows from the document be meshed?

Before this milestone, yes, and nothing in the repository could have stopped it. `state()`
reports **what the last pass did**. After an edit with no regeneration:

```text
state(feature)          UpToDate      <- true of that pass, and useless here
body(feature)           the old body  <- valid, closed, positive volume
builtRevision(feature)  != revisionOf <- the only thing that knew, and it was PRIVATE
```

**Resolution.** `Regenerator::builtRevision()` and `isCurrent()`, with the dirtiness rule
factored into one private `dirtySources()` shared with `regenerate()`.

### F4 — An own-revision check would have looked right and been wrong

**Attack.** Is comparing the feature's own built revision to its current revision enough?

No, and this is the trap. **Editing a sketch does not change the revision of the extrude that
consumes it.** A guard built that way would have called the extrude's body current while its
profile had moved underneath it — and it would have passed a test that edited the feature
directly.

**Resolution.** `isCurrent()` walks the dependency graph, seeding from `dirtySources()` and
testing membership in `downstreamOf()`, which includes its seeds.

**Mutation-proved, not argued.** Removing the graph walk — leaving the own-revision comparison —
fails **four** tests, including the one that asserts `isCurrent` is false after a sketch edit.

### F5 — The check ordering was not pinned, and the test that claimed it did not

**Attack.** Is a stale body reported as stale, or as whatever defect it happens to have?

The code checks currency before inspecting the body, and
`Meshable_StalenessIsReportedBeforeTheOldBodyIsInspected` looked like the proof. **It was not.**
Moving the currency check to the end of `classify()` broke nothing: the stale body in that
fixture is valid, closed and positive-volume, so every later check passes and `GeometryStale` is
returned either way. The test proves a *good* stale body is refused. It says nothing about
order.

My first conclusion was that the ordering could not be pinned, because a body that is both stale
and geometrically defective is not constructible — features validate their results.

**That conclusion was wrong, and checking it is what produced the fix.**
`LoftFeatureTests` has a case where an `Intersect` operation that misses leaves an **empty body
that the regenerator stores with a healthy state**: `body() != nullptr`, `isEmpty()`, not
`Failed`. So a feature can hold an up-to-date, geometrically unusable body.

**Resolution.** `EmptyIntersection` builds exactly that from two disjoint boxes, giving two new
tests: one for `EmptyBody` — a branch I had been about to record as unreachable — and
`Meshable_ReportsStalenessRatherThanTheDefectOfAStaleBody`, where the body is **both** stale and
empty so the two candidate diagnostics differ. Moving the currency check to the end now fails
that test. The ordering is a property of the suite.

Why it matters beyond tidiness: reporting `EmptyBody` for a stale body is a confident statement
about geometry the user has already replaced, and it sends someone debugging the wrong thing.

### F6 — OPEN, OUT OF SCOPE: mass properties has no currency check

**Attack.** Does any other consumer of `Regenerator::body()` have the F3 gap?

`partMassProperties` checks the configuration override, then nullptr, then `Failed`/`Blocked`,
then volume. It does **not** check built-revision currency. So editing a sketch and asking for
mass properties without regenerating returns a mass computed from the previous geometry.

**Status.** Recorded, not fixed. This milestone authorizes the meshing geometry boundary, not a
change to P15 behaviour, and `isCurrent()` now exists for whoever takes that decision. The
meshing tests demonstrate that the meshing boundary refuses the case. Whether mass properties
should refuse it, or whether its contract is that callers regenerate first, is a scope decision.

---

## The brief's attacks, answered

**Can a previous successful body be meshed after the current regeneration fails?** No, twice
over. The regenerator erases the body on `fail` and on `block`, which `RegeneratorTests` states
as a property, and `Meshable_RefusesAFeatureBlockedByAFailedUpstreamSketch` checks
`body() == nullptr` and then checks the refusal anyway.

**Can blocked geometry reuse the old body?** No — same test, and `Blocked` is reported
distinctly from `Failed`, which the same test also checks on the sketch.

**Can configuration effective values change while geometry stays old and still pass?** No —
`Meshable_RefusesEveryGeometryWhileAConfigurationOverridesAParameter`. It records the defect
(the body's volume is unchanged under the override) and then requires the refusal, naming the
configuration and the reason. It deliberately does not record what the stale volume *is*, because
that would turn a defect into a contract.

**Can meshability rely only on `body != null`?** No. Three of the eleven ineligibility reasons
fire with a non-null body: `GeometryStale`, `EmptyBody`, and the `NoBody` case is the one that is
*about* nullness and is distinguished from failure.

**Can an open shell pass because `BRepCheck` says the shape is valid?** No, and the order is
deliberate: `bodyDefect` tests `topology().solids == 0` **before** `isValid()`. A shell can be a
perfectly valid shape and still enclose nothing, so closure is a topological question answered
before any measurement.

**Can a zero-volume solid pass because topology says SOLID?** No — `bodyDefect` continues to the
volume after the solid check, and requires positive and finite.

**Can a hole disappear during preparation?** No. Nothing is rebuilt: a `Body` copy shares the
kernel shape. `Meshable_AHollowTubePreservesItsVoidAndItsAnalyticalVolume` pins the arithmetic
that would catch it — using the outer bounding cylinder instead of the annulus would be 2.78x
larger.

**Can a hollow cavity be accidentally filled?** Same test, same reason, and it also checks the
face count is more than three so a filled tube could not pass.

**Can a top-level COMPOUND hide multiple solids?** `topology().solids` counts solids wherever
they sit, rather than switching on a top-level shape type.
`Meshable_AcceptsAMultiSolidBodyAndReportsTheSolidCount` gets 2 from one extrude of two disjoint
rectangles and checks the hand-computed volume of both.

**Can a one-solid compound be rejected incorrectly?** No, for the same reason: the count is what
is tested, not the container.

**Can a transformed body's `TopLoc_Location` be stripped?** There is no code path that could.
This layer never names a `TopoDS_Shape`, a `TShape` or a `Location` — the architecture check
confines OCCT to `src/(.+/)?occt/` and `src/meshing/` has no such directory.
`Meshable_KeepsABodyWhereItIsInModelSpace` checks the prepared body's bounding box equals the
regenerator's exactly, for a body built on the XZ plane away from the origin.

**Can shape copying destroy future face mapping?** No: `Body` copies share the shape, so
subshape identity is untouched and P16-MAP-001's attribution is not disturbed.

**Can geometry preparation silently heal the shape?** No. `ShapeFix` and `ShapeAnalysis` appear
in no source file in the repository, and this layer adds none.
`Meshable_PreparationDoesNotTouchTheDocumentOrTheRegenerator` checks the document revision, the
built revision, the body pointer and the body's topology are all unchanged after four
preparations.

**Can the meshing tolerance differ arbitrarily from the existing geometry tolerance?** There is
no meshing tolerance to differ. Closure is topological and volume is "positive and finite" —
neither has a threshold, so §32's prohibition is satisfied by not needing one.

**Can a material edit invalidate geometry unnecessarily?** No —
`GeometryRevision_IsUnchangedByAMaterialEdit` creates a material, assigns it, checks the document
revision moved, and checks the geometry revision did not and the geometry is still meshable. This
is the narrowing ADR-030 authorized.

**Can a geometry edit fail to change currentness or the revision?** No —
`GeometryRevision_ChangesWhenAnUpstreamProfileChangesTheGeometry` edits the *sketch*, whose
revision the feature does not share, and requires the stamp to move.

**Can stale geometry produce the wrong diagnostic because validation inspects it first?** Not any
more; see F5. It could before, and the mutation proves the fix.

**Can finite-but-pathological geometry overflow a volume check?** The positivity test requires
finite, which is F2. A volume that overflows to infinity is refused rather than accepted as very
large.

**Can a read-only geometry query mutate the document?** Every argument is `const` and nothing in
the call graph takes a mutable reference. The test above checks observable state as well as the
signature.

**Can the backend later receive a different shape from the one validated here?** **Not
guaranteed, and recorded rather than claimed.** `MeshableGeometry` carries the exact body that
was validated, so BetterCAD hands on what it checked. What a backend does internally — including
repairing geometry — is outside this layer's control, and auditing it is `P16-VOL-001`'s, which
cannot start until the backend decision is taken.

---

## Attacks raised here

**Can `isCurrent` be fooled by an item that is current but whose dependency was deleted?** A
missing reference makes the dependent a dirty source in `dirtySources()`, which is one of the
terms shared with `regenerate()` — so a deleted dependency makes the dependent stale by the same
rule that would make the regenerator rebuild it.

**Does `isCurrent` answer the same question as `regenerate`?** Not quite, and the header says
which term it omits: `regenerate()` additionally seeds from parameter expressions that failed in
the pass it is running, which only a pass can know. The omission can only make `isCurrent` answer
*current* where a pass would have found more work, never *stale* where a pass would have found
none.

**Can two topological orders of the same graph give two different geometry revisions?** No. The
stamp mixes dependencies in **ascending ObjectId**, not topological order, because two valid
topological orders of one graph would otherwise stamp the same model differently.

**Can the stamp differ between presets?** The mix is splitmix64's finalizer written out, not
`std::hash`, which is not required to agree between builds. the stability test
(`GeometryRevision_IsStableWhenNothingChanges`) repeats it eight times in process, and the three-preset regression runs it in Debug,
Release and Debug-shared.

---

## What this review could not establish

`NotASolid`, `InvalidBRep` and the `VolumeUnavailable`/`NonPositiveVolume` paths are **not
exercised by a fixture**, because no feature was found that stores a body with a solid count of
zero, a shape the kernel rejects, or a non-positive volume. `EmptyBody` was in that category
until the `Intersect`-that-misses route was found, which is a reason to treat the remaining three
as not-yet-reachable rather than unreachable.

They are kept on the same grounds P15-MASS-001 states for its own unreachable guard: *"what it
guards against is a mass computed from geometry that no longer follows from the document. That is
worth a lookup even at a probability of zero."* Here the cost is one call to a function the rest
of BetterCAD already calls, and what it guards against is meshing a shape the kernel does not
accept.

The branches are not untested in the sense that matters: `features::bodyDefect` is the same
function `validateDocument()` uses, and the existing validation suite covers its ordering and its
messages.
