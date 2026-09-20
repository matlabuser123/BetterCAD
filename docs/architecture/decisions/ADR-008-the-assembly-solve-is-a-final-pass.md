# ADR-008 — The assembly solve is a final pass, not an object's handler

```text
Status:    Accepted
Date:      2026-09-21
Milestone: P13-REGEN-001
Builds on: ADR-005 (placement is intent, transforms are derived)
           ADR-006 (assembly module and layer)
```

## Context

`features::Regenerator` keeps a document's derived results current. Its unit
of work is **one object**: a handler takes an `ObjectId` and returns that
object's body, and the pass walks the dependency graph in topological order,
rebuilding only what is dirty.

That shape fits everything it has had to do so far. A sketch owns its own
solve, so the "sketch" handler solves it. A feature owns its own body.

An assembly solve does not fit it. It spans **every** active component and
mate at once, and produces transforms keyed by `ComponentId`. There is no
object whose handler it is: picking a component would make that component
special for no modelling reason, and solving once per component would solve
the same system N times.

There is also a layering constraint. `features` is layer 2 and `assembly` is
layer 3, so `Regenerator` cannot call `assembly::solve()`. Whatever runs the
solve has to be injected by the assembly module, the way `registerHandlers()`
already injects the component handler (ADR-006).

And ADR-005 has already decided half the question: a solved transform is
derived state, "held beside the bodies keyed by `ComponentId`, dropped when a
component's regeneration fails exactly as a body is". That says where the
transforms live and how they die. It does not say who triggers the solve.

## Decision

**The assembly solve is a document-level final pass, registered by the
assembly module and run by the regenerator after the object pass.**

```cpp
// features
using FinalPass = std::function<Result<std::map<ComponentId, RigidTransform3D>>(
    Document&, const Regenerator&, RegenerationReport&)>;

void registerFinalPass(std::string name, FinalPass pass);
const RigidTransform3D* transform(ComponentId) const noexcept;
```

The pass returns the transforms and the regenerator stores them beside the
bodies — the same contract a handler already has, raised from one object to
the document. `assembly::registerHandlers()` registers it, so a caller that
already registers the assembly handlers gets regeneration that solves, with
no second call to remember.

This makes the pass structure symmetric, which is the argument that settled
it:

```text
parameters   ->  a document-level phase, before the objects
objects      ->  one handler each, in dependency order
assembly     ->  a document-level phase, after the objects
```

The regenerator already had the first. It now has the third, for the same
reason: a result that belongs to the document rather than to any one object,
and that everything else depends on being computed at the right moment. The
solve must run after the bodies exist, because a face target resolves against
them.

`features` learns nothing about assemblies. `ComponentId` and
`RigidTransform3D` are `core` types, layer 0, which it may already name; it
stores what the pass returned without knowing what a component is, exactly as
it stores a body without knowing what an extrude is.

## Candidates considered

### A. A handler on a designated object — rejected

Give one object's handler the job of solving the whole assembly.

Rejected because no object is the right one. A component is an instance of a
part; it does not own the system it participates in. Whichever were chosen
would acquire a responsibility nothing in the model gives it, and the choice
would have to be re-litigated whenever that object was deleted. Running the
handler for every component instead solves the same system once per
component.

### B. Assembly owns the derived store, updated by the caller — rejected

The assembly module keeps its own map, and whoever drives regeneration calls
a second function afterwards.

Rejected because it makes "regenerate" mean two calls, and every caller —
the CLI, the GUI, every test — has to remember the second one. A caller that
forgets gets stale transforms that look current, which is precisely the
failure this milestone exists to prevent. It also contradicts ADR-005's
"beside the bodies": two stores with two lifetimes, which have to be kept in
step by hand.

### C. A document-level final pass — chosen

As above.

The cost is honest: `Regenerator` gains a second registration mechanism and a
second result map. That is a real increase in its surface, and it is the
price of a derived result that is not an object's.

## Consequences

**The solve runs when what it consumes changed, and not otherwise.** The
trigger is derived from the solve's own inputs rather than from a revision
proxy: the active component set, the active mate set, whether any of them was
rebuilt or broken in this pass, the active configuration, and the resolved
placement of every active component. Each of those is something the solve
actually reads, which is what makes "no re-solve for an unrelated change"
provable rather than hoped for.

The last item closes a gap a revision-based trigger would leave. A
configuration that overrides a *free* parameter changes no object's revision —
the base value is untouched, and only `effectiveParameterValue()` differs — so
nothing downstream would look dirty. Comparing the resolved placements catches
it, because `placementOf()` reads the value in force.

**A broken assembly publishes no transforms at all.** If any active component
or mate failed or was blocked, or the solve does not converge, the pass
returns nothing and the stored transforms are dropped. Not a partial set, and
not the previous pass's: a transform that is one edit out of date is worse
than an absent one, because it renders.

**Suppressed objects are already handled.** The pass asks for
`activeComponents()` and `activeMates()`, so P13-CONF-001's semantics come
through unchanged — and a suppression change moves those sets, which is one of
the triggers.

**What this does not decide:** undo and redo of assembly edits, which is
`P13-CMD-001`, and whether the transforms are ever written to a file — ADR-005
already says they are not.
