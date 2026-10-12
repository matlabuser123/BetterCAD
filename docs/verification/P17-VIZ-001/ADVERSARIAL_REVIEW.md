# P17-VIZ-001 — adversarial review

```text
CLAIM BEING CHALLENGED
    A read-only inspection layer over the already-qualified P17 result that
    computes no engineering quantity of its own, converts no units, mutates
    nothing it reads, and refuses rather than guesses.

ASSUMPTIONS THE IMPLEMENTATION MAKES
    1. `RecoveredFields` already stores every scalar a viewer needs, so the
       viewer never has a formula.
    2. `descriptorOf`'s domain table and the two value readers agree about
       which channel lives on which domain.
    3. P16 owns the only metres-to-millimetres conversion, so a buffer leaving
       this layer in metres is consistent with the undeformed one by
       construction.
    4. A `MeshStamp` plus a `ResultSource` is enough to decide whether a
       result may be drawn as current.
    5. Possession of a `RecoveredFields` proves the result is finite and
       complete, so the viewer's own non-finite guards are belt-and-braces.
```

Read against the final diff, not the intention.

## Finding 1 — `skipped` was declared, gated on, asserted, and never written

**CONFIRMED. Fixed at the root, with a regression test, and reverified.**

`ScalarField::skipped` was a public member, documented as "entities the source
had no value for". `complete()` required `skipped == 0`. The cardinality test
asserted `field->skipped == 0`.

Nothing anywhere incremented it.

```text
grep -n skipped  src/renderer/StructuralResultView.cpp         0 writes
                 include/.../StructuralResultView.hpp          1 declaration,
                                                               1 read in complete()
                 tests/renderer/StructuralResultViewTests.cpp  1 assertion == 0
```

So the gate was vacuous and the assertion was a tautology — this project's
recorded "an unreachable refusal is an unchecked refusal", which three earlier
P17 milestones have each been caught by.

### Why it mattered, which is worse than the dead counter

The two value readers ended in `default: break;` followed by `return 0.0;`.
`field()` chooses its reader from `descriptorOf(channel).domain`, so the only
way to reach that arm is for the descriptor table to name a domain whose reader
does not serve the channel — a channel added to the enum, given a descriptor,
and forgotten by its reader.

The consequence was not an error. It was:

```text
expected   == classified        every entity got a value
nonFinite  == 0                0.0 is perfectly finite
skipped    == 0                nothing could ever set it
complete() == true             so a caller draws it
range      == [0, 0]           uniform(), so no division by zero either
```

**A complete, uniformly zero field, presented as a valid result.** That is the
P16 "the classifier processed zero entities, therefore everything is valid"
trap one layer down, wearing different clothes — and it was invisible to all
nineteen existing tests, every one of which asked whether a field came back and
whether its values matched the source for channels the readers *did* serve.

### The fix

The readers became the public numerical kernel:

```cpp
std::optional<double> channelValue(ResultChannel, const structural::NodalDisplacement&);
std::optional<double> channelValue(ResultChannel, const structural::ElementFields&);
```

- **Absent is not zero.** A node that did not move has a displacement of
  exactly 0.0, which is an ordinary answer; a stress channel asked of a node
  has no answer. The two no longer share a representation.
- Both switches now **write out every case** of the enum, so a channel added
  to `ResultChannel` fails to compile in both readers instead of silently
  joining a `default:` arm.
- `field()` counts an absent value as a skip and **refuses** with the new
  `FieldProblem::ChannelUnavailable`, mapped to `ErrorCode::Internal` because
  no argument the caller could pass would fix a disagreement between this
  library's own two tables.
- The kernel is **public** for the reason P17-SOLVE-001 exposed its own: the
  production path cannot reach the absent arm, so a test must.

### The regression test, and the proof it has teeth

`StructuralViz_AChannelWithNoValueIsAbsentNotZero` asserts, over the whole
enum, that **exactly one** of the two overloads serves each channel — not both
(which would mean an element value had been averaged onto the nodes) and not
neither (the dead arm). It then separates absent from a stored zero, and
asserts every served value equals its stored member **exactly**.

Two of its fixtures are deliberately unphysical, and that is the point:

```text
magnitude = 4.0e-6  while the components norm to 3.742e-6
vonMises  = 123.0   while the six components' invariant is 1.6e2-ish

A physically consistent fixture cannot distinguish a READ from a second
formula, because there the two agree. These can.
```

Measured, by reintroducing the defect in the nodal reader only:

```text
nodal channelValue returns 0.0 instead of std::nullopt
    -> 1 of 20 failed:  StructuralViz_AChannelWithNoValueIsAbsentNotZero
    -> the other 19 all passed

So the new case is the ONLY test that notices, which is the measurement of
the gap it closes.
```

## Finding 2 — the `skipped > 0` refusal is unreachable from the public API

**ACKNOWLEDGED, KEPT, AND DOCUMENTED AS UNREACHABLE. Not fixed, by decision.**

With the descriptor table and the two readers agreeing — which Finding 1's
test now asserts over the whole enum — `field()` cannot reach its own
`skipped > 0` branch. A mutation that removes that refusal therefore survives.

That survivor is recorded rather than hidden, and the branch is kept, because:

- it is the guard against a future channel being added to the enum and given a
  descriptor whose reader does not serve it, which is precisely how the
  original defect would have arisen in practice;
- the *behaviour* it protects is reachable and checked — `channelValue`'s
  absent arm is public and tested directly;
- removing it would restore the silent-zero field as the failure mode.

This is the same disposition P17-SOLVE-001 recorded for its own unreachable
numerical refusals: expose the kernel, test the kernel, state which values stay
unreachable through the composed path and why the guard is retained.

## The remaining questions, answered with evidence

### Could this pass its tests and still be wrong?

The `skipped` defect is exactly that case, and it was found here rather than by
a test. It is the reason this review was not a formality. After the fix, the
whole-enum agreement assertion closes the family it belonged to.

### Are the expected values independent of the implementation?

Yes, and deliberately so in three different ways:

```text
the ranges and extremes     an independent sweep in the test recomputes
                            min/max/argmin/argmax from the published arrays,
                            never from the view's own bookkeeping
the deformed buffer         x + s*u is formed in the test from the CANONICAL
                            Mesh::findNode(id)->position and the published
                            displacement, not from the view's output
the boundary highlight      compared against meshing::boundaryFacetsOf()
                            called directly, so a geometric search would
                            disagree
the kernel reads            compared against unphysical stored values chosen
                            so a recomputation CANNOT agree
```

### Did a test get weakened, deleted, skipped or renamed away?

No. Nineteen cases before this review, twenty after; the added case asserts
strictly more. No tolerance moved — the kernel assertions are exact `==`,
which is the strongest available form and is justified because every value is
a copy of a stored double rather than the result of arithmetic. No assertion
was turned into a disjunction. (This project has lost three mutation kills to a
disjunction or a tolerance "added for safety"; none was added here.)

### Is there hidden global state? Does order matter?

No state outside the objects passed in. `StructuralResultView` holds a
`MeshStamp`, a `ResultSource` and two counts, all by value. The deformed buffer
is **rebuilt from the undeformed positions on every call** — no in-place update
and no accumulator — so repeated toggles and repeated scale changes are
idempotent by construction rather than by a caller remembering to reset. The
extreme tie-break is the domain's own ascending traversal with strict
comparisons, so it does not depend on a sort nobody can see.

### Can a parameter change, or a remesh, leave a stale result drawable?

`structuralDisplayState` is the only answer to that question, and it composes
P16's `resultCurrency` with the mesh's own currency rather than forming a
second opinion. `resultMayDrawAsCurrent()` requires `SolvedCurrent` exactly;
`resultInspectable()` and `resultStaleOnACurrentMesh()` are separate questions
so a UI can show a stale result *as stale* instead of hiding or lying about it.
Four probes target exactly these predicates.

### Can a stable reference bind to the wrong face instead of failing?

Not through this layer, because this layer resolves nothing: `highlightForLoad`
and `highlightForRestraint` extract the canonical target and hand it to P16's
`highlightFor`, carrying the mapping's own `MappingState` whole. The drilled
hole wall still has no `FaceName`, and the viewer **does not** paper over it —
there is no cylinder test, axis fit, radius or coordinate tolerance anywhere in
the two production files, and the honest routes (unresolved, or P16's
face-index overload) are the only ones.

### Could Debug, Release and Debug-shared differ?

The qualification answers this and is not yet run — recorded as outstanding in
[REMAINING_SCOPE.md](REMAINING_SCOPE.md), not assumed. Two specific risks were
checked by inspection in advance:

```text
export macros on header-defined constexpr/inline
    the recorded trap that fails ONLY in debug-shared-ext.
    `complete()`, `uniform()` and the inline predicates on
    StructuralDisplayState carry no export macro: checked by grep.

a data struct with an out-of-line member
    not exported, and it has recurred in three modules. ScalarField,
    ChannelDescriptor, ExtremeEntity and the inspection/glyph structs are
    aggregates with no out-of-line members.
```

### Can a failure leave partial state committed?

`field()` builds a local `ScalarField` and returns it or an error; a refusal
publishes nothing. Nothing in this layer writes to the document, the mesh, the
result or the regenerator — which is asserted, not asserted-about:
`StructuralViz_RenderingChangesNothingItReads` compares the mesh stamp, the
node and element counts, every node position and every published displacement
and stress before and after building every channel, every extreme and a
deformed buffer at three scales.

### Was an architectural boundary crossed, or the scope widened?

One predecessor was changed: `StructuralSolveOutcome` gained `fields` and
`reactions`. Both were **already computed one statement earlier** and thrown
away, so the change is additive and computes nothing new. It is recorded in
[README.md](README.md) with its reverification (P17-VALID 38/38, structural
266/266) and it **owes P17-VALID-001 a requalification**, which is listed as
outstanding.

No OCCT header entered a non-adapter directory, no Qt header entered a
non-renderer directory, and `architecture.layering` passes. The renderer sits
at layer 70 and structural at 50, so the new dependency points downward.

## Verdict

```text
FINDINGS        2 -- one confirmed defect, fixed at the root with a regression
                     test proven to have teeth; one unreachable branch kept
                     deliberately and documented as unreachable
REQUIRED FIXES  Finding 1: DONE and reverified (20/20 targeted AT THE TIME;
                the file has since grown to 43, and the fix is still green)
VERDICT         NOT COMPLETE
```

The review is complete; the **milestone** is not, and the verdict above is
about the milestone. What remains is unchanged by this review and is listed in
[REMAINING_SCOPE.md](REMAINING_SCOPE.md): the mutation probe set, the OCCT
presentation and Qt surface, the deformed buffer's millimetre measurement, and
the three-preset qualification of both this milestone and the predecessor it
touched.

---

# The display increment — a second adversarial review

```text
CLAIM BEING CHALLENGED
    A deformed display and a nodal contour that are visual transforms of a
    derived result: no second conversion, no second authority, and no
    picture that reads as data when the data is not there.

ASSUMPTIONS
    1. toModelLength stays the only metres-to-millimetres conversion.
    2. Per-vertex colour overrides a group aspect's interior colour.
    3. MeshView::elementOfTriangle names the element whose values a
       channel holds.
    4. Every visual state the undeformed presentation has still applies to
       a deformed or contoured one.
```

Two of those four assumptions were wrong, and both were caught here.

## Finding 1 — assumption 3 was false, and it blocks the element contours

**CONFIRMED. Not fixed: it is a scope decision, recorded and refused by name.**

`volumeBoundaryOf` builds from the mesh's Triangle3 **surface** elements, so
`elementOfTriangle` returns a *triangle* id, while an element channel's values
are per *tetrahedron*. `meshing::Triangle` carries `id`, `nodes`, `region` —
no owning tetrahedron, and no accessor answers for one.

An earlier revision of this milestone's own `REMAINING_SCOPE.md` had asserted
the opposite — "`MeshView::elementOfTriangle` gives the owner-Tet mapping" —
and the implementation refuted it. The note is corrected in place rather than
quietly rewritten.

Resolved by refusing **by name** and by proving the distinction: the same
function on a `tetrahedraOf` view produces correct flat per-facet bands. *What
is absent is the mapping, not the colouring* — and without that test, "element
contours do not work" would have sent the next session looking in the wrong
place.

## Finding 2 — assumption 4 was false, and it was nearly a false checkbox

**CONFIRMED. Fixed at the root, with a test proven to have teeth.**

Assumptions 2 and 4 are incompatible and I held both. If per-vertex colour
overrides a group aspect — which this milestone *measured* — then a stale
contoured mesh cannot show `staleFacetColour`, because the contour is in the
array and the stale colour is in the aspect.

Measured: **1535** distinct colours where the stale uniform image was
required. In `Shaded` style there was no stale indication at all.

It is in [DISPLAY.md](DISPLAY.md) with the numbers, and it aborted a
qualification run. The thing to note is that it was reachable from two facts I
already had written down in the same document; the review's value was asking
how they interacted.

## The remaining questions

### Could this pass its tests and still be wrong?

The pixel tests are coverage- and colour-count-based, so a *wrong* colour
mapping that still produced many colours would satisfy them. That is why the
mapping is pinned headlessly instead:
`StructuralViz_ANodalChannelColoursEachCornerFromItsOwnNode` compares every
corner's band against an independent lookup — the view's own vertex-to-node
bridge and a linear scan of the field's id array — and requires at least one
facet to span two bands, so a per-facet mapping could not masquerade as a
per-corner one.

### Are the expected values independent of the implementation?

```text
the deformed scale      the CAD solid's silhouette, supplied by the KERNEL.
                        A self-computed millimetre expectation would agree
                        with a wrong answer; this cannot.
the contour reaching    a colour COUNT, not a colour. 1 versus 4807 is a
the screen              fact about the image, not about the palette.
the corner mapping      an independent lookup, as above.
the band colours        compared against legend.bands[i] directly.
```

### Did a test get weakened, deleted or renamed away?

No. 33 cases before this increment, 44 after. No tolerance moved — the image
comparisons are `==` on the whole buffer, and the band comparisons are exact.

### Is there hidden state? Does order matter?

Order mattered twice, and both are now pinned.

```text
the two guards          an empty deformed buffer reported a LENGTH mismatch
                        because the length check ran first, so its own
                        diagnostic was unreachable. Fixed; probe D6
                        measures it.
contour then stale      Finding 2. Fixed; the test asserts the stale image
                        both ways and that clearing stale restores the
                        bands.
```

`clearMeshContour` returning a **byte-identical** image to the never-contoured
one is the direct evidence that a contour leaves no residue — no changed
aspect, no changed style, no moved geometry.

### Can a failure leave partial state committed?

`contourCornerColours` bands the field *first*, so the legend-mismatch and
partition checks run before anything is mapped onto geometry; a refusal means
no buffer was built. `setMeshContour` validates length and every component
before touching the presentation.

### Was an architectural boundary crossed?

Two qualified P16 files were modified, which is why this increment has its own
three-preset run. No OCCT header left the adapter, `architecture.layering`
passes, and the renderer→structural edge still points downward.

The boundary that was **not** crossed is the interesting one: the element
contour needs a capability in `meshing`, and the renderer does not reach across
to build it inline.

## Verdict

```text
FINDINGS        2, both confirmed. One fixed at the root with a measured
                reproducer; one is a scope decision, refused by name with the
                distinction tested.
REQUIRED FIXES  Finding 2: DONE, reverified, and the test proven to fail
                without the fix (1535 colours versus the stale image).
VERDICT         the INCREMENT is complete and pending its qualification.
                The MILESTONE is not: 5 of 23 boxes remain.
```

### What this review cost, and why it was still cheap

It aborted a qualification 15 minutes in. Finishing would have cost ~195
minutes and recorded a PASS for a tree with a known defect.

**The lesson repeated inside one milestone.** This evidence already said probes
belong after the review, for the same reason; the review belongs before the
qualification too. Twice now an expensive run was started while a cheap gate
was still outstanding.
