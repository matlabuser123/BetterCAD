# ADR-023 — A drawing handler resolves references, never the drawing

```text
Status:    Accepted
Date:      2026-09-24
Milestone: P14-REGEN-001
Builds on: ADR-008 (the assembly solve is a final pass)
           ADR-011 (drawing intent is canonical, projection is derived)
           ADR-014 (drawing geometry is built on demand) — which MANDATED
                   these handlers and left what they check open
Answers:   what "a handler resolves the object's references" means, given
           that the assembly solve runs AFTER the object phase
```

## Context

`ADR-014` decided, in as many words, that `Sheet`, `View`, `Dimension` and
`Annotation` "each get a `RegenerationHandler` registered by the drawing
module", that such a handler "resolves the object's references and **fails**
if one does not resolve", and that this "is not optional politeness: an
object with no handler is silently marked `UpToDate` and never validated".

**They were never implemented.** `grep` for `registerHandler` across
`include/bettercad/drawing/` and `src/drawing/` matches nothing; the only
module that registers any is `assembly`. So every sheet, view, dimension and
annotation in this codebase takes the branch `ADR-014` named
(`src/features/Regenerator.cpp:344-347`):

```cpp
} else {
    // Parameters and plain data objects are inputs: nothing to build.
    states_[id] = NodeState::UpToDate;
}
```

A dimension whose face has stopped existing regenerates as `UpToDate` and
reports success. That is the defect `P13-REGEN-001` fixed for mates, still
open for drawings, and `P14-REGEN-001` is where it closes.

The half that is already right should be said plainly, because it decides how
much is left to build. **The dependency edges exist, and are nearly correct.**
`View::dependencies()` returns its sheet, its local source and its parent;
`Dimension::dependencies()` and `Annotation::dependencies()` return their view
plus `referencedObjects(target)`. So the graph already marks a drawing object
dirty when the model it names changes, and already blocks it when that model
fails. What is missing is the handler at the end of that chain — and one edge
that should never have been declared.

**An assembly view declared a dependency on `ObjectId{0}`.** Such a view names
no source at all (`ADR-021`), so `definition_.source` is a default-constructed
`ObjectReference`; `localTarget()` returns a reference's object whenever the
reference is internal, and a default one is internal with an *invalid* object.
`Dimension::dependencies()` and `Annotation::dependencies()` both guard with
`isValid()`. `View::dependencies()` did not. The graph's missing-reference
check therefore failed **every assembly view ever created**, since
`P14-ASM-001`, with "references object:0, which does not exist".

It went unnoticed because the drawing fixtures required `regenerateAll()` to
*return* a report, which it does even when objects inside it failed, and never
asserted `succeeded()`. The one-line guard is part of this milestone, because
a view that always fails regeneration has no dirty-propagation contract to
speak of.

The open question is what the handler may look at, and it is not free to
choose. Three constraints bound it.

**The assembly solve has not happened yet.** `ADR-008` made it a *final pass*:
`transforms_.clear()` runs after the object loop, and the passes fill it
(`src/features/Regenerator.cpp:356-368`). During the object phase
`Regenerator::transforms()` therefore holds **the previous pass's map**. A
drawing handler that consulted it would read exactly the stale derived state
this milestone exists to forbid.

**The full drawing resolvers need that map.** Not incidentally — by design:

```text
Annotations.cpp:476   a balloon moves its anchor by the occurrence's solved
                      transform, and FAILS "the assembly did not solve" when
                      there is none (ADR-005)
Dimensions.cpp:337    a dimension in a view OF a component does the same
```

So a handler calling `draw()` or `measure()` has no good option: the live map
is stale, and an empty one makes every balloon fail on every pass.

**Projection is the expensive operation in this codebase.** `viewGeometry()`
resolves five references and then runs exact HLR (`ADR-019`). Calling it per
view per regeneration pass would build, and throw away, the geometry `ADR-014`
says is built on demand — against `TODO`'s own "regenerate only affected
drawing state where feasible".

## Options

**Option 1 — the handler runs the object's full resolver.** `viewResolution`,
`dimensionResolution`, `annotationResolution` from `drawing/Resolution.hpp`,
qualified in `P14-STREF-001`. One resolver, one answer, nothing new to write.

**Option 2 — the handler runs the reference-resolving PREFIX of that same
resolver**, and stops where geometry construction begins.

**Option 3 — a drawing final pass**, registered after `assembly.solve` in name
order, validating every drawing object once the transforms are published.

## Comparison

| | 1 full resolver | 2 prefix | 3 final pass |
| --- | --- | --- | --- |
| Correct during the object phase | **no** — stale or absent transforms | yes | yes |
| Catches a reference that stopped resolving | yes | yes | yes |
| Catches a projection that fails numerically | yes | no | yes |
| Cost per pass | one HLR per view | a lookup per reference | one HLR per view |
| Per-object dirtiness | yes | yes | **no** — all, every pass |
| Blocks dependents | yes | yes | **no** |
| Builds geometry during regeneration | yes | **no** | yes |
| New public surface | none | two functions | a pass |

Option 1 fails on the first row, and that is decisive: a balloon would report
"the assembly did not solve" on every pass of a perfectly good document, and
an assembly-view dimension would be measured against the previous pass's
positions. A validator that is wrong is worse than no validator.

Option 3 is viable and was taken seriously: a pass named after `assembly.solve`
does see published transforms, and `FinalPass` may mark objects failed. It
loses the two things that make per-object regeneration worth having — the
graph's dirty set, so every drawing is re-validated on every pass, and
`block()`, so a view whose source failed is not blocked and a stale view is
not stopped from being drawn. `ADR-014` already rejected a drawing final pass
on the signature; this rejects it again on behaviour.

## Decision

**Option 2.** A drawing regeneration handler resolves what its object *names*,
and never computes what its object *draws*.

Concretely, the boundary is already a line in `viewGeometry()`
(`src/drawing/Views.cpp`), and the handler stops exactly there:

```text
effectiveSubject -> effectiveSource -> effectiveBasis -> effectiveScale
                 -> effectivePlacement -> bodiesForView     <- references end
                 -> section cut -> hiddenLineDrawing        <- geometry begins
```

This is a **prefix of the same code**, not a second resolver. `P14-STREF-001`
refused to add a parallel resolution path on the grounds that "a second
resolution path would be a second answer to 'where is this', and the two could
differ"; a prefix cannot differ from the whole, because it *is* the whole up to
the cut.

Two functions become public so the prefix can be reached, each calling the
same internal `resolveTarget` its full path calls:

```cpp
Result<void> resolveDimensionTargets(const Document&, DimensionId, const BodyLookup&);
Result<void> resolveAnnotationTarget(const Document&, AnnotationId, const BodyLookup&);
```

A view needs none: `effectiveSubject`, `effectiveSource`, `effectiveBasis`,
`effectiveScale`, `effectivePlacement` and `drawnOccurrences` are public
already.

**One question the prefix has to ask that the full path asks differently.** A
balloon names a component occurrence, and an occurrence resolves through its
*part* — which a configuration does not touch. So `resolveTarget` anchors a
balloon perfectly well on a component the active configuration suppresses;
`draw()` only catches it one step later, by finding no solved transform for it
(`Annotations.cpp:476`), and that step is the one a handler cannot reach.

Cutting at the resolver boundary alone would therefore make a suppressed
balloon *pass* regeneration and fail when drawn — the silence this milestone
exists to end. `resolveAnnotationTarget` asks `assembly::activeComponents()`
instead, which gets the same answer from the **configuration** rather than
from the **solver**, and is the same call `drawnOccurrences()` makes. The rule
holds: the handler asks what the object names, and whether the configuration
places it, and never where the solver put it.

Three consequences follow, and they are the decision as much as the rule is.

**A reference that does not resolve is a failure, not silence.** The handler
returns the resolver's own `Error` unchanged — `NotFound`, `FailedPrecondition`
or `InvalidArgument` — prefixed with the object's label, so
`P14-STREF-001`'s Resolved / Unresolved / Invalid split survives into
`Regenerator::error()`. A balloon whose occurrence this configuration
suppresses **fails**, because the balloon is in force even though its target
is not; that is the opposite of the mate rule
(`src/assembly/Resolution.cpp:218-224`), where a *suppressed mate* is silent
because the mate itself is not in force. The intent is kept either way and
recovers on the next pass, because a `Failed` node is a source of dirtiness
(`src/features/Regenerator.cpp:256-259`).

**A drawing handler never writes.** It is a pure read of the document, so a
failed drawing regeneration commits nothing, by construction rather than by
care — unlike the sketch handler, which calls `modifyObject`.

**A drawing is never the first to notice that the assembly did not solve.**
Position-dependent failures are outside the prefix on purpose, and they are
not lost: a solve that fails puts its error in the report itself
(`Regenerator.cpp:360-364`), and a drawing that is asked for geometry gets the
same diagnostic from `draw()` at that moment. The drawing layer does not
duplicate the assembly's diagnosis.

## Consequences

**Good.** `ADR-014`'s mandate is honoured with no parallel regeneration
system, no new state and no cache. Drawing objects join the dependency graph's
dirty set, so a model edit reports precisely which drawing objects it reaches,
and an unrelated sheet is not touched. Failure atomicity is free, because
there is nothing to commit. Stale drawing geometry remains impossible for the
reason `ADR-014` gave — nothing is stored — and the handler does not
reintroduce a store.

**Bad.** A projection that resolves but fails numerically is not caught during
regeneration; it is caught when the view is drawn. A view can therefore be
`Regenerated` and still fail to produce geometry. This is a deliberate trade
of completeness for correctness and cost, and it is the one place where
"regenerated" means less than it does for a feature.

**Registration.** `drawing::registerHandlers(features::Regenerator&)` is called
by whoever builds a regenerator at layer 4 or above, exactly as
`assembly::registerHandlers` is. `features::validateDocument` builds a bare
`Regenerator` (`src/features/Validation.cpp:634-636`) and so validates neither
assemblies nor drawings; that is pre-existing and is a layering fact, not an
oversight — layer 2 cannot call layer 3 or 4. A drawing-aware CLI check is
`P14-CLI-001`'s.
