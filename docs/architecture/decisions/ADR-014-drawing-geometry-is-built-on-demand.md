# ADR-014 — Drawing objects regenerate; drawing geometry is built on demand

```text
Status:    Accepted
Date:      2026-09-22
Milestone: P14-ARCH-001
Builds on: ADR-008 (the assembly solve is a final pass)
           ADR-011 (drawing intent is canonical)
```

## Context

ADR-008 gave `features::Regenerator` a second injection point. Alongside the
per-object `RegenerationHandler`, a module may register a **final pass** that
runs after every object is built and produces a document-level derived result.
The assembly solve is the one that exists.

A drawing has derived state too, so the obvious move is a second final pass.
Reading the mechanism says otherwise, on two counts.

**The final-pass contract cannot carry a drawing's result.** The signature is
not generic (`include/bettercad/features/Regenerator.hpp:67-68`):

```cpp
using FinalPass = std::function<Result<std::map<ComponentId, RigidTransform3D>>(
    Document& document, const Regenerator& regenerator, RegenerationReport& report)>;
```

and the regenerator has exactly one store for it,
`std::map<ComponentId, RigidTransform3D> transforms_` (`:134`), merged at
`src/features/Regenerator.cpp:366-368`. Projected curves, edge
classifications, section geometry and computed BOM rows have nowhere to go.

**Final passes are ordered alphabetically, and nothing more.** They live in a
`std::map<std::string, FinalPass, std::less<>>` (`Regenerator.hpp:133`) and
run in name order (`Regenerator.cpp:358`). The header says why — "so that two
of them cannot depend on registration order" — but name order is not a
dependency mechanism either. A drawing pass needs the assembly solve to have
run. `"assembly.solve"` sorts before `"drawing.views"` because `a` < `d`. It
would sort *after* a pass called `"annotations"`. Nothing declares the
dependency, nothing validates it, and nothing tests it.

Three further sharp edges were found in the same reading, and they matter to
whichever option is chosen:

- A final pass returning `std::unexpected` does **not** make
  `RegenerationReport::succeeded()` false; the error is filed under a
  default-constructed `ObjectId{}` and the loop continues
  (`Regenerator.cpp:360-365`). Two failing passes collide on that one key and
  the second overwrites the first.
- A final pass can push onto `report.failed`, but cannot update the
  regenerator's private `states_`/`errors_`, so `Regenerator::state(id)` and
  the report would disagree.
- `regenerateAll()` clears the regenerator's maps but **not** a pass's own
  lambda-captured memo (`Regenerator.cpp:216-223` against
  `src/assembly/Resolution.cpp:247`).

## Options

1. **Widen the final-pass contract** to a type-erased, keyed derived store, so
   any module can publish any result.
2. **Register a drawing final pass** that publishes through a caller-owned
   out-parameter, the shape `assembly::registerHandlers`'s
   `AssemblyRegeneration*` already uses.
3. **Do not put drawing geometry in the regenerator at all.** Drawing objects
   get ordinary regeneration *handlers*; the projected geometry is built on
   demand from an already-regenerated document.

## Decision

**Option 3**, in two halves.

**Drawing objects regenerate like every other object.** `Sheet`, `View`,
`Dimension` and `Annotation` each get a `RegenerationHandler` registered by
the drawing module through the existing `registerHandler`. A handler resolves
the object's references and **fails** if one does not resolve; it owns no
geometry, so it returns `std::nullopt` — exactly the shape of the component
and mate handlers (`src/assembly/Resolution.cpp:188-240`). This is not
optional politeness: an object with no handler is silently marked
`UpToDate` and never validated (`src/features/Regenerator.cpp:341-344`),
which is the precise defect `P13-REGEN-001` fixed for mates.

**Drawing geometry is not published by the regenerator.** Projection, hidden
-line classification, section curves, dimension values and BOM rows are
computed on demand by the drawing module, from a document that has already
been regenerated and solved — the shape `io::exportStep` uses today
(`src/io/ModelExport.cpp:57-62`: clone, register handlers, `regenerateAll`,
read the results). Nothing is cached, so nothing can be stale.

## Rationale

Option 1 is the clean-looking answer and the expensive one. ADR-008 already
recorded the price of the mechanism it added — "`features::Regenerator` now
has two registration mechanisms — handlers and final passes — and a second
result map. That is a real increase in its surface". Generalising it to a
type-erased store would be a third, inside qualified infrastructure, to serve
one new consumer. It would also have to solve the ordering problem properly:
a real dependency between passes needs declaration and validation, not a
naming convention. That is a change to `features` that `P14-ARCH-001` has no
mandate for and that no measurement yet justifies.

Option 2 needs no change to `features` and has precedent — but the precedent
carries a **summary** (a trigger, a status, a DOF count, a transform count),
not derived geometry. Stretching it to carry every projected curve in a
drawing makes the out-parameter the real derived store while the official one
stays empty, which is worse than either honest alternative. And it inherits
the ordering problem untouched: correctness resting on `a` < `d`.

Option 3 removes the problem rather than accommodating it. A drawing never
races the assembly solve, because it is not in the same phase as the solve —
it *consumes* a document that has already finished regenerating. The
alphabetical-ordering landmine simply does not apply. It needs no change to
qualified infrastructure. And it is the strict conclusion of ADR-011: if
nothing derived is stored, nothing derived can be stale, which is the property
a drawing needs most.

The cost is real and is stated plainly: **every query re-projects.** Asking
for a view's curves twice does the work twice, and hidden-line removal on a
large assembly is expensive. The existing export path has the same shape and
the same cost — both `exportStep` and `exportStl` run a full `regenerateAll`
on a document clone per call, so exporting both formats regenerates twice.
That is tolerable for an export and may not be for an interactive drawing.

`P14-REGEN-001` owns that measurement. If a cache is needed, this ADR
constrains it in advance:

- it lives in the **drawing** module, not in `features::Regenerator`;
- it is invalidated by comparing its own **resolved inputs**, the way
  `SolveInputs` does (`src/assembly/Resolution.cpp:106-135`), never by object
  revision alone — because a configuration overriding a free parameter changes
  no revision, which is the case `P13-REGEN-001` measured and the case a
  revision-based cache would silently miss;
- it publishes **all or nothing**: a view whose inputs are broken yields no
  geometry rather than its previous geometry, because a projection one edit
  out of date still prints.

## Consequences

- No change to `features::Regenerator`. The final-pass mechanism keeps one
  consumer, and its assembly-specific return type stays honest about what it
  is for.
- Drawing objects participate fully in dirty propagation, ordering, failure
  and blocking, because they are ordinary graph nodes with ordinary handlers.
- A drawing of a broken model fails through the ordinary report — its view
  objects are `failed` or `blocked`, which *does* make `succeeded()` false,
  unlike a final-pass error.
- The four sharp edges above are avoided rather than navigated: no second
  final pass, so no `ObjectId{}` error collision, no state/report divergence,
  no memo surviving `regenerateAll`.
- Building a view is explicit and traceable: a caller regenerates, then asks.
  There is no hidden moment at which a drawing silently became current.
- Interactive performance is an open question with an owner (`P14-REGEN-001`)
  and a constrained solution space, rather than an architectural assumption.

## Rejected alternatives, and what would make them right

**Widening the final-pass contract (Option 1)** becomes right when a *second*
module needs a document-level derived result that is not component
transforms — at which point the generalisation serves two consumers and can
be designed with a real inter-pass dependency declaration instead of name
order. Doing it for one consumer is speculative.

**A drawing final pass through an out-parameter (Option 2)** would be right if
drawing geometry were small and summary-shaped. It is not.

## Verification

For `P14`: a view whose source object fails is itself failed, and
`succeeded()` is false; a dimension whose reference does not resolve fails
rather than regenerating as though all were well — the mate lesson, tested;
building the same view twice from the same document gives bit-identical
geometry; changing a driving parameter changes the view's geometry on the next
build; and a configuration overriding a free parameter — which changes no
object revision — is reflected in the next build.
